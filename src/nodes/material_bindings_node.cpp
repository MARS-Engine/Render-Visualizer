#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/material_bindings_node.hpp>

#include <render_visualizer/runtime/impl.hpp>
#include <render_visualizer/ui/shared_editors.hpp>

namespace rv::nodes {

namespace {

const NE_Node* linked_pipeline_node(const NodeGraph& graph, const NE_Node& node) {
	const NE_Pin* pin = find_pin_by_label(node.inputs, "pipeline");
	if (pin == nullptr)
		return nullptr;
	for (const auto& link : graph.links) {
		if (link.to_node != node.id || link.to_pin != pin->id)
			continue;
		const NE_Node* source = graph.find_node(link.from_node);
		return source == nullptr ? nullptr : resolve_pipeline_source_node(graph, *source);
	}
	return nullptr;
}

const graph_shader_interface* linked_pipeline_shader_interface(const NodeGraph& graph, const NE_Node& node) {
	const NE_Node* pipeline_node = linked_pipeline_node(graph, node);
	if (pipeline_node == nullptr)
		return nullptr;
	return resolved_pipeline_shader_interface(graph, *pipeline_node);
}

} // namespace

int* find_selected_texture_slot(material_bindings_state& state, std::string_view label) {
	auto it = state.sampled_texture_slots.find(std::string(label));
	return it == state.sampled_texture_slots.end() ? nullptr : &it->second;
}

const int* find_selected_texture_slot(const material_bindings_state& state, std::string_view label) {
	auto it = state.sampled_texture_slots.find(std::string(label));
	return it == state.sampled_texture_slots.end() ? nullptr : &it->second;
}

int& ensure_selected_texture_slot(material_bindings_state& state, std::string_view label) {
	return state.sampled_texture_slots.try_emplace(std::string(label), -1).first->second;
}

const NE_Link* find_material_binding_input_link(const NodeGraph& graph, const NE_Node& node, std::string_view label) {
	const NE_Pin* pin = find_pin_by_label(node.inputs, label);
	if (pin == nullptr)
		return nullptr;
	for (const auto& link : graph.links)
		if (link.to_node == node.id && link.to_pin == pin->id)
			return &link;
	return nullptr;
}

void prune_material_binding_state(material_bindings_state& state, const graph_shader_interface* shader_interface) {
	if (shader_interface == nullptr) {
		state.sampled_texture_slots.clear();
		return;
	}
	std::unordered_map<std::string, int> kept;
	for (const auto& slot : shader_interface->slots) {
		if (slot.kind != graph_shader_resource_kind::sampled_texture)
			continue;
		if (const int* selected = find_selected_texture_slot(state, slot.label); selected != nullptr)
			kept.emplace(slot.label, *selected);
		else
			kept.emplace(slot.label, -1);
	}
	state.sampled_texture_slots = std::move(kept);
}

std::vector<NE_Pin> make_material_binding_inputs(const graph_shader_interface& shader_interface) {
	std::vector<NE_Pin> pins;
	pins.reserve(shader_interface.slots.size());
	for (const auto& slot : shader_interface.slots) {
		if (slot.kind == graph_shader_resource_kind::uniform_value)
			pins.push_back(make_uniform_value_pin(uniform_kind_from_type_hash(slot.type_hash), slot.label));
		else
			pins.push_back(make_pin<rv::resource_tags::texture_slot>(slot.label, false, false));
	}
	return pins;
}

void sync_material_bindings_node(NodeGraph& graph, NE_Node& node) {
	std::vector<NE_Pin> generated_inputs;
	if (const graph_shader_interface* shader_interface = linked_pipeline_shader_interface(graph, node); shader_interface != nullptr && shader_interface->valid)
		generated_inputs = make_material_binding_inputs(*shader_interface);
	if (equivalent_pin_layout(node.generated_inputs, generated_inputs))
		return;
	graph.replace_generated_pins(node.id, std::move(generated_inputs), {});
}

void refresh_dynamic_material_binding_nodes(NodeGraph& graph) {
	for (auto& node : graph.nodes) {
		if (node.type != node_type_v<material_bindings_node_tag>)
			continue;
		sync_material_bindings_node(graph, node);
	}
}

void material_bindings_node::configure(NodeTypeInfo& info) {
	info.pins.inputs = {
		make_pin<rv::resource_tags::graphics_pipeline>("pipeline")
	};
	info.pins.outputs = {
		make_pin<rv::resource_tags::material_resource>("material")
	};
	info.meta.show_in_spawn_menu = false;
}

std::string material_bindings_node::save(const material_bindings_state& state) {
	material_bindings_state copy = state;
	return json_stringify(copy);
}

bool material_bindings_node::load(material_bindings_state& state, std::string_view json, std::string& error) {
	if (!json_parse(json, state)) {
		error = "Failed to parse material bindings state.";
		return false;
	}
	return true;
}

void material_bindings_node::edit(NodeGraph& graph, NE_Node& node, material_bindings_state& state) {
	sync_material_bindings_node(graph, node);
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::Separator();
	const graph_shader_interface* shader_interface = linked_pipeline_shader_interface(graph, node);
	if (shader_interface == nullptr) {
		prune_material_binding_state(state, nullptr);
		state.diagnostics = "Connect a Graphics Pipeline to configure material slots.";
		ImGui::TextWrapped("%s", state.diagnostics.c_str());
		return;
	}
	prune_material_binding_state(state, shader_interface);
	if (!shader_interface->valid) {
		state.diagnostics = shader_interface->diagnostics;
		ImGui::TextWrapped("%s", state.diagnostics.c_str());
		return;
	}
	if (shader_interface->slots.empty()) {
		state.diagnostics = "No reflected shader resource slots are available.";
		ImGui::TextWrapped("%s", state.diagnostics.c_str());
		return;
	}
	state.diagnostics = "Resolved " + std::to_string(shader_interface->slots.size()) + " material slot(s).";
	ImGui::TextWrapped("%s", state.diagnostics.c_str());
	ImGui::Spacing();
	for (const auto& slot : shader_interface->slots) {
		ImGui::BulletText("%s | binding %zu | %s | %s", slot.label.c_str(), slot.binding, shader_resource_kind_name(slot.kind), pipeline_stage_name(slot.stage));
		if (slot.kind != graph_shader_resource_kind::sampled_texture)
			continue;
		ImGui::PushID(slot.label.c_str());
		int& selected_slot_id = ensure_selected_texture_slot(state, slot.label);
		const bool changed = render_texture_slot_selector(graph, selected_slot_id, "Texture Slot", slot.label.c_str());
		(void)changed;
		if (const NE_Link* link = find_material_binding_input_link(graph, node, slot.label); link != nullptr) {
			const NE_Node* source = graph.find_node(link->from_node);
			if (source != nullptr) {
				const std::string message = "Linked texture node overrides sidebar selection: " + source->title;
				ImGui::TextDisabled("%s", message.c_str());
			} else {
				ImGui::TextDisabled("Linked texture node overrides sidebar selection.");
			}
		} else if (const auto* texture_slot = graph.find_texture_slot(selected_slot_id); texture_slot != nullptr) {
			if (!texture_slot->path.empty())
				ImGui::TextWrapped("%s", texture_slot->path.c_str());
			if (!texture_slot->status.empty())
				ImGui::TextDisabled("%s", texture_slot->status.c_str());
		} else {
			ImGui::TextDisabled("No shared texture slot selected.");
		}
		ImGui::PopID();
	}
}

void material_bindings_node::on_connect(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node, const NE_Link&) {
	sync_material_bindings_node(graph, node);
}

void material_bindings_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_material_bindings_node(graph, node);
}

bool material_bindings_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Material";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;
	auto& state = node.custom_state.as<material_bindings_state>();
	const graph_shader_interface* shader_interface = linked_pipeline_shader_interface(*services->graph, node);
	if (shader_interface == nullptr) {
		state.diagnostics = "Material Bindings is missing its Graphics Pipeline input.";
		error = state.diagnostics;
		result.status = error;
		return false;
	}
	if (!shader_interface->valid) {
		state.diagnostics = shader_interface->diagnostics;
		error = state.diagnostics;
		result.status = error;
		return false;
	}
	if (!equivalent_pin_layout(node.generated_inputs, make_material_binding_inputs(*shader_interface))) {
		state.diagnostics = "Material slot pins are out of date. Reconnect the Graphics Pipeline.";
		error = state.diagnostics;
		result.status = error;
		return false;
	}
	for (const auto& slot : shader_interface->slots) {
		const NE_Link* link = find_material_binding_input_link(*services->graph, node, slot.label);
		const auto* selected_slot = find_selected_texture_slot(state, slot.label);
		const bool has_sidebar_texture = slot.kind == graph_shader_resource_kind::sampled_texture &&
			selected_slot != nullptr && *selected_slot != -1;
		if (link == nullptr && !has_sidebar_texture) {
			state.diagnostics = "Missing binding for slot '" + slot.label + "'.";
			error = state.diagnostics;
			result.status = error;
			return false;
		}
	}
	state.diagnostics = "Resolved " + std::to_string(shader_interface->slots.size()) + " material slot(s).";
	result.executed_count = shader_interface->slots.size();
	result.status = state.diagnostics;
	return true;
}

} // namespace rv::nodes
