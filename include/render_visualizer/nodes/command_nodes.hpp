#pragma once

#include <imgui.h>

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/graphics_pipeline_node.hpp>
#include <render_visualizer/nodes/shader_interface_support.hpp>
#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/variable_nodes.hpp>
#include <render_visualizer/nodes/virtual_struct.hpp>

namespace rv::nodes {

struct begin_render_pass_node;
using begin_render_pass_node_tag = begin_render_pass_node;
struct bind_pipeline_node;
using bind_pipeline_node_tag = bind_pipeline_node;
struct bind_resources_node;
using bind_resources_node_tag = bind_resources_node;
struct bind_vertex_buffers_node;
using bind_vertex_buffers_node_tag = bind_vertex_buffers_node;
struct bind_index_buffer_node;
using bind_index_buffer_node_tag = bind_index_buffer_node;
struct draw_node;
using draw_node_tag = draw_node;
struct draw_indexed_node;
using draw_indexed_node_tag = draw_indexed_node;
struct present_texture_node;
using present_texture_node_tag = present_texture_node;

struct bind_vertex_buffers_state {
	int schema_id = -1;
};

struct [[=mars::meta::display("Begin Render Pass")]] begin_render_pass_node {
	[[=rv::node::input()]]
	rv::resource_tags::render_pass render_pass;

	[[=rv::node::input()]]
	rv::resource_tags::framebuffer framebuffer;

	[[=rv::node::input()]]
	[[=rv::node::optional()]]
	rv::resource_tags::depth_buffer depth_buffer;

	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

struct [[=mars::meta::display("Bind Pipeline")]] bind_pipeline_node {
	[[=rv::node::input()]]
	rv::resource_tags::graphics_pipeline pipeline;

	[[=rv::node::input()]]
	rv::resource_tags::framebuffer framebuffer;

	[[=rv::node::on_connect()]] static void on_connect(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node, const NE_Link& link);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

struct [[=mars::meta::display("Bind Resources")]] bind_resources_node {
	[[=rv::node::input()]]
	rv::resource_tags::graphics_pipeline pipeline;

	[[=rv::node::on_connect()]] static void on_connect(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node, const NE_Link& link);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
	[[=rv::node::destroy()]] static void destroy(rv::graph_services& services, NE_Node& node);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
};

void sync_bind_vertex_buffers_node(NodeGraph& graph, NE_Node& node);

void refresh_dynamic_bind_vertex_buffer_nodes(NodeGraph& graph);

struct [[=mars::meta::display("Bind Vertex Buffers")]] bind_vertex_buffers_node {
	using custom_state_t = bind_vertex_buffers_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, bind_vertex_buffers_state& state);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
};

struct [[=mars::meta::display("Bind Index Buffer")]] bind_index_buffer_node {
	[[=rv::node::input()]]
	rv::resource_tags::index_buffer index_buffer;

	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

struct [[=mars::meta::display("Draw")]] draw_node {
	[[=rv::node::input()]]
	unsigned int vertex_count = 3;

	[[=rv::node::input()]]
	unsigned int instance_count = 1;

	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

struct [[=mars::meta::display("Draw Indexed")]] draw_indexed_node {
	[[=rv::node::input()]]
	unsigned int index_count = 0;

	[[=rv::node::input()]]
	unsigned int instance_count = 1;

	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

struct [[=mars::meta::display("Present Texture")]] present_texture_node {
	[[=rv::node::input()]]
	[[=rv::node::texture()]]
	mars::vector3<unsigned char> texture;

	[[=rv::node::output()]]
	[[=mars::meta::display("texture")]]
	[[=rv::node::texture()]]
	mars::vector3<unsigned char> texture_out;

	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

inline const NodeRegistry::node_auto_registrar begin_render_pass_node_registration(
	NodeRegistry::make_reflected_registration<begin_render_pass_node>()
);
inline const NodeRegistry::node_auto_registrar bind_pipeline_node_registration(
	NodeRegistry::make_reflected_registration<bind_pipeline_node>()
);
inline const NodeRegistry::node_auto_registrar bind_resources_node_registration(
	NodeRegistry::make_reflected_registration<bind_resources_node>()
);
inline const NodeRegistry::node_auto_registrar bind_vertex_buffers_node_registration(
	NodeRegistry::make_reflected_registration<bind_vertex_buffers_node>()
);
inline const NodeRegistry::node_auto_registrar bind_index_buffer_node_registration(
	NodeRegistry::make_reflected_registration<bind_index_buffer_node>()
);
inline const NodeRegistry::node_auto_registrar draw_node_registration(
	NodeRegistry::make_reflected_registration<draw_node>()
);
inline const NodeRegistry::node_auto_registrar draw_indexed_node_registration(
	NodeRegistry::make_reflected_registration<draw_indexed_node>()
);
inline const NodeRegistry::node_auto_registrar present_texture_node_registration(
	NodeRegistry::make_reflected_registration<present_texture_node>()
);

} // namespace rv::nodes
