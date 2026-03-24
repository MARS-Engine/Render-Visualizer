#include <render_visualizer/nodes/shader_interface_support.hpp>

#include <render_visualizer/nodes/graphics_pipeline_node.hpp>
#include <render_visualizer/nodes/shader_module_node.hpp>
#include <render_visualizer/nodes/variable_nodes.hpp>

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

const NE_Node* linked_shader_module_node(const NodeGraph& graph, const NE_Node& node) {
	const NE_Pin* shader_pin = find_pin_by_label(node.inputs, "shader");
	if (shader_pin == nullptr)
		return nullptr;
	for (const auto& link : graph.links) {
		if (link.to_node != node.id || link.to_pin != shader_pin->id)
			continue;
		const NE_Node* source = graph.find_node(link.from_node);
		if (source == nullptr || source->type != node_type_v<shader_module_node_tag> || source->custom_state.storage == nullptr)
			return nullptr;
		return source;
	}
	return nullptr;
}

} // namespace

uniform_value_kind uniform_kind_from_type_hash(size_t type_hash) {
	if (type_hash == rv::detail::pin_type_hash<float>())
		return uniform_value_kind::float1;
	if (type_hash == rv::detail::pin_type_hash<mars::vector2<float>>())
		return uniform_value_kind::float2;
	if (type_hash == rv::detail::pin_type_hash<mars::vector3<float>>())
		return uniform_value_kind::float3;
	if (type_hash == rv::detail::pin_type_hash<mars::vector4<float>>())
		return uniform_value_kind::float4;
	if (type_hash == rv::detail::pin_type_hash<mars::matrix4<float>>())
		return uniform_value_kind::float4x4;
	if (type_hash == rv::detail::pin_type_hash<unsigned int>())
		return uniform_value_kind::uint1;
	if (type_hash == rv::detail::pin_type_hash<mars::vector2<unsigned int>>())
		return uniform_value_kind::uint2;
	if (type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned int>>())
		return uniform_value_kind::uint3;
	return uniform_value_kind::uint4;
}

NE_Pin make_shader_interface_slot_pin(const graph_shader_interface_slot& slot) {
	if (slot.kind == graph_shader_resource_kind::sampled_texture)
		return make_pin<mars::vector3<unsigned char>>(slot.label, false, false);
	return make_uniform_value_pin(uniform_kind_from_type_hash(slot.type_hash), slot.label);
}

const char* shader_resource_kind_name(graph_shader_resource_kind kind) {
	switch (kind) {
	case graph_shader_resource_kind::uniform_value:
		return "Uniform Value";
	case graph_shader_resource_kind::sampled_texture:
	default:
		return "Sampled Texture";
	}
}

const char* pipeline_stage_name(mars_pipeline_stage stage) {
	if (stage == MARS_PIPELINE_STAGE_VERTEX)
		return "Vertex";
	if (stage == MARS_PIPELINE_STAGE_FRAGMENT)
		return "Fragment";
	if (stage == MARS_PIPELINE_STAGE_VERTEX_FRAGMENT)
		return "Vertex+Fragment";
	if (stage == MARS_PIPELINE_STAGE_COMPUTE)
		return "Compute";
	return "Unknown";
}

std::string shader_resource_value_type_name(graph_shader_resource_kind kind, size_t type_hash) {
	if (kind == graph_shader_resource_kind::sampled_texture)
		return "texture";
	if (type_hash == rv::detail::pin_type_hash<mars::matrix4<float>>())
		return "float4x4";

	std::string name = "unknown";
	const bool handled = rv::detail::dispatch_type_hash<local_numeric_types>(type_hash, [&]<typename value_t>() {
		name = std::string(rv::reflect<value_t>::name);
	});
	return handled ? name : std::string("unsupported");
}

const NE_Node* resolve_pipeline_source_node(const NodeGraph& graph, const NE_Node& source_node) {
	if (source_node.type == node_type_v<graphics_pipeline_node_tag>)
		return &source_node;

	if (source_node.type == node_type_v<variable_get_node_tag> && source_node.custom_state.storage != nullptr) {
		const int variable_id = source_node.custom_state.as<variable_node_state>().variable_id;
		for (const auto& node : graph.nodes) {
			if (node.type != node_type_v<variable_set_node_tag> || node.custom_state.storage == nullptr)
				continue;
			if (node.custom_state.as<variable_node_state>().variable_id != variable_id)
				continue;
			const NE_Pin* value_pin = find_pin_by_label(node.inputs, "value");
			if (value_pin == nullptr)
				continue;
			for (const auto& link : graph.links) {
				if (link.to_node != node.id || link.to_pin != value_pin->id)
					continue;
				const NE_Node* pipeline_node = graph.find_node(link.from_node);
				if (pipeline_node != nullptr && pipeline_node->type == node_type_v<graphics_pipeline_node_tag>)
					return pipeline_node;
			}
		}
	}

	return nullptr;
}

const graph_shader_interface* resolved_pipeline_shader_interface(const NodeGraph& graph, const NE_Node& pipeline_node, std::string* error) {
	const NE_Node* shader_node = linked_shader_module_node(graph, pipeline_node);
	if (shader_node == nullptr) {
		if (error != nullptr && error->empty())
			*error = "Graphics Pipeline is missing its Shader Module Creator input.";
		return nullptr;
	}

	const graph_shader_interface* shader_interface = find_generated_shader_interface(graph, shader_node->id);
	if (shader_interface == nullptr && error != nullptr && error->empty())
		*error = "Graphics Pipeline could not resolve the linked shader interface.";
	return shader_interface;
}

} // namespace rv::nodes
