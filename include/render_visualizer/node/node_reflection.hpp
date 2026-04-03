#pragma once

#include "meta"
#include <mars/hash/meta.hpp>
#include <mars/imgui/struct_editor.hpp>
#include <mars/math/vector3.hpp>
#include <mars/meta.hpp>
#include <mars/meta/type_erased.hpp>
#include <mars/utility/enum_flags.hpp>

#include <render_visualizer/node/node_metadata.hpp>
#include <render_visualizer/node/operations.hpp>
#include <render_visualizer/type_reflection.hpp>

#include <algorithm>
#include <cstddef>
#include <new>
#include <string_view>
#include <type_traits>
#include <vector>


namespace rv {
enum pin_flags : uint8_t {
	none = 0,
	mandatory = 1 << 0,
};
}

template <>
struct mars::enum_flags::enabled<rv::pin_flags> : std::true_type {};

namespace rv {

enum class pin_kind {
	data,
	execution
};

template<>
struct type_reflection<execution_pin_tag> {
	static constexpr std::string_view name = "Exec";
	static constexpr mars::vector3<unsigned char> colour = { 255, 255, 255 };
};

} // namespace rv

namespace rv {

template<typename T>
bool render_pin_inspector(mars::meta::type_erased_ptr _value, std::string_view _label) {
	return mars::imgui::struct_editor<T>(*_value.get<T>()).render(_label);
}

struct pin_draw_data {
	std::string_view name = {};
	mars::vector3<unsigned char> colour = {};
	std::size_t type_hash = 0;
	pin_kind kind = pin_kind::data;
	pin_operations ops = {};
	pin_flags flags = pin_flags::none;
};

namespace detail {

template<auto MemberPtr>
mars::meta::type_erased_ptr resolve_pin_value(mars::meta::type_erased_ptr _instance) {
	using owner_t = typename mars::meta::member_pointer_info<decltype(MemberPtr)>::parent;

	owner_t* instance = static_cast<owner_t*>(_instance.get<void>());
	if (instance == nullptr)
		return {};
	return mars::meta::type_erased_ptr(&(instance->*MemberPtr));
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
void assign_ptr_value(mars::meta::type_erased_ptr _destination, mars::meta::type_erased_ptr _source) {
	T** destination = _destination.get<T*>();
	T* source = _source.get<T>();
	if (destination == nullptr || source == nullptr)
		return;
	*destination = source;
}

template<typename T>
void copy_node_instance(mars::meta::type_erased_ptr _destination, mars::meta::type_erased_ptr _source) {
	T* destination = static_cast<T*>(_destination.get<void>());
	const T* source = _source.get<T>();
	if (destination == nullptr || source == nullptr)
		return;
	new (destination) T(*source);
}

template<typename T>
void destroy_node_instance(mars::meta::type_erased_ptr _instance) {
	T* instance = static_cast<T*>(_instance.get<void>());
	if (instance == nullptr)
		return;
	instance->~T();
}

template<auto MemberPtr>
void invoke_execute_member(mars::meta::type_erased_ptr _instance, mars::meta::type_erased_ptr* _dynamic_pins, std::size_t _dynamic_pin_count) {
	using owner_t = typename mars::meta::member_function_pointer_info<decltype(MemberPtr)>::t_parent;
	using return_t = typename mars::meta::member_function_pointer_info<decltype(MemberPtr)>::t_return;

	static_assert(std::is_same_v<return_t, void>);

	owner_t* instance = static_cast<owner_t*>(_instance.get<void>());
	if (instance == nullptr)
		return;
	
	if constexpr (mars::meta::member_function_pointer_info<decltype(MemberPtr)>::args_size == 0) {
		(instance->*MemberPtr)();
	} 
	else if constexpr (mars::meta::member_function_pointer_info<decltype(MemberPtr)>::args_size == 1) {
		using arg0_t = std::tuple_element_t<0, typename mars::meta::member_function_pointer_info<decltype(MemberPtr)>::t_args_tuple>;
		static_assert(std::is_same_v<std::decay_t<arg0_t>, std::vector<mars::meta::type_erased_ptr>>);
		std::vector<mars::meta::type_erased_ptr> dyn_pins(_dynamic_pins, _dynamic_pins + _dynamic_pin_count);
		(instance->*MemberPtr)(dyn_pins);
	}
}

template<std::meta::info Mem>
consteval pin_draw_data make_data_pin() {
	using value_t = [:std::meta::type_of(Mem):];

	pin_draw_data result = {
		.name = mars::meta::display_name(Mem),
		.colour = type_reflection<std::remove_pointer_t<value_t>>::colour,
		.type_hash = mars::hash::type_fingerprint_v<std::remove_pointer_t<value_t>>,
		.kind = pin_kind::data,
		.ops = {
			.resolve_value = &resolve_pin_value<(&[:Mem:])>,
			.copy_value = nullptr,
			.render_inspector = &render_pin_inspector<value_t>
		}
	};

	if constexpr (std::is_pointer_v<value_t>) {
		result.flags = pin_flags::mandatory;
		result.ops.copy_value = &assign_ptr_value<std::remove_pointer_t<value_t>>;
	} 
	else
		result.ops.copy_value = &copy_pin_value<value_t>;

	return result;
}

template<typename T>
bool inspect_property(T& _value, std::string_view _label, [[maybe_unused]] const std::vector<std::unique_ptr<rv::variable>>* _variables) {
	return mars::imgui::struct_editor<T>(_value).render(_label);
}

template<typename T>
bool inspect_properties_impl(mars::meta::type_erased_ptr _instance, const std::vector<std::unique_ptr<rv::variable>>* _variables) {
	constexpr auto ctx = std::meta::access_context::current();
	T* instance = _instance.get<T>();
	
	if (instance == nullptr)
		return false;
	
	bool changed = false;
	template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^T, ctx))) {
		if constexpr (mars::meta::has_annotation<stack_annotation>(mem)) {
			changed |= inspect_property(instance->*(&[:mem:]), mars::meta::display_name(mem), _variables);
		}
	}
	return changed;
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
node_operations make_node_operations() {
	static_assert(execute_member_count<T>() == 1, "Nodes must define exactly one [[=rv::execute]] member function");

	node_operations ops = {
		.copy_construct = &copy_node_instance<T>,
		.destroy = &destroy_node_instance<T>,
		.execute = nullptr,
		.inspect_properties = &inspect_properties_impl<T>
	};

	constexpr auto ctx = std::meta::access_context::current();
	template for (constexpr auto mem : std::define_static_array(std::meta::members_of(^^T, ctx))) {
		if constexpr (std::meta::is_function(mem) && !std::meta::is_special_member_function(mem) && mars::meta::has_annotation<execute_annotation>(mem)) {
			ops.execute = &invoke_execute_member<&[:mem:]>;
		}
	}

	return ops;
}

} // namespace detail

template<typename T>
struct node_reflection {
	static constexpr std::string_view name = mars::meta::display_name<T>();
	static constexpr bool pure = mars::meta::has_annotation<node_pure_annotation>(^^T);
	static constexpr bool hidden = mars::meta::has_annotation<node_hidden_annotation>(^^T);

	static void get_pin_draw_info(mars::meta::type_erased_ptr _instance, std::vector<pin_draw_data>& _inputs, std::vector<pin_draw_data>& _outputs) {
		if constexpr (!pure) {
			_inputs.push_back({
				.name = "exec_in",
				.colour = type_reflection<execution_pin_tag>::colour,
				.type_hash = mars::hash::type_fingerprint_v<execution_pin_tag>,
				.kind = pin_kind::execution,
			});
			_outputs.push_back({
				.name = "exec_out",
				.colour = type_reflection<execution_pin_tag>::colour,
				.type_hash = mars::hash::type_fingerprint_v<execution_pin_tag>,
				.kind = pin_kind::execution,
			});
		}

		constexpr auto ctx = std::meta::access_context::current();
		template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^T, ctx))) {
			constexpr bool is_input = mars::meta::has_annotation<input_annotation>(mem);
			constexpr bool is_output = mars::meta::has_annotation<output_annotation>(mem);

			if constexpr (is_input) {
				pin_draw_data data = detail::make_data_pin<mem>();
				_inputs.push_back(data);
			} else if constexpr (is_output) {
				pin_draw_data data = detail::make_data_pin<mem>();
				_outputs.push_back(data);
			}
		}

		template for (constexpr auto mem : std::define_static_array(std::meta::members_of(^^T, ctx))) {
			if constexpr (std::meta::is_function(mem) && !std::meta::is_special_member_function(mem) && mars::meta::has_annotation<pins_override_annotation>(mem)) {
				const T* instance = _instance.get<T>();
				if (instance != nullptr) {
					(instance->*(&[:mem:]))(_inputs, _outputs);
				}
			}
		}
	}

	static node_metadata get_metadata() {
		return {
			.pure = mars::meta::has_annotation<node_pure_annotation>(^^T),
			.instance_size = sizeof(T),
			.instance_alignment = alignof(T),
			.operations = detail::make_node_operations<T>()
		};
	}
};

} // namespace rv

