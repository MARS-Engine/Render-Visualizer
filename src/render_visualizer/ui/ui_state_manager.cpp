#include <render_visualizer/ui/ui_state_manager.hpp>

#include <render_visualizer/blackboard.hpp>
#include <render_visualizer/ui/ui_render.hpp>
#include <render_visualizer/utils/bezier.hpp>

#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace rv {

namespace {

struct resolved_pin {
	mars::vector2<float> position = {};
	ImU32 color = 0;
	std::size_t type_hash = 0;
};

bool point_in_circle(const mars::vector2<float>& _point, const mars::vector2<float>& _center, float _radius) {
	const float dx = _point.x - _center.x;
	const float dy = _point.y - _center.y;
	return dx * dx + dy * dy <= _radius * _radius;
}

std::optional<std::size_t> pin_index_find(const std::vector<pin_draw_data>& _pins, std::string_view _pin_name) {
	const auto pin_it = std::ranges::find_if(_pins, [&](const pin_draw_data& _pin) {
		return _pin.name == _pin_name;
	});
	if (pin_it == _pins.end())
		return std::nullopt;

	return static_cast<std::size_t>(std::ranges::distance(_pins.begin(), pin_it));
}

std::optional<resolved_pin> pin_resolve(const graph_builder_node& _node, std::string_view _pin_name, bool _is_output) {
	std::vector<pin_draw_data> inputs;
	std::vector<pin_draw_data> outputs;
	graph_builder::collect_pins(_node, inputs, outputs);
	std::erase_if(inputs, [](const pin_draw_data& _p) { return _p.kind == pin_kind::property; });
	std::erase_if(outputs, [](const pin_draw_data& _p) { return _p.kind == pin_kind::property; });

	const std::vector<pin_draw_data>& pins = _is_output ? outputs : inputs;
	const std::optional<std::size_t> pin_index = pin_index_find(pins, _pin_name);
	if (!pin_index.has_value())
		return std::nullopt;

	return resolved_pin {
		.position = calculate_pin_position(_node, *pin_index, _is_output),
		.color = mars_to_imgui_colour(pins[*pin_index].colour),
		.type_hash = pins[*pin_index].type_hash
	};
}

} // namespace

ui_state_manager::ui_state_manager(graph_builder* _builder, const node_registry* _registry, selection_manager* _selection) : m_builder(_builder), m_registry(_registry), m_selection(_selection) {
	if (m_selection != nullptr) {
		m_selection->listen<&selection_event::on_selection_changed, &ui_state_manager::on_selection_manager_changed>(*this);
	}
}

void ui_state_manager::on_selection_manager_changed(const mars::meta::type_erased_ptr&, ui_state_manager& _manager) {
	if (_manager.m_builder && (_manager.m_selection == nullptr || _manager.m_selection->selected_node() == nullptr)) {
		_manager.m_builder->clear_selection();
	}
}

void ui_state_manager::set_builder(graph_builder* _builder) {
	m_dragged_node = nullptr;
	m_link_active = false;
	m_builder = _builder;
}

void ui_state_manager::on_window_mouse_change(mars::window&, const mars::window_mouse_state& _mouse_state, ui_state_manager& _manager) {
	_manager.on_mouse_change(_mouse_state);
}

void ui_state_manager::on_window_mouse_motion(mars::window&, const mars::window_mouse_state& _mouse_state, ui_state_manager& _manager) {
	_manager.on_mouse_motion(_mouse_state);
}

void ui_state_manager::on_window_mouse_wheel(mars::window&, const mars::window_mouse_wheel_state& _mouse_wheel_state, ui_state_manager& _manager) {
	_manager.on_mouse_wheel(_mouse_wheel_state);
}

void ui_state_manager::on_mouse_change(const mars::window_mouse_state& _mouse_state) {
	const mars::vector2<float> screen_position = { static_cast<float>(_mouse_state.position.x), static_cast<float>(_mouse_state.position.y) };
	m_mouse_screen_position = screen_position;
	const bool over_ui = ui_contains_point(screen_position);

	if (!_mouse_state.previous_buttons.left_button_down && _mouse_state.buttons.left_button_down && !over_ui) {
		if (const std::optional<hit_pin_result> pin_hit = hit_pin(screen_position); pin_hit.has_value()) {
			select_node(m_builder != nullptr ? m_builder->find_node(pin_hit->node_id) : nullptr);
			m_link_active = true;
			m_link_start = *pin_hit;
			m_dragged_node = nullptr;
		}
		else {
			m_dragged_node = hit_node(screen_position);
			select_node(m_dragged_node);
			if (m_dragged_node != nullptr) {
				const mars::vector2<float> node_position = blackboard_canvas_to_screen({ m_dragged_node->position.x, m_dragged_node->position.y });
				const mars::vector2<float> canvas_position = blackboard_screen_to_canvas(screen_position);
				if (screen_position.y <= node_position.y + node_title_height())
					m_drag_offset = {
						canvas_position.x - m_dragged_node->position.x,
						canvas_position.y - m_dragged_node->position.y
					};
				else
					m_dragged_node = nullptr;
			}
		}
	}

	if (!_mouse_state.previous_buttons.middle_button_down && _mouse_state.buttons.middle_button_down && !over_ui) {
		m_camera_drag_active = true;
		m_dragged_node = nullptr;
	}

	if (_mouse_state.previous_buttons.left_button_down && !_mouse_state.buttons.left_button_down) {
		if (m_link_active && !over_ui) {
			const std::optional<hit_pin_result> pin_hit = hit_pin(screen_position);
			if (pin_hit.has_value() &&
				pin_hit->is_output != m_link_start.is_output &&
				(pin_hit->node_id != m_link_start.node_id || pin_hit->pin_name != m_link_start.pin_name)) {
				const hit_pin_result& from_pin = m_link_start.is_output ? m_link_start : *pin_hit;
				const hit_pin_result& to_pin = m_link_start.is_output ? *pin_hit : m_link_start;
				if (m_builder != nullptr)
					m_builder->add_link(from_pin.node_id, from_pin.pin_name, to_pin.node_id, to_pin.pin_name);
			}
		}
		m_link_active = false;
		m_dragged_node = nullptr;
	}

	if (_mouse_state.previous_buttons.middle_button_down && !_mouse_state.buttons.middle_button_down)
		m_camera_drag_active = false;

	if (_mouse_state.previous_buttons.right_button_down && !_mouse_state.buttons.right_button_down && !over_ui) {
		m_pending_right_click_release = true;
		m_pending_screen_position = screen_position;
	}
}

void ui_state_manager::select_node(graph_builder_node* _node) const {
	if (m_builder == nullptr)
		return;

	for (graph_builder_node& node : *m_builder)
		node.selected = &node == _node;
	
	if (m_selection) {
		if (_node)
			m_selection->select_node(_node);
		else
			m_selection->clear_selection();
	}
}

void ui_state_manager::on_mouse_motion(const mars::window_mouse_state& _mouse_state) {
	m_mouse_screen_position = { static_cast<float>(_mouse_state.position.x), static_cast<float>(_mouse_state.position.y) };

	if (_mouse_state.buttons.middle_button_down && m_camera_drag_active) {
		blackboard_camera_move({
			static_cast<float>(_mouse_state.position.x) - static_cast<float>(_mouse_state.previous_position.x),
			static_cast<float>(_mouse_state.position.y) - static_cast<float>(_mouse_state.previous_position.y)
		});
		return;
	}

	if (!_mouse_state.buttons.left_button_down || m_dragged_node == nullptr)
		return;

	const mars::vector2<float> canvas_position = blackboard_screen_to_canvas(m_mouse_screen_position);
	m_dragged_node->position = {
		canvas_position.x - m_drag_offset.x,
		canvas_position.y - m_drag_offset.y
	};
}

void ui_state_manager::on_mouse_wheel(const mars::window_mouse_wheel_state& _mouse_wheel_state) {
	const mars::vector2<float> screen_position = { static_cast<float>(_mouse_wheel_state.position.x), static_cast<float>(_mouse_wheel_state.position.y) };
	if (ui_contains_point(screen_position))
		return;

	blackboard_zoom_at(_mouse_wheel_state.delta, screen_position);
}

void ui_state_manager::render_links() {
	if (m_builder == nullptr)
		return;

	for (const graph_builder_node& node : *m_builder) {
		for (const graph_builder_pin_links& pin_links : node.links) {
			const std::optional<resolved_pin> from_pin = pin_resolve(node, pin_links.name, true);
			if (!from_pin.has_value())
				continue;

			for (const graph_builder_pin_link_target& target : pin_links.targets) {
				const graph_builder_node* target_node = m_builder->find_node(target.node_id);
				if (target_node == nullptr)
					continue;

				const std::optional<resolved_pin> to_pin = pin_resolve(*target_node, target.pin_name, false);
				if (!to_pin.has_value())
					continue;

				draw_bezier(calculate_bezier_curve(from_pin->position, to_pin->position, blackboard_zoom()), from_pin->color);
			}
		}
	}

	if (!m_link_active)
		return;

	const graph_builder_node* start_node = m_builder->find_node(m_link_start.node_id);
	if (start_node == nullptr)
		return;

	const std::optional<resolved_pin> start_pin = pin_resolve(*start_node, m_link_start.pin_name, m_link_start.is_output);
	if (!start_pin.has_value())
		return;

	const mars::vector2<float> begin_position = m_link_start.is_output ? start_pin->position : m_mouse_screen_position;
	const mars::vector2<float> end_position = m_link_start.is_output ? m_mouse_screen_position : start_pin->position;
	draw_bezier(calculate_bezier_curve(begin_position, end_position, blackboard_zoom()), m_link_start.color, 2.0f);
}

void ui_state_manager::open_variable_drop_menu(std::size_t _variable_index, const mars::vector2<float>& _screen_position) {
	m_dropped_variable_index = _variable_index;
	m_dropped_variable_position = _screen_position;
	m_variable_popup_open_requested = true;
}

ui_state_manager::render_result ui_state_manager::render() {
	render_result result = {};

	if (m_pending_right_click_release) {
		m_pending_right_click_release = false;
		if (!m_link_active && hit_node(m_pending_screen_position) == nullptr) {
			m_popup_open_requested = true;
			m_popup_position = m_pending_screen_position;
		}
	}

	if (m_popup_open_requested) {
		ImGui::SetNextWindowPos({ m_popup_position.x, m_popup_position.y }, ImGuiCond_Always);
		ImGui::OpenPopup(m_popup_id);
		m_popup_open_requested = false;
	}

	if (ImGui::BeginPopup(m_popup_id, ImGuiWindowFlags_NoMove)) {
		if (m_registry == nullptr || m_registry->registered_nodes().empty())
			ImGui::TextDisabled("No registered nodes");
		else {
			for (const node_registry_entry& entry : m_registry->registered_nodes()) {
				if (entry.hidden) continue;
				if (ImGui::Selectable(entry.name.data()) && m_builder != nullptr) {
					const mars::vector2<float> canvas_position = blackboard_screen_to_canvas(m_popup_position);
					m_builder->add(entry, {
						canvas_position.x,
						canvas_position.y
					});
					ImGui::CloseCurrentPopup();
				}
			}
		}
		ImGui::EndPopup();
	}

	if (m_variable_popup_open_requested) {
		ImGui::SetNextWindowPos({ m_dropped_variable_position.x, m_dropped_variable_position.y }, ImGuiCond_Always);
		ImGui::OpenPopup("rv_variable_drop_menu", ImGuiPopupFlags_None);
		m_variable_popup_open_requested = false;
	}

	if (ImGui::BeginPopup("rv_variable_drop_menu")) {
		if (ImGui::Selectable("Get")) {
			result.create_variable_node = { m_dropped_variable_index, false };
			result.drop_position = m_dropped_variable_position;
			ImGui::CloseCurrentPopup();
		}
		if (ImGui::Selectable("Set")) {
			result.create_variable_node = { m_dropped_variable_index, true };
			result.drop_position = m_dropped_variable_position;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	return result;
}

graph_builder_node* ui_state_manager::hit_node(const mars::vector2<float>& _screen_position) const {
	if (m_builder == nullptr)
		return nullptr;

	for (auto node_it = m_builder->end(); node_it != m_builder->begin();) {
		--node_it;
		const mars::vector2<float> node_size = calculate_node_size(*node_it);
		const mars::vector2<float> node_min = blackboard_canvas_to_screen({ node_it->position.x, node_it->position.y });
		const mars::vector2<float> node_max = { node_min.x + node_size.x, node_min.y + node_size.y };
		if (_screen_position.x >= node_min.x && _screen_position.x <= node_max.x &&
			_screen_position.y >= node_min.y && _screen_position.y <= node_max.y)
			return &*node_it;
	}

	return nullptr;
}

std::optional<ui_state_manager::hit_pin_result> ui_state_manager::hit_pin(const mars::vector2<float>& _screen_position) const {
	if (m_builder == nullptr)
		return std::nullopt;

	const float hit_radius = pin_radius() * 1.75f;

	for (auto node_it = m_builder->end(); node_it != m_builder->begin();) {
		--node_it;

		std::vector<pin_draw_data> inputs;
		std::vector<pin_draw_data> outputs;
		graph_builder::collect_pins(*node_it, inputs, outputs);
		std::erase_if(inputs, [](const pin_draw_data& _p) { return _p.kind == pin_kind::property; });
		std::erase_if(outputs, [](const pin_draw_data& _p) { return _p.kind == pin_kind::property; });

		for (std::size_t pin_index = 0; pin_index < outputs.size(); ++pin_index) {
			const mars::vector2<float> pin_position = calculate_pin_position(*node_it, pin_index, true);
			if (point_in_circle(_screen_position, pin_position, hit_radius))
				return hit_pin_result {
					.node_id = node_it->id,
					.pin_name = std::string(outputs[pin_index].name),
					.is_output = true,
					.color = mars_to_imgui_colour(outputs[pin_index].colour)
				};
		}

		for (std::size_t pin_index = 0; pin_index < inputs.size(); ++pin_index) {
			const mars::vector2<float> pin_position = calculate_pin_position(*node_it, pin_index, false);
			if (point_in_circle(_screen_position, pin_position, hit_radius))
				return hit_pin_result {
					.node_id = node_it->id,
					.pin_name = std::string(inputs[pin_index].name),
					.is_output = false,
					.color = mars_to_imgui_colour(inputs[pin_index].colour)
				};
		}
	}

	return std::nullopt;
}

} // namespace rv
