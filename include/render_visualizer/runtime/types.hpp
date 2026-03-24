#pragma once

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/all.hpp>

#include <mars/graphics/functional/buffer.hpp>
#include <mars/graphics/functional/depth_buffer.hpp>
#include <mars/graphics/functional/descriptor.hpp>
#include <mars/graphics/functional/framebuffer.hpp>
#include <mars/graphics/functional/pipeline.hpp>
#include <mars/graphics/functional/render_pass.hpp>
#include <mars/graphics/functional/shader.hpp>
#include <mars/graphics/functional/texture.hpp>

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ImVec2;

namespace rv::runtime_detail {

template <typename T>
struct gpu_resource {
	T value{};
	bool valid = false;
	std::string error;
};

std::string state_name(bool running, bool dirty, bool has_error);

struct step_info {
	int node_id = -1;
	std::string label;
	std::string kind;
	std::string status;
	size_t executed_count = 0;
	bool valid = true;
};

struct vertex_attribute {
	std::string semantic;
	mars_format_type format = MARS_FORMAT_UNDEFINED;
	size_t offset = 0;
	size_t size = 0;
};

struct vertex_buffer_resources : gpu_resource<mars::buffer> {
	mars::buffer& buffer;
	vertex_buffer_resources() : buffer(this->value) {}
	vertex_buffer_resources(const vertex_buffer_resources& _other)
		: gpu_resource<mars::buffer>(_other)
		, buffer(this->value)
		, attributes(_other.attributes)
		, stride(_other.stride)
		, vertex_count(_other.vertex_count)
		, schema_name(_other.schema_name)
		, schema_layout_fingerprint(_other.schema_layout_fingerprint) {}
	vertex_buffer_resources(vertex_buffer_resources&& _other) noexcept
		: gpu_resource<mars::buffer>(std::move(_other))
		, buffer(this->value)
		, attributes(std::move(_other.attributes))
		, stride(_other.stride)
		, vertex_count(_other.vertex_count)
		, schema_name(std::move(_other.schema_name))
		, schema_layout_fingerprint(_other.schema_layout_fingerprint) {}
	vertex_buffer_resources& operator=(const vertex_buffer_resources& _other) {
		if (this != &_other) {
			gpu_resource<mars::buffer>::operator=(_other);
			attributes = _other.attributes;
			stride = _other.stride;
			vertex_count = _other.vertex_count;
			schema_name = _other.schema_name;
			schema_layout_fingerprint = _other.schema_layout_fingerprint;
		}
		return *this;
	}
	vertex_buffer_resources& operator=(vertex_buffer_resources&& _other) noexcept {
		if (this != &_other) {
			gpu_resource<mars::buffer>::operator=(std::move(_other));
			attributes = std::move(_other.attributes);
			stride = _other.stride;
			vertex_count = _other.vertex_count;
			schema_name = std::move(_other.schema_name);
			schema_layout_fingerprint = _other.schema_layout_fingerprint;
		}
		return *this;
	}
	std::vector<vertex_attribute> attributes;
	size_t stride = 0;
	size_t vertex_count = 0;
	std::string schema_name;
	size_t schema_layout_fingerprint = 0;

	void destroy(const mars::device& device);
};

struct index_buffer_resources : gpu_resource<mars::buffer> {
	mars::buffer& buffer;
	index_buffer_resources() : buffer(this->value) {}
	index_buffer_resources(const index_buffer_resources& _other)
		: gpu_resource<mars::buffer>(_other)
		, buffer(this->value)
		, index_count(_other.index_count)
		, max_index(_other.max_index) {}
	index_buffer_resources(index_buffer_resources&& _other) noexcept
		: gpu_resource<mars::buffer>(std::move(_other))
		, buffer(this->value)
		, index_count(_other.index_count)
		, max_index(_other.max_index) {}
	index_buffer_resources& operator=(const index_buffer_resources& _other) {
		if (this != &_other) {
			gpu_resource<mars::buffer>::operator=(_other);
			index_count = _other.index_count;
			max_index = _other.max_index;
		}
		return *this;
	}
	index_buffer_resources& operator=(index_buffer_resources&& _other) noexcept {
		if (this != &_other) {
			gpu_resource<mars::buffer>::operator=(std::move(_other));
			index_count = _other.index_count;
			max_index = _other.max_index;
		}
		return *this;
	}
	size_t index_count = 0;
	unsigned int max_index = 0;

	void destroy(const mars::device& device);
};

struct shader_module_resources : gpu_resource<mars::shader> {
	mars::shader& shader;
	shader_module_resources() : shader(this->value) {}
	shader_module_resources(const shader_module_resources& _other)
		: gpu_resource<mars::shader>(_other)
		, shader(this->value) {}
	shader_module_resources(shader_module_resources&& _other) noexcept
		: gpu_resource<mars::shader>(std::move(_other))
		, shader(this->value) {}
	shader_module_resources& operator=(const shader_module_resources& _other) {
		if (this != &_other)
			gpu_resource<mars::shader>::operator=(_other);
		return *this;
	}
	shader_module_resources& operator=(shader_module_resources&& _other) noexcept {
		if (this != &_other)
			gpu_resource<mars::shader>::operator=(std::move(_other));
		return *this;
	}

	void destroy(const mars::device& device);
};

struct render_pass_resources : gpu_resource<mars::render_pass> {
	mars::render_pass& render_pass;
	render_pass_resources() : render_pass(this->value) {}
	render_pass_resources(const render_pass_resources& _other)
		: gpu_resource<mars::render_pass>(_other)
		, render_pass(this->value)
		, params(_other.params)
		, clear_color(_other.clear_color) {}
	render_pass_resources(render_pass_resources&& _other) noexcept
		: gpu_resource<mars::render_pass>(std::move(_other))
		, render_pass(this->value)
		, params(std::move(_other.params))
		, clear_color(_other.clear_color) {}
	render_pass_resources& operator=(const render_pass_resources& _other) {
		if (this != &_other) {
			gpu_resource<mars::render_pass>::operator=(_other);
			params = _other.params;
			clear_color = _other.clear_color;
		}
		return *this;
	}
	render_pass_resources& operator=(render_pass_resources&& _other) noexcept {
		if (this != &_other) {
			gpu_resource<mars::render_pass>::operator=(std::move(_other));
			params = std::move(_other.params);
			clear_color = _other.clear_color;
		}
		return *this;
	}
	mars::render_pass_create_params params = {};
	mars::vector4<float> clear_color = {};

	void destroy(const mars::device& device);
};

struct framebuffer_attachment_resources;

struct framebuffer_resources {
	std::vector<mars::texture> targets;
	mars::framebuffer framebuffer{};
	std::vector<framebuffer_attachment_resources> attachments;
	mars::vector2<size_t> extent = {};
	bool valid = false;
	std::string error;

	void destroy(const mars::device& device);
};

struct framebuffer_attachment_resources {
	framebuffer_resources* owner = nullptr;
	size_t attachment_index = 0;
	bool valid = false;
	std::string error;

	void destroy(const mars::device&) {}
};

struct depth_buffer_resources : gpu_resource<mars::depth_buffer> {
	mars::depth_buffer& depth;
	depth_buffer_resources() : depth(this->value) {}
	depth_buffer_resources(const depth_buffer_resources& _other)
		: gpu_resource<mars::depth_buffer>(_other)
		, depth(this->value)
		, extent(_other.extent) {}
	depth_buffer_resources(depth_buffer_resources&& _other) noexcept
		: gpu_resource<mars::depth_buffer>(std::move(_other))
		, depth(this->value)
		, extent(_other.extent) {}
	depth_buffer_resources& operator=(const depth_buffer_resources& _other) {
		if (this != &_other) {
			gpu_resource<mars::depth_buffer>::operator=(_other);
			extent = _other.extent;
		}
		return *this;
	}
	depth_buffer_resources& operator=(depth_buffer_resources&& _other) noexcept {
		if (this != &_other) {
			gpu_resource<mars::depth_buffer>::operator=(std::move(_other));
			extent = _other.extent;
		}
		return *this;
	}
	mars::vector2<size_t> extent = {};

	void destroy(const mars::device& device);
};

struct graphics_pipeline_resources : gpu_resource<mars::pipeline> {
	mars::pipeline& pipeline;
	graphics_pipeline_resources() : pipeline(this->value) {}
	graphics_pipeline_resources(const graphics_pipeline_resources& _other)
		: gpu_resource<mars::pipeline>(_other)
		, pipeline(this->value)
		, vertex_schema_name(_other.vertex_schema_name)
		, vertex_schema_layout_fingerprint(_other.vertex_schema_layout_fingerprint)
		, requires_vertex_buffer(_other.requires_vertex_buffer) {}
	graphics_pipeline_resources(graphics_pipeline_resources&& _other) noexcept
		: gpu_resource<mars::pipeline>(std::move(_other))
		, pipeline(this->value)
		, vertex_schema_name(std::move(_other.vertex_schema_name))
		, vertex_schema_layout_fingerprint(_other.vertex_schema_layout_fingerprint)
		, requires_vertex_buffer(_other.requires_vertex_buffer) {}
	graphics_pipeline_resources& operator=(const graphics_pipeline_resources& _other) {
		if (this != &_other) {
			gpu_resource<mars::pipeline>::operator=(_other);
			vertex_schema_name = _other.vertex_schema_name;
			vertex_schema_layout_fingerprint = _other.vertex_schema_layout_fingerprint;
			requires_vertex_buffer = _other.requires_vertex_buffer;
		}
		return *this;
	}
	graphics_pipeline_resources& operator=(graphics_pipeline_resources&& _other) noexcept {
		if (this != &_other) {
			gpu_resource<mars::pipeline>::operator=(std::move(_other));
			vertex_schema_name = std::move(_other.vertex_schema_name);
			vertex_schema_layout_fingerprint = _other.vertex_schema_layout_fingerprint;
			requires_vertex_buffer = _other.requires_vertex_buffer;
		}
		return *this;
	}
	std::string vertex_schema_name;
	size_t vertex_schema_layout_fingerprint = 0;
	bool requires_vertex_buffer = true;

	void destroy(const mars::device& device);
};

struct uniform_buffer_resources : gpu_resource<mars::buffer> {
	mars::buffer& buffer;
	uniform_buffer_resources() : buffer(this->value) {}
	uniform_buffer_resources(const uniform_buffer_resources& _other)
		: gpu_resource<mars::buffer>(_other)
		, buffer(this->value)
		, payload_size(_other.payload_size) {}
	uniform_buffer_resources(uniform_buffer_resources&& _other) noexcept
		: gpu_resource<mars::buffer>(std::move(_other))
		, buffer(this->value)
		, payload_size(_other.payload_size) {}
	uniform_buffer_resources& operator=(const uniform_buffer_resources& _other) {
		if (this != &_other) {
			gpu_resource<mars::buffer>::operator=(_other);
			payload_size = _other.payload_size;
		}
		return *this;
	}
	uniform_buffer_resources& operator=(uniform_buffer_resources&& _other) noexcept {
		if (this != &_other) {
			gpu_resource<mars::buffer>::operator=(std::move(_other));
			payload_size = _other.payload_size;
		}
		return *this;
	}
	size_t payload_size = 0;

	void destroy(const mars::device& device);
};

struct bind_resource_slot {
	std::string label;
	graph_shader_resource_kind kind = graph_shader_resource_kind::uniform_value;
	nodes::uniform_value_kind uniform_kind = nodes::uniform_value_kind::float4;
	size_t binding = 0;
	int texture_slot_id = -1;
	uniform_buffer_resources buffer;
	std::vector<uniform_buffer_resources> execution_buffers;

	void destroy(const mars::device& device);
};

struct material_binding_resources {
	mars::descriptor descriptor{};
	mars::descriptor_set set{};
	std::vector<bind_resource_slot> slots;
	std::vector<mars::descriptor_set> execution_sets;
	size_t next_execution_set = 0;
	bool valid = false;
	std::string error;

	void destroy(const mars::device& device);
};

struct texture_slot_resources : gpu_resource<mars::texture> {
	mars::texture& texture;
	texture_slot_resources() : texture(this->value) {}
	texture_slot_resources(const texture_slot_resources& _other)
		: gpu_resource<mars::texture>(_other)
		, texture(this->value)
		, extent(_other.extent)
		, pending_upload(_other.pending_upload)
		, source_path(_other.source_path) {}
	texture_slot_resources(texture_slot_resources&& _other) noexcept
		: gpu_resource<mars::texture>(std::move(_other))
		, texture(this->value)
		, extent(_other.extent)
		, pending_upload(_other.pending_upload)
		, source_path(std::move(_other.source_path)) {}
	texture_slot_resources& operator=(const texture_slot_resources& _other) {
		if (this != &_other) {
			gpu_resource<mars::texture>::operator=(_other);
			extent = _other.extent;
			pending_upload = _other.pending_upload;
			source_path = _other.source_path;
		}
		return *this;
	}
	texture_slot_resources& operator=(texture_slot_resources&& _other) noexcept {
		if (this != &_other) {
			gpu_resource<mars::texture>::operator=(std::move(_other));
			extent = _other.extent;
			pending_upload = _other.pending_upload;
			source_path = std::move(_other.source_path);
		}
		return *this;
	}
	mars::vector2<size_t> extent = {};
	bool pending_upload = false;
	std::string source_path;

	void destroy(const mars::device& device);
};

struct present_resources : gpu_resource<mars::pipeline> {
	mars::pipeline& pipeline;
	present_resources() : pipeline(this->value) {}
	present_resources(const present_resources& _other)
		: gpu_resource<mars::pipeline>(_other)
		, pipeline(this->value)
		, shader(_other.shader)
		, render_pass_key(_other.render_pass_key) {}
	present_resources(present_resources&& _other) noexcept
		: gpu_resource<mars::pipeline>(std::move(_other))
		, pipeline(this->value)
		, shader(std::move(_other.shader))
		, render_pass_key(_other.render_pass_key) {}
	present_resources& operator=(const present_resources& _other) {
		if (this != &_other) {
			gpu_resource<mars::pipeline>::operator=(_other);
			shader = _other.shader;
			render_pass_key = _other.render_pass_key;
		}
		return *this;
	}
	present_resources& operator=(present_resources&& _other) noexcept {
		if (this != &_other) {
			gpu_resource<mars::pipeline>::operator=(std::move(_other));
			shader = std::move(_other.shader);
			render_pass_key = _other.render_pass_key;
		}
		return *this;
	}
	mars::shader shader{};
	const void* render_pass_key = nullptr;

	void destroy(const mars::device& device);
};
} // namespace rv::runtime_detail
