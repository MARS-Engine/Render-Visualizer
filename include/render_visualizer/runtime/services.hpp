#pragma once

#include <render_visualizer/runtime/resource_storage.hpp>
#include <render_visualizer/runtime/frame.hpp>

namespace rv {

class graph_runtime;

struct graph_services {
	NodeGraph* graph = nullptr;
	const mars::device* device = nullptr;
	graph_runtime* runtime = nullptr;
	size_t swapchain_size = 1;
	mars::vector2<size_t> frame_size = {};
};

graph_services* require_build_services(graph_build_context& ctx, std::string& error);

namespace runtime_detail {

template <typename T>
struct owned_node_resource {
	graph_runtime* runtime = nullptr;
	int node_id = -1;
	size_t node_type = 0;
	T value {};

	~owned_node_resource();
};

} // namespace runtime_detail

} // namespace rv
