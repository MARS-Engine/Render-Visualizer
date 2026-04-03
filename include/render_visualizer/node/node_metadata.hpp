#pragma once

#include <render_visualizer/node/operations.hpp>

#include <cstddef>

namespace rv {

struct node_metadata {
	bool pure = false;
	std::size_t instance_size = 0;
	std::size_t instance_alignment = alignof(std::max_align_t);
	node_operations operations = {};
};

} // namespace rv