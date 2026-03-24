#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/matrix_node.hpp>

namespace rv::nodes {

void matrix_node::configure(NodeTypeInfo& info) {
	info.meta.vm_execution_shape = NodeTypeInfo::execution_shape::expand;
}

void matrix_node::describe(NodeGraph&, NE_Node& node) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::TextDisabled("Expands each incoming execution item into x * y items.");
	ImGui::TextDisabled("Outputs centered row-major float3 offsets and a zero-based uint index.");
}

bool matrix_node::expand_grid(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	unsigned int x = 1;
	unsigned int y = 1;
	float spacing = 2.5f;
	if (!ctx.resolve_input<unsigned int>(node, "x", x, error))
		return false;
	if (!ctx.resolve_input<unsigned int>(node, "y", y, error))
		return false;
	if (!ctx.resolve_input<float>(node, "spacing", spacing, error))
		return false;

	const float x_center = (static_cast<float>(x) - 1.0f) * 0.5f;
	const float y_center = (static_cast<float>(y) - 1.0f) * 0.5f;

	for (unsigned int row = 0; row < y; ++row) {
		for (unsigned int column = 0; column < x; ++column) {
			ctx.set_output(node, "position", mars::vector3<float> {
				(static_cast<float>(column) - x_center) * spacing,
				(static_cast<float>(row) - y_center) * spacing,
				0.0f
			});
			ctx.set_output(node, "index", row * x + column);
		}
	}
	return true;
}

} // namespace rv::nodes
