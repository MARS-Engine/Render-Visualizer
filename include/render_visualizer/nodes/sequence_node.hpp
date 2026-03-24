#pragma once

#include <imgui.h>

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/support.hpp>

namespace rv::nodes {

struct sequence_node_state {
	int then_count = 3;
};

std::vector<NE_Pin> make_sequence_exec_outputs(const sequence_node_state& state);
bool equivalent_exec_pin_layout(const std::vector<NE_Pin>& lhs, const std::vector<NE_Pin>& rhs);
void sync_sequence_node(NodeGraph& graph, NE_Node& node);

struct [[=mars::meta::display("Sequence")]] sequence_node {
	using custom_state_t = sequence_node_state;

	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, sequence_node_state& state);
	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context&, NE_Node&, std::string&);
};

using sequence_node_tag = sequence_node;

inline const NodeRegistry::node_auto_registrar sequence_node_registration(
	NodeRegistry::make_reflected_registration<sequence_node>()
);

} // namespace rv::nodes
