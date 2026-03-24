#pragma once

#include <imgui.h>

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/shader_interface_support.hpp>
#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/texture_resource_node.hpp>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace rv::nodes {

struct material_bindings_node;
using material_bindings_node_tag = material_bindings_node;

struct material_bindings_state {
	std::string diagnostics [[=mars::json::skip]] = "Connect a Graphics Pipeline to configure material slots.";
	std::unordered_map<std::string, int> sampled_texture_slots;
};

int* find_selected_texture_slot(material_bindings_state& state, std::string_view label);
const int* find_selected_texture_slot(const material_bindings_state& state, std::string_view label);
int& ensure_selected_texture_slot(material_bindings_state& state, std::string_view label);
const NE_Link* find_material_binding_input_link(const NodeGraph& graph, const NE_Node& node, std::string_view label);
void prune_material_binding_state(material_bindings_state& state, const graph_shader_interface* shader_interface);
std::vector<NE_Pin> make_material_binding_inputs(const graph_shader_interface& shader_interface);
void sync_material_bindings_node(NodeGraph& graph, NE_Node& node);
void refresh_dynamic_material_binding_nodes(NodeGraph& graph);

struct [[=mars::meta::display("Material Bindings")]] material_bindings_node {
	using custom_state_t = material_bindings_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::save_state()]] static std::string save(const material_bindings_state& state);
	[[=rv::node::load_state()]] static bool load(material_bindings_state& state, std::string_view json, std::string& error);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, material_bindings_state& state);
	[[=rv::node::on_connect()]] static void on_connect(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node, const NE_Link& link);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

inline const NodeRegistry::node_auto_registrar material_bindings_node_registration(
	NodeRegistry::make_reflected_registration<material_bindings_node>()
);

} // namespace rv::nodes
