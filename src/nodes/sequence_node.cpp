#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/sequence_node.hpp>

namespace rv::nodes {

std::vector<NE_Pin> make_sequence_exec_outputs(const sequence_node_state& state) {
	std::vector<NE_Pin> pins;
	const int count = std::max(state.then_count, 1);
	pins.reserve(static_cast<size_t>(count));
	for (int index = 0; index < count; ++index) {
		NE_Pin pin {};
		pin.label = "Then " + std::to_string(index);
		pin.display_label = pin.label;
		pin.kind = NE_PinKind::exec;
		pins.push_back(std::move(pin));
	}
	return pins;
}

bool equivalent_exec_pin_layout(const std::vector<NE_Pin>& lhs, const std::vector<NE_Pin>& rhs) {
	// Exec pins only care about control-flow identity, so label + kind is the whole layout here.
	if (lhs.size() != rhs.size())
		return false;
	for (size_t index = 0; index < lhs.size(); ++index) {
		if (lhs[index].label != rhs[index].label || lhs[index].kind != rhs[index].kind)
			return false;
	}
	return true;
}

void sync_sequence_node(NodeGraph& graph, NE_Node& node) {
	if (node.custom_state.storage == nullptr)
		return;
	std::vector<NE_Pin> exec_outputs = make_sequence_exec_outputs(node.custom_state.as<sequence_node_state>());
	if (equivalent_exec_pin_layout(node.exec_outputs, exec_outputs))
		return;
	graph.replace_exec_outputs(node.id, std::move(exec_outputs));
}

void sequence_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_sequence_node(graph, node);
}

void sequence_node::edit(NodeGraph& graph, NE_Node& node, sequence_node_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::TextDisabled("Runs each Then branch in order using the same incoming batch.");
	int then_count = std::max(state.then_count, 1);
	if (ImGui::InputInt("Then Count", &then_count)) {
		state.then_count = std::max(then_count, 1);
		sync_sequence_node(graph, node);
		graph.notify_graph_dirty();
	}
}

void sequence_node::configure(NodeTypeInfo& info) {
	info.meta.is_vm_node = true;
	info.meta.is_vm_callable = true;
	info.meta.vm_execution_shape = NodeTypeInfo::execution_shape::map;
	info.meta.compiled_visibility = NodeTypeInfo::compiled_branch_visibility::sequence_accumulate;
}

bool sequence_node::execute(rv::graph_execution_context&, NE_Node&, std::string&) {
	return true;
}

} // namespace rv::nodes
