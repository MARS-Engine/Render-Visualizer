#pragma once

#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/virtual_struct.hpp>

#include <meta>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>


namespace rv::nodes {

struct variable_get_node;
struct variable_set_node;
using variable_get_node_tag = variable_get_node;
using variable_set_node_tag = variable_set_node;

struct variable_node_state {
	int variable_id = -1;
};

struct variable_type_descriptor {
	const char* label = "float";
	size_t type_hash = rv::detail::pin_type_hash<float>();
	bool is_container = false;
	bool supports_default_editor = true;
	bool supports_virtual_struct = false;
};

template <typename T>
struct variable_type_traits {
	static constexpr bool is_container = false;
	using element_t = T;
};

template <typename T, typename Allocator>
struct variable_type_traits<std::vector<T, Allocator>> {
	static constexpr bool is_container = true;
	using element_t = T;
};

template <typename T>
inline constexpr bool is_resource_variable_type_v =
	std::is_same_v<T, rv::resource_tags::vertex_buffer> ||
	std::is_same_v<T, rv::resource_tags::index_buffer> ||
	std::is_same_v<T, rv::resource_tags::uniform_resource> ||
	std::is_same_v<T, mars::vector3<unsigned char>> ||
	std::is_same_v<T, rv::resource_tags::texture_slot> ||
	std::is_same_v<T, rv::resource_tags::shader_module> ||
	std::is_same_v<T, rv::resource_tags::render_pass> ||
	std::is_same_v<T, rv::resource_tags::framebuffer> ||
	std::is_same_v<T, rv::resource_tags::depth_buffer> ||
	std::is_same_v<T, rv::resource_tags::graphics_pipeline> ||
	std::is_same_v<T, rv::resource_tags::material_resource> ||
	std::is_same_v<T, rv::resource_tags::virtual_struct_schema>;

template <typename T>
consteval std::string_view variable_type_label() {
	if constexpr (std::is_same_v<T, std::string>)
		return "string";
	else if constexpr (std::is_same_v<T, bool>)
		return "bool";
	else if constexpr (std::is_same_v<T, rv::resource_tags::vertex_buffer>)
		return "Vertex Buffer";
	else if constexpr (std::is_same_v<T, rv::resource_tags::index_buffer>)
		return "Index Buffer";
	else if constexpr (std::is_same_v<T, rv::resource_tags::uniform_resource>)
		return "Uniform Resource";
	else if constexpr (std::is_same_v<T, mars::vector3<unsigned char>>)
		return "Color Texture";
	else if constexpr (std::is_same_v<T, rv::resource_tags::texture_slot>)
		return "Texture Slot";
	else if constexpr (std::is_same_v<T, rv::resource_tags::shader_module>)
		return "Shader Module";
	else if constexpr (std::is_same_v<T, rv::resource_tags::render_pass>)
		return "Render Pass";
	else if constexpr (std::is_same_v<T, rv::resource_tags::framebuffer>)
		return "Framebuffer";
	else if constexpr (std::is_same_v<T, rv::resource_tags::depth_buffer>)
		return "Depth Buffer";
	else if constexpr (std::is_same_v<T, rv::resource_tags::graphics_pipeline>)
		return "Graphics Pipeline";
	else if constexpr (std::is_same_v<T, rv::resource_tags::material_resource>)
		return "Material Resource";
	else if constexpr (variable_type_traits<T>::is_container) {
		using element_t = typename variable_type_traits<T>::element_t;
		if constexpr (std::is_same_v<element_t, float>)
			return "float[]";
		else if constexpr (std::is_same_v<element_t, mars::vector2<float>>)
			return "float2[]";
		else if constexpr (std::is_same_v<element_t, mars::vector3<float>>)
			return "float3[]";
		else if constexpr (std::is_same_v<element_t, unsigned int>)
			return "uint[]";
		else
			return "array";
	} else
		return rv::reflect<T>::name;
}

template <typename T>
consteval variable_type_descriptor make_variable_type_descriptor() {
	using element_t = typename variable_type_traits<T>::element_t;
	constexpr bool is_container = variable_type_traits<T>::is_container;
	constexpr bool supports_default_editor =
		!is_resource_variable_type_v<element_t> &&
		!is_container &&
		(std::is_same_v<element_t, float> ||
		 std::is_same_v<element_t, mars::vector2<float>> ||
		 std::is_same_v<element_t, mars::vector3<float>> ||
		 std::is_same_v<element_t, mars::vector4<float>> ||
		 std::is_same_v<element_t, unsigned int> ||
		 std::is_same_v<element_t, mars::vector2<unsigned int>> ||
		 std::is_same_v<element_t, mars::vector3<unsigned int>> ||
		 std::is_same_v<element_t, mars::vector4<unsigned int>> ||
		 std::is_same_v<element_t, bool> ||
		 std::is_same_v<element_t, std::string>);
	return {
		.label = std::define_static_string(variable_type_label<T>()),
		.type_hash = rv::detail::pin_type_hash<element_t>(),
		.is_container = is_container,
		.supports_default_editor = supports_default_editor,
		.supports_virtual_struct = std::is_same_v<element_t, rv::resource_tags::vertex_buffer>,
	};
}

const std::vector<variable_type_descriptor>& variable_type_descriptors();
const variable_type_descriptor* find_variable_type_descriptor(const variable_slot_state& slot);
const char* variable_slot_type_name(const variable_slot_state& slot);
bool variable_slot_uses_pipeline_template(const variable_slot_state& slot);
void clear_variable_slot_template(variable_slot_state& slot);

template <typename T>
inline bool variable_slot_matches_type(const variable_slot_state& slot) {
	constexpr auto descriptor = make_variable_type_descriptor<T>();
	return descriptor.type_hash == slot.type_hash &&
		   descriptor.is_container == slot.is_container;
}

void reset_variable_slot_default(variable_slot_state& slot);
variable_slot_state make_default_variable_slot(std::string name = "Variable");
int ensure_default_variable_slot(NodeGraph& graph, std::string name = "Variable");

void sync_variable_get_node(NodeGraph& graph, NE_Node& node);
void sync_variable_set_node(NodeGraph& graph, NE_Node& node);
void refresh_dynamic_variable_nodes(NodeGraph& graph);

bool load_variable_node_state(variable_node_state& state, std::string_view json, std::string& error, const char* failure_message);

template <typename Tag>
inline NE_Node* spawn_bound_variable_node(NodeGraph& graph, int variable_id, const mars::vector2<float>& pos) {
	NE_Node* node = graph.spawn_node(node_type_v<Tag>, pos);
	if (node == nullptr || node->custom_state.storage == nullptr)
		return node;
	node->custom_state.as<variable_node_state>().variable_id = variable_id;
	if constexpr (std::is_same_v<Tag, variable_get_node_tag>)
		sync_variable_get_node(graph, *node);
	else
		sync_variable_set_node(graph, *node);
	return node;
}

} // namespace rv::nodes
