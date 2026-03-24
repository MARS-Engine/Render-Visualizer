#pragma once

#include <render_visualizer/node_graph.hpp>

#include <any>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

#include <mars/utility/enum_flags.hpp>

namespace rv::runtime_detail {

enum class value_source_kind {
	none,
	endpoint,
	inline_cpu
};

struct stored_value {
	value_source_kind source_kind = value_source_kind::none;
	int source_node_id = -1;
	int source_pin_id = -1;
	size_t source_item_index = 0;
	std::vector<std::byte> inline_bytes;
	size_t inline_element_count = 0;
	std::string status;

	void reset();
};

struct resolved_value {
	value_source_kind source_kind = value_source_kind::none;
	int source_node_id = -1;
	int source_pin_id = -1;
	size_t source_item_index = 0;
	std::vector<std::byte> inline_bytes;
	size_t inline_element_count = 0;
	std::string status;
};

struct value_batch {
	std::vector<std::any> items;

	size_t size() const { return items.size(); }
	bool empty() const { return items.empty(); }
};

using pin_key = std::uint64_t;

enum class slot_route_kind {
	none,
	global_variable,
	function_input,
	function_output,
	node_output
};

enum class stack_slot_flags : std::uint32_t {
	none = 0u,
	batched = 1u << 0,
	container = 1u << 1,
	virtual_struct = 1u << 2,
	persistent_global = 1u << 3,
	resource = 1u << 4,
};

struct slot_type_info {
	size_t type_hash = 0;
	std::string_view reflected_name;
	size_t value_size = 0;
};

struct stack_slot {
	slot_type_info type;
	stack_slot_flags flags = stack_slot_flags::none;
	std::string virtual_struct_name;
	size_t virtual_struct_layout_fingerprint = 0;
};

struct slot_route {
	slot_route_kind kind = slot_route_kind::none;
	int variable_id = -1;
	int node_id = -1;
	int pin_id = -1;
	std::string label;
};

struct function_stack_view {
	size_t input_slot_count = 0;
	size_t output_slot_count = 0;
	std::vector<int> owned_slots;
	std::unordered_map<std::string, int> input_slots_by_label;
	std::unordered_map<std::string, int> output_slots_by_label;
};

struct vm_stack {
	size_t global_slot_count = 0;
	std::vector<stack_slot> slots;
	std::unordered_map<int, int> global_slot_by_variable_id;
	std::unordered_map<pin_key, int> input_source_slot_by_pin;
	std::unordered_map<pin_key, int> output_slot_by_pin;
	std::unordered_map<int, slot_route> routes_by_slot;
	std::unordered_map<int, function_stack_view> functions;
};

struct pin_binding {
	int pin_id = -1;
	std::string label;
	int slot_index = -1;
};

struct exec_step_edge {
	int exec_pin_id = -1;
	int next_step_index = -1;
};

struct function_step {
	const NE_Node* node = nullptr;
	std::vector<pin_binding> inputs;
	std::vector<pin_binding> outputs;
	std::vector<exec_step_edge> next_steps;
};

struct function_plan {
	const NE_Node* start_node = nullptr;
	const NE_Node* outputs_node = nullptr;
	int entry_step_index = -1;
	std::vector<function_step> steps;
	std::unordered_map<int, int> step_index_by_node_id;
};

struct graph_execution_plan {
	int root_node_id = -1;
	vm_stack stack;
	std::vector<function_step> build_steps;
	std::unordered_map<int, function_plan> function_plans;
};

} // namespace rv::runtime_detail

template <>
struct mars::enum_flags::enabled<rv::runtime_detail::stack_slot_flags> : std::true_type {};
