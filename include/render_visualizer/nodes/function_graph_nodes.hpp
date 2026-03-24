#pragma once

#include <render_visualizer/execution_context.hpp>
#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/variable_nodes.hpp>

namespace rv::nodes {

struct setup_start_node;
using setup_start_node_tag = setup_start_node;
struct render_start_node;
using render_start_node_tag = render_start_node;
struct function_inputs_node;
using function_inputs_node_tag = function_inputs_node;
struct function_outputs_node;
using function_outputs_node_tag = function_outputs_node;
struct call_function_node;
using call_function_node_tag = call_function_node;

struct call_function_node_state {
	int function_id = -1;
};

graph_function_signature_pin make_function_signature_pin_from_descriptor(const variable_type_descriptor& descriptor, std::string label = "value");
const variable_type_descriptor* find_function_signature_descriptor(const graph_function_signature_pin& pin);
void apply_function_signature_pin(NE_Pin& pin, const graph_function_signature_pin& signature_pin);
std::vector<NE_Pin> make_function_signature_inputs(const std::vector<graph_function_signature_pin>& signature);
std::vector<NE_Pin> make_function_signature_outputs(const std::vector<graph_function_signature_pin>& signature);
const NE_Node* find_function_node_by_type(const NodeGraph& graph, int function_id, size_t node_type);
NE_Node* find_function_node_by_type(NodeGraph& graph, int function_id, size_t node_type);

void sync_function_inputs_node(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
void sync_function_outputs_node(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);
void sync_call_function_node(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node);

struct [[=mars::meta::display("Setup Start"), =rv::node::pin_flow(enum_type::output_only)]] setup_start_node {
	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context&, NE_Node&, std::string&);
	[[=rv::node::validate()]] static bool validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error);
};

struct [[=mars::meta::display("Render Start"), =rv::node::pin_flow(enum_type::output_only)]] render_start_node {
	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context&, NE_Node&, std::string&);
	[[=rv::node::validate()]] static bool validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error);
};

struct [[=mars::meta::display("Function Inputs"), =rv::node::pin_flow(enum_type::output_only)]] function_inputs_node {
	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node);
	[[=rv::node::pure()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::validate()]] static bool validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error);
};

struct [[=mars::meta::display("Function Outputs"), =rv::node::pin_flow(enum_type::input_only), =rv::node::function_outputs()]] function_outputs_node {
	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::validate()]] static bool validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error);
};

struct [[=mars::meta::display("Call Function")]] call_function_node {
	using custom_state_t = call_function_node_state;

	[[=rv::node::configure()]] static void configure(NodeTypeInfo& info);
	[[=rv::node::refresh()]] static void refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node);
	[[=rv::node::editor()]] static void edit(NodeGraph& graph, NE_Node& node, call_function_node_state& state);
	[[=rv::node::callable()]] static bool execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error);
	[[=rv::node::validate()]] static bool validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error);
};

void ensure_function_boundary_nodes(NodeGraph& graph);
void refresh_dynamic_function_graph_nodes(NodeGraph& graph);

inline const NodeRegistry::node_auto_registrar setup_start_node_registration(
	NodeRegistry::make_reflected_registration<setup_start_node>({ .is_permanent = true, .show_in_spawn_menu = false })
);
inline const NodeRegistry::node_auto_registrar render_start_node_registration(
	NodeRegistry::make_reflected_registration<render_start_node>({ .is_permanent = true, .show_in_spawn_menu = false })
);
inline const NodeRegistry::node_auto_registrar function_inputs_node_registration(
	NodeRegistry::make_reflected_registration<function_inputs_node>({ .is_permanent = true, .show_in_spawn_menu = false })
);
inline const NodeRegistry::node_auto_registrar function_outputs_node_registration(
	NodeRegistry::make_reflected_registration<function_outputs_node>({ .is_permanent = true, .show_in_spawn_menu = false })
);
inline const NodeRegistry::node_auto_registrar call_function_node_registration(
	NodeRegistry::make_reflected_registration<call_function_node>()
);

} // namespace rv::nodes
