#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/render_pass_node.hpp>

#include <render_visualizer/runtime/impl.hpp>

namespace rv::nodes {

void render_pass_node::edit(NodeGraph& graph, NE_Node& node, render_pass_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	bool changed = false;
	changed |= ImGui::ColorEdit4("Clear Color", &state.clear_color.x);
	int load_op = state.load_operation == MARS_RENDER_PASS_LOAD_OP_CLEAR ? 0 : 1;
	if (ImGui::Combo("Load Op", &load_op, "Clear\0Load\0")) {
		state.load_operation = load_op == 0 ? MARS_RENDER_PASS_LOAD_OP_CLEAR : MARS_RENDER_PASS_LOAD_OP_LOAD;
		changed = true;
	}
	changed |= ImGui::Checkbox("Depth Enabled", &state.depth_enabled);
	int depth_format = 0;
	if (ImGui::Combo("Depth Format", &depth_format, "D32_SFLOAT\0")) {
		state.depth_format = MARS_DEPTH_FORMAT_D32_SFLOAT;
		changed = true;
	}
	changed |= ImGui::InputFloat("Clear Depth", &state.clear_depth);
	if (changed)
		graph.notify_graph_dirty();
}

bool render_pass_node::build_render_pass(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Render Pass";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	auto& state = node.custom_state.as<render_pass_state>();
	runtime_detail::render_pass_resources resources;
	resources.params = {};
	resources.params.color_formats = { MARS_FORMAT_RGBA8_UNORM };
	resources.params.load_operation = state.load_operation;
	resources.params.depth_format = state.depth_enabled ? state.depth_format : MARS_DEPTH_FORMAT_UNDEFINED;
	resources.params.depth_clear_value = state.clear_depth;
	resources.clear_color = state.clear_color;
	resources.render_pass = mars::graphics::render_pass_create(*services->device, resources.params);
	resources.valid = resources.render_pass.engine != nullptr;
	if (!resources.valid) {
		resources.error = "Failed to create the render pass.";
		error = resources.error;
		result.status = error;
		return false;
	}

	resources.error.clear();
	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	if (!store_output_resource(*services, node, "render_pass", &stored, error)) {
		result.status = error;
		return false;
	}
	result.executed_count = 1;
	result.status = "Render pass ready";
	return true;
}

void render_pass_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::render_pass_resources>(services, node);
}

} // namespace rv::nodes
