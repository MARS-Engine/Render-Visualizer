#pragma once

#include <mars/hash/meta.hpp>
#include <mars/imgui/struct_editor.hpp>
#include <mars/math/vector3.hpp>
#include <mars/meta.hpp>
#include <mars/meta/type_erased.hpp>

#include <algorithm>
#include <cstddef>
#include <new>
#include <string_view>
#include <type_traits>
#include <vector>

namespace rv {

struct input_annotation {};
struct output_annotation {};
struct node_pure_annotation {};
struct execute_annotation {};
struct execution_pin_tag {};

enum class pin_kind {
	data,
	execution
};

constexpr struct input_annotation input = {};
constexpr struct output_annotation output = {};
constexpr struct execute_annotation execute = {};
consteval node_pure_annotation node_pure() { return {}; }

template<typename T>
struct pin_reflection {
	static constexpr mars::vector3<unsigned char> colour = { 173, 216, 230 };
};

template<>
struct pin_reflection<execution_pin_tag> {
	static constexpr mars::vector3<unsigned char> colour = { 255, 255, 255 };
};

using pin_value_resolve_fn = mars::meta::type_erased_ptr (*)(void* _instance, const void* _member_handle);
using pin_value_copy_fn = void (*)(mars::meta::type_erased_ptr _destination, mars::meta::type_erased_ptr _source);
using pin_inspector_render_fn = bool (*)(mars::meta::type_erased_ptr _value, std::string_view _label);
using node_instance_copy_construct_fn = void (*)(void* _destination, const void* _source);
using node_instance_destroy_fn = void (*)(void* _instance);
using node_execute_invoke_fn = void (*)(void* _instance, const void* _member_handle);

struct node_execute_data {
	const void* member_handle = nullptr;
	node_execute_invoke_fn invoke = nullptr;

	bool valid() const {
		return member_handle != nullptr && invoke != nullptr;
	}
};

struct node_runtime_info {
	std::size_t instance_size = 0;
	std::size_t instance_alignment = alignof(std::max_align_t);
	node_instance_copy_construct_fn copy_construct = nullptr;
	node_instance_destroy_fn destroy = nullptr;
	node_execute_data execute = {};
	bool pure = false;
};

template<typename T>
bool render_pin_inspector(mars::meta::type_erased_ptr _value, std::string_view _label) {
	T* value = _value.get<T>();
	if (value == nullptr)
		return false;
	return mars::imgui::struct_editor<T>(*value).render(_label);
}

struct pin_draw_data {
	std::string_view name = {};
	mars::vector3<unsigned char> colour = {};
	std::size_t type_hash = 0;
	pin_kind kind = pin_kind::data;
	const void* member_handle = nullptr;
	pin_value_resolve_fn resolve_value = nullptr;
	pin_value_copy_fn copy_value = nullptr;
	pin_inspector_render_fn render_inspector = nullptr;
};

namespace detail {

template<auto MemberPtr>
struct reflected_member_storage {
	inline static constexpr auto value = MemberPtr;
};

template<auto MemberPtr>
mars::meta::type_erased_ptr resolve_pin_value(void* _instance, const void* _member_handle) {
	using owner_t = typename mars::meta::member_pointer_info<decltype(MemberPtr)>::parent;

	owner_t* instance = static_cast<owner_t*>(_instance);
	const auto* member = static_cast<const decltype(MemberPtr)*>(_member_handle);
	if (instance == nullptr || member == nullptr)
		return {};
	return mars::meta::type_erased_ptr(&(instance->**member));
}

template<typename T>
void copy_pin_value(mars::meta::type_erased_ptr _destination, mars::meta::type_erased_ptr _source) {
	T* destination = _destination.get<T>();
	T* source = _source.get<T>();
	if (destination == nullptr || source == nullptr)
		return;
	*destination = *source;
}

template<typename T>
void copy_node_instance(void* _destination, const void* _source) {
	if (_destination == nullptr || _source == nullptr)
		return;
	new (_destination) T(*static_cast<const T*>(_source));
}

template<typename T>
void destroy_node_instance(void* _instance) {
	if (_instance == nullptr)
		return;
	static_cast<T*>(_instance)->~T();
}

template<auto MemberPtr>
void invoke_execute_member(void* _instance, const void* _member_handle) {
	using owner_t = typename mars::meta::member_function_pointer_info<decltype(MemberPtr)>::t_parent;
	using return_t = typename mars::meta::member_function_pointer_info<decltype(MemberPtr)>::t_return;

	static_assert(std::is_same_v<return_t, void>);
	static_assert(mars::meta::member_function_pointer_info<decltype(MemberPtr)>::args_size == 0);

	owner_t* instance = static_cast<owner_t*>(_instance);
	const auto* member = static_cast<const decltype(MemberPtr)*>(_member_handle);
	if (instance == nullptr || member == nullptr)
		return;
	(instance->**member)();
}

template<auto MemberPtr>
pin_draw_data make_data_pin(std::string_view _name) {
	using value_t = typename mars::meta::member_pointer_info<decltype(MemberPtr)>::type;

	return {
		.name = _name,
		.colour = pin_reflection<value_t>::colour,
		.type_hash = mars::hash::type_fingerprint_v<value_t>,
		.kind = pin_kind::data,
		.member_handle = &reflected_member_storage<MemberPtr>::value,
		.resolve_value = &resolve_pin_value<MemberPtr>,
		.copy_value = &copy_pin_value<value_t>,
		.render_inspector = &render_pin_inspector<value_t>
	};
}

template<typename T>
consteval std::size_t execute_member_count() {
	constexpr auto ctx = std::meta::access_context::current();
	std::size_t count = 0;

	template for (constexpr auto mem : std::define_static_array(std::meta::members_of(^^T, ctx))) {
		if constexpr (std::meta::is_function(mem) && !std::meta::is_special_member_function(mem) && mars::meta::has_annotation<execute_annotation>(mem))
			++count;
	}

	return count;
}

template<typename T>
node_execute_data make_execute_data() {
	static_assert(execute_member_count<T>() == 1, "Nodes must define exactly one [[=rv::execute]] member function");

	node_execute_data data = {};
	constexpr auto ctx = std::meta::access_context::current();
	template for (constexpr auto mem : std::define_static_array(std::meta::members_of(^^T, ctx))) {
		if constexpr (std::meta::is_function(mem) && !std::meta::is_special_member_function(mem) && mars::meta::has_annotation<execute_annotation>(mem)) {
			constexpr auto member_ptr = &[:mem:];
			data = {
				.member_handle = &reflected_member_storage<member_ptr>::value,
				.invoke = &invoke_execute_member<member_ptr>
			};
		}
	}

	return data;
}

} // namespace detail

template<typename T>
struct node_reflection {
	static constexpr std::string_view name = mars::meta::display_name<T>();
	static constexpr bool pure = mars::meta::has_annotation<node_pure_annotation>(^^T);

	static void get_pin_draw_info(std::vector<pin_draw_data>& _inputs, std::vector<pin_draw_data>& _outputs) {
		if constexpr (!pure) {
			_inputs.push_back({
				.name = "exec_in",
				.colour = pin_reflection<execution_pin_tag>::colour,
				.type_hash = mars::hash::type_fingerprint_v<execution_pin_tag>,
				.kind = pin_kind::execution
			});
			_outputs.push_back({
				.name = "exec_out",
				.colour = pin_reflection<execution_pin_tag>::colour,
				.type_hash = mars::hash::type_fingerprint_v<execution_pin_tag>,
				.kind = pin_kind::execution
			});
		}

		constexpr auto ctx = std::meta::access_context::current();
		template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^T, ctx))) {
			if constexpr (!mars::meta::has_annotation<input_annotation>(mem) && !mars::meta::has_annotation<output_annotation>(mem))
				continue;
			else {
				constexpr auto member_ptr = &[:mem:];
				pin_draw_data data = detail::make_data_pin<member_ptr>(mars::meta::display_name(mem));

				if constexpr (mars::meta::has_annotation<input_annotation>(mem))
					_inputs.push_back(data);
				else
					_outputs.push_back(data);
			}
		}
	}

	static node_runtime_info get_runtime_info() {
		return {
			.instance_size = sizeof(T),
			.instance_alignment = alignof(T),
			.copy_construct = &detail::copy_node_instance<T>,
			.destroy = &detail::destroy_node_instance<T>,
			.execute = detail::make_execute_data<T>(),
			.pure = pure
		};
	}
};

} // namespace rv
