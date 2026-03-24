#pragma once

#include <render_visualizer/node_graph.hpp>

#include <any>
#include <cstddef>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <vector>

namespace rv::detail {

template <typename Tuple>
struct processor_param_tuple;

template <typename... Args>
struct processor_param_tuple<std::tuple<Args...>> {
	using type = std::tuple<Args...>;
	static constexpr std::size_t skip_count = 0;
};

template <typename NodeT, typename... Args>
struct processor_param_tuple<std::tuple<NodeT&, Args...>> {
	using type = std::tuple<Args...>;
	static constexpr std::size_t skip_count = 1;
};

template <typename FnPtr, typename NodeT, typename Tuple>
struct processor_executor;

template <typename FnPtr, typename NodeT, typename... Args>
struct processor_executor<FnPtr, NodeT, std::tuple<Args...>> {
	static bool execute(NE_Node& node, FnPtr fn, NodeT& output) {
		using arg_tuple = std::tuple<Args...>;
		using param_tuple = typename processor_param_tuple<arg_tuple>::type;
		return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
			if constexpr (processor_param_tuple<arg_tuple>::skip_count == 0) {
				output = fn(node.processor_params[Is].template as<std::remove_cvref_t<std::tuple_element_t<Is, param_tuple>>>()...);
				return true;
			}
			else
				return fn(output, node.processor_params[Is].template as<std::remove_cvref_t<std::tuple_element_t<Is, param_tuple>>>()...);
		}(std::make_index_sequence<std::tuple_size_v<param_tuple>>{});
	}
};

template <auto Fn, typename StateT>
std::string save_state_adapter(const NE_Node& node) {
	return Fn(node.custom_state.as<StateT>());
}

template <auto Fn, typename StateT>
bool load_state_adapter(NE_Node& node, std::string_view json, std::string& error) {
	return Fn(node.custom_state.as<StateT>(), json, error);
}

template <auto Fn, typename StateT>
void editor_adapter_with_state(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	Fn(graph, node, node.custom_state.as<StateT>());
}

template <auto Fn>
void editor_adapter(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	Fn(graph, node);
}

} // namespace rv::detail

template <typename T>
void NodeRegistry::register_node(NodeRegistrationOptions options) {
	NodeTypeInfo info;
	constexpr bool is_start_node = mars::meta::has_annotation<rv::node::start>(^^T);
	constexpr bool is_end_node = mars::meta::has_annotation<rv::node::end_node>(^^T);
	constexpr bool is_function_outputs_node = mars::meta::has_annotation<rv::node::function_outputs>(^^T);
	static_assert(!(is_start_node && is_end_node), "Node types cannot be both [[=rv::node::start()]] and [[=rv::node::end_node()]].");

	info.meta.type = mars::hash::type_fingerprint_v<T>;
	info.meta.title = mars::meta::display_name(^^T);
	info.meta.is_permanent = options.is_permanent;
	info.meta.show_in_spawn_menu = options.show_in_spawn_menu;
	info.meta.is_start = is_start_node;
	info.meta.is_end = is_end_node;
	info.meta.is_function_outputs = is_function_outputs_node;

	if (info.meta.is_start)
		info.meta.pin_flow = enum_type::output_only;
	else if (info.meta.is_end)
		info.meta.pin_flow = enum_type::input_only;
	else if constexpr (mars::meta::has_annotation<rv::node::pin_flow_annotation>(^^T))
		info.meta.pin_flow = mars::meta::get_annotation<rv::node::pin_flow_annotation>(^^T)->value;
	else
		info.meta.pin_flow = enum_type::both;

	constexpr std::meta::access_context ctx = std::meta::access_context::current();
	template for (constexpr auto mem : std::define_static_array(std::meta::nonstatic_data_members_of(^^T, ctx))) {
		NE_Pin pin;
		pin.id = static_cast<int>(info.pins.inputs.size() + info.pins.outputs.size());
		pin.label = mars::meta::display_name(mem);

		constexpr auto mem_type = std::meta::type_of(mem);
		using C = [:mem_type:];

		constexpr bool is_container =
			std::meta::has_template_arguments(mem_type) &&
			std::meta::template_of(mem_type) == ^^std::vector;

		pin.is_container = is_container;
		if constexpr (is_container) {
			using Inner = [:std::meta::template_arguments_of(mem_type)[0]:];
			pin.type_hash = rv::detail::pin_type_hash<Inner>();
		} else {
			pin.type_hash = rv::detail::pin_type_hash<C>();
		}
		pin.required = !mars::meta::has_annotation<rv::node::optional>(mem);

		if constexpr (mars::meta::has_annotation<rv::node::input>(mem)) {
			info.pins.inputs.push_back(pin);
		} else if constexpr (mars::meta::has_annotation<rv::node::output>(mem)) {
			info.pins.outputs.push_back(pin);
		}
	};

	template for (constexpr auto ann : std::define_static_array(std::meta::members_of(^^rv::node, ctx))) {
		if constexpr (std::meta::is_type(ann) && std::meta::is_class_type(ann)) {
			using C = [:ann:];
			static_assert(count_annotated_functions<C, T>() <= 1,
				"Node types may define at most one function with a given hook annotation.");
		}
	}
	constexpr size_t vm_node_count =
		count_annotated_functions<rv::node::pure, T>() +
		count_annotated_functions<rv::node::callable, T>() +
		count_annotated_functions<rv::node::event, T>();
	static_assert(vm_node_count <= 1, "Node types may define at most one reflected VM execute function.");
	if constexpr (!is_start_node && !is_end_node && !mars::meta::has_annotation<rv::node::pin_flow_annotation>(^^T) && vm_node_count == 1) {
		if constexpr (count_annotated_functions<rv::node::pure, T>() == 1)
			info.meta.pin_flow = enum_type::none;
		else if constexpr (count_annotated_functions<rv::node::event, T>() == 1)
			info.meta.pin_flow = enum_type::output_only;
		else
			info.meta.pin_flow = enum_type::both;
	}

	template for (constexpr auto mem : std::define_static_array(std::meta::members_of(^^T, ctx))) {
		if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::processor>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			using ReturnT = typename mars::meta::function_pointer_info<FnPtr>::t_return;
			using ArgTuple = typename mars::meta::function_pointer_info<FnPtr>::t_args_tuple;
			using ParamTupleTraits = rv::detail::processor_param_tuple<ArgTuple>;

			static_assert(std::is_pointer_v<FnPtr>, "rv::node::processor must refer to a static function.");
			static_assert(std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::processor must refer to a static function.");
			static_assert(
				std::is_same_v<ReturnT, T> ||
				(std::is_same_v<ReturnT, bool> &&
				 std::tuple_size_v<ArgTuple> >= 1 &&
				 std::is_lvalue_reference_v<std::tuple_element_t<0, ArgTuple>> &&
				 std::is_same_v<std::remove_cvref_t<std::tuple_element_t<0, ArgTuple>>, T>),
				"rv::node::processor must either return the enclosing node type or return bool and take the enclosing node type as its first output reference parameter."
			);

			info.meta.has_processor = true;
			info.hooks.make_processor_params = []() {
				std::vector<NE_ProcessorParamValue> params;
				int param_id = 0;
				constexpr auto params_meta = std::define_static_array(std::meta::parameters_of(mem));
				template for (constexpr size_t index : std::views::iota(size_t{0}, params_meta.size())) {
					constexpr auto param = params_meta[index];
					if constexpr (index < ParamTupleTraits::skip_count)
						continue;
					constexpr auto param_type = std::meta::remove_cvref(std::meta::type_of(param));
					using ParamT = [:param_type:];
					constexpr bool is_container =
						std::meta::has_template_arguments(param_type) &&
						std::meta::template_of(param_type) == ^^std::vector;
					const char* param_label = [=] {
						if constexpr (std::meta::has_identifier(param))
							return std::define_static_string(std::meta::identifier_of(param));
						return "param";
					}();
					params.push_back(NE_ProcessorParamValue::make<ParamT>(param_id++, std::string(param_label), is_container));
				}

				return params;
			};
			info.hooks.make_runtime_value = []() {
				return NE_RuntimeValue::make<T>();
			};
			info.hooks.execute_processor = [](NE_Node& node) {
				if (node.runtime_value.storage == nullptr)
					node.runtime_value = NE_RuntimeValue::make<T>();

				FnPtr fn = &[:mem:];
				rv::detail::clear_processor_status_message();
				node.last_run_message.clear();
				node.last_run_success = false;

				T result{};
				const bool success = rv::detail::processor_executor<FnPtr, T, ArgTuple>::execute(node, fn, result);
				node.has_run_result = true;
				node.last_run_success = success;
				node.last_run_message = rv::detail::take_processor_status_message();

				if (!success) {
					if (node.last_run_message.empty())
						node.last_run_message = "Processor returned false.";
					return false;
				}

				node.runtime_value.as<T>() = std::move(result);
				if (node.last_run_message.empty())
					node.last_run_message = "Completed successfully.";
				return true;
			};

			int param_id = 0;
			constexpr auto params_meta = std::define_static_array(std::meta::parameters_of(mem));
			template for (constexpr size_t index : std::views::iota(size_t{0}, params_meta.size())) {
				constexpr auto param = params_meta[index];
				if constexpr (index < ParamTupleTraits::skip_count)
					continue;
				constexpr auto param_type = std::meta::remove_cvref(std::meta::type_of(param));
				using ParamT = [:param_type:];
				constexpr bool is_container =
					std::meta::has_template_arguments(param_type) &&
					std::meta::template_of(param_type) == ^^std::vector;
				const char* param_label = [=] {
					if constexpr (std::meta::has_identifier(param))
						return std::define_static_string(std::meta::identifier_of(param));
					return "param";
				}();
				info.hooks.processor_params.push_back({
					.id = param_id++,
					.label = std::string(param_label),
					.type_hash = rv::detail::pin_type_hash<ParamT>(),
					.is_container = is_container
				});
			}
		}
	};

	if constexpr (requires { typename T::custom_state_t; }) {
		using state_t = typename T::custom_state_t;
		info.hooks.make_custom_state = []() {
			return NE_CustomState::make<state_t>();
		};

		info.hooks.save_custom_state_json = [](const NE_Node& node) {
			return rv::detail::generic_json_stringify(node.custom_state.as<state_t>());
		};
		info.hooks.load_custom_state_json = [](NE_Node& node, std::string_view json, std::string& error) {
			state_t snapshot;
			if (!rv::detail::generic_json_parse(json, snapshot)) {
				error = "Failed to parse custom node state.";
				return false;
			}
			node.custom_state.as<state_t>() = std::move(snapshot);
			return true;
		};
	}

	template for (constexpr auto mem : std::define_static_array(std::meta::members_of(^^T, ctx))) {
		if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::configure>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr configure_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::configure must refer to a static function.");
			static_assert(std::is_invocable_r_v<void, FnPtr, NodeTypeInfo&>, "rv::node::configure must have signature void(NodeTypeInfo&).");
			configure_fn(info);
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::save_state>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr save_state_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::save_state must refer to a static function.");
			static_assert(requires { typename T::custom_state_t; }, "[[=rv::node::save_state()]] requires a custom_state_t.");
			if constexpr (requires { typename T::custom_state_t; }) {
				using state_t = typename T::custom_state_t;
				static_assert(std::is_invocable_r_v<std::string, FnPtr, const state_t&>, "rv::node::save_state must have signature std::string(const custom_state_t&).");
				info.hooks.save_custom_state_json = rv::detail::save_state_adapter<save_state_fn, state_t>;
			}
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::load_state>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr load_state_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::load_state must refer to a static function.");
			static_assert(requires { typename T::custom_state_t; }, "[[=rv::node::load_state()]] requires a custom_state_t.");
			if constexpr (requires { typename T::custom_state_t; }) {
				using state_t = typename T::custom_state_t;
				static_assert(std::is_invocable_r_v<bool, FnPtr, state_t&, std::string_view, std::string&>, "rv::node::load_state must have signature bool(custom_state_t&, std::string_view, std::string&).");
				info.hooks.load_custom_state_json = rv::detail::load_state_adapter<load_state_fn, state_t>;
			}
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::editor>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr editor_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::editor must refer to a static function.");
			if constexpr (requires { typename T::custom_state_t; }) {
				using state_t = typename T::custom_state_t;
				static_assert(std::is_invocable_r_v<void, FnPtr, NodeGraph&, NE_Node&, state_t&>,
					"rv::node::editor must have signature void(NodeGraph&, NE_Node&, custom_state_t&) for stateful nodes.");
				info.hooks.render_selected_sidebar = rv::detail::editor_adapter_with_state<editor_fn, state_t>;
			} else {
				static_assert(std::is_invocable_r_v<void, FnPtr, NodeGraph&, NE_Node&>,
					"rv::node::editor must have signature void(NodeGraph&, NE_Node&) for stateless nodes.");
				info.hooks.render_selected_sidebar = rv::detail::editor_adapter<editor_fn>;
			}
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::build>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr build_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::build must refer to a static function.");
			static_assert(std::is_invocable_r_v<bool, FnPtr, rv::graph_build_context&, NE_Node&, rv::graph_build_result&, std::string&>,
				"rv::node::build must have signature bool(graph_build_context&, NE_Node&, graph_build_result&, std::string&).");
			info.hooks.build_execute = build_fn;
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::destroy>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr destroy_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::destroy must refer to a static function.");
			static_assert(std::is_invocable_r_v<void, FnPtr, rv::graph_services&, NE_Node&>,
				"rv::node::destroy must have signature void(graph_services&, NE_Node&).");
			info.hooks.destroy_execute = destroy_fn;
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::validate>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr validate_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::validate must refer to a static function.");
			static_assert(std::is_invocable_r_v<bool, FnPtr, const NodeGraph&, const NodeTypeInfo&, const NE_Node&, std::string&>,
				"rv::node::validate must have signature bool(const NodeGraph&, const NodeTypeInfo&, const NE_Node&, std::string&).");
			info.hooks.validate_node = validate_fn;
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::refresh>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr refresh_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::refresh must refer to a static function.");
			static_assert(std::is_invocable_r_v<void, FnPtr, NodeGraph&, const NodeTypeInfo&, NE_Node&>,
				"rv::node::refresh must have signature void(NodeGraph&, const NodeTypeInfo&, NE_Node&).");
			info.hooks.refresh_dynamic_pins = refresh_fn;
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::on_connect>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr on_connect_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::on_connect must refer to a static function.");
			static_assert(std::is_invocable_r_v<void, FnPtr, NodeGraph&, const NodeTypeInfo&, NE_Node&, const NE_Link&>,
				"rv::node::on_connect must have signature void(NodeGraph&, const NodeTypeInfo&, NE_Node&, const NE_Link&).");
			info.hooks.on_connect = on_connect_fn;
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::on_tick>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr on_tick_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::on_tick must refer to a static function.");
			static_assert(std::is_invocable_r_v<void, FnPtr, NE_Node&, float>,
				"rv::node::on_tick must have signature void(NE_Node&, float delta_time).");
			info.hooks.on_tick = on_tick_fn;
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::build_propagate>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr build_propagate_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::build_propagate must refer to a static function.");
			static_assert(std::is_invocable_r_v<bool, FnPtr, rv::graph_build_context&, NE_Node&, std::string&>,
				"rv::node::build_propagate must have signature bool(graph_build_context&, NE_Node&, std::string&).");
			info.hooks.build_propagate = build_propagate_fn;
		} else if constexpr (std::meta::is_function(mem) && mars::meta::has_annotation<rv::node::get_cpu_output>(mem)) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr get_cpu_output_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "rv::node::get_cpu_output must refer to a static function.");
			static_assert(std::is_invocable_r_v<bool, FnPtr, const NE_Node&, std::string_view, std::vector<std::byte>&, size_t&>,
				"rv::node::get_cpu_output must have signature bool(const NE_Node&, std::string_view pin_label, std::vector<std::byte>&, size_t&).");
			info.hooks.get_cpu_output = get_cpu_output_fn;
		} else if constexpr (std::meta::is_function(mem) && (mars::meta::has_annotation<rv::node::pure>(mem) || mars::meta::has_annotation<rv::node::callable>(mem) || mars::meta::has_annotation<rv::node::event>(mem))) {
			using FnPtr = decltype(&[:mem:]);
			constexpr FnPtr vm_execute_fn = &[:mem:];
			static_assert(std::is_pointer_v<FnPtr> && std::is_function_v<std::remove_pointer_t<FnPtr>>, "Reflected VM execute hooks must refer to static functions.");
			static_assert(std::is_invocable_r_v<bool, FnPtr, rv::graph_execution_context&, NE_Node&, std::string&>,
				"Reflected VM execute hooks must have signature bool(graph_execution_context&, NE_Node&, std::string&).");
			info.meta.is_vm_node = true;
			info.meta.is_vm_pure = mars::meta::has_annotation<rv::node::pure>(mem);
			info.meta.is_vm_callable = mars::meta::has_annotation<rv::node::callable>(mem);
			info.meta.is_vm_event = mars::meta::has_annotation<rv::node::event>(mem);
			info.hooks.vm_execute = vm_execute_fn;
		}
	}

	register_custom_node(std::move(info));
}

template <typename T>
void NodeRegistry::register_pin_type(const char* name, bool is_numeric, bool is_resource, bool is_virtual_struct_field) {
	using traits = rv::detail::value_vector_traits<T>;
	using element_t = typename traits::element_t;
	constexpr bool is_container = traits::is_vector;

	pin_type_info info;
	info.name = name;
	info.element_size = sizeof(element_t);
	info.element_type_hash = rv::detail::pin_type_hash<element_t>();
	info.is_container = is_container;
	info.is_numeric = is_numeric;
	info.is_resource = is_resource;
	info.is_virtual_struct_field = is_virtual_struct_field;

	if (!is_resource) {
		if constexpr (!is_container) {
			info.to_json = [](const std::any& value) -> std::string {
				if (value.type() != typeid(T)) return {};
				return rv::detail::generic_json_stringify(std::any_cast<const T&>(value));
			};
			info.from_json = [](std::string_view json, std::any& value) -> bool {
				T result{};
				if (!rv::detail::generic_json_parse(json, result)) return false;
				value = std::move(result);
				return true;
			};
		}

		if constexpr (std::is_trivially_copyable_v<element_t>) {
			if constexpr (is_container) {
				info.copy_to_bytes = [](const std::any& value, std::vector<std::byte>& bytes, size_t& count) -> bool {
					if (value.type() != typeid(T)) return false;
					const auto& vec = std::any_cast<const T&>(value);
					bytes.resize(sizeof(element_t) * vec.size());
					if (!bytes.empty())
						std::memcpy(bytes.data(), vec.data(), bytes.size());
					count = vec.size();
					return true;
				};
				info.copy_from_bytes = [](const std::vector<std::byte>& bytes, size_t count, std::any& value) -> bool {
					T vec(count);
					if (!bytes.empty() && bytes.size() >= sizeof(element_t) * count)
						std::memcpy(vec.data(), bytes.data(), sizeof(element_t) * count);
					value = std::move(vec);
					return true;
				};
			} else {
				info.copy_to_bytes = [](const std::any& value, std::vector<std::byte>& bytes, size_t& count) -> bool {
					if (value.type() != typeid(T)) return false;
					bytes.resize(sizeof(T));
					std::memcpy(bytes.data(), &std::any_cast<const T&>(value), sizeof(T));
					count = 1;
					return true;
				};
				info.copy_from_bytes = [](const std::vector<std::byte>& bytes, size_t count, std::any& value) -> bool {
					if (count != 1 || bytes.size() < sizeof(T)) return false;
					T v{};
					std::memcpy(&v, bytes.data(), sizeof(T));
					value = v;
					return true;
				};
			}
		}

		info.to_wildcard_value = [](const std::any& src, const NE_WildcardTypeInfo& type_info, NE_WildcardValue& dst) -> bool {
			if (src.type() != typeid(T)) return false;
			dst = NE_WildcardValue::make(std::any_cast<T>(src));
			dst.type = type_info;
			return true;
		};
		info.from_wildcard_value = [](const NE_WildcardValue& src, std::any& dst) -> bool {
			if (src.storage == nullptr) return false;
			dst = src.as<T>();
			return true;
		};
	}

	pin_types_[rv::detail::pin_type_hash<T>()] = std::move(info);
}

inline const pin_type_info* NodeRegistry::pin_type(size_t type_hash) const {
	const auto it = pin_types_.find(type_hash);
	return it == pin_types_.end() ? nullptr : &it->second;
}

inline const pin_type_info* NodeRegistry::pin_type_for_element(size_t element_type_hash, bool is_container) const {
	for (const auto& [hash, info] : pin_types_) {
		if (info.element_type_hash == element_type_hash && info.is_container == is_container)
			return &info;
	}
	return nullptr;
}
