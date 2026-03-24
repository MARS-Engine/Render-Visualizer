#pragma once

#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/execution_context.hpp>

namespace rv::nodes {

struct [[= mars::meta::display("Render Targets Create")]] framebuffer_node {
	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::on_connect()]] static void on_connect(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node, const NE_Link& link);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::build()]] static bool build_framebuffer(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
	[[=rv::node::destroy()]] static void destroy(rv::graph_services& services, NE_Node& node);
};

using framebuffer_node_tag = framebuffer_node;

inline const NodeRegistry::node_auto_registrar framebuffer_node_registration(
	NodeRegistry::make_reflected_registration<framebuffer_node>()
);

} // namespace rv::nodes
