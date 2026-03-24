#pragma once

#include <render_visualizer/node_graph.hpp>

#include <string>
#include <string_view>

namespace rv {

inline constexpr std::string_view default_graph_snapshot_path = "default.json";

bool save_graph_to_file(const NodeGraph& graph, std::string_view path = default_graph_snapshot_path, std::string* error = nullptr);
bool load_graph_from_file(NodeGraph& graph, std::string_view path = default_graph_snapshot_path, std::string* error = nullptr);

} // namespace rv
