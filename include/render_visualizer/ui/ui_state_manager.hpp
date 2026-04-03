#pragma once

#include <imgui.h>

#include <render_visualizer/runtime/graph_builder.hpp>
#include <render_visualizer/ui/selection_manager.hpp>

#include <mars/graphics/functional/window.hpp>
#include <mars/math/vector2.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace rv {

class node_registry;

class ui_state_manager {
public:
	ui_state_manager(graph_builder* _builder, const node_registry* _registry, selection_manager* _selection);

	void set_builder(graph_builder* _builder);

	static void on_window_mouse_change(mars::window&, const mars::window_mouse_state& _mouse_state, ui_state_manager& _manager);
	static void on_window_mouse_motion(mars::window&, const mars::window_mouse_state& _mouse_state, ui_state_manager& _manager);
	static void on_window_mouse_wheel(mars::window&, const mars::window_mouse_wheel_state& _mouse_wheel_state, ui_state_manager& _manager);

	void on_mouse_change(const mars::window_mouse_state& _mouse_state);
	void on_mouse_motion(const mars::window_mouse_state& _mouse_state);
	void on_mouse_wheel(const mars::window_mouse_wheel_state& _mouse_wheel_state);

	static void on_selection_manager_changed(const mars::meta::type_erased_ptr& _selection, ui_state_manager& _manager);

	struct render_result {
		std::optional<std::pair<std::size_t, bool>> create_variable_node = std::nullopt;
		mars::vector2<float> drop_position = {};
	};

	void render_links();
	render_result render();
	void open_variable_drop_menu(std::size_t _variable_index, const mars::vector2<float>& _screen_position);

private:
	struct hit_pin_result {
		std::uint16_t node_id = 0;
		std::string pin_name = {};
		bool is_output = false;
		ImU32 color = 0;
	};

	void select_node(graph_builder_node* _node) const;
	graph_builder_node* hit_node(const mars::vector2<float>& _screen_position) const;
	std::optional<hit_pin_result> hit_pin(const mars::vector2<float>& _screen_position) const;

	static constexpr const char* m_popup_id = "rv_blackboard_context_menu";

	graph_builder* m_builder = nullptr;
	const node_registry* m_registry = nullptr;
	selection_manager* m_selection = nullptr;
	bool m_pending_right_click_release = false;
	bool m_popup_open_requested = false;
	mars::vector2<float> m_pending_screen_position = {};
	mars::vector2<float> m_popup_position = {};
	mars::vector2<float> m_mouse_screen_position = {};
	graph_builder_node* m_dragged_node = nullptr;
	mars::vector2<float> m_drag_offset = {};
	bool m_camera_drag_active = false;
	bool m_link_active = false;
	hit_pin_result m_link_start = {};

	bool m_variable_popup_open_requested = false;
	std::size_t m_dropped_variable_index = 0;
	mars::vector2<float> m_dropped_variable_position = {};
};

} // namespace rv
