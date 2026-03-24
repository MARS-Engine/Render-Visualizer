#pragma once

#include <render_visualizer/nodes/command_nodes.hpp>
#include <render_visualizer/nodes/camera_node.hpp>
#include <render_visualizer/nodes/dynamic_uniform_node.hpp>
#include <render_visualizer/nodes/framebuffer_node.hpp>
#include <render_visualizer/nodes/function_nodes.hpp>
#include <render_visualizer/nodes/function_graph_nodes.hpp>
#include <render_visualizer/nodes/gltf_node.hpp>
#include <render_visualizer/nodes/graphics_pipeline_node.hpp>
#include <render_visualizer/nodes/material_bindings_node.hpp>
#include <render_visualizer/nodes/matrix_node.hpp>
#include <render_visualizer/nodes/render_pass_node.hpp>
#include <render_visualizer/nodes/sequence_node.hpp>
#include <render_visualizer/nodes/shader_module_node.hpp>
#include <render_visualizer/nodes/shader_interface_support.hpp>
#include <render_visualizer/nodes/simple_nodes.hpp>
#include <render_visualizer/nodes/texture_resource_node.hpp>
#include <render_visualizer/nodes/uniform_data_node.hpp>
#include <render_visualizer/nodes/variable_nodes.hpp>
#include <render_visualizer/nodes/vertex_buffer_node.hpp>
#include <render_visualizer/nodes/virtual_struct.hpp>

namespace rv::nodes {

void refresh_dynamic_nodes(NodeGraph& graph);

} // namespace rv::nodes
