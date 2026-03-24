#pragma once

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/support.hpp>

namespace rv::nodes {

struct [[=mars::meta::display("Camera")]] camera_node {
	[[=rv::node::output()]]
	mars::matrix4<float> view_proj;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::pure()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
};

inline const NodeRegistry::node_auto_registrar camera_node_registration(NodeRegistry::make_reflected_registration<camera_node>());

} // namespace rv::nodes
