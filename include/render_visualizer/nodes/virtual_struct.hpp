#pragma once

#include <render_visualizer/nodes/support.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace rv::nodes {

struct virtual_struct_type_descriptor {
	size_t type_hash = 0;
	std::string_view name = "unknown";
	size_t size = 0;
	size_t offset = 0;
	mars_format_type format = MARS_FORMAT_UNDEFINED;
};

template <typename T>
inline constexpr virtual_struct_type_descriptor make_virtual_struct_type_descriptor() {
	return {
		.type_hash = reflect<T>::type_hash,
		.name = reflect<T>::name,
		.size = reflect<T>::size,
		.offset = reflect<T>::offset,
		.format = reflect<T>::format,
	};
}

const std::vector<virtual_struct_type_descriptor>& virtual_struct_type_descriptors();
const virtual_struct_type_descriptor& ensure_virtual_struct_type_descriptor(const virtual_struct_type_descriptor& descriptor);
const virtual_struct_type_descriptor* find_virtual_struct_type_descriptor(size_t type_hash);

template <typename T>
inline const virtual_struct_type_descriptor& ensure_virtual_struct_type_descriptor() {
	return ensure_virtual_struct_type_descriptor(make_virtual_struct_type_descriptor<T>());
}

struct semantic_info {
	std::string name;
	size_t index = 0;
};

const char* virtual_struct_field_type_name(const virtual_struct_field& field);
size_t virtual_struct_field_size(const virtual_struct_field& field);
mars_format_type virtual_struct_field_format(const virtual_struct_field& field);
size_t virtual_struct_layout_fingerprint(const virtual_struct_schema_state& state);
std::string uppercase_identifier(std::string_view text);
std::string virtual_struct_semantic(const virtual_struct_field& field);
semantic_info parse_semantic(std::string_view label);
void apply_virtual_struct_schema(NE_Pin& pin, const virtual_struct_schema_state& state);

template <typename T>
inline virtual_struct_schema_state reflected_virtual_struct_schema(std::string schema_name = std::define_static_string(mars::meta::display_name<T>())) {
	virtual_struct_schema_state state;
	state.name = std::move(schema_name);
	state.fields.clear();

	constexpr auto ctx = std::meta::access_context::current();
	template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^T, ctx))) {
		constexpr auto mem_type = std::meta::remove_cvref(std::meta::type_of(mem));
		using field_t = [:mem_type:];
		const auto& descriptor = ensure_virtual_struct_type_descriptor<field_t>();

		virtual_struct_field field;
		field.name = std::string(std::meta::identifier_of(mem));
		field.semantic = uppercase_identifier(field.name);
		field.type_hash = descriptor.type_hash;
		state.fields.push_back(std::move(field));
	}

	return state;
}

bool validate_virtual_struct_schema(const virtual_struct_schema_state& state, std::string& diagnostics);
virtual_struct_schema_state make_default_virtual_struct(std::string name = "Struct");
int ensure_default_virtual_struct(NodeGraph& graph, std::string name = "Struct");

} // namespace rv::nodes
