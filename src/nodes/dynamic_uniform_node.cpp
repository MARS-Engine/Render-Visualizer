#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/dynamic_uniform_node.hpp>

#include <render_visualizer/runtime/impl.hpp>

namespace rv::nodes {

NE_Pin make_dynamic_uniform_value_pin(uniform_value_kind kind) {
	switch (kind) {
	case uniform_value_kind::float1: return make_pin<float>("value");
	case uniform_value_kind::float2: return make_pin<mars::vector2<float>>("value");
	case uniform_value_kind::float3: return make_pin<mars::vector3<float>>("value");
	case uniform_value_kind::float4: return make_pin<mars::vector4<float>>("value");
	case uniform_value_kind::uint1: return make_pin<unsigned int>("value");
	case uniform_value_kind::uint2: return make_pin<mars::vector2<unsigned int>>("value");
	case uniform_value_kind::uint3: return make_pin<mars::vector3<unsigned int>>("value");
	case uniform_value_kind::uint4:
	default: return make_pin<mars::vector4<unsigned int>>("value");
	}
}

std::vector<std::byte> make_dynamic_uniform_zero_bytes(uniform_value_kind kind) {
	uniform_data_state zero_state;
	zero_state.kind = kind;
	zero_state.float_values = {0.0f, 0.0f, 0.0f, 0.0f};
	zero_state.uint_values = {0u, 0u, 0u, 0u};
	return make_uniform_bytes(zero_state);
}

void sync_dynamic_uniform_node(NodeGraph& graph, NE_Node& node) {
	if (node.custom_state.storage == nullptr)
		return;
	std::vector<NE_Pin> inputs = { make_dynamic_uniform_value_pin(node.custom_state.as<dynamic_uniform_state>().kind) };
	if (equivalent_pin_layout(node.generated_inputs, inputs) && node.generated_outputs.empty())
		return;
	graph.replace_generated_pins(node.id, std::move(inputs), {});
}

void dynamic_uniform_node::configure(NodeTypeInfo& info) {
	info.pins.outputs = {
		make_pin<rv::resource_tags::uniform_resource>("uniform")
	};
	info.meta.show_in_spawn_menu = false;
	info.meta.is_vm_node = true;
	info.meta.is_vm_callable = true;
	info.meta.vm_execution_shape = NodeTypeInfo::execution_shape::map;
	info.meta.vm_execute_per_item = true;
}

void dynamic_uniform_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_dynamic_uniform_node(graph, node);
}

void dynamic_uniform_node::edit(NodeGraph& graph, NE_Node& node, dynamic_uniform_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::TextDisabled("Updates a uniform buffer from the current execution item.");
	int kind = static_cast<int>(state.kind);
	if (ImGui::Combo("Type", &kind, "float\0float2\0float3\0float4\0uint\0uint2\0uint3\0uint4\0")) {
		state.kind = static_cast<uniform_value_kind>(kind);
		sync_dynamic_uniform_node(graph, node);
		graph.notify_graph_dirty();
	}
}

bool dynamic_uniform_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Uniform";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	runtime_detail::uniform_buffer_resources resources;
	const auto& state = node.custom_state.as<dynamic_uniform_state>();
	const std::vector<std::byte> payload = make_dynamic_uniform_zero_bytes(state.kind);
	if (!services->runtime->ensure_uniform_payload(resources, payload, resources.error)) {
		error = resources.error;
		result.status = error;
		resources.destroy(*services->device);
		return false;
	}

	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	store_node_resource(*services, node, &stored);
	if (!store_output_resource(*services, node, "uniform", &stored, error)) {
		result.status = error;
		return false;
	}
	result.executed_count = uniform_kind_component_count(state.kind);
	result.status = std::string("Dynamic ") + uniform_value_kind_name(state.kind) + " ready";
	return true;
}

bool dynamic_uniform_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	if (ctx.runtime == nullptr) { error = "Execution context has no runtime."; return false; }
	return ctx.runtime->execute_dynamic_uniform(node, error);
}

void dynamic_uniform_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::uniform_buffer_resources>(services, node);
}

} // namespace rv::nodes
