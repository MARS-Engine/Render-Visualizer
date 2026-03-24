#pragma once

#include <render_visualizer/node_value_types.hpp>

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <ranges>
#include <vector>

struct NE_Link;

struct NodeTypeInfo {
	enum class execution_shape {
		map,
		expand,
		reduce
	};

	enum class compiled_branch_visibility {
		isolate_siblings,
		sequence_accumulate
	};

	struct meta_info {
		size_t type = 0;
		std::string title;
		enum_type pin_flow = enum_type::both;
		bool is_permanent = false;
		bool show_in_spawn_menu = true;
		bool is_start = false;
		bool is_end = false;
		bool has_processor = false;
		bool is_vm_node = false;
		bool is_vm_pure = false;
		bool is_vm_callable = false;
		bool is_vm_event = false;
		bool is_function_outputs = false;
		bool vm_reexecute_each_tick = false;
		execution_shape vm_execution_shape = execution_shape::map;
		compiled_branch_visibility compiled_visibility = compiled_branch_visibility::isolate_siblings;
		bool vm_execute_per_item = false;
	} meta;

	struct pin_info {
		std::vector<NE_Pin> inputs;
		std::vector<NE_Pin> outputs;
		bool has_exec_input = true;
		bool has_exec_output = true;
		NE_Pin exec_input;
		NE_Pin exec_output;
		std::vector<NE_Pin> exec_outputs;
		std::vector<NE_Pin> wildcard_input_templates;
		std::vector<NE_Pin> wildcard_output_templates;
	} pins;

	struct hook_info {
		std::vector<NE_ProcessorParamDescriptor> processor_params;
		std::vector<NE_ProcessorParamValue> (*make_processor_params)() = nullptr;
		NE_RuntimeValue (*make_runtime_value)() = nullptr;
		bool (*execute_processor)(NE_Node&) = nullptr;
		NE_CustomState (*make_custom_state)() = nullptr;
		std::string (*save_custom_state_json)(const NE_Node&) = nullptr;
		bool (*load_custom_state_json)(NE_Node&, std::string_view, std::string&) = nullptr;
		void (*render_selected_sidebar)(NodeGraph&, const NodeTypeInfo&, NE_Node&) = nullptr;
		void (*refresh_dynamic_pins)(NodeGraph&, const NodeTypeInfo&, NE_Node&) = nullptr;
		void (*on_connect)(NodeGraph&, const NodeTypeInfo&, NE_Node&, const NE_Link&) = nullptr;
		bool (*validate_node)(const NodeGraph&, const NodeTypeInfo&, const NE_Node&, std::string&) = nullptr;
		void (*destroy_execute)(rv::graph_services&, NE_Node&) = nullptr;
		bool (*build_execute)(rv::graph_build_context&, NE_Node&, rv::graph_build_result&, std::string&) = nullptr;
		bool (*vm_execute)(rv::graph_execution_context&, NE_Node&, std::string&) = nullptr;
		void (*on_tick)(NE_Node&, float) = nullptr;
		bool (*get_cpu_output)(const NE_Node&, std::string_view, std::vector<std::byte>&, size_t&) = nullptr;
		bool (*build_propagate)(rv::graph_build_context&, NE_Node&, std::string&) = nullptr;
	} hooks;
};

struct NodeRegistrationOptions {
	bool is_permanent = false;
	bool show_in_spawn_menu = true;
};

struct NE_Node {
	int id = -1;
	int function_id = -1;
	size_t type = 0;
	std::string title;
	enum_type pin_flow = enum_type::both;
	bool has_exec_input = false;
	bool has_exec_output = false;
	NE_Pin exec_input;
	NE_Pin exec_output;
	std::vector<NE_Pin> exec_outputs;
	bool is_permanent = false;
	mars::vector2<float> pos = {0.0f, 0.0f};
	std::vector<NE_Pin> static_inputs;
	std::vector<NE_Pin> static_outputs;
	std::vector<NE_Pin> generated_inputs;
	std::vector<NE_Pin> generated_outputs;
	std::vector<NE_Pin> inputs;
	std::vector<NE_Pin> outputs;
	std::vector<NE_ProcessorParamValue> processor_params;
	std::vector<NE_InlineInputValue> inline_input_values;
	NE_RuntimeValue runtime_value;
	NE_CustomState custom_state;
	bool has_run_result = false;
	bool last_run_success = false;
	std::string last_run_message;
};

struct NE_Link {
	int from_node = -1;
	int from_pin = -1;
	int to_node = -1;
	int to_pin = -1;
};

struct pin_type_info {
	const char* name = nullptr;
	size_t element_size = 0;
	size_t element_type_hash = 0;
	bool is_container = false;
	bool is_numeric = false;
	bool is_resource = false;
	bool is_virtual_struct_field = false;

	std::string (*to_json)(const std::any& value) = nullptr;
	bool (*from_json)(std::string_view json, std::any& value) = nullptr;
	bool (*copy_to_bytes)(const std::any& value, std::vector<std::byte>& bytes, size_t& element_count) = nullptr;
	bool (*copy_from_bytes)(const std::vector<std::byte>& bytes, size_t element_count, std::any& value) = nullptr;
	bool (*to_wildcard_value)(const std::any& src, const NE_WildcardTypeInfo& type_info, NE_WildcardValue& dst) = nullptr;
	bool (*from_wildcard_value)(const NE_WildcardValue& src, std::any& dst) = nullptr;
};

struct NodeRegistry {
	struct registration_descriptor {
		size_t type = 0;
		std::function<void(NodeRegistry&)> apply;
	};

	struct node_auto_registrar {
		explicit node_auto_registrar(registration_descriptor descriptor) {
			NodeRegistry::add_registration_descriptor(std::move(descriptor));
		}
	};

	NodeRegistry();

	template <typename T>
	static registration_descriptor make_reflected_registration(NodeRegistrationOptions options = {}) {
		return {
			.type = mars::hash::type_fingerprint_v<T>,
			.apply = [options](NodeRegistry& registry) {
				registry.register_node<T>(options);
			}
		};
	}

	template <typename Tag>
	static registration_descriptor make_custom_registration(std::function<NodeTypeInfo()> factory) {
		return {
			.type = mars::hash::type_fingerprint_v<Tag>,
			.apply = [factory = std::move(factory)](NodeRegistry& registry) {
				registry.register_custom_node(factory());
			}
		};
	}

	template <typename T>
	void register_node(NodeRegistrationOptions options = {});

	template <auto FunctionInfo>
	void register_function();

	void register_custom_node(NodeTypeInfo info);

	const NodeTypeInfo* find(size_t type) const;

	bool contains(size_t type) const;

	const NodeTypeInfo* find(std::string_view title) const;

	const std::vector<size_t>& registered_types() const;

	template <typename T>
	void register_pin_color(mars::vector4<float> color) {
		pin_colors[rv::detail::pin_type_hash<T>()] = color;
	}

	mars::vector4<float> pin_color(size_t type_hash) const;

	template <typename T>
	void register_pin_type(const char* name, bool is_numeric = false, bool is_resource = false, bool is_virtual_struct_field = false);

	const pin_type_info* pin_type(size_t type_hash) const;
	const pin_type_info* pin_type_for_element(size_t element_type_hash, bool is_container) const;

  private:
	struct registration_catalog {
		std::vector<registration_descriptor> descriptors;
		std::unordered_map<size_t, size_t> descriptor_indices;
	};

	static registration_catalog& global_registration_catalog();

	static void add_registration_descriptor(registration_descriptor descriptor);

	void import_registration_catalog();

	template <typename Annotation, typename T>
	static consteval size_t count_annotated_functions() {
		size_t count = 0;
		constexpr std::meta::access_context ctx = std::meta::access_context::current();
		template for (constexpr auto mem : std::define_static_array(std::meta::members_of(^^T, ctx))) {
			if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<Annotation>(mem))
				++count;
		}
		return count;
	}

	static void normalize_type_pins(NodeTypeInfo& info);

	std::unordered_map<size_t, NodeTypeInfo> types;
	std::unordered_map<size_t, mars::vector4<float>> pin_colors;
	std::unordered_map<size_t, pin_type_info> pin_types_;
	std::vector<size_t> type_order;
};

struct NodeGraph {
	explicit NodeGraph(const NodeRegistry& registry);

	std::vector<NE_Node> nodes;
	std::vector<NE_Link> links;
	std::vector<graph_function_definition> functions;
	std::vector<graph_virtual_struct_schema> virtual_structs;
	std::vector<graph_shader_interface> shader_interfaces;
	std::vector<graph_texture_slot> texture_slots;
	std::vector<graph_variable_slot> variable_slots;

	NE_Node* spawn_node(size_t type, mars::vector2<float> pos = {0.0f, 0.0f});

	NE_Node* spawn_node(size_t type, mars::vector2<float> pos, int forced_id);

	NE_Node* spawn_node(size_t type, mars::vector2<float> pos, int forced_id, int function_id);

	template <typename NodeType>
	NE_Node* spawn_node(mars::vector2<float> pos = {0.0f, 0.0f}) {
		return spawn_node(mars::hash::type_fingerprint_v<NodeType>, pos);
	}

	NE_Node* spawn_node(std::string_view title, mars::vector2<float> pos = {0.0f, 0.0f});

	NE_Node* spawn_node(std::string_view title, mars::vector2<float> pos, int forced_id);

	NE_Node* spawn_node(std::string_view title, mars::vector2<float> pos, int forced_id, int function_id);

	NE_Node* find_node(int node_id);

	const NE_Node* find_node(int node_id) const;

	bool add_link(NE_Link lnk);
	static bool is_data_link_compatible(const NE_Pin& from_pin, const NE_Pin& to_pin);

	void remove_node(int node_id);

	void remove_link(const NE_Link& target);

	bool replace_generated_pins(int node_id, std::vector<NE_Pin> generated_inputs, std::vector<NE_Pin> generated_outputs);

	bool replace_exec_outputs(int node_id, std::vector<NE_Pin> exec_outputs);

	size_t run_start_nodes();

	bool is_node_permanent(int node_id) const;

	void notify_node_moved(int node_id);

	void notify_graph_dirty();

	graph_virtual_struct_schema* create_virtual_struct(graph_virtual_struct_schema schema = {});

	graph_virtual_struct_schema* create_virtual_struct(graph_virtual_struct_schema schema, int forced_id);

	graph_texture_slot* create_texture_slot(graph_texture_slot slot = {});

	graph_texture_slot* create_texture_slot(graph_texture_slot slot, int forced_id);

	graph_variable_slot* create_variable_slot(graph_variable_slot slot = {});

	graph_variable_slot* create_variable_slot(graph_variable_slot slot, int forced_id);

	graph_virtual_struct_schema* find_virtual_struct(int schema_id);

	const graph_virtual_struct_schema* find_virtual_struct(int schema_id) const;

	graph_virtual_struct_schema* find_virtual_struct(std::string_view name, size_t layout_fingerprint);

	const graph_virtual_struct_schema* find_virtual_struct(std::string_view name, size_t layout_fingerprint) const;

	graph_shader_interface* upsert_shader_interface(graph_shader_interface shader_interface);

	graph_shader_interface* find_shader_interface(int interface_id);

	const graph_shader_interface* find_shader_interface(int interface_id) const;

	graph_shader_interface* find_shader_interface_by_source_node(int node_id);

	const graph_shader_interface* find_shader_interface_by_source_node(int node_id) const;

	graph_texture_slot* find_texture_slot(int slot_id);

	const graph_texture_slot* find_texture_slot(int slot_id) const;

	graph_variable_slot* find_variable_slot(int slot_id);

	const graph_variable_slot* find_variable_slot(int slot_id) const;

	const NodeRegistry* node_registry() const;

	int active_function_id() const;

	bool set_active_function(int function_id);

	graph_function_definition* find_function(int function_id);

	const graph_function_definition* find_function(int function_id) const;

	graph_function_definition* setup_function();
	const graph_function_definition* setup_function() const;
	graph_function_definition* render_function();
	const graph_function_definition* render_function() const;
	int setup_function_id() const;
	int render_function_id() const;
	bool is_builtin_function(int id) const;

	graph_function_definition* create_function(std::string name = "Function");

	bool remove_function(int function_id);

	std::vector<NE_Node*> nodes_in_function(int function_id);

	std::vector<const NE_Node*> nodes_in_function(int function_id) const;

	std::vector<const NE_Link*> links_in_function(int function_id) const;

	void sync_function_metadata();

	void clear();

	std::function<void(const NE_Node&)> on_node_spawned;
	std::function<void(int node_id)> on_node_removed;
	std::function<void(const NE_Link&)> on_link_created;
	std::function<void(const NE_Link&)> on_link_removed;
	std::function<void(int node_id)> on_node_moved;
	std::function<void()> on_graph_dirty;

  private:
	void refresh_dynamic_nodes(const std::vector<int>& node_ids);

	bool contains_link(const NE_Link& candidate) const;

	static const NE_Pin* find_pin(const std::vector<NE_Pin>& pins, int pin_id);

	static const NE_Pin* find_pin_by_label(const std::vector<NE_Pin>& pins, std::string_view label);

	static const NE_Pin* find_input_pin(const NE_Node& node, int pin_id);

	static const NE_Pin* find_output_pin(const NE_Node& node, int pin_id);

	NE_Link* find_link_by_labels(int from_node_id, std::string_view from_label, int to_node_id, std::string_view to_label);

	static void rebuild_visible_pins(NE_Node& node);

	const NodeRegistry* registry_ = nullptr;
	int next_node_id_ = 0;
	int next_function_id_ = 0;
	int next_function_signature_pin_id_ = 0;
	int next_virtual_struct_id_ = 0;
	int next_texture_slot_id_ = 0;
	int next_variable_slot_id_ = 0;
	int setup_function_id_ = -1;
	int render_function_id_ = -1;
	int active_function_id_ = -1;

	int resolved_function_id(int function_id) const;

	graph_function_definition* create_builtin_function(std::string name, int& stored_id);

	void ensure_builtin_functions();
};
