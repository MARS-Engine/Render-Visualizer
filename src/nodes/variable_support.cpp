#include <render_visualizer/nodes/variable_support.hpp>

namespace rv::nodes {

namespace {

using local_variable_types = std::tuple<
	float,
	mars::vector2<float>,
	mars::vector3<float>,
	mars::vector4<float>,
	mars::matrix4<float>,
	unsigned int,
	mars::vector2<unsigned int>,
	mars::vector3<unsigned int>,
	mars::vector4<unsigned int>,
	bool,
	std::string,
	std::vector<float>,
	std::vector<mars::vector2<float>>,
	std::vector<mars::vector3<float>>,
	std::vector<unsigned int>,
	rv::resource_tags::vertex_buffer,
	rv::resource_tags::index_buffer,
	rv::resource_tags::uniform_resource,
	mars::vector3<unsigned char>,
	rv::resource_tags::texture_slot,
	rv::resource_tags::shader_module,
	rv::resource_tags::render_pass,
	rv::resource_tags::framebuffer,
	rv::resource_tags::depth_buffer,
	rv::resource_tags::graphics_pipeline,
	rv::resource_tags::material_resource,
	rv::resource_tags::virtual_struct_schema
>;

constexpr size_t kGraphicsPipelineTypeHash = rv::detail::pin_type_hash<rv::resource_tags::graphics_pipeline>();

bool is_resource_variable_slot(const variable_slot_state& slot) {
	return slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::vertex_buffer>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::index_buffer>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::uniform_resource>() ||
		slot.type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned char>>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::texture_slot>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::shader_module>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::render_pass>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::framebuffer>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::depth_buffer>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::graphics_pipeline>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::material_resource>() ||
		slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::virtual_struct_schema>();
}

template <typename T>
void reset_variable_slot_default_impl(variable_slot_state& slot) {
	if constexpr (std::is_same_v<T, std::string>) {
		std::string value;
		slot.default_json = json_stringify(value);
	} else {
		T value {};
		slot.default_json = json_stringify(value);
	}
}

} // namespace

const std::vector<variable_type_descriptor>& variable_type_descriptors() {
	static const std::vector<variable_type_descriptor> descriptors = [] {
		std::vector<variable_type_descriptor> result;
		[&]<size_t... Indices>(std::index_sequence<Indices...>) {
			(result.push_back(make_variable_type_descriptor<std::tuple_element_t<Indices, local_variable_types>>()), ...);
		}(std::make_index_sequence<std::tuple_size_v<local_variable_types>> {});
		return result;
	}();
	return descriptors;
}

const variable_type_descriptor* find_variable_type_descriptor(const variable_slot_state& slot) {
	for (const auto& descriptor : variable_type_descriptors()) {
		if (descriptor.type_hash == slot.type_hash &&
			descriptor.is_container == slot.is_container)
			return &descriptor;
	}
	return nullptr;
}

const char* variable_slot_type_name(const variable_slot_state& slot) {
	if (const auto* descriptor = find_variable_type_descriptor(slot); descriptor != nullptr) {
		if (variable_slot_uses_pipeline_template(slot) && !slot.template_display_name.empty()) {
			thread_local std::string label;
			label = std::string(descriptor->label) + " <" + slot.template_display_name + ">";
			return label.c_str();
		}
		return descriptor->label;
	}
	return "Unknown";
}

bool variable_slot_uses_pipeline_template(const variable_slot_state& slot) {
	return slot.type_hash == kGraphicsPipelineTypeHash &&
		slot.template_base_type_hash == kGraphicsPipelineTypeHash;
}

void clear_variable_slot_template(variable_slot_state& slot) {
	slot.template_base_type_hash = 0;
	slot.template_value_hash = 0;
	slot.template_display_name.clear();
}

void reset_variable_slot_default(variable_slot_state& slot) {
	if (is_resource_variable_slot(slot) || slot.is_container) {
		slot.default_json.clear();
		return;
	}

	bool handled = false;
	[&]<size_t... Indices>(std::index_sequence<Indices...>) {
		([&]<typename value_t>() {
			if constexpr (!variable_type_traits<value_t>::is_container) {
				if (!handled && variable_slot_matches_type<value_t>(slot)) {
					if constexpr (make_variable_type_descriptor<value_t>().supports_default_editor)
						reset_variable_slot_default_impl<value_t>(slot);
					else
						slot.default_json.clear();
					handled = true;
				}
			}
		}.template operator()<std::tuple_element_t<Indices, local_variable_types>>(), ...);
	}(std::make_index_sequence<std::tuple_size_v<local_variable_types>> {});

	if (!handled)
		slot.default_json.clear();
}

variable_slot_state make_default_variable_slot(std::string name) {
	variable_slot_state slot;
	slot.name = std::move(name);
	reset_variable_slot_default(slot);
	slot.status = "Uses its authored default until a Set node writes to it.";
	return slot;
}

int ensure_default_variable_slot(NodeGraph& graph, std::string name) {
	if (variable_slot_state* slot = graph.create_variable_slot(make_default_variable_slot(std::move(name))); slot != nullptr)
		return slot->id;
	return -1;
}

void sync_variable_get_node(NodeGraph& graph, NE_Node& node) {
	std::vector<NE_Pin> outputs;
	const auto* state = node.custom_state.storage ? &node.custom_state.as<variable_node_state>() : nullptr;
	if (state != nullptr) {
			if (const auto* slot = graph.find_variable_slot(state->variable_id); slot != nullptr) {
				NE_Pin pin = make_pin<float>("value");
				apply_variable_slot_metadata(pin, *slot);
				if (variable_slot_uses_pipeline_template(*slot) && !slot->template_display_name.empty())
					apply_pin_template_metadata(pin, slot->template_base_type_hash, slot->template_value_hash, slot->template_display_name);
				outputs.push_back(std::move(pin));
				node.title = slot->name + " Get";
			} else {
			node.title = "Variable Get";
		}
	}
	if (equivalent_pin_layout(node.generated_outputs, outputs))
		return;
	graph.replace_generated_pins(node.id, {}, std::move(outputs));
}

void sync_variable_set_node(NodeGraph& graph, NE_Node& node) {
	std::vector<NE_Pin> inputs;
	const auto* state = node.custom_state.storage ? &node.custom_state.as<variable_node_state>() : nullptr;
	if (state != nullptr) {
			if (const auto* slot = graph.find_variable_slot(state->variable_id); slot != nullptr) {
				NE_Pin pin = make_pin<float>("value");
				apply_variable_slot_metadata(pin, *slot);
				if (variable_slot_uses_pipeline_template(*slot) && !slot->template_display_name.empty())
					apply_pin_template_metadata(pin, slot->template_base_type_hash, slot->template_value_hash, slot->template_display_name);
				inputs.push_back(std::move(pin));
				node.title = slot->name + " Set";
			} else {
			node.title = "Variable Set";
		}
	}
	if (equivalent_pin_layout(node.generated_inputs, inputs))
		return;
	graph.replace_generated_pins(node.id, std::move(inputs), {});
}

void refresh_dynamic_variable_nodes(NodeGraph& graph) {
	for (auto& node : graph.nodes) {
		if (node.type == node_type_v<variable_get_node_tag>)
			sync_variable_get_node(graph, node);
		else if (node.type == node_type_v<variable_set_node_tag>)
			sync_variable_set_node(graph, node);
	}
}

bool load_variable_node_state(variable_node_state& state, std::string_view json, std::string& error, const char* failure_message) {
	variable_node_state snapshot;
	if (json_parse(json, snapshot)) {
		state = snapshot;
		return true;
	}
	error = failure_message;
	return false;
}

} // namespace rv::nodes
