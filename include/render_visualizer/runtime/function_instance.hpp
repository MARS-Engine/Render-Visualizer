#pragma once

#include <render_visualizer/runtime/graph_builder.hpp>

#include <string>

namespace rv {

struct function_instance {
	std::string name;
	rv::graph_builder graph;
};

} // namespace rv
