#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/uniform_data_node.hpp>

namespace rv::nodes {

NE_Pin make_uniform_value_pin(uniform_value_kind kind, std::string label, bool required) {
	switch (kind) {
	case uniform_value_kind::float1: return make_pin<float>(std::move(label), false, required);
	case uniform_value_kind::float2: return make_pin<mars::vector2<float>>(std::move(label), false, required);
	case uniform_value_kind::float3: return make_pin<mars::vector3<float>>(std::move(label), false, required);
	case uniform_value_kind::float4: return make_pin<mars::vector4<float>>(std::move(label), false, required);
	case uniform_value_kind::float4x4: return make_pin<mars::matrix4<float>>(std::move(label), false, required);
	case uniform_value_kind::uint1: return make_pin<unsigned int>(std::move(label), false, required);
	case uniform_value_kind::uint2: return make_pin<mars::vector2<unsigned int>>(std::move(label), false, required);
	case uniform_value_kind::uint3: return make_pin<mars::vector3<unsigned int>>(std::move(label), false, required);
	case uniform_value_kind::uint4:
	default: return make_pin<mars::vector4<unsigned int>>(std::move(label), false, required);
	}
}

const char* uniform_value_kind_name(uniform_value_kind kind) {
	switch (kind) {
	case uniform_value_kind::float1: return "float";
	case uniform_value_kind::float2: return "float2";
	case uniform_value_kind::float3: return "float3";
	case uniform_value_kind::float4: return "float4";
	case uniform_value_kind::float4x4: return "float4x4";
	case uniform_value_kind::uint1: return "uint";
	case uniform_value_kind::uint2: return "uint2";
	case uniform_value_kind::uint3: return "uint3";
	case uniform_value_kind::uint4:
	default: return "uint4";
	}
}

bool uniform_kind_is_float(uniform_value_kind kind) {
	return kind == uniform_value_kind::float1 ||
		kind == uniform_value_kind::float2 ||
		kind == uniform_value_kind::float3 ||
		kind == uniform_value_kind::float4 ||
		kind == uniform_value_kind::float4x4;
}

int uniform_kind_component_count(uniform_value_kind kind) {
	switch (kind) {
	case uniform_value_kind::float1:
	case uniform_value_kind::uint1: return 1;
	case uniform_value_kind::float2:
	case uniform_value_kind::uint2: return 2;
	case uniform_value_kind::float3:
	case uniform_value_kind::uint3: return 3;
	case uniform_value_kind::float4:
		return 4;
	case uniform_value_kind::float4x4:
		return 16;
	case uniform_value_kind::uint4:
	default: return 4;
	}
}

std::vector<std::byte> make_uniform_bytes(const uniform_data_state& state) {
	std::vector<std::byte> bytes;
	bytes.reserve(state.kind == uniform_value_kind::float4x4 ? sizeof(mars::matrix4<float>) : 16);
	if (state.kind == uniform_value_kind::float4x4) {
		const mars::matrix4<float> matrix(1.0f);
		append_bytes(bytes, matrix);
		return bytes;
	}
	const int component_count = uniform_kind_component_count(state.kind);
	if (uniform_kind_is_float(state.kind)) {
		for (int i = 0; i < component_count; ++i)
			append_bytes(bytes, state.float_values[static_cast<size_t>(i)]);
		for (int i = component_count; i < 4; ++i)
			append_bytes(bytes, 0.0f);
	} else {
		for (int i = 0; i < component_count; ++i)
			append_bytes(bytes, state.uint_values[static_cast<size_t>(i)]);
		for (int i = component_count; i < 4; ++i)
			append_bytes(bytes, 0u);
	}
	return bytes;
}

std::any make_uniform_value_any(const uniform_data_state& state) {
	switch (state.kind) {
	case uniform_value_kind::float1: return std::any(state.float_values[0]);
	case uniform_value_kind::float2: return std::any(mars::vector2<float> { state.float_values[0], state.float_values[1] });
	case uniform_value_kind::float3: return std::any(mars::vector3<float> { state.float_values[0], state.float_values[1], state.float_values[2] });
	case uniform_value_kind::float4: return std::any(mars::vector4<float> { state.float_values[0], state.float_values[1], state.float_values[2], state.float_values[3] });
	case uniform_value_kind::float4x4: return std::any(mars::matrix4<float>(1.0f));
	case uniform_value_kind::uint1: return std::any(state.uint_values[0]);
	case uniform_value_kind::uint2: return std::any(mars::vector2<unsigned int> { state.uint_values[0], state.uint_values[1] });
	case uniform_value_kind::uint3: return std::any(mars::vector3<unsigned int> { state.uint_values[0], state.uint_values[1], state.uint_values[2] });
	case uniform_value_kind::uint4:
	default: return std::any(mars::vector4<unsigned int> { state.uint_values[0], state.uint_values[1], state.uint_values[2], state.uint_values[3] });
	}
}

void sync_uniform_data_node(NodeGraph& graph, NE_Node& node) {
	if (node.custom_state.storage == nullptr)
		return;
	std::vector<NE_Pin> outputs = {
		make_uniform_value_pin(node.custom_state.as<uniform_data_state>().kind)
	};
	if (equivalent_pin_layout(node.generated_outputs, outputs) && node.generated_inputs.empty())
		return;
	graph.replace_generated_pins(node.id, {}, std::move(outputs));
}

std::string uniform_data_node::save(const uniform_data_state& state) {
	uniform_data_state copy = state;
	return json_stringify(copy);
}

bool uniform_data_node::load(uniform_data_state& state, std::string_view json, std::string& error) {
	if (!json_parse(json, state)) {
		error = "Failed to parse uniform data state.";
		return false;
	}
	return true;
}

void uniform_data_node::edit(NodeGraph& graph, NE_Node& node, uniform_data_state& state) {
	ImGui::TextUnformatted(node.title.c_str());
	ImGui::TextDisabled("Produces a typed CPU value for direct resource binding.");
	ImGui::Separator();
	int kind = static_cast<int>(state.kind);
	if (ImGui::Combo("Type", &kind, "float\0float2\0float3\0float4\0uint\0uint2\0uint3\0uint4\0")) {
		state.kind = static_cast<uniform_value_kind>(kind);
		sync_uniform_data_node(graph, node);
		graph.notify_graph_dirty();
	}
	const int components = uniform_kind_component_count(state.kind);
	bool changed = false;
	if (uniform_kind_is_float(state.kind)) {
		if (state.kind == uniform_value_kind::float4x4)
			ImGui::TextDisabled("Matrix editing is not supported in Uniform Data.");
		else if (components == 1)
			changed |= ImGui::InputFloat("Value", &state.float_values[0]);
		else if (components == 2)
			changed |= ImGui::InputFloat2("Value", state.float_values.data());
		else if (components == 3)
			changed |= ImGui::InputFloat3("Value", state.float_values.data());
		else
			changed |= ImGui::InputFloat4("Value", state.float_values.data());
	} else {
		if (components == 1)
			changed |= ImGui::InputScalar("Value", ImGuiDataType_U32, &state.uint_values[0]);
		else if (components == 2)
			changed |= ImGui::InputScalarN("Value", ImGuiDataType_U32, state.uint_values.data(), 2);
		else if (components == 3)
			changed |= ImGui::InputScalarN("Value", ImGuiDataType_U32, state.uint_values.data(), 3);
		else
			changed |= ImGui::InputScalarN("Value", ImGuiDataType_U32, state.uint_values.data(), 4);
	}
	if (changed)
		graph.notify_graph_dirty();
}

void uniform_data_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	sync_uniform_data_node(graph, node);
}

void uniform_data_node::configure(NodeTypeInfo& info) {
	info.meta.is_vm_node = true;
	info.meta.is_vm_pure = true;
	info.meta.vm_execution_shape = NodeTypeInfo::execution_shape::map;
}

bool uniform_data_node::emit(rv::graph_execution_context& ctx, NE_Node& node, std::string&) {
	const auto& state = node.custom_state.as<uniform_data_state>();
	switch (state.kind) {
	case uniform_value_kind::float1: ctx.set_output(node, "value", state.float_values[0]); break;
	case uniform_value_kind::float2: ctx.set_output(node, "value", mars::vector2<float> { state.float_values[0], state.float_values[1] }); break;
	case uniform_value_kind::float3: ctx.set_output(node, "value", mars::vector3<float> { state.float_values[0], state.float_values[1], state.float_values[2] }); break;
	case uniform_value_kind::float4: ctx.set_output(node, "value", mars::vector4<float> { state.float_values[0], state.float_values[1], state.float_values[2], state.float_values[3] }); break;
	case uniform_value_kind::float4x4: ctx.set_output(node, "value", mars::matrix4<float>(1.0f)); break;
	case uniform_value_kind::uint1: ctx.set_output(node, "value", state.uint_values[0]); break;
	case uniform_value_kind::uint2: ctx.set_output(node, "value", mars::vector2<unsigned int> { state.uint_values[0], state.uint_values[1] }); break;
	case uniform_value_kind::uint3: ctx.set_output(node, "value", mars::vector3<unsigned int> { state.uint_values[0], state.uint_values[1], state.uint_values[2] }); break;
	case uniform_value_kind::uint4:
	default: ctx.set_output(node, "value", mars::vector4<unsigned int> { state.uint_values[0], state.uint_values[1], state.uint_values[2], state.uint_values[3] }); break;
	}
	return true;
}

} // namespace rv::nodes
