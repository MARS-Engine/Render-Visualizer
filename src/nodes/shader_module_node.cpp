#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/shader_module_node.hpp>

#include <render_visualizer/runtime/impl.hpp>

namespace rv::nodes {

namespace {

constexpr std::string_view kShaderModuleCreatorTitle = "Shader Module Creator";
constexpr std::string_view kShaderModuleFallbackName = "shader_module";

std::string shader_module_title(const shader_module_state& state) {
	return state.module_name.empty() ? std::string(kShaderModuleCreatorTitle) : state.module_name;
}

std::string shader_module_asset_name(const shader_module_state& state, const NE_Node& node) {
	if (!state.module_name.empty())
		return state.module_name;
	return std::string(kShaderModuleFallbackName) + "_" + std::to_string(node.id);
}

std::string shader_interface_name(const shader_module_state& state, const NE_Node& node) {
	if (!state.module_name.empty())
		return state.module_name + " Interface";
	return shader_module_asset_name(state, node) + " Interface";
}

void sync_shader_module_node(NE_Node& node) {
	if (node.custom_state.storage == nullptr)
		return;
	node.title = shader_module_title(node.custom_state.as<shader_module_state>());
}

void render_selectable_text_block(const char* id, const std::string& text, ImVec2 size) {
	std::vector<char> buffer(text.begin(), text.end());
	buffer.push_back('\0');
	ImGui::InputTextMultiline(
		id,
		buffer.data(),
		buffer.size(),
		size,
		ImGuiInputTextFlags_ReadOnly
	);
}

} // namespace

void compile_shader_module(shader_module_state& state) {
	rv::raster::compile_result compile = rv::raster::compile_source(state.source);
	state.diagnostics = compile.diagnostics;
	state.last_compile_success = compile.success;
	state.compile_required = false;
	if (compile.success) {
		state.reflected_inputs = std::move(compile.input_pins);
		state.reflected_outputs = std::move(compile.output_pins);
		state.reflected_resources = std::move(compile.resources);
	} else {
		state.reflected_inputs.clear();
		state.reflected_outputs.clear();
		state.reflected_resources.clear();
	}
}

graph_shader_interface make_generated_shader_interface(const shader_module_state& state, const NE_Node& node) {
	graph_shader_interface shader_interface;
	shader_interface.id = node.id;
	shader_interface.source_node_id = node.id;
	shader_interface.name = shader_interface_name(state, node);
	shader_interface.valid = !state.compile_required && state.last_compile_success;
	shader_interface.diagnostics = state.compile_required
		? "Compile the shader module to generate its reflected shader interface."
		: state.diagnostics;
	shader_interface.slots.reserve(state.reflected_resources.size());
	for (const auto& resource : state.reflected_resources) {
		shader_interface.slots.push_back({
			.label = resource.label,
			.binding = resource.binding,
			.stage = resource.stage,
			.kind = resource.kind,
			.type_hash = resource.type_hash,
		});
	}
	return shader_interface;
}

void refresh_generated_shader_interfaces(NodeGraph& graph) {
	graph.shader_interfaces.clear();
	for (auto& node : graph.nodes) {
		if (node.type != node_type_v<shader_module_node_tag> || node.custom_state.storage == nullptr)
			continue;
		sync_shader_module_node(node);
		graph.upsert_shader_interface(make_generated_shader_interface(node.custom_state.as<shader_module_state>(), node));
	}
}

graph_shader_interface* find_generated_shader_interface(NodeGraph& graph, int shader_node_id) {
	return graph.find_shader_interface_by_source_node(shader_node_id);
}

const graph_shader_interface* find_generated_shader_interface(const NodeGraph& graph, int shader_node_id) {
	return graph.find_shader_interface_by_source_node(shader_node_id);
}

void shader_module_node::configure(NodeTypeInfo& info) {
	info.pins.outputs = {
		make_pin<rv::resource_tags::shader_module>("shader")
	};
}

std::string shader_module_node::save(const shader_module_state& state) {
	shader_module_state copy = state;
	return json_stringify(copy);
}

bool shader_module_node::load(shader_module_state& state, std::string_view json, std::string& error) {
	if (!json_parse(json, state)) {
		error = "Failed to parse shader module state.";
		return false;
	}
	compile_shader_module(state);
	return true;
}

void shader_module_node::edit(NodeGraph& graph, NE_Node& node, shader_module_state& state) {
	bool changed = false;
	changed |= ui::input_text_string("Module Name", state.module_name);
	if (changed) {
		sync_shader_module_node(node);
		refresh_generated_shader_interfaces(graph);
		nodes::refresh_dynamic_nodes(graph);
		graph.notify_graph_dirty();
	}

	ImGui::TextUnformatted(node.title.c_str());
	ImGui::TextDisabled(state.last_compile_success ? "Compiled" : "Not compiled");
	ImGui::Separator();
	const float diagnostics_height = 120.0f;
	const float editor_height = std::max(160.0f, ImGui::GetContentRegionAvail().y - diagnostics_height - ImGui::GetFrameHeight() - 24.0f);
	if (ui::input_text_multiline_string("##shader_source", state.source, { -FLT_MIN, editor_height })) {
		state.compile_required = true;
		state.last_compile_success = false;
		state.reflected_inputs.clear();
		state.reflected_outputs.clear();
		state.reflected_resources.clear();
		state.diagnostics = "Source changed. Compile to refresh shader reflection.";
		refresh_generated_shader_interfaces(graph);
		nodes::refresh_dynamic_nodes(graph);
		graph.notify_graph_dirty();
	}
	ImGui::TextUnformatted("Diagnostics");
	ImGui::BeginChild("##shader_diagnostics", { 0.0f, diagnostics_height }, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
	render_selectable_text_block("##shader_diagnostics_text", state.diagnostics, { -FLT_MIN, -FLT_MIN });
	ImGui::EndChild();
	if (ImGui::Button("Compile", { -1.0f, 0.0f })) {
		compile_shader_module(state);
		refresh_generated_shader_interfaces(graph);
		nodes::refresh_dynamic_nodes(graph);
		graph.notify_graph_dirty();
	}
	if (!state.reflected_inputs.empty()) {
		ImGui::Spacing();
		ImGui::TextUnformatted("Reflected VS Inputs");
		for (const auto& input : state.reflected_inputs)
			ImGui::BulletText("%s", input.label.c_str());
	}
	if (!state.reflected_resources.empty()) {
		ImGui::Spacing();
		ImGui::TextUnformatted("Reflected Shader Resources");
		for (const auto& resource : state.reflected_resources) {
			const std::string value_type = shader_resource_value_type_name(resource.kind, resource.type_hash);
			ImGui::BulletText(
				"%s | binding %zu | %s | %s | %s",
				resource.label.c_str(),
				resource.binding,
				pipeline_stage_name(resource.stage),
				shader_resource_kind_name(resource.kind),
				value_type.c_str()
			);
		}
	}
}

void shader_module_node::refresh(NodeGraph&, const NodeTypeInfo&, NE_Node& node) {
	sync_shader_module_node(node);
}

bool shader_module_node::build(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Shader";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	auto& state = node.custom_state.as<shader_module_state>();
	if (!state.last_compile_success)
		compile_shader_module(state);
	if (services->graph != nullptr)
		refresh_generated_shader_interfaces(*services->graph);
	if (!state.last_compile_success) {
		error = state.diagnostics;
		result.status = error;
		return false;
	}

	sync_shader_module_node(node);
	const std::string module_name = shader_module_asset_name(state, node);
	runtime_detail::shader_module_resources resources;
	std::vector<mars::shader_module> modules = {
		{
			.type = MARS_SHADER_TYPE_VERTEX,
			.path = {},
			.source = state.source,
			.name = module_name + ".vertex"
		},
		{
			.type = MARS_SHADER_TYPE_FRAGMENT,
			.path = {},
			.source = state.source,
			.name = module_name + ".fragment"
		},
	};
	resources.shader = mars::graphics::shader_create(*services->device, modules);
	resources.valid = resources.shader.engine != nullptr;
	if (!resources.valid) {
		resources.error = "Failed to create the shader module.";
		error = resources.error;
		result.status = error;
		return false;
	}

	resources.error.clear();
	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	if (!store_output_resource(*services, node, "shader", &stored, error)) {
		result.status = error;
		return false;
	}
	result.executed_count = 1;
	result.status = "Shader ready";
	return true;
}

void shader_module_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::shader_module_resources>(services, node);
}

} // namespace rv::nodes
