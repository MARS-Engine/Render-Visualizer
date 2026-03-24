#pragma once

#include <imgui.h>

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/support.hpp>

#include <array>
#include <string>
#include <vector>

namespace rv::nodes {

enum class uniform_value_kind {
	float1,
	float2,
	float3,
	float4,
	float4x4,
	uint1,
	uint2,
	uint3,
	uint4,
};

struct uniform_data_state {
	uniform_value_kind kind = uniform_value_kind::float4;
	std::array<float, 4> float_values = {1.0f, 1.0f, 1.0f, 1.0f};
	std::array<unsigned int, 4> uint_values = {0u, 0u, 0u, 0u};
};

NE_Pin make_uniform_value_pin(uniform_value_kind kind, std::string label = "value", bool required = true);
const char* uniform_value_kind_name(uniform_value_kind kind);
bool uniform_kind_is_float(uniform_value_kind kind);
int uniform_kind_component_count(uniform_value_kind kind);
std::vector<std::byte> make_uniform_bytes(const uniform_data_state& state);
std::any make_uniform_value_any(const uniform_data_state& state);
void sync_uniform_data_node(NodeGraph& graph, NE_Node& node);

struct [[=mars::meta::display("Uniform Data"), =rv::node::pin_flow(enum_type::none)]] uniform_data_node {
	using custom_state_t = uniform_data_state;

	[[=rv::node::save_state()]] static std::string save(const uniform_data_state& state);
	[[=rv::node::load_state()]] static bool load(uniform_data_state& state, std::string_view json, std::string& error);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, uniform_data_state& state);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::pure()]] static bool emit(rv::graph_execution_context& ctx, NE_Node& node, std::string&);
};

using uniform_data_node_tag = uniform_data_node;

inline const NodeRegistry::node_auto_registrar uniform_data_node_registration(
	NodeRegistry::make_reflected_registration<uniform_data_node>()
);

} // namespace rv::nodes
