#include <render_visualizer/runtime/frame.hpp>

namespace rv::runtime_detail {

void stored_value::reset() {
	source_kind = value_source_kind::none;
	source_node_id = -1;
	source_pin_id = -1;
	source_item_index = 0;
	inline_bytes.clear();
	inline_element_count = 0;
	status.clear();
}

void slot_value::reset() {
	initialized = false;
	element_count = 0;
	element_stride = 0;
	bytes.clear();
	status.clear();
}

void frame::reset(int next_function_id, const vm_stack* next_stack, std::vector<slot_value>& shared_globals) {
	function_id = next_function_id;
	stack = next_stack;
	global_values = &shared_globals;
	returned = false;
	const size_t slot_count = stack ? stack->slots.size() : 0u;
	local_values.assign(slot_count, {});
}

slot_value* frame::slot(int slot_index) {
	if (stack == nullptr || slot_index < 0)
		return nullptr;
	if (static_cast<size_t>(slot_index) < stack->global_slot_count) {
		if (global_values == nullptr || static_cast<size_t>(slot_index) >= global_values->size())
			return nullptr;
		return &(*global_values)[static_cast<size_t>(slot_index)];
	}
	if (static_cast<size_t>(slot_index) >= local_values.size())
		return nullptr;
	return &local_values[static_cast<size_t>(slot_index)];
}

const slot_value* frame::slot(int slot_index) const {
	if (stack == nullptr || slot_index < 0)
		return nullptr;
	if (static_cast<size_t>(slot_index) < stack->global_slot_count) {
		if (global_values == nullptr || static_cast<size_t>(slot_index) >= global_values->size())
			return nullptr;
		return &(*global_values)[static_cast<size_t>(slot_index)];
	}
	if (static_cast<size_t>(slot_index) >= local_values.size())
		return nullptr;
	return &local_values[static_cast<size_t>(slot_index)];
}

} // namespace rv::runtime_detail
