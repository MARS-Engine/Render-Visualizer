#include <render_visualizer/nodes/function_nodes.hpp>

#include <mars/debug/logger.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace rv::nodes {

namespace {

using local_numeric_types = std::tuple<
	float,
	mars::vector2<float>,
	mars::vector3<float>,
	mars::vector4<float>,
	unsigned int,
	mars::vector2<unsigned int>,
	mars::vector3<unsigned int>,
	mars::vector4<unsigned int>
>;

mars::log_channel g_app_log_channel("app");

bool wildcard_type_is_resource(size_t type_hash) {
	return type_hash == rv::detail::pin_type_hash<rv::resource_tags::vertex_buffer>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::index_buffer>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::uniform_resource>() ||
		type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned char>>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::texture_slot>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::shader_module>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::render_pass>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::framebuffer>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::depth_buffer>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::graphics_pipeline>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::material_resource>() ||
		type_hash == rv::detail::pin_type_hash<rv::resource_tags::virtual_struct_schema>();
}

using graph_blackboard_t = rv::graph_blackboard;

const blackboard_field_descriptor* find_selected_blackboard_field(int field_index) {
	const blackboard_field_descriptor* descriptor_ptr = nullptr;
	visit_blackboard_field<graph_blackboard_t>(field_index, [&]<typename>(const auto& descriptor) {
		descriptor_ptr = &descriptor;
	});
	return descriptor_ptr;
}

template <typename Fn>
bool render_blackboard_field_selector(int& field_index, Fn&& on_changed) {
	const blackboard_field_descriptor* current_descriptor = find_selected_blackboard_field(field_index);
	const char* preview = current_descriptor != nullptr ? current_descriptor->label.data() : "Select field";
	bool changed = false;
	if (ImGui::BeginCombo("Field", preview)) {
		template for (constexpr size_t index : std::views::iota(size_t { 0 }, blackboard_field_descriptors_v<graph_blackboard_t>.size())) {
			constexpr auto descriptor = blackboard_field_descriptors_v<graph_blackboard_t>[index];
			if constexpr (descriptor.label != std::string_view("Window Close Requested")) {
				const bool selected = field_index == static_cast<int>(index);
				if (ImGui::Selectable(descriptor.label.data(), selected)) {
					field_index = static_cast<int>(index);
					on_changed();
					changed = true;
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}

}

bool is_addable_wildcard_type(const wildcard_type_info& type) {
	if (wildcard_type_is_resource(type.type_hash) ||
		type.is_container ||
		type.has_virtual_struct) {
		return false;
	}

	bool supported = false;
	rv::detail::dispatch_wildcard_type<local_numeric_types>(type, [&]<typename>() {
		supported = true;
	});
	return supported;
}

bool add_wildcard_values(const wildcard_value_view& lhs, const wildcard_value_view& rhs, wildcard_value& out_value) {
	if (!wildcard_types_equal(lhs.type, rhs.type))
		return false;

	return rv::detail::dispatch_wildcard_type<local_numeric_types>(lhs.type, [&]<typename T>() {
		out_value = make_wildcard_value(lhs.as<T>() + rhs.as<T>());
	});
}

const NE_Pin* find_linked_wildcard_source_pin(const NodeGraph& graph, const NE_Node& node, std::string_view label, bool input_pin) {
	for (const auto& link : graph.links) {
		if (input_pin) {
			if (link.to_node != node.id)
				continue;
			const NE_Pin* node_pin = nodes::find_pin_by_id(node.inputs, link.to_pin);
			if (node_pin == nullptr || node_pin->label != label)
				continue;
			if (const NE_Node* other = graph.find_node(link.from_node))
				return nodes::find_pin_by_id(other->outputs, link.from_pin);
		} else {
			if (link.from_node != node.id)
				continue;
			const NE_Pin* node_pin = nodes::find_pin_by_id(node.outputs, link.from_pin);
			if (node_pin == nullptr || node_pin->label != label)
				continue;
			if (const NE_Node* other = graph.find_node(link.to_node))
				return nodes::find_pin_by_id(other->inputs, link.to_pin);
		}
	}
	return nullptr;
}

bool try_resolve_wildcard_group(
	const NodeGraph& graph,
	const NE_Node& node,
	const std::vector<NE_Pin>& wildcard_inputs,
	const std::vector<NE_Pin>& wildcard_outputs,
	std::string_view group,
	wildcard_type_info& out_type
) {
	struct ordered_pin {
		const NE_Pin* template_pin = nullptr;
		bool input_pin = true;
	};
	std::vector<ordered_pin> ordered;
	for (const auto& pin : wildcard_inputs)
		if (pin.wildcard_group == group)
			ordered.push_back({ .template_pin = &pin, .input_pin = true });
	for (const auto& pin : wildcard_outputs)
		if (pin.wildcard_group == group)
			ordered.push_back({ .template_pin = &pin, .input_pin = false });
	std::ranges::sort(ordered, [](const ordered_pin& lhs, const ordered_pin& rhs) {
		return lhs.template_pin->id < rhs.template_pin->id;
	});

	for (const auto& candidate : ordered) {
		if (const NE_Pin* linked_pin = find_linked_wildcard_source_pin(graph, node, candidate.template_pin->label, candidate.input_pin); linked_pin != nullptr) {
			if (!linked_pin->is_wildcard || linked_pin->wildcard_resolved) {
				out_type = wildcard_type_from_pin(*linked_pin);
				return out_type.valid();
			}
		}

		if (candidate.input_pin) {
			const NE_Pin* current_pin = nodes::find_pin_by_label(node.inputs, candidate.template_pin->label);
			const NE_InlineInputValue* inline_value = nullptr;
			for (const auto& value : node.inline_input_values) {
				if (value.label == candidate.template_pin->label && value.enabled) {
					inline_value = &value;
					break;
				}
			}
			if (inline_value != nullptr && current_pin != nullptr && current_pin->type_hash != 0) {
				out_type = wildcard_type_from_pin(*current_pin);
				return out_type.valid();
			}
		}
	}

	return false;
}

void refresh_wildcard_function_node(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node) {
	if (info.pins.wildcard_input_templates.empty() && info.pins.wildcard_output_templates.empty())
		return;

	std::unordered_map<std::string, wildcard_type_info> resolved_groups;
	for (const auto& pin : info.pins.wildcard_input_templates) {
		if (!pin.is_wildcard || resolved_groups.contains(pin.wildcard_group))
			continue;
		wildcard_type_info resolved;
		if (try_resolve_wildcard_group(graph, node, info.pins.wildcard_input_templates, info.pins.wildcard_output_templates, pin.wildcard_group, resolved))
			resolved_groups.emplace(pin.wildcard_group, std::move(resolved));
	}
	for (const auto& pin : info.pins.wildcard_output_templates) {
		if (!pin.is_wildcard || resolved_groups.contains(pin.wildcard_group))
			continue;
		wildcard_type_info resolved;
		if (try_resolve_wildcard_group(graph, node, info.pins.wildcard_input_templates, info.pins.wildcard_output_templates, pin.wildcard_group, resolved))
			resolved_groups.emplace(pin.wildcard_group, std::move(resolved));
	}

	std::vector<NE_Pin> generated_inputs;
	std::vector<NE_Pin> generated_outputs;
	for (const auto& template_pin : info.pins.wildcard_input_templates) {
		NE_Pin pin = template_pin;
		if (const auto it = resolved_groups.find(pin.wildcard_group); it != resolved_groups.end()) {
			apply_wildcard_type_to_pin(pin, it->second);
		} else {
			pin.type_hash = 0;
			pin.is_container = false;
			pin.has_virtual_struct = false;
			pin.virtual_struct_name.clear();
			pin.virtual_struct_layout_fingerprint = 0;
			pin.wildcard_resolved = false;
		}
		generated_inputs.push_back(std::move(pin));
	}
	for (const auto& template_pin : info.pins.wildcard_output_templates) {
		NE_Pin pin = template_pin;
		if (const auto it = resolved_groups.find(pin.wildcard_group); it != resolved_groups.end()) {
			apply_wildcard_type_to_pin(pin, it->second);
		} else {
			pin.type_hash = 0;
			pin.is_container = false;
			pin.has_virtual_struct = false;
			pin.virtual_struct_name.clear();
			pin.virtual_struct_layout_fingerprint = 0;
			pin.wildcard_resolved = false;
		}
		generated_outputs.push_back(std::move(pin));
	}

	if (equivalent_pin_layout(node.generated_inputs, generated_inputs) &&
		equivalent_pin_layout(node.generated_outputs, generated_outputs))
		return;
	graph.replace_generated_pins(node.id, std::move(generated_inputs), std::move(generated_outputs));
}

void sync_blackboard_get_node(NodeGraph& graph, NE_Node& node) {
	std::vector<NE_Pin> generated_outputs;
	std::string title = "Blackboard Get";
	if (node.custom_state.storage != nullptr) {
		const auto& state = node.custom_state.as<blackboard_node_state>();
		if (const auto* descriptor = find_selected_blackboard_field(state.field_index); descriptor != nullptr) {
			generated_outputs.push_back(make_reflected_pin(*descriptor));
			title = "Get " + std::string(descriptor->label);
		}
	}
	node.title = std::move(title);
	if (equivalent_pin_layout(node.generated_outputs, generated_outputs))
		return;
	graph.replace_generated_pins(node.id, {}, std::move(generated_outputs));
}

void sync_blackboard_set_node(NodeGraph& graph, NE_Node& node) {
	std::vector<NE_Pin> generated_inputs;
	std::string title = "Blackboard Set";
	if (node.custom_state.storage != nullptr) {
		const auto& state = node.custom_state.as<blackboard_node_state>();
		if (const auto* descriptor = find_selected_blackboard_field(state.field_index); descriptor != nullptr) {
			generated_inputs.push_back(make_reflected_pin(*descriptor));
			title = "Set " + std::string(descriptor->label);
		}
	}
	node.title = std::move(title);
	if (equivalent_pin_layout(node.generated_inputs, generated_inputs))
		return;
	graph.replace_generated_pins(node.id, std::move(generated_inputs), {});
}

void blackboard_get_node::configure(NodeTypeInfo& info) {
	info.meta.vm_reexecute_each_tick = true;
}

void blackboard_get_node::edit(NodeGraph& graph, NE_Node& node, blackboard_node_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::Separator();
	render_blackboard_field_selector(state.field_index, [&] {
		sync_blackboard_get_node(graph, node);
		graph.notify_graph_dirty();
	});
}

void blackboard_get_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_blackboard_get_node(graph, node);
}

bool blackboard_get_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (node.custom_state.storage == nullptr) {
		error = "Blackboard Get is missing its state.";
		return false;
	}

	bool ok = false;
	const auto& state = node.custom_state.as<blackboard_node_state>();
	if (!visit_blackboard_field<graph_blackboard_t>(state.field_index, [&]<typename value_t>(const auto& descriptor) {
		value_t value {};
		if (!ctx.read_blackboard<value_t>(descriptor.key, value, error))
			return;
		ctx.set_output(node, descriptor.label, value);
		ok = true;
	})) {
		error = "Blackboard Get has no selected field.";
		return false;
	}

	return ok;
}

bool blackboard_get_node::validate(const NodeGraph&, const NodeTypeInfo&, const NE_Node& node, std::string& error) {
	if (node.custom_state.storage == nullptr) {
		error = "Blackboard Get is missing its state.";
		return false;
	}
	if (find_selected_blackboard_field(node.custom_state.as<blackboard_node_state>().field_index) == nullptr) {
		error = "Blackboard Get has no field selected.";
		return false;
	}
	error.clear();
	return true;
}

void blackboard_set_node::configure(NodeTypeInfo&) {
}

void blackboard_set_node::edit(NodeGraph& graph, NE_Node& node, blackboard_node_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::Separator();
	render_blackboard_field_selector(state.field_index, [&] {
		sync_blackboard_set_node(graph, node);
		graph.notify_graph_dirty();
	});
}

void blackboard_set_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_blackboard_set_node(graph, node);
}

bool blackboard_set_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (node.custom_state.storage == nullptr) {
		error = "Blackboard Set is missing its state.";
		return false;
	}

	bool ok = false;
	const auto& state = node.custom_state.as<blackboard_node_state>();
	if (!visit_blackboard_field<graph_blackboard_t>(state.field_index, [&]<typename value_t>(const auto& descriptor) {
		value_t value {};
		if (!ctx.resolve_input<value_t>(node, descriptor.label, value, error))
			return;
		ok = ctx.write_blackboard<value_t>(descriptor.key, value, error);
	})) {
		error = "Blackboard Set has no selected field.";
		return false;
	}

	return ok;
}

bool blackboard_set_node::validate(const NodeGraph&, const NodeTypeInfo&, const NE_Node& node, std::string& error) {
	if (node.custom_state.storage == nullptr) {
		error = "Blackboard Set is missing its state.";
		return false;
	}
	if (find_selected_blackboard_field(node.custom_state.as<blackboard_node_state>().field_index) == nullptr) {
		error = "Blackboard Set has no field selected.";
		return false;
	}
	error.clear();
	return true;
}

void Tick(float delta_time, float time) {
	(void)delta_time;
	(void)time;
}

void RightMouseClick(bool right_mouse_clicked) {
	(void)right_mouse_clicked;
}

void WindowResize(mars::vector2<size_t> window_size) {
	(void)window_size;
}

void WindowClose(bool window_close_requested) {
	(void)window_close_requested;
}

void Add(
	wildcard_value_view lhs,
	wildcard_value_view rhs,
	wildcard_value& result
) {
	add_wildcard_values(lhs, rhs, result);
}

float Multiply(float lhs, float rhs) {
	return lhs * rhs;
}

float Sine(float angle) {
	return std::sin(angle);
}

mars::vector2<float> MakeFloat2(float x, float y) {
	return { x, y };
}

void LogFloat(float value) {
	mars::logger::warning(g_app_log_channel, "VM LogFloat: {}", value);
}

mars::vector3<float> MakeFloat3(float x, float y, float z) {
	return { x, y, z };
}

const NodeRegistry::node_auto_registrar tick_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^Tick>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^Tick>(); }
	}
);
const NodeRegistry::node_auto_registrar right_mouse_click_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^RightMouseClick>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^RightMouseClick>(); }
	}
);
const NodeRegistry::node_auto_registrar window_resize_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^WindowResize>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^WindowResize>(); }
	}
);
const NodeRegistry::node_auto_registrar window_close_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^WindowClose>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^WindowClose>(); }
	}
);
const NodeRegistry::node_auto_registrar add_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^Add>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^Add>(); }
	}
);
const NodeRegistry::node_auto_registrar multiply_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^Multiply>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^Multiply>(); }
	}
);
const NodeRegistry::node_auto_registrar sine_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^Sine>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^Sine>(); }
	}
);
const NodeRegistry::node_auto_registrar make_float2_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^MakeFloat2>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^MakeFloat2>(); }
	}
);
const NodeRegistry::node_auto_registrar make_float3_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^MakeFloat3>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^MakeFloat3>(); }
	}
);
const NodeRegistry::node_auto_registrar log_float_function_registration(
	NodeRegistry::registration_descriptor{
		.type = function_node_type_id<^^LogFloat>(),
		.apply = [](NodeRegistry& registry) { registry.template register_function<^^LogFloat>(); }
	}
);

} // namespace rv::nodes
