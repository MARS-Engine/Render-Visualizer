#pragma once

#include <any>
#include <deque>
#include <string_view>

#include <render_visualizer/main_pass.hpp>
#include <render_visualizer/node_graph.hpp>
#include <render_visualizer/runtime/services.hpp>
#include <render_visualizer/runtime/resource_type_map.hpp>

#include <mars/graphics/backend/command_pool.hpp>
#include <mars/graphics/object/main_pass_object.hpp>
#include <mars/graphics/object/pass_scope.hpp>
#include <mars/graphics/object/swapchain.hpp>

namespace rv {

namespace runtime_detail {

struct graph_builder {
	graph_runtime* owner = nullptr;

	bool compile(const mars::vector2<size_t>& frame_size, std::string& error);
};

struct graph_executor {
	graph_runtime* owner = nullptr;

	bool ensure_built(const mars::vector2<size_t>& frame_size);
	bool execute_build_steps(const mars::vector2<size_t>& frame_size, std::string& error);
	void record_pre_swapchain(const mars::command_buffer& cmd, size_t current_frame);
};

} // namespace runtime_detail

class graph_runtime {
  public:
	NodeGraph& graph;
	const NodeRegistry& registry;
	mars::device device;
	mars::graphics::object::main_pass_object<main_pass_desc>& main_pass;
	mars::graphics::object::swapchain& swapchain;

	bool running = false;
	bool dirty = true;
	bool last_build_rebuilt = false;
	bool has_error = false;
	float frame_delta_time = 0.0f;
	graph_blackboard blackboard;
	size_t last_gpu_step_count = 0;
	size_t last_exec_instance_count = 0;
	size_t last_skipped_instance_count = 0;
	std::string last_error;
	std::vector<runtime_detail::step_info> steps;
	runtime_detail::graph_builder builder;
	runtime_detail::graph_executor executor;

	runtime_detail::render_state render;
	graph_services build_services;
	std::deque<NE_RuntimeValue> owned_resources;
	std::unordered_map<int, NE_RuntimeValue> shared_texture_slot_resources;
	std::unordered_map<const void*, runtime_detail::present_resources> present_by_pass;

	runtime_detail::graph_execution_plan plan;
	std::vector<runtime_detail::slot_value> global_slot_values;
	std::unordered_map<int, runtime_detail::frame> frames_by_function;
	std::unordered_map<int, runtime_detail::stored_value> variable_values;
	std::unordered_map<int, std::unordered_map<std::string, runtime_detail::value_batch>> vm_outputs;
	std::unordered_set<int> vm_evaluating;
	std::vector<size_t> pending_vm_event_types;
	std::vector<runtime_detail::frame> call_frames;
	std::unordered_map<int, size_t> active_function_call_counts;
	bool setup_pending = true;
	void* current_destroying_resource = nullptr;
	int current_destroying_node_id = -1;
	size_t current_destroying_node_type = 0;

	graph_runtime(
		NodeGraph& graph_ref,
		const NodeRegistry& registry_ref,
		const mars::device& device_ref,
		mars::graphics::object::main_pass_object<main_pass_desc>& main_pass_ref,
		mars::graphics::object::swapchain& swapchain_ref
	);
	~graph_runtime();

	graph_runtime(const graph_runtime&) = delete;
	graph_runtime& operator=(const graph_runtime&) = delete;
	graph_runtime(graph_runtime&&) = delete;
	graph_runtime& operator=(graph_runtime&&) = delete;

	void attach_graph_callbacks();
	void mark_dirty();
	void start();
	void stop();
	bool is_running() const { return running; }
	bool is_dirty() const { return dirty; }
	void destroy_resources();
	void destroy_all();

	std::vector<const NE_Link*> incoming_exec_links(int node_id) const;
	std::vector<const NE_Link*> outgoing_exec_links(const NE_Node& node) const;
	bool build_order_from_start(int node_id, std::unordered_set<int>& visited, std::unordered_map<int, bool>& reaches_end_cache, std::vector<int>& ordered_nodes, int& root_node_id);
	const NE_Node* root_node() const;
	const NE_Node* function_start_node(int function_id) const;
	const NE_Node* function_outputs_node(int function_id) const;
	const NE_Link* find_input_link(const NE_Node& node, std::string_view input_label) const;
	const NE_Node* find_linked_node(const NE_Node& node, std::string_view input_label) const;

	bool compile_function_layouts(std::string& error);
	static runtime_detail::pin_key make_pin_key(int node_id, int pin_id);
	const runtime_detail::vm_stack* stack_for_function(int function_id) const;
	const runtime_detail::function_plan* function_plan(int function_id) const;
	const runtime_detail::function_step* function_step_for_node(const NE_Node& node) const;
	int global_slot(int variable_id) const;
	int input_source_slot(const NE_Node& node, std::string_view input_label) const;
	int output_slot(const NE_Node& node, std::string_view output_label) const;
	const runtime_detail::stack_slot* stack_slot_meta(const runtime_detail::vm_stack& stack, int slot_index) const;
	const runtime_detail::slot_route* slot_route_meta(const runtime_detail::vm_stack& stack, int slot_index) const;

	runtime_detail::frame* current_frame();
	const runtime_detail::frame* current_frame() const;
	runtime_detail::frame* frame_for_node(const NE_Node& node);
	const runtime_detail::frame* frame_for_node(const NE_Node& node) const;
	runtime_detail::frame& ensure_function_frame(int function_id);
	bool slot_has_value(const NE_Node& node, int slot_index) const;
	bool read_slot_value(const NE_Node& node, int slot_index, size_t expected_type_hash, bool expected_container, size_t current_item_index, size_t current_item_count, std::any& out_value, std::string& error);
	bool read_slot_wildcard_value(const NE_Node& node, int slot_index, size_t current_item_index, size_t current_item_count, nodes::wildcard_value& out_value, std::string& error);
	bool write_output_slot_value(const NE_Node& node, std::string_view label, std::any value, std::string& error);
	bool copy_slot_value(const NE_Node& node, int source_slot_index, runtime_detail::slot_value& out_value, std::string& error);
	void clear_output_slot(const NE_Node& node, std::string_view label);
	bool append_output_slot_value(const NE_Node& node, std::string_view label, std::any value, std::string& error);
	bool write_slot_value(int function_id, int slot_index, const runtime_detail::slot_value& value, std::string& error);
	const NE_Node* resolve_slot_source_node(const NE_Node& node, int slot_index, std::string& status) const;
	runtime_detail::resolved_value resolve_variable_set_source(int variable_id, std::unordered_set<int>& visited_refs) const;
	bool push_frame(int function_id, std::string& error);
	void pop_frame();

	runtime_detail::value_batch* find_vm_output(int node_id, std::string_view label);
	const runtime_detail::value_batch* find_vm_output(int node_id, std::string_view label) const;
	size_t vm_output_size(int node_id) const;

	template <size_t Index>
	bool read_blackboard_member(std::string_view name, size_t expected_type_hash, std::any& out_value, std::string& error) const {
		constexpr auto ctx = std::meta::access_context::current();
		constexpr auto MemberInfo = std::define_static_array(std::meta::nonstatic_data_members_of(^^graph_blackboard, ctx))[Index];
		using member_t = typename[:std::meta::type_of(MemberInfo):];
		if (name != std::define_static_string(std::meta::identifier_of(MemberInfo)))
			return false;
		if (expected_type_hash != rv::detail::pin_type_hash<member_t>()) {
			error = "Blackboard value '" + std::string(name) + "' was requested with the wrong type.";
			return true;
		}
		out_value = blackboard.[:MemberInfo:];
		return true;
	}

	template <size_t Index>
	bool write_blackboard_member(std::string_view name, size_t expected_type_hash, const std::any& value, std::string& error) {
		constexpr auto ctx = std::meta::access_context::current();
		constexpr auto MemberInfo = std::define_static_array(std::meta::nonstatic_data_members_of(^^graph_blackboard, ctx))[Index];
		using member_t = typename[:std::meta::type_of(MemberInfo):];
		if (name != std::define_static_string(std::meta::identifier_of(MemberInfo)))
			return false;
		if (expected_type_hash != rv::detail::pin_type_hash<member_t>() || value.type() != typeid(member_t)) {
			error = "Blackboard write '" + std::string(name) + "' used the wrong type.";
			return true;
		}
		blackboard.[:MemberInfo:] = std::any_cast<member_t>(value);
		return true;
	}

	bool read_blackboard_value(std::string_view name, size_t expected_type_hash, std::any& out_value, std::string& error) const;
	bool write_blackboard_value(std::string_view name, size_t expected_type_hash, const std::any& value, std::string& error);
	bool set_blackboard_value(std::string_view name, size_t type_hash, std::any value);
	template <typename T>
	bool set_blackboard(std::string_view name, const T& value) {
		return set_blackboard_value(name, rv::detail::pin_type_hash<T>(), std::any(value));
	}
	void queue_event(size_t node_type) { pending_vm_event_types.push_back(node_type); }

	graph_build_context make_build_context(const mars::vector2<size_t>& frame_size);
	void reset_record_state(const mars::command_buffer& cmd, size_t current_frame);
	void finish_record_state();
	bool execute_begin_render_pass(const NE_Node& node, std::string& error);
	bool execute_bind_pipeline(const NE_Node& node, std::string& error);
	bool execute_bind_resources(const NE_Node& node, std::string& error);
	bool execute_bind_vertex_buffers(const NE_Node& node, std::string& error);
	bool execute_bind_index_buffer(const NE_Node& node, std::string& error);
	bool execute_draw(graph_execution_context& ctx, const NE_Node& node, std::string& error);
	bool execute_draw_indexed(graph_execution_context& ctx, const NE_Node& node, std::string& error);
	void clear_vm_outputs(int node_id);
	void begin_vm_output(const NE_Node& node, std::string_view label);
	void store_vm_output(const NE_Node& node, std::string_view label, std::any value);
	bool pull_vm_wildcard_input(const NE_Node& node, std::string_view input_label, size_t current_item_index, size_t current_item_count, nodes::wildcard_value& out_value, std::string& error);
	bool store_vm_wildcard_output(const NE_Node& node, std::string_view label, const nodes::wildcard_value& value, std::string& error);
	bool ensure_vm_node_executed(graph_execution_context& ctx, NE_Node& node, std::string& error);
	size_t determine_vm_node_batch_size(NE_Node& node, std::string& error);
	bool execute_vm_node_batch(graph_execution_context& ctx, NE_Node& node, size_t batch_size, std::string& error, size_t* outgoing_batch_size = nullptr);
	bool execute_call_function(NE_Node& node, std::string& error);
	bool read_function_input(const NE_Node& node, std::string_view label, std::any& out_value, std::string& error);
	bool write_function_output(const NE_Node& node, std::string_view label, std::any value, std::string& error);
	bool pull_vm_input(const NE_Node& node, std::string_view input_label, size_t expected_type_hash, bool expected_container, size_t current_item_index, size_t current_item_count, std::any& out_value, std::string& error);
	const NE_Link* find_exec_input_link(const NE_Node& node) const;
	bool is_render_command_node(const NE_Node& node) const;
	bool is_exec_node(const NE_Node& node) const;
	bool execute_dynamic_uniform(const NE_Node& node, std::string& error);
	graph_execution_context make_vm_context();
	bool execute_exec_chain_for_item(int node_id, graph_execution_context& ctx, size_t item_index, size_t item_count, std::unordered_set<int>& recursion_stack);
	bool execute_exec_chain_itemwise_from(int node_id, graph_execution_context& ctx, size_t item_count);
	void execute_exec_chain_from(int node_id, graph_execution_context& ctx, size_t incoming_batch_size, std::unordered_set<int>& executed_nodes);
	void execute_vm_chain_from(int node_id, graph_execution_context& ctx, size_t incoming_batch_size);
	void dispatch_vm_event_type(size_t event_type);

	template <typename T>
	bool parse_ref_default_bytes(std::string_view json, std::vector<std::byte>& bytes) const {
		T value {};
		if (!nodes::json_parse(json, value))
			return false;
		bytes.resize(sizeof(T));
		if (!bytes.empty())
			std::memcpy(bytes.data(), &value, sizeof(T));
		return true;
	}

	bool initialize_variable_defaults();
	bool initialize_ref_defaults();
	bool ensure_uniform_payload(runtime_detail::uniform_buffer_resources& resources, const std::vector<std::byte>& payload, std::string& error);
	bool load_texture_slot_from_file(runtime_detail::texture_slot_resources& resources, const std::string& source_path, std::string& status);
	bool any_from_inline_cpu_payload(size_t type_hash, bool is_container, const std::vector<std::byte>& bytes, size_t element_count, std::any& value) const;
	bool any_to_inline_cpu_payload(const std::any& value, size_t type_hash, bool is_container, std::vector<std::byte>& bytes, size_t& element_count) const;
	bool any_from_slot_value(const runtime_detail::slot_value& slot_value, const runtime_detail::stack_slot& slot, size_t item_index, std::any& value, std::string& error) const;
	bool any_to_slot_value(const std::any& value, const runtime_detail::stack_slot& slot, runtime_detail::slot_value& out_value, bool append, std::string& error) const;
	bool read_variable_value(int variable_id, size_t expected_type_hash, bool expected_container, std::any& out_value, std::string& error) const;
	bool write_variable_input_value(const NE_Node& node, int variable_id, std::string_view input_label, size_t current_item_index, size_t current_item_count, std::string& error);
	runtime_detail::resolved_value resolve_source_endpoint(int node_id, int pin_id, std::unordered_set<int>& visited_refs) const;
	runtime_detail::resolved_value resolve_current_input_source(const NE_Node& node, std::string_view input_label, size_t current_item_index, size_t current_item_count) const;
	runtime_detail::resolved_value resolve_input_source(const NE_Node& node, std::string_view input_label) const;
	const NE_Node* resolve_input_source_node(const NE_Node& node, std::string_view input_label, std::string& status) const;
	bool copy_variable_payload(const runtime_detail::resolved_value& source, std::vector<std::byte>& bytes, size_t& element_count) const;

	template <typename T>
	T* read_input_resource(const NE_Node& node, std::string_view input_label, std::string& error) const;
	template <typename T>
	T* read_output_resource(const NE_Node& node, std::string_view output_label, std::string& error) const;
	template <typename T>
	bool store_output_resource(const NE_Node& node, std::string_view output_label, T* resource, std::string& error) {
		return write_output_slot_value(node, output_label, std::any(resource), error);
	}
	template <typename T>
	void store_node_resource(NE_Node& node, T* resource) {
		node.runtime_value.reset();
		node.runtime_value = NE_RuntimeValue::make<T*>();
		node.runtime_value.as<T*>() = resource;
	}
	template <typename T>
	T* read_node_resource(const NE_Node& node) const {
		if (node.runtime_value.storage == nullptr)
			return nullptr;
		return node.runtime_value.as<T*>();
	}
	runtime_detail::texture_slot_resources* shared_texture_slot_resource(int slot_id);
	const runtime_detail::texture_slot_resources* shared_texture_slot_resource(int slot_id) const;
	bool ensure_texture_slot_resource(int slot_id, std::string& status);
	runtime_detail::present_resources* ensure_present_resources(const mars::render_pass& render_pass);
	const runtime_detail::framebuffer_attachment_resources* resolve_texture_source_from_node(const NE_Node& node) const;
	const runtime_detail::framebuffer_attachment_resources* find_present_source_resources(const NE_Node& end_node) const;
	const runtime_detail::framebuffer_attachment_resources* find_present_source_resources() const;
	bool ensure_present_output(const NE_Node& node) {
		return ensure_present_resources(main_pass.get_render_pass()) != nullptr &&
			find_present_source_resources(node) != nullptr;
	}

	bool build_plan(const mars::vector2<size_t>& frame_size) {
		std::string error;
		return builder.compile(frame_size, error);
	}
	bool ensure_built(const mars::vector2<size_t>& frame_size) { return executor.ensure_built(frame_size); }
	void tick(float delta_time);
	void record_pre_swapchain(const mars::command_buffer& cmd, size_t current_frame) { executor.record_pre_swapchain(cmd, current_frame); }
	void record_preview(mars::raster_scope<main_pass_desc>& scope, const mars::vector2<size_t>& frame_size);
	void record_swapchain(mars::raster_scope<main_pass_desc>& scope, const mars::vector2<size_t>& frame_size) { record_preview(scope, frame_size); }

	template <typename T>
	T& add_owned_node_resource(const NE_Node& node, T resources) {
		NE_RuntimeValue entry = NE_RuntimeValue::make<runtime_detail::owned_node_resource<T>>();
		auto& owned = entry.as<runtime_detail::owned_node_resource<T>>();
		owned.runtime = this;
		owned.node_id = node.id;
		owned.node_type = node.type;
		owned.value = std::move(resources);
		T* resource_ptr = &owned.value;
		owned_resources.push_back(std::move(entry));
		return *resource_ptr;
	}

	template <typename T>
	T* current_destroying_resource_for(int node_id, size_t node_type) {
		if (current_destroying_resource == nullptr || current_destroying_node_id != node_id || current_destroying_node_type != node_type)
			return nullptr;
		return static_cast<T*>(current_destroying_resource);
	}

	template <typename T>
	void destroy_owned_node_resource(runtime_detail::owned_node_resource<T>& owned) {
		current_destroying_resource = &owned.value;
		current_destroying_node_id = owned.node_id;
		current_destroying_node_type = owned.node_type;

		bool handled = false;
		if (NE_Node* node = graph.find_node(owned.node_id); node != nullptr) {
			if (const NodeTypeInfo* info = registry.find(owned.node_type); info != nullptr && info->hooks.destroy_execute) {
				info->hooks.destroy_execute(build_services, *node);
				handled = true;
			}
		}
		if (!handled)
			owned.value.destroy(device);

		current_destroying_resource = nullptr;
		current_destroying_node_id = -1;
		current_destroying_node_type = 0;
	}
};

template <typename T>
T& publish_owned_resource(graph_services& services, const NE_Node& node, T resources) {
	return services.runtime->add_owned_node_resource<T>(node, std::move(resources));
}

template <typename T>
bool store_output_resource(graph_services& services, const NE_Node& node, std::string_view output_label, T* resource, std::string& error) {
	return services.runtime->store_output_resource(node, output_label, resource, error);
}

template <typename T>
bool store_output_value(graph_services& services, const NE_Node& node, std::string_view output_label, T value, std::string& error) {
	if (services.runtime == nullptr) {
		error = "Build context has no runtime services.";
		return false;
	}
	return services.runtime->write_output_slot_value(node, output_label, std::any(std::move(value)), error);
}

template <typename T>
void store_node_resource(graph_services& services, NE_Node& node, T* resource) {
	services.runtime->store_node_resource(node, resource);
}

template <typename T>
T* read_input_resource(graph_services& services, const NE_Node& node, std::string_view input_label, std::string& error) {
	return services.runtime->read_input_resource<T>(node, input_label, error);
}

template <typename T>
T* read_output_resource(graph_services& services, const NE_Node& node, std::string_view output_label, std::string& error) {
	return services.runtime->read_output_resource<T>(node, output_label, error);
}

template <typename T>
T* current_owned_resource(graph_services& services, const NE_Node& node) {
	if (services.runtime == nullptr)
		return nullptr;
	return services.runtime->current_destroying_resource_for<T>(node.id, node.type);
}

template <typename T>
void destroy_current_owned_resource(graph_services& services, NE_Node& node) {
	if (T* resource = current_owned_resource<T>(services, node); resource != nullptr)
		resource->destroy(*services.device);
}

namespace runtime_detail {

template <typename T>
inline owned_node_resource<T>::~owned_node_resource() {
	if (runtime != nullptr)
		runtime->destroy_owned_node_resource(*this);
}

} // namespace runtime_detail

template <typename T>
inline T* graph_runtime::read_input_resource(const NE_Node& node, std::string_view input_label, std::string& error) const {
	const int source_slot = input_source_slot(node, input_label);
	if (source_slot == -1) {
		error = "Missing input '" + std::string(input_label) + "'.";
		return nullptr;
	}
	const runtime_detail::frame* stack = frame_for_node(node);
	const runtime_detail::slot_value* runtime_value = stack == nullptr ? nullptr : stack->slot(source_slot);
	const runtime_detail::vm_stack* vm_stack = stack == nullptr ? nullptr : stack->stack;
	const runtime_detail::stack_slot* slot_meta = vm_stack == nullptr ? nullptr : stack_slot_meta(*vm_stack, source_slot);
	if (runtime_value == nullptr || !runtime_value->initialized) {
		error = "Runtime resource input '" + std::string(input_label) + "' is unset.";
		return nullptr;
	}
	auto slot_matches_requested_type = [&]() {
		if (slot_meta == nullptr)
			return false;
		constexpr size_t expected_hash = rv::runtime_detail::resource_impl_tag_hash_v<T>;
		return expected_hash != 0 && slot_meta->type.type_hash == expected_hash;
	};
	if (!slot_matches_requested_type()) {
		error = "Runtime resource input '" + std::string(input_label) + "' has the wrong resource category.";
		return nullptr;
	}
	if (runtime_value->element_count == 0 || runtime_value->bytes.size() < sizeof(T*)) {
		error = "Runtime resource input '" + std::string(input_label) + "' has no value.";
		return nullptr;
	}
	T* resource = nullptr;
	std::memcpy(&resource, runtime_value->bytes.data(), sizeof(T*));
	if (resource == nullptr || !resource->valid) {
		error = "Runtime resource input '" + std::string(input_label) + "' is not ready.";
		return nullptr;
	}
	error.clear();
	return resource;
}

template <typename T>
inline T* graph_runtime::read_output_resource(const NE_Node& node, std::string_view output_label, std::string& error) const {
	const int slot_index = this->output_slot(node, output_label);
	if (slot_index == -1) {
		error = "Missing output '" + std::string(output_label) + "'.";
		return nullptr;
	}
	const runtime_detail::frame* stack = frame_for_node(node);
	const runtime_detail::slot_value* runtime_value = stack == nullptr ? nullptr : stack->slot(slot_index);
	const runtime_detail::vm_stack* vm_stack = stack == nullptr ? nullptr : stack->stack;
	const runtime_detail::stack_slot* slot_meta = vm_stack == nullptr ? nullptr : stack_slot_meta(*vm_stack, slot_index);
	auto slot_matches_requested_type = [&]() {
		if (slot_meta == nullptr)
			return false;
		constexpr size_t expected_hash = rv::runtime_detail::resource_impl_tag_hash_v<T>;
		return expected_hash != 0 && slot_meta->type.type_hash == expected_hash;
	};
	if (!slot_matches_requested_type()) {
		error = "Runtime resource output '" + std::string(output_label) + "' has the wrong resource category.";
		return nullptr;
	}
	if (runtime_value == nullptr || !runtime_value->initialized || runtime_value->element_count == 0 || runtime_value->bytes.size() < sizeof(T*)) {
		error = "Runtime resource output '" + std::string(output_label) + "' is unset.";
		return nullptr;
	}
	T* resource = nullptr;
	std::memcpy(&resource, runtime_value->bytes.data(), sizeof(T*));
	if (resource == nullptr || !resource->valid) {
		error = "Runtime resource output '" + std::string(output_label) + "' is not ready.";
		return nullptr;
	}
	error.clear();
	return resource;
}

} // namespace rv
