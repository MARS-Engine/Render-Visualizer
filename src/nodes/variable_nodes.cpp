#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/variable_nodes.hpp>
#include <render_visualizer/runtime/impl.hpp>
#include <render_visualizer/ui/shared_editors.hpp>

namespace rv::nodes {

namespace {

using local_variable_types = std::tuple<
	float,
	mars::vector2<float>,
	mars::vector3<float>,
	mars::vector4<float>,
	unsigned int,
	mars::vector2<unsigned int>,
	mars::vector3<unsigned int>,
	mars::vector4<unsigned int>,
	bool
>;

} // namespace

void variable_get_node::configure(NodeTypeInfo& info) {
	info.meta.show_in_spawn_menu = false;
	info.meta.is_vm_node = true;
	info.meta.is_vm_pure = true;
}

bool variable_get_node::load(variable_node_state& state, std::string_view json, std::string& error) {
	return load_variable_node_state(state, json, error, "Failed to parse variable get node state.");
}

void variable_get_node::edit(NodeGraph& graph, NE_Node& node, variable_node_state& state) {
	if (render_variable_slot_selector(graph, state.variable_id, "Variable"))
		graph.notify_graph_dirty();
	sync_variable_get_node(graph, node);
	if (const auto* slot = graph.find_variable_slot(state.variable_id); slot != nullptr) {
		ImGui::Separator();
		ImGui::Text("Type: %s", variable_slot_type_name(*slot));
		if (slot->has_virtual_struct)
			ImGui::Text("Struct: %s", slot->virtual_struct_name.c_str());
	} else {
		ImGui::TextDisabled("Choose a shared variable from the left sidebar.");
	}
}

void variable_get_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_variable_get_node(graph, node);
}

bool variable_get_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (node.custom_state.storage == nullptr) {
		error = "Variable Get state is missing.";
		return false;
	}
	const auto& state = node.custom_state.as<variable_node_state>();
	if (state.variable_id == -1) {
		error = "Variable Get has no shared variable selected.";
		return false;
	}

	bool handled = false;
	bool ok = false;
	[&]<size_t... Indices>(std::index_sequence<Indices...>) {
		([&]<typename value_t>() {
			if (!handled && node.outputs.size() == 1 && node.outputs[0].type_hash == rv::detail::pin_type_hash<typename variable_type_traits<value_t>::element_t>() &&
				node.outputs[0].is_container == variable_type_traits<value_t>::is_container) {
				if constexpr (!is_resource_variable_type_v<value_t> && !std::is_same_v<value_t, std::string>) {
					value_t value {};
					ok = ctx.read_variable<value_t>(state.variable_id, value, error);
					if (ok)
						ctx.set_output(node, "value", value);
				} else {
					error = "Variable Get exec evaluation currently supports CPU numeric/bool values only.";
					ok = false;
				}
				handled = true;
			}
		}.template operator()<std::tuple_element_t<Indices, local_variable_types>>(), ...);
	}(std::make_index_sequence<std::tuple_size_v<local_variable_types>>{});

	if (!handled)
		error = "Variable Get has an unsupported output type.";
	return handled && ok;
}

void variable_set_node::configure(NodeTypeInfo& info) {
	info.meta.show_in_spawn_menu = false;
	info.meta.is_vm_node = true;
	info.meta.is_vm_callable = true;
	info.meta.vm_execute_per_item = true;
}

bool variable_set_node::load(variable_node_state& state, std::string_view json, std::string& error) {
	return load_variable_node_state(state, json, error, "Failed to parse variable set node state.");
}

void variable_set_node::edit(NodeGraph& graph, NE_Node& node, variable_node_state& state) {
	if (render_variable_slot_selector(graph, state.variable_id, "Variable"))
		graph.notify_graph_dirty();
	sync_variable_set_node(graph, node);
	if (const auto* slot = graph.find_variable_slot(state.variable_id); slot != nullptr) {
		ImGui::Separator();
		ImGui::Text("Type: %s", variable_slot_type_name(*slot));
		if (slot->has_virtual_struct)
			ImGui::Text("Struct: %s", slot->virtual_struct_name.c_str());
	} else {
		ImGui::TextDisabled("Choose a shared variable from the left sidebar.");
	}
}

void variable_set_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_variable_set_node(graph, node);
}

bool variable_set_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (node.custom_state.storage == nullptr) {
		error = "Variable Set state is missing.";
		return false;
	}
	const auto& state = node.custom_state.as<variable_node_state>();
	if (state.variable_id == -1) {
		error = "Variable Set has no shared variable selected.";
		return false;
	}
	return ctx.write_variable_input(node, state.variable_id, "value", error);
}

bool variable_set_node::build_propagate(rv::graph_build_context& ctx, NE_Node& node, std::string& error) {
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;
	if (node.custom_state.storage == nullptr) {
		error = "Variable Set state is missing.";
		return false;
	}
	const auto& state = node.custom_state.as<variable_node_state>();
	if (state.variable_id == -1) {
		error = "Variable Set has no shared variable selected.";
		return false;
	}
	return services->runtime->write_variable_input_value(node, state.variable_id, "value", 0u, 1u, error);
}

} // namespace rv::nodes
