#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/function_graph_nodes.hpp>

#include <imgui.h>

namespace rv::nodes {

namespace {

NE_Pin make_exec_pin(std::string label) {
	NE_Pin pin {};
	pin.label = std::move(label);
	pin.display_label = pin.label;
	pin.kind = NE_PinKind::exec;
	return pin;
}

} // namespace

graph_function_signature_pin make_function_signature_pin_from_descriptor(const variable_type_descriptor& descriptor, std::string label) {
	graph_function_signature_pin pin;
	pin.label = std::move(label);
	pin.type_hash = descriptor.type_hash;
	pin.is_container = descriptor.is_container;
	return pin;
}

const variable_type_descriptor* find_function_signature_descriptor(const graph_function_signature_pin& pin) {
	for (const auto& descriptor : variable_type_descriptors()) {
		if (descriptor.type_hash == pin.type_hash &&
			descriptor.is_container == pin.is_container) {
			return &descriptor;
		}
	}
	return nullptr;
}

void apply_function_signature_pin(NE_Pin& pin, const graph_function_signature_pin& signature_pin) {
	rv::detail::copy_matching_fields(pin, signature_pin);
}

std::vector<NE_Pin> make_function_signature_inputs(const std::vector<graph_function_signature_pin>& signature) {
	std::vector<NE_Pin> pins;
	pins.reserve(signature.size());
	for (const auto& entry : signature) {
		NE_Pin pin;
		apply_function_signature_pin(pin, entry);
		pin.required = true;
		pins.push_back(std::move(pin));
	}
	return pins;
}

std::vector<NE_Pin> make_function_signature_outputs(const std::vector<graph_function_signature_pin>& signature) {
	return make_function_signature_inputs(signature);
}

const NE_Node* find_function_node_by_type(const NodeGraph& graph, int function_id, size_t node_type) {
	for (const auto* node : graph.nodes_in_function(function_id)) {
		if (node->type == node_type)
			return node;
	}
	return nullptr;
}

NE_Node* find_function_node_by_type(NodeGraph& graph, int function_id, size_t node_type) {
	for (auto* node : graph.nodes_in_function(function_id)) {
		if (node->type == node_type)
			return node;
	}
	return nullptr;
}

void sync_function_inputs_node(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	const graph_function_definition* function = graph.find_function(node.function_id);
	std::vector<NE_Pin> generated_outputs;
	if (function != nullptr)
		generated_outputs = make_function_signature_outputs(function->inputs);
	if (equivalent_pin_layout(node.generated_outputs, generated_outputs))
		return;
	graph.replace_generated_pins(node.id, {}, std::move(generated_outputs));
}

void sync_function_outputs_node(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	const graph_function_definition* function = graph.find_function(node.function_id);
	std::vector<NE_Pin> generated_inputs;
	if (function != nullptr)
		generated_inputs = make_function_signature_inputs(function->outputs);
	if (equivalent_pin_layout(node.generated_inputs, generated_inputs))
		return;
	graph.replace_generated_pins(node.id, std::move(generated_inputs), {});
}

void sync_call_function_node(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	if (node.custom_state.storage == nullptr)
		return;

	auto& state = node.custom_state.as<call_function_node_state>();
	const graph_function_definition* function = graph.find_function(state.function_id);
	std::vector<NE_Pin> generated_inputs;
	std::vector<NE_Pin> generated_outputs;
	if (function != nullptr && !graph.is_builtin_function(function->id)) {
		generated_inputs = make_function_signature_inputs(function->inputs);
		generated_outputs = make_function_signature_outputs(function->outputs);
		node.title = "Call " + function->name;
	} else {
		node.title = "Call Function";
	}

	if (equivalent_pin_layout(node.generated_inputs, generated_inputs) &&
		equivalent_pin_layout(node.generated_outputs, generated_outputs))
		return;
	graph.replace_generated_pins(node.id, std::move(generated_inputs), std::move(generated_outputs));
}

void ensure_function_boundary_nodes(NodeGraph& graph) {
	for (const auto& function : graph.functions) {
		if (function.id == graph.setup_function_id()) {
			if (find_function_node_by_type(graph, function.id, node_type_v<setup_start_node_tag>) == nullptr)
				graph.spawn_node(node_type_v<setup_start_node_tag>, { 0.0f, 0.0f }, -1, function.id);
		} else if (function.id == graph.render_function_id()) {
			if (find_function_node_by_type(graph, function.id, node_type_v<render_start_node_tag>) == nullptr)
				graph.spawn_node(node_type_v<render_start_node_tag>, { 0.0f, 0.0f }, -1, function.id);
		} else {
			if (find_function_node_by_type(graph, function.id, node_type_v<function_inputs_node_tag>) == nullptr)
				graph.spawn_node(node_type_v<function_inputs_node_tag>, { 0.0f, 0.0f }, -1, function.id);
			if (find_function_node_by_type(graph, function.id, node_type_v<function_outputs_node_tag>) == nullptr)
				graph.spawn_node(node_type_v<function_outputs_node_tag>, { 480.0f, 0.0f }, -1, function.id);
		}
	}
}

void refresh_dynamic_function_graph_nodes(NodeGraph& graph) {
	ensure_function_boundary_nodes(graph);
	const NodeRegistry* registry = graph.node_registry();
	const NodeTypeInfo* inputs_type = registry != nullptr ? registry->find(node_type_v<function_inputs_node_tag>) : nullptr;
	const NodeTypeInfo* outputs_type = registry != nullptr ? registry->find(node_type_v<function_outputs_node_tag>) : nullptr;
	const NodeTypeInfo* call_type = registry != nullptr ? registry->find(node_type_v<call_function_node_tag>) : nullptr;
	for (auto& node : graph.nodes) {
		if (node.type == node_type_v<function_inputs_node_tag>) {
			if (inputs_type != nullptr)
				sync_function_inputs_node(graph, *inputs_type, node);
		} else if (node.type == node_type_v<function_outputs_node_tag>) {
			if (outputs_type != nullptr)
				sync_function_outputs_node(graph, *outputs_type, node);
		} else if (node.type == node_type_v<call_function_node_tag>) {
			if (call_type != nullptr)
				sync_call_function_node(graph, *call_type, node);
		}
	}
}

void setup_start_node::configure(NodeTypeInfo& info) {
	info.pins.exec_outputs = { make_exec_pin("Exec") };
	info.pins.exec_output = info.pins.exec_outputs.front();
	info.meta.is_permanent = true;
	info.meta.show_in_spawn_menu = false;
	info.meta.is_vm_node = true;
	info.meta.is_vm_callable = true;
}

bool setup_start_node::execute(rv::graph_execution_context&, NE_Node&, std::string&) { return true; }
bool setup_start_node::validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error) {
	const graph_function_definition* function = graph.find_function(node.function_id);
	if (function == nullptr || function->id != graph.setup_function_id()) {
		error = "Setup Start must live inside the setup function.";
		return false;
	}
	error.clear();
	return true;
}

void render_start_node::configure(NodeTypeInfo& info) {
	info.pins.exec_outputs = { make_exec_pin("Exec") };
	info.pins.exec_output = info.pins.exec_outputs.front();
	info.meta.is_permanent = true;
	info.meta.show_in_spawn_menu = false;
	info.meta.is_vm_node = true;
	info.meta.is_vm_callable = true;
}

bool render_start_node::execute(rv::graph_execution_context&, NE_Node&, std::string&) { return true; }
bool render_start_node::validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error) {
	const graph_function_definition* function = graph.find_function(node.function_id);
	if (function == nullptr || function->id != graph.render_function_id()) {
		error = "Render Start must live inside the render function.";
		return false;
	}
	error.clear();
	return true;
}

void function_inputs_node::configure(NodeTypeInfo& info) {
	info.pins.exec_outputs = { make_exec_pin("Exec") };
	info.pins.exec_output = info.pins.exec_outputs.front();
	info.meta.is_permanent = true;
	info.meta.show_in_spawn_menu = false;
	info.meta.is_vm_node = true;
	info.meta.is_vm_pure = true;
}

void function_inputs_node::refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node) { sync_function_inputs_node(graph, info, node); }
bool function_inputs_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	for (const auto& pin : node.outputs) {
		std::any value;
		if (!ctx.read_function_input_any(node, pin.label, value, error))
			return false;
		ctx.push_output_any(node, pin.label, std::move(value));
	}
	error.clear();
	return true;
}
bool function_inputs_node::validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error) {
	const graph_function_definition* function = graph.find_function(node.function_id);
	if (function == nullptr || graph.is_builtin_function(function->id)) {
		error = "Function Inputs must live inside a custom function.";
		return false;
	}
	error.clear();
	return true;
}

void function_outputs_node::configure(NodeTypeInfo& info) {
	info.meta.is_permanent = true;
	info.meta.show_in_spawn_menu = false;
	info.meta.is_vm_node = true;
	info.meta.is_vm_callable = true;
}

void function_outputs_node::refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node) { sync_function_outputs_node(graph, info, node); }
bool function_outputs_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	for (const auto& pin : node.inputs) {
		std::any value;
		if (!ctx.resolve_input_any(node, pin.label, pin.type_hash, pin.is_container, ctx.current_item_index, ctx.current_item_count, value, error))
			return false;
		if (!ctx.write_function_output_any(node, pin.label, std::move(value), error))
			return false;
	}
	error.clear();
	return true;
}
bool function_outputs_node::validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error) {
	const graph_function_definition* function = graph.find_function(node.function_id);
	if (function == nullptr || graph.is_builtin_function(function->id)) {
		error = "Function Outputs must live inside a custom function.";
		return false;
	}
	error.clear();
	return true;
}

void call_function_node::configure(NodeTypeInfo& info) {
	info.meta.is_vm_node = true;
	info.meta.is_vm_callable = true;
}

void call_function_node::refresh(NodeGraph& graph, const NodeTypeInfo& info, NE_Node& node) { sync_call_function_node(graph, info, node); }
void call_function_node::edit(NodeGraph& graph, NE_Node& node, call_function_node_state& state) {
	const NodeRegistry* registry = graph.node_registry();
	const NodeTypeInfo* info = registry != nullptr ? registry->find(node.type) : nullptr;
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::Separator();
	const graph_function_definition* current = graph.find_function(state.function_id);
	const char* preview = current != nullptr ? current->name.c_str() : "Select function";
	if (ImGui::BeginCombo("Function", preview)) {
		for (const auto& function : graph.functions) {
			if (graph.is_builtin_function(function.id))
				continue;
			const bool selected = function.id == state.function_id;
			if (ImGui::Selectable(function.name.c_str(), selected)) {
				state.function_id = function.id;
				if (info != nullptr)
					sync_call_function_node(graph, *info, node);
				graph.notify_graph_dirty();
			}
			if (selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::TextDisabled("Calls a custom function as a virtual node.");
}
bool call_function_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) { return ctx.call_function(node, error); }
bool call_function_node::validate(const NodeGraph& graph, const NodeTypeInfo&, const NE_Node& node, std::string& error) {
	if (node.custom_state.storage == nullptr) {
		error = "Call Function is missing its state.";
		return false;
	}
	const auto& state = node.custom_state.as<call_function_node_state>();
	const graph_function_definition* function = graph.find_function(state.function_id);
	if (function == nullptr || graph.is_builtin_function(function->id)) {
		error = "Call Function has no target custom function selected.";
		return false;
	}
	error.clear();
	return true;
}

} // namespace rv::nodes
