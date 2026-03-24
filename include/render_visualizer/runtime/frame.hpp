#pragma once

#include <render_visualizer/runtime/stack.hpp>

namespace rv::runtime_detail {

struct slot_value {
	bool initialized = false;
	size_t element_count = 0;
	size_t element_stride = 0;
	std::vector<std::byte> bytes;
	std::string status;

	void reset();
};

struct frame {
	int function_id = -1;
	const vm_stack* stack = nullptr;
	std::vector<slot_value>* global_values = nullptr;
	std::vector<slot_value> local_values;
	bool returned = false;

	void reset(int next_function_id, const vm_stack* next_stack, std::vector<slot_value>& shared_globals);
	slot_value* slot(int slot_index);
	const slot_value* slot(int slot_index) const;
};

} // namespace rv::runtime_detail
