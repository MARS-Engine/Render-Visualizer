#pragma once

#include <mars/meta/type_erased.hpp>
#include <memory>
#include <string_view>
#include <vector>

namespace rv {

struct variable;

struct input_annotation {};
struct output_annotation {};
struct node_pure_annotation {};
struct execute_annotation {};
struct execution_pin_tag {};
struct node_hidden_annotation {};
struct pins_override_annotation {};
struct stack_annotation {};

constexpr struct input_annotation input = {};
constexpr struct output_annotation output = {};
constexpr struct execute_annotation execute = {};
consteval node_pure_annotation node_pure() { return {}; }
consteval node_hidden_annotation node_hidden() { return {}; }
constexpr struct pins_override_annotation pins_override = {};
constexpr struct stack_annotation stack = {};

using pin_value_resolve_fn = mars::meta::type_erased_ptr (*)(mars::meta::type_erased_ptr _instance);
using pin_value_copy_fn = void (*)(mars::meta::type_erased_ptr _destination, mars::meta::type_erased_ptr _source);
using pin_inspector_render_fn = bool (*)(mars::meta::type_erased_ptr _value, std::string_view _label);
using pin_json_stringify_fn = void (*)(mars::meta::type_erased_ptr _value, std::string& _out);
using pin_json_parse_fn = bool (*)(mars::meta::type_erased_ptr _value, std::string_view _json, const std::vector<std::unique_ptr<rv::variable>>& _variables);

struct pin_operations {
	pin_value_resolve_fn resolve_value = nullptr;
	pin_value_copy_fn copy_value = nullptr;
	pin_inspector_render_fn render_inspector = nullptr;
	pin_json_stringify_fn json_stringify = nullptr;
	pin_json_parse_fn json_parse = nullptr;
};

using node_instance_copy_construct_fn = void (*)(mars::meta::type_erased_ptr _destination, mars::meta::type_erased_ptr _source);
using node_instance_destroy_fn = void (*)(mars::meta::type_erased_ptr _instance);
using node_execute_invoke_fn = void (*)(mars::meta::type_erased_ptr _instance, mars::meta::type_erased_ptr* _dynamic_pins, std::size_t _dynamic_pin_count);
using node_inspect_properties_fn = bool (*)(mars::meta::type_erased_ptr _instance, const std::vector<std::unique_ptr<rv::variable>>* _variables);

struct node_operations {
	node_instance_copy_construct_fn copy_construct = nullptr;
	node_instance_destroy_fn destroy = nullptr;
	node_execute_invoke_fn execute = nullptr;
	node_inspect_properties_fn inspect_properties = nullptr;
};

} // namespace rv