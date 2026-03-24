#include <render_visualizer/node_graph.hpp>

NodeRegistry::NodeRegistry() {
	import_registration_catalog();
}

void NodeRegistry::register_custom_node(NodeTypeInfo info) {
	normalize_type_pins(info);
	if (!contains(info.meta.type))
		type_order.push_back(info.meta.type);
	types[info.meta.type] = std::move(info);
}

const NodeTypeInfo* NodeRegistry::find(size_t type) const {
	auto it = types.find(type);
	return it == types.end() ? nullptr : &it->second;
}

bool NodeRegistry::contains(size_t type) const {
	return find(type) != nullptr;
}

const NodeTypeInfo* NodeRegistry::find(std::string_view title) const {
	for (size_t type : type_order) {
		const NodeTypeInfo* info = find(type);
		if (info != nullptr && info->meta.title == title)
			return info;
	}
	return nullptr;
}

const std::vector<size_t>& NodeRegistry::registered_types() const {
	return type_order;
}

mars::vector4<float> NodeRegistry::pin_color(size_t type_hash) const {
	auto it = pin_colors.find(type_hash);
	if (it == pin_colors.end())
		return {0.5f, 0.5f, 0.5f, 1.0f};
	return it->second;
}

NodeRegistry::registration_catalog& NodeRegistry::global_registration_catalog() {
	static registration_catalog catalog;
	return catalog;
}

void NodeRegistry::add_registration_descriptor(registration_descriptor descriptor) {
	auto& catalog = global_registration_catalog();
	if (catalog.descriptor_indices.contains(descriptor.type)) {
		std::fprintf(stderr, "Duplicate node registration detected for type id %zu\n", descriptor.type);
		std::abort();
	}
	catalog.descriptor_indices.emplace(descriptor.type, catalog.descriptors.size());
	catalog.descriptors.push_back(std::move(descriptor));
}

void NodeRegistry::import_registration_catalog() {
	const auto& catalog = global_registration_catalog();
	for (const auto& descriptor : catalog.descriptors) {
		if (contains(descriptor.type))
			continue;
		descriptor.apply(*this);
	}
}

void NodeRegistry::normalize_type_pins(NodeTypeInfo& info) {
	int next_id = 0;
	for (auto& pin : info.pins.inputs) {
		pin.generated = false;
		pin.kind = NE_PinKind::data;
		if (pin.id < 0)
			pin.id = next_id;
		next_id = std::max(next_id, pin.id + 1);
	}
	for (auto& pin : info.pins.outputs) {
		pin.generated = false;
		pin.kind = NE_PinKind::data;
		if (pin.id < 0)
			pin.id = next_id;
		next_id = std::max(next_id, pin.id + 1);
	}

	info.pins.has_exec_input = info.meta.pin_flow == enum_type::both || info.meta.pin_flow == enum_type::input_only;
	info.pins.has_exec_output = info.meta.pin_flow == enum_type::both || info.meta.pin_flow == enum_type::output_only;
	if (info.pins.has_exec_input) {
		info.pins.exec_input.generated = false;
		info.pins.exec_input.kind = NE_PinKind::exec;
		info.pins.exec_input.label = "Exec";
		if (info.pins.exec_input.id < 0)
			info.pins.exec_input.id = next_id;
		next_id = std::max(next_id, info.pins.exec_input.id + 1);
	} else {
		info.pins.exec_input = {};
	}

	if (info.pins.has_exec_output) {
		if (info.pins.exec_outputs.empty())
			info.pins.exec_outputs.push_back(info.pins.exec_output);
		for (auto& pin : info.pins.exec_outputs) {
			pin.generated = false;
			pin.kind = NE_PinKind::exec;
			if (pin.label.empty())
				pin.label = "Exec";
			if (pin.id < 0)
				pin.id = next_id;
			next_id = std::max(next_id, pin.id + 1);
		}
		info.pins.exec_output = info.pins.exec_outputs.front();
	} else {
		info.pins.exec_output = {};
		info.pins.exec_outputs.clear();
	}
}

NodeGraph::NodeGraph(const NodeRegistry& registry)
	: registry_(&registry) {
	ensure_builtin_functions();
}

NE_Node* NodeGraph::spawn_node(size_t type, mars::vector2<float> pos) {
	return spawn_node(type, pos, -1, active_function_id_);
}

NE_Node* NodeGraph::spawn_node(size_t type, mars::vector2<float> pos, int forced_id) {
	return spawn_node(type, pos, forced_id, active_function_id_);
}

NE_Node* NodeGraph::spawn_node(size_t type, mars::vector2<float> pos, int forced_id, int function_id) {
	if (registry_ == nullptr)
		return nullptr;
	const NodeTypeInfo* info = registry_->find(type);
	if (info == nullptr)
		return nullptr;
	if (find_function(resolved_function_id(function_id)) == nullptr)
		return nullptr;

	NE_Node node;
	node.id = forced_id >= 0 ? forced_id : next_node_id_++;
	node.function_id = resolved_function_id(function_id);
	node.type = info->meta.type;
	node.title = info->meta.title;
	node.pin_flow = info->meta.pin_flow;
	node.has_exec_input = info->pins.has_exec_input;
	node.has_exec_output = info->pins.has_exec_output;
	node.exec_input = info->pins.exec_input;
	node.exec_output = info->pins.exec_output;
	node.exec_outputs = info->pins.exec_outputs;
	node.is_permanent = info->meta.is_permanent;
	node.pos = pos;
	node.static_inputs = info->pins.inputs;
	node.static_outputs = info->pins.outputs;
	if (info->hooks.make_processor_params)
		node.processor_params = info->hooks.make_processor_params();
	if (info->hooks.make_runtime_value)
		node.runtime_value = info->hooks.make_runtime_value();
	if (info->hooks.make_custom_state)
		node.custom_state = info->hooks.make_custom_state();
	rebuild_visible_pins(node);
	nodes.push_back(std::move(node));
	next_node_id_ = std::max(next_node_id_, nodes.back().id + 1);
	if (info->hooks.refresh_dynamic_pins)
		info->hooks.refresh_dynamic_pins(*this, *info, nodes.back());

	if (on_node_spawned)
		on_node_spawned(nodes.back());
	notify_graph_dirty();
	return &nodes.back();
}

NE_Node* NodeGraph::spawn_node(std::string_view title, mars::vector2<float> pos) {
	return spawn_node(title, pos, -1, active_function_id_);
}

NE_Node* NodeGraph::spawn_node(std::string_view title, mars::vector2<float> pos, int forced_id) {
	return spawn_node(title, pos, forced_id, active_function_id_);
}

NE_Node* NodeGraph::spawn_node(std::string_view title, mars::vector2<float> pos, int forced_id, int function_id) {
	if (registry_ == nullptr)
		return nullptr;
	const NodeTypeInfo* info = registry_->find(title);
	if (info == nullptr)
		return nullptr;
	return spawn_node(info->meta.type, pos, forced_id, function_id);
}

NE_Node* NodeGraph::find_node(int node_id) {
	for (auto& node : nodes)
		if (node.id == node_id)
			return &node;
	return nullptr;
}

const NE_Node* NodeGraph::find_node(int node_id) const {
	for (const auto& node : nodes)
		if (node.id == node_id)
			return &node;
	return nullptr;
}

bool NodeGraph::add_link(NE_Link lnk) {
	NE_Node* from_node = find_node(lnk.from_node);
	NE_Node* to_node = find_node(lnk.to_node);
	if (from_node == nullptr || to_node == nullptr)
		return false;
	if (from_node->function_id != to_node->function_id)
		return false;

	const NodeTypeInfo* from_info = registry_ ? registry_->find(from_node->type) : nullptr;
	const NodeTypeInfo* to_info = registry_ ? registry_->find(to_node->type) : nullptr;
	const NE_Pin* from_pin = find_output_pin(*from_node, lnk.from_pin);
	const NE_Pin* to_pin = find_input_pin(*to_node, lnk.to_pin);
	if (from_pin == nullptr || to_pin == nullptr)
		return false;
	const std::string from_label = from_pin->label;
	const std::string to_label = to_pin->label;

	if (from_pin->kind != to_pin->kind)
		return false;

	if (contains_link(lnk))
		return false;

	if (from_pin->kind == NE_PinKind::exec && std::any_of(links.begin(), links.end(), [&](const NE_Link& link) {
		return link.from_node == lnk.from_node && link.from_pin == lnk.from_pin;
	})) {
		return false;
	}
	if (to_pin->kind == NE_PinKind::exec && std::any_of(links.begin(), links.end(), [&](const NE_Link& link) {
		return link.to_node == lnk.to_node && link.to_pin == lnk.to_pin;
	})) {
		return false;
	}

	if (from_pin->kind == NE_PinKind::data &&
		!is_data_link_compatible(*from_pin, *to_pin) &&
		!from_pin->is_wildcard &&
		!to_pin->is_wildcard) {
		return false;
	}

	const auto from_generated_inputs_snapshot = from_node->generated_inputs;
	const auto from_generated_outputs_snapshot = from_node->generated_outputs;
	const auto from_inputs_snapshot = from_node->inputs;
	const auto from_outputs_snapshot = from_node->outputs;
	const bool same_node = from_node->id == to_node->id;
	std::vector<NE_Pin> to_generated_inputs_snapshot;
	std::vector<NE_Pin> to_generated_outputs_snapshot;
	std::vector<NE_Pin> to_inputs_snapshot;
	std::vector<NE_Pin> to_outputs_snapshot;
	if (!same_node) {
		to_generated_inputs_snapshot = to_node->generated_inputs;
		to_generated_outputs_snapshot = to_node->generated_outputs;
		to_inputs_snapshot = to_node->inputs;
		to_outputs_snapshot = to_node->outputs;
	}
	const auto links_snapshot = links;
	links.push_back(lnk);

	if (from_info != nullptr && from_info->hooks.on_connect)
		from_info->hooks.on_connect(*this, *from_info, *from_node, lnk);
	if (!same_node && to_info != nullptr && to_info->hooks.on_connect)
		to_info->hooks.on_connect(*this, *to_info, *to_node, lnk);
	if (from_info != nullptr && from_info->hooks.refresh_dynamic_pins)
		from_info->hooks.refresh_dynamic_pins(*this, *from_info, *from_node);
	if (!same_node && to_info != nullptr && to_info->hooks.refresh_dynamic_pins)
		to_info->hooks.refresh_dynamic_pins(*this, *to_info, *to_node);
	if (same_node && to_info != nullptr && to_info->hooks.refresh_dynamic_pins && to_info != from_info)
		to_info->hooks.refresh_dynamic_pins(*this, *to_info, *to_node);

	NE_Link* resolved_link = find_link_by_labels(lnk.from_node, from_label, lnk.to_node, to_label);
	if (resolved_link == nullptr) {
		links = links_snapshot;
		from_node->generated_inputs = from_generated_inputs_snapshot;
		from_node->generated_outputs = from_generated_outputs_snapshot;
		from_node->inputs = from_inputs_snapshot;
		from_node->outputs = from_outputs_snapshot;
		if (!same_node) {
			to_node->generated_inputs = to_generated_inputs_snapshot;
			to_node->generated_outputs = to_generated_outputs_snapshot;
			to_node->inputs = to_inputs_snapshot;
			to_node->outputs = to_outputs_snapshot;
		}
		return false;
	}

	from_pin = find_output_pin(*from_node, resolved_link->from_pin);
	to_pin = find_input_pin(*to_node, resolved_link->to_pin);
	if (from_pin == nullptr || to_pin == nullptr ||
		(from_pin->kind == NE_PinKind::data && !is_data_link_compatible(*from_pin, *to_pin))) {
		links = links_snapshot;
		from_node->generated_inputs = from_generated_inputs_snapshot;
		from_node->generated_outputs = from_generated_outputs_snapshot;
		from_node->inputs = from_inputs_snapshot;
		from_node->outputs = from_outputs_snapshot;
		if (!same_node) {
			to_node->generated_inputs = to_generated_inputs_snapshot;
			to_node->generated_outputs = to_generated_outputs_snapshot;
			to_node->inputs = to_inputs_snapshot;
			to_node->outputs = to_outputs_snapshot;
		}
		return false;
	}

	std::string validation_error;
	if (from_info != nullptr && from_info->hooks.validate_node &&
		!from_info->hooks.validate_node(*this, *from_info, *from_node, validation_error)) {
		links = links_snapshot;
		from_node->generated_inputs = from_generated_inputs_snapshot;
		from_node->generated_outputs = from_generated_outputs_snapshot;
		from_node->inputs = from_inputs_snapshot;
		from_node->outputs = from_outputs_snapshot;
		if (!same_node) {
			to_node->generated_inputs = to_generated_inputs_snapshot;
			to_node->generated_outputs = to_generated_outputs_snapshot;
			to_node->inputs = to_inputs_snapshot;
			to_node->outputs = to_outputs_snapshot;
		}
		return false;
	}
	if (!same_node && to_info != nullptr && to_info->hooks.validate_node &&
		!to_info->hooks.validate_node(*this, *to_info, *to_node, validation_error)) {
		links = links_snapshot;
		from_node->generated_inputs = from_generated_inputs_snapshot;
		from_node->generated_outputs = from_generated_outputs_snapshot;
		from_node->inputs = from_inputs_snapshot;
		from_node->outputs = from_outputs_snapshot;
		to_node->generated_inputs = to_generated_inputs_snapshot;
		to_node->generated_outputs = to_generated_outputs_snapshot;
		to_node->inputs = to_inputs_snapshot;
		to_node->outputs = to_outputs_snapshot;
		return false;
	}

	if (on_link_created)
		on_link_created(*resolved_link);
	notify_graph_dirty();
	return true;
}

void NodeGraph::remove_node(int node_id) {
	const NE_Node* node = find_node(node_id);
	if (node != nullptr && node->is_permanent)
		return;

	std::vector<int> affected_nodes;
	links.erase(std::remove_if(links.begin(), links.end(), [&](const NE_Link& link) {
		const bool incident = link.from_node == node_id || link.to_node == node_id;
		if (incident) {
			if (link.from_node != node_id)
				affected_nodes.push_back(link.from_node);
			if (link.to_node != node_id)
				affected_nodes.push_back(link.to_node);
			if (on_link_removed)
				on_link_removed(link);
		}
		return incident;
	}), links.end());

	nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const NE_Node& node_ref) {
		return node_ref.id == node_id;
	}), nodes.end());

	if (on_node_removed)
		on_node_removed(node_id);
	refresh_dynamic_nodes(affected_nodes);
	notify_graph_dirty();
}

void NodeGraph::remove_link(const NE_Link& target) {
	std::vector<int> affected_nodes;
	links.erase(std::remove_if(links.begin(), links.end(), [&](const NE_Link& link) {
		const bool match =
			link.from_node == target.from_node &&
			link.from_pin == target.from_pin &&
			link.to_node == target.to_node &&
			link.to_pin == target.to_pin;
		if (match) {
			affected_nodes.push_back(link.from_node);
			affected_nodes.push_back(link.to_node);
			if (on_link_removed)
				on_link_removed(link);
		}
		return match;
	}), links.end());
	refresh_dynamic_nodes(affected_nodes);
	notify_graph_dirty();
}

bool NodeGraph::replace_generated_pins(int node_id, std::vector<NE_Pin> generated_inputs, std::vector<NE_Pin> generated_outputs) {
	NE_Node* node = find_node(node_id);
	if (node == nullptr)
		return false;

	std::vector<int> old_input_ids;
	std::vector<int> old_output_ids;
	old_input_ids.reserve(node->generated_inputs.size());
	old_output_ids.reserve(node->generated_outputs.size());
	for (const auto& pin : node->generated_inputs)
		old_input_ids.push_back(pin.id);
	for (const auto& pin : node->generated_outputs)
		old_output_ids.push_back(pin.id);

	int next_pin_id = 0;
	if (node->has_exec_input)
		next_pin_id = std::max(next_pin_id, node->exec_input.id + 1);
	for (const auto& exec_pin : node->exec_outputs)
		next_pin_id = std::max(next_pin_id, exec_pin.id + 1);
	for (const auto& pin : node->static_inputs)
		next_pin_id = std::max(next_pin_id, pin.id + 1);
	for (const auto& pin : node->static_outputs)
		next_pin_id = std::max(next_pin_id, pin.id + 1);

	for (auto& pin : generated_inputs) {
		pin.generated = true;
		pin.kind = NE_PinKind::data;
		pin.id = next_pin_id++;
	}
	for (auto& pin : generated_outputs) {
		pin.generated = true;
		pin.kind = NE_PinKind::data;
		pin.id = next_pin_id++;
	}

	const auto old_generated_inputs = node->generated_inputs;
	const auto old_generated_outputs = node->generated_outputs;
	node->generated_inputs = std::move(generated_inputs);
	node->generated_outputs = std::move(generated_outputs);
	rebuild_visible_pins(*node);

	for (auto it = links.begin(); it != links.end();) {
		bool touched = false;
		bool drop = false;

		if (it->to_node == node_id &&
			std::find(old_input_ids.begin(), old_input_ids.end(), it->to_pin) != old_input_ids.end()) {
			touched = true;
			const NE_Pin* old_pin = find_pin(old_generated_inputs, it->to_pin);
			const NE_Pin* new_pin = old_pin ? find_pin_by_label(node->generated_inputs, old_pin->label) : nullptr;
			if (new_pin != nullptr)
				it->to_pin = new_pin->id;
			else
				drop = true;
		}

		if (!drop &&
			it->from_node == node_id &&
			std::find(old_output_ids.begin(), old_output_ids.end(), it->from_pin) != old_output_ids.end()) {
			touched = true;
			const NE_Pin* old_pin = find_pin(old_generated_outputs, it->from_pin);
			const NE_Pin* new_pin = old_pin ? find_pin_by_label(node->generated_outputs, old_pin->label) : nullptr;
			if (new_pin != nullptr)
				it->from_pin = new_pin->id;
			else
				drop = true;
		}

		if (!drop && touched) {
			const NE_Node* current_from_node = find_node(it->from_node);
			const NE_Node* current_to_node = find_node(it->to_node);
			const NE_Pin* current_from_pin = current_from_node ? find_output_pin(*current_from_node, it->from_pin) : nullptr;
			const NE_Pin* current_to_pin = current_to_node ? find_input_pin(*current_to_node, it->to_pin) : nullptr;
			if (current_from_pin == nullptr || current_to_pin == nullptr ||
				(current_from_pin->kind == NE_PinKind::data && !is_data_link_compatible(*current_from_pin, *current_to_pin))) {
				drop = true;
			}
		}

		if (drop) {
			if (on_link_removed)
				on_link_removed(*it);
			it = links.erase(it);
		} else {
			++it;
		}
	}

	notify_graph_dirty();
	return true;
}

bool NodeGraph::replace_exec_outputs(int node_id, std::vector<NE_Pin> exec_outputs) {
	NE_Node* node = find_node(node_id);
	if (node == nullptr)
		return false;

	std::vector<int> old_exec_ids;
	old_exec_ids.reserve(node->exec_outputs.size());
	for (const auto& pin : node->exec_outputs)
		old_exec_ids.push_back(pin.id);

	int next_pin_id = 0;
	if (node->has_exec_input)
		next_pin_id = std::max(next_pin_id, node->exec_input.id + 1);
	for (const auto& pin : node->static_inputs)
		next_pin_id = std::max(next_pin_id, pin.id + 1);
	for (const auto& pin : node->static_outputs)
		next_pin_id = std::max(next_pin_id, pin.id + 1);
	for (const auto& pin : node->generated_inputs)
		next_pin_id = std::max(next_pin_id, pin.id + 1);
	for (const auto& pin : node->generated_outputs)
		next_pin_id = std::max(next_pin_id, pin.id + 1);

	for (auto& pin : exec_outputs) {
		pin.generated = false;
		pin.kind = NE_PinKind::exec;
		if (pin.label.empty())
			pin.label = "Exec";
		pin.id = next_pin_id++;
	}

	const auto old_exec_outputs = node->exec_outputs;
	node->exec_outputs = std::move(exec_outputs);
	node->has_exec_output = !node->exec_outputs.empty();
	node->exec_output = node->has_exec_output ? node->exec_outputs.front() : NE_Pin {};
	rebuild_visible_pins(*node);

	for (auto it = links.begin(); it != links.end();) {
		bool touched = false;
		bool drop = false;

		if (it->from_node == node_id &&
			std::find(old_exec_ids.begin(), old_exec_ids.end(), it->from_pin) != old_exec_ids.end()) {
			touched = true;
			const NE_Pin* old_pin = find_pin(old_exec_outputs, it->from_pin);
			const NE_Pin* new_pin = old_pin ? find_pin_by_label(node->exec_outputs, old_pin->label) : nullptr;
			if (new_pin != nullptr)
				it->from_pin = new_pin->id;
			else
				drop = true;
		}

		if (!drop && touched) {
			const NE_Node* current_from_node = find_node(it->from_node);
			const NE_Node* current_to_node = find_node(it->to_node);
			const NE_Pin* current_from_pin = current_from_node ? find_output_pin(*current_from_node, it->from_pin) : nullptr;
			const NE_Pin* current_to_pin = current_to_node ? find_input_pin(*current_to_node, it->to_pin) : nullptr;
			if (current_from_pin == nullptr || current_to_pin == nullptr || current_from_pin->kind != current_to_pin->kind)
				drop = true;
		}

		if (drop) {
			if (on_link_removed)
				on_link_removed(*it);
			it = links.erase(it);
		} else {
			++it;
		}
	}

	notify_graph_dirty();
	return true;
}

size_t NodeGraph::run_start_nodes() {
	if (registry_ == nullptr)
		return 0;

	size_t executed = 0;
	for (auto& node : nodes) {
		const NodeTypeInfo* info = registry_->find(node.type);
		if (info == nullptr || !info->meta.is_start || !info->hooks.execute_processor)
			continue;
		if (info->hooks.execute_processor(node))
			++executed;
	}

	return executed;
}

bool NodeGraph::is_node_permanent(int node_id) const {
	const NE_Node* node = find_node(node_id);
	return node != nullptr && node->is_permanent;
}

void NodeGraph::notify_node_moved(int node_id) {
	if (on_node_moved)
		on_node_moved(node_id);
}

void NodeGraph::notify_graph_dirty() {
	if (on_graph_dirty)
		on_graph_dirty();
}

graph_virtual_struct_schema* NodeGraph::create_virtual_struct(graph_virtual_struct_schema schema) {
	return create_virtual_struct(std::move(schema), -1);
}

graph_virtual_struct_schema* NodeGraph::create_virtual_struct(graph_virtual_struct_schema schema, int forced_id) {
	schema.id = forced_id >= 0 ? forced_id : next_virtual_struct_id_++;
	virtual_structs.push_back(std::move(schema));
	next_virtual_struct_id_ = std::max(next_virtual_struct_id_, virtual_structs.back().id + 1);
	notify_graph_dirty();
	return &virtual_structs.back();
}

graph_texture_slot* NodeGraph::create_texture_slot(graph_texture_slot slot) {
	return create_texture_slot(std::move(slot), -1);
}

graph_texture_slot* NodeGraph::create_texture_slot(graph_texture_slot slot, int forced_id) {
	slot.id = forced_id >= 0 ? forced_id : next_texture_slot_id_++;
	texture_slots.push_back(std::move(slot));
	next_texture_slot_id_ = std::max(next_texture_slot_id_, texture_slots.back().id + 1);
	notify_graph_dirty();
	return &texture_slots.back();
}

graph_variable_slot* NodeGraph::create_variable_slot(graph_variable_slot slot) {
	return create_variable_slot(std::move(slot), -1);
}

graph_variable_slot* NodeGraph::create_variable_slot(graph_variable_slot slot, int forced_id) {
	slot.id = forced_id >= 0 ? forced_id : next_variable_slot_id_++;
	variable_slots.push_back(std::move(slot));
	next_variable_slot_id_ = std::max(next_variable_slot_id_, variable_slots.back().id + 1);
	notify_graph_dirty();
	return &variable_slots.back();
}

graph_virtual_struct_schema* NodeGraph::find_virtual_struct(int schema_id) {
	for (auto& schema : virtual_structs)
		if (schema.id == schema_id)
			return &schema;
	return nullptr;
}

const graph_virtual_struct_schema* NodeGraph::find_virtual_struct(int schema_id) const {
	for (const auto& schema : virtual_structs)
		if (schema.id == schema_id)
			return &schema;
	return nullptr;
}

graph_virtual_struct_schema* NodeGraph::find_virtual_struct(std::string_view name, size_t layout_fingerprint) {
	for (auto& schema : virtual_structs) {
		if (schema.name == name && graph_virtual_struct_layout_fingerprint(schema) == layout_fingerprint)
			return &schema;
	}
	return nullptr;
}

const graph_virtual_struct_schema* NodeGraph::find_virtual_struct(std::string_view name, size_t layout_fingerprint) const {
	for (const auto& schema : virtual_structs) {
		if (schema.name == name && graph_virtual_struct_layout_fingerprint(schema) == layout_fingerprint)
			return &schema;
	}
	return nullptr;
}

graph_shader_interface* NodeGraph::upsert_shader_interface(graph_shader_interface shader_interface) {
	if (graph_shader_interface* existing = find_shader_interface(shader_interface.id); existing != nullptr) {
		*existing = std::move(shader_interface);
		return existing;
	}
	shader_interfaces.push_back(std::move(shader_interface));
	return &shader_interfaces.back();
}

graph_shader_interface* NodeGraph::find_shader_interface(int interface_id) {
	for (auto& shader_interface : shader_interfaces) {
		if (shader_interface.id == interface_id)
			return &shader_interface;
	}
	return nullptr;
}

const graph_shader_interface* NodeGraph::find_shader_interface(int interface_id) const {
	for (const auto& shader_interface : shader_interfaces) {
		if (shader_interface.id == interface_id)
			return &shader_interface;
	}
	return nullptr;
}

graph_shader_interface* NodeGraph::find_shader_interface_by_source_node(int node_id) {
	for (auto& shader_interface : shader_interfaces) {
		if (shader_interface.source_node_id == node_id)
			return &shader_interface;
	}
	return nullptr;
}

const graph_shader_interface* NodeGraph::find_shader_interface_by_source_node(int node_id) const {
	for (const auto& shader_interface : shader_interfaces) {
		if (shader_interface.source_node_id == node_id)
			return &shader_interface;
	}
	return nullptr;
}

graph_texture_slot* NodeGraph::find_texture_slot(int slot_id) {
	for (auto& slot : texture_slots)
		if (slot.id == slot_id)
			return &slot;
	return nullptr;
}

const graph_texture_slot* NodeGraph::find_texture_slot(int slot_id) const {
	for (const auto& slot : texture_slots)
		if (slot.id == slot_id)
			return &slot;
	return nullptr;
}

graph_variable_slot* NodeGraph::find_variable_slot(int slot_id) {
	for (auto& slot : variable_slots)
		if (slot.id == slot_id)
			return &slot;
	return nullptr;
}

const graph_variable_slot* NodeGraph::find_variable_slot(int slot_id) const {
	for (const auto& slot : variable_slots)
		if (slot.id == slot_id)
			return &slot;
	return nullptr;
}

const NodeRegistry* NodeGraph::node_registry() const {
	return registry_;
}

int NodeGraph::active_function_id() const {
	return active_function_id_;
}

bool NodeGraph::set_active_function(int function_id) {
	if (find_function(function_id) == nullptr)
		return false;
	active_function_id_ = function_id;
	return true;
}

graph_function_definition* NodeGraph::find_function(int function_id) {
	for (auto& function : functions)
		if (function.id == function_id)
			return &function;
	return nullptr;
}

const graph_function_definition* NodeGraph::find_function(int function_id) const {
	for (const auto& function : functions)
		if (function.id == function_id)
			return &function;
	return nullptr;
}

graph_function_definition* NodeGraph::setup_function() {
	return find_function(setup_function_id_);
}

const graph_function_definition* NodeGraph::setup_function() const {
	return find_function(setup_function_id_);
}

graph_function_definition* NodeGraph::render_function() {
	return find_function(render_function_id_);
}

const graph_function_definition* NodeGraph::render_function() const {
	return find_function(render_function_id_);
}

int NodeGraph::setup_function_id() const {
	return setup_function_id_;
}

int NodeGraph::render_function_id() const {
	return render_function_id_;
}

graph_function_definition* NodeGraph::create_function(std::string name) {
	graph_function_definition function;
	function.id = next_function_id_++;
	function.name = std::move(name);
	functions.push_back(std::move(function));
	if (active_function_id_ == -1)
		active_function_id_ = functions.back().id;
	notify_graph_dirty();
	return &functions.back();
}

bool NodeGraph::remove_function(int function_id) {
	graph_function_definition* function = find_function(function_id);
	if (function == nullptr || is_builtin_function(function->id))
		return false;

	for (auto it = links.begin(); it != links.end();) {
		const NE_Node* from_node = find_node(it->from_node);
		const NE_Node* to_node = find_node(it->to_node);
		const bool remove = (from_node != nullptr && from_node->function_id == function_id) ||
			(to_node != nullptr && to_node->function_id == function_id);
		if (remove) {
			if (on_link_removed)
				on_link_removed(*it);
			it = links.erase(it);
		} else {
			++it;
		}
	}
	for (auto it = nodes.begin(); it != nodes.end();) {
		if (it->function_id == function_id) {
			if (on_node_removed)
				on_node_removed(it->id);
			it = nodes.erase(it);
		} else {
			++it;
		}
	}

	functions.erase(std::remove_if(functions.begin(), functions.end(), [&](const graph_function_definition& candidate) {
		return candidate.id == function_id;
	}), functions.end());
	if (active_function_id_ == function_id)
		active_function_id_ = setup_function_id_;
	notify_graph_dirty();
	return true;
}

std::vector<NE_Node*> NodeGraph::nodes_in_function(int function_id) {
	std::vector<NE_Node*> result;
	for (auto& node : nodes) {
		if (node.function_id == function_id)
			result.push_back(&node);
	}
	return result;
}

std::vector<const NE_Node*> NodeGraph::nodes_in_function(int function_id) const {
	std::vector<const NE_Node*> result;
	for (const auto& node : nodes) {
		if (node.function_id == function_id)
			result.push_back(&node);
	}
	return result;
}

std::vector<const NE_Link*> NodeGraph::links_in_function(int function_id) const {
	std::vector<const NE_Link*> result;
	for (const auto& link : links) {
		const NE_Node* from_node = find_node(link.from_node);
		const NE_Node* to_node = find_node(link.to_node);
		if (from_node == nullptr || to_node == nullptr)
			continue;
		if (from_node->function_id == function_id && to_node->function_id == function_id)
			result.push_back(&link);
	}
	return result;
}

void NodeGraph::sync_function_metadata() {
	next_function_id_ = 0;
	setup_function_id_ = -1;
	render_function_id_ = -1;
	for (const auto& function : functions) {
		next_function_id_ = std::max(next_function_id_, function.id + 1);
		if (function.name == "setup")
			setup_function_id_ = function.id;
		else if (function.name == "render")
			render_function_id_ = function.id;
	}
	ensure_builtin_functions();
	if (find_function(active_function_id_) == nullptr)
		active_function_id_ = setup_function_id_;
}

void NodeGraph::clear() {
	for (const auto& link : links) {
		if (on_link_removed)
			on_link_removed(link);
	}
	for (const auto& node : nodes) {
		if (on_node_removed)
			on_node_removed(node.id);
	}
	links.clear();
	nodes.clear();
	functions.clear();
	virtual_structs.clear();
	shader_interfaces.clear();
	texture_slots.clear();
	variable_slots.clear();
	next_node_id_ = 0;
	next_function_signature_pin_id_ = 0;
	next_function_id_ = 0;
	next_virtual_struct_id_ = 0;
	next_texture_slot_id_ = 0;
	next_variable_slot_id_ = 0;
	setup_function_id_ = -1;
	render_function_id_ = -1;
	active_function_id_ = -1;
	ensure_builtin_functions();
	notify_graph_dirty();
}

void NodeGraph::refresh_dynamic_nodes(const std::vector<int>& node_ids) {
	if (registry_ == nullptr)
		return;
	for (int node_id : node_ids) {
		NE_Node* node = find_node(node_id);
		if (node == nullptr)
			continue;
		const NodeTypeInfo* info = registry_->find(node->type);
		if (info != nullptr && info->hooks.refresh_dynamic_pins)
			info->hooks.refresh_dynamic_pins(*this, *info, *node);
	}
}

bool NodeGraph::contains_link(const NE_Link& candidate) const {
	return std::any_of(links.begin(), links.end(), [&](const NE_Link& link) {
		return link.from_node == candidate.from_node &&
			link.from_pin == candidate.from_pin &&
			link.to_node == candidate.to_node &&
			link.to_pin == candidate.to_pin;
	});
}

const NE_Pin* NodeGraph::find_pin(const std::vector<NE_Pin>& pins, int pin_id) {
	for (const auto& pin : pins)
		if (pin.id == pin_id)
			return &pin;
	return nullptr;
}

const NE_Pin* NodeGraph::find_pin_by_label(const std::vector<NE_Pin>& pins, std::string_view label) {
	for (const auto& pin : pins) {
		if (pin.label == label)
			return &pin;
	}
	return nullptr;
}

const NE_Pin* NodeGraph::find_input_pin(const NE_Node& node, int pin_id) {
	if (node.has_exec_input && node.exec_input.id == pin_id)
		return &node.exec_input;
	return find_pin(node.inputs, pin_id);
}

const NE_Pin* NodeGraph::find_output_pin(const NE_Node& node, int pin_id) {
	if (const NE_Pin* exec_pin = find_pin(node.exec_outputs, pin_id); exec_pin != nullptr)
		return exec_pin;
	return find_pin(node.outputs, pin_id);
}

bool NodeGraph::is_data_link_compatible(const NE_Pin& from_pin, const NE_Pin& to_pin) {
	if (from_pin.kind != NE_PinKind::data || to_pin.kind != NE_PinKind::data)
		return from_pin.kind == to_pin.kind;
	if ((from_pin.is_wildcard && !from_pin.wildcard_resolved) || (to_pin.is_wildcard && !to_pin.wildcard_resolved))
		return false;
	if (from_pin.type_hash != to_pin.type_hash)
		return false;
	if (from_pin.is_container != to_pin.is_container)
		return false;
	if (from_pin.has_virtual_struct || to_pin.has_virtual_struct) {
		if (!from_pin.has_virtual_struct || !to_pin.has_virtual_struct)
			return false;
		if (from_pin.virtual_struct_name != to_pin.virtual_struct_name)
			return false;
		if (from_pin.virtual_struct_layout_fingerprint != to_pin.virtual_struct_layout_fingerprint)
			return false;
	}
	if (from_pin.template_base_type_hash != 0 || to_pin.template_base_type_hash != 0) {
		if (from_pin.template_base_type_hash != 0 &&
			to_pin.template_base_type_hash != 0 &&
			from_pin.template_base_type_hash != to_pin.template_base_type_hash) {
			return false;
		}
		if (from_pin.template_value_hash != 0 &&
			to_pin.template_value_hash != 0 &&
			from_pin.template_value_hash != to_pin.template_value_hash) {
			return false;
		}
	}
	return true;
}

NE_Link* NodeGraph::find_link_by_labels(int from_node_id, std::string_view from_label, int to_node_id, std::string_view to_label) {
	for (auto& link : links) {
		if (link.from_node != from_node_id || link.to_node != to_node_id)
			continue;
		const NE_Node* from_node = find_node(link.from_node);
		const NE_Node* to_node = find_node(link.to_node);
		const NE_Pin* from_pin = from_node ? find_output_pin(*from_node, link.from_pin) : nullptr;
		const NE_Pin* to_pin = to_node ? find_input_pin(*to_node, link.to_pin) : nullptr;
		if (from_pin != nullptr && to_pin != nullptr &&
			from_pin->label == from_label &&
			to_pin->label == to_label) {
			return &link;
		}
	}
	return nullptr;
}

void NodeGraph::rebuild_visible_pins(NE_Node& node) {
	node.inputs = node.static_inputs;
	node.inputs.insert(node.inputs.end(), node.generated_inputs.begin(), node.generated_inputs.end());
	node.outputs = node.static_outputs;
	node.outputs.insert(node.outputs.end(), node.generated_outputs.begin(), node.generated_outputs.end());
}

int NodeGraph::resolved_function_id(int function_id) const {
	return function_id >= 0 ? function_id : active_function_id_;
}

graph_function_definition* NodeGraph::create_builtin_function(std::string name, int& stored_id) {
	graph_function_definition function;
	function.id = next_function_id_++;
	function.name = std::move(name);
	functions.push_back(std::move(function));
	stored_id = functions.back().id;
	return &functions.back();
}

bool NodeGraph::is_builtin_function(int id) const {
	return id == setup_function_id_ || id == render_function_id_;
}

void NodeGraph::ensure_builtin_functions() {
	if (setup_function() == nullptr)
		create_builtin_function("setup", setup_function_id_);
	if (render_function() == nullptr)
		create_builtin_function("render", render_function_id_);
	if (active_function_id_ == -1)
		active_function_id_ = setup_function_id_;
}
