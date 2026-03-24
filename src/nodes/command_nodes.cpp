#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/command_nodes.hpp>

#include <render_visualizer/runtime/impl.hpp>
#include <render_visualizer/ui/shared_editors.hpp>

namespace rv::nodes {

namespace {

bool mark_command_ready(rv::graph_build_result& result) {
	result.kind = "Command";
	result.status = "Ready";
	result.executed_count = 1;
	return true;
}

void sync_pipeline_input_display(NodeGraph& graph, NE_Node& node) {
	const NE_Pin* pipeline_pin = find_pin_by_label(node.inputs, "pipeline");
	const NE_Pin* source_pin = nullptr;
	if (pipeline_pin != nullptr) {
		for (const auto& link : graph.links) {
			if (link.to_node != node.id || link.to_pin != pipeline_pin->id)
				continue;
			const NE_Node* source_node = graph.find_node(link.from_node);
			source_pin = source_node == nullptr ? nullptr : find_pin_by_id(source_node->outputs, link.from_pin);
			break;
		}
	}

	for (auto& pin : node.static_inputs) {
		if (pin.label != "pipeline")
			continue;
		if (source_pin != nullptr && source_pin->template_base_type_hash != 0)
			apply_pin_template_metadata(pin, source_pin->template_base_type_hash, source_pin->template_value_hash, source_pin->template_display_name);
		else
			clear_pin_template_metadata(pin);
	}
	for (auto& pin : node.inputs) {
		if (pin.label != "pipeline")
			continue;
		if (source_pin != nullptr && source_pin->template_base_type_hash != 0)
			apply_pin_template_metadata(pin, source_pin->template_base_type_hash, source_pin->template_value_hash, source_pin->template_display_name);
		else
			clear_pin_template_metadata(pin);
	}
}

bool resolve_bound_texture(
	graph_services& services,
	const NE_Node& node,
	std::string_view input_label,
	mars::texture& out_texture,
	std::string& error
) {
	if (const runtime_detail::texture_slot_resources* texture_resources =
			read_input_resource<runtime_detail::texture_slot_resources>(services, node, input_label, error);
		texture_resources != nullptr && texture_resources->texture.engine) {
		out_texture = texture_resources->texture;
		error.clear();
		return true;
	}

	std::string framebuffer_error;
	if (const runtime_detail::framebuffer_resources* framebuffer_resources =
			read_input_resource<runtime_detail::framebuffer_resources>(services, node, input_label, framebuffer_error);
		framebuffer_resources != nullptr && !framebuffer_resources->targets.empty() && framebuffer_resources->targets.front().engine) {
		out_texture = framebuffer_resources->targets.front();
		error.clear();
		return true;
	}

	if (const runtime_detail::framebuffer_attachment_resources* attachment_resources =
			read_input_resource<runtime_detail::framebuffer_attachment_resources>(services, node, input_label, framebuffer_error);
		attachment_resources != nullptr &&
		attachment_resources->owner != nullptr &&
		attachment_resources->attachment_index < attachment_resources->owner->targets.size() &&
		attachment_resources->owner->targets[attachment_resources->attachment_index].engine) {
		out_texture = attachment_resources->owner->targets[attachment_resources->attachment_index];
		error.clear();
		return true;
	}

	if (error.empty())
		error = framebuffer_error;
	if (error.empty())
		error = "Texture source '" + std::string(input_label) + "' has no loaded GPU texture.";
	return false;
}

} // namespace

void sync_bind_vertex_buffers_node(NodeGraph& graph, NE_Node& node) {
	std::vector<NE_Pin> generated_inputs;
	if (node.custom_state.storage == nullptr)
		return;
	const auto& state = node.custom_state.as<bind_vertex_buffers_state>();
	if (const virtual_struct_schema_state* schema = graph.find_virtual_struct(state.schema_id); schema != nullptr) {
		NE_Pin pin = make_pin<rv::resource_tags::vertex_buffer>("vertex_buffer");
		apply_virtual_struct_schema(pin, *schema);
		generated_inputs.push_back(std::move(pin));
	}

	if (equivalent_pin_layout(node.generated_inputs, generated_inputs))
		return;
	graph.replace_generated_pins(node.id, std::move(generated_inputs), {});
}

void refresh_dynamic_bind_vertex_buffer_nodes(NodeGraph& graph) {
	for (auto& node : graph.nodes) {
		if (node.type != node_type_v<bind_vertex_buffers_node_tag>)
			continue;
		sync_bind_vertex_buffers_node(graph, node);
	}
}

bool begin_render_pass_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (ctx.runtime == nullptr) { error = "Execution context has no runtime."; return false; }
	return ctx.runtime->execute_begin_render_pass(node, error);
}
bool begin_render_pass_node::build(rv::graph_build_context&, NE_Node&, rv::graph_build_result& result, std::string&) { return mark_command_ready(result); }

void bind_pipeline_node::on_connect(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node, const NE_Link&) {
	sync_pipeline_input_display(graph, node);
}

void bind_pipeline_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_pipeline_input_display(graph, node);
}

bool bind_pipeline_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (ctx.runtime == nullptr) { error = "Execution context has no runtime."; return false; }
	return ctx.runtime->execute_bind_pipeline(node, error);
}
bool bind_pipeline_node::build(rv::graph_build_context&, NE_Node&, rv::graph_build_result& result, std::string&) { return mark_command_ready(result); }

void bind_resources_node::on_connect(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node, const NE_Link&) {
	bind_resources_node::refresh(graph, {}, node);
}

void bind_resources_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_pipeline_input_display(graph, node);
	std::vector<NE_Pin> generated_inputs;
	const NE_Pin* pipeline_pin = find_pin_by_label(node.inputs, "pipeline");
	if (pipeline_pin != nullptr) {
		for (const auto& link : graph.links) {
			if (link.to_node != node.id || link.to_pin != pipeline_pin->id)
				continue;
			const NE_Node* raw_source_node = graph.find_node(link.from_node);
			const NE_Node* pipeline_node = raw_source_node == nullptr ? nullptr : resolve_pipeline_source_node(graph, *raw_source_node);
			if (pipeline_node == nullptr)
				break;
			std::string error;
			const graph_shader_interface* shader_interface = resolved_pipeline_shader_interface(graph, *pipeline_node, &error);
			if (shader_interface == nullptr || !shader_interface->valid)
				break;
			generated_inputs.reserve(shader_interface->slots.size());
			for (const auto& slot : shader_interface->slots)
				generated_inputs.push_back(make_shader_interface_slot_pin(slot));
			break;
		}
	}
	if (equivalent_pin_layout(node.generated_inputs, generated_inputs))
		return;
	graph.replace_generated_pins(node.id, std::move(generated_inputs), {});
}

bool bind_resources_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Command";
	bool build_succeeded = false;
	struct failure_guard {
		bool& build_succeeded;
		rv::graph_build_result& result;
		std::string& error;

		~failure_guard() {
			if (build_succeeded || !error.empty() || !result.status.empty())
				return;
			error = "Bind Resources build returned false without setting an error.";
			result.status = error;
		}
	} failure_context_guard { build_succeeded, result, error };
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	constexpr size_t execution_variant_capacity = 256u;
	const size_t frame_variant_count = std::max<size_t>(1u, services->swapchain_size);
	runtime_detail::material_binding_resources resources;
	const auto describe_slot = [](const runtime_detail::bind_resource_slot& slot) {
		std::string description = "'";
		description += slot.label;
		description += "' (kind=";
		description += shader_resource_kind_name(slot.kind);
		if (slot.kind == graph_shader_resource_kind::uniform_value) {
			description += ", uniform=";
			description += uniform_value_kind_name(slot.uniform_kind);
		}
		description += ")";
		return description;
	};
	const auto fail = [&](std::string message, bool destroy_resources = false) {
		resources.error = std::move(message);
		if (destroy_resources)
			resources.destroy(*services->device);
		error = resources.error;
		result.status = error;
		return false;
	};
	const runtime_detail::graphics_pipeline_resources* pipeline_resources =
		read_input_resource<runtime_detail::graphics_pipeline_resources>(*services, node, "pipeline", error);
	if (pipeline_resources == nullptr)
		return fail(error.empty() ? "Bind Resources pipeline input is not ready." : error);

	const NE_Node* pipeline_node = services->runtime->resolve_input_source_node(node, "pipeline", error);
	if (pipeline_node == nullptr)
		return fail(error.empty() ? "Bind Resources could not resolve its pipeline input source node." : error);
	pipeline_node = resolve_pipeline_source_node(*services->graph, *pipeline_node);
	if (pipeline_node == nullptr)
		return fail("Bind Resources requires a Graphics Pipeline source.");

	const graph_shader_interface* shader_interface = resolved_pipeline_shader_interface(*services->graph, *pipeline_node, &error);
	if (shader_interface == nullptr)
		return fail(error.empty() ? "Bind Resources requires a pipeline whose shader resolves to a generated shader interface." : error);
	if (!shader_interface->valid)
		return fail(shader_interface->diagnostics.empty() ? "Bind Resources could not use the generated shader interface." : shader_interface->diagnostics);
	if (shader_interface->slots.empty())
		return fail("Bind Resources requires a pipeline whose shader reflects bindable resources.");

	std::vector<NE_Pin> expected_inputs;
	for (const auto& slot : shader_interface->slots)
		expected_inputs.push_back(make_shader_interface_slot_pin(slot));
	if (!equivalent_pin_layout(node.generated_inputs, expected_inputs))
		return fail("Bind Resources inputs are out of date. Reconnect the Graphics Pipeline.");

	mars::descriptor_create_params pool_params = {};
	std::vector<mars::descriptor_set_create_params> set_params(frame_variant_count);
	pool_params.max_sets = set_params.size() * execution_variant_capacity;

	for (const auto& slot : shader_interface->slots) {
		runtime_detail::bind_resource_slot entry;
		entry.label = slot.label;
		entry.kind = slot.kind;
		entry.uniform_kind = uniform_kind_from_type_hash(slot.type_hash);
		entry.binding = slot.binding;

		if (slot.kind == graph_shader_resource_kind::uniform_value) {
			const runtime_detail::resolved_value resolved = services->runtime->resolve_input_source(node, slot.label);
			std::vector<std::byte> resolved_bytes;
			size_t resolved_element_count = 0;
			if (!services->runtime->copy_variable_payload(resolved, resolved_bytes, resolved_element_count)) {
				return fail(
					resolved.status.empty()
						? ("Bind Resources requires a CPU value for " + describe_slot(entry) + ".")
						: ("Bind Resources input " + describe_slot(entry) + " failed: " + resolved.status),
					true
				);
			}
			if (resolved_bytes.empty()) {
				return fail("Bind Resources input " + describe_slot(entry) + " resolved to an empty CPU payload.", true);
			}

			uniform_data_state zero_state;
			zero_state.kind = entry.uniform_kind;
			zero_state.float_values = {0.0f, 0.0f, 0.0f, 0.0f};
			zero_state.uint_values = {0u, 0u, 0u, 0u};
			const std::vector<std::byte> payload = make_uniform_bytes(zero_state);
			entry.execution_buffers.resize(execution_variant_capacity);
			for (auto& execution_buffer : entry.execution_buffers) {
				if (!services->runtime->ensure_uniform_payload(execution_buffer, payload, entry.buffer.error)) {
					return fail(
						entry.buffer.error.empty()
							? ("Failed to create uniform buffer for " + describe_slot(entry) + ".")
							: ("Bind Resources input " + describe_slot(entry) + " failed: " + entry.buffer.error),
						true
					);
				}
			}
			pool_params.descriptors_size[MARS_DESCRIPTOR_TYPE_UNIFORM_BUFFER] += set_params.size() * execution_variant_capacity;
		} else {
			const NE_Link* link = find_input_link(*services->graph, node, slot.label);
			if (link == nullptr)
				return fail("Missing Bind Resources input " + describe_slot(entry) + ".", true);

			mars::texture bound_texture;
			if (!resolve_bound_texture(*services, node, slot.label, bound_texture, error))
				return fail(
					error.empty()
						? ("Bind Resources input " + describe_slot(entry) + " failed without a texture error.")
						: ("Bind Resources input " + describe_slot(entry) + " failed: " + error),
					true
				);
			entry.texture_slot_id = -1;
			pool_params.descriptors_size[MARS_DESCRIPTOR_TYPE_SAMPLED_IMAGE] += set_params.size() * execution_variant_capacity;
		}

		resources.slots.push_back(std::move(entry));
	}

	resources.descriptor = mars::graphics::descriptor_create(*services->device, pool_params, set_params.size());
	if (!resources.descriptor.engine)
		return fail("Failed to allocate bind resource descriptor resources.", true);

	resources.execution_sets.reserve(execution_variant_capacity);
	for (size_t execution_index = 0; execution_index < execution_variant_capacity; ++execution_index) {
		std::vector<mars::descriptor_set_create_params> execution_params(frame_variant_count);
		for (auto& slot : resources.slots) {
			for (auto& params : execution_params) {
				if (slot.kind == graph_shader_resource_kind::uniform_value) {
					params.buffers.emplace_back(slot.execution_buffers[execution_index].buffer, slot.binding);
					continue;
				}
				mars::texture bound_texture;
				if (!resolve_bound_texture(*services, node, slot.label, bound_texture, error))
					return fail(
						error.empty()
							? ("Bind Resources input " + describe_slot(slot) + " failed without a texture error.")
							: ("Bind Resources input " + describe_slot(slot) + " failed: " + error),
						true
					);
				params.textures.push_back({
					.image = bound_texture,
					.binding = slot.binding,
					.descriptor_type = MARS_PIPELINE_DESCRIPTOR_TYPE_IMAGE_SAMPLER,
				});
			}
		}
		resources.execution_sets.push_back(mars::graphics::descriptor_set_create(
			resources.descriptor,
			*services->device,
			pipeline_resources->pipeline,
			execution_params
		));
	}

	resources.valid = !resources.execution_sets.empty();
	if (!resources.valid)
		return fail("Failed to create the bind resource descriptor set for the reflected shader resources.", true);

	resources.error.clear();
	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	store_node_resource(*services, node, &stored);
	result.executed_count = shader_interface->slots.size();
	result.status = "Bind Resources ready";
	build_succeeded = true;
	return true;
}
bool bind_resources_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (ctx.runtime == nullptr) { error = "Execution context has no runtime."; return false; }
	return ctx.runtime->execute_bind_resources(node, error);
}

void bind_resources_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::material_binding_resources>(services, node);
}

void bind_vertex_buffers_node::configure(NodeTypeInfo& info) {
	info.meta.is_vm_node = true;
	info.meta.is_vm_callable = true;
}

void bind_vertex_buffers_node::edit(NodeGraph& graph, NE_Node& node, bind_vertex_buffers_state& state) {
	sync_bind_vertex_buffers_node(graph, node);
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::Separator();
	if (render_virtual_struct_selector(graph, state.schema_id, "Struct", "VertexStruct"))
		sync_bind_vertex_buffers_node(graph, node);
	const virtual_struct_schema_state* schema = graph.find_virtual_struct(state.schema_id);
	if (schema == nullptr) {
		ImGui::TextWrapped("Select a virtual struct when you want to bind a typed vertex buffer. Leave this out entirely for shaders that only use system inputs like SV_VertexID.");
		return;
	}
	ImGui::Text("Schema: %s", schema->name.c_str());
	ImGui::TextDisabled("Waiting for a typed vertex buffer input.");
}

void bind_vertex_buffers_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) { sync_bind_vertex_buffers_node(graph, node); }
bool bind_vertex_buffers_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Command";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	if (node.custom_state.storage == nullptr) {
		error = "Bind Vertex Buffers state is missing.";
		result.status = error;
		return false;
	}
	const auto& node_state = node.custom_state.as<bind_vertex_buffers_state>();
	const virtual_struct_schema_state* schema = services->graph->find_virtual_struct(node_state.schema_id);
	if (schema == nullptr) {
		error = "Bind Vertex Buffers is missing its Virtual Struct schema. Remove this node if the shader does not use vertex buffer inputs.";
		result.status = error;
		return false;
	}
	std::string diagnostics;
	if (!validate_virtual_struct_schema(*schema, diagnostics)) {
		error = diagnostics;
		result.status = error;
		return false;
	}
	std::vector<NE_Pin> expected_inputs;
	NE_Pin pin = make_pin<rv::resource_tags::vertex_buffer>("vertex_buffer");
	apply_virtual_struct_schema(pin, *schema);
	expected_inputs.push_back(std::move(pin));
	if (!equivalent_pin_layout(node.generated_inputs, expected_inputs)) {
		error = "Bind Vertex Buffers pins are out of date. Reconnect the Virtual Struct schema.";
		result.status = error;
		return false;
	}
	result.executed_count = 1;
	result.status = "Ready for schema '" + schema->name + "'";
	return true;
}
bool bind_vertex_buffers_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (ctx.runtime == nullptr) { error = "Execution context has no runtime."; return false; }
	return ctx.runtime->execute_bind_vertex_buffers(node, error);
}

bool bind_index_buffer_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (ctx.runtime == nullptr) { error = "Execution context has no runtime."; return false; }
	return ctx.runtime->execute_bind_index_buffer(node, error);
}
bool bind_index_buffer_node::build(rv::graph_build_context&, NE_Node&, rv::graph_build_result& result, std::string&) { return mark_command_ready(result); }

bool draw_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (ctx.runtime == nullptr) { error = "Execution context has no runtime."; return false; }
	return ctx.runtime->execute_draw(ctx, node, error);
}
bool draw_node::build(rv::graph_build_context&, NE_Node&, rv::graph_build_result& result, std::string&) { return mark_command_ready(result); }

bool draw_indexed_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (ctx.runtime == nullptr) { error = "Execution context has no runtime."; return false; }
	return ctx.runtime->execute_draw_indexed(ctx, node, error);
}
bool draw_indexed_node::build(rv::graph_build_context&, NE_Node&, rv::graph_build_result& result, std::string&) { return mark_command_ready(result); }

bool present_texture_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Present";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	runtime_detail::framebuffer_attachment_resources* resources =
		read_input_resource<runtime_detail::framebuffer_attachment_resources>(*services, node, "texture", error);
	if (resources == nullptr) {
		result.status = error.empty() ? "Present Texture input is not ready." : error;
		return false;
	}
	if (!store_output_resource(*services, node, "texture", resources, error)) {
		result.status = error;
		return false;
	}

	result.executed_count = 1;
	result.status = "Ready";
	return true;
}

} // namespace rv::nodes
