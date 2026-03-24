#pragma once

#include <imgui.h>

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/shader_interface_support.hpp>
#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/ui/widgets.hpp>
#include <render_visualizer/raster_node.hpp>

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

namespace rv::nodes {

struct shader_module_state {
	std::string module_name [[=mars::json::skip_empty]];
	std::string source = rv::raster::default_source();
	std::string diagnostics [[=mars::json::skip]] = "Not compiled yet.";
	bool last_compile_success [[=mars::json::skip]] = false;
	bool compile_required [[=mars::json::skip]] = true;
	std::vector<NE_Pin> reflected_inputs [[=mars::json::skip]];
	std::vector<NE_Pin> reflected_outputs [[=mars::json::skip]];
	std::vector<rv::raster::reflected_shader_resource> reflected_resources [[=mars::json::skip]];
};

void compile_shader_module(shader_module_state& state);
void refresh_generated_shader_interfaces(NodeGraph& graph);
graph_shader_interface make_generated_shader_interface(const shader_module_state& state, const NE_Node& node);
graph_shader_interface* find_generated_shader_interface(NodeGraph& graph, int shader_node_id);
const graph_shader_interface* find_generated_shader_interface(const NodeGraph& graph, int shader_node_id);

struct [[=mars::meta::display("Shader Module Creator")]] shader_module_node {
	using custom_state_t = shader_module_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::save_state()]] static std::string save(const shader_module_state& state);
	[[=rv::node::load_state()]] static bool load(shader_module_state& state, std::string_view json, std::string& error);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, shader_module_state& state);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
	[[=rv::node::destroy()]] static void destroy(rv::graph_services& services, NE_Node& node);
};

using shader_module_node_tag = shader_module_node;

inline const NodeRegistry::node_auto_registrar shader_module_node_registration(
	NodeRegistry::make_reflected_registration<shader_module_node>()
);

} // namespace rv::nodes
