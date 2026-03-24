#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/vertex_buffer_node.hpp>

#include <render_visualizer/runtime/impl.hpp>
#include <render_visualizer/ui/shared_editors.hpp>

namespace rv::nodes {

std::vector<NE_Pin> make_vertex_buffer_inputs(const virtual_struct_schema_state& state) {
	std::vector<NE_Pin> pins;
	pins.reserve(state.fields.size());
	for (const auto& field : state.fields) {
		NE_Pin pin = make_pin<float>(field.name, true);
		pin.type_hash = field.type_hash;
		pins.push_back(std::move(pin));
	}
	return pins;
}

std::vector<NE_Pin> make_vertex_buffer_outputs(const virtual_struct_schema_state& state) {
	NE_Pin pin = make_pin<rv::resource_tags::vertex_buffer>("vertex_buffer");
	apply_virtual_struct_schema(pin, state);
	return { std::move(pin) };
}

void sync_vertex_buffer_node(NodeGraph& graph, NE_Node& node) {
	std::vector<NE_Pin> generated_inputs;
	std::vector<NE_Pin> generated_outputs;
	if (node.custom_state.storage == nullptr)
		return;
	const auto& state_ref = node.custom_state.as<vertex_buffer_state>();
	if (const virtual_struct_schema_state* state = graph.find_virtual_struct(state_ref.schema_id); state != nullptr) {
		generated_inputs = make_vertex_buffer_inputs(*state);
		generated_outputs = make_vertex_buffer_outputs(*state);
	}
	if (equivalent_pin_layout(node.generated_inputs, generated_inputs) &&
		equivalent_pin_layout(node.generated_outputs, generated_outputs))
		return;
	graph.replace_generated_pins(node.id, std::move(generated_inputs), std::move(generated_outputs));
}

void refresh_dynamic_vertex_buffer_nodes(NodeGraph& graph) {
	for (auto& node : graph.nodes) {
		if (node.type != node_type_v<vertex_buffer_node_tag>)
			continue;
		sync_vertex_buffer_node(graph, node);
	}
}

void vertex_buffer_node::edit(NodeGraph& graph, NE_Node& node, vertex_buffer_state& state) {
	sync_vertex_buffer_node(graph, node);
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::Separator();
	if (render_virtual_struct_selector(graph, state.schema_id, "Struct", "VertexStruct"))
		sync_vertex_buffer_node(graph, node);
	const virtual_struct_schema_state* schema = graph.find_virtual_struct(state.schema_id);
	if (schema == nullptr) {
		ImGui::TextWrapped("Select an existing virtual struct or create a new one to generate the CPU attribute inputs and typed vertex buffer output.");
		return;
	}
	std::string diagnostics;
	if (!validate_virtual_struct_schema(*schema, diagnostics)) {
		ImGui::TextWrapped("%s", diagnostics.c_str());
		return;
	}
	ImGui::Text("Schema: %s", schema->name.c_str());
	ImGui::TextDisabled("%s", diagnostics.c_str());
	ImGui::Spacing();
	for (const auto& field : schema->fields) {
		ImGui::BulletText("%s <- %s (%s)", field.name.c_str(), virtual_struct_semantic(field).c_str(), virtual_struct_field_type_name(field));
	}
}

void vertex_buffer_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_vertex_buffer_node(graph, node);
}

bool vertex_buffer_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Upload";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	if (node.custom_state.storage == nullptr) {
		error = "Vertex Buffer state is missing.";
		result.status = error;
		return false;
	}

	const auto& node_state = node.custom_state.as<vertex_buffer_state>();
	const virtual_struct_schema_state* schema = services->graph->find_virtual_struct(node_state.schema_id);
	runtime_detail::vertex_buffer_resources resources;

	if (schema == nullptr) {
		resources.error = "Vertex Buffer is missing its Virtual Struct schema.";
		error = resources.error;
		result.status = error;
		return false;
	}

	std::string schema_diagnostics;
	if (!validate_virtual_struct_schema(*schema, schema_diagnostics)) {
		resources.error = schema_diagnostics;
		error = resources.error;
		result.status = error;
		return false;
	}

	if (!equivalent_pin_layout(node.generated_inputs, make_vertex_buffer_inputs(*schema)) ||
		!equivalent_pin_layout(node.generated_outputs, make_vertex_buffer_outputs(*schema))) {
		resources.error = "Vertex Buffer pins are out of date. Reconnect the Virtual Struct schema.";
		error = resources.error;
		result.status = error;
		return false;
	}

	std::vector<std::vector<std::byte>> payloads(node.generated_inputs.size());
	std::vector<size_t> counts(node.generated_inputs.size(), 0);
	for (size_t i = 0; i < node.generated_inputs.size(); ++i) {
		const NE_Pin& input_pin = node.generated_inputs[i];
		runtime_detail::resolved_value source = services->runtime->resolve_input_source(node, input_pin.label);
		if (!services->runtime->copy_variable_payload(source, payloads[i], counts[i])) {
			resources.error = "Unsupported CPU source mapping for " + input_pin.label + ".";
			error = resources.error + (source.status.empty() ? "" : (" " + source.status));
			result.status = error;
			return false;
		}
	}

	size_t vertex_count = std::numeric_limits<size_t>::max();
	for (size_t count : counts)
		vertex_count = std::min(vertex_count, count);

	if (vertex_count == std::numeric_limits<size_t>::max() || vertex_count == 0) {
		resources.error = "Vertex buffer has no data.";
		error = resources.error;
		result.status = error;
		return false;
	}

	resources.attributes.clear();
	resources.stride = 0;
	for (size_t i = 0; i < node.generated_inputs.size(); ++i) {
		runtime_detail::vertex_attribute attribute;
		attribute.semantic = virtual_struct_semantic(schema->fields[i]);
		attribute.format = virtual_struct_field_format(schema->fields[i]);
		attribute.offset = resources.stride;
		attribute.size = virtual_struct_field_size(schema->fields[i]);
		resources.stride += attribute.size;
		resources.attributes.push_back(std::move(attribute));
	}

	std::vector<std::byte> interleaved;
	interleaved.reserve(resources.stride * vertex_count);
	for (size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
		for (size_t attribute_index = 0; attribute_index < node.generated_inputs.size(); ++attribute_index) {
			const size_t value_size = virtual_struct_field_size(schema->fields[attribute_index]);
			const std::byte* begin = payloads[attribute_index].data() + vertex_index * value_size;
			interleaved.insert(interleaved.end(), begin, begin + value_size);
		}
	}

	resources.buffer = mars::graphics::buffer_create(*services->device, {
		.buffer_type = MARS_BUFFER_TYPE_VERTEX,
		.buffer_property = MARS_BUFFER_PROPERTY_HOST_VISIBLE,
		.allocated_size = interleaved.size(),
		.stride = resources.stride
	});
	if (!resources.buffer.engine) {
		resources.error = "Failed to allocate the vertex buffer.";
		error = resources.error;
		result.status = error;
		return false;
	}

	void* mapped = mars::graphics::buffer_map(resources.buffer, *services->device, interleaved.size(), 0);
	if (mapped == nullptr) {
		resources.error = "Failed to map the vertex buffer.";
		resources.destroy(*services->device);
		error = resources.error;
		result.status = error;
		return false;
	}

	if (!interleaved.empty())
		std::memcpy(mapped, interleaved.data(), interleaved.size());
	mars::graphics::buffer_unmap(resources.buffer, *services->device);

	resources.vertex_count = vertex_count;
	resources.schema_name = schema->name;
	resources.schema_layout_fingerprint = virtual_struct_layout_fingerprint(*schema);
	resources.valid = true;
	resources.error.clear();
	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	if (!store_output_resource(*services, node, "vertex_buffer", &stored, error)) {
		result.status = error;
		return false;
	}
	result.executed_count = vertex_count;
	result.status = "Uploaded " + std::to_string(vertex_count) + " vertices for schema '" + schema->name + "'";
	return true;
}

void vertex_buffer_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::vertex_buffer_resources>(services, node);
}

} // namespace rv::nodes
