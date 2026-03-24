#pragma once

#include <render_visualizer/node_graph.hpp>

#include <mars/graphics/backend/pipeline.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace rv::raster {

struct reflected_shader_resource {
	std::string label;
	size_t binding = 0;
	mars_pipeline_stage stage = MARS_PIPELINE_STAGE_FRAGMENT;
	graph_shader_resource_kind kind = graph_shader_resource_kind::uniform_value;
	size_t type_hash = 0;
};

struct compile_result {
	bool success = false;
	std::string diagnostics;
	std::vector<NE_Pin> input_pins;
	std::vector<NE_Pin> output_pins;
	std::vector<reflected_shader_resource> resources;
};

struct node_state {
	std::string source;
	std::string diagnostics = "Not compiled yet.";
	bool last_compile_success = false;
	std::vector<NE_Pin> last_input_pins;
	std::vector<NE_Pin> last_output_pins;
};

constexpr size_t node_type() {
	return mars::hash::detail::fnv1a64("rv::raster_node");
}

std::string default_source();
compile_result compile_source(std::string_view source);

} // namespace rv::raster
