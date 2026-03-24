#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/texture_resource_node.hpp>

#include <imgui.h>

#include <render_visualizer/runtime/impl.hpp>
#include <render_visualizer/ui/shared_editors.hpp>

namespace rv::nodes {

texture_slot_state make_default_texture_slot(std::string name) {
	texture_slot_state slot;
	slot.name = std::move(name);
	slot.status = "No file selected.";
	return slot;
}

int ensure_default_texture_slot(NodeGraph& graph, std::string name) {
	if (texture_slot_state* slot = graph.create_texture_slot(make_default_texture_slot(std::move(name))); slot != nullptr)
		return slot->id;
	return -1;
}

void texture_resource_node::configure(NodeTypeInfo& info) {
	info.pins.outputs = {
		make_pin<mars::vector3<unsigned char>>("texture"),
		make_pin<unsigned int>("index", false)
	};
}

void texture_resource_node::edit(NodeGraph& graph, NE_Node& node, texture_slot_node_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::Separator();
	render_texture_slot_selector(graph, state.slot_id, "Slot", "Texture");
	if (const auto* slot = graph.find_texture_slot(state.slot_id); slot != nullptr) {
		ImGui::Spacing();
		ImGui::Text("Slot: %s", slot->name.c_str());
		ImGui::TextDisabled("Index Output: %d", slot->id);
		if (!slot->path.empty())
			ImGui::TextWrapped("%s", slot->path.c_str());
		ImGui::TextDisabled("%s", slot->status.c_str());
	} else {
		ImGui::TextWrapped("Select an existing texture slot or create a new one to reference a shared external texture. This node also outputs the slot index as a uint for ResourceDescriptorHeap-style shaders.");
	}
}

bool texture_resource_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Texture";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	if (node.custom_state.storage == nullptr) {
		error = "Texture Slot state is missing.";
		result.status = error;
		return false;
	}
	const auto& state = node.custom_state.as<texture_slot_node_state>();
	if (state.slot_id == -1) {
		error = "Texture Slot has no shared slot selected.";
		result.status = error;
		return false;
	}

	std::string status;
	if (!services->runtime->ensure_texture_slot_resource(state.slot_id, status)) {
		error = status;
		result.status = error;
		return false;
	}
	runtime_detail::texture_slot_resources* resources =
		services->runtime->shared_texture_slot_resource(state.slot_id);
	if (resources == nullptr || !resources->valid || !resources->texture.engine) {
		error = status.empty() ? "Texture Slot has no ready runtime resource." : status;
		result.status = error;
		return false;
	}
	if (!store_output_resource(*services, node, "texture", resources, error)) {
		result.status = error;
		return false;
	}
	const unsigned int srv_index = mars::graphics::texture_get_srv_index(resources->texture);
	if (!store_output_value(*services, node, "index", srv_index, error)) {
		result.status = error;
		return false;
	}

	result.executed_count = 1;
	result.status = status;
	return true;
}

} // namespace rv::nodes
