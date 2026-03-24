#pragma once

#include <render_visualizer/node_graph.hpp>
#include <render_visualizer/node_registration.hpp>

#include <mars/graphics/backend/format.hpp>
#include <mars/graphics/backend/pipeline.hpp>
#include <mars/hash/meta.hpp>
#include <mars/math/matrix4.hpp>
#include <mars/math/vector2.hpp>
#include <mars/math/vector3.hpp>
#include <mars/math/vector4.hpp>
#include <mars/parser/json/json.hpp>
#include <mars/parser/parser.hpp>

#include <algorithm>
#include <any>
#include <array>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <tuple>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <ranges>

namespace mars::json {

template <>
struct json_type_parser<int> : public json_type_parser_base<int> {
	inline static std::string_view::iterator parse(const std::string_view& _json, int& _value) {
		std::string_view::iterator start = parse::first_space<false>(_json.begin(), _json.end());
		if (start == _json.end())
			return _json.begin();

		std::string_view::iterator current = start;
		if (*current == '-')
			++current;
		if (current == _json.end() || !std::isdigit(static_cast<unsigned char>(*current)))
			return _json.begin();
		while (current != _json.end() && std::isdigit(static_cast<unsigned char>(*current)))
			++current;

		const std::string_view::iterator delimiter = parse::first_space<false>(current, _json.end());
		if (delimiter != _json.end() && *delimiter != ',' && *delimiter != '}' && *delimiter != ']')
			return _json.begin();

		const std::string_view token(start, current);
		const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), _value);
		if (ec != std::errc {} || ptr != token.data() + token.size())
			return _json.begin();
		return current;
	}

	inline static void stringify(int& _value, std::string& _out) {
		_out += std::to_string(_value);
	}

	static constexpr bool number_support = true;
};

} // namespace mars::json

namespace rv::nodes {

template <typename Tag>
inline constexpr size_t node_type_v = mars::hash::type_fingerprint_v<Tag>;

using virtual_struct_field = graph_virtual_struct_field;
using virtual_struct_schema_state = graph_virtual_struct_schema;
using texture_slot_state = graph_texture_slot;
using variable_slot_state = graph_variable_slot;
using wildcard_type_info = NE_WildcardTypeInfo;
using wildcard_value = NE_WildcardValue;
using wildcard_value_view = NE_WildcardValueView;

} // namespace rv::nodes

namespace rv::detail {

template <typename... Tuples>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<Tuples>()...));

template <typename Tuple, typename Fn>
inline bool dispatch_type_hash(size_t type_hash, Fn&& fn) {
	bool handled = false;
	[&]<size_t... Indices>(std::index_sequence<Indices...>) {
		([&]<typename value_t>() {
			if (!handled && type_hash == rv::detail::pin_type_hash<value_t>()) {
				fn.template operator()<value_t>();
				handled = true;
			}
		}.template operator()<std::tuple_element_t<Indices, Tuple>>(), ...);
	}(std::make_index_sequence<std::tuple_size_v<Tuple>> {});
	return handled;
}

template <typename Tuple, typename Fn>
inline bool dispatch_wildcard_type(const nodes::wildcard_type_info& type, Fn&& fn);

template <typename Dst, typename Src>
inline void copy_matching_fields(Dst& dst, const Src& src);

} // namespace rv::detail


namespace rv {

template <typename T>
struct reflect_base {
	static constexpr size_t type_hash = rv::detail::pin_type_hash<T>();
	static constexpr size_t size = sizeof(T);
	static constexpr size_t offset = 0;
	static constexpr std::string_view name = mars::meta::display_name<T>();
	static constexpr mars_format_type format = mars::graphics::make_format<T>();
};

template <typename T>
struct reflect : public reflect_base<T> {
};

template <>
struct reflect<float> : public reflect_base<float> {
	static constexpr std::string_view name = "float";
};

template <>
struct reflect<mars::matrix4<float>> : public reflect_base<mars::matrix4<float>> {
	static constexpr std::string_view name = "float4x4";
};

template <>
struct reflect<mars::vector2<float>> : public reflect_base<mars::vector2<float>> {
	static constexpr std::string_view name = "float2";
};

template <>
struct reflect<mars::vector3<float>> : public reflect_base<mars::vector3<float>> {
	static constexpr std::string_view name = "float3";
};

template <>
struct reflect<mars::vector4<float>> : public reflect_base<mars::vector4<float>> {
	static constexpr std::string_view name = "float4";
};

template <>
struct reflect<unsigned int> : public reflect_base<unsigned int> {
	static constexpr std::string_view name = "uint";
};

template <>
struct reflect<mars::vector2<unsigned int>> : public reflect_base<mars::vector2<unsigned int>> {
	static constexpr std::string_view name = "uint2";
};

template <>
struct reflect<mars::vector3<unsigned int>> : public reflect_base<mars::vector3<unsigned int>> {
	static constexpr std::string_view name = "uint3";
};

template <>
struct reflect<mars::vector4<unsigned int>> : public reflect_base<mars::vector4<unsigned int>> {
	static constexpr std::string_view name = "uint4";
};

namespace nodes {

template <typename T>
using wildcard_type_traits = rv::detail::value_vector_traits<T>;

template <typename T>
consteval bool wildcard_type_is_texture() {
	return std::is_same_v<T, mars::vector3<unsigned char>> ||
		std::is_same_v<T, rv::resource_tags::texture_slot>;
}

template <typename T>
inline wildcard_type_info make_wildcard_type_info() {
	using value_t = typename wildcard_type_traits<T>::element_t;
	return {
		.type_hash = rv::detail::pin_type_hash<value_t>(),
		.is_container = wildcard_type_traits<T>::is_vector,
		.has_virtual_struct = false,
		.virtual_struct_name = {},
		.virtual_struct_layout_fingerprint = 0,
	};
}

wildcard_type_info wildcard_type_from_pin(const NE_Pin& pin);

void apply_wildcard_type_to_pin(NE_Pin& pin, const wildcard_type_info& type);

bool wildcard_types_equal(const wildcard_type_info& lhs, const wildcard_type_info& rhs);

template <typename T>
inline bool wildcard_type_matches(const wildcard_type_info& type) {
	using value_t = typename wildcard_type_traits<T>::element_t;
	return type.type_hash == rv::detail::pin_type_hash<value_t>() &&
		   type.is_container == wildcard_type_traits<T>::is_vector &&
		   !type.has_virtual_struct;
}

} // namespace nodes

namespace detail {

template <typename Tuple, typename Fn>
inline bool dispatch_wildcard_type(const nodes::wildcard_type_info& type, Fn&& fn) {
	bool handled = false;
	[&]<size_t... Indices>(std::index_sequence<Indices...>) {
		([&]<typename value_t>() {
			if (!handled && nodes::wildcard_type_matches<value_t>(type)) {
				fn.template operator()<value_t>();
				handled = true;
			}
		}.template operator()<std::tuple_element_t<Indices, Tuple>>(), ...);
	}(std::make_index_sequence<std::tuple_size_v<Tuple>> {});
	return handled;
}

template <typename Dst, typename Src>
inline void copy_matching_fields(Dst& dst, const Src& src) {
	constexpr auto ctx = std::meta::access_context::current();
	template for (constexpr auto dst_mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^Dst, ctx))) {
		template for (constexpr auto src_mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^Src, ctx))) {
			if constexpr (
				std::meta::identifier_of(dst_mem) == std::meta::identifier_of(src_mem) &&
				std::meta::is_same_type(std::meta::type_of(dst_mem), std::meta::type_of(src_mem))
			) {
				dst.[:dst_mem:] = src.[:src_mem:];
			}
		}
	}
}

} // namespace detail

namespace nodes {

template <typename T>
inline wildcard_value make_wildcard_value(T value) {
	wildcard_value result = wildcard_value::make<T>(std::move(value));
	result.type = make_wildcard_type_info<T>();
	return result;
}

template <typename Tuple>
inline bool any_to_wildcard_value(const std::any& source, const wildcard_type_info& type, wildcard_value& out_value) {
	bool success = false;
	rv::detail::dispatch_wildcard_type<Tuple>(type, [&]<typename value_t>() {
		if (source.type() != typeid(value_t))
			return;
		out_value = make_wildcard_value(std::any_cast<value_t>(source));
		if (type.has_virtual_struct) {
			out_value.type.has_virtual_struct = true;
			out_value.type.virtual_struct_name = type.virtual_struct_name;
			out_value.type.virtual_struct_layout_fingerprint = type.virtual_struct_layout_fingerprint;
		}
		success = true;
	});
	return success;
}

template <typename Tuple>
inline bool wildcard_value_to_any(const wildcard_value& source, std::any& out_value) {
	bool success = false;
	rv::detail::dispatch_wildcard_type<Tuple>(source.type, [&]<typename value_t>() {
		if (source.storage == nullptr)
			return;
		out_value = source.as<value_t>();
		success = true;
	});
	return success;
}

template <typename T>
inline void append_bytes(std::vector<std::byte>& bytes, const T& value) {
	static_assert(std::is_trivially_copyable_v<T>);
	const auto* begin = reinterpret_cast<const std::byte*>(&value);
	bytes.insert(bytes.end(), begin, begin + sizeof(T));
}

const NE_Pin* find_pin_by_label(const std::vector<NE_Pin>& pins, std::string_view label);

const NE_Pin* find_pin_by_id(const std::vector<NE_Pin>& pins, int pin_id);

inline const NE_Link* find_input_link(const NodeGraph& graph, const NE_Node& node, std::string_view label) {
	const NE_Pin* pin = find_pin_by_label(node.inputs, label);
	if (pin == nullptr)
		return nullptr;
	const auto it = std::ranges::find_if(graph.links, [&](const NE_Link& link) {
		return link.to_node == node.id && link.to_pin == pin->id;
	});
	return it == graph.links.end() ? nullptr : &*it;
}

template <typename T>
inline std::string json_stringify(T value) {
	return rv::detail::generic_json_stringify(std::move(value));
}

template <typename T>
inline bool json_parse(std::string_view json, T& value) {
	return rv::detail::generic_json_parse(json, value);
}

template <typename T>
inline NE_Pin make_pin(std::string label, bool is_container = false, bool required = true) {
	NE_Pin pin;
	pin.label = std::move(label);
	pin.type_hash = rv::detail::pin_type_hash<T>();
	pin.is_container = is_container;
	pin.required = required;
	return pin;
}

void apply_variable_slot_metadata(NE_Pin& pin, const variable_slot_state& slot);

std::string pin_display_label(const NE_Pin& pin);

void clear_pin_template_metadata(NE_Pin& pin);

void apply_pin_template_metadata(NE_Pin& pin, size_t base_type_hash, size_t value_hash, std::string display_name);

bool equivalent_pin_layout(const std::vector<NE_Pin>& lhs, const std::vector<NE_Pin>& rhs);

NodeTypeInfo make_basic_custom_node(size_t type, std::string title, std::vector<NE_Pin> inputs = {}, std::vector<NE_Pin> outputs = {});

template <typename Tag>
inline NodeTypeInfo make_basic_custom_node(std::string title, std::vector<NE_Pin> inputs = {}, std::vector<NE_Pin> outputs = {}) {
	return make_basic_custom_node(node_type_v<Tag>, std::move(title), std::move(inputs), std::move(outputs));
}

} // namespace rv::nodes
} // namespace rv
