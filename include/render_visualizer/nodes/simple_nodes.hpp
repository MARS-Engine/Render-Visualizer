#pragma once

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/support.hpp>

#include <mars/math/vector3.hpp>

namespace rv::nodes {

struct [[= mars::meta::display("Depth Buffer")]] depth_buffer_node {
	[[=rv::node::input()]]
	rv::resource_tags::render_pass render_pass;

	[[=rv::node::output()]]
	rv::resource_tags::depth_buffer depth_buffer;

	[[=rv::node::editor()]]
	static void describe(NodeGraph& graph, NE_Node& node);

	[[=rv::node::build()]]
	static bool build_depth(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);

	[[=rv::node::destroy()]]
	static void destroy(rv::graph_services& services, NE_Node& node);
};

using depth_buffer_node_tag = depth_buffer_node;

inline const NodeRegistry::node_auto_registrar depth_buffer_node_registration(
	NodeRegistry::make_reflected_registration<depth_buffer_node>()
);

struct [[= mars::meta::display("Index Buffer")]] index_buffer_node {
	[[=rv::node::input()]]
	std::vector<unsigned int> indices;

	[[=rv::node::output()]]
	rv::resource_tags::index_buffer index_buffer;

	[[=rv::node::build()]]
	static bool build_index(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);

	[[=rv::node::destroy()]]
	static void destroy(rv::graph_services& services, NE_Node& node);
};

using index_buffer_node_tag = index_buffer_node;

inline const NodeRegistry::node_auto_registrar index_buffer_node_registration(
	NodeRegistry::make_reflected_registration<index_buffer_node>()
);

struct scroll_time_state {
	mars::vector2<float> speed = {0.15f, 0.0f};
	mars::vector2<float> value = {0.0f, 0.0f};
};

std::vector<std::byte> make_scroll_time_bytes(const scroll_time_state& state);

struct [[= mars::meta::display("Scroll Time")]] [[=rv::node::pin_flow(enum_type::none)]] scroll_time_node {
	[[=rv::node::output()]]
	mars::vector2<float> offset;

	using custom_state_t = scroll_time_state;

	[[=rv::node::editor()]]
	static void edit(NodeGraph& graph, NE_Node& node, scroll_time_state& state);

	[[=rv::node::on_tick()]]
	static void tick(NE_Node& node, float delta_time);

	[[=rv::node::pure()]]
	static bool emit_offset(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);

	[[=rv::node::configure()]]
	static void configure(NodeTypeInfo& info);
};

using scroll_time_node_tag = scroll_time_node;

inline const NodeRegistry::node_auto_registrar scroll_time_node_registration(
	NodeRegistry::make_reflected_registration<scroll_time_node>()
);

struct [[=rv::node::end_node()]] swapchain_node {
	[[=rv::node::input()]]
	[[=rv::node::texture()]]
	mars::vector3<unsigned char> color;

	[[=rv::node::build()]]
	static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

inline const NodeRegistry::node_auto_registrar swapchain_node_registration(
	NodeRegistry::make_reflected_registration<swapchain_node>({
		.is_permanent = true,
		.show_in_spawn_menu = false
	})
);

} // namespace rv::nodes
