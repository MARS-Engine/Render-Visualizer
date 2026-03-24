#pragma once

#include <imgui.h>

#include <render_visualizer/ui/editor_state.hpp>

namespace rv {

class graph_runtime;

}

namespace rv::ui {

struct editor_actions {
	bool save_requested = false;
	bool load_requested = false;
	bool runtime_toggle_requested = false;
	bool overview_toggle_requested = false;
};

struct runtime_panel_context {
	const graph_runtime* runtime = nullptr;
	bool overview_open = false;
	bool overview_captured = false;
};

void clear_selection(editor_state& _state);
void render_canvas(editor_state& _state, const char* _id, mars::vector2<float> _size = { 0.0f, 0.0f });
editor_actions render_editor(editor_state& _state, const runtime_panel_context& _runtime_panel, const char* _id, mars::vector2<float> _size = { 0.0f, 0.0f });

} // namespace rv::ui
