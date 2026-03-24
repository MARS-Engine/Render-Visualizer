#include <render_visualizer/runtime/impl.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "../../mars/third_party/stb/stb_image.h"

namespace rv {
using namespace runtime_detail;

// Runtime resource helpers

bool graph_runtime::ensure_uniform_payload(uniform_buffer_resources& resources, const std::vector<std::byte>& payload, std::string& error) {
	if (!resources.buffer.engine) {
		resources.buffer = mars::graphics::buffer_create(device, {
			.buffer_type = MARS_BUFFER_TYPE_UNIFORM,
			.buffer_property = MARS_BUFFER_PROPERTY_HOST_VISIBLE,
			.allocated_size = 256u,
			.stride = 0u
		});
		if (!resources.buffer.engine) {
			error = "Failed to allocate the uniform buffer.";
			return false;
		}
	}

	void* mapped = mars::graphics::buffer_map(resources.buffer, device, payload.size(), 0u);
	if (mapped == nullptr) {
		error = "Failed to map the uniform buffer.";
		return false;
	}
	if (!payload.empty())
		std::memcpy(mapped, payload.data(), payload.size());
	mars::graphics::buffer_unmap(resources.buffer, device);
	resources.payload_size = payload.size();
	resources.valid = true;
	resources.error.clear();
	error.clear();
	return true;
}

bool graph_runtime::load_texture_slot_from_file(texture_slot_resources& resources, const std::string& source_path, std::string& status) {
	if (resources.valid && resources.source_path == source_path) {
		status = resources.pending_upload
			? "Queued upload for " + std::to_string(resources.extent.x) + "x" + std::to_string(resources.extent.y) + "."
			: "Ready " + std::to_string(resources.extent.x) + "x" + std::to_string(resources.extent.y) + ".";
		return true;
	}

	resources.destroy(device);

	std::error_code path_error;
	std::filesystem::path texture_path(source_path);
	if (texture_path.is_relative())
		texture_path = std::filesystem::absolute(texture_path, path_error);
	if (path_error) {
		status = "Failed to resolve texture path: " + path_error.message();
		return false;
	}
	if (!std::filesystem::exists(texture_path, path_error) || path_error) {
		status = "Texture file does not exist: " + texture_path.string();
		return false;
	}

	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pixels = stbi_load(texture_path.string().c_str(), &width, &height, &channels, 4);
	if (pixels == nullptr) {
		const char* reason = stbi_failure_reason();
		status = std::string("Failed to decode texture: ") + (reason == nullptr ? "unknown stb_image error" : reason);
		return false;
	}

	mars::texture_create_params params = {};
	params.size = { static_cast<size_t>(width), static_cast<size_t>(height) };
	params.format = MARS_FORMAT_RGBA8_UNORM;
	resources.texture = mars::graphics::texture_create(device, params);
	if (!resources.texture.engine) {
		stbi_image_free(pixels);
		status = "Failed to create GPU texture.";
		return false;
	}

	const mars::texture_upload_layout layout = mars::graphics::texture_get_upload_layout(resources.texture, device);
	void* mapped_ptr = mars::graphics::texture_map(resources.texture, device);
	if (mapped_ptr == nullptr) {
		stbi_image_free(pixels);
		resources.destroy(device);
		status = "Failed to map GPU texture upload memory.";
		return false;
	}

	char* mapped = static_cast<char*>(mapped_ptr) + layout.offset;
	const size_t src_row_size = static_cast<size_t>(width) * 4u;
	for (size_t row = 0; row < static_cast<size_t>(height); ++row) {
		std::memcpy(
			mapped + row * layout.row_pitch,
			pixels + row * src_row_size,
			std::min(layout.row_size, src_row_size)
		);
	}
	mars::graphics::texture_unmap(resources.texture, device);
	stbi_image_free(pixels);

	resources.extent = params.size;
	resources.pending_upload = true;
	resources.valid = true;
	resources.source_path = source_path;
	status = "Queued upload for " + std::to_string(params.size.x) + "x" + std::to_string(params.size.y) + ".";
	return true;
}

void render_state::reset_record_state(const mars::command_buffer& cmd, size_t current_frame) {
	active_record_cmd = &cmd;
	active_record_frame = current_frame;
	active_render_pass = nullptr;
}

void render_state::finish_record_state() {
	active_record_cmd = nullptr;
	active_record_frame = 0;
	active_render_pass = nullptr;
}

// Record-state entry points

void graph_runtime::reset_record_state(const mars::command_buffer& cmd, size_t current_frame) {
	render.reset_record_state(cmd, current_frame);
	for (auto& node : graph.nodes) {
		if (node.type != nodes::node_type_v<nodes::bind_resources_node_tag>)
			continue;
		if (material_binding_resources* resources = read_node_resource<material_binding_resources>(node); resources != nullptr)
			resources->next_execution_set = 0;
	}
}

void graph_runtime::finish_record_state() {
	render.finish_record_state();
}

// Render command execution

bool graph_runtime::execute_begin_render_pass(const NE_Node& node, std::string& error) {
	error.clear();
	if (render.active_record_cmd == nullptr) {
		error = "No active command buffer.";
		return false;
	}

	const render_pass_resources* pass_resources = read_input_resource<render_pass_resources>(node, "render_pass", error);
	if (pass_resources == nullptr)
		return false;
	const framebuffer_resources* framebuffer_resource = read_input_resource<framebuffer_resources>(node, "framebuffer", error);
	if (framebuffer_resource == nullptr)
		return false;
	const depth_buffer_resources* depth_resources = nullptr;
	if (pass_resources->params.depth_format != MARS_DEPTH_FORMAT_UNDEFINED) {
		depth_resources = read_input_resource<depth_buffer_resources>(node, "depth_buffer", error);
		if (depth_resources == nullptr)
			return false;
	}

	if (render.active_render_pass != nullptr)
		mars::graphics::render_pass_unbind(render.active_render_pass->render_pass, *render.active_record_cmd);
	mars::render_pass_bind_param params = {};
	params.image_index = 0;
	params.clear_color = pass_resources->clear_color;
	params.clear_depth = pass_resources->params.depth_clear_value;
	mars::graphics::render_pass_bind(pass_resources->render_pass, *render.active_record_cmd, framebuffer_resource->framebuffer, depth_resources != nullptr ? &depth_resources->depth : nullptr, params);
	render.active_render_pass = pass_resources;
	return true;
}

bool graph_runtime::execute_bind_pipeline(const NE_Node& node, std::string& error) {
	error.clear();
	if (render.active_record_cmd == nullptr) {
		error = "No active command buffer.";
		return false;
	}

	graphics_pipeline_resources* pipeline_resources = read_input_resource<graphics_pipeline_resources>(node, "pipeline", error);
	if (pipeline_resources == nullptr)
		return false;
	runtime_detail::framebuffer_resources* framebuffer_resource = read_input_resource<runtime_detail::framebuffer_resources>(node, "framebuffer", error);
	if (framebuffer_resource == nullptr)
		return false;

	mars::graphics::pipeline_bind(pipeline_resources->pipeline, *render.active_record_cmd, { .size = framebuffer_resource->extent });
	return true;
}

bool graph_runtime::execute_bind_resources(const NE_Node& node, std::string& error) {
	error.clear();
	if (render.active_record_cmd == nullptr) {
		error = "No active command buffer.";
		return false;
	}

	const graphics_pipeline_resources* pipeline_resources = read_input_resource<graphics_pipeline_resources>(node, "pipeline", error);
	if (pipeline_resources == nullptr)
		return false;

	material_binding_resources* bind_resources = read_node_resource<material_binding_resources>(node);
	if (bind_resources == nullptr || !bind_resources->valid) {
		error = "Bind Resources is not ready.";
		return false;
	}
	if (bind_resources->next_execution_set >= bind_resources->execution_sets.size()) {
		error = "Bind Resources exceeded its per-frame descriptor capacity.";
		return false;
	}
	const size_t execution_set_index = bind_resources->next_execution_set++;

	for (auto& slot : bind_resources->slots) {
		if (slot.kind != graph_shader_resource_kind::uniform_value)
			continue;

		const resolved_value resolved = resolve_current_input_source(node, slot.label, blackboard.current_item_index, blackboard.current_item_count);
		std::vector<std::byte> resolved_bytes;
		size_t resolved_element_count = 0;
		if (!copy_variable_payload(resolved, resolved_bytes, resolved_element_count)) {
			error = resolved.status.empty()
				? ("Bind Resources requires a CPU value for '" + slot.label + "'.")
				: ("Bind Resources input '" + slot.label + "' failed: " + resolved.status);
			return false;
		}

		nodes::uniform_data_state zero_state;
		zero_state.kind = slot.uniform_kind;
		zero_state.float_values = {0.0f, 0.0f, 0.0f, 0.0f};
		zero_state.uint_values = {0u, 0u, 0u, 0u};
		std::vector<std::byte> payload = nodes::make_uniform_bytes(zero_state);
		if (resolved_bytes.empty()) {
			error = "Bind Resources input '" + slot.label + "' has no payload.";
			return false;
		}
		std::memcpy(payload.data(), resolved_bytes.data(), std::min(payload.size(), resolved_bytes.size()));
		if (execution_set_index >= slot.execution_buffers.size()) {
			error = "Bind Resources uniform slot '" + slot.label + "' exceeded its per-frame buffer capacity.";
			return false;
		}
		uniform_buffer_resources& execution_buffer = slot.execution_buffers[execution_set_index];
		if (!ensure_uniform_payload(execution_buffer, payload, error))
			return false;
	}

	mars::graphics::descriptor_set_bind(bind_resources->execution_sets[execution_set_index], *render.active_record_cmd, pipeline_resources->pipeline, render.active_record_frame);
	return true;
}

bool graph_runtime::execute_bind_vertex_buffers(const NE_Node& node, std::string& error) {
	error.clear();
	if (render.active_record_cmd == nullptr) {
		error = "No active command buffer.";
		return false;
	}

	vertex_buffer_resources* buffer_resources = read_input_resource<vertex_buffer_resources>(node, "vertex_buffer", error);
	if (buffer_resources == nullptr)
		return false;

	mars::graphics::buffer_bind(buffer_resources->buffer, *render.active_record_cmd);
	return true;
}

bool graph_runtime::execute_bind_index_buffer(const NE_Node& node, std::string& error) {
	error.clear();
	if (render.active_record_cmd == nullptr) {
		error = "No active command buffer.";
		return false;
	}

	index_buffer_resources* buffer_resources = read_input_resource<index_buffer_resources>(node, "index_buffer", error);
	if (buffer_resources == nullptr)
		return false;

	mars::graphics::buffer_bind_index(buffer_resources->buffer, *render.active_record_cmd);
	return true;
}

bool graph_runtime::execute_draw(graph_execution_context& ctx, const NE_Node& node, std::string& error) {
	error.clear();
	if (render.active_record_cmd == nullptr) {
		error = "No active command buffer.";
		return false;
	}
	if (render.active_render_pass == nullptr) {
		error = "Draw requires an active render pass.";
		return false;
	}

	unsigned int vertex_count = 0;
	unsigned int instance_count = 0;
	if (!ctx.resolve_input(node, "vertex_count", vertex_count, error))
		return false;
	if (!ctx.resolve_input(node, "instance_count", instance_count, error))
		return false;
	if (vertex_count == 0) {
		error = "Draw requires a positive vertex count.";
		return false;
	}
	mars::graphics::command_buffer_draw(*render.active_record_cmd, {
		.vertex_count = static_cast<size_t>(vertex_count),
		.instance_count = static_cast<size_t>(instance_count),
		.first_vertex = 0,
		.first_instance = 0
	});
	return true;
}

bool graph_runtime::execute_draw_indexed(graph_execution_context& ctx, const NE_Node& node, std::string& error) {
	error.clear();
	if (render.active_record_cmd == nullptr) {
		error = "No active command buffer.";
		return false;
	}
	if (render.active_render_pass == nullptr) {
		error = "Draw Indexed requires an active render pass.";
		return false;
	}
	unsigned int index_count = 0;
	unsigned int instance_count = 0;
	if (!ctx.resolve_input(node, "index_count", index_count, error))
		return false;
	if (!ctx.resolve_input(node, "instance_count", instance_count, error))
		return false;
	if (index_count == 0) {
		error = "Draw Indexed requires a positive index count.";
		return false;
	}
	mars::graphics::command_buffer_draw_indexed(*render.active_record_cmd, {
		.index_count = static_cast<size_t>(index_count),
		.instance_count = static_cast<size_t>(instance_count),
		.first_index = 0,
		.vertex_offset = 0,
		.first_instance = 0
	});
	return true;
}

bool graph_runtime::execute_dynamic_uniform(const NE_Node& node, std::string& error) {
	error.clear();
	if (node.custom_state.storage == nullptr) {
		error = "Dynamic Uniform state is missing.";
		return false;
	}

	uniform_buffer_resources* resources = read_output_resource<uniform_buffer_resources>(node, "uniform", error);
	if (resources == nullptr)
		return false;

	const auto& state = node.custom_state.as<nodes::dynamic_uniform_state>();
	const resolved_value resolved = resolve_current_input_source(node, "value", blackboard.current_item_index, blackboard.current_item_count);
	if (resolved.source_kind != value_source_kind::inline_cpu) {
		error = resolved.status.empty() ? "Dynamic Uniform requires a simple CPU value input." : resolved.status;
		return false;
	}

	std::vector<std::byte> payload = nodes::make_dynamic_uniform_zero_bytes(state.kind);
	if (resolved.inline_bytes.empty()) {
		error = "Dynamic Uniform input has no payload.";
		return false;
	}
	std::memcpy(payload.data(), resolved.inline_bytes.data(), std::min(payload.size(), resolved.inline_bytes.size()));
	if (!ensure_uniform_payload(*resources, payload, error))
		return false;
	return true;
}

} // namespace rv
