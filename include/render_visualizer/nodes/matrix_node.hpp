#pragma once

#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/execution_context.hpp>

namespace rv::nodes {

struct [[= mars::meta::display("Matrix")]] [[=rv::node::pin_flow(enum_type::none)]] matrix_node {
	[[=rv::node::input()]]
	unsigned int x;

	[[=rv::node::input()]]
	unsigned int y;

	[[=rv::node::input()]]
	float spacing;

	[[=rv::node::output()]]
	mars::vector3<float> position;

	[[=rv::node::output()]]
	unsigned int index;

	[[=rv::node::editor()]]
	static void describe(NodeGraph&, NE_Node& node);

	[[=rv::node::pure()]]
	static bool expand_grid(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);

	[[=rv::node::configure()]]
	static void configure(NodeTypeInfo& info);
};

using matrix_node_tag = matrix_node;

inline const NodeRegistry::node_auto_registrar matrix_node_registration(
	NodeRegistry::make_reflected_registration<matrix_node>()
);

} // namespace rv::nodes
