#pragma once

#include <imgui.h>

#include <render_visualizer/runtime/function_instance.hpp>
#include <render_visualizer/runtime/graph_builder.hpp>
#include <render_visualizer/type_reflection.hpp>
#include <render_visualizer/ui/selection_manager.hpp>

#include <mars/math/vector2.hpp>
#include <mars/math/vector3.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rv {

struct ui_render_result {
	bool start_requested = false;
	bool stop_requested = false;
	bool graph_inputs_changed = false;
	bool create_function_requested = false;
	bool create_variable_requested = false;
	std::optional<std::size_t> select_function_index = {};
	std::optional<std::size_t> delete_function_index = {};
	std::optional<std::size_t> delete_variable_index = {};
};

ui_render_result ui_render(const std::vector<std::unique_ptr<rv::function_instance>>& _functions, std::size_t _active_function_index, const std::vector<std::unique_ptr<rv::variable>>& _variables, selection_manager& _selection, graph_builder& _graph, bool _running);
bool ui_contains_point(const mars::vector2<float>& _screen_position);

} // namespace rv
