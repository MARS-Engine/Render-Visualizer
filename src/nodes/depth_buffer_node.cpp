#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/simple_nodes.hpp>

#include <render_visualizer/runtime/impl.hpp>

namespace rv::nodes {

void depth_buffer_node::describe(NodeGraph&, NE_Node& node) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::TextDisabled("Creates an explicit depth resource for the linked render pass.");
}

bool depth_buffer_node::build_depth(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Depth";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	runtime_detail::depth_buffer_resources resources;
	const runtime_detail::render_pass_resources* pass_resources =
		read_input_resource<runtime_detail::render_pass_resources>(*services, node, "render_pass", error);
	if (pass_resources == nullptr) {
		resources.error = error.empty() ? "Render pass input is not ready." : error;
		error = resources.error;
		result.status = error;
		return false;
	}
	if (pass_resources->params.depth_format == MARS_DEPTH_FORMAT_UNDEFINED) {
		resources.error = "Linked render pass has depth disabled.";
		error = resources.error;
		result.status = error;
		return false;
	}

	mars::depth_buffer_create_params params = {};
	params.size = services->frame_size;
	params.format = pass_resources->params.depth_format;
	params.clear_depth = pass_resources->params.depth_clear_value;
	params.sampled = true;
	resources.depth = mars::graphics::depth_buffer_create(*services->device, params);
	if (!resources.depth.engine) {
		resources.error = "Failed to create the depth buffer.";
		error = resources.error;
		result.status = error;
		return false;
	}

	resources.extent = services->frame_size;
	resources.valid = true;
	resources.error.clear();
	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	if (!store_output_resource(*services, node, "depth_buffer", &stored, error)) {
		result.status = error;
		return false;
	}
	result.executed_count = 1;
	result.status = "Depth buffer ready";
	return true;
}

void depth_buffer_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::depth_buffer_resources>(services, node);
}

} // namespace rv::nodes
