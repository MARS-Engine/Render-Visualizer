#include <render_visualizer/graph_persistence.hpp>

#include <render_visualizer/nodes/all.hpp>
#include <render_visualizer/nodes/variable_support.hpp>

#include <mars/debug/logger.hpp>
#include <mars/parser/json/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rv {
namespace {

mars::log_channel g_app_log_channel("app");

struct pin_reference_snapshot {
	std::string label;
	bool is_exec = false;
};

struct saved_processor_param_snapshot {
	std::string label;
	std::string json;
};

struct loaded_processor_param_snapshot {
	int id = -1;
	std::string label;
	size_t type_hash = 0;
	bool is_container = false;
	std::string json;
};

struct saved_connection_target_snapshot {
	int node_id = -1;
	std::string input;
};

struct saved_output_connections_snapshot {
	std::string output;
	std::vector<saved_connection_target_snapshot> targets [[=mars::json::skip_empty]];
};

struct saved_node_snapshot {
	int id = -1;
	int function_id = -1;
	size_t type = 0;
	mars::vector2<float> pos = { 0.0f, 0.0f };
	std::vector<saved_processor_param_snapshot> processor_params [[=mars::json::skip_empty]];
	std::vector<NE_InlineInputValue> inline_input_values [[=mars::json::skip_empty]];
	std::string custom_state_json [[=mars::json::skip_empty]];
	std::vector<saved_output_connections_snapshot> connections [[=mars::json::skip_empty]];
};

struct loaded_node_snapshot {
	int id = -1;
	int function_id = -1;
	size_t type = 0;
	std::string title;
	mars::vector2<float> pos = { 0.0f, 0.0f };
	std::vector<loaded_processor_param_snapshot> processor_params;
	std::vector<NE_InlineInputValue> inline_input_values;
	std::string custom_state_json;
	std::vector<saved_output_connections_snapshot> connections;
};

struct loaded_link_snapshot {
	int from_node = -1;
	pin_reference_snapshot from_pin;
	int to_node = -1;
	pin_reference_snapshot to_pin;
};

struct saved_texture_slot_snapshot {
	int id = -1;
	std::string name [[=mars::json::skip_default]] = "Texture";
	std::string path;
};

struct saved_variable_slot_snapshot {
	int id = -1;
	std::string name = "Variable";
	size_t type_hash = rv::detail::pin_type_hash<float>();
	bool is_container [[=mars::json::skip_default]] = false;
	bool has_virtual_struct [[=mars::json::skip_default]] = false;
	std::string virtual_struct_name [[=mars::json::skip_empty]];
	size_t virtual_struct_layout_fingerprint [[=mars::json::skip_default]] = 0;
	size_t template_base_type_hash [[=mars::json::skip_default]] = 0;
	size_t template_value_hash [[=mars::json::skip_default]] = 0;
	std::string template_display_name [[=mars::json::skip_empty]];
};

struct saved_graph_snapshot {
	unsigned int version = 3;
	std::vector<graph_function_definition> functions;
	int active_function_id [[=mars::json::skip_default]] = -1;
	std::vector<graph_virtual_struct_schema> virtual_structs [[=mars::json::skip_empty]];
	std::vector<saved_texture_slot_snapshot> texture_slots [[=mars::json::skip_empty]];
	std::vector<saved_variable_slot_snapshot> variable_slots [[=mars::json::skip_empty]];
	std::vector<saved_node_snapshot> nodes;
};

struct loaded_graph_snapshot {
	unsigned int version = 2;
	std::vector<graph_function_definition> functions;
	int active_function_id = -1;
	std::vector<graph_virtual_struct_schema> virtual_structs;
	std::vector<graph_texture_slot> texture_slots;
	std::vector<graph_variable_slot> variable_slots;
	std::vector<loaded_node_snapshot> nodes;
	std::vector<loaded_link_snapshot> links;
};

struct resolved_link_snapshot {
	int from_node = -1;
	std::string output;
	int to_node = -1;
	std::string input;
};

template <typename T>
bool save_param_json(const NE_ProcessorParamValue& param, std::string& json) {
	T value = param.as<T>();
	json = nodes::json_stringify(value);
	return true;
}

template <typename T>
bool load_param_json(NE_ProcessorParamValue& param, std::string_view json) {
	T value {};
	if (!nodes::json_parse(json, value))
		return false;
	param.as<T>() = std::move(value);
	return true;
}

using serializable_processor_param_types = std::tuple<
	nodes::file,
	float,
	mars::vector2<float>,
	mars::vector3<float>,
	mars::vector4<float>,
	unsigned int,
	mars::vector2<unsigned int>,
	mars::vector3<unsigned int>,
	mars::vector4<unsigned int>,
	bool,
	std::string
>;

template <typename Param, typename Fn, typename Tuple = serializable_processor_param_types>
bool dispatch_processor_param(Param& param, Fn&& fn) {
	if (param.is_container)
		return false;

	return rv::detail::dispatch_type_hash<Tuple>(param.type_hash, [&]<typename T>() {
		fn.template operator()<T>(param);
	});
}

bool serialize_processor_param(const NE_ProcessorParamValue& param, std::string& json) {
	bool success = false;
	dispatch_processor_param(param, [&]<typename T>(const auto& current_param) {
		success = save_param_json<T>(current_param, json);
	});
	return success;
}

bool deserialize_processor_param(NE_ProcessorParamValue& param, std::string_view json) {
	bool success = false;
	dispatch_processor_param(param, [&]<typename T>(auto& current_param) {
		success = load_param_json<T>(current_param, json);
	});
	return success;
}

pin_reference_snapshot make_output_pin_reference(const NodeGraph& graph, const NE_Link& link) {
	pin_reference_snapshot reference;
	if (const NE_Node* node = graph.find_node(link.from_node); node != nullptr) {
		if (const NE_Pin* exec_pin = nodes::find_pin_by_id(node->exec_outputs, link.from_pin); exec_pin != nullptr) {
			reference.label = exec_pin->label;
			reference.is_exec = true;
		} else if (const NE_Pin* pin = nodes::find_pin_by_id(node->outputs, link.from_pin); pin != nullptr) {
			reference.label = pin->label;
			reference.is_exec = false;
		}
	}
	return reference;
}

pin_reference_snapshot make_input_pin_reference(const NodeGraph& graph, const NE_Link& link) {
	pin_reference_snapshot reference;
	if (const NE_Node* node = graph.find_node(link.to_node); node != nullptr) {
		if (node->has_exec_input && node->exec_input.id == link.to_pin) {
			reference.label = node->exec_input.label;
			reference.is_exec = true;
		} else if (const NE_Pin* pin = nodes::find_pin_by_id(node->inputs, link.to_pin); pin != nullptr) {
			reference.label = pin->label;
			reference.is_exec = false;
		}
	}
	return reference;
}

bool read_file(std::string_view path, std::string& contents) {
	std::ifstream in(std::filesystem::path(path), std::ios::in | std::ios::binary);
	if (!in)
		return false;
	std::ostringstream stream;
	stream << in.rdbuf();
	contents = stream.str();
	return true;
}

bool write_file(std::string_view path, const std::string& contents) {
	std::ofstream out(std::filesystem::path(path), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out << contents;
	return static_cast<bool>(out);
}

saved_texture_slot_snapshot build_saved_texture_slot(const graph_texture_slot& slot) {
	return {
		.id = slot.id,
		.name = slot.name,
		.path = slot.path,
	};
}

saved_variable_slot_snapshot build_saved_variable_slot(const graph_variable_slot& slot) {
	return {
		.id = slot.id,
		.name = slot.name,
		.type_hash = slot.type_hash,
		.is_container = slot.is_container,
		.has_virtual_struct = slot.has_virtual_struct,
		.virtual_struct_name = slot.virtual_struct_name,
		.virtual_struct_layout_fingerprint = slot.virtual_struct_layout_fingerprint,
		.template_base_type_hash = slot.template_base_type_hash,
		.template_value_hash = slot.template_value_hash,
		.template_display_name = slot.template_display_name,
	};
}

graph_variable_slot restore_saved_variable_slot(const saved_variable_slot_snapshot& saved_slot) {
	graph_variable_slot slot = nodes::make_default_variable_slot(saved_slot.name);
	slot.id = saved_slot.id;
	slot.type_hash = saved_slot.type_hash;
	slot.is_container = saved_slot.is_container;
	slot.has_virtual_struct = saved_slot.has_virtual_struct;
	slot.virtual_struct_name = saved_slot.virtual_struct_name;
	slot.virtual_struct_layout_fingerprint = saved_slot.virtual_struct_layout_fingerprint;
	slot.template_base_type_hash = saved_slot.template_base_type_hash;
	slot.template_value_hash = saved_slot.template_value_hash;
	slot.template_display_name = saved_slot.template_display_name;
	nodes::reset_variable_slot_default(slot);
	return slot;
}

bool parse_graph_snapshot(std::string_view json, loaded_graph_snapshot& snapshot) {
	if (json.empty())
		return false;
	std::string_view::iterator it = mars::json::json_type_parser<loaded_graph_snapshot>::parse(json, snapshot);
	if (it == json.begin())
		return false;
	it = mars::parse::first_space<false>(it, json.end());
	return it == json.end();
}

saved_graph_snapshot build_snapshot(const NodeGraph& graph, std::string& error) {
	saved_graph_snapshot snapshot;
	snapshot.functions = graph.functions;
	snapshot.active_function_id = graph.active_function_id();
	snapshot.virtual_structs = graph.virtual_structs;
	for (const auto& slot : graph.texture_slots)
		snapshot.texture_slots.push_back(build_saved_texture_slot(slot));
	for (const auto& slot : graph.variable_slots)
		snapshot.variable_slots.push_back(build_saved_variable_slot(slot));

	snapshot.nodes.reserve(graph.nodes.size());
	std::unordered_map<int, size_t> saved_node_indices_by_id;
	saved_node_indices_by_id.reserve(graph.nodes.size());

	for (const auto& node : graph.nodes) {
		saved_node_snapshot saved_node;
		saved_node.id = node.id;
		saved_node.function_id = node.function_id;
		saved_node.type = node.type;
		saved_node.pos = node.pos;
		saved_node.inline_input_values = node.inline_input_values;

		for (const auto& param : node.processor_params) {
			saved_processor_param_snapshot saved_param;
			saved_param.label = param.label;
			if (!serialize_processor_param(param, saved_param.json)) {
				error = "Unsupported processor parameter type for node '" + node.title + "' and parameter '" + param.label + "'.";
				return {};
			}
			saved_node.processor_params.push_back(std::move(saved_param));
		}

		if (const NodeRegistry* registry = graph.node_registry(); registry != nullptr) {
			if (const NodeTypeInfo* info = registry->find(node.type); info != nullptr && info->hooks.save_custom_state_json && node.custom_state.storage != nullptr)
				saved_node.custom_state_json = info->hooks.save_custom_state_json(node);
		}

		snapshot.nodes.push_back(std::move(saved_node));
		saved_node_indices_by_id.emplace(node.id, snapshot.nodes.size() - 1);
	}

	for (const auto& link : graph.links) {
		auto saved_node_it = saved_node_indices_by_id.find(link.from_node);
		if (saved_node_it == saved_node_indices_by_id.end())
			continue;

		const pin_reference_snapshot output_pin = make_output_pin_reference(graph, link);
		const pin_reference_snapshot input_pin = make_input_pin_reference(graph, link);
		if (output_pin.label.empty() || input_pin.label.empty())
			continue;

		saved_node_snapshot& saved_node = snapshot.nodes[saved_node_it->second];
		auto connections_it = std::find_if(saved_node.connections.begin(), saved_node.connections.end(), [&](const saved_output_connections_snapshot& connection) {
			return connection.output == output_pin.label;
		});
		if (connections_it == saved_node.connections.end()) {
			saved_output_connections_snapshot connections;
			connections.output = output_pin.label;
			saved_node.connections.push_back(std::move(connections));
			connections_it = std::prev(saved_node.connections.end());
		}

		connections_it->targets.push_back({
			.node_id = link.to_node,
			.input = input_pin.label,
		});
	}

	return snapshot;
}

const NE_Pin* resolve_saved_output_pin_reference(const NE_Node& node, std::string_view label) {
	if (const NE_Pin* pin = nodes::find_pin_by_label(node.outputs, label); pin != nullptr)
		return pin;
	return nodes::find_pin_by_label(node.exec_outputs, label);
}

const NE_Pin* resolve_saved_input_pin_reference(const NE_Node& node, std::string_view label) {
	if (const NE_Pin* pin = nodes::find_pin_by_label(node.inputs, label); pin != nullptr)
		return pin;
	return node.has_exec_input && node.exec_input.label == label ? &node.exec_input : nullptr;
}

void prune_stale_inline_input_values(NodeGraph& graph) {
	for (auto& node : graph.nodes) {
		std::unordered_set<std::string> seen_labels;
		node.inline_input_values.erase(std::remove_if(node.inline_input_values.begin(), node.inline_input_values.end(), [&](const NE_InlineInputValue& value) {
			const NE_Pin* pin = nodes::find_pin_by_label(node.inputs, value.label);
			if (pin == nullptr)
				return true;
			if (!seen_labels.insert(value.label).second)
				return true;
			return false;
		}), node.inline_input_values.end());
	}
}

const NE_Pin* resolve_live_output_pin(const NE_Node& node, int pin_id) {
	if (const NE_Pin* pin = nodes::find_pin_by_id(node.exec_outputs, pin_id); pin != nullptr)
		return pin;
	return nodes::find_pin_by_id(node.outputs, pin_id);
}

const NE_Pin* resolve_live_input_pin(const NE_Node& node, int pin_id) {
	if (node.has_exec_input && node.exec_input.id == pin_id)
		return &node.exec_input;
	return nodes::find_pin_by_id(node.inputs, pin_id);
}

void prune_invalid_links(NodeGraph& graph) {
	for (auto it = graph.links.begin(); it != graph.links.end();) {
		const NE_Node* from_node = graph.find_node(it->from_node);
		const NE_Node* to_node = graph.find_node(it->to_node);
		const NE_Pin* from_pin = from_node == nullptr ? nullptr : resolve_live_output_pin(*from_node, it->from_pin);
		const NE_Pin* to_pin = to_node == nullptr ? nullptr : resolve_live_input_pin(*to_node, it->to_pin);

		const bool drop =
			from_node == nullptr ||
			to_node == nullptr ||
			from_pin == nullptr ||
			to_pin == nullptr ||
			from_pin->kind != to_pin->kind;

		if (drop) {
			if (graph.on_link_removed)
				graph.on_link_removed(*it);
			it = graph.links.erase(it);
		} else {
			++it;
		}
	}
}

bool restore_snapshot(NodeGraph& graph, const loaded_graph_snapshot& snapshot, std::string& error) {
	const NodeRegistry* registry = graph.node_registry();
	if (registry == nullptr) {
		error = "Graph has no registry.";
		return false;
	}
	if (snapshot.version < 2) {
		error = "This graph snapshot uses the old flat-graph format and cannot be loaded by the function-graph runtime.";
		return false;
	}
	if (snapshot.functions.empty()) {
		error = "Graph snapshot does not contain any functions.";
		return false;
	}

	graph.clear();
	graph.functions = snapshot.functions;
	graph.sync_function_metadata();
	graph.set_active_function(snapshot.active_function_id);

	for (const auto& schema : snapshot.virtual_structs)
		graph.create_virtual_struct(schema, schema.id);
	for (const auto& slot : snapshot.texture_slots)
		graph.create_texture_slot(slot, slot.id);
	const auto& variable_slots = snapshot.variable_slots;
	for (const auto& variable : variable_slots) {
		if (snapshot.version >= 3) {
			graph.create_variable_slot(restore_saved_variable_slot({
				.id = variable.id,
				.name = variable.name,
				.type_hash = variable.type_hash,
				.is_container = variable.is_container,
				.has_virtual_struct = variable.has_virtual_struct,
				.virtual_struct_name = variable.virtual_struct_name,
				.virtual_struct_layout_fingerprint = variable.virtual_struct_layout_fingerprint,
				.template_base_type_hash = variable.template_base_type_hash,
				.template_value_hash = variable.template_value_hash,
				.template_display_name = variable.template_display_name,
			}), variable.id);
		} else {
			graph.create_variable_slot(variable, variable.id);
		}
	}

	std::unordered_set<int> skipped_node_ids;

	for (const auto& saved_node : snapshot.nodes) {
		if (registry->find(saved_node.type) == nullptr) {
			skipped_node_ids.insert(saved_node.id);
			mars::logger::warning(g_app_log_channel, "Skipping unknown saved node type id {} for node {}", saved_node.type, saved_node.id);
			continue;
		}
		NE_Node* node = graph.spawn_node(saved_node.type, saved_node.pos, saved_node.id, saved_node.function_id);
		if (node == nullptr) {
			skipped_node_ids.insert(saved_node.id);
			mars::logger::warning(g_app_log_channel, "Skipping node {} because type id {} could not be recreated", saved_node.id, saved_node.type);
			continue;
		}
		if (!saved_node.title.empty())
			node->title = saved_node.title;
	}

	for (const auto& saved_node : snapshot.nodes) {
		if (skipped_node_ids.contains(saved_node.id))
			continue;
		NE_Node* node = graph.find_node(saved_node.id);
		if (node == nullptr)
			continue;

		node->inline_input_values = saved_node.inline_input_values;

			std::ranges::sort(node->processor_params, {}, &NE_ProcessorParamValue::id);
			for (const auto& saved_param : saved_node.processor_params) {
				auto it = std::ranges::find(node->processor_params, saved_param.label, &NE_ProcessorParamValue::label);
				if (it == node->processor_params.end() && saved_param.id >= 0)
					it = std::ranges::find(node->processor_params, saved_param.id, &NE_ProcessorParamValue::id);
				if (it == node->processor_params.end()) {
					mars::logger::warning(g_app_log_channel, "Skipping missing saved processor parameter '{}' on node '{}'", saved_param.label, node->title);
					continue;
				}
				if (saved_param.type_hash != 0 && (it->type_hash != saved_param.type_hash || it->is_container != saved_param.is_container)) {
					mars::logger::warning(g_app_log_channel, "Skipping mismatched processor parameter '{}' on node '{}'", saved_param.label, node->title);
					continue;
				}
			if (!deserialize_processor_param(*it, saved_param.json)) {
				mars::logger::warning(g_app_log_channel, "Skipping processor parameter '{}' on node '{}' because it could not be restored", saved_param.label, node->title);
				continue;
			}
		}

		if (!saved_node.custom_state_json.empty()) {
			const NodeTypeInfo* info = registry->find(node->type);
			if (info != nullptr && info->hooks.load_custom_state_json) {
				if (!info->hooks.load_custom_state_json(*node, saved_node.custom_state_json, error)) {
					mars::logger::warning(g_app_log_channel, "Skipping custom state restore for node '{}': {}", node->title, error);
					error.clear();
				}
			}
		}
	}

	nodes::refresh_dynamic_nodes(graph);

	std::vector<resolved_link_snapshot> resolved_links;
	if (snapshot.version >= 3) {
		for (const auto& saved_node : snapshot.nodes) {
			for (const auto& connection_group : saved_node.connections) {
				for (const auto& target : connection_group.targets) {
					resolved_links.push_back({
						.from_node = saved_node.id,
						.output = connection_group.output,
						.to_node = target.node_id,
						.input = target.input,
					});
				}
			}
		}
	} else {
		resolved_links.reserve(snapshot.links.size());
		for (const auto& saved_link : snapshot.links) {
			resolved_links.push_back({
				.from_node = saved_link.from_node,
				.output = saved_link.from_pin.label,
				.to_node = saved_link.to_node,
				.input = saved_link.to_pin.label,
			});
		}
	}

	std::vector<const resolved_link_snapshot*> pending_links;
	pending_links.reserve(resolved_links.size());
	for (const auto& saved_link : resolved_links)
		pending_links.push_back(&saved_link);

	for (size_t pass = 0; pass < resolved_links.size() && !pending_links.empty(); ++pass) {
		std::vector<const resolved_link_snapshot*> next_pending_links;
		next_pending_links.reserve(pending_links.size());
		bool added_any_link = false;

		for (const resolved_link_snapshot* saved_link : pending_links) {
			const NE_Node* from_node = graph.find_node(saved_link->from_node);
			const NE_Node* to_node = graph.find_node(saved_link->to_node);
			if (from_node == nullptr || to_node == nullptr) {
				mars::logger::warning(g_app_log_channel, "Skipping saved link with missing endpoint nodes ({} -> {})", saved_link->from_node, saved_link->to_node);
				continue;
			}

			const NE_Pin* from_pin = resolve_saved_output_pin_reference(*from_node, saved_link->output);
			const NE_Pin* to_pin = resolve_saved_input_pin_reference(*to_node, saved_link->input);
			if (from_pin == nullptr || to_pin == nullptr) {
				next_pending_links.push_back(saved_link);
				continue;
			}

			if (!graph.add_link({ .from_node = from_node->id, .from_pin = from_pin->id, .to_node = to_node->id, .to_pin = to_pin->id })) {
				mars::logger::warning(g_app_log_channel, "Skipping invalid saved link '{}:{}' -> '{}:{}'",
					from_node->title, saved_link->output, to_node->title, saved_link->input);
				continue;
			}

			added_any_link = true;
		}

		if (!next_pending_links.empty())
			nodes::refresh_dynamic_nodes(graph);

		if (!added_any_link && next_pending_links.size() == pending_links.size()) {
			for (const resolved_link_snapshot* unresolved_link : next_pending_links) {
				const NE_Node* from_node = graph.find_node(unresolved_link->from_node);
				const NE_Node* to_node = graph.find_node(unresolved_link->to_node);
				mars::logger::warning(g_app_log_channel, "Skipping saved link '{}:{}' -> '{}:{}' because pins could not be resolved",
					from_node != nullptr ? from_node->title : std::string("<missing>"),
					unresolved_link->output,
					to_node != nullptr ? to_node->title : std::string("<missing>"),
					unresolved_link->input);
			}
			break;
		}

		pending_links = std::move(next_pending_links);
	}

	graph.notify_graph_dirty();
	prune_stale_inline_input_values(graph);
	prune_invalid_links(graph);
	nodes::refresh_dynamic_nodes(graph);
	prune_stale_inline_input_values(graph);
	return true;
}

} // namespace

bool save_graph_to_file(const NodeGraph& graph, std::string_view path, std::string* error) {
	std::string local_error;
	saved_graph_snapshot snapshot = build_snapshot(graph, local_error);
	if (!local_error.empty()) {
		if (error != nullptr)
			*error = local_error;
		return false;
	}

	std::string json = nodes::json_stringify(snapshot);
	if (!write_file(path, json)) {
		if (error != nullptr)
			*error = "Failed to write graph snapshot to '" + std::string(path) + "'.";
		return false;
	}
	return true;
}

bool load_graph_from_file(NodeGraph& graph, std::string_view path, std::string* error) {
	std::string contents;
	if (!read_file(path, contents)) {
		if (error != nullptr)
			*error = "Failed to read graph snapshot from '" + std::string(path) + "'.";
		return false;
	}

	loaded_graph_snapshot snapshot;
	if (!parse_graph_snapshot(contents, snapshot)) {
		if (error != nullptr)
			*error = "Failed to parse graph snapshot from '" + std::string(path) + "'.";
		return false;
	}
	if (snapshot.version < 2) {
		if (error != nullptr)
			*error = "This saved graph uses the old flat-graph format and cannot be loaded automatically.";
		return false;
	}

	const NodeRegistry* registry = graph.node_registry();
	if (registry == nullptr) {
		if (error != nullptr)
			*error = "Graph has no registry.";
		return false;
	}

	NodeGraph restored_graph(*registry);
	std::string local_error;
	if (!restore_snapshot(restored_graph, snapshot, local_error)) {
		if (error != nullptr)
			*error = std::move(local_error);
		return false;
	}

	graph = std::move(restored_graph);
	return true;
}

} // namespace rv
