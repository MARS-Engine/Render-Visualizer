#pragma once

#include <render_visualizer/ui/editor.hpp>

namespace rv::ui {

struct app_shell_context {
	editor_state& editor;
	const graph_runtime* runtime = nullptr;
	float fps = 0.0f;
	bool overview_open = false;
	bool overview_captured = false;
	bool right_click_signal = false;
	bool right_click_held = false;
};

editor_actions render_app_shell(app_shell_context& _context);

} // namespace rv::ui
