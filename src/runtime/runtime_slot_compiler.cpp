#include <render_visualizer/runtime/impl.hpp>

#include <format>

namespace rv {
using namespace runtime_detail;

namespace {

template <typename T>
inline constexpr bool is_runtime_resource_type_v =
	rv::runtime_detail::is_in_resource_type_map_v<T> ||
	std::is_same_v<T, rv::resource_tags::virtual_struct_schema>;

slot_type_info make_slot_type_info(size_t _type_hash, const NodeRegistry* registry) {
	slot_type_info info;
	info.type_hash = _type_hash;
	if (registry) {
		if (const pin_type_info* pt = registry->pin_type_for_element(_type_hash, false)) {
			info.reflected_name = pt->name ? pt->name : "";
			info.value_size = pt->element_size;
		}
	}
	return info;
}

stack_slot_flags make_stack_slot_flags(
	size_t _type_hash,
	bool _is_container,
	bool _has_virtual_struct,
	bool _persistent_global,
	const NodeRegistry* registry
) {
	stack_slot_flags flags = stack_slot_flags::none;
	if (_is_container)
		flags |= stack_slot_flags::container;
	if (_has_virtual_struct)
		flags |= stack_slot_flags::virtual_struct;
	if (_persistent_global)
		flags |= stack_slot_flags::persistent_global;
	if (registry) {
		if (const pin_type_info* pt = registry->pin_type(_type_hash); pt && pt->is_resource)
			flags |= stack_slot_flags::resource;
	}
	return flags;
}

stack_slot make_stack_slot(
	size_t _type_hash,
	bool _is_container,
	bool _has_virtual_struct,
	std::string _virtual_struct_name,
	size_t _virtual_struct_layout_fingerprint,
	bool _persistent_global,
	const NodeRegistry* registry
) {
	stack_slot slot;
	slot.type = make_slot_type_info(_type_hash, registry);
	slot.flags = make_stack_slot_flags(_type_hash, _is_container, _has_virtual_struct, _persistent_global, registry);
	slot.virtual_struct_name = std::move(_virtual_struct_name);
	slot.virtual_struct_layout_fingerprint = _virtual_struct_layout_fingerprint;
	return slot;
}

bool output_has_consumer(const NodeGraph& _graph, const NE_Node& _node, const NE_Pin& _output_pin) {
	for (const auto& link : _graph.links) {
		const NE_Node* from_node = _graph.find_node(link.from_node);
		const NE_Node* to_node = _graph.find_node(link.to_node);
		if (from_node == nullptr || to_node == nullptr || from_node->function_id != _node.function_id || to_node->function_id != _node.function_id)
			continue;
		if (link.from_node == _node.id && link.from_pin == _output_pin.id)
			return true;
	}
	return false;
}

} // namespace

pin_key graph_runtime::make_pin_key(int node_id, int pin_id) {
	return (static_cast<pin_key>(static_cast<std::uint32_t>(node_id)) << 32u) |
		static_cast<pin_key>(static_cast<std::uint32_t>(pin_id));
}

const vm_stack* graph_runtime::stack_for_function(int function_id) const {
	return plan.stack.functions.contains(function_id) ? &plan.stack : nullptr;
}

const runtime_detail::function_plan* graph_runtime::function_plan(int function_id) const {
	const auto it = plan.function_plans.find(function_id);
	return it == plan.function_plans.end() ? nullptr : &it->second;
}

const runtime_detail::function_step* graph_runtime::function_step_for_node(const NE_Node& node) const {
	const runtime_detail::function_plan* fplan = function_plan(node.function_id);
	if (fplan == nullptr)
		return nullptr;
	const auto it = fplan->step_index_by_node_id.find(node.id);
	if (it == fplan->step_index_by_node_id.end())
		return nullptr;
	if (it->second < 0 || static_cast<size_t>(it->second) >= fplan->steps.size())
		return nullptr;
	return &fplan->steps[static_cast<size_t>(it->second)];
}

int graph_runtime::global_slot(int variable_id) const {
	const auto it = plan.stack.global_slot_by_variable_id.find(variable_id);
	return it == plan.stack.global_slot_by_variable_id.end() ? -1 : it->second;
}

int graph_runtime::input_source_slot(const NE_Node& node, std::string_view input_label) const {
	const vm_stack* stack = stack_for_function(node.function_id);
	if (stack == nullptr)
		return -1;
	const NE_Pin* pin = nodes::find_pin_by_label(node.inputs, input_label);
	if (pin == nullptr)
		return -1;
	const auto it = stack->input_source_slot_by_pin.find(make_pin_key(node.id, pin->id));
	return it == stack->input_source_slot_by_pin.end() ? -1 : it->second;
}

int graph_runtime::output_slot(const NE_Node& node, std::string_view output_label) const {
	const vm_stack* stack = stack_for_function(node.function_id);
	if (stack == nullptr)
		return -1;
	const NE_Pin* output_pin = nodes::find_pin_by_label(node.outputs, output_label);
	if (output_pin != nullptr) {
		const auto it = stack->output_slot_by_pin.find(make_pin_key(node.id, output_pin->id));
		return it == stack->output_slot_by_pin.end() ? -1 : it->second;
	}
	const NE_Pin* input_pin = nodes::find_pin_by_label(node.inputs, output_label);
	if (input_pin == nullptr)
		return -1;
	const auto it = stack->output_slot_by_pin.find(make_pin_key(node.id, input_pin->id));
	return it == stack->output_slot_by_pin.end() ? -1 : it->second;
}

const stack_slot* graph_runtime::stack_slot_meta(const vm_stack& stack, int slot_index) const {
	if (slot_index < 0 || static_cast<size_t>(slot_index) >= stack.slots.size())
		return nullptr;
	return &stack.slots[static_cast<size_t>(slot_index)];
}

const slot_route* graph_runtime::slot_route_meta(const vm_stack& stack, int slot_index) const {
	const auto it = stack.routes_by_slot.find(slot_index);
	return it == stack.routes_by_slot.end() ? nullptr : &it->second;
}

bool graph_runtime::compile_function_layouts(std::string& error) {
	plan.stack = {};
	frames_by_function.clear();
	global_slot_values.clear();

	std::vector<const nodes::variable_slot_state*> sorted_globals;
	sorted_globals.reserve(graph.variable_slots.size());
	for (const auto& slot : graph.variable_slots)
		sorted_globals.push_back(&slot);
	std::ranges::sort(sorted_globals, {}, [](const nodes::variable_slot_state* _slot) {
		return _slot->id;
	});

	for (const auto& function : graph.functions)
		plan.stack.functions.try_emplace(function.id);

	for (const auto* slot : sorted_globals) {
		const int slot_index = static_cast<int>(plan.stack.slots.size());
		plan.stack.slots.push_back(make_stack_slot(
			slot->type_hash,
			slot->is_container,
			slot->has_virtual_struct,
			slot->virtual_struct_name,
			slot->virtual_struct_layout_fingerprint,
			true,
			graph.node_registry()
		));
		plan.stack.global_slot_by_variable_id[slot->id] = slot_index;
		plan.stack.routes_by_slot[slot_index] = {
			.kind = slot_route_kind::global_variable,
			.variable_id = slot->id,
			.label = slot->name,
		};
	}
	plan.stack.global_slot_count = plan.stack.slots.size();

	const auto append_owned_slot = [&](int _function_id, stack_slot _slot, slot_route _route) {
		const int slot_index = static_cast<int>(plan.stack.slots.size());
		plan.stack.slots.push_back(std::move(_slot));
		plan.stack.routes_by_slot[slot_index] = std::move(_route);
		plan.stack.functions[_function_id].owned_slots.push_back(slot_index);
		return slot_index;
	};

	const auto ensure_output_slot = [&](const NE_Node& _source_node, const NE_Pin& _source_pin) -> int {
		const pin_key key = make_pin_key(_source_node.id, _source_pin.id);
		if (const auto it = plan.stack.output_slot_by_pin.find(key); it != plan.stack.output_slot_by_pin.end())
			return it->second;

		const int slot_index = append_owned_slot(
			_source_node.function_id,
			make_stack_slot(
				_source_pin.type_hash,
				_source_pin.is_container,
				_source_pin.has_virtual_struct,
				_source_pin.virtual_struct_name,
				_source_pin.virtual_struct_layout_fingerprint,
				false,
				graph.node_registry()
			),
			{
				.kind = slot_route_kind::node_output,
				.node_id = _source_node.id,
				.pin_id = _source_pin.id,
				.label = _source_pin.label,
			}
		);
		plan.stack.output_slot_by_pin[key] = slot_index;
		return slot_index;
	};

	for (const auto& function : graph.functions) {
		if (graph.is_builtin_function(function.id))
			continue;

		const NE_Node* inputs_node = function_start_node(function.id);
		const NE_Node* outputs_node = function_outputs_node(function.id);
		if (inputs_node == nullptr || outputs_node == nullptr) {
			error = "Custom function '" + function.name + "' is missing its Function Inputs or Function Outputs node.";
			return false;
		}

		auto& function_view = plan.stack.functions[function.id];

		for (const auto& pin : inputs_node->outputs) {
			const int slot_index = append_owned_slot(
				function.id,
				make_stack_slot(
					pin.type_hash,
					pin.is_container,
					pin.has_virtual_struct,
					pin.virtual_struct_name,
					pin.virtual_struct_layout_fingerprint,
					false,
					graph.node_registry()
				),
				{
					.kind = slot_route_kind::function_input,
					.node_id = inputs_node->id,
					.pin_id = pin.id,
					.label = pin.label,
				}
			);
			plan.stack.output_slot_by_pin[make_pin_key(inputs_node->id, pin.id)] = slot_index;
			function_view.input_slots_by_label[pin.label] = slot_index;
			++function_view.input_slot_count;
		}

		for (const auto& pin : outputs_node->inputs) {
			const int slot_index = append_owned_slot(
				function.id,
				make_stack_slot(
					pin.type_hash,
					pin.is_container,
					pin.has_virtual_struct,
					pin.virtual_struct_name,
					pin.virtual_struct_layout_fingerprint,
					false,
					graph.node_registry()
				),
				{
					.kind = slot_route_kind::function_output,
					.node_id = outputs_node->id,
					.pin_id = pin.id,
					.label = pin.label,
				}
			);
			plan.stack.output_slot_by_pin[make_pin_key(outputs_node->id, pin.id)] = slot_index;
			function_view.output_slots_by_label[pin.label] = slot_index;
			++function_view.output_slot_count;
		}
	}

	const auto is_data_only_producer = [&](const NE_Node& node) {
		const NodeTypeInfo* info = registry.find(node.type);
		if (info == nullptr)
			return false;
		return info->meta.pin_flow == enum_type::none && !info->meta.has_processor && !info->meta.is_start && !info->meta.is_end;
	};

	std::function<bool(const NE_Node&, const std::unordered_set<int>&, std::unordered_set<int>&, std::unordered_set<int>&, std::string&)> compile_data_node;
	compile_data_node = [&](const NE_Node& node, const std::unordered_set<int>& available_slots, std::unordered_set<int>& available_out, std::unordered_set<int>& recursion_stack, std::string& compile_error) -> bool {
		if (!recursion_stack.insert(node.id).second) {
			compile_error = "Data dependency cycle detected while compiling the VM stack.";
			return false;
		}

		std::unordered_set<int> visible_slots = available_slots;
		for (const auto& input_pin : node.inputs) {
			const NE_Link* link = find_input_link(node, input_pin.label);
			if (link == nullptr)
				continue;
			const NE_Node* source_node = graph.find_node(link->from_node);
			if (source_node == nullptr) {
				compile_error = "Input '" + input_pin.label + "' on node '" + node.title + "' has a missing source node.";
				recursion_stack.erase(node.id);
				return false;
			}

			int source_slot = -1;
			if (source_node->type == nodes::node_type_v<nodes::variable_get_node_tag>) {
				if (source_node->custom_state.storage == nullptr) {
					compile_error = "Variable Get source is missing its state.";
					recursion_stack.erase(node.id);
					return false;
				}
				source_slot = global_slot(source_node->custom_state.as<nodes::variable_node_state>().variable_id);
			}
			else {
				const NE_Pin* source_pin = nodes::find_pin_by_id(source_node->outputs, link->from_pin);
				if (source_pin == nullptr) {
					compile_error = "Source pin for input '" + input_pin.label + "' on node '" + node.title + "' is missing.";
					recursion_stack.erase(node.id);
					return false;
				}
				source_slot = ensure_output_slot(*source_node, *source_pin);
			}

			if (source_slot == -1) {
				compile_error = "Input '" + input_pin.label + "' on node '" + node.title + "' could not be assigned a VM stack slot.";
				recursion_stack.erase(node.id);
				return false;
			}

			plan.stack.input_source_slot_by_pin[make_pin_key(node.id, input_pin.id)] = source_slot;
			if (!visible_slots.contains(source_slot)) {
				if (!is_data_only_producer(*source_node)) {
					compile_error = "Input '" + input_pin.label + "' on node '" + node.title + "' references data that is not visible here.";
					recursion_stack.erase(node.id);
					return false;
				}
				std::unordered_set<int> data_visible_slots;
				auto data_stack = recursion_stack;
				if (!compile_data_node(*source_node, available_slots, data_visible_slots, data_stack, compile_error)) {
					recursion_stack.erase(node.id);
					return false;
				}
				visible_slots.insert(data_visible_slots.begin(), data_visible_slots.end());
				if (!visible_slots.contains(source_slot)) {
					compile_error = "Input '" + input_pin.label + "' on node '" + node.title + "' references data that is not visible here.";
					recursion_stack.erase(node.id);
					return false;
				}
			}
		}

		for (const auto& output_pin : node.outputs) {
			if (!output_has_consumer(graph, node, output_pin))
				continue;
			visible_slots.insert(ensure_output_slot(node, output_pin));
		}

		available_out = std::move(visible_slots);
		recursion_stack.erase(node.id);
		return true;
	};

	std::function<bool(const NE_Node&, std::unordered_set<int>, std::unordered_set<int>&, std::unordered_set<int>&, std::string&)> compile_path;
	compile_path = [&](const NE_Node& node, std::unordered_set<int> available_slots, std::unordered_set<int>& available_out, std::unordered_set<int>& recursion_stack, std::string& compile_error) -> bool {
		if (!recursion_stack.insert(node.id).second) {
			compile_error = "Exec cycle detected while compiling the VM stack.";
			return false;
		}

		for (const auto& input_pin : node.inputs) {
			const NE_Link* link = find_input_link(node, input_pin.label);
			if (link == nullptr)
				continue;
			const NE_Node* source_node = graph.find_node(link->from_node);
			if (source_node == nullptr) {
				compile_error = "Input '" + input_pin.label + "' on node '" + node.title + "' has a missing source node.";
				recursion_stack.erase(node.id);
				return false;
			}

			int source_slot = -1;
			if (source_node->type == nodes::node_type_v<nodes::variable_get_node_tag>) {
				if (source_node->custom_state.storage == nullptr) {
					compile_error = "Variable Get source is missing its state.";
					recursion_stack.erase(node.id);
					return false;
				}
				source_slot = global_slot(source_node->custom_state.as<nodes::variable_node_state>().variable_id);
			}
			else {
				const NE_Pin* source_pin = nodes::find_pin_by_id(source_node->outputs, link->from_pin);
				if (source_pin == nullptr) {
					compile_error = "Source pin for input '" + input_pin.label + "' on node '" + node.title + "' is missing.";
					recursion_stack.erase(node.id);
					return false;
				}
				source_slot = ensure_output_slot(*source_node, *source_pin);
			}

			if (source_slot == -1) {
				compile_error = "Input '" + input_pin.label + "' on node '" + node.title + "' could not be assigned a VM stack slot.";
				recursion_stack.erase(node.id);
				return false;
			}

			plan.stack.input_source_slot_by_pin[make_pin_key(node.id, input_pin.id)] = source_slot;
			if (!available_slots.contains(source_slot)) {
				if (!is_data_only_producer(*source_node)) {
					compile_error = "Input '" + input_pin.label + "' on node '" + node.title + "' references data that is not visible here.";
					recursion_stack.erase(node.id);
					return false;
				}
				std::unordered_set<int> data_visible_slots;
				auto data_stack = recursion_stack;
				if (!compile_data_node(*source_node, available_slots, data_visible_slots, data_stack, compile_error)) {
					recursion_stack.erase(node.id);
					return false;
				}
				available_slots.insert(data_visible_slots.begin(), data_visible_slots.end());
				if (!available_slots.contains(source_slot)) {
					compile_error = "Input '" + input_pin.label + "' on node '" + node.title + "' references data that is not visible here.";
					recursion_stack.erase(node.id);
					return false;
				}
			}
		}

		std::unordered_set<int> available_after = available_slots;
		for (const auto& output_pin : node.outputs) {
			if (!output_has_consumer(graph, node, output_pin))
				continue;
			available_after.insert(ensure_output_slot(node, output_pin));
		}

		const auto next_links = outgoing_exec_links(node);
		if (next_links.empty()) {
			available_out = std::move(available_after);
			recursion_stack.erase(node.id);
			return true;
		}

		const NodeTypeInfo* info = registry.find(node.type);
		if (next_links.size() == 1u || info == nullptr) {
			std::unordered_set<int> child_available;
			const bool ok = compile_path(*graph.find_node(next_links.front()->to_node), std::move(available_after), child_available, recursion_stack, compile_error);
			if (ok)
				available_out = std::move(child_available);
			recursion_stack.erase(node.id);
			return ok;
		}

		if (info->meta.compiled_visibility == NodeTypeInfo::compiled_branch_visibility::sequence_accumulate) {
			std::unordered_set<int> accumulated = std::move(available_after);
			for (const NE_Link* link : next_links) {
				if (link == nullptr)
					continue;
				const NE_Node* next_node = graph.find_node(link->to_node);
				if (next_node == nullptr)
					continue;
				std::unordered_set<int> branch_available;
				auto branch_stack = recursion_stack;
				if (!compile_path(*next_node, accumulated, branch_available, branch_stack, compile_error)) {
					recursion_stack.erase(node.id);
					return false;
				}
				accumulated = std::move(branch_available);
			}
			available_out = std::move(accumulated);
			recursion_stack.erase(node.id);
			return true;
		}

		for (const NE_Link* link : next_links) {
			if (link == nullptr)
				continue;
			const NE_Node* next_node = graph.find_node(link->to_node);
			if (next_node == nullptr)
				continue;
			std::unordered_set<int> ignored_available;
			auto branch_stack = recursion_stack;
			if (!compile_path(*next_node, available_after, ignored_available, branch_stack, compile_error)) {
				recursion_stack.erase(node.id);
				return false;
			}
		}
		available_out = std::move(available_slots);
		recursion_stack.erase(node.id);
		return true;
	};

	for (const auto& function : graph.functions) {
		const NE_Node* start_node = function_start_node(function.id);
		if (start_node == nullptr)
			continue;

		std::unordered_set<int> available_slots;
		for (size_t slot_index = 0; slot_index < plan.stack.global_slot_count; ++slot_index)
			available_slots.insert(static_cast<int>(slot_index));

		if (!graph.is_builtin_function(function.id)) {
			const auto function_it = plan.stack.functions.find(function.id);
			if (function_it != plan.stack.functions.end()) {
				for (const auto& [_, slot_index] : function_it->second.input_slots_by_label)
					available_slots.insert(slot_index);
			}
		}

		std::unordered_set<int> recursion_stack;
		std::unordered_set<int> ignored_available;
		if (!compile_path(*start_node, std::move(available_slots), ignored_available, recursion_stack, error))
			return false;
	}

	global_slot_values.resize(plan.stack.global_slot_count);
	for (const auto& function : graph.functions)
		(void)ensure_function_frame(function.id);

	error.clear();
	return true;
}

} // namespace rv
