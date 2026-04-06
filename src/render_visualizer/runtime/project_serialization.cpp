#include <render_visualizer/runtime/project_serialization.hpp>

#include <render_visualizer/type_reflection.hpp>
#include <render_visualizer/type_registry.hpp>

#include <mars/parser/json/json.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rv::detail {

struct json_blob {
	std::string value = {};
};

struct serialized_pin_ref {
	std::size_t node_index = 0;
	std::string pin_name = {};
};

struct serialized_pin_links {
	serialized_pin_ref source = {};
	std::vector<serialized_pin_ref> targets = {};
};

struct serialized_node {
	std::string node_key = {};
	float pos_x = 0.0f;
	float pos_y = 0.0f;
	json_blob state = {};
};

struct serialized_variable {
	std::string name = {};
	std::string type_key = {};
	json_blob value = {};
};

struct serialized_function {
	std::string name = {};
	std::vector<serialized_node> nodes = {};
	std::vector<serialized_pin_links> links = {};
};

struct serialized_project {
	std::size_t active_function_index = 0;
	std::vector<serialized_variable> variables = {};
	std::vector<serialized_function> functions = {};
};

void log_append(std::string& _log, std::string _message) {
	if (!_log.empty())
		_log += '\n';
	_log += std::move(_message);
}

serialized_variable variable_to_dto(const rv::variable& _var) {
	serialized_variable result = {
		.name = _var.name,
		.type_key = _var.type ? _var.type->type_key : ""
	};

	if (_var.type && _var.memory && _var.type->json_stringify)
		_var.type->json_stringify(_var.memory, result.value.value);

	return result;
}

std::unique_ptr<rv::variable> variable_from_dto(const serialized_variable& _serialized_variable, const rv::type_registry& _type_registry) {
	if (_serialized_variable.type_key.empty())
		return nullptr;

	const rv::variable_type_desc* type = _type_registry.find_by_key(_serialized_variable.type_key);
	if (!type)
		return nullptr;

	auto variable = std::make_unique<rv::variable>();
	variable->name = _serialized_variable.name;
	variable->set_type(type->type_hash, _type_registry);

	if (variable->memory && variable->type && variable->type->json_parse && !_serialized_variable.value.value.empty())
		variable->type->json_parse(_serialized_variable.value.value, variable->memory);

	return variable;
}

serialized_node node_to_dto(const rv::graph_builder_node& _node) {
	serialized_node result = {
		.node_key = _node.type_key,
		.pos_x = _node.position.x,
		.pos_y = _node.position.y
	};

	result.state.value += '{';

	if (_node.get_pin_draw_info) {
		std::vector<rv::pin_draw_data> inputs = {};
		std::vector<rv::pin_draw_data> outputs = {};
		_node.get_pin_draw_info(_node.instance_ptr, inputs, outputs);

		bool first = true;
		for (const rv::pin_draw_data& pin : inputs) {
			if ((pin.kind != rv::pin_kind::data && pin.kind != rv::pin_kind::property) || !pin.ops.json_stringify || !pin.ops.resolve_value)
				continue;

			if (!first)
				result.state.value += ',';

			first = false;
			result.state.value += '"';
			result.state.value += pin.name;
			result.state.value += "\":";
			pin.ops.json_stringify(pin.ops.resolve_value(_node.instance_ptr), result.state.value);
		}
	}

	result.state.value += '}';
	return result;
}

void apply_node_state(rv::graph_builder_node& _node, const serialized_node& _serialized_node, const std::vector<std::unique_ptr<rv::variable>>& _variables) {
	if (_serialized_node.state.value.empty() || !_node.get_pin_draw_info)
		return;

	std::vector<rv::pin_draw_data> inputs = {};
	std::vector<rv::pin_draw_data> outputs = {};
	_node.get_pin_draw_info(_node.instance_ptr, inputs, outputs);

	std::string_view state_view = _serialized_node.state.value;
	auto current = mars::parse::first_space<false>(state_view.begin(), state_view.end());

	if (current == state_view.end() || *current != '{')
		return;

	++current;

	while (current != state_view.end() && *current != '}') {
		current = mars::parse::first_space<false>(current, state_view.end());
		if (current == state_view.end() || *current == '}')
			break;

		std::string pin_key = {};
		const auto next = mars::parse::parse_quoted_string(current, state_view.end(), pin_key);
		if (next == current)
			break;

		current = next;
		current = mars::parse::first_space<false>(current, state_view.end());
		if (current == state_view.end() || *current != ':')
			break;

		++current;
		current = mars::parse::first_space<false>(current, state_view.end());

		const auto value_end = mars::json::skip_value(current, state_view.end());
		const std::string_view value_json(current, value_end);

		const auto pin_it = std::ranges::find_if(inputs, [&](const rv::pin_draw_data& _pin) {
			return _pin.name == pin_key;
		});

		if (pin_it != inputs.end() && pin_it->ops.json_parse && pin_it->ops.resolve_value)
			pin_it->ops.json_parse(pin_it->ops.resolve_value(_node.instance_ptr), value_json, _variables);

		current = mars::parse::first_space<false>(value_end, state_view.end());
		if (current != state_view.end() && *current == ',')
			++current;
	}
}

serialized_function function_to_dto(const rv::function_instance& _function) {
	serialized_function result = {
		.name = _function.name
	};

	std::vector<const rv::graph_builder_node*> node_ptrs = {};
	for (const rv::graph_builder_node& node : _function.graph)
		node_ptrs.push_back(&node);

	const auto node_index_find = [&](std::uint16_t _id) -> std::size_t {
		for (std::size_t i = 0; i < node_ptrs.size(); ++i)
			if (node_ptrs[i]->id == _id)
				return i;

		return static_cast<std::size_t>(-1);
	};

	for (const rv::graph_builder_node* node : node_ptrs)
		result.nodes.push_back(node_to_dto(*node));

	for (std::size_t node_index = 0; node_index < node_ptrs.size(); ++node_index) {
		for (const rv::graph_builder_pin_links& links : node_ptrs[node_index]->links) {
			if (links.targets.empty())
				continue;

			serialized_pin_links serialized_links = {
				.source = {
					.node_index = node_index,
					.pin_name = links.name
				}
			};

			for (const rv::graph_builder_pin_link_target& target : links.targets) {
				serialized_links.targets.push_back({
					.node_index = node_index_find(target.node_id),
					.pin_name = target.pin_name
				});
			}

			result.links.push_back(std::move(serialized_links));
		}
	}

	return result;
}

std::unique_ptr<rv::function_instance> function_from_dto(const serialized_function& _serialized_function, const rv::node_registry& _node_registry, const std::vector<std::unique_ptr<rv::variable>>& _variables) {
	auto function = std::make_unique<rv::function_instance>();
	function->name = _serialized_function.name;
	function->graph.clear();

	std::vector<std::optional<std::uint16_t>> node_ids(_serialized_function.nodes.size(), std::nullopt);
	auto start_it = function->graph.begin();
	if (start_it != function->graph.end())
		node_ids[0] = start_it->id;

	for (std::size_t i = 1; i < _serialized_function.nodes.size(); ++i) {
		const serialized_node& serialized_node = _serialized_function.nodes[i];
		if (serialized_node.node_key.empty())
			continue;

		const std::size_t type_hash = mars::hash::type_fingerprint_from_string(serialized_node.node_key);
		const rv::node_registry_entry* entry = type_hash ? _node_registry.find(type_hash) : nullptr;
		if (!entry || entry->type_key != serialized_node.node_key)
			continue;

		rv::graph_builder_node& node = function->graph.add(*entry, {
			serialized_node.pos_x,
			serialized_node.pos_y
		});

		apply_node_state(node, serialized_node, _variables);
		node_ids[i] = node.id;
	}

	for (const serialized_pin_links& link : _serialized_function.links) {
		if (link.source.node_index >= node_ids.size() || !node_ids[link.source.node_index].has_value())
			continue;

		const std::uint16_t from_id = *node_ids[link.source.node_index];
		for (const serialized_pin_ref& target : link.targets) {
			if (target.node_index >= node_ids.size() || !node_ids[target.node_index].has_value())
				continue;

			function->graph.add_link(from_id, link.source.pin_name, *node_ids[target.node_index], target.pin_name);
		}
	}

	function->graph.mark_runtime_dirty();
	return function;
}

} // namespace rv::detail

namespace mars::json {

template<>
struct json_type_parser<rv::detail::json_blob> : public json_type_parser_base<rv::detail::json_blob> {
	template<typename... Args>
	inline static std::string_view::iterator parse(const std::string_view& _json, rv::detail::json_blob& _value, Args&&... /*_args*/) {
		auto current = mars::parse::first_space<false>(_json.begin(), _json.end());
		const auto value_end = mars::json::skip_value(current, _json.end());

		if (current == _json.end() || value_end == _json.end())
			return current;

		_value.value.assign(current, value_end);
		return value_end;
	}

	inline static void stringify(rv::detail::json_blob& _value, std::string& _out) {
		_out += _value.value.empty() ? "null" : _value.value;
	}

	static constexpr bool string_support = true;
	static constexpr bool number_support = true;
	static constexpr bool bool_support = true;
	static constexpr bool struct_support = true;
	static constexpr bool array_support = true;
};

} // namespace mars::json

std::string rv::save_project_json(const frame_executor& _project) {
	rv::detail::serialized_project project = {
		.active_function_index = _project.active_function_index()
	};

	for (const std::unique_ptr<rv::variable>& variable : _project.global_variables())
		if (variable)
			project.variables.push_back(rv::detail::variable_to_dto(*variable));

	for (const std::unique_ptr<rv::function_instance>& function : _project.functions())
		if (function)
			project.functions.push_back(rv::detail::function_to_dto(*function));

	std::string result = {};
	mars::json::json_type_parser<rv::detail::serialized_project>::stringify(project, result);
	return result;
}

bool rv::load_project_json(frame_executor& _project, const node_registry& _node_registry, const type_registry& _type_registry, std::string_view _json, std::string& _error_message) {
	rv::detail::serialized_project project = {};
	mars::json::json_type_parser<rv::detail::serialized_project>::parse(_json, project);

	std::vector<std::unique_ptr<rv::variable>> variables = {};
	for (const rv::detail::serialized_variable& serialized_variable : project.variables) {
		auto variable = rv::detail::variable_from_dto(serialized_variable, _type_registry);
		if (variable)
			variables.push_back(std::move(variable));
	}

	std::vector<std::unique_ptr<rv::function_instance>> functions = {};
	for (const rv::detail::serialized_function& serialized_function : project.functions) {
		auto function = rv::detail::function_from_dto(serialized_function, _node_registry, variables);
		if (function)
			functions.push_back(std::move(function));
	}

	if (functions.empty()) {
		functions.push_back(std::make_unique<rv::function_instance>());
		functions.back()->name = "Main";
		rv::detail::log_append(_error_message, "project.functions: no valid functions, created default Main");
	}

	_project.stop();
	_project.global_variables() = std::move(variables);
	_project.functions() = std::move(functions);
	_project.select_function(std::min(project.active_function_index, _project.functions().size() - 1));
	return true;
}
