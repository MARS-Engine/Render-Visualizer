#pragma once

#include <render_visualizer/node/node_reflection.hpp>
#include <render_visualizer/node/node_registry.hpp>

#include <iostream>

namespace rv {

struct print_node {
	[[=rv::input]] float* value = nullptr;

	[[=rv::execute]] void run() {
		if (value) {
			std::cout << "Print Node: " << *value << std::endl;
		} else {
			std::cout << "Print Node: <null>" << std::endl;
		}
	}
};

} // namespace rv

template const rv::node_registry::node_auto_registrar rv::auto_register_node_v<rv::print_node>;