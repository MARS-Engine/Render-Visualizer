#pragma once

#include <mars/graphics/backend/pipeline.hpp>
#include <mars/math/vector4.hpp>

#include <string>
#include <string_view>
#include <vector>

struct graph_virtual_struct_field {
	std::string name;
	std::string semantic;
	size_t type_hash = 0;
};

struct graph_virtual_struct_schema {
	int id = -1;
	std::string name;
	mars::vector4<float> color = {};
	std::vector<graph_virtual_struct_field> fields;
};

struct graph_texture_slot {
	int id = -1;
	std::string name;
	std::string path;
	std::string status;
};

enum class graph_shader_resource_kind {
	uniform_value,
	sampled_texture,
};

struct graph_shader_interface_slot {
	std::string label;
	size_t binding = 0;
	mars_pipeline_stage stage = MARS_PIPELINE_STAGE_FRAGMENT;
	graph_shader_resource_kind kind = graph_shader_resource_kind::uniform_value;
	size_t type_hash = 0;
};

struct graph_shader_interface {
	int id = -1;
	int source_node_id = -1;
	std::string name;
	bool valid = false;
	std::string diagnostics;
	std::vector<graph_shader_interface_slot> slots;
};

struct graph_variable_slot {
	int id = -1;
	std::string name;
	size_t type_hash = 0;
	bool is_container = false;
	bool has_virtual_struct = false;
	std::string virtual_struct_name;
	size_t virtual_struct_layout_fingerprint = 0;
	size_t template_base_type_hash = 0;
	size_t template_value_hash = 0;
	std::string template_display_name;
	std::string default_json;
	std::string status;
};

struct graph_function_signature_pin {
	int id = -1;
	std::string label;
	size_t type_hash = 0;
	bool is_container = false;
	bool has_virtual_struct = false;
	std::string virtual_struct_name;
	size_t virtual_struct_layout_fingerprint = 0;
};

struct graph_function_definition {
	int id = -1;
	std::string name;
	std::vector<graph_function_signature_pin> inputs;
	std::vector<graph_function_signature_pin> outputs;
};

constexpr size_t graph_runtime_fnv1a64(std::string_view _text) {
	size_t hash = 14695981039346656037ull;
	for (unsigned char c : _text) {
		hash ^= c;
		hash *= 1099511628211ull;
	}
	return hash;
}

inline size_t graph_virtual_struct_layout_fingerprint(const graph_virtual_struct_schema& _schema) {
	auto mix = [](size_t _seed, size_t _value) {
		return _seed ^ (_value + 0x9e3779b97f4a7c15ull + (_seed << 6) + (_seed >> 2));
	};

	size_t result = graph_runtime_fnv1a64("rv::virtual_struct_layout");
	for (const auto& field : _schema.fields) {
		result = mix(result, graph_runtime_fnv1a64(field.name));
		result = mix(result, graph_runtime_fnv1a64(field.semantic));
		result = mix(result, field.type_hash);
	}
	return result;
}
