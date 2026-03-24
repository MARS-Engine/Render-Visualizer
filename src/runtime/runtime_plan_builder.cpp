#include <render_visualizer/runtime/impl.hpp>

#include <mars/debug/logger.hpp>

namespace rv {
using namespace runtime_detail;

namespace {

mars::log_channel g_app_log_channel("app");

bool is_texture_output_type(size_t _type_hash) {
	return _type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned char>>() ||
		_type_hash == rv::detail::pin_type_hash<rv::resource_tags::texture_slot>();
}

function_step make_function_step(graph_runtime& runtime, const NE_Node& node) {
	function_step step;
	step.node = &node;
	auto append_pins = [&](auto& _bindings, const auto& _pins, auto&& _slot_for) {
		for (const auto& pin : _pins) {
			const int slot_index = _slot_for(pin.label);
			if (slot_index == -1)
				continue;
			_bindings.push_back({
				.pin_id = pin.id,
				.label = pin.label,
				.slot_index = slot_index,
			});
		}
	};

	append_pins(step.inputs, node.inputs, [&](std::string_view _label) { return runtime.input_source_slot(node, _label); });
	append_pins(step.outputs, node.outputs, [&](std::string_view _label) { return runtime.output_slot(node, _label); });
	if (const NodeTypeInfo* info = runtime.registry.find(node.type); info && info->meta.is_function_outputs)
		append_pins(step.inputs, node.inputs, [&](std::string_view _label) { return runtime.output_slot(node, _label); });
	return step;
}

}

const framebuffer_attachment_resources* graph_runtime::resolve_texture_source_from_node(const NE_Node& node) const {
	if (node.type == nodes::node_type_v<nodes::present_texture_node_tag>) {
		std::string error;
		return read_input_resource<framebuffer_attachment_resources>(node, "texture", error);
	}
	if (node.type == nodes::node_type_v<nodes::framebuffer_node_tag>) {
		for (const auto& output : node.outputs) {
			if (output.kind != NE_PinKind::data || !is_texture_output_type(output.type_hash))
				continue;
			std::string error;
			if (auto* attachment = read_output_resource<framebuffer_attachment_resources>(node, output.label, error); attachment != nullptr)
				return attachment;
		}
	}
	return nullptr;
}

const framebuffer_attachment_resources* graph_runtime::find_present_source_resources(const NE_Node& end_node) const {
	std::string error;
	return read_input_resource<framebuffer_attachment_resources>(end_node, "color", error);
}

const framebuffer_attachment_resources* graph_runtime::find_present_source_resources() const {
	const NE_Node* end_node = root_node();
	if (end_node == nullptr)
		return nullptr;
	return find_present_source_resources(*end_node);
}

bool runtime_detail::graph_builder::compile(const mars::vector2<size_t>&, std::string& error) {
	if (owner == nullptr) {
		error = "Runtime builder is not attached.";
		return false;
	}

	nodes::refresh_dynamic_nodes(owner->graph);
	owner->plan.root_node_id = -1;
	owner->plan.build_steps.clear();
	owner->plan.function_plans.clear();
	owner->steps.clear();

	const NE_Node* setup_start = owner->function_start_node(owner->graph.setup_function_id());
	const NE_Node* render_start = owner->function_start_node(owner->graph.render_function_id());
	if (setup_start == nullptr || render_start == nullptr) {
		error = "Both setup and render functions must have their mandatory start nodes.";
		return false;
	}

	const auto has_swapchain_node = [&](int _function_id) {
		return std::ranges::any_of(owner->graph.nodes_in_function(_function_id), [&](const NE_Node* _node) {
			const NodeTypeInfo* info = owner->registry.find(_node->type);
			return info != nullptr && info->meta.is_end;
		});
	};
	if (has_swapchain_node(owner->graph.setup_function_id())) {
		error = "Setup cannot contain a Swapchain node.";
		return false;
	}

	for (const auto& function : owner->graph.functions) {
		if (owner->graph.is_builtin_function(function.id))
			continue;
		if (has_swapchain_node(function.id)) {
			error = "Custom functions cannot contain a Swapchain node.";
			return false;
		}
	}

	std::vector<int> ordered_nodes;
	int root_node_id = -1;
	std::unordered_set<int> visited;
	std::unordered_map<int, bool> reaches_end_cache;
	if (!owner->build_order_from_start(setup_start->id, visited, reaches_end_cache, ordered_nodes, root_node_id)) {
		mars::logger::warning(
			g_app_log_channel,
			"Setup chain starting at '{}' does not reach an end node. This is allowed.",
			setup_start->title
		);
	}
	if (!owner->build_order_from_start(render_start->id, visited, reaches_end_cache, ordered_nodes, root_node_id)) {
		error = "Render must reach a Swapchain node.";
		return false;
	}

	owner->plan.root_node_id = root_node_id;

	if (!owner->compile_function_layouts(error)) {
		if (error.empty())
			error = "Failed to compile function stack frames.";
		return false;
	}

	const auto is_build_relevant = [&](const NodeTypeInfo& info) {
		return info.meta.has_processor || info.hooks.build_execute || info.meta.is_end;
	};

	const auto schedule_build_node = [&](NE_Node& node, std::unordered_set<int>& built_nodes) {
		if (!built_nodes.insert(node.id).second)
			return;
		const NodeTypeInfo* info = owner->registry.find(node.type);
		if (info == nullptr)
			return;
		const char* kind = "Node";
		if (info->hooks.build_propagate)
			kind = "Variable";
		else if (info->meta.has_processor)
			kind = "Processor";
		else if (info->meta.is_end)
			kind = "Swapchain";
		else if (info->meta.is_vm_event)
			kind = "Event";
		else if (info->meta.is_vm_callable)
			kind = "Callable";
		else if (info->meta.is_vm_pure)
			kind = "Pure";
		else if (info->meta.is_start)
			kind = "Entry";

		owner->plan.build_steps.push_back(make_function_step(*owner, node));
		owner->steps.push_back({
			.node_id = node.id,
			.label = node.title,
			.kind = kind,
			.status = "Compiled",
			.executed_count = 0,
			.valid = true,
		});
	};

	std::unordered_set<int> built_nodes;
	std::unordered_set<int> building_nodes;
	std::function<void(NE_Node&)> build_dependencies = [&](NE_Node& node) {
		if (!building_nodes.insert(node.id).second)
			return;

		for (const auto& pin : node.inputs) {
			const NE_Link* link = owner->find_input_link(node, pin.label);
			if (link == nullptr)
				continue;
			NE_Node* source_node = owner->graph.find_node(link->from_node);
			if (source_node == nullptr || source_node->function_id != node.function_id)
				continue;

			build_dependencies(*source_node);

			const NodeTypeInfo* source_info = owner->registry.find(source_node->type);
			if (source_info != nullptr && is_build_relevant(*source_info))
				schedule_build_node(*source_node, built_nodes);
		}

		building_nodes.erase(node.id);
	};

	for (int node_id : ordered_nodes) {
		NE_Node* node = owner->graph.find_node(node_id);
		const NodeTypeInfo* info = node == nullptr ? nullptr : owner->registry.find(node->type);
		if (node == nullptr || info == nullptr)
			continue;
		build_dependencies(*node);
		schedule_build_node(*node, built_nodes);
	}

	for (const auto& function : owner->graph.functions) {
		const NE_Node* start_node = owner->function_start_node(function.id);
		if (start_node == nullptr)
			continue;

		auto& fplan = owner->plan.function_plans[function.id];
		fplan.start_node = start_node;
		if (const NE_Node* outputs_node = owner->function_outputs_node(function.id); outputs_node != nullptr)
			fplan.outputs_node = outputs_node;

		std::unordered_set<int> visited_nodes;
		std::function<void(const NE_Node&)> visit_step = [&](const NE_Node& node) {
			if (!visited_nodes.insert(node.id).second)
				return;
			const NodeTypeInfo* info = owner->registry.find(node.type);
			if (info == nullptr)
				return;

			fplan.step_index_by_node_id[node.id] = static_cast<int>(fplan.steps.size());
			fplan.steps.push_back(make_function_step(*owner, node));
			for (const NE_Link* link : owner->outgoing_exec_links(node)) {
				if (link == nullptr)
					continue;
				const NE_Node* next_node = owner->graph.find_node(link->to_node);
				if (next_node == nullptr || next_node->function_id != function.id)
					continue;
				visit_step(*next_node);
			}
		};

		visit_step(*start_node);
		for (const NE_Node* node : owner->graph.nodes_in_function(function.id)) {
			const NodeTypeInfo* info = owner->registry.find(node->type);
			if (info != nullptr && info->meta.is_vm_event)
				visit_step(*node);
		}

		const auto root_it = fplan.step_index_by_node_id.find(start_node->id);
		fplan.entry_step_index = root_it == fplan.step_index_by_node_id.end() ? -1 : root_it->second;

		for (auto& step : fplan.steps) {
			const NE_Node* node = step.node;
			if (node == nullptr)
				continue;
			for (const NE_Link* link : owner->outgoing_exec_links(*node)) {
				if (link == nullptr)
					continue;
				const auto next_it = fplan.step_index_by_node_id.find(link->to_node);
				if (next_it == fplan.step_index_by_node_id.end())
					continue;
				step.next_steps.push_back({
					.exec_pin_id = link->from_pin,
					.next_step_index = next_it->second,
				});
			}
		}
	}

	error.clear();
	return true;
}

void graph_runtime::tick(float delta_time) {
	frame_delta_time = delta_time;
	blackboard.delta_time = delta_time;
	blackboard.time += delta_time;
	if (!running || has_error)
		return;

	dispatch_vm_event_type(nodes::function_node_type_id<^^nodes::Tick>());
	for (size_t event_type : pending_vm_event_types) {
		if (has_error)
			break;
		dispatch_vm_event_type(event_type);
	}
	pending_vm_event_types.clear();
	if (has_error)
		return;

	for (auto& node : graph.nodes) {
		const NodeTypeInfo* info = registry.find(node.type);
		if (info == nullptr || !info->hooks.on_tick)
			continue;
		info->hooks.on_tick(node, frame_delta_time);
		clear_vm_outputs(node.id);
	}
}

void graph_runtime::record_preview(mars::raster_scope<main_pass_desc>& scope, const mars::vector2<size_t>& frame_size) {
	if (!running || has_error)
		return;

	const framebuffer_attachment_resources* resources = find_present_source_resources();
	if (resources == nullptr)
		return;
	if (resources->owner == nullptr || resources->attachment_index >= resources->owner->targets.size())
		return;

	present_resources* present = ensure_present_resources(scope.get_render_pass());
	if (present == nullptr)
		return;

	mars::graphics::pipeline_bind(present->pipeline, scope.get(), { .size = frame_size });
	const present_push_constants push_constants = {
		.source_texture_index = mars::graphics::texture_get_srv_index(resources->owner->targets[resources->attachment_index])
	};
	scope.set_push_constants(present->pipeline, push_constants);
	mars::graphics::command_buffer_draw(scope.get(), {
		.vertex_count = 3,
		.instance_count = 1,
		.first_vertex = 0,
		.first_instance = 0
	});
}

} // namespace rv
