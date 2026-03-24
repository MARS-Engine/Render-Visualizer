#pragma once

#include <render_visualizer/runtime/render_state.hpp>

#include <tuple>
#include <type_traits>
#include <utility>

namespace rv::runtime_detail {

// Compile-time map: pin tag type → runtime impl resource struct.
using resource_type_map = std::tuple<
	std::pair<rv::resource_tags::vertex_buffer,     vertex_buffer_resources>,
	std::pair<rv::resource_tags::index_buffer,      index_buffer_resources>,
	std::pair<rv::resource_tags::uniform_resource,  uniform_buffer_resources>,
	std::pair<mars::vector3<unsigned char>,         framebuffer_attachment_resources>,
	std::pair<rv::resource_tags::texture_slot,      texture_slot_resources>,
	std::pair<rv::resource_tags::shader_module,     shader_module_resources>,
	std::pair<rv::resource_tags::render_pass,       render_pass_resources>,
	std::pair<rv::resource_tags::framebuffer,       framebuffer_resources>,
	std::pair<rv::resource_tags::depth_buffer,      depth_buffer_resources>,
	std::pair<rv::resource_tags::graphics_pipeline, graphics_pipeline_resources>,
	std::pair<rv::resource_tags::material_resource, material_binding_resources>
>;

namespace detail {

template <typename TagT, typename Tuple>
struct is_tag_in_map_impl : std::false_type {};

template <typename TagT, typename First, typename... Rest>
struct is_tag_in_map_impl<TagT, std::tuple<First, Rest...>>
	: std::conditional_t<
		std::is_same_v<TagT, typename First::first_type>,
		std::true_type,
		is_tag_in_map_impl<TagT, std::tuple<Rest...>>
	> {};

template <typename ImplT, typename Tuple>
struct impl_tag_hash_impl {
	static constexpr size_t value = 0;
};

template <typename ImplT, typename First, typename... Rest>
struct impl_tag_hash_impl<ImplT, std::tuple<First, Rest...>> {
	static constexpr size_t value =
		std::is_same_v<ImplT, typename First::second_type>
			? rv::detail::pin_type_hash<typename First::first_type>()
			: impl_tag_hash_impl<ImplT, std::tuple<Rest...>>::value;
};

} // namespace detail

// True if TagT is a key in resource_type_map.
template <typename TagT>
inline constexpr bool is_in_resource_type_map_v =
	detail::is_tag_in_map_impl<TagT, resource_type_map>::value;

// pin_type_hash of the tag for a given impl type (0 if not in map).
template <typename ImplT>
inline constexpr size_t resource_impl_tag_hash_v =
	detail::impl_tag_hash_impl<ImplT, resource_type_map>::value;

// Calls fn.operator()<TagT, ImplT>() for the entry whose tag hash matches type_hash.
// Returns true if matched.
template <typename Fn>
inline bool dispatch_resource_type(size_t type_hash, Fn&& fn) {
	bool handled = false;
	[&]<size_t... Is>(std::index_sequence<Is...>) {
		([&]<size_t I>() {
			using Pair = std::tuple_element_t<I, resource_type_map>;
			using tag_t = typename Pair::first_type;
			using impl_t = typename Pair::second_type;
			if (!handled && type_hash == rv::detail::pin_type_hash<tag_t>()) {
				fn.template operator()<tag_t, impl_t>();
				handled = true;
			}
		}.template operator()<Is>(), ...);
	}(std::make_index_sequence<std::tuple_size_v<resource_type_map>>{});
	return handled;
}

} // namespace rv::runtime_detail
