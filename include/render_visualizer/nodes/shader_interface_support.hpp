#pragma once

#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/uniform_data_node.hpp>

#include <string>

namespace rv::nodes {

uniform_value_kind uniform_kind_from_type_hash(size_t type_hash);
NE_Pin make_shader_interface_slot_pin(const graph_shader_interface_slot& slot);
const char* shader_resource_kind_name(graph_shader_resource_kind kind);
const char* pipeline_stage_name(mars_pipeline_stage stage);
std::string shader_resource_value_type_name(graph_shader_resource_kind kind, size_t type_hash);
const NE_Node* resolve_pipeline_source_node(const NodeGraph& graph, const NE_Node& source_node);
const graph_shader_interface* resolved_pipeline_shader_interface(const NodeGraph& graph, const NE_Node& pipeline_node, std::string* error = nullptr);

} // namespace rv::nodes
