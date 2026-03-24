#pragma once

#include <imgui.h>

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/uniform_data_node.hpp>

namespace rv::nodes {

struct dynamic_uniform_state {
	uniform_value_kind kind = uniform_value_kind::float3;
};

NE_Pin make_dynamic_uniform_value_pin(uniform_value_kind kind);
std::vector<std::byte> make_dynamic_uniform_zero_bytes(uniform_value_kind kind);
void sync_dynamic_uniform_node(NodeGraph& graph, NE_Node& node);

struct [[=mars::meta::display("Dynamic Uniform")]] dynamic_uniform_node {
	using custom_state_t = dynamic_uniform_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, dynamic_uniform_state& state);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
	[[=rv::node::destroy()]] static void destroy(rv::graph_services& services, NE_Node& node);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
};

using dynamic_uniform_node_tag = dynamic_uniform_node;

inline const NodeRegistry::node_auto_registrar dynamic_uniform_node_registration(
	NodeRegistry::make_reflected_registration<dynamic_uniform_node>()
);

} // namespace rv::nodes
