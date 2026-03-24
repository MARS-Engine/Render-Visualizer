#include <render_visualizer/runtime/impl.hpp>

namespace rv {

// Graph traversal

std::vector<const NE_Link*> graph_runtime::incoming_exec_links(int node_id) const {
	std::vector<const NE_Link*> result;
	const NE_Node* node = graph.find_node(node_id);
	if (node == nullptr || !node->has_exec_input)
		return result;

	for (const auto& link : graph.links) {
		const NE_Node* from_node = graph.find_node(link.from_node);
		const NE_Node* to_node = graph.find_node(link.to_node);
		if (from_node == nullptr || to_node == nullptr || from_node->function_id != node->function_id || to_node->function_id != node->function_id)
			continue;
		if (link.to_node == node_id && link.to_pin == node->exec_input.id)
			result.push_back(&link);
	}

	std::ranges::sort(result, [&](const NE_Link* lhs, const NE_Link* rhs) {
		const NE_Node* lhs_node = graph.find_node(lhs->from_node);
		const NE_Node* rhs_node = graph.find_node(rhs->from_node);
		const float lhs_y = lhs_node ? lhs_node->pos.y : 0.0f;
		const float rhs_y = rhs_node ? rhs_node->pos.y : 0.0f;
		if (lhs_y == rhs_y)
			return lhs->from_node < rhs->from_node;
		return lhs_y < rhs_y;
	});
	return result;
}

std::vector<const NE_Link*> graph_runtime::outgoing_exec_links(const NE_Node& node) const {
	std::vector<const NE_Link*> result;
	if (node.exec_outputs.empty())
		return result;

	for (const auto& exec_pin : node.exec_outputs) {
		for (const auto& link : graph.links) {
			const NE_Node* from_node = graph.find_node(link.from_node);
			const NE_Node* to_node = graph.find_node(link.to_node);
			if (from_node == nullptr || to_node == nullptr || from_node->function_id != node.function_id || to_node->function_id != node.function_id)
				continue;
			if (link.from_node == node.id && link.from_pin == exec_pin.id) {
				result.push_back(&link);
				break;
			}
		}
	}
	return result;
}

bool graph_runtime::build_order_from_start(int node_id, std::unordered_set<int>& visited, std::unordered_map<int, bool>& reaches_end_cache, std::vector<int>& ordered_nodes, int& root_node_id) {
	if (const auto cached = reaches_end_cache.find(node_id); cached != reaches_end_cache.end()) {
		if (visited.insert(node_id).second)
			ordered_nodes.push_back(node_id);
		return cached->second;
	}

	const NE_Node* node = graph.find_node(node_id);
	const NodeTypeInfo* info = node == nullptr ? nullptr : registry.find(node->type);
	const bool inserted = visited.insert(node_id).second;
	if (inserted)
		ordered_nodes.push_back(node_id);
	else
		return false;

	bool reaches_end = info != nullptr && info->meta.is_end;
	if (reaches_end && root_node_id == -1)
		root_node_id = node_id;

	if (node != nullptr) {
		if (node->type == nodes::node_type_v<nodes::call_function_node_tag> && node->custom_state.storage != nullptr) {
			const auto& state = node->custom_state.as<nodes::call_function_node_state>();
			if (const NE_Node* callee_start = function_start_node(state.function_id); callee_start != nullptr) {
				if (build_order_from_start(callee_start->id, visited, reaches_end_cache, ordered_nodes, root_node_id))
					reaches_end = true;
			}
		}
		for (const NE_Link* link : outgoing_exec_links(*node)) {
			if (link == nullptr)
				continue;
			if (build_order_from_start(link->to_node, visited, reaches_end_cache, ordered_nodes, root_node_id))
				reaches_end = true;
		}
	}

	reaches_end_cache[node_id] = reaches_end;
	return reaches_end;
}

const NE_Node* graph_runtime::root_node() const {
	return plan.root_node_id == -1 ? nullptr : graph.find_node(plan.root_node_id);
}

const NE_Node* graph_runtime::function_start_node(int function_id) const {
	const graph_function_definition* function = graph.find_function(function_id);
	if (function == nullptr)
		return nullptr;
	const size_t expected_type =
		function->id == graph.setup_function_id() ? nodes::node_type_v<nodes::setup_start_node_tag> :
		function->id == graph.render_function_id() ? nodes::node_type_v<nodes::render_start_node_tag> :
		nodes::node_type_v<nodes::function_inputs_node_tag>;
	for (const NE_Node* node : graph.nodes_in_function(function_id)) {
		if (node->type == expected_type)
			return node;
	}
	return nullptr;
}

const NE_Node* graph_runtime::function_outputs_node(int function_id) const {
	for (const NE_Node* node : graph.nodes_in_function(function_id)) {
		if (node->type == nodes::node_type_v<nodes::function_outputs_node_tag>)
			return node;
	}
	return nullptr;
}

const NE_Link* graph_runtime::find_input_link(const NE_Node& node, std::string_view input_label) const {
	const NE_Pin* pin = nodes::find_pin_by_label(node.inputs, input_label);
	if (pin == nullptr)
		return nullptr;
	for (const auto& link : graph.links) {
		const NE_Node* from_node = graph.find_node(link.from_node);
		const NE_Node* to_node = graph.find_node(link.to_node);
		if (from_node == nullptr || to_node == nullptr || from_node->function_id != node.function_id || to_node->function_id != node.function_id)
			continue;
		if (link.to_node == node.id && link.to_pin == pin->id)
			return &link;
	}
	return nullptr;
}

const NE_Node* graph_runtime::find_linked_node(const NE_Node& node, std::string_view input_label) const {
	const NE_Link* link = find_input_link(node, input_label);
	return link == nullptr ? nullptr : graph.find_node(link->from_node);
}

} // namespace rv
