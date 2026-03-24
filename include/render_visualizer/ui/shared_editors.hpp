#pragma once

#include <render_visualizer/nodes/function_graph_nodes.hpp>
#include <render_visualizer/nodes/texture_resource_node.hpp>
#include <render_visualizer/nodes/variable_support.hpp>
#include <render_visualizer/nodes/virtual_struct.hpp>
#include <render_visualizer/ui/widgets.hpp>

namespace rv::ui {

bool render_function_signature_editor(std::vector<graph_function_signature_pin>& _pins, const char* _label_prefix);
bool render_texture_slot_editor(NodeGraph& _graph, nodes::texture_slot_state& _slot);
bool render_texture_slot_selector(NodeGraph& _graph, int& _slot_id, const char* _combo_label, const char* _create_label_prefix = "Texture");
bool render_pipeline_variable_template_selector(NodeGraph& _graph, nodes::variable_slot_state& _slot);
bool render_variable_default_editor(nodes::variable_slot_state& _slot);
bool render_variable_slot_editor(NodeGraph& _graph, nodes::variable_slot_state& _slot);
bool render_variable_slot_selector(NodeGraph& _graph, int& _variable_id, const char* _combo_label);
bool render_virtual_struct_editor(NodeGraph& _graph, nodes::virtual_struct_schema_state& _state);
bool render_virtual_struct_selector(NodeGraph& _graph, int& _schema_id, const char* _combo_label, const char* _create_label_prefix = "Struct");

} // namespace rv::ui

namespace rv::nodes {

using rv::ui::render_function_signature_editor;
using rv::ui::render_pipeline_variable_template_selector;
using rv::ui::render_texture_slot_editor;
using rv::ui::render_texture_slot_selector;
using rv::ui::render_variable_default_editor;
using rv::ui::render_variable_slot_editor;
using rv::ui::render_variable_slot_selector;
using rv::ui::render_virtual_struct_editor;
using rv::ui::render_virtual_struct_selector;

} // namespace rv::nodes
