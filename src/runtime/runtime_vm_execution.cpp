#include <render_visualizer/runtime/impl.hpp>

#include <charconv>
#include <cmath>
#include <format>
#include <mars/debug/logger.hpp>
#include <sstream>

namespace rv {
using namespace runtime_detail;

namespace {

mars::log_channel g_app_log_channel("app");

template <typename T>
inline constexpr bool is_runtime_resource_type_v =
	rv::runtime_detail::is_in_resource_type_map_v<T> ||
	std::is_same_v<T, rv::resource_tags::virtual_struct_schema>;

bool stack_slot_is_container(const stack_slot& _slot) {
	return mars::enum_has_flag(_slot.flags, stack_slot_flags::container);
}

bool stack_slot_has_virtual_struct(const stack_slot& _slot) {
	return mars::enum_has_flag(_slot.flags, stack_slot_flags::virtual_struct);
}

bool stack_slot_is_resource(const stack_slot& _slot) {
	return mars::enum_has_flag(_slot.flags, stack_slot_flags::resource);
}

resolved_value make_resolved_status(std::string _status) {
	resolved_value resolved;
	resolved.status = std::move(_status);
	return resolved;
}

std::vector<int> planned_next_node_ids(const graph_runtime& _runtime, const NE_Node& _node) {
	std::vector<int> next_node_ids;
	const runtime_detail::function_plan* plan = _runtime.function_plan(_node.function_id);
	const runtime_detail::function_step* step = _runtime.function_step_for_node(_node);
	if (plan == nullptr || step == nullptr)
		return next_node_ids;

	next_node_ids.reserve(step->next_steps.size());
	for (const auto& edge : step->next_steps) {
		if (edge.next_step_index < 0 || static_cast<size_t>(edge.next_step_index) >= plan->steps.size())
			continue;
		const runtime_detail::function_step& next_step = plan->steps[static_cast<size_t>(edge.next_step_index)];
		if (next_step.node != nullptr)
			next_node_ids.push_back(next_step.node->id);
	}
	return next_node_ids;
}

slot_type_info make_runtime_slot_type_info(size_t _type_hash, const NodeRegistry* registry) {
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

stack_slot make_runtime_stack_slot(
	size_t _type_hash,
	bool _is_container,
	bool _has_virtual_struct,
	std::string _virtual_struct_name,
	size_t _virtual_struct_layout_fingerprint,
	bool _persistent_global,
	const NodeRegistry* registry
) {
	stack_slot slot;
	slot.type = make_runtime_slot_type_info(_type_hash, registry);
	slot.virtual_struct_name = std::move(_virtual_struct_name);
	slot.virtual_struct_layout_fingerprint = _virtual_struct_layout_fingerprint;
	if (_is_container)
		slot.flags |= stack_slot_flags::container;
	if (_has_virtual_struct)
		slot.flags |= stack_slot_flags::virtual_struct;
	if (_persistent_global)
		slot.flags |= stack_slot_flags::persistent_global;
	if (registry) {
		if (const pin_type_info* pt = registry->pin_type(_type_hash); pt && pt->is_resource)
			slot.flags |= stack_slot_flags::resource;
	}
	return slot;
}

}

// Runtime frames

frame* graph_runtime::current_frame() {
	return call_frames.empty() ? nullptr : &call_frames.back();
}

const frame* graph_runtime::current_frame() const {
	return call_frames.empty() ? nullptr : &call_frames.back();
}

frame* graph_runtime::frame_for_node(const NE_Node& node) {
	if (frame* frame = current_frame(); frame != nullptr && frame->function_id == node.function_id)
		return frame;
	const auto it = frames_by_function.find(node.function_id);
	return it == frames_by_function.end() ? nullptr : &it->second;
}

const frame* graph_runtime::frame_for_node(const NE_Node& node) const {
	if (const frame* frame = current_frame(); frame != nullptr && frame->function_id == node.function_id)
		return frame;
	const auto it = frames_by_function.find(node.function_id);
	return it == frames_by_function.end() ? nullptr : &it->second;
}

frame& graph_runtime::ensure_function_frame(int function_id) {
	auto [it, inserted] = frames_by_function.try_emplace(function_id);
	if (inserted || it->second.stack != stack_for_function(function_id))
		it->second.reset(function_id, stack_for_function(function_id), global_slot_values);
	return it->second;
}

bool graph_runtime::push_frame(int function_id, std::string& error) {
	const vm_stack* stack = stack_for_function(function_id);
	if (stack == nullptr) {
		error = "Compiled function frame is missing.";
		return false;
	}
	frame next_frame;
	next_frame.reset(function_id, stack, global_slot_values);
	call_frames.push_back(std::move(next_frame));
	error.clear();
	return true;
}

void graph_runtime::pop_frame() {
	if (!call_frames.empty())
		call_frames.pop_back();
}

// Blackboard + event helpers

value_batch* graph_runtime::find_vm_output(int node_id, std::string_view label) {
	const auto node_it = vm_outputs.find(node_id);
	if (node_it == vm_outputs.end())
		return nullptr;
	const auto value_it = node_it->second.find(std::string(label));
	return value_it == node_it->second.end() ? nullptr : &value_it->second;
}

const value_batch* graph_runtime::find_vm_output(int node_id, std::string_view label) const {
	const auto node_it = vm_outputs.find(node_id);
	if (node_it == vm_outputs.end())
		return nullptr;
	const auto value_it = node_it->second.find(std::string(label));
	return value_it == node_it->second.end() ? nullptr : &value_it->second;
}

size_t graph_runtime::vm_output_size(int node_id) const {
	const auto node_it = vm_outputs.find(node_id);
	if (node_it == vm_outputs.end())
		return 0;

	size_t batch_size = 0;
	for (const auto& [_, batch] : node_it->second)
		batch_size = std::max(batch_size, batch.size());
	return batch_size;
}

bool graph_runtime::read_blackboard_value(std::string_view name, size_t expected_type_hash, std::any& out_value, std::string& error) const {
	error.clear();
	constexpr auto ctx = std::meta::access_context::current();
	template for (constexpr size_t index : std::views::iota(size_t { 0 }, std::meta::nonstatic_data_members_of(^^graph_blackboard, ctx).size())) {
		if (read_blackboard_member<index>(name, expected_type_hash, out_value, error))
			return error.empty();
	}
	error = "Unknown blackboard value '" + std::string(name) + "'.";
	return false;
}

bool graph_runtime::write_blackboard_value(std::string_view name, size_t expected_type_hash, const std::any& value, std::string& error) {
	error.clear();
	constexpr auto ctx = std::meta::access_context::current();
	template for (constexpr size_t index : std::views::iota(size_t { 0 }, std::meta::nonstatic_data_members_of(^^graph_blackboard, ctx).size())) {
		if (write_blackboard_member<index>(name, expected_type_hash, value, error))
			return error.empty();
	}
	error = "Unknown blackboard value '" + std::string(name) + "'.";
	return false;
}

namespace {

// Inline value parsing + debug formatting

std::string_view trim_inline_value(std::string_view text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
		text.remove_prefix(1);
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
		text.remove_suffix(1);
	return text;
}

bool try_parse_inline_u32_text(std::string_view json, unsigned int& out_value) {
	std::string_view text = trim_inline_value(json);
	if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
		text = text.substr(1, text.size() - 2);
	text = trim_inline_value(text);
	if (text.empty())
		return false;

	unsigned int direct_value = 0;
	const auto [direct_ptr, direct_ec] = std::from_chars(text.data(), text.data() + text.size(), direct_value);
	if (direct_ec == std::errc {} && direct_ptr == text.data() + text.size()) {
		out_value = direct_value;
		return true;
	}

	int signed_value = 0;
	const auto [signed_ptr, signed_ec] = std::from_chars(text.data(), text.data() + text.size(), signed_value);
	if (signed_ec == std::errc {} && signed_ptr == text.data() + text.size() && signed_value >= 0) {
		out_value = static_cast<unsigned int>(signed_value);
		return true;
	}

	char* float_end = nullptr;
	const std::string owned(text);
	const float float_value = std::strtof(owned.c_str(), &float_end);
	if (float_end == owned.c_str() + owned.size() &&
		float_value >= 0.0f &&
		std::floor(float_value) == float_value &&
		float_value <= static_cast<float>(std::numeric_limits<unsigned int>::max())) {
		out_value = static_cast<unsigned int>(float_value);
		return true;
	}

	return false;
}

bool parse_inline_input_json(const NodeRegistry* registry, std::string_view json, size_t expected_type_hash, bool expected_container, std::any& out_value) {
	if (expected_container)
		return false;
	if (expected_type_hash == rv::detail::pin_type_hash<unsigned int>()) {
		unsigned int value {};
		if (try_parse_inline_u32_text(json, value) || rv::nodes::json_parse(json, value)) {
			out_value = value;
			return true;
		}
		int signed_value = 0;
		if (rv::nodes::json_parse(json, signed_value) && signed_value >= 0) {
			out_value = static_cast<unsigned int>(signed_value);
			return true;
		}
		float float_value = 0.0f;
		if (rv::nodes::json_parse(json, float_value) &&
			float_value >= 0.0f &&
			std::floor(float_value) == float_value &&
			float_value <= static_cast<float>(std::numeric_limits<unsigned int>::max())) {
			out_value = static_cast<unsigned int>(float_value);
			return true;
		}
	}
	if (!registry) return false;
	const pin_type_info* pt = registry->pin_type(expected_type_hash);
	if (!pt || pt->is_resource || !pt->from_json) return false;
	return pt->from_json(json, out_value);
}

} // namespace

void graph_runtime::clear_vm_outputs(int node_id) {
	vm_outputs.erase(node_id);
	const NE_Node* node = graph.find_node(node_id);
	if (node == nullptr)
		return;
	for (const auto& pin : node->outputs)
		clear_output_slot(*node, pin.label);
}

bool graph_runtime::slot_has_value(const NE_Node& node, int slot_index) const {
	const frame* stack = frame_for_node(node);
	const slot_value* value = stack == nullptr ? nullptr : stack->slot(slot_index);
	return value != nullptr && value->initialized;
}

void graph_runtime::clear_output_slot(const NE_Node& node, std::string_view label) {
	vm_outputs[node.id][std::string(label)];
	const int slot_index = output_slot(node, label);
	if (slot_index == -1)
		return;
	frame* stack = frame_for_node(node);
	const vm_stack* vm_stack = stack ? stack->stack : stack_for_function(node.function_id);
	const slot_route* route = vm_stack ? slot_route_meta(*vm_stack, slot_index) : nullptr;
	if (stack == nullptr || route == nullptr)
		return;
	if (route->kind == slot_route_kind::function_input)
		return;
	if (slot_value* value = stack->slot(slot_index); value != nullptr)
		value->reset();
}

bool graph_runtime::write_output_slot_value(const NE_Node& node, std::string_view label, std::any value, std::string& error) {
	const int slot_index = output_slot(node, label);
	if (slot_index == -1) {
		error.clear();
		return true;
	}
	frame* stack = frame_for_node(node);
	const vm_stack* vm_stack = stack ? stack->stack : stack_for_function(node.function_id);
	const stack_slot* slot_meta = vm_stack ? stack_slot_meta(*vm_stack, slot_index) : nullptr;
	const slot_route* route = vm_stack ? slot_route_meta(*vm_stack, slot_index) : nullptr;
	if (stack == nullptr || slot_meta == nullptr || route == nullptr) {
		error = "Compiled output slot is missing.";
		return false;
	}
	slot_value* slot_runtime = stack->slot(slot_index);
	if (slot_runtime == nullptr) {
		error = "Output slot runtime storage is missing.";
		return false;
	}
	const bool append = route->kind == slot_route_kind::function_output;
	return any_to_slot_value(
		value,
		*slot_meta,
		*slot_runtime,
		append,
		error
	);
}

bool graph_runtime::append_output_slot_value(const NE_Node& node, std::string_view label, std::any value, std::string& error) {
	vm_outputs[node.id][std::string(label)].items.push_back(value);
	const int slot_index = output_slot(node, label);
	if (slot_index == -1) {
		error.clear();
		return true;
	}
	frame* stack = frame_for_node(node);
	const vm_stack* vm_stack = stack ? stack->stack : stack_for_function(node.function_id);
	const stack_slot* slot_meta = vm_stack ? stack_slot_meta(*vm_stack, slot_index) : nullptr;
	if (stack == nullptr || slot_meta == nullptr) {
		error = "Compiled output slot is missing.";
		return false;
	}
	slot_value* slot_runtime = stack->slot(slot_index);
	if (slot_runtime == nullptr) {
		error = "Output slot runtime storage is missing.";
		return false;
	}
	return any_to_slot_value(value, *slot_meta, *slot_runtime, true, error);
}

bool graph_runtime::write_slot_value(int function_id, int slot_index, const slot_value& value, std::string& error) {
	frame* stack = nullptr;
	if (frame* active_stack = current_frame(); active_stack != nullptr && active_stack->function_id == function_id)
		stack = active_stack;
	else {
		auto it = frames_by_function.find(function_id);
		if (it != frames_by_function.end())
			stack = &it->second;
	}
	if (stack == nullptr) {
		error = "Compiled stack frame is missing.";
		return false;
	}
	slot_value* destination = stack->slot(slot_index);
	if (destination == nullptr) {
		error = "Compiled stack slot is missing.";
		return false;
	}
	*destination = value;
	if (stack->stack != nullptr) {
		if (const slot_route* route = slot_route_meta(*stack->stack, slot_index); route != nullptr &&
			route->kind == slot_route_kind::global_variable) {
			mars::logger::log(
				g_app_log_channel,
				"Write global slot '{}' (slot {}) from function {} as {}",
				route->label,
				slot_index,
				function_id,
				value.bytes.empty() ? "empty value" : "cpu value"
			);
		}
	}
	error.clear();
	return true;
}

bool graph_runtime::copy_slot_value(const NE_Node& node, int source_slot_index, slot_value& out_value, std::string& error) {
	const frame* stack = frame_for_node(node);
	const vm_stack* vm_stack = stack ? stack->stack : stack_for_function(node.function_id);
	const slot_route* route = vm_stack ? slot_route_meta(*vm_stack, source_slot_index) : nullptr;
	if (stack == nullptr || route == nullptr) {
		error = "Compiled source slot is missing.";
		return false;
	}

	if (const slot_value* source_value = stack->slot(source_slot_index); source_value != nullptr && source_value->initialized) {
		out_value = *source_value;
		error.clear();
		return true;
	}

	error = "Stack slot '" + route->label + "' is not initialized.";
	return false;
}

const NE_Node* graph_runtime::resolve_slot_source_node(const NE_Node& node, int slot_index, std::string& status) const {
	const vm_stack* vm_stack = stack_for_function(node.function_id);
	const slot_route* route = vm_stack ? slot_route_meta(*vm_stack, slot_index) : nullptr;
	if (route == nullptr) {
		status = "Compiled source slot is missing.";
		return nullptr;
	}
	if (route->kind == slot_route_kind::node_output) {
		status.clear();
		return graph.find_node(route->node_id);
	}
	status = "Input does not resolve to a runtime node source.";
	return nullptr;
}

bool graph_runtime::read_slot_value(
	const NE_Node& node,
	int slot_index,
	size_t expected_type_hash,
	bool expected_container,
	size_t current_item_index,
	size_t current_item_count,
	std::any& out_value,
	std::string& error
) {
	frame* stack = frame_for_node(node);
	const vm_stack* vm_stack = stack ? stack->stack : stack_for_function(node.function_id);
	const stack_slot* slot_meta = vm_stack ? stack_slot_meta(*vm_stack, slot_index) : nullptr;
	const slot_route* route = vm_stack ? slot_route_meta(*vm_stack, slot_index) : nullptr;
	if (stack == nullptr || slot_meta == nullptr || route == nullptr) {
		error = "Compiled source slot is missing.";
		return false;
	}
	if (slot_meta->type.type_hash != expected_type_hash || stack_slot_is_container(*slot_meta) != expected_container) {
		error = "Input resolved to an incompatible stack slot type.";
		return false;
	}

	slot_value* runtime_value = stack->slot(slot_index);
	if (runtime_value == nullptr) {
		error = "Compiled source slot runtime storage is missing.";
		return false;
	}
	if (route->kind == slot_route_kind::node_output) {
		if (NE_Node* source_node = graph.find_node(route->node_id); source_node != nullptr) {
			const NodeTypeInfo* source_info = registry.find(source_node->type);
			if (source_info != nullptr && source_info->meta.is_vm_pure) {
				graph_execution_context child_ctx = make_vm_context();
				if (!ensure_vm_node_executed(child_ctx, *source_node, error))
					return false;
			}
		}
	}

	const slot_value* final_value = stack->slot(slot_index);
	if (final_value == nullptr || !final_value->initialized) {
		error = final_value == nullptr || final_value->status.empty()
			? "Compiled source slot is unset."
			: final_value->status;
		return false;
	}
	if (!stack_slot_is_container(*slot_meta) && final_value->element_count == 0) {
		error = "Compiled source slot has no values.";
		return false;
	}
	if (!stack_slot_is_container(*slot_meta) &&
		final_value->element_count != 1u &&
		final_value->element_count != current_item_count) {
		error = "Input source batch size " + std::to_string(final_value->element_count) +
			" does not match current execution batch size " + std::to_string(current_item_count) + ".";
		return false;
	}
	const size_t resolved_index = stack_slot_is_container(*slot_meta) || final_value->element_count == 1u ? 0u : current_item_index;
	if ((!stack_slot_is_container(*slot_meta) && resolved_index >= final_value->element_count) ||
		(stack_slot_is_container(*slot_meta) && resolved_index > 0u)) {
		error = "Compiled source slot item index is out of bounds.";
		return false;
	}
	return any_from_slot_value(*final_value, *slot_meta, resolved_index, out_value, error);
}

bool graph_runtime::read_slot_wildcard_value(
	const NE_Node& node,
	int slot_index,
	size_t current_item_index,
	size_t current_item_count,
	nodes::wildcard_value& out_value,
	std::string& error
) {
	frame* stack = frame_for_node(node);
	const vm_stack* vm_stack = stack ? stack->stack : stack_for_function(node.function_id);
	const stack_slot* slot_meta = vm_stack ? stack_slot_meta(*vm_stack, slot_index) : nullptr;
	if (stack == nullptr || slot_meta == nullptr) {
		error = "Compiled wildcard source slot is missing.";
		return false;
	}
	std::any any_value;
	if (!read_slot_value(node, slot_index, slot_meta->type.type_hash, stack_slot_is_container(*slot_meta), current_item_index, current_item_count, any_value, error))
		return false;
	nodes::wildcard_type_info type_info;
	type_info.type_hash = slot_meta->type.type_hash;
	type_info.is_container = stack_slot_is_container(*slot_meta);
	type_info.has_virtual_struct = stack_slot_has_virtual_struct(*slot_meta);
	type_info.virtual_struct_name = slot_meta->virtual_struct_name;
	type_info.virtual_struct_layout_fingerprint = slot_meta->virtual_struct_layout_fingerprint;
	{
		const pin_type_info* pt = graph.node_registry()
			? graph.node_registry()->pin_type_for_element(type_info.type_hash, type_info.is_container)
			: nullptr;
		if (!pt || !pt->to_wildcard_value || !pt->to_wildcard_value(any_value, type_info, out_value)) {
			error = "Compiled source slot could not be converted to a wildcard value.";
			return false;
		}
	}
	error.clear();
	return true;
}

void graph_runtime::begin_vm_output(const NE_Node& node, std::string_view label) {
	clear_output_slot(node, label);
}

void graph_runtime::store_vm_output(const NE_Node& node, std::string_view label, std::any value) {
	std::string ignored_error;
	(void)append_output_slot_value(node, label, std::move(value), ignored_error);
}

bool graph_runtime::pull_vm_wildcard_input(
	const NE_Node& node,
	std::string_view input_label,
	size_t current_item_index,
	size_t current_item_count,
	nodes::wildcard_value& out_value,
	std::string& error
) {
	const int source_slot = input_source_slot(node, input_label);
	if (source_slot == -1) {
		const NE_Pin* current_pin = nodes::find_pin_by_label(node.inputs, input_label);
		if (current_pin == nullptr || !current_pin->wildcard_resolved) {
			error = "Missing input '" + std::string(input_label) + "'.";
			return false;
		}

		for (const auto& inline_value : node.inline_input_values) {
			if (inline_value.label != input_label || !inline_value.enabled || inline_value.json.empty())
				continue;

			std::any any_value;
			if (!parse_inline_input_json(graph.node_registry(), inline_value.json, current_pin->type_hash, current_pin->is_container, any_value)) {
				error = "Inline input '" + std::string(input_label) + "' has an incompatible value.";
				return false;
			}
			{
				const nodes::wildcard_type_info wtype = nodes::wildcard_type_from_pin(*current_pin);
				const pin_type_info* pt = graph.node_registry()
					? graph.node_registry()->pin_type_for_element(wtype.type_hash, wtype.is_container)
					: nullptr;
				if (!pt || !pt->to_wildcard_value || !pt->to_wildcard_value(any_value, wtype, out_value)) {
					error = "Inline input '" + std::string(input_label) + "' could not be converted to a wildcard value.";
					return false;
				}
			}
			error.clear();
			return true;
		}

		error = "Missing input '" + std::string(input_label) + "'.";
		return false;
	}
	return read_slot_wildcard_value(node, source_slot, current_item_index, current_item_count, out_value, error);
}

bool graph_runtime::store_vm_wildcard_output(const NE_Node& node, std::string_view label, const nodes::wildcard_value& value, std::string& error) {
	std::any any_value;
	{
		const pin_type_info* pt = graph.node_registry()
			? graph.node_registry()->pin_type_for_element(value.type.type_hash, value.type.is_container)
			: nullptr;
		if (!pt || !pt->from_wildcard_value || !pt->from_wildcard_value(value, any_value)) {
			error = "Wildcard output '" + std::string(label) + "' uses an unsupported runtime type.";
			return false;
		}
	}
	store_vm_output(node, label, std::move(any_value));
	error.clear();
	return true;
}

size_t graph_runtime::determine_vm_node_batch_size(NE_Node& node, std::string& error) {
	size_t batch_size = 1;
	std::vector<std::pair<std::string, size_t>> linked_batch_sizes;
	graph_execution_context child_ctx = make_vm_context();

	for (const auto& pin : node.inputs) {
		const NE_Link* link = find_input_link(node, pin.label);
		if (link == nullptr)
			continue;

		NE_Node* source_node = graph.find_node(link->from_node);
		if (source_node == nullptr) {
			error = "Input source node for '" + pin.label + "' is missing.";
			return 0;
		}

		const NodeTypeInfo* source_info = registry.find(source_node->type);
		if (source_info == nullptr || !source_info->meta.is_vm_node) {
			error = "Input source '" + source_node->title + "' is not a VM node.";
			return 0;
		}

		if ((source_info->meta.is_vm_pure || source_info->meta.is_vm_event) && !ensure_vm_node_executed(child_ctx, *source_node, error))
			return 0;

		const NE_Pin* source_pin = nodes::find_pin_by_id(source_node->outputs, link->from_pin);
		if (source_pin == nullptr) {
			error = "Source pin for '" + pin.label + "' could not be resolved.";
			return 0;
		}
		const value_batch* source_batch = find_vm_output(source_node->id, source_pin->label);
		if (source_batch == nullptr || source_batch->empty()) {
			error = "Source node '" + source_node->title + "' has no cached output for '" + source_pin->label + "'.";
			return 0;
		}

		linked_batch_sizes.emplace_back(pin.label, source_batch->size());
		if (source_batch->size() > batch_size)
			batch_size = source_batch->size();
	}

	for (const auto& [label, size] : linked_batch_sizes) {
		if (size != 1 && size != batch_size) {
			error = "Input '" + label + "' has " + std::to_string(size) +
				" items but this node is executing with " + std::to_string(batch_size) + " item(s).";
			return 0;
		}
	}

	error.clear();
	return batch_size;
}

bool graph_runtime::execute_vm_node_batch(graph_execution_context& ctx, NE_Node& node, size_t batch_size, std::string& error, size_t* outgoing_batch_size) {
	const NodeTypeInfo* info = registry.find(node.type);
	if (info == nullptr || !info->meta.is_vm_node || !info->hooks.vm_execute) {
		error = "Node '" + node.title + "' is not a VM node.";
		return false;
	}
	if (info->hooks.validate_node && !info->hooks.validate_node(graph, *info, node, error))
		return false;

	const size_t effective_batch_size = std::max<size_t>(batch_size, 1u);
	clear_vm_outputs(node.id);
	for (const auto& pin : node.outputs)
		begin_vm_output(node, pin.label);

	const size_t invocation_count = info->meta.vm_execution_shape == NodeTypeInfo::execution_shape::reduce ? 1u : effective_batch_size;
	for (size_t item_index = 0; item_index < invocation_count; ++item_index) {
		blackboard.current_item_index = std::min(item_index, effective_batch_size - 1);
		blackboard.current_item_count = effective_batch_size;
		ctx.current_item_index = blackboard.current_item_index;
		ctx.current_item_count = blackboard.current_item_count;
		ctx.delta_time = blackboard.delta_time;
		ctx.time = blackboard.time;

		if (!info->hooks.vm_execute(ctx, node, error))
			return false;
	}

	size_t produced_batch_size = 0;
	for (const auto& pin : node.outputs) {
		const value_batch* batch = find_vm_output(node.id, pin.label);
		const size_t size = batch == nullptr ? 0 : batch->size();
		produced_batch_size = std::max(produced_batch_size, size);
	}
	for (const auto& pin : node.outputs) {
		const value_batch* batch = find_vm_output(node.id, pin.label);
		const size_t size = batch == nullptr ? 0 : batch->size();
		if (size != 0 && size != produced_batch_size) {
			error = "Node '" + node.title + "' produced mismatched output batch sizes.";
			return false;
		}
	}

	if (info->meta.vm_execution_shape == NodeTypeInfo::execution_shape::map) {
		for (const auto& pin : node.outputs) {
			const value_batch* batch = find_vm_output(node.id, pin.label);
			if (batch != nullptr && !batch->empty() && batch->size() != effective_batch_size) {
				error = "Node '" + node.title + "' produced " + std::to_string(batch->size()) +
					" items for output '" + pin.label + "', expected " + std::to_string(effective_batch_size) + ".";
				return false;
			}
		}
	}
	if (info->meta.vm_execution_shape == NodeTypeInfo::execution_shape::reduce) {
		for (const auto& pin : node.outputs) {
			const value_batch* batch = find_vm_output(node.id, pin.label);
			if (batch != nullptr && !batch->empty() && batch->size() != 1u) {
				error = "Node '" + node.title + "' is reduce-shaped but output '" + pin.label + "' produced " +
					std::to_string(batch->size()) + " items.";
				return false;
			}
		}
	}

	if (outgoing_batch_size != nullptr) {
		// Some callable side-effect nodes intentionally don't emit per-item VM values
		// even though they may still have graph-visible resource outputs. In that case
		// they should preserve the incoming execution batch instead of collapsing the
		// downstream exec chain to 0 items.
		*outgoing_batch_size = produced_batch_size == 0 ? effective_batch_size : produced_batch_size;
	}
	return true;
}

bool graph_runtime::ensure_vm_node_executed(graph_execution_context& ctx, NE_Node& node, std::string& error) {
	const NodeTypeInfo* info = registry.find(node.type);
	if (info == nullptr || !info->meta.is_vm_node || !info->hooks.vm_execute) {
		error = "Node '" + node.title + "' is not a VM node.";
		return false;
	}
	if (!info->meta.is_vm_pure && !info->meta.is_vm_event)
		return true;
	if (info->meta.vm_reexecute_each_tick) {
		// Blackboard-backed values should always observe the latest frame state instead of
		// reusing a cached pure-node result from an earlier tick.
	} else if (!find_vm_output(node.id, node.outputs.empty() ? std::string_view {} : std::string_view(node.outputs.front().label))) {
		// fall through
	} else if (vm_output_size(node.id) > 0) {
		return true;
	}
	if (!vm_evaluating.insert(node.id).second) {
		error = "VM cycle detected at node '" + node.title + "'.";
		return false;
	}
	const size_t batch_size = info->meta.is_vm_event ? 1u : determine_vm_node_batch_size(node, error);
	const bool ok = !error.empty() ? false : execute_vm_node_batch(ctx, node, batch_size, error, nullptr);
	vm_evaluating.erase(node.id);
	return ok;
}

bool graph_runtime::pull_vm_input(
	const NE_Node& node,
	std::string_view input_label,
	size_t expected_type_hash,
	bool expected_container,
	size_t current_item_index,
	size_t current_item_count,
	std::any& out_value,
	std::string& error
) {
	const int source_slot = input_source_slot(node, input_label);
	if (source_slot == -1) {
		for (const auto& inline_value : node.inline_input_values) {
			if (inline_value.label != input_label || !inline_value.enabled || inline_value.json.empty())
				continue;
			if (parse_inline_input_json(graph.node_registry(), inline_value.json, expected_type_hash, expected_container, out_value)) {
				error.clear();
				return true;
			}
			error = "Inline input '" + std::string(input_label) + "' has an incompatible value: " + inline_value.json;
			return false;
		}
		error = "Missing input '" + std::string(input_label) + "'.";
		return false;
	}
	return read_slot_value(node, source_slot, expected_type_hash, expected_container, current_item_index, current_item_count, out_value, error);
}

const NE_Link* graph_runtime::find_exec_input_link(const NE_Node& node) const {
	if (!node.has_exec_input)
		return nullptr;
	for (const auto& link : graph.links) {
		if (link.to_node == node.id && link.to_pin == node.exec_input.id)
			return &link;
	}
	return nullptr;
}

bool graph_runtime::is_render_command_node(const NE_Node& node) const {
	const NodeTypeInfo* info = registry.find(node.type);
	return info != nullptr && !info->meta.is_vm_node && info->hooks.vm_execute != nullptr;
}

bool graph_runtime::is_exec_node(const NE_Node& node) const {
	const NodeTypeInfo* info = registry.find(node.type);
	if (info == nullptr)
		return false;
	return (info->meta.is_vm_node && info->meta.is_vm_callable) || is_render_command_node(node);
}

bool graph_runtime::read_function_input(const NE_Node&, std::string_view label, std::any& out_value, std::string& error) {
	if (call_frames.empty() || call_frames.back().stack == nullptr) {
		error = "Function input was requested outside of a function call.";
		return false;
	}
	const frame& stack = call_frames.back();
	const auto function_it = stack.stack->functions.find(stack.function_id);
	if (function_it == stack.stack->functions.end()) {
		error = "Function stack view is missing.";
		return false;
	}
	const auto slot_it = function_it->second.input_slots_by_label.find(std::string(label));
	if (slot_it == function_it->second.input_slots_by_label.end()) {
		error = "Function input '" + std::string(label) + "' is missing.";
		return false;
	}
	const int slot_index = slot_it->second;
	const stack_slot* slot_meta = stack_slot_meta(*stack.stack, slot_index);
	const slot_value* slot_runtime = stack.slot(slot_index);
	if (slot_meta == nullptr || slot_runtime == nullptr || !slot_runtime->initialized) {
		error = "Function input '" + std::string(label) + "' is unset.";
		return false;
	}
	if (!stack_slot_is_container(*slot_meta) && slot_runtime->element_count == 0) {
		error = "Function input '" + std::string(label) + "' has no runtime value.";
		return false;
	}
	if (!stack_slot_is_container(*slot_meta) &&
		slot_runtime->element_count != 1u &&
		slot_runtime->element_count != blackboard.current_item_count) {
		error = "Function input batch size " + std::to_string(slot_runtime->element_count) +
			" does not match current execution batch size " + std::to_string(blackboard.current_item_count) + ".";
		return false;
	}
	const size_t resolved_index = stack_slot_is_container(*slot_meta) || slot_runtime->element_count == 1u ? 0u : blackboard.current_item_index;
	if ((!stack_slot_is_container(*slot_meta) && resolved_index >= slot_runtime->element_count) ||
		(stack_slot_is_container(*slot_meta) && resolved_index > 0u)) {
		error = "Function input item index is out of bounds.";
		return false;
	}
	if (!any_from_slot_value(*slot_runtime, *slot_meta, resolved_index, out_value, error))
		return false;
	error.clear();
	return true;
}

bool graph_runtime::write_function_output(const NE_Node&, std::string_view label, std::any value, std::string& error) {
	if (call_frames.empty() || call_frames.back().stack == nullptr) {
		error = "Function output was written outside of a function call.";
		return false;
	}
	frame& stack = call_frames.back();
	const auto function_it = stack.stack->functions.find(stack.function_id);
	if (function_it == stack.stack->functions.end()) {
		error = "Function stack view is missing.";
		return false;
	}
	const auto slot_it = function_it->second.output_slots_by_label.find(std::string(label));
	if (slot_it == function_it->second.output_slots_by_label.end()) {
		error = "Function output '" + std::string(label) + "' is missing.";
		return false;
	}
	const int slot_index = slot_it->second;
	const stack_slot* slot_meta = stack_slot_meta(*stack.stack, slot_index);
	slot_value* slot_runtime = stack.slot(slot_index);
	if (slot_meta == nullptr || slot_runtime == nullptr) {
		error = "Function output slot storage is missing.";
		return false;
	}
	return any_to_slot_value(value, *slot_meta, *slot_runtime, true, error);
}

bool graph_runtime::execute_call_function(NE_Node& node, std::string& error) {
	if (node.custom_state.storage == nullptr) {
		error = "Call Function is missing its state.";
		return false;
	}

	const auto& state = node.custom_state.as<nodes::call_function_node_state>();
	const graph_function_definition* function = graph.find_function(state.function_id);
	if (function == nullptr || graph.is_builtin_function(function->id)) {
		error = "Call Function target is invalid.";
		return false;
	}
	const NE_Node* start_node = function_start_node(function->id);
	if (start_node == nullptr) {
		error = "Target function '" + function->name + "' is missing its Function Inputs node.";
		return false;
	}
	const size_t active_count = ++active_function_call_counts[function->id];
	if (active_count > 64u) {
		--active_function_call_counts[function->id];
		error = "Function '" + function->name + "' exceeded the recursion limit of 64 active calls.";
		return false;
	}

	if (!push_frame(function->id, error)) {
		--active_function_call_counts[function->id];
		return false;
	}
	for (const auto& pin : node.inputs) {
		const int caller_source_slot = input_source_slot(node, pin.label);
		const int callee_input_slot = output_slot(*start_node, pin.label);
		if (caller_source_slot == -1 || callee_input_slot == -1) {
			pop_frame();
			--active_function_call_counts[function->id];
			error = "Call Function has unresolved stack slots for '" + pin.label + "'.";
			return false;
		}
		slot_value copied_value;
		if (!copy_slot_value(node, caller_source_slot, copied_value, error) ||
			!write_slot_value(function->id, callee_input_slot, copied_value, error)) {
			pop_frame();
			--active_function_call_counts[function->id];
			return false;
		}
	}
	graph_execution_context child_ctx = make_vm_context();
	std::unordered_set<int> executed_nodes;
	execute_exec_chain_from(start_node->id, child_ctx, 1u, executed_nodes);
	frame completed_stack = std::move(call_frames.back());
	pop_frame();
	--active_function_call_counts[function->id];
	if (has_error) {
		error = last_error.empty() ? ("Function '" + function->name + "' failed.") : last_error;
		return false;
	}

	const NE_Node* outputs_node = function_outputs_node(function->id);
	if (outputs_node == nullptr) {
		error = "Target function '" + function->name + "' is missing its Function Outputs node.";
		return false;
	}
	for (const auto& pin : node.outputs) {
		const int callee_output_slot = output_slot(*outputs_node, pin.label);
		const slot_value* output_value = callee_output_slot == -1 ? nullptr : completed_stack.slot(callee_output_slot);
		const stack_slot* output_meta = callee_output_slot == -1 ? nullptr : stack_slot_meta(*completed_stack.stack, callee_output_slot);
		if (output_meta == nullptr || output_value == nullptr || !output_value->initialized || (!stack_slot_is_container(*output_meta) && output_value->element_count == 0)) {
			error = "Function '" + function->name + "' did not produce output '" + pin.label + "'.";
			return false;
		}
		const size_t item_count = stack_slot_is_container(*output_meta) ? 1u : output_value->element_count;
		for (size_t item_index = 0; item_index < item_count; ++item_index) {
			std::any item;
			if (!any_from_slot_value(*output_value, *output_meta, item_index, item, error))
				return false;
			store_vm_output(node, pin.label, std::move(item));
		}
	}

	error.clear();
	return true;
}

graph_execution_context graph_runtime::make_vm_context() {
	graph_execution_context ctx;
	ctx.runtime = this;
	ctx.delta_time = blackboard.delta_time;
	ctx.time = blackboard.time;
	ctx.current_item_index = blackboard.current_item_index;
	ctx.current_item_count = blackboard.current_item_count;
	return ctx;
}

bool graph_execution_context::resolve_input_any(const NE_Node& node, std::string_view label, size_t expected_type_hash, bool expected_container, size_t item_index, size_t item_count, std::any& out_value, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->pull_vm_input(node, label, expected_type_hash, expected_container, item_index, item_count, out_value, error);
}

bool graph_execution_context::resolve_wildcard_input(const NE_Node& node, std::string_view label, NE_WildcardValue& value, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->pull_vm_wildcard_input(node, label, current_item_index, current_item_count, value, error);
}

bool graph_execution_context::read_function_input_any(const NE_Node& node, std::string_view label, std::any& out_value, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->read_function_input(node, label, out_value, error);
}

bool graph_execution_context::write_function_output_any(const NE_Node& node, std::string_view label, std::any value, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->write_function_output(node, label, std::move(value), error);
}

bool graph_execution_context::call_function(NE_Node& node, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->execute_call_function(node, error);
}

void graph_execution_context::begin_output(const NE_Node& node, std::string_view label) const {
	if (runtime == nullptr)
		return;
	runtime->begin_vm_output(node, label);
}

void graph_execution_context::push_output_any(const NE_Node& node, std::string_view label, std::any value) const {
	if (runtime == nullptr)
		return;
	runtime->store_vm_output(node, label, std::move(value));
}

bool graph_execution_context::set_wildcard_output(const NE_Node& node, std::string_view label, const NE_WildcardValue& value, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->store_vm_wildcard_output(node, label, value, error);
}

bool graph_execution_context::read_blackboard_any(std::string_view name, size_t expected_type_hash, std::any& out_value, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->read_blackboard_value(name, expected_type_hash, out_value, error);
}

bool graph_execution_context::write_blackboard_any(std::string_view name, size_t expected_type_hash, std::any value, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->write_blackboard_value(name, expected_type_hash, value, error);
}

bool graph_execution_context::read_variable_any(int variable_id, size_t expected_type_hash, bool expected_container, std::any& out_value, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->read_variable_value(variable_id, expected_type_hash, expected_container, out_value, error);
}

bool graph_execution_context::write_variable_input(const NE_Node& node, int variable_id, std::string_view input_label, std::string& error) const {
	if (runtime == nullptr) {
		error = "Execution context has no runtime.";
		return false;
	}
	return runtime->write_variable_input_value(node, variable_id, input_label, current_item_index, current_item_count, error);
}


bool graph_runtime::execute_exec_chain_for_item(
	int node_id,
	graph_execution_context& ctx,
	size_t item_index,
	size_t item_count,
	std::unordered_set<int>& recursion_stack
) {
	int current_id = node_id;
	while (current_id != -1) {
		if (!recursion_stack.insert(current_id).second) {
			has_error = true;
			last_error = "Exec cycle detected at node id " + std::to_string(current_id) + ".";
			return false;
		}

		NE_Node* node = graph.find_node(current_id);
		if (node == nullptr) {
			recursion_stack.erase(current_id);
			return true;
		}
		const NodeTypeInfo* info = registry.find(node->type);
		if (info == nullptr) {
			recursion_stack.erase(current_id);
			return true;
		}

		std::string error;
		blackboard.current_item_index = item_index;
		blackboard.current_item_count = item_count;
		ctx.current_item_index = item_index;
		ctx.current_item_count = item_count;
		ctx.delta_time = blackboard.delta_time;
		ctx.time = blackboard.time;

		if (info->meta.is_vm_node && info->meta.is_vm_callable) {
			if (!info->hooks.vm_execute || !info->hooks.vm_execute(ctx, *node, error)) {
				node->has_run_result = true;
				node->last_run_success = false;
				node->last_run_message = error.empty() ? "VM execution failed." : error;
				mars::logger::error(g_app_log_channel, "Node '{}' failed: {}", node->title, node->last_run_message);
				has_error = true;
				last_error = error.empty() ? ("VM execution failed for " + node->title) : error;
				recursion_stack.erase(current_id);
				return false;
			}
			node->has_run_result = true;
			node->last_run_success = true;
			node->last_run_message.clear();
		} else if (is_render_command_node(*node)) {
			if (!info->hooks.vm_execute || !info->hooks.vm_execute(ctx, *node, error)) {
				node->has_run_result = true;
				node->last_run_success = false;
				node->last_run_message = error.empty() ? "Command execution failed." : error;
				mars::logger::error(g_app_log_channel, "Node '{}' failed: {}", node->title, node->last_run_message);
				has_error = true;
				last_error = error.empty() ? (node->title + ": command execution failed") : (node->title + ": " + error);
				recursion_stack.erase(current_id);
				return false;
			}
			node->has_run_result = true;
			node->last_run_success = true;
			node->last_run_message.clear();
		} else {
			node->has_run_result = true;
			node->last_run_success = true;
			node->last_run_message.clear();
		}

		if (!call_frames.empty() &&
			node->type == nodes::node_type_v<nodes::function_outputs_node_tag> &&
			node->function_id == call_frames.back().function_id) {
			call_frames.back().returned = true;
			recursion_stack.erase(current_id);
			return true;
		}

		const std::vector<int> next_node_ids = planned_next_node_ids(*this, *node);
		recursion_stack.erase(current_id);
		if (next_node_ids.empty()) {
			const auto next_links = outgoing_exec_links(*node);
			if (next_links.empty())
				return true;
			if (next_links.size() == 1u) {
				current_id = next_links.front()->to_node;
				continue;
			}

			for (const NE_Link* next_link : next_links) {
				if (next_link == nullptr)
					continue;
				auto branch_stack = recursion_stack;
				if (!execute_exec_chain_for_item(next_link->to_node, ctx, item_index, item_count, branch_stack))
					return false;
				if (has_error)
					return false;
			}
			return true;
		}
		if (next_node_ids.size() == 1u) {
			current_id = next_node_ids.front();
			continue;
		}

		for (int next_node_id : next_node_ids) {
			auto branch_stack = recursion_stack;
			if (!execute_exec_chain_for_item(next_node_id, ctx, item_index, item_count, branch_stack))
				return false;
			if (has_error)
				return false;
		}
		return true;
	}

	return true;
}

bool graph_runtime::execute_exec_chain_itemwise_from(int node_id, graph_execution_context& ctx, size_t item_count) {
	const size_t effective_count = std::max<size_t>(item_count, 1u);
	for (size_t item_index = 0; item_index < effective_count; ++item_index) {
		std::unordered_set<int> recursion_stack;
		if (!execute_exec_chain_for_item(node_id, ctx, item_index, effective_count, recursion_stack))
			return false;
		if (has_error)
			return false;
	}
	return true;
}

void graph_runtime::execute_exec_chain_from(int node_id, graph_execution_context& ctx, size_t incoming_batch_size, std::unordered_set<int>& executed_nodes) {
	int current_id = node_id;
	size_t current_batch_size = std::max<size_t>(incoming_batch_size, 1u);
	while (current_id != -1) {
		if (!executed_nodes.insert(current_id).second)
			return;

		NE_Node* node = graph.find_node(current_id);
		if (node == nullptr)
			return;
		const NodeTypeInfo* info = registry.find(node->type);
		if (info == nullptr)
			return;

		std::string error;
		if (info->meta.is_vm_node && info->meta.is_vm_callable) {
			size_t outgoing_batch_size = current_batch_size;
			if (!execute_vm_node_batch(ctx, *node, current_batch_size, error, &outgoing_batch_size)) {
				node->has_run_result = true;
				node->last_run_success = false;
				node->last_run_message = error.empty() ? "VM execution failed." : error;
				mars::logger::error(g_app_log_channel, "Node '{}' failed: {}", node->title, node->last_run_message);
				has_error = true;
				last_error = error.empty() ? ("VM execution failed for " + node->title) : error;
				return;
			}
			node->has_run_result = true;
			node->last_run_success = true;
			node->last_run_message.clear();
			current_batch_size = outgoing_batch_size;
		} else if (is_render_command_node(*node)) {
			blackboard.current_item_index = std::min(blackboard.current_item_index, current_batch_size - 1);
			blackboard.current_item_count = current_batch_size;
			ctx.current_item_index = blackboard.current_item_index;
			ctx.current_item_count = blackboard.current_item_count;
			ctx.delta_time = blackboard.delta_time;
			ctx.time = blackboard.time;
			if (!info->hooks.vm_execute(ctx, *node, error)) {
				node->has_run_result = true;
				node->last_run_success = false;
				node->last_run_message = error.empty() ? "Command execution failed." : error;
				mars::logger::error(g_app_log_channel, "Node '{}' failed: {}", node->title, node->last_run_message);
				has_error = true;
				last_error = error.empty() ? (node->title + ": command execution failed") : (node->title + ": " + error);
				return;
			}
			node->has_run_result = true;
			node->last_run_success = true;
			node->last_run_message.clear();
		} else {
			node->has_run_result = true;
			node->last_run_success = true;
			node->last_run_message.clear();
		}

		if (!call_frames.empty() &&
			node->type == nodes::node_type_v<nodes::function_outputs_node_tag> &&
			node->function_id == call_frames.back().function_id) {
			call_frames.back().returned = true;
			return;
		}

		const std::vector<int> next_node_ids = planned_next_node_ids(*this, *node);
		if (next_node_ids.empty()) {
			const auto next_links = outgoing_exec_links(*node);
			if (next_links.empty())
				return;
			if (next_links.size() == 1u) {
				current_id = next_links.front()->to_node;
				continue;
			}
			for (const NE_Link* next_link : next_links) {
				if (next_link == nullptr)
					continue;
				execute_exec_chain_from(next_link->to_node, ctx, current_batch_size, executed_nodes);
				if (has_error)
					return;
			}
			return;
		}
		if (next_node_ids.size() == 1u) {
			current_id = next_node_ids.front();
			continue;
		}
		for (int next_node_id : next_node_ids) {
			execute_exec_chain_from(next_node_id, ctx, current_batch_size, executed_nodes);
			if (has_error)
				return;
		}
		return;
	}
}

void graph_runtime::execute_vm_chain_from(int node_id, graph_execution_context& ctx, size_t incoming_batch_size) {
	std::unordered_set<int> executed_nodes;
	execute_exec_chain_from(node_id, ctx, incoming_batch_size, executed_nodes);
}

void graph_runtime::dispatch_vm_event_type(size_t event_type) {
	vm_outputs.clear();
	vm_evaluating.clear();
	graph_execution_context ctx = make_vm_context();
	for (auto& node : graph.nodes) {
		const NodeTypeInfo* info = registry.find(node.type);
		if (node.type != event_type || info == nullptr || !info->meta.is_vm_event || !info->hooks.vm_execute)
			continue;
		std::string error;
		size_t outgoing_batch_size = 1;
		if (!execute_vm_node_batch(ctx, node, 1u, error, &outgoing_batch_size)) {
			node.has_run_result = true;
			node.last_run_success = false;
			node.last_run_message = error.empty() ? "VM event failed." : error;
			mars::logger::error(g_app_log_channel, "Node '{}' failed: {}", node.title, node.last_run_message);
			has_error = true;
			last_error = error.empty() ? ("VM event failed for " + node.title) : error;
			return;
		}
		node.has_run_result = true;
		node.last_run_success = true;
		node.last_run_message.clear();
		const std::vector<int> next_node_ids = planned_next_node_ids(*this, node);
		if (next_node_ids.empty()) {
			for (const NE_Link* next_link : outgoing_exec_links(node)) {
				if (next_link != nullptr)
					execute_vm_chain_from(next_link->to_node, ctx, outgoing_batch_size);
				if (has_error)
					return;
			}
		}
		else {
			for (int next_node_id : next_node_ids) {
				execute_vm_chain_from(next_node_id, ctx, outgoing_batch_size);
				if (has_error)
					return;
			}
		}
	}
}

bool graph_runtime::initialize_variable_defaults() {
	variable_values.clear();
	for (auto& value : global_slot_values)
		value.reset();
	for (const auto& slot : graph.variable_slots) {
		const int slot_index = global_slot(slot.id);
		if (slot_index == -1 || static_cast<size_t>(slot_index) >= global_slot_values.size())
			continue;
		slot_value& value = global_slot_values[static_cast<size_t>(slot_index)];
		const stack_slot slot_meta = make_runtime_stack_slot(
			slot.type_hash,
			slot.is_container,
			slot.has_virtual_struct,
			slot.virtual_struct_name,
			slot.virtual_struct_layout_fingerprint,
			true,
			graph.node_registry()
		);
		value.status = "Unset";
		if (mars::enum_has_flag(slot_meta.flags, stack_slot_flags::resource)) {
			value.status = "Waiting for Set.";
			continue;
		}
		if (slot.is_container) {
			value.initialized = true;
			value.element_count = 0;
			value.element_stride = make_runtime_slot_type_info(slot.type_hash, graph.node_registry()).value_size;
			value.status = "Using empty default container.";
			continue;
		}

		bool ok = false;
		std::any any_value;
		if (const pin_type_info* pt = graph.node_registry() ? graph.node_registry()->pin_type(slot.type_hash) : nullptr;
			pt && !pt->is_resource && !slot.is_container && pt->from_json) {
			ok = pt->from_json(slot.default_json, any_value);
		}

		if (ok) {
			std::string write_error;
			if (!any_to_slot_value(any_value, slot_meta, value, false, write_error))
				value.status = write_error.empty() ? "No inline default." : write_error;
			else
				value.status = "Using authored default.";
		} else {
			value.status = "No inline default.";
		}
	}
	return true;
}

bool graph_runtime::initialize_ref_defaults() {
	return initialize_variable_defaults();
}

bool graph_runtime::any_to_inline_cpu_payload(const std::any& value, size_t type_hash, bool is_container, std::vector<std::byte>& bytes, size_t& element_count) const {
	const pin_type_info* pt = graph.node_registry()
		? graph.node_registry()->pin_type_for_element(type_hash, is_container)
		: nullptr;
	if (!pt || pt->is_resource || !pt->copy_to_bytes)
		return false;
	return pt->copy_to_bytes(value, bytes, element_count);
}

bool graph_runtime::any_from_inline_cpu_payload(size_t type_hash, bool is_container, const std::vector<std::byte>& bytes, size_t element_count, std::any& value) const {
	const pin_type_info* pt = graph.node_registry()
		? graph.node_registry()->pin_type_for_element(type_hash, is_container)
		: nullptr;
	if (!pt || pt->is_resource || !pt->copy_from_bytes)
		return false;
	return pt->copy_from_bytes(bytes, element_count, value);
}

bool graph_runtime::any_from_slot_value(const slot_value& slot_value, const stack_slot& slot, size_t item_index, std::any& value, std::string& error) const {
	value.reset();
	if (!slot_value.initialized) {
		error = slot_value.status.empty() ? "VM stack slot is unset." : slot_value.status;
		return false;
	}
	if (!stack_slot_is_container(slot) && slot_value.element_count == 0) {
		error = "VM stack slot has no value.";
		return false;
	}
	if (!stack_slot_is_container(slot) && item_index >= slot_value.element_count) {
		error = "VM stack slot item index is out of bounds.";
		return false;
	}

	if (stack_slot_is_resource(slot)) {
		if (slot_value.bytes.size() < sizeof(void*)) {
			error = "VM stack resource slot is empty.";
			return false;
		}
		void* pointer = nullptr;
		const size_t offset = item_index * sizeof(void*);
		if (offset + sizeof(void*) > slot_value.bytes.size()) {
			error = "VM stack resource slot is truncated.";
			return false;
		}
		std::memcpy(&pointer, slot_value.bytes.data() + offset, sizeof(void*));
		const bool matched = rv::runtime_detail::dispatch_resource_type(slot.type.type_hash, [&]<typename, typename impl_t>() {
			value = static_cast<impl_t*>(pointer);
			error.clear();
		});
		if (!matched) {
			error = "VM stack resource slot has an unsupported resource type.";
			return false;
		}
		return true;
	}

	if (slot.type.type_hash == rv::detail::pin_type_hash<std::string>()) {
		if (slot_value.element_count != 1u || item_index != 0u) {
			error = "Batched string VM stack values are not supported.";
			return false;
		}
		value = std::string(reinterpret_cast<const char*>(slot_value.bytes.data()), slot_value.bytes.size());
		error.clear();
		return true;
	}

	if (stack_slot_is_container(slot)) {
		if (!any_from_inline_cpu_payload(slot.type.type_hash, true, slot_value.bytes, slot_value.element_count, value)) {
			error = "VM stack container slot could not be converted back to a typed value.";
			return false;
		}
		error.clear();
		return true;
	}

	if (slot_value.element_stride == 0 || slot_value.bytes.size() < (item_index + 1u) * slot_value.element_stride) {
		error = "VM stack slot payload is truncated.";
		return false;
	}
	std::vector<std::byte> payload(slot_value.element_stride);
	std::memcpy(payload.data(), slot_value.bytes.data() + item_index * slot_value.element_stride, slot_value.element_stride);
	if (!any_from_inline_cpu_payload(slot.type.type_hash, false, payload, 1u, value)) {
		error = "VM stack slot could not be converted back to a typed value.";
		return false;
	}
	error.clear();
	return true;
}

bool graph_runtime::any_to_slot_value(const std::any& value, const stack_slot& slot, slot_value& out_value, bool append, std::string& error) const {
	auto append_bytes = [&](const std::byte* _data, size_t _size, size_t _stride, std::string _status) {
		if (!append)
			out_value.reset();
		if (append && out_value.initialized && out_value.element_stride != 0 && out_value.element_stride != _stride) {
			error = "VM stack slot append used a different element stride.";
			return false;
		}
		out_value.bytes.insert(out_value.bytes.end(), _data, _data + _size);
		out_value.initialized = true;
		out_value.element_stride = _stride;
		out_value.element_count = append ? (out_value.element_count + 1u) : 1u;
		out_value.status = std::move(_status);
		error.clear();
		return true;
	};

	if (stack_slot_is_resource(slot)) {
		auto append_pointer = [&](auto* _pointer) {
			const std::byte* begin = reinterpret_cast<const std::byte*>(&_pointer);
			return append_bytes(begin, sizeof(_pointer), sizeof(_pointer), "Stored runtime resource.");
		};

		bool result = false;
		const bool matched = rv::runtime_detail::dispatch_resource_type(slot.type.type_hash, [&]<typename, typename impl_t>() {
			if (value.type() == typeid(impl_t*))
				result = append_pointer(std::any_cast<impl_t*>(value));
			else
				error = "VM stack slot received the wrong resource pointer type.";
		});
		if (!matched) {
			error = "VM stack slot received an unsupported resource value.";
			return false;
		}
		return result;
	}

	if (slot.type.type_hash == rv::detail::pin_type_hash<std::string>()) {
		if (value.type() != typeid(std::string)) {
			error = "VM stack string slot received the wrong type.";
			return false;
		}
		if (append) {
			error = "Batched string VM stack values are not supported.";
			return false;
		}
		const auto& typed_value = std::any_cast<const std::string&>(value);
		out_value.reset();
		out_value.initialized = true;
		out_value.element_count = 1;
		out_value.element_stride = 0;
		out_value.bytes.resize(typed_value.size());
		if (!typed_value.empty())
			std::memcpy(out_value.bytes.data(), typed_value.data(), typed_value.size());
		out_value.status = "Stored CPU value.";
		error.clear();
		return true;
	}

	std::vector<std::byte> payload;
	size_t payload_count = 0;
	if (!any_to_inline_cpu_payload(value, slot.type.type_hash, stack_slot_is_container(slot), payload, payload_count)) {
		error = "VM stack slot received a non-copyable CPU value.";
		return false;
	}
	if (!stack_slot_is_container(slot) && payload_count != 1u) {
		error = "VM stack slot payload conversion returned an unexpected element count.";
		return false;
	}
	const size_t payload_stride = stack_slot_is_container(slot) ? slot.type.value_size : payload.size();
	return append_bytes(payload.data(), payload.size(), payload_stride, append ? "Appended CPU value." : "Stored CPU value.");
}

bool graph_runtime::read_variable_value(int variable_id, size_t expected_type_hash, bool expected_container, std::any& out_value, std::string& error) const {
	const nodes::variable_slot_state* slot = graph.find_variable_slot(variable_id);
	if (slot == nullptr) {
		error = "Variable is missing.";
		return false;
	}
	if (slot->type_hash != expected_type_hash || slot->is_container != expected_container) {
		error = "Variable '" + slot->name + "' was requested with the wrong type.";
		return false;
	}
	const int slot_index = global_slot(variable_id);
	if (slot_index == -1 || static_cast<size_t>(slot_index) >= global_slot_values.size()) {
		error = "Variable '" + slot->name + "' has no compiled stack slot.";
		return false;
	}
	const slot_value& value = global_slot_values[static_cast<size_t>(slot_index)];
	if (!value.initialized) {
		error = value.status.empty() ? ("Variable '" + slot->name + "' is unset.") : value.status;
		return false;
	}
	if (!slot->is_container && value.element_count == 0) {
		error = "Variable '" + slot->name + "' has no runtime value.";
		return false;
	}
	const stack_slot* slot_meta = stack_slot_meta(plan.stack, slot_index);
	if (slot_meta == nullptr) {
		error = "Variable '" + slot->name + "' has no compiled slot metadata.";
		return false;
	}
	return any_from_slot_value(value, *slot_meta, 0u, out_value, error);
}

bool graph_runtime::write_variable_input_value(const NE_Node& node, int variable_id, std::string_view input_label, size_t current_item_index, size_t current_item_count, std::string& error) {
	nodes::variable_slot_state* slot = graph.find_variable_slot(variable_id);
	if (slot == nullptr) {
		error = "Variable Set has no shared variable selected.";
		return false;
	}

	const int source_slot = input_source_slot(node, input_label);
	if (source_slot == -1) {
		error = "Variable Set is missing its compiled input source.";
		return false;
	}
	const int destination_slot = global_slot(slot->id);
	if (destination_slot == -1) {
		error = "Variable Set has no compiled global slot.";
		return false;
	}
	const stack_slot* destination_meta = stack_slot_meta(plan.stack, destination_slot);
	if (destination_meta == nullptr) {
		error = "Variable Set destination slot metadata is missing.";
		return false;
	}

	slot_value copied_value;
	resolved_value resolved_source = resolve_current_input_source(node, input_label, current_item_index, current_item_count);
	if (resolved_source.source_kind != value_source_kind::none &&
		copy_variable_payload(resolved_source, copied_value.bytes, copied_value.element_count)) {
		if (!stack_slot_is_container(*destination_meta) && copied_value.element_count != 1u) {
			error = "Variable Set expected a single value for '" + slot->name + "'.";
			return false;
		}
		if (!stack_slot_is_resource(*destination_meta))
			copied_value.element_stride = destination_meta->type.value_size;
		else
			copied_value.element_stride = sizeof(void*);
		copied_value.initialized = true;
		copied_value.status = resolved_source.status.empty() ? "Copied variable input source." : resolved_source.status;
	}
	else if (!copy_slot_value(node, source_slot, copied_value, error)) {
		return false;
	}

	if (!write_slot_value(node.function_id, destination_slot, copied_value, error))
		return false;

	for (const auto& graph_node : graph.nodes) {
		if (graph_node.type != nodes::node_type_v<nodes::variable_get_node_tag> || graph_node.custom_state.storage == nullptr)
			continue;
		const auto& get_state = graph_node.custom_state.as<nodes::variable_node_state>();
		if (get_state.variable_id == variable_id)
			clear_vm_outputs(graph_node.id);
	}
	error.clear();
	return true;
}

resolved_value graph_runtime::resolve_variable_set_source(int variable_id, std::unordered_set<int>& visited_refs) const {
	for (const auto& graph_node : graph.nodes) {
		if (graph_node.type != nodes::node_type_v<nodes::variable_set_node_tag> || graph_node.custom_state.storage == nullptr)
			continue;
		const auto& set_state = graph_node.custom_state.as<nodes::variable_node_state>();
		if (set_state.variable_id != variable_id)
			continue;
		const NE_Link* value_link = find_input_link(graph_node, "value");
		if (value_link == nullptr)
			continue;
		resolved_value fallback = resolve_source_endpoint(value_link->from_node, value_link->from_pin, visited_refs);
		if (fallback.source_kind != value_source_kind::none)
			return fallback;
	}
	return {};
}

resolved_value graph_runtime::resolve_source_endpoint(int node_id, int pin_id, std::unordered_set<int>& visited_refs) const {
	const NE_Node* node = graph.find_node(node_id);
	if (node == nullptr)
		return make_resolved_status("Source node is missing.");

	if (node->type == nodes::node_type_v<nodes::variable_get_node_tag>) {
		if (node->custom_state.storage == nullptr)
			return make_resolved_status("Variable Get state is missing.");
		const auto& state = node->custom_state.as<nodes::variable_node_state>();
		if (state.variable_id == -1)
			return make_resolved_status("Variable Get has no shared variable selected.");
		if (!visited_refs.insert(state.variable_id).second)
			return make_resolved_status("Variable cycle detected.");
		const nodes::variable_slot_state* slot = graph.find_variable_slot(state.variable_id);
		if (slot == nullptr)
			return make_resolved_status("Variable is missing.");
		const int slot_index = global_slot(state.variable_id);
		if (slot_index == -1 || static_cast<size_t>(slot_index) >= global_slot_values.size())
			return make_resolved_status("Variable has no compiled stack slot.");
		const slot_value& value = global_slot_values[static_cast<size_t>(slot_index)];
		if (value.initialized) {
			resolved_value resolved;
			if (value.element_count > 0) {
				std::any any_value;
				const stack_slot* slot_meta = stack_slot_meta(plan.stack, slot_index);
				if (slot_meta != nullptr && any_from_slot_value(value, *slot_meta, 0u, any_value, resolved.status) &&
					any_to_inline_cpu_payload(any_value, slot->type_hash, slot->is_container, resolved.inline_bytes, resolved.inline_element_count)) {
					resolved.source_kind = value_source_kind::inline_cpu;
					return resolved;
				}
			}
		}
		if (resolved_value fallback = resolve_variable_set_source(state.variable_id, visited_refs); fallback.source_kind != value_source_kind::none)
			return fallback;
		return make_resolved_status(value.status.empty() ? "Variable is unset." : value.status);
	}
	const vm_stack* stack = stack_for_function(node->function_id);
	if (stack == nullptr)
		return make_resolved_status("Compiled function frame is missing.");
	const pin_key key = make_pin_key(node_id, pin_id);
	const auto slot_it = stack->output_slot_by_pin.find(key);
	if (slot_it == stack->output_slot_by_pin.end()) {
		resolved_value resolved;
		resolved.source_kind = value_source_kind::endpoint;
		resolved.source_node_id = node_id;
		resolved.source_pin_id = pin_id;
		return resolved;
	}

	const runtime_detail::frame* value_frame = frame_for_node(*node);
	const slot_value* slot_value_ptr = value_frame == nullptr ? nullptr : value_frame->slot(slot_it->second);
	if (slot_value_ptr != nullptr && slot_value_ptr->initialized && slot_value_ptr->element_count > 0) {
		const stack_slot* slot_meta = stack_slot_meta(*stack, slot_it->second);
		const NE_Pin* output_pin = nodes::find_pin_by_id(node->outputs, pin_id);
		resolved_value resolved;
		std::any any_value;
		if (slot_meta != nullptr && output_pin != nullptr &&
			any_from_slot_value(*slot_value_ptr, *slot_meta, 0u, any_value, resolved.status) &&
			any_to_inline_cpu_payload(any_value, output_pin->type_hash, output_pin->is_container, resolved.inline_bytes, resolved.inline_element_count)) {
			resolved.source_kind = value_source_kind::inline_cpu;
			return resolved;
		}
	}

	resolved_value resolved;
	resolved.source_kind = value_source_kind::endpoint;
	resolved.source_node_id = node_id;
	resolved.source_pin_id = pin_id;
	return resolved;
}

resolved_value graph_runtime::resolve_current_input_source(const NE_Node& node, std::string_view input_label, size_t current_item_index, size_t current_item_count) const {
	const int source_slot = input_source_slot(node, input_label);
	if (source_slot == -1) {
		const NE_Pin* current_pin = nodes::find_pin_by_label(node.inputs, input_label);
		if (current_pin != nullptr) {
			for (const auto& inline_value : node.inline_input_values) {
				if (inline_value.label != input_label || !inline_value.enabled || inline_value.json.empty())
					continue;

				std::any any_value;
				if (!parse_inline_input_json(graph.node_registry(), inline_value.json, current_pin->type_hash, current_pin->is_container, any_value)) {
					return make_resolved_status("Inline input '" + std::string(input_label) + "' has an incompatible value: " + inline_value.json);
				}

				resolved_value resolved;
				if (!any_to_inline_cpu_payload(any_value, current_pin->type_hash, current_pin->is_container, resolved.inline_bytes, resolved.inline_element_count)) {
					return make_resolved_status("Inline input '" + std::string(input_label) + "' could not be converted to a CPU payload.");
				}

				resolved.source_kind = value_source_kind::inline_cpu;
				resolved.source_item_index = 0u;
				resolved.status = "Using inline input.";
				return resolved;
			}
		}
		return make_resolved_status("Missing input '" + std::string(input_label) + "'.");
	}

	frame* stack = const_cast<graph_runtime*>(this)->frame_for_node(node);
	const vm_stack* vm_stack = stack ? stack->stack : stack_for_function(node.function_id);
	const stack_slot* slot_meta = vm_stack ? stack_slot_meta(*vm_stack, source_slot) : nullptr;
	const slot_route* route = vm_stack ? slot_route_meta(*vm_stack, source_slot) : nullptr;
	if (stack == nullptr || slot_meta == nullptr || route == nullptr)
		return make_resolved_status("Compiled source slot is missing.");

	slot_value* runtime_value = stack->slot(source_slot);
	if (runtime_value == nullptr)
		return make_resolved_status("Compiled source slot runtime storage is missing.");
	auto try_global_set_fallback = [&]() -> resolved_value {
		if (route->kind != slot_route_kind::global_variable || route->variable_id == -1)
			return {};
		std::unordered_set<int> visited_refs;
		return resolve_variable_set_source(route->variable_id, visited_refs);
	};
	if (route->kind == slot_route_kind::node_output) {
		if (NE_Node* source_node = graph.find_node(route->node_id); source_node != nullptr) {
			const NodeTypeInfo* source_info = registry.find(source_node->type);
			if (source_info != nullptr && source_info->meta.is_vm_pure) {
				graph_execution_context ctx = const_cast<graph_runtime*>(this)->make_vm_context();
				std::string error;
				if (!const_cast<graph_runtime*>(this)->ensure_vm_node_executed(ctx, *source_node, error))
					return make_resolved_status(error.empty() ? ("Failed to evaluate VM node '" + source_node->title + "'.") : error);
				runtime_value = stack->slot(source_slot);
			}
		}
	}

	if (runtime_value != nullptr && runtime_value->initialized) {
		if (!stack_slot_is_container(*slot_meta) && runtime_value->element_count == 0) {
			if (resolved_value fallback = try_global_set_fallback(); fallback.source_kind != value_source_kind::none)
				return fallback;
			return make_resolved_status("Compiled source slot has no values.");
		}
		if (!stack_slot_is_container(*slot_meta) &&
			runtime_value->element_count != 1u &&
			runtime_value->element_count != current_item_count) {
			return make_resolved_status(
				"Input source batch size " + std::to_string(runtime_value->element_count) +
				" does not match current execution batch size " + std::to_string(current_item_count) + "."
			);
		}
		const size_t resolved_index = stack_slot_is_container(*slot_meta) || runtime_value->element_count == 1u ? 0u : current_item_index;
		if ((!stack_slot_is_container(*slot_meta) && resolved_index >= runtime_value->element_count) ||
			(stack_slot_is_container(*slot_meta) && resolved_index > 0u))
			return make_resolved_status("Compiled source slot item index is out of bounds.");

		resolved_value resolved;
		std::any any_value;
		if (any_from_slot_value(*runtime_value, *slot_meta, resolved_index, any_value, resolved.status) &&
			any_to_inline_cpu_payload(any_value, slot_meta->type.type_hash, stack_slot_is_container(*slot_meta), resolved.inline_bytes, resolved.inline_element_count)) {
				resolved.source_kind = value_source_kind::inline_cpu;
				resolved.source_item_index = resolved_index;
				resolved.status = runtime_value->status;
				return resolved;
		}

		if (route->kind == slot_route_kind::node_output) {
			resolved.source_kind = value_source_kind::endpoint;
			resolved.source_node_id = route->node_id;
			resolved.source_pin_id = route->pin_id;
			resolved.source_item_index = resolved_index;
			resolved.status = runtime_value->status;
			return resolved;
		}
		if (resolved_value fallback = try_global_set_fallback(); fallback.source_kind != value_source_kind::none)
			return fallback;
		return make_resolved_status(runtime_value->status.empty() ? "Compiled source slot is not CPU-copyable." : runtime_value->status);
	}

	if (route->kind == slot_route_kind::global_variable && route->variable_id != -1) {
		if (resolved_value fallback = try_global_set_fallback(); fallback.source_kind != value_source_kind::none)
			return fallback;
	}

	if (route->kind == slot_route_kind::node_output) {
		resolved_value resolved;
		resolved.source_kind = value_source_kind::endpoint;
		resolved.source_node_id = route->node_id;
		resolved.source_pin_id = route->pin_id;
		resolved.source_item_index = current_item_index;
		return resolved;
	}
	std::string owner;
	switch (route->kind) {
	case slot_route_kind::global_variable:
		owner = "global variable";
		break;
	case slot_route_kind::function_input:
		owner = "function input";
		break;
	case slot_route_kind::function_output:
		owner = "function output";
		break;
	case slot_route_kind::node_output:
	default:
		owner = "node output";
		break;
	}
	return make_resolved_status(std::format(
		"Compiled source slot is unset for input '{}' (slot {}, owner {}, label '{}').",
		input_label,
		source_slot,
		owner,
		route->label
	));
}

resolved_value graph_runtime::resolve_input_source(const NE_Node& node, std::string_view input_label) const {
	return resolve_current_input_source(node, input_label, 0u, 1u);
}

const NE_Node* graph_runtime::resolve_input_source_node(const NE_Node& node, std::string_view input_label, std::string& status) const {
	const int source_slot = input_source_slot(node, input_label);
	if (source_slot == -1) {
		status = "Missing input '" + std::string(input_label) + "'.";
		return nullptr;
	}
	const NE_Node* source = resolve_slot_source_node(node, source_slot, status);
	if (source != nullptr)
		return source;
	resolved_value resolved = resolve_current_input_source(node, input_label, 0u, 1u);
	if (resolved.source_kind != value_source_kind::endpoint) {
		status = resolved.status.empty() ? ("Input '" + std::string(input_label) + "' does not resolve to a runtime node source.") : resolved.status;
		return nullptr;
	}
	const NE_Node* source_node = graph.find_node(resolved.source_node_id);
	if (source_node == nullptr) {
		status = "Input '" + std::string(input_label) + "' resolved to missing node id " + std::to_string(resolved.source_node_id) + ".";
		return nullptr;
	}
	status.clear();
	return source_node;
}

bool graph_runtime::copy_variable_payload(const resolved_value& source, std::vector<std::byte>& bytes, size_t& element_count) const {
	if (source.source_kind == value_source_kind::inline_cpu) {
		bytes = source.inline_bytes;
		element_count = source.inline_element_count;
		return true;
	}
	if (source.source_kind != value_source_kind::endpoint)
		return false;
	const NE_Node* source_node = graph.find_node(source.source_node_id);
	if (source_node == nullptr)
		return false;
	const NE_Pin* source_pin = nodes::find_pin_by_id(source_node->outputs, source.source_pin_id);
	if (source_pin == nullptr)
		return false;

	if (const NodeTypeInfo* src_info = registry.find(source_node->type); src_info && src_info->hooks.get_cpu_output)
		return src_info->hooks.get_cpu_output(*source_node, source_pin->label, bytes, element_count);

	const value_batch* batch = find_vm_output(source.source_node_id, source_pin->label);
	if (batch == nullptr || batch->items.empty())
		return false;
	const size_t item_index = std::min(source.source_item_index, batch->items.size() - 1);
	return any_to_inline_cpu_payload(batch->items[item_index], source_pin->type_hash, source_pin->is_container, bytes, element_count);
}

bool runtime_detail::graph_executor::execute_build_steps(const mars::vector2<size_t>& frame_size, std::string& error) {
	if (owner == nullptr) {
		error = "Runtime executor is not attached.";
		return false;
	}

	owner->initialize_variable_defaults();
	graph_build_context build_ctx = owner->make_build_context(frame_size);

	for (size_t index = 0; index < owner->plan.build_steps.size(); ++index) {
		const function_step& step = owner->plan.build_steps[index];
		step_info& current = owner->steps[index];
		NE_Node* node = const_cast<NE_Node*>(step.node);
		const NodeTypeInfo* info = node == nullptr ? nullptr : owner->registry.find(node->type);
		if (node == nullptr || info == nullptr)
			continue;
		auto set_node_result = [&](bool _success) {
			node->has_run_result = true;
			node->last_run_success = _success;
			node->last_run_message = current.status;
		};
		auto fail_step = [&](bool _update_node_result, bool _fallback_unknown) {
			current.valid = false;
			owner->has_error = true;
			if (owner->last_error.empty())
				owner->last_error = current.label + ": " + current.status;
			mars::logger::error(
				g_app_log_channel,
				"Node '{}' failed: {}",
				current.label,
				_fallback_unknown && current.status.empty() ? std::string("Unknown error.") : current.status
			);
			if (_update_node_result)
				set_node_result(false);
			return false;
		};

		if (info->hooks.build_propagate) {
			current.kind = "Variable";
			std::string propagate_error;
			const bool ok = info->hooks.build_propagate(build_ctx, *node, propagate_error);
			current.executed_count = ok ? 1 : 0;
			current.status = ok ? "Propagated variable slot" : (propagate_error.empty() ? "Variable build propagation failed." : propagate_error);
			if (!ok)
				return fail_step(true, false);
			set_node_result(true);
			continue;
		}

		if (info->meta.is_vm_node && !info->hooks.build_execute) {
			current.kind = info->meta.is_vm_event ? "Event" : (info->meta.is_vm_callable ? "Callable" : "Pure");
			current.executed_count = 1;
			current.status = "Ready";
			set_node_result(true);
			continue;
		}

		if (info->meta.has_processor) {
			if (!info->hooks.execute_processor || !info->hooks.execute_processor(*node)) {
				current.status = node->last_run_message.empty() ? "Processor execution failed" : node->last_run_message;
				return fail_step(false, true);
			}
			if (info->hooks.get_cpu_output && !node->outputs.empty()) {
				std::vector<std::byte> count_bytes;
				size_t count = 0;
				info->hooks.get_cpu_output(*node, node->outputs[0].label, count_bytes, count);
				current.executed_count = count;
			}
			else {
				current.executed_count = 1;
			}
			current.status = node->last_run_message.empty() ? "Ready" : node->last_run_message;
			++owner->last_exec_instance_count;
			set_node_result(true);
			continue;
		}

		bool ok = true;
		if (info->hooks.build_execute) {
			graph_build_result result;
			std::string build_error;
			if (info->hooks.validate_node && !info->hooks.validate_node(owner->graph, *info, *node, build_error)) {
				ok = false;
				current.status = build_error;
			}
			else {
				ok = info->hooks.build_execute(build_ctx, *node, result, build_error);
				if (!result.kind.empty())
					current.kind = result.kind;
				current.executed_count = result.executed_count;
				current.status = !result.status.empty() ? result.status : build_error;
				if (!ok && current.status.empty()) {
					current.status =
						"Build failed without reporting an error "
						"(node='" + current.label +
						"', type=" + std::to_string(node->type) +
						", build_execute returned false).";
				}
			}
		}
		else {
			current.status = "Ready";
		}

		if (!ok)
			return fail_step(true, true);
		set_node_result(true);
	}

	error.clear();
	return true;
}

bool runtime_detail::graph_executor::ensure_built(const mars::vector2<size_t>& frame_size) {
	if (owner == nullptr || !owner->running)
		return false;
	if (!owner->dirty) {
		owner->last_build_rebuilt = false;
		return true;
	}

	owner->last_build_rebuilt = true;
	owner->dirty = false;

	mars::graphics::device_flush(owner->device);
	owner->destroy_all();
	for (auto& node : owner->graph.nodes) {
		node.has_run_result = false;
		node.last_run_success = false;
		node.last_run_message.clear();
	}

	std::string error;
	if (!owner->builder.compile(frame_size, error)) {
		owner->has_error = true;
		owner->last_error = error.empty() ? "Failed to compile the graph runtime." : error;
		return false;
	}
	if (!execute_build_steps(frame_size, error)) {
		owner->has_error = true;
		if (owner->last_error.empty())
			owner->last_error = error.empty() ? "Failed to execute compiled build steps." : error;
		return false;
	}

	for (const auto& step : owner->steps)
		if (step.kind == "Render Pass" || step.kind == "Pipeline" || step.kind == "Command" || step.kind == "Swapchain")
			++owner->last_gpu_step_count;

	return true;
}

void runtime_detail::graph_executor::record_pre_swapchain(const mars::command_buffer& cmd, size_t current_frame) {
	if (owner == nullptr)
		return;
	graph_runtime& runtime = *owner;
	if (!runtime.running || runtime.has_error)
		return;

	if (runtime.global_slot_values.empty())
		runtime.initialize_variable_defaults();

	for (auto& [_, owned] : runtime.shared_texture_slot_resources) {
		auto& resources = owned.as<owned_shared_resource<texture_slot_resources>>().value;
		if (!resources.valid || !resources.pending_upload || !resources.texture.engine)
			continue;
		mars::buffer dummy = {};
		mars::graphics::texture_copy(resources.texture, dummy, cmd, 0u);
		mars::graphics::texture_transition(cmd, resources.texture, mars::MARS_TEXTURE_STATE_COPY_DST, mars::MARS_TEXTURE_STATE_SHADER_READ);
		resources.pending_upload = false;
	}
	runtime.reset_record_state(cmd, current_frame);
	graph_execution_context ctx = runtime.make_vm_context();
	std::unordered_set<int> executed_nodes;
	const NE_Node* setup_start = runtime.function_start_node(runtime.graph.setup_function_id());
	const NE_Node* render_start = runtime.function_start_node(runtime.graph.render_function_id());
	if (setup_start == nullptr || render_start == nullptr) {
		runtime.has_error = true;
		runtime.last_error = "Missing setup or render start node.";
		runtime.finish_record_state();
		return;
	}

	if (runtime.setup_pending) {
		runtime.execute_exec_chain_from(setup_start->id, ctx, 1u, executed_nodes);
		runtime.setup_pending = false;
	}
	if (!runtime.has_error)
		runtime.execute_exec_chain_from(render_start->id, ctx, 1u, executed_nodes);
	if (runtime.has_error)
		runtime.setup_pending = true;
	if (runtime.has_error)
		runtime.last_error = runtime.last_error.empty() ? "Function execution failed." : runtime.last_error;
	if (runtime.render.active_render_pass != nullptr)
		mars::graphics::render_pass_unbind(runtime.render.active_render_pass->render_pass, cmd);
	runtime.finish_record_state();
}

} // namespace rv
