#pragma once

#include <mars/graphics/backend/render_pass.hpp>

#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/execution_context.hpp>

namespace rv::nodes {

struct render_pass_state {
	mars::vector4<float> clear_color = {0.03f, 0.03f, 0.05f, 1.0f};
	mars_render_pass_load_op load_operation = MARS_RENDER_PASS_LOAD_OP_CLEAR;
	bool depth_enabled = false;
	mars_depth_format depth_format = MARS_DEPTH_FORMAT_D32_SFLOAT;
	float clear_depth = 1.0f;
};

struct [[= mars::meta::display("Render Pass")]] render_pass_node {
	[[=rv::node::output()]]
	rv::resource_tags::render_pass render_pass;

	using custom_state_t = render_pass_state;

	[[=rv::node::editor()]]
	static void edit(NodeGraph& graph, NE_Node& node, render_pass_state& state);

	[[=rv::node::build()]]
	static bool build_render_pass(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);

	[[=rv::node::destroy()]]
	static void destroy(rv::graph_services& services, NE_Node& node);
};

using render_pass_node_tag = render_pass_node;

inline const NodeRegistry::node_auto_registrar render_pass_node_registration(
	NodeRegistry::make_reflected_registration<render_pass_node>()
);

} // namespace rv::nodes
