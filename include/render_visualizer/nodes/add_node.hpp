#pragma once

#include <render_visualizer/node/node_reflection.hpp>
#include <render_visualizer/node/node_registry.hpp>

namespace rv {

struct [[=rv::node_pure()]] add_node {
	[[=rv::input]] float a = 0.0f;
	[[=rv::input]] float b = 0.0f;

	[[=rv::output]] float result = 0.0f;

	[[=rv::execute]] void run() {
		result = a + b;
	}
};

} // namespace rv

template const rv::node_registry::node_auto_registrar rv::auto_register_node_v<rv::add_node>;