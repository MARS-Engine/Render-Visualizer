#pragma once

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/variable_support.hpp>

namespace rv::nodes {

struct [[=mars::meta::display("Variable Get"), =rv::node::pin_flow(enum_type::none)]] variable_get_node {
	using custom_state_t = variable_node_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::load_state()]] static bool load(variable_node_state& state, std::string_view json, std::string& error);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, variable_node_state& state);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::pure()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
};

struct [[=mars::meta::display("Variable Set")]] variable_set_node {
	using custom_state_t = variable_node_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::load_state()]] static bool load(variable_node_state& state, std::string_view json, std::string& error);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, variable_node_state& state);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::build_propagate()]] static bool build_propagate(rv::graph_build_context& ctx, NE_Node& node, std::string& error);
};

inline const NodeRegistry::node_auto_registrar variable_get_node_registration(
	NodeRegistry::make_reflected_registration<variable_get_node>({ .show_in_spawn_menu = false })
);
inline const NodeRegistry::node_auto_registrar variable_set_node_registration(
	NodeRegistry::make_reflected_registration<variable_set_node>({ .show_in_spawn_menu = false })
);

} // namespace rv::nodes
