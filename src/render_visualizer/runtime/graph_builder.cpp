#include <render_visualizer/runtime/graph_builder.hpp>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace {

struct input_source_ref {
	const rv::graph_builder_node* node = nullptr;
	std::string_view pin_name = {};
};

struct node_index_mapping {
	std::uint16_t node_id = 0;
	std::size_t index = 0;
};

bool has_node_id(const std::vector<std::uint16_t>& _node_ids, std::uint16_t _node_id) {
	return std::ranges::find(_node_ids, _node_id) != _node_ids.end();
}

std::string node_name(const rv::graph_builder_node& _node) {
	return std::string(_node.name);
}

std::optional<std::size_t> mapped_index_find(const std::vector<node_index_mapping>& _mappings, std::uint16_t _node_id) {
	const auto mapping_it = std::ranges::find_if(_mappings, [&](const node_index_mapping& _mapping) {
		return _mapping.node_id == _node_id;
	});
	if (mapping_it == _mappings.end())
		return std::nullopt;
	return mapping_it->index;
}

bool input_source_find(const rv::graph_builder& _graph, std::uint16_t _node_id, std::string_view _pin_name, std::optional<input_source_ref>& _source, std::string& _error_message) {
	_source.reset();

	for (const rv::graph_builder_node& node : _graph) {
		for (const rv::graph_builder_pin_links& pin_links : node.links) {
			const auto target_it = std::ranges::find_if(pin_links.targets, [&](const rv::graph_builder_pin_link_target& _target) {
				return _target.node_id == _node_id && _target.pin_name == _pin_name;
			});
			if (target_it == pin_links.targets.end())
				continue;
			if (_source.has_value()) {
				_error_message = "Input pin '" + std::string(_pin_name) + "' on node '" + std::string(_graph.find_node(_node_id)->name) + "' has multiple sources";
				return false;
			}
			_source = input_source_ref {
				.node = &node,
				.pin_name = pin_links.name
			};
		}
	}

	return true;
}

bool exec_target_find(const rv::graph_builder_node& _node, std::optional<rv::graph_builder_pin_link_target>& _target, std::string& _error_message) {
	_target.reset();

	const auto pin_links_it = std::ranges::find_if(_node.links, [](const rv::graph_builder_pin_links& _pin_links) {
		return _pin_links.name == "exec_out";
	});
	if (pin_links_it == _node.links.end())
		return true;
	if (pin_links_it->targets.size() > 1) {
		_error_message = "Execution output on node '" + node_name(_node) + "' has multiple targets";
		return false;
	}
	if (!pin_links_it->targets.empty())
		_target = pin_links_it->targets.front();

	return true;
}

bool pure_dependencies_append(const rv::graph_builder& _graph, const rv::graph_builder_node& _node, std::vector<const rv::graph_builder_node*>& _ordered_steps, std::vector<std::uint16_t>& _local_visiting, std::vector<std::uint16_t>& _local_scheduled, std::string& _error_message);

bool pure_node_append(const rv::graph_builder& _graph, const rv::graph_builder_node& _node, std::vector<const rv::graph_builder_node*>& _ordered_steps, std::vector<std::uint16_t>& _local_visiting, std::vector<std::uint16_t>& _local_scheduled, std::string& _error_message) {
	if (has_node_id(_local_scheduled, _node.id))
		return true;
	if (has_node_id(_local_visiting, _node.id)) {
		_error_message = "Pure dependency cycle detected at node '" + node_name(_node) + "'";
		return false;
	}

	_local_visiting.push_back(_node.id);
	if (!pure_dependencies_append(_graph, _node, _ordered_steps, _local_visiting, _local_scheduled, _error_message))
		return false;

	_ordered_steps.push_back(&_node);
	_local_scheduled.push_back(_node.id);
	_local_visiting.pop_back();
	return true;
}

bool pure_dependencies_append(const rv::graph_builder& _graph, const rv::graph_builder_node& _node, std::vector<const rv::graph_builder_node*>& _ordered_steps, std::vector<std::uint16_t>& _local_visiting, std::vector<std::uint16_t>& _local_scheduled, std::string& _error_message) {
	std::vector<rv::pin_draw_data> inputs;
	std::vector<rv::pin_draw_data> outputs;
	rv::graph_builder::collect_pins(_node, inputs, outputs);

	for (const rv::pin_draw_data& input_pin : inputs) {
		if (input_pin.kind == rv::pin_kind::execution)
			continue;

		std::optional<input_source_ref> source = {};
		if (!input_source_find(_graph, _node.id, input_pin.name, source, _error_message))
			return false;
		if (!source.has_value())
			continue;
		if (source->node == nullptr) {
			_error_message = "Node '" + node_name(_node) + "' references a missing input source";
			return false;
		}

		const std::optional<rv::pin_draw_data> source_pin = rv::graph_builder::find_pin(*source->node, source->pin_name, true);
		if (!source_pin.has_value()) {
			_error_message = "Node '" + node_name(*source->node) + "' is missing output pin '" + std::string(source->pin_name) + "'";
			return false;
		}
		if (source_pin->kind != input_pin.kind || source_pin->type_hash != input_pin.type_hash) {
			_error_message = "Pin type mismatch between '" + std::string(source->pin_name) + "' and '" + std::string(input_pin.name) + "'";
			return false;
		}
		if (!source->node->runtime.pure)
			continue;
		if (!pure_node_append(_graph, *source->node, _ordered_steps, _local_visiting, _local_scheduled, _error_message))
			return false;
	}

	return true;
}

} // namespace

rv::graph_builder::graph_builder() {
	reset_with_start_node();
}

void rv::graph_builder::clear() {
	reset_with_start_node();
}

rv::graph_builder_node& rv::graph_builder::add(std::size_t _type_hash, std::string_view _name, pin_draw_info_fn _get_pin_draw_info, const mars::vector2<float>& _position) {
	return add(node_registry_entry {
		.type_hash = _type_hash,
		.name = _name,
		.get_pin_draw_info = _get_pin_draw_info
	}, _position);
}

rv::graph_builder_node& rv::graph_builder::add(const node_registry_entry& _entry, const mars::vector2<float>& _position) {
	const node_instance_storage instance = _entry.create_instance ? _entry.create_instance() : node_instance_storage {};
	const node_runtime_info runtime = _entry.get_runtime_info ? _entry.get_runtime_info() : node_runtime_info {};
	m_nodes.push_back({
		.id = m_next_node_id++,
		.type_hash = _entry.type_hash,
		.name = _entry.name,
		.get_pin_draw_info = _entry.get_pin_draw_info,
		.runtime = runtime,
		.instance = instance.storage,
		.instance_ptr = instance.ptr,
		.position = _position,
		.links = make_pin_links(_entry.get_pin_draw_info)
	});
	mark_runtime_dirty();
	return m_nodes.back();
}

rv::graph_builder_node* rv::graph_builder::find_node(std::uint16_t _id) {
	return const_cast<graph_builder_node*>(std::as_const(*this).find_node(_id));
}

const rv::graph_builder_node* rv::graph_builder::find_node(std::uint16_t _id) const {
	const auto node_it = std::ranges::find_if(m_nodes, [&](const graph_builder_node& _node) {
		return _node.id == _id;
	});
	return node_it == m_nodes.end() ? nullptr : &*node_it;
}

rv::graph_builder_node* rv::graph_builder::selected_node() {
	return const_cast<graph_builder_node*>(std::as_const(*this).selected_node());
}

const rv::graph_builder_node* rv::graph_builder::selected_node() const {
	const auto node_it = std::ranges::find_if(m_nodes, [&](const graph_builder_node& _node) {
		return _node.selected;
	});
	return node_it == m_nodes.end() ? nullptr : &*node_it;
}

const rv::graph_builder_node* rv::graph_builder::start_node() const {
	if (m_nodes.empty())
		return nullptr;
	return &m_nodes.front();
}

void rv::graph_builder::collect_pins(const graph_builder_node& _node, std::vector<pin_draw_data>& _inputs, std::vector<pin_draw_data>& _outputs) {
	if (_node.get_pin_draw_info == nullptr)
		return;
	_node.get_pin_draw_info(_inputs, _outputs);
}

std::optional<rv::pin_draw_data> rv::graph_builder::find_pin(const graph_builder_node& _node, std::string_view _pin_name, bool _is_output) {
	std::vector<pin_draw_data> inputs;
	std::vector<pin_draw_data> outputs;
	collect_pins(_node, inputs, outputs);

	const std::vector<pin_draw_data>& pins = _is_output ? outputs : inputs;
	const auto pin_it = std::ranges::find_if(pins, [&](const pin_draw_data& _pin) {
		return _pin.name == _pin_name;
	});
	return pin_it == pins.end() ? std::nullopt : std::optional<pin_draw_data>(*pin_it);
}

bool rv::graph_builder::add_link(std::uint16_t _from_node_id, std::string_view _from_pin_name, std::uint16_t _to_node_id, std::string_view _to_pin_name) {
	graph_builder_node* from_node = find_node(_from_node_id);
	graph_builder_node* to_node = find_node(_to_node_id);
	if (from_node == nullptr || to_node == nullptr)
		return false;

	const std::optional<pin_draw_data> from_pin = find_pin(*from_node, _from_pin_name, true);
	const std::optional<pin_draw_data> to_pin = find_pin(*to_node, _to_pin_name, false);
	if (!from_pin.has_value() || !to_pin.has_value())
		return false;
	if (from_pin->kind != to_pin->kind || from_pin->type_hash != to_pin->type_hash)
		return false;
	if (input_has_source(_to_node_id, _to_pin_name))
		return false;

	const auto pin_links_it = std::ranges::find_if(from_node->links, [&](const graph_builder_pin_links& _pin_links) {
		return _pin_links.name == _from_pin_name;
	});
	if (pin_links_it == from_node->links.end())
		return false;
	if (from_pin->kind == pin_kind::execution && !pin_links_it->targets.empty())
		return false;

	const auto existing_target_it = std::ranges::find_if(pin_links_it->targets, [&](const graph_builder_pin_link_target& _target) {
		return _target.node_id == _to_node_id && _target.pin_name == _to_pin_name;
	});
	if (existing_target_it != pin_links_it->targets.end())
		return false;

	pin_links_it->targets.push_back({
		.node_id = _to_node_id,
		.pin_name = std::string(_to_pin_name)
	});
	mark_runtime_dirty();
	return true;
}

void rv::graph_builder::mark_runtime_dirty() {
	++m_runtime_revision;
}

std::size_t rv::graph_builder::runtime_revision() const {
	return m_runtime_revision;
}

rv::graph_frame_build_result rv::graph_builder::build_frame() const {
	graph_frame_build_result result = {};
	result.source_revision = m_runtime_revision;

	const graph_builder_node* root = start_node();
	if (root == nullptr) {
		result.error_message = "Graph is missing its Start node";
		return result;
	}

	std::vector<const graph_builder_node*> ordered_steps;
	std::vector<std::uint16_t> exec_visited = {};
	std::optional<graph_builder_pin_link_target> exec_target = {};
	if (!exec_target_find(*root, exec_target, result.error_message))
		return result;

	while (exec_target.has_value()) {
		const graph_builder_node* current_node = find_node(exec_target->node_id);
		if (current_node == nullptr) {
			result.error_message = "Execution link points to a missing node";
			return result;
		}

		const std::optional<pin_draw_data> exec_input = find_pin(*current_node, exec_target->pin_name, false);
		if (!exec_input.has_value() || exec_input->kind != pin_kind::execution) {
			result.error_message = "Execution chain points to non-execution pin '" + exec_target->pin_name + "' on node '" + node_name(*current_node) + "'";
			return result;
		}
		if (current_node->runtime.pure) {
			result.error_message = "Execution chain cannot enter pure node '" + node_name(*current_node) + "'";
			return result;
		}
		if (has_node_id(exec_visited, current_node->id)) {
			result.error_message = "Execution cycle detected at node '" + node_name(*current_node) + "'";
			return result;
		}

		std::vector<std::uint16_t> local_visiting = {};
		std::vector<std::uint16_t> local_scheduled = {};
		if (!pure_dependencies_append(*this, *current_node, ordered_steps, local_visiting, local_scheduled, result.error_message))
			return result;

		ordered_steps.push_back(current_node);
		exec_visited.push_back(current_node->id);

		if (!exec_target_find(*current_node, exec_target, result.error_message))
			return result;
	}

	std::vector<node_index_mapping> stack_index_map = {};
	frame_stack_builder stack_builder = {};
	for (const graph_builder_node* node : ordered_steps) {
		if (mapped_index_find(stack_index_map, node->id).has_value())
			continue;
		if (!node->runtime.execute.valid()) {
			result.error_message = "Node '" + node_name(*node) + "' does not expose a valid execute function";
			return result;
		}
		if (node->instance_ptr.get<void>() == nullptr || node->runtime.copy_construct == nullptr || node->runtime.destroy == nullptr || node->runtime.instance_size == 0) {
			result.error_message = "Node '" + node_name(*node) + "' is missing runtime instance data";
			return result;
		}

		stack_index_map.push_back({
			.node_id = node->id,
			.index = stack_builder.types.size()
		});
		stack_builder.add({
			.size = node->runtime.instance_size,
			.alignment = node->runtime.instance_alignment,
			.name = node->name,
			.node_id = node->id,
			.type_hash = node->type_hash,
			.source_instance = node->instance_ptr.get<void>(),
			.copy_construct = node->runtime.copy_construct,
			.destroy = node->runtime.destroy
		});
	}

	result.stack = stack_builder.build();

	std::vector<node_index_mapping> last_execution_index = {};
	for (const graph_builder_node* node : ordered_steps) {
		const std::optional<std::size_t> stack_index = mapped_index_find(stack_index_map, node->id);
		if (!stack_index.has_value()) {
			result.error_message = "Node '" + node_name(*node) + "' is missing stack storage";
			return result;
		}

		frame_execution_step step = {
			.stack_index = *stack_index,
			.execute_member_index = result.execute_members.size(),
			.invoke = node->runtime.execute.invoke,
			.node_id = node->id
		};
		result.execute_members.push_back(node->runtime.execute.member_handle);

		std::vector<pin_draw_data> inputs;
		std::vector<pin_draw_data> outputs;
		collect_pins(*node, inputs, outputs);

		for (const pin_draw_data& input_pin : inputs) {
			if (input_pin.kind == pin_kind::execution)
				continue;

			std::optional<input_source_ref> source = {};
			if (!input_source_find(*this, node->id, input_pin.name, source, result.error_message))
				return result;
			if (!source.has_value())
				continue;
			if (source->node == nullptr) {
				result.error_message = "Node '" + node_name(*node) + "' references a missing source node";
				return result;
			}

			const std::optional<pin_draw_data> source_pin = find_pin(*source->node, source->pin_name, true);
			if (!source_pin.has_value()) {
				result.error_message = "Node '" + node_name(*source->node) + "' is missing output pin '" + std::string(source->pin_name) + "'";
				return result;
			}
			if (source_pin->kind != input_pin.kind || source_pin->type_hash != input_pin.type_hash) {
				result.error_message = "Pin type mismatch between '" + std::string(source->pin_name) + "' and '" + std::string(input_pin.name) + "'";
				return result;
			}
			if (input_pin.member_handle == nullptr || input_pin.resolve_value == nullptr || input_pin.copy_value == nullptr) {
				result.error_message = "Input pin '" + std::string(input_pin.name) + "' on node '" + node_name(*node) + "' is missing runtime metadata";
				return result;
			}
			if (source_pin->member_handle == nullptr || source_pin->resolve_value == nullptr) {
				result.error_message = "Output pin '" + std::string(source->pin_name) + "' on node '" + node_name(*source->node) + "' is missing runtime metadata";
				return result;
			}

			const std::optional<std::size_t> source_stack_index = mapped_index_find(stack_index_map, source->node->id);
			if (!source_stack_index.has_value()) {
				result.error_message = "Node '" + node_name(*source->node) + "' is not reachable from Start";
				return result;
			}

			const std::optional<std::size_t> source_execution_index = mapped_index_find(last_execution_index, source->node->id);
			if (!source_execution_index.has_value() || *source_execution_index >= result.steps.size()) {
				result.error_message = "Node '" + node_name(*node) + "' depends on data from node '" + node_name(*source->node) + "' before it has executed";
				return result;
			}

			step.copy_operations.push_back({
				.source_stack_index = *source_stack_index,
				.source_member_handle = source_pin->member_handle,
				.source_resolve = source_pin->resolve_value,
				.target_stack_index = *stack_index,
				.target_member_handle = input_pin.member_handle,
				.target_resolve = input_pin.resolve_value,
				.copy_value = input_pin.copy_value
			});
		}

		result.steps.push_back(std::move(step));

		const auto last_execution_it = std::ranges::find_if(last_execution_index, [&](const node_index_mapping& _mapping) {
			return _mapping.node_id == node->id;
		});
		if (last_execution_it == last_execution_index.end()) {
			last_execution_index.push_back({
				.node_id = node->id,
				.index = result.steps.size() - 1
			});
		}
		else
			last_execution_it->index = result.steps.size() - 1;
	}

	result.valid = true;
	return result;
}

void rv::graph_builder::start_node_pin_draw_info(std::vector<pin_draw_data>&, std::vector<pin_draw_data>& _outputs) {
	_outputs.push_back({
		.name = "exec_out",
		.colour = pin_reflection<execution_pin_tag>::colour,
		.type_hash = mars::hash::type_fingerprint_v<execution_pin_tag>,
		.kind = pin_kind::execution
	});
}

rv::node_runtime_info rv::graph_builder::start_node_runtime_info() {
	return {};
}

std::vector<rv::graph_builder_pin_links> rv::graph_builder::make_pin_links(pin_draw_info_fn _get_pin_draw_info) {
	if (_get_pin_draw_info == nullptr)
		return {};

	std::vector<pin_draw_data> inputs;
	std::vector<pin_draw_data> outputs;
	_get_pin_draw_info(inputs, outputs);

	std::vector<graph_builder_pin_links> result;
	result.reserve(outputs.size());
	for (const pin_draw_data& pin : outputs)
		result.push_back({
			.name = std::string(pin.name)
		});
	return result;
}

void rv::graph_builder::reset_with_start_node() {
	m_nodes.clear();
	m_next_node_id = 0;
	m_nodes.push_back({
		.id = m_next_node_id++,
		.type_hash = mars::hash::type_fingerprint_v<execution_pin_tag>,
		.name = "Start",
		.get_pin_draw_info = &start_node_pin_draw_info,
		.runtime = start_node_runtime_info(),
		.position = { 80.0f, 140.0f },
		.links = make_pin_links(&start_node_pin_draw_info)
	});
	mark_runtime_dirty();
}

bool rv::graph_builder::input_has_source(std::uint16_t _node_id, std::string_view _pin_name) const {
	for (const graph_builder_node& node : m_nodes) {
		for (const graph_builder_pin_links& pin_links : node.links) {
			const auto target_it = std::ranges::find_if(pin_links.targets, [&](const graph_builder_pin_link_target& _target) {
				return _target.node_id == _node_id && _target.pin_name == _pin_name;
			});
			if (target_it != pin_links.targets.end())
				return true;
		}
	}
	return false;
}
