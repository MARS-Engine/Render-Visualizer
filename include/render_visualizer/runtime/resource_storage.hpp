#pragma once

#include <render_visualizer/runtime/render_state.hpp>

namespace rv::runtime_detail {

template <typename T>
struct owned_node_resource;

template <typename T>
struct owned_shared_resource {
	const mars::device* device = nullptr;
	T value {};

	~owned_shared_resource() {
		if (device != nullptr)
			value.destroy(*device);
	}
};

} // namespace rv::runtime_detail
