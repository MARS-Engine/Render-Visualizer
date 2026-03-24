#pragma once

#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/execution_context.hpp>

namespace rv::nodes {

struct graphics_pipeline_state {
	mars_pipeline_primitive_topology topology = MARS_PIPELINE_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	int schema_id = -1;
	bool depth_test_enable = true;
	bool depth_write_enable = true;
	mars_compare_op depth_compare = MARS_COMPARE_OP_LESS;
};

struct [[= mars::meta::display("Graphics Pipeline")]] graphics_pipeline_node {
	[[=rv::node::input()]]
	rv::resource_tags::shader_module shader;

	[[=rv::node::input()]]
	rv::resource_tags::render_pass render_pass;

	[[=rv::node::output()]]
	rv::resource_tags::graphics_pipeline pipeline;

	using custom_state_t = graphics_pipeline_state;

	[[=rv::node::editor()]]
	static void edit_pipeline(NodeGraph& graph, NE_Node& node, graphics_pipeline_state& state);

	[[=rv::node::refresh()]]
	static void refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node);

	[[=rv::node::build()]]
	static bool build_pipeline(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);

	[[=rv::node::destroy()]]
	static void destroy(rv::graph_services& services, NE_Node& node);
};

using graphics_pipeline_node_tag = graphics_pipeline_node;

inline const NodeRegistry::node_auto_registrar graphics_pipeline_node_registration(
	NodeRegistry::make_reflected_registration<graphics_pipeline_node>()
);

} // namespace rv::nodes
