#pragma once

#include <imgui.h>

#include <render_visualizer/node_graph.hpp>

#include <array>
#include <utility>
#include <vector>

namespace rv::ui {

struct editor_state {
	explicit editor_state(NodeGraph& _graph) : graph(_graph) {}

	enum class sidebar_selection_kind {
		none,
		function,
		virtual_struct,
		shader_module,
		texture_slot,
		variable
	};

	NodeGraph& graph;
	mars::vector2<float> scroll = { 0.0f, 0.0f };
	float zoom = 1.0f;
	ImFont* font = nullptr;
	float font_size = 13.0f;
	struct selection_state {
		int node_id = -1;
		std::vector<int> node_ids;
		sidebar_selection_kind sidebar_kind = sidebar_selection_kind::none;
		int sidebar_id = -1;
	} selection;

	struct drag_state {
		int node_id = -1;
		mars::vector2<float> offset = {};
		mars::vector2<float> anchor_canvas = {};
		std::vector<std::pair<int, mars::vector2<float>>> group_origins;
	} drag;

	struct marquee_state {
		bool active = false;
		bool additive = false;
		bool moved = false;
		mars::vector2<float> origin_screen = {};
		mars::vector2<float> current_screen = {};
	} marquee;

	struct mouse_state {
		bool panning = false;
		bool right_click_signal = false;
		bool right_click_held = false;
		bool right_click_was_down = false;
		bool context_click_armed = false;
		mars::vector2<float> context_click_origin = {};
	} mouse;

	struct link_state {
		bool active = false;
		int node_id = -1;
		int pin_id = -1;
		bool is_output = false;
		NE_PinKind pin_kind = NE_PinKind::data;
	} link;

	struct spawn_menu_state {
		bool open = false;
		bool request_open = false;
		bool from_link = false;
		int link_node_id = -1;
		int link_pin_id = -1;
		bool link_is_output = false;
		NE_PinKind link_pin_kind = NE_PinKind::data;
		bool focus_search = false;
		mars::vector2<float> screen_pos = {};
		mars::vector2<float> canvas_pos = {};
		std::array<char, 128> search = {};
	} spawn_menu;

	struct variable_drop_state {
		bool request_open = false;
		int pending_id = -1;
		mars::vector2<float> screen_pos = {};
		mars::vector2<float> canvas_pos = {};
	} variable_drop;

	struct pin_menu_state {
		bool request_open = false;
		int node_id = -1;
		int pin_id = -1;
		bool is_output = false;
		mars::vector2<float> screen_pos = {};
	} pin_menu;

	bool request_save = false;
	bool request_load = false;
	float left_sidebar_width = 280.0f;
	float right_sidebar_width = 280.0f;
	float right_panel_top_height = 340.0f;
};

} // namespace rv::ui
