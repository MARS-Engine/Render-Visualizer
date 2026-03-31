#pragma once

#include <imgui.h>

#include <render_visualizer/runtime/graph_builder.hpp>

#include <mars/math/vector2.hpp>

namespace rv {

struct ui_render_result {
	bool start_requested = false;
	bool stop_requested = false;
	bool graph_inputs_changed = false;
};

ui_render_result ui_render(graph_builder& _graph, bool _running);
bool ui_contains_point(const mars::vector2<float>& _screen_position);

} // namespace rv
