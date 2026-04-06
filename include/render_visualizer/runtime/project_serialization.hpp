#pragma once

#include <render_visualizer/node/node_registry.hpp>
#include <render_visualizer/runtime/frame_executor.hpp>
#include <render_visualizer/type_registry.hpp>

#include <string>
#include <string_view>

namespace rv {

std::string save_project_json(const frame_executor& _project);
bool load_project_json(frame_executor& _project, const node_registry& _node_registry, const type_registry& _type_registry, std::string_view _json, std::string& _error_message);

} // namespace rv
