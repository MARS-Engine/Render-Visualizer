#include <render_visualizer/nodes/all.hpp>

namespace rv::nodes {

void refresh_dynamic_nodes(NodeGraph& graph) {
	// Custom function boundary nodes must exist before the general registry refresh runs.
	ensure_function_boundary_nodes(graph);
	refresh_generated_shader_interfaces(graph);
	if (const NodeRegistry* registry = graph.node_registry(); registry != nullptr) {
		for (auto& node : graph.nodes) {
			if (const NodeTypeInfo* info = registry->find(node.type); info != nullptr && info->hooks.refresh_dynamic_pins)
				info->hooks.refresh_dynamic_pins(graph, *info, node);
		}
	}
}

} // namespace rv::nodes
