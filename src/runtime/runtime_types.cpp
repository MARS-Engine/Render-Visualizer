#include <render_visualizer/runtime/types.hpp>

namespace rv::runtime_detail {

void vertex_buffer_resources::destroy(const mars::device& device) {
	if (buffer.engine)
		mars::graphics::buffer_destroy(buffer, device);
	buffer = {};
	attributes.clear();
	stride = 0;
	vertex_count = 0;
	schema_name.clear();
	schema_layout_fingerprint = 0;
	valid = false;
	error.clear();
}

void index_buffer_resources::destroy(const mars::device& device) {
	if (buffer.engine)
		mars::graphics::buffer_destroy(buffer, device);
	buffer = {};
	index_count = 0;
	max_index = 0;
	valid = false;
	error.clear();
}

void shader_module_resources::destroy(const mars::device& device) {
	if (shader.engine)
		mars::graphics::shader_destroy(shader, device);
	shader = {};
	valid = false;
	error.clear();
}

void render_pass_resources::destroy(const mars::device& device) {
	if (render_pass.engine)
		mars::graphics::render_pass_destroy(render_pass, device);
	render_pass = {};
	params = {};
	clear_color = {};
	valid = false;
	error.clear();
}

void framebuffer_resources::destroy(const mars::device& device) {
	if (framebuffer.engine)
		mars::graphics::framebuffer_destroy(framebuffer, device);
	for (auto& target : targets) {
		if (target.engine)
			mars::graphics::texture_destroy(target, device);
	}
	framebuffer = {};
	targets.clear();
	attachments.clear();
	extent = {};
	valid = false;
	error.clear();
}

void depth_buffer_resources::destroy(const mars::device& device) {
	if (depth.engine)
		mars::graphics::depth_buffer_destroy(depth, device);
	depth = {};
	extent = {};
	valid = false;
	error.clear();
}

void graphics_pipeline_resources::destroy(const mars::device& device) {
	if (pipeline.engine)
		mars::graphics::pipeline_destroy(pipeline, device);
	pipeline = {};
	vertex_schema_name.clear();
	vertex_schema_layout_fingerprint = 0;
	requires_vertex_buffer = true;
	valid = false;
	error.clear();
}

void uniform_buffer_resources::destroy(const mars::device& device) {
	if (buffer.engine)
		mars::graphics::buffer_destroy(buffer, device);
	buffer = {};
	payload_size = 0;
	valid = false;
	error.clear();
}

void bind_resource_slot::destroy(const mars::device& device) {
	for (auto& execution_buffer : execution_buffers)
		execution_buffer.destroy(device);
	execution_buffers.clear();
	buffer.destroy(device);
	texture_slot_id = -1;
}

void material_binding_resources::destroy(const mars::device& device) {
	for (auto& slot : slots)
		slot.destroy(device);
	slots.clear();
	execution_sets.clear();
	next_execution_set = 0;
	if (descriptor.engine)
		mars::graphics::descriptor_destroy(descriptor, device);
	descriptor = {};
	set = {};
	valid = false;
	error.clear();
}

void texture_slot_resources::destroy(const mars::device& device) {
	if (texture.engine)
		mars::graphics::texture_destroy(texture, device);
	texture = {};
	extent = {};
	pending_upload = false;
	valid = false;
	source_path.clear();
	error.clear();
}

void present_resources::destroy(const mars::device& device) {
	if (pipeline.engine)
		mars::graphics::pipeline_destroy(pipeline, device);
	if (shader.engine)
		mars::graphics::shader_destroy(shader, device);
	shader = {};
	pipeline = {};
	render_pass_key = nullptr;
	valid = false;
	error.clear();
}

} // namespace rv::runtime_detail
