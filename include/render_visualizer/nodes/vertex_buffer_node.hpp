#pragma once

#include <imgui.h>

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/virtual_struct.hpp>

#include <string>
#include <vector>

namespace rv::nodes {

struct vertex_buffer_node;
using vertex_buffer_node_tag = vertex_buffer_node;

struct vertex_buffer_state {
	int schema_id = -1;
};

std::vector<NE_Pin> make_vertex_buffer_inputs(const virtual_struct_schema_state& state);
std::vector<NE_Pin> make_vertex_buffer_outputs(const virtual_struct_schema_state& state);
void sync_vertex_buffer_node(NodeGraph& graph, NE_Node& node);
void refresh_dynamic_vertex_buffer_nodes(NodeGraph& graph);

struct [[=mars::meta::display("Vertex Buffer")]] vertex_buffer_node {
	using custom_state_t = vertex_buffer_state;

	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, vertex_buffer_state& state);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
	[[=rv::node::destroy()]] static void destroy(rv::graph_services& services, NE_Node& node);
};

inline const NodeRegistry::node_auto_registrar vertex_buffer_node_registration(
	NodeRegistry::make_reflected_registration<vertex_buffer_node>()
);

} // namespace rv::nodes
