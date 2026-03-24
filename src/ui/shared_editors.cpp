#include <render_visualizer/ui/shared_editors.hpp>

#include <imgui.h>

#include <render_visualizer/nodes/shader_module_node.hpp>

#include <algorithm>
#include <tuple>

namespace rv::ui {

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

constexpr size_t k_graphics_pipeline_type_hash = rv::detail::pin_type_hash<rv::resource_tags::graphics_pipeline>();

const NE_Node* find_pipeline_template_shader_node(const NodeGraph& _graph, int _node_id) {
	const NE_Node* _node = _graph.find_node(_node_id);
	if (_node == nullptr || _node->type != rv::nodes::node_type_v<rv::nodes::shader_module_node_tag>)
		return nullptr;
	return _node;
}

size_t pipeline_template_hash_from_shader_node(const NE_Node& _shader_node) {
	return static_cast<size_t>(_shader_node.id + 1);
}

bool is_resource_variable_slot(const rv::nodes::variable_slot_state& _slot) {
	return _slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::vertex_buffer>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::index_buffer>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::uniform_resource>() ||
		_slot.type_hash == rv::detail::pin_type_hash<mars::vector3<unsigned char>>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::texture_slot>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::shader_module>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::render_pass>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::framebuffer>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::depth_buffer>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::graphics_pipeline>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::material_resource>() ||
		_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::virtual_struct_schema>();
}

template <typename T>
bool render_variable_default_editor_impl(const char* _label, std::string& _json) {
	T _value {};
	if (!_json.empty())
		rv::nodes::json_parse(_json, _value);

	const bool _changed = rv::ui::render_typed_value_editor(_label, _value);
	if (_changed)
		_json = rv::nodes::json_stringify(_value);
	return _changed;
}

} // namespace

bool render_function_signature_editor(std::vector<graph_function_signature_pin>& _pins, const char* _label_prefix) {
	bool _changed = false;
	bool _remove_requested = false;
	size_t _remove_index = 0;
	for (size_t _index = 0; _index < _pins.size(); ++_index) {
		auto& _pin = _pins[_index];
		ImGui::PushID(static_cast<int>(_index));
		ImGui::Separator();
		_changed |= input_text_string("Name", _pin.label);
		const rv::nodes::variable_type_descriptor* _current_descriptor = rv::nodes::find_function_signature_descriptor(_pin);
		const char* _current_label = _current_descriptor != nullptr ? _current_descriptor->label : "Unknown";
		if (ImGui::BeginCombo("Type", _current_label)) {
			for (const auto& _descriptor : rv::nodes::variable_type_descriptors()) {
				const bool _selected =
					_descriptor.type_hash == _pin.type_hash &&
					_descriptor.is_container == _pin.is_container;
				if (ImGui::Selectable(_descriptor.label, _selected)) {
					_pin.type_hash = _descriptor.type_hash;
					_pin.is_container = _descriptor.is_container;
					if (!_descriptor.supports_virtual_struct) {
						_pin.has_virtual_struct = false;
						_pin.virtual_struct_name.clear();
						_pin.virtual_struct_layout_fingerprint = 0;
					}
					_changed = true;
				}
				if (_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ImGui::Button("Remove")) {
			_remove_requested = true;
			_remove_index = _index;
		}
		ImGui::PopID();
	}
	if (_remove_requested) {
		_pins.erase(_pins.begin() + static_cast<std::ptrdiff_t>(_remove_index));
		_changed = true;
	}

	std::string _button_label = std::string("Add ") + _label_prefix;
	if (ImGui::Button(_button_label.c_str())) {
		const auto& _descriptors = rv::nodes::variable_type_descriptors();
		if (!_descriptors.empty()) {
			_pins.push_back(rv::nodes::make_function_signature_pin_from_descriptor(
				_descriptors.front(),
				std::string(_label_prefix) + std::to_string(_pins.size())
			));
			_changed = true;
		}
	}
	return _changed;
}

bool render_texture_slot_editor(NodeGraph& _graph, rv::nodes::texture_slot_state& _slot) {
	bool _changed = false;
	_changed |= input_text_string("Name##texture_slot_name", _slot.name);
	_changed |= input_text_string("Path##texture_slot_path", _slot.path);
	const bool _dialog_open = is_file_picker_open();
	if (_dialog_open)
		ImGui::BeginDisabled();
	if (ImGui::Button("Browse...", { -1.0f, 0.0f }))
		open_file_picker(_slot.path);
	if (_dialog_open)
		ImGui::EndDisabled();
	if (std::optional<std::string> _selected = consume_selected_file_path(); _selected.has_value()) {
		_slot.path = *_selected;
		_changed = true;
	}
	if (!_slot.status.empty())
		ImGui::TextDisabled("%s", _slot.status.c_str());
	if (_changed)
		_graph.notify_graph_dirty();
	return _changed;
}

bool render_texture_slot_selector(NodeGraph& _graph, int& _slot_id, const char* _combo_label, const char* _create_label_prefix) {
	bool _changed = false;
	const char* _current_label = "None";
	if (const auto* _slot = _graph.find_texture_slot(_slot_id); _slot != nullptr)
		_current_label = _slot->name.c_str();

	if (ImGui::BeginCombo(_combo_label, _current_label)) {
		if (ImGui::Selectable("None", _slot_id == -1)) {
			_slot_id = -1;
			_changed = true;
		}
		for (const auto& _slot : _graph.texture_slots) {
			const bool _selected = _slot.id == _slot_id;
			if (ImGui::Selectable(_slot.name.c_str(), _selected)) {
				_slot_id = _slot.id;
				_changed = true;
			}
			if (_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	std::string _button_label = std::string("Create New##") + _combo_label;
	if (ImGui::Button(_button_label.c_str(), { -1.0f, 0.0f })) {
		const int _suffix = static_cast<int>(_graph.texture_slots.size());
		_slot_id = rv::nodes::ensure_default_texture_slot(_graph, std::string(_create_label_prefix) + std::to_string(_suffix));
		_changed = true;
	}

	if (_changed)
		_graph.notify_graph_dirty();
	return _changed;
}

bool render_pipeline_variable_template_selector(NodeGraph& _graph, rv::nodes::variable_slot_state& _slot) {
	if (_slot.type_hash != k_graphics_pipeline_type_hash)
		return false;

	int _selected_node_id = -1;
	if (_slot.template_value_hash != 0) {
		for (const auto& _node : _graph.nodes) {
			if (_node.type != rv::nodes::node_type_v<rv::nodes::shader_module_node_tag>)
				continue;
			if (pipeline_template_hash_from_shader_node(_node) == _slot.template_value_hash) {
				_selected_node_id = _node.id;
				break;
			}
		}
	}

	const char* _current_label = "None";
	if (const NE_Node* _shader_node = find_pipeline_template_shader_node(_graph, _selected_node_id); _shader_node != nullptr)
		_current_label = _shader_node->title.c_str();

	bool _changed = false;
	if (ImGui::BeginCombo("Base Shader##pipeline_template", _current_label)) {
		if (ImGui::Selectable("None", _selected_node_id == -1)) {
			rv::nodes::clear_variable_slot_template(_slot);
			_changed = true;
		}
		for (const auto& _node : _graph.nodes) {
			if (_node.type != rv::nodes::node_type_v<rv::nodes::shader_module_node_tag>)
				continue;
			const bool _selected = _node.id == _selected_node_id;
			if (ImGui::Selectable(_node.title.c_str(), _selected)) {
				_slot.template_base_type_hash = k_graphics_pipeline_type_hash;
				_slot.template_value_hash = pipeline_template_hash_from_shader_node(_node);
				_slot.template_display_name = _node.title;
				_changed = true;
			}
			if (_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	return _changed;
}

bool render_variable_default_editor(rv::nodes::variable_slot_state& _slot) {
	if (is_resource_variable_slot(_slot)) {
		ImGui::TextDisabled("Resource variables do not have an authored inline default in v1.");
		return false;
	}
	if (_slot.is_container) {
		ImGui::TextDisabled("Container variables start empty until a Set node writes to them.");
		return false;
	}

	bool _handled = false;
	bool _changed = false;
	[&]<size_t... _indices>(std::index_sequence<_indices...>) {
		([&]<typename value_t>() {
			if constexpr (!rv::nodes::variable_type_traits<value_t>::is_container && rv::nodes::make_variable_type_descriptor<value_t>().supports_default_editor) {
				if (!_handled && rv::nodes::variable_slot_matches_type<value_t>(_slot)) {
					_changed = render_variable_default_editor_impl<value_t>("Default", _slot.default_json);
					_handled = true;
				}
			}
		}.template operator()<std::tuple_element_t<_indices, local_variable_types>>(), ...);
	}(std::make_index_sequence<std::tuple_size_v<local_variable_types>> {});

	if (!_handled)
		ImGui::TextDisabled("This CPU variable type has no inline editor yet.");
	return _changed;
}

bool render_variable_slot_editor(NodeGraph& _graph, rv::nodes::variable_slot_state& _slot) {
	bool _changed = false;
	_changed |= input_text_string("Name##variable_name", _slot.name);

	int _selected_type = 0;
	const auto& _descriptors = rv::nodes::variable_type_descriptors();
	for (size_t _type_index = 0; _type_index < _descriptors.size(); ++_type_index) {
		const auto& _descriptor = _descriptors[_type_index];
		if (_descriptor.type_hash == _slot.type_hash &&
			_descriptor.is_container == _slot.is_container) {
			_selected_type = static_cast<int>(_type_index);
			break;
		}
	}

	std::string _combo_items;
	for (const auto& _descriptor : _descriptors) {
		_combo_items += _descriptor.label;
		_combo_items.push_back('\0');
	}
	_combo_items.push_back('\0');
	if (ImGui::Combo("Type##variable_type", &_selected_type, _combo_items.c_str())) {
		const auto& _descriptor = _descriptors[static_cast<size_t>(_selected_type)];
		_slot.type_hash = _descriptor.type_hash;
		_slot.is_container = _descriptor.is_container;
		_slot.has_virtual_struct = false;
		_slot.virtual_struct_name.clear();
		_slot.virtual_struct_layout_fingerprint = 0;
		rv::nodes::clear_variable_slot_template(_slot);
		rv::nodes::reset_variable_slot_default(_slot);
		_changed = true;
	}

	if (_slot.type_hash == rv::detail::pin_type_hash<rv::resource_tags::vertex_buffer>()) {
		int _schema_id = -1;
		if (_slot.has_virtual_struct) {
			if (const auto* _schema = _graph.find_virtual_struct(_slot.virtual_struct_name, _slot.virtual_struct_layout_fingerprint); _schema != nullptr)
				_schema_id = _schema->id;
		}
		if (render_virtual_struct_selector(_graph, _schema_id, "Struct##variable_struct", "VariableStruct")) {
			if (const auto* _schema = _graph.find_virtual_struct(_schema_id); _schema != nullptr) {
				_slot.has_virtual_struct = true;
				_slot.virtual_struct_name = _schema->name;
				_slot.virtual_struct_layout_fingerprint = rv::nodes::virtual_struct_layout_fingerprint(*_schema);
			} else {
				_slot.has_virtual_struct = false;
				_slot.virtual_struct_name.clear();
				_slot.virtual_struct_layout_fingerprint = 0;
			}
			_changed = true;
		}
	}

	_changed |= render_pipeline_variable_template_selector(_graph, _slot);

	if (!is_resource_variable_slot(_slot) && !_slot.is_container) {
		bool _handled = false;
		[&]<size_t... _indices>(std::index_sequence<_indices...>) {
			([&]<typename value_t>() {
				if constexpr (!rv::nodes::variable_type_traits<value_t>::is_container && rv::nodes::make_variable_type_descriptor<value_t>().supports_default_editor) {
					if (!_handled && rv::nodes::variable_slot_matches_type<value_t>(_slot)) {
						_changed |= render_variable_default_editor_impl<value_t>("Default##variable_default", _slot.default_json);
						_handled = true;
					}
				}
			}.template operator()<std::tuple_element_t<_indices, local_variable_types>>(), ...);
		}(std::make_index_sequence<std::tuple_size_v<local_variable_types>> {});

		if (!_handled)
			_changed |= render_variable_default_editor(_slot);
	} else
		_changed |= render_variable_default_editor(_slot);

	if (!_slot.status.empty())
		ImGui::TextDisabled("%s", _slot.status.c_str());

	if (_changed) {
		rv::nodes::refresh_dynamic_variable_nodes(_graph);
		_graph.notify_graph_dirty();
	}
	return _changed;
}

bool render_variable_slot_selector(NodeGraph& _graph, int& _variable_id, const char* _combo_label) {
	bool _changed = false;
	const char* _current_label = "None";
	if (const auto* _slot = _graph.find_variable_slot(_variable_id); _slot != nullptr)
		_current_label = _slot->name.c_str();

	if (ImGui::BeginCombo(_combo_label, _current_label)) {
		if (ImGui::Selectable("None", _variable_id == -1)) {
			_variable_id = -1;
			_changed = true;
		}
		for (const auto& _slot : _graph.variable_slots) {
			const bool _selected = _slot.id == _variable_id;
			if (ImGui::Selectable(_slot.name.c_str(), _selected)) {
				_variable_id = _slot.id;
				_changed = true;
			}
			if (_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return _changed;
}

bool render_virtual_struct_editor(NodeGraph& _graph, rv::nodes::virtual_struct_schema_state& _state) {
	bool _changed = false;
	const auto& _descriptors = rv::nodes::virtual_struct_type_descriptors();
	_changed |= input_text_string("Name", _state.name);
	if (_state.fields.empty())
		ImGui::TextDisabled("No fields yet. Add one below.");
	for (size_t _index = 0; _index < _state.fields.size(); ++_index) {
		ImGui::PushID(static_cast<int>(_index));
		_changed |= input_text_string("Field", _state.fields[_index].name);
		_changed |= input_text_string("Semantic", _state.fields[_index].semantic);
		int _selected_type = -1;
		const char* _current_type_label = "Unknown";
		for (size_t _type_index = 0; _type_index < _descriptors.size(); ++_type_index) {
			if (_descriptors[_type_index].type_hash == _state.fields[_index].type_hash) {
				_selected_type = static_cast<int>(_type_index);
				_current_type_label = _descriptors[_type_index].name.data();
				break;
			}
		}
		if (ImGui::BeginCombo("Type", _current_type_label)) {
			for (size_t _type_index = 0; _type_index < _descriptors.size(); ++_type_index) {
				const bool _selected = static_cast<int>(_type_index) == _selected_type;
				if (ImGui::Selectable(_descriptors[_type_index].name.data(), _selected)) {
					_state.fields[_index].type_hash = _descriptors[_type_index].type_hash;
					_selected_type = static_cast<int>(_type_index);
					_changed = true;
				}
				if (_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ImGui::ArrowButton("Move Up", ImGuiDir_Up) && _index > 0) {
			std::swap(_state.fields[_index - 1], _state.fields[_index]);
			_changed = true;
		}
		ImGui::SameLine();
		if (ImGui::ArrowButton("Move Down", ImGuiDir_Down) && _index + 1 < _state.fields.size()) {
			std::swap(_state.fields[_index], _state.fields[_index + 1]);
			_changed = true;
		}
		ImGui::SameLine();
		if (_state.fields.size() > 1 && ImGui::Button("Remove")) {
			_state.fields.erase(_state.fields.begin() + static_cast<std::ptrdiff_t>(_index));
			_changed = true;
			ImGui::PopID();
			break;
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (ImGui::Button("Add Field")) {
		_state.fields.push_back({
			.name = std::string("field") + std::to_string(_state.fields.size()),
			.semantic = std::string("FIELD") + std::to_string(_state.fields.size()),
			.type_hash = rv::reflect<mars::vector3<float>>::type_hash
		});
		_changed = true;
	}

	if (ImGui::ColorEdit4("Pin Color", &_state.color.x))
		_changed = true;

	if (_changed)
		_graph.notify_graph_dirty();
	return _changed;
}

bool render_virtual_struct_selector(NodeGraph& _graph, int& _schema_id, const char* _combo_label, const char* _create_label_prefix) {
	bool _changed = false;
	const char* _current_label = "None";
	if (const auto* _schema = _graph.find_virtual_struct(_schema_id); _schema != nullptr)
		_current_label = _schema->name.c_str();

	if (ImGui::BeginCombo(_combo_label, _current_label)) {
		if (ImGui::Selectable("None", _schema_id == -1)) {
			_schema_id = -1;
			_changed = true;
		}
		for (const auto& _schema : _graph.virtual_structs) {
			const bool _selected = _schema.id == _schema_id;
			if (ImGui::Selectable(_schema.name.c_str(), _selected)) {
				_schema_id = _schema.id;
				_changed = true;
			}
			if (_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	std::string _button_label = std::string("Create New##") + _combo_label;
	if (ImGui::Button(_button_label.c_str(), { -1.0f, 0.0f })) {
		const int _suffix = static_cast<int>(_graph.virtual_structs.size());
		_schema_id = rv::nodes::ensure_default_virtual_struct(_graph, std::string(_create_label_prefix) + std::to_string(_suffix));
		_changed = true;
	}

	if (_changed)
		_graph.notify_graph_dirty();
	return _changed;
}

} // namespace rv::ui
