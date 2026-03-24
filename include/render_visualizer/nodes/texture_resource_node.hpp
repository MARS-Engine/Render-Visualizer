#pragma once

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/support.hpp>

#include <optional>
#include <string>

namespace rv::nodes {

struct texture_slot_node_state {
	int slot_id = -1;
};

texture_slot_state make_default_texture_slot(std::string name = "Texture");
int ensure_default_texture_slot(NodeGraph& graph, std::string name = "Texture");

struct [[=mars::meta::display("Texture Slot"), =rv::node::pin_flow(enum_type::none)]] texture_resource_node {
	using custom_state_t = texture_slot_node_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, texture_slot_node_state& state);
	[[=rv::node::build()]] static bool build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error);
};

using texture_resource_node_tag = texture_resource_node;

inline const NodeRegistry::node_auto_registrar texture_resource_node_registration(
	NodeRegistry::make_reflected_registration<texture_resource_node>()
);

} // namespace rv::nodes
