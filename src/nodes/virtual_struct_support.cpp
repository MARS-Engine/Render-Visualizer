#include <render_visualizer/nodes/virtual_struct.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ranges>

namespace rv::nodes {
namespace {

using local_virtual_struct_field_types = std::tuple<
	float,
	mars::vector2<float>,
	mars::vector3<float>,
	mars::vector4<float>,
	unsigned int,
	mars::vector2<unsigned int>,
	mars::vector3<unsigned int>,
	mars::vector4<unsigned int>
>;

std::vector<virtual_struct_type_descriptor> make_builtin_virtual_struct_type_descriptors() {
	std::vector<virtual_struct_type_descriptor> descriptors;
	descriptors.reserve(std::tuple_size_v<local_virtual_struct_field_types>);
	[&]<size_t... Indices>(std::index_sequence<Indices...>) {
		(descriptors.push_back(make_virtual_struct_type_descriptor<std::tuple_element_t<Indices, local_virtual_struct_field_types>>()), ...);
	}(std::make_index_sequence<std::tuple_size_v<local_virtual_struct_field_types>> {});
	return descriptors;
}

std::vector<virtual_struct_type_descriptor>& mutable_virtual_struct_type_descriptors() {
	static std::vector<virtual_struct_type_descriptor> descriptors = make_builtin_virtual_struct_type_descriptors();
	return descriptors;
}

} // namespace

const std::vector<virtual_struct_type_descriptor>& virtual_struct_type_descriptors() {
	return mutable_virtual_struct_type_descriptors();
}

const virtual_struct_type_descriptor& ensure_virtual_struct_type_descriptor(const virtual_struct_type_descriptor& descriptor) {
	auto& descriptors = mutable_virtual_struct_type_descriptors();
	if (const auto it = std::ranges::find(descriptors, descriptor.type_hash, &virtual_struct_type_descriptor::type_hash); it != descriptors.end())
		return *it;
	descriptors.push_back(descriptor);
	return descriptors.back();
}

const virtual_struct_type_descriptor* find_virtual_struct_type_descriptor(size_t type_hash) {
	auto& descriptors = mutable_virtual_struct_type_descriptors();
	const auto it = std::ranges::find(descriptors, type_hash, &virtual_struct_type_descriptor::type_hash);
	return it == descriptors.end() ? nullptr : &*it;
}

const char* virtual_struct_field_type_name(const virtual_struct_field& field) {
	if (const auto* descriptor = find_virtual_struct_type_descriptor(field.type_hash); descriptor != nullptr)
		return descriptor->name.data();
	return "unknown";
}

size_t virtual_struct_field_size(const virtual_struct_field& field) {
	if (const auto* descriptor = find_virtual_struct_type_descriptor(field.type_hash); descriptor != nullptr)
		return descriptor->size;
	return 0;
}

mars_format_type virtual_struct_field_format(const virtual_struct_field& field) {
	if (const auto* descriptor = find_virtual_struct_type_descriptor(field.type_hash); descriptor != nullptr)
		return descriptor->format;
	return MARS_FORMAT_UNDEFINED;
}

size_t virtual_struct_layout_fingerprint(const virtual_struct_schema_state& state) {
	return graph_virtual_struct_layout_fingerprint(state);
}

std::string uppercase_identifier(std::string_view text) {
	std::string result(text);
	for (char& c : result)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return result;
}

std::string virtual_struct_semantic(const virtual_struct_field& field) {
	return field.semantic.empty() ? uppercase_identifier(field.name) : field.semantic;
}

semantic_info parse_semantic(std::string_view label) {
	semantic_info result;
	size_t split = label.size();
	while (split > 0 && std::isdigit(static_cast<unsigned char>(label[split - 1])) != 0)
		--split;
	result.name = std::string(label.substr(0, split));
	if (split < label.size())
		result.index = static_cast<size_t>(std::strtoul(std::string(label.substr(split)).c_str(), nullptr, 10));
	return result;
}

void apply_virtual_struct_schema(NE_Pin& pin, const virtual_struct_schema_state& state) {
	pin.has_virtual_struct = true;
	pin.virtual_struct_name = state.name;
	pin.virtual_struct_layout_fingerprint = virtual_struct_layout_fingerprint(state);
}

} // namespace rv::nodes
