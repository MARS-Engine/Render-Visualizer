#pragma once

#include <imgui.h>

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/graph_blackboard.hpp>
#include <render_visualizer/nodes/support.hpp>

#include <array>
#include <functional>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace rv::nodes {

enum class function_node_behavior {
	pure,
	callable,
	event
};

struct function_param_descriptor {
	std::string_view label;
	size_t type_hash = 0;
	bool is_container = false;
	bool is_output_ref = false;
	bool is_context = false;
	bool is_wildcard = false;
	std::string_view wildcard_group;
	size_t order = 0;
};

struct blackboard_field_descriptor {
	std::string_view label;
	std::string_view key;
	size_t type_hash = 0;
	bool is_container = false;
};

struct blackboard_node_state {
	int field_index = -1;
};

template <typename T>
using vector_value_traits = rv::detail::value_vector_traits<T>;

template <typename>
struct function_type_info;

template <typename R, typename... Args>
struct function_type_info<R(Args...)> {
	using t_return = R;
	using t_args_tuple = std::tuple<Args...>;
	inline static constexpr size_t args_size = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_type_info<R(Args...) noexcept> {
	using t_return = R;
	using t_args_tuple = std::tuple<Args...>;
	inline static constexpr size_t args_size = sizeof...(Args);
};

template <auto FunctionInfo>
using reflected_function_type_t = typename[:std::meta::type_of(FunctionInfo):];

template <typename T>
inline constexpr bool is_context_arg_v =
	std::is_same_v<T, rv::graph_execution_context&> ||
	std::is_same_v<T, const rv::graph_execution_context&>;

template <typename T>
inline constexpr bool is_output_ref_arg_v =
	std::is_lvalue_reference_v<T> &&
	!std::is_const_v<std::remove_reference_t<T>> &&
	!is_context_arg_v<T>;

template <typename T>
using value_type_t = std::remove_cvref_t<T>;

template <typename T>
inline constexpr bool is_wildcard_view_arg_v =
	std::is_same_v<value_type_t<T>, wildcard_value_view>;

template <typename T>
inline constexpr bool is_wildcard_value_arg_v =
	std::is_same_v<value_type_t<T>, wildcard_value>;

template <typename T>
inline constexpr bool is_wildcard_arg_v =
	is_wildcard_view_arg_v<T> || is_wildcard_value_arg_v<T>;

template <typename T>
using pin_value_type_t = typename vector_value_traits<value_type_t<T>>::element_t;

template <typename T>
using storage_type_t = std::conditional_t<
	is_context_arg_v<T>,
	std::monostate,
	std::conditional_t<is_wildcard_arg_v<T>, wildcard_value, value_type_t<T>>
>;

template <typename T>
consteval function_param_descriptor make_type_descriptor(
	std::string_view label,
	bool is_output_ref = false,
	bool is_context = false,
	bool is_wildcard = false,
	std::string_view wildcard_group = {},
	size_t order = 0
) {
	using value_t = value_type_t<T>;
	using pin_t = std::conditional_t<is_wildcard_arg_v<T>, float, pin_value_type_t<T>>;
	return {
		.label = label,
		.type_hash = is_wildcard ? 0u : rv::detail::pin_type_hash<pin_t>(),
		.is_container = is_wildcard ? false : vector_value_traits<value_t>::is_vector,
		.is_output_ref = is_output_ref,
		.is_context = is_context,
		.is_wildcard = is_wildcard,
		.wildcard_group = wildcard_group,
		.order = order
	};
}

template <auto FunctionInfo>
consteval auto make_function_param_descriptors() {
	constexpr size_t N = std::meta::parameters_of(FunctionInfo).size();
	std::array<function_param_descriptor, N> descriptors {};
	size_t index = 0;

	template for (constexpr auto param : std::define_static_array(std::meta::parameters_of(FunctionInfo))) {
		constexpr auto param_type = std::meta::type_of(param);
		using declared_t = typename[:param_type:];
		std::string_view label = "value";
		if constexpr (std::meta::has_identifier(param))
			label = std::define_static_string(std::meta::identifier_of(param));
		constexpr bool is_wildcard = is_wildcard_arg_v<declared_t>;
		descriptors[index] = make_type_descriptor<declared_t>(
			label,
			is_output_ref_arg_v<declared_t>,
			is_context_arg_v<declared_t>,
			is_wildcard,
			is_wildcard ? std::string_view("value") : std::string_view {},
			index
		);
		++index;
	}

	return descriptors;
}

template <typename Blackboard>
consteval auto make_blackboard_field_descriptors() {
	constexpr auto ctx = std::meta::access_context::current();
	constexpr size_t N = std::meta::nonstatic_data_members_of(^^Blackboard, ctx).size();
	std::array<blackboard_field_descriptor, N> descriptors {};
	size_t index = 0;

	template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^Blackboard, ctx))) {
		constexpr auto member_type = std::meta::type_of(mem);
		using declared_t = typename[:member_type:];
		using value_t = value_type_t<declared_t>;
		using pin_t = pin_value_type_t<declared_t>;
		descriptors[index++] = {
			.label = std::define_static_string(mars::meta::display_name(mem)),
			.key = std::define_static_string(std::meta::identifier_of(mem)),
			.type_hash = rv::detail::pin_type_hash<pin_t>(),
			.is_container = vector_value_traits<value_t>::is_vector
		};
	}

	return descriptors;
}

template <auto FunctionInfo>
inline constexpr auto function_param_descriptors_v = make_function_param_descriptors<FunctionInfo>();

template <typename Blackboard>
inline constexpr auto blackboard_field_descriptors_v = make_blackboard_field_descriptors<Blackboard>();

template <auto FunctionInfo>
consteval function_node_behavior detect_function_behavior() {
	constexpr bool is_pure = mars::meta::has_annotation<rv::node::pure>(FunctionInfo);
	constexpr bool is_callable = mars::meta::has_annotation<rv::node::callable>(FunctionInfo);
	constexpr bool is_event = mars::meta::has_annotation<rv::node::event>(FunctionInfo);
	static_assert((is_pure ? 1 : 0) + (is_callable ? 1 : 0) + (is_event ? 1 : 0) == 1,
		"Reflected function nodes must have exactly one [[=rv::node::pure()]], [[=rv::node::callable()]], or [[=rv::node::event()]] annotation.");

	if constexpr (is_event)
		return function_node_behavior::event;
	else if constexpr (is_callable)
		return function_node_behavior::callable;
	else
		return function_node_behavior::pure;
}

inline NE_Pin make_reflected_pin(const function_param_descriptor& descriptor) {
	NE_Pin pin;
	pin.label = std::string(descriptor.label);
	pin.type_hash = descriptor.type_hash;
	pin.is_container = descriptor.is_container;
	pin.kind = NE_PinKind::data;
	pin.is_wildcard = descriptor.is_wildcard;
	pin.wildcard_group = std::string(descriptor.wildcard_group);
	pin.wildcard_resolved = !descriptor.is_wildcard;
	pin.id = static_cast<int>(descriptor.order);
	return pin;
}

inline NE_Pin make_reflected_pin(const blackboard_field_descriptor& descriptor) {
	NE_Pin pin;
	pin.label = std::string(descriptor.label);
	pin.type_hash = descriptor.type_hash;
	pin.is_container = descriptor.is_container;
	pin.kind = NE_PinKind::data;
	return pin;
}

template <auto FunctionInfo>
consteval std::string_view function_display_name_sv() {
	if constexpr (std::meta::has_identifier(FunctionInfo))
		return std::define_static_string(std::meta::identifier_of(FunctionInfo));
	return std::define_static_string(std::meta::display_string_of(FunctionInfo));
}

template <auto FunctionInfo>
inline std::string function_display_name() {
	return std::string(function_display_name_sv<FunctionInfo>());
}

template <auto FunctionInfo>
consteval size_t function_node_type_id() {
	std::string key = "rv::function::";
	switch (detect_function_behavior<FunctionInfo>()) {
	case function_node_behavior::pure: key += "pure::"; break;
	case function_node_behavior::callable: key += "callable::"; break;
	case function_node_behavior::event: key += "event::"; break;
	}
	key += std::string(function_display_name_sv<FunctionInfo>());
	return graph_runtime_fnv1a64(key);
}

template <typename Blackboard, size_t Index>
consteval auto blackboard_member_info() {
	constexpr auto ctx = std::meta::access_context::current();
	return std::define_static_array(std::meta::nonstatic_data_members_of(^^Blackboard, ctx))[Index];
}

template <typename Blackboard, size_t Index>
using blackboard_member_type_t = typename[:std::meta::type_of(blackboard_member_info<Blackboard, Index>()):];

template <typename Blackboard, typename Fn>
bool visit_blackboard_field(int field_index, Fn&& fn) {
	bool handled = false;
	template for (constexpr size_t index : std::views::iota(size_t { 0 }, blackboard_field_descriptors_v<Blackboard>.size())) {
		constexpr auto descriptor = blackboard_field_descriptors_v<Blackboard>[index];
		if constexpr (descriptor.label != std::string_view("Window Close Requested")) {
			if (field_index == static_cast<int>(index)) {
				fn.template operator()<blackboard_member_type_t<Blackboard, index>>(descriptor);
				handled = true;
			}
		}
	}
	return handled;
}

template <auto FunctionInfo>
bool (*make_function_executor())(rv::graph_execution_context&, NE_Node&, std::string&) {
	constexpr auto behavior = detect_function_behavior<FunctionInfo>();

	return [](rv::graph_execution_context& ctx, NE_Node& node, std::string& error) -> bool {
		error.clear();

		using fn_t = reflected_function_type_t<FunctionInfo>;
		using fn_info_t = function_type_info<fn_t>;
		using arg_tuple_t = typename fn_info_t::t_args_tuple;
		using return_t = typename fn_info_t::t_return;
		constexpr auto descriptors = function_param_descriptors_v<FunctionInfo>;

		if constexpr (behavior == function_node_behavior::event) {
			bool ok = true;
			template for (constexpr size_t index : std::views::iota(size_t { 0 }, descriptors.size())) {
				using declared_t = std::tuple_element_t<index, arg_tuple_t>;
				using value_t = value_type_t<declared_t>;
				if constexpr (!is_context_arg_v<declared_t>) {
					if (ok) {
						value_t value {};
						if (!ctx.read_blackboard<value_t>(descriptors[index].label, value, error))
							ok = false;
						else
							ctx.set_output(node, descriptors[index].label, value);
					}
				}
			}
			return ok;
		} else {
			bool ok = true;
			auto fn = &[:FunctionInfo:];

			[&]<size_t... Is>(std::index_sequence<Is...>) {
				using storage_tuple_t = std::tuple<storage_type_t<std::tuple_element_t<Is, arg_tuple_t>>...>;
				storage_tuple_t storage {};

				auto prepare_one = [&]<size_t I>() -> bool {
					using declared_t = std::tuple_element_t<I, arg_tuple_t>;
					using value_t = value_type_t<declared_t>;
					if constexpr (is_context_arg_v<declared_t>) {
						return true;
					} else if constexpr (is_output_ref_arg_v<declared_t> && is_wildcard_arg_v<declared_t>) {
						std::get<I>(storage) = wildcard_value {};
						return true;
					} else if constexpr (is_output_ref_arg_v<declared_t>) {
						std::get<I>(storage) = value_t {};
						return true;
					} else if constexpr (is_wildcard_arg_v<declared_t>) {
						return ctx.resolve_wildcard_input(node, descriptors[I].label, std::get<I>(storage), error);
					} else {
						return ctx.resolve_input<value_t>(node, descriptors[I].label, std::get<I>(storage), error);
					}
				};

				ok = (prepare_one.template operator()<Is>() && ...);
				if (!ok)
					return;

				auto invoke_one = [&]<size_t I>() -> decltype(auto) {
					using declared_t = std::tuple_element_t<I, arg_tuple_t>;
					if constexpr (is_context_arg_v<declared_t>) {
						return static_cast<declared_t>(ctx);
					} else if constexpr (is_wildcard_view_arg_v<declared_t>) {
						return wildcard_value_view {
							.storage = std::get<I>(storage).data(),
							.type = std::get<I>(storage).type
						};
					} else if constexpr (std::is_lvalue_reference_v<declared_t>) {
						return static_cast<declared_t>(std::get<I>(storage));
					} else {
						return static_cast<declared_t>(std::get<I>(storage));
					}
				};

				if constexpr (std::is_void_v<return_t>) {
					fn(invoke_one.template operator()<Is>()...);
				} else {
					return_t result = fn(invoke_one.template operator()<Is>()...);
					if constexpr (std::is_same_v<return_t, wildcard_value>) {
						if (!ctx.set_wildcard_output(node, "result", result, error))
							ok = false;
					} else {
						ctx.set_output(node, "result", result);
					}
				}

				auto store_one = [&]<size_t I>() {
					using declared_t = std::tuple_element_t<I, arg_tuple_t>;
					if constexpr (is_output_ref_arg_v<declared_t> && is_wildcard_arg_v<declared_t>) {
						if (!ctx.set_wildcard_output(node, descriptors[I].label, std::get<I>(storage), error))
							ok = false;
					} else if constexpr (is_output_ref_arg_v<declared_t>) {
						ctx.set_output(node, descriptors[I].label, std::get<I>(storage));
					}
				};
				(store_one.template operator()<Is>(), ...);
			}(std::make_index_sequence<std::tuple_size_v<arg_tuple_t>> {});

			return ok;
		}
	};
}

bool is_addable_wildcard_type(const wildcard_type_info& type);
bool add_wildcard_values(const wildcard_value_view& lhs, const wildcard_value_view& rhs, wildcard_value& out_value);
const NE_Pin* find_linked_wildcard_source_pin(const NodeGraph& graph, const NE_Node& node, std::string_view label, bool input_pin);
bool try_resolve_wildcard_group(
	const NodeGraph& graph,
	const NE_Node& node,
	const std::vector<NE_Pin>& wildcard_inputs,
	const std::vector<NE_Pin>& wildcard_outputs,
	std::string_view group,
	wildcard_type_info& out_type
);
void refresh_wildcard_function_node(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node);

template <auto FunctionInfo>
bool (*make_function_validator())(const NodeGraph&, const NodeTypeInfo&, const NE_Node&, std::string&) {
	if constexpr (function_display_name_sv<FunctionInfo>() == std::string_view("Add")) {
		return [](const NodeGraph&, const NodeTypeInfo&, const NE_Node& node, std::string& error) {
			const NE_Pin* lhs = nodes::find_pin_by_label(node.inputs, "lhs");
			const NE_Pin* rhs = nodes::find_pin_by_label(node.inputs, "rhs");
			const NE_Pin* result = nodes::find_pin_by_label(node.outputs, "result");
			if (lhs == nullptr || rhs == nullptr || result == nullptr) {
				error = "Add wildcard pins are missing.";
				return false;
			}
			if (!lhs->wildcard_resolved || !rhs->wildcard_resolved || !result->wildcard_resolved) {
				error.clear();
				return true;
			}
			const wildcard_type_info lhs_type = wildcard_type_from_pin(*lhs);
			if (!wildcard_types_equal(lhs_type, wildcard_type_from_pin(*rhs)) ||
				!wildcard_types_equal(lhs_type, wildcard_type_from_pin(*result))) {
				error = "All Add wildcard pins must resolve to the same type.";
				return false;
			}
			if (!is_addable_wildcard_type(lhs_type)) {
				error = "Add only supports float/uint scalars and vectors in v1.";
				return false;
			}
			error.clear();
			return true;
		};
	} else {
		return nullptr;
	}
}

template <auto FunctionInfo>
NodeTypeInfo make_function_node_type_info() {
	constexpr auto behavior = detect_function_behavior<FunctionInfo>();
	NodeTypeInfo info;
	info.meta.type = function_node_type_id<FunctionInfo>();
	info.meta.title = function_display_name<FunctionInfo>();

	if constexpr (behavior == function_node_behavior::pure)
		info.meta.pin_flow = enum_type::none;
	else if constexpr (behavior == function_node_behavior::event)
		info.meta.pin_flow = enum_type::output_only;
	else
		info.meta.pin_flow = enum_type::both;

	constexpr bool has_wildcards = [] {
		for (const auto& descriptor : function_param_descriptors_v<FunctionInfo>) {
			if (descriptor.is_wildcard)
				return true;
		}
		using return_t = typename function_type_info<reflected_function_type_t<FunctionInfo>>::t_return;
		return std::is_same_v<return_t, wildcard_value>;
	}();

	template for (constexpr size_t index : std::views::iota(size_t { 0 }, function_param_descriptors_v<FunctionInfo>.size())) {
		constexpr auto descriptor = function_param_descriptors_v<FunctionInfo>[index];
		if constexpr (behavior == function_node_behavior::event) {
			if constexpr (!descriptor.is_context) {
				if constexpr (descriptor.is_wildcard)
					info.pins.wildcard_output_templates.push_back(make_reflected_pin(descriptor));
				else
					info.pins.outputs.push_back(make_reflected_pin(descriptor));
			}
		} else {
			if constexpr (!descriptor.is_context && !descriptor.is_output_ref) {
				if constexpr (descriptor.is_wildcard)
					info.pins.wildcard_input_templates.push_back(make_reflected_pin(descriptor));
				else
					info.pins.inputs.push_back(make_reflected_pin(descriptor));
			}
			if constexpr (descriptor.is_output_ref) {
				if constexpr (descriptor.is_wildcard)
					info.pins.wildcard_output_templates.push_back(make_reflected_pin(descriptor));
				else
					info.pins.outputs.push_back(make_reflected_pin(descriptor));
			}
		}
	}

	using return_t = typename function_type_info<reflected_function_type_t<FunctionInfo>>::t_return;
	if constexpr (behavior != function_node_behavior::event && !std::is_void_v<return_t>) {
		if constexpr (std::is_same_v<return_t, wildcard_value>) {
			NE_Pin pin = make_reflected_pin(make_type_descriptor<return_t>("result", false, false, true, "result", function_param_descriptors_v<FunctionInfo>.size()));
			info.pins.wildcard_output_templates.push_back(std::move(pin));
		} else {
			info.pins.outputs.push_back(make_reflected_pin(make_type_descriptor<return_t>("result", false, false, false, {}, function_param_descriptors_v<FunctionInfo>.size())));
		}
	}

	info.meta.is_vm_node = true;
	info.meta.is_vm_pure = behavior == function_node_behavior::pure;
	info.meta.is_vm_callable = behavior == function_node_behavior::callable;
	info.meta.is_vm_event = behavior == function_node_behavior::event;
	info.hooks.vm_execute = make_function_executor<FunctionInfo>();
	info.hooks.validate_node = make_function_validator<FunctionInfo>();
	if constexpr (has_wildcards) {
		info.hooks.refresh_dynamic_pins = [](NodeGraph& graph, const NodeTypeInfo& type, NE_Node& node) {
			refresh_wildcard_function_node(graph, type, node);
		};
	}
	return info;
}

template <auto FunctionInfo>
NodeRegistry::registration_descriptor make_function_registration() {
	return {
		.type = function_node_type_id<FunctionInfo>(),
		.apply = [](NodeRegistry& registry) {
			registry.template register_function<FunctionInfo>();
		}
	};
}

struct blackboard_get_node;
using blackboard_get_node_tag = blackboard_get_node;

struct [[=mars::meta::display("Blackboard Get")]] blackboard_get_node {
	using custom_state_t = blackboard_node_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, blackboard_node_state& state);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node);
	[[=rv::node::pure()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::validate()]] static bool validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error);
};

struct blackboard_set_node;
using blackboard_set_node_tag = blackboard_set_node;

struct [[=mars::meta::display("Blackboard Set")]] blackboard_set_node {
	using custom_state_t = blackboard_node_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, blackboard_node_state& state);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::validate()]] static bool validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error);
};

inline const NodeRegistry::node_auto_registrar blackboard_get_node_registration(
	NodeRegistry::make_reflected_registration<blackboard_get_node>()
);

inline const NodeRegistry::node_auto_registrar blackboard_set_node_registration(
	NodeRegistry::make_reflected_registration<blackboard_set_node>()
);

[[=mars::meta::display("Tick")]]
[[=rv::node::event()]]
void Tick(float delta_time, float time);

[[=mars::meta::display("RightMouseClick")]]
[[=rv::node::event()]]
void RightMouseClick(bool right_mouse_clicked);

[[=mars::meta::display("WindowResize")]]
[[=rv::node::event()]]
void WindowResize(mars::vector2<size_t> window_size);

[[=mars::meta::display("WindowClose")]]
[[=rv::node::event()]]
void WindowClose(bool window_close_requested);

[[=mars::meta::display("Add")]]
[[=rv::node::pure()]]
void Add(
	wildcard_value_view lhs [[=rv::node::wildcard("value")]],
	wildcard_value_view rhs [[=rv::node::wildcard("value")]],
	wildcard_value& result [[=rv::node::wildcard("value")]]
);

[[=mars::meta::display("Multiply")]]
[[=rv::node::pure()]]
float Multiply(float lhs, float rhs);

[[=mars::meta::display("Sine")]]
[[=rv::node::pure()]]
float Sine(float angle);

[[=mars::meta::display("MakeFloat2")]]
[[=rv::node::pure()]]
mars::vector2<float> MakeFloat2(float x, float y);

[[=mars::meta::display("LogFloat")]]
[[=rv::node::callable()]]
void LogFloat(float value);

[[=mars::meta::display("MakeFloat3")]]
[[=rv::node::pure()]]
mars::vector3<float> MakeFloat3(float x, float y, float z);

} // namespace rv::nodes

template <auto FunctionInfo>
inline void NodeRegistry::register_function() {
	register_custom_node(rv::nodes::make_function_node_type_info<FunctionInfo>());
}
