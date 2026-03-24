#pragma once

#include <render_visualizer/ui/editor.hpp>

namespace rv::ui {

bool render_runtime_panel(const runtime_panel_context& _context);
void render_runtime_overlay_contents(const graph_runtime& _runtime);
void render_runtime_overlay(const graph_runtime& _runtime);

} // namespace rv::ui
