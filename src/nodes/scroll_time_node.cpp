#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/simple_nodes.hpp>
#include <render_visualizer/runtime/impl.hpp>

namespace rv::nodes {

std::vector<std::byte> make_scroll_time_bytes(const scroll_time_state& state) {
	std::vector<std::byte> bytes;
	bytes.reserve(16);
	append_bytes(bytes, state.value.x);
	append_bytes(bytes, state.value.y);
	// Pad to a float4-sized payload so the CPU buffer matches the shader's uniform alignment.
	append_bytes(bytes, 0.0f);
	append_bytes(bytes, 0.0f);
	return bytes;
}

void scroll_time_node::tick(NE_Node& node, float delta_time) {
	if (node.custom_state.storage == nullptr)
		return;
	auto& state = node.custom_state.as<scroll_time_state>();
	state.value.x += state.speed.x * delta_time;
	state.value.y += state.speed.y * delta_time;
	while (state.value.x >= 1.0f) state.value.x -= 1.0f;
	while (state.value.y >= 1.0f) state.value.y -= 1.0f;
	while (state.value.x < 0.0f) state.value.x += 1.0f;
	while (state.value.y < 0.0f) state.value.y += 1.0f;
}

void scroll_time_node::configure(NodeTypeInfo& info) {
	info.meta.vm_execution_shape = NodeTypeInfo::execution_shape::map;
}

void scroll_time_node::edit(NodeGraph& graph, NE_Node& node, scroll_time_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::TextDisabled("Accumulates delta time and wraps to [0, 1).");
	ImGui::Separator();
	if (ImGui::InputFloat2("Speed", &state.speed.x))
		graph.notify_graph_dirty();
	ImGui::BeginDisabled();
	ImGui::InputFloat2("Value", &state.value.x);
	ImGui::EndDisabled();
	if (ImGui::Button("Reset", { -1.0f, 0.0f })) {
		state.value = {0.0f, 0.0f};
		graph.notify_graph_dirty();
	}
}

bool scroll_time_node::emit_offset(rv::graph_execution_context& ctx, NE_Node& node, std::string&) {
	ctx.set_output(node, "offset", node.custom_state.as<scroll_time_state>().value);
	return true;
}

bool swapchain_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Swapchain";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr) return false;
	const bool ok = services->runtime->ensure_present_output(node);
	result.executed_count = ok ? 1 : 0;
	result.status = ok ? "Ready to present" : "Swapchain input texture is missing";
	if (!ok) error = result.status;
	return ok;
}

} // namespace rv::nodes
