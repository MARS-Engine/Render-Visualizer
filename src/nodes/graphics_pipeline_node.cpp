#include <render_visualizer/nodes/graphics_pipeline_node.hpp>

#include <imgui.h>

#include <render_visualizer/nodes/shader_interface_support.hpp>
#include <render_visualizer/nodes/shader_module_node.hpp>
#include <render_visualizer/nodes/virtual_struct.hpp>

#include <render_visualizer/runtime/impl.hpp>
#include <render_visualizer/ui/shared_editors.hpp>

#include <cctype>

namespace rv::nodes {

namespace {

constexpr size_t kGraphicsPipelineTypeHash = rv::detail::pin_type_hash<rv::resource_tags::graphics_pipeline>();

bool is_system_value_semantic(std::string_view semantic_label) {
	const semantic_info semantic = parse_semantic(semantic_label);
	return semantic.name.size() >= 3 && semantic.name[0] == 'S' && semantic.name[1] == 'V' && semantic.name[2] == '_';
}

void update_pipeline_pin_template(NE_Pin& pin, const NE_Node* shader_node) {
	if (shader_node == nullptr || shader_node->type != node_type_v<shader_module_node_tag>) {
		clear_pin_template_metadata(pin);
		return;
	}

	apply_pin_template_metadata(
		pin,
		kGraphicsPipelineTypeHash,
		static_cast<size_t>(shader_node->id + 1),
		shader_node->title
	);
}

const NE_Node* linked_shader_node(const NodeGraph& graph, const NE_Node& node) {
	const NE_Pin* shader_pin = find_pin_by_label(node.inputs, "shader");
	if (shader_pin == nullptr)
		return nullptr;
	for (const auto& link : graph.links) {
		if (link.to_node == node.id && link.to_pin == shader_pin->id)
			return graph.find_node(link.from_node);
	}
	return nullptr;
}

bool shader_requires_vertex_buffer(const shader_module_state& state) {
	return std::any_of(state.reflected_inputs.begin(), state.reflected_inputs.end(), [](const auto& input) {
		return !is_system_value_semantic(input.label);
	});
}

bool is_render_target_output_label(std::string_view label, size_t& index) {
	std::string normalized;
	normalized.reserve(label.size());
	for (const char c : label) {
		if (c == '_' || c == ' ')
			continue;
		normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	}

	std::string_view prefix;
	if (normalized.starts_with("svtarget"))
		prefix = "svtarget";
	else if (normalized.starts_with("target"))
		prefix = "target";
	else
		return false;

	index = 0;
	if (normalized.size() > prefix.size())
		index = static_cast<size_t>(std::strtoul(normalized.substr(prefix.size()).c_str(), nullptr, 10));
	return true;
}

size_t reflected_render_target_count(const shader_module_state& state) {
	size_t max_index = 0;
	bool has_target = false;
	for (const auto& output : state.reflected_outputs) {
		size_t index = 0;
		if (!is_render_target_output_label(output.label, index))
			continue;
		has_target = true;
		max_index = (std::max)(max_index, index);
	}
	if (!has_target && state.reflected_outputs.size() > 1u)
		return state.reflected_outputs.size();
	return has_target ? (max_index + 1u) : 1u;
}

} // namespace

void graphics_pipeline_node::edit_pipeline(NodeGraph& graph, NE_Node& node, graphics_pipeline_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	bool changed = false;
	const NE_Node* shader_node = linked_shader_node(graph, node);
	const auto* shader_state = shader_node != nullptr && shader_node->custom_state.storage != nullptr
		? &shader_node->custom_state.as<shader_module_state>()
		: nullptr;
	const bool needs_vertex_buffer = shader_state != nullptr && shader_requires_vertex_buffer(*shader_state);
	if (needs_vertex_buffer) {
		if (render_virtual_struct_selector(graph, state.schema_id, "Vertex Struct", "VertexStruct"))
			changed = true;
		if (const auto* schema = graph.find_virtual_struct(state.schema_id); schema != nullptr)
			ImGui::TextDisabled("Schema: %s", schema->name.c_str());
	} else {
		ImGui::TextDisabled("This shader uses only system vertex inputs like SV_VertexID.");
		ImGui::TextDisabled("No vertex buffer or vertex struct is required.");
	}
	int topology = static_cast<int>(state.topology);
	if (ImGui::Combo("Topology", &topology, "Triangle List\0Line List\0Point List\0")) {
		state.topology = static_cast<mars_pipeline_primitive_topology>(topology);
		changed = true;
	}
	changed |= ImGui::Checkbox("Depth Test", &state.depth_test_enable);
	changed |= ImGui::Checkbox("Depth Write", &state.depth_write_enable);
	int depth_compare = static_cast<int>(state.depth_compare);
	if (ImGui::Combo("Depth Compare", &depth_compare, "Never\0Less\0Equal\0Less Equal\0Greater\0Not Equal\0Greater Equal\0Always\0")) {
		state.depth_compare = static_cast<mars_compare_op>(depth_compare);
		changed = true;
	}
	if (changed)
		graph.notify_graph_dirty();
}

void graphics_pipeline_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	const NE_Node* shader_node = linked_shader_node(graph, node);
	for (auto& pin : node.static_outputs) {
		if (pin.label == "pipeline")
			update_pipeline_pin_template(pin, shader_node);
	}
	for (auto& pin : node.outputs) {
		if (pin.label == "pipeline")
			update_pipeline_pin_template(pin, shader_node);
	}
}

bool graphics_pipeline_node::build_pipeline(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Pipeline";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	auto& state = node.custom_state.as<graphics_pipeline_state>();
	runtime_detail::graphics_pipeline_resources resources;
	const NE_Node* shader_node = services->runtime->resolve_input_source_node(node, "shader", error);
	const NE_Node* pass_node = services->runtime->resolve_input_source_node(node, "render_pass", error);

	const runtime_detail::shader_module_resources* shader_module =
		read_input_resource<runtime_detail::shader_module_resources>(*services, node, "shader", error);
	if (shader_module == nullptr) {
		resources.error = error.empty() ? "Shader module is not ready." : error;
		error = resources.error;
		result.status = error;
		return false;
	}

	const runtime_detail::render_pass_resources* pass_resources =
		read_input_resource<runtime_detail::render_pass_resources>(*services, node, "render_pass", error);
	if (pass_resources == nullptr) {
		resources.error = error.empty() ? "Render pass is not ready." : error;
		error = resources.error;
		result.status = error;
		return false;
	}
	if (shader_node == nullptr || pass_node == nullptr) {
		resources.error = !error.empty() ? error : "Graphics pipeline inputs are incomplete.";
		error = resources.error;
		result.status = error;
		return false;
	}

	mars::pipeline_setup setup;
	setup.pipeline_shader = shader_module->shader;
	setup.primitive_topology = state.topology;
	setup.has_depth_test_override = true;
	setup.depth_test_enable = state.depth_test_enable;
	setup.has_depth_write_override = true;
	setup.depth_write_enable = state.depth_write_enable;
	setup.has_depth_compare_override = true;
	setup.depth_compare = state.depth_compare;

	const auto& shader_state = shader_node->custom_state.as<shader_module_state>();
	const bool requires_vertex_buffer = shader_requires_vertex_buffer(shader_state);
	const graph_shader_interface* shader_interface = resolved_pipeline_shader_interface(*services->graph, node, &error);
	if (shader_interface == nullptr) {
		resources.error = error.empty() ? "Graphics pipeline could not resolve a generated shader interface." : error;
		error = resources.error;
		result.status = error;
		return false;
	}
	if (!shader_interface->valid) {
		resources.error = shader_interface->diagnostics.empty() ? "Shader interface is unavailable." : shader_interface->diagnostics;
		error = resources.error;
		result.status = error;
		return false;
	}
	const virtual_struct_schema_state* vertex_schema = requires_vertex_buffer
		? services->graph->find_virtual_struct(state.schema_id)
		: nullptr;
	if (requires_vertex_buffer && vertex_schema == nullptr) {
		resources.error = !error.empty() ? error : "Graphics pipeline inputs are incomplete.";
		error = resources.error;
		result.status = error;
		return false;
	}

	if (requires_vertex_buffer) {
		std::string schema_diagnostics;
		if (!validate_virtual_struct_schema(*vertex_schema, schema_diagnostics)) {
			resources.error = "Vertex schema is invalid: " + schema_diagnostics;
			error = resources.error;
			result.status = error;
			return false;
		}

		for (const auto& input : shader_state.reflected_inputs) {
			if (is_system_value_semantic(input.label))
				continue;

			auto field_it = std::find_if(vertex_schema->fields.begin(), vertex_schema->fields.end(), [&](const virtual_struct_field& field) {
				return virtual_struct_semantic(field) == input.label;
			});
			if (field_it == vertex_schema->fields.end()) {
				resources.error = "Vertex schema is missing shader input semantic " + input.label + ".";
				error = resources.error;
				result.status = error;
				return false;
			}
			if (field_it->type_hash != input.type_hash) {
				resources.error = "Vertex schema semantic " + input.label + " has the wrong type for the shader.";
				error = resources.error;
				result.status = error;
				return false;
			}
		}

		size_t stride = 0;
		for (size_t field_index = 0; field_index < vertex_schema->fields.size(); ++field_index) {
			const auto& field = vertex_schema->fields[field_index];
			mars::pipeline_attribute_description attribute = {};
			attribute.binding = 0;
			attribute.location = field_index;
			attribute.offset = stride;
			attribute.input_format = virtual_struct_field_format(field);
			const std::string semantic_label = virtual_struct_semantic(field);
			const semantic_info semantic = parse_semantic(semantic_label);
			attribute.semantic_name = semantic.name;
			attribute.semantic_index = semantic.index;
			setup.attributes.push_back(std::move(attribute));
			stride += virtual_struct_field_size(field);
		}
		setup.bindings.push_back({ .stride = stride, .binding = 0 });
	}

	std::unordered_set<size_t> used_bindings;
	for (const auto& slot : shader_interface->slots) {
		if (!used_bindings.insert(slot.binding).second) {
			resources.error = "Shader resource binding " + std::to_string(slot.binding) + " is declared more than once in the generated shader interface.";
			error = resources.error;
			result.status = error;
			return false;
		}
		setup.descriptors.push_back({
			.stage = slot.stage,
			.descriptor_type = slot.kind == graph_shader_resource_kind::sampled_texture
				? MARS_PIPELINE_DESCRIPTOR_TYPE_IMAGE_SAMPLER
				: MARS_PIPELINE_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.binding = slot.binding
		});
	}

	resources.requires_vertex_buffer = requires_vertex_buffer;
	const size_t desired_render_target_count = reflected_render_target_count(shader_state);
	mars::render_pass pipeline_render_pass = pass_resources->render_pass;
	mars::render_pass temporary_render_pass = {};
	if (desired_render_target_count > pass_resources->params.color_formats.size()) {
		mars::render_pass_create_params expanded_params = pass_resources->params;
		const mars_format_type fallback_format = expanded_params.color_formats.empty()
			? MARS_FORMAT_RGBA8_UNORM
			: expanded_params.color_formats.front();
		expanded_params.color_formats.resize(desired_render_target_count, fallback_format);
		temporary_render_pass = mars::graphics::render_pass_create(*services->device, expanded_params);
		if (!temporary_render_pass.engine) {
			resources.error = "Failed to create an MRT-compatible render pass for the graphics pipeline.";
			error = resources.error;
			result.status = error;
			return false;
		}
		pipeline_render_pass = temporary_render_pass;
	}

	resources.pipeline = mars::graphics::pipeline_create(*services->device, pipeline_render_pass, setup);
	if (temporary_render_pass.engine)
		mars::graphics::render_pass_destroy(temporary_render_pass, *services->device);
	resources.valid = resources.pipeline.engine != nullptr;
	if (!resources.valid) {
		resources.error = "Failed to create the graphics pipeline.";
		error = resources.error;
		result.status = error;
		return false;
	}

	if (requires_vertex_buffer) {
		resources.vertex_schema_name = vertex_schema->name;
		resources.vertex_schema_layout_fingerprint = virtual_struct_layout_fingerprint(*vertex_schema);
	} else {
		resources.vertex_schema_name.clear();
		resources.vertex_schema_layout_fingerprint = 0;
	}
	resources.error.clear();
	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	if (!store_output_resource(*services, node, "pipeline", &stored, error)) {
		result.status = error;
		return false;
	}
	result.executed_count = 1;
	result.status = requires_vertex_buffer
		? ("Pipeline ready for schema '" + vertex_schema->name + "'")
		: "Pipeline ready without vertex buffer inputs";
	return true;
}

void graphics_pipeline_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::graphics_pipeline_resources>(services, node);
}

} // namespace rv::nodes
