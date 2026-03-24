#include <render_visualizer/nodes/support.hpp>
#include <render_visualizer/nodes/framebuffer_node.hpp>

#include <render_visualizer/nodes/graphics_pipeline_node.hpp>
#include <render_visualizer/nodes/shader_module_node.hpp>
#include <render_visualizer/runtime/impl.hpp>

#include <cstdlib>
#include <cctype>

namespace rv::nodes {
namespace {

const NE_Node* linked_pipeline_node(const NodeGraph& graph, const NE_Node& node) {
	const NE_Pin* pipeline_pin = find_pin_by_label(node.inputs, "pipeline");
	if (pipeline_pin == nullptr)
		return nullptr;
	for (const auto& link : graph.links) {
		if (link.to_node != node.id || link.to_pin != pipeline_pin->id)
			continue;
		return graph.find_node(link.from_node);
	}
	return nullptr;
}

const NE_Node* linked_shader_node(const NodeGraph& graph, const NE_Node& pipeline_node) {
	const NE_Pin* shader_pin = find_pin_by_label(pipeline_node.inputs, "shader");
	if (shader_pin == nullptr)
		return nullptr;
	for (const auto& link : graph.links) {
		if (link.to_node != pipeline_node.id || link.to_pin != shader_pin->id)
			continue;
		return graph.find_node(link.from_node);
	}
	return nullptr;
}

size_t render_target_count_from_shader(const shader_module_state& shader_state) {
	auto is_render_target_label = [](std::string_view label, size_t& index) {
		std::string normalized;
		normalized.reserve(label.size());
		for (const char c : label) {
			if (c == '_' || c == ' ')
				continue;
			normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}

		std::string_view prefix;
		if (normalized.starts_with("svtarget"))
			prefix = "svtarget";
		else if (normalized.starts_with("target"))
			prefix = "target";
		else
			return false;

		index = 0;
		if (normalized.size() > prefix.size())
			index = static_cast<size_t>(std::strtoul(normalized.substr(prefix.size()).c_str(), nullptr, 10));
		return true;
	};

	size_t max_index = 0;
	bool has_target = false;
	for (const auto& output : shader_state.reflected_outputs) {
		size_t index = 0;
		if (!is_render_target_label(output.label, index))
			continue;
		has_target = true;
		max_index = (std::max)(max_index, index);
	}

	// Some reflection paths normalize target semantics differently; if the shader
	// clearly exposes multiple fragment outputs, prefer that count over collapsing
	// back to a single attachment.
	if (!has_target && shader_state.reflected_outputs.size() > 1u)
		return shader_state.reflected_outputs.size();
	return has_target ? (max_index + 1u) : 1u;
}

size_t desired_render_target_count(const NodeGraph& graph, const NE_Node& node) {
	const NE_Node* pipeline_node = linked_pipeline_node(graph, node);
	if (pipeline_node == nullptr)
		return 1u;
	const NE_Node* source_node = resolve_pipeline_source_node(graph, *pipeline_node);
	if (source_node == nullptr || source_node->type != node_type_v<graphics_pipeline_node_tag>)
		return 1u;
	const NE_Node* shader_node = linked_shader_node(graph, *source_node);
	if (shader_node == nullptr || shader_node->type != node_type_v<shader_module_node_tag> || shader_node->custom_state.storage == nullptr)
		return 1u;
	return render_target_count_from_shader(shader_node->custom_state.as<shader_module_state>());
}

std::vector<NE_Pin> make_render_target_outputs(size_t count, bool explicit_indices) {
	std::vector<NE_Pin> outputs;
	outputs.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		const std::string label = explicit_indices ? ("color" + std::to_string(i)) : "color";
		outputs.push_back(make_pin<mars::vector3<unsigned char>>(label));
	}
	return outputs;
}

} // namespace

void framebuffer_node::configure(NodeTypeInfo& info) {
	info.pins.inputs = {
		make_pin<rv::resource_tags::render_pass>("render_pass"),
		make_pin<rv::resource_tags::graphics_pipeline>("pipeline", false, false),
	};
	info.pins.outputs = {
		make_pin<rv::resource_tags::framebuffer>("framebuffer"),
	};
}

void framebuffer_node::on_connect(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node, const NE_Link&) {
	framebuffer_node::refresh(graph, {}, node);
}

void framebuffer_node::refresh(NodeGraph& graph, const NodeTypeInfo&, NE_Node& node) {
	const size_t target_count = desired_render_target_count(graph, node);
	const bool explicit_indices = linked_pipeline_node(graph, node) != nullptr;
	std::vector<NE_Pin> generated_outputs = make_render_target_outputs(target_count, explicit_indices);
	if (!equivalent_pin_layout(node.generated_outputs, generated_outputs))
		graph.replace_generated_pins(node.id, {}, std::move(generated_outputs));
}

bool framebuffer_node::build_framebuffer(rv::graph_build_context& ctx, NE_Node& node, rv::graph_build_result& result, std::string& error) {
	result.kind = "Render Targets";
	auto* services = require_build_services(ctx, error);
	if (services == nullptr)
		return false;

	runtime_detail::framebuffer_resources resources;
	const runtime_detail::render_pass_resources* pass_resources =
		read_input_resource<runtime_detail::render_pass_resources>(*services, node, "render_pass", error);
	if (pass_resources == nullptr) {
		resources.error = error.empty() ? "Render pass input is not ready." : error;
		error = resources.error;
		result.status = error;
		return false;
	}

	const bool has_pipeline = services->runtime->resolve_input_source_node(node, "pipeline", error) != nullptr;
	const size_t target_count = services->graph != nullptr
		? desired_render_target_count(*services->graph, node)
		: (std::max)(size_t{1}, node.outputs.size() - size_t{1});
	resources.targets.reserve(target_count);
	for (size_t i = 0; i < target_count; ++i) {
		mars::texture_create_params texture_params = {};
		texture_params.size = services->frame_size;
		texture_params.format = MARS_FORMAT_RGBA8_UNORM;
		texture_params.usage = MARS_TEXTURE_USAGE_SAMPLED | MARS_TEXTURE_USAGE_COLOR_ATTACHMENT;
		texture_params.clear_color = pass_resources->clear_color;
		mars::texture target = mars::graphics::texture_create(*services->device, texture_params);
		if (!target.engine) {
			resources.error = "Failed to create render target texture " + std::to_string(i) + ".";
			resources.destroy(*services->device);
			error = resources.error;
			result.status = error;
			return false;
		}
		resources.targets.push_back(target);
	}

	mars::framebuffer_create_params framebuffer_params = {};
	framebuffer_params.framebuffer_render_pass = pass_resources->render_pass;
	framebuffer_params.size = services->frame_size;
	framebuffer_params.views.reserve(resources.targets.size());
	for (auto& target : resources.targets)
		framebuffer_params.views.push_back(target.view);
	resources.framebuffer = mars::graphics::framebuffer_create(*services->device, framebuffer_params);
	if (!resources.framebuffer.engine) {
		resources.error = "Failed to create the framebuffer.";
		resources.destroy(*services->device);
		error = resources.error;
		result.status = error;
		return false;
	}

	resources.extent = services->frame_size;
	resources.valid = true;
	resources.error.clear();
	auto& stored = publish_owned_resource(*services, node, std::move(resources));
	stored.attachments.resize(stored.targets.size());
	for (size_t i = 0; i < stored.attachments.size(); ++i) {
		auto& attachment = stored.attachments[i];
		attachment.owner = &stored;
		attachment.attachment_index = i;
		attachment.valid = true;
		attachment.error.clear();
	}

	if (!store_output_resource(*services, node, "framebuffer", &stored, error)) {
		result.status = error;
		return false;
	}
	for (size_t i = 0; i < stored.attachments.size(); ++i) {
		const std::string label = has_pipeline ? ("color" + std::to_string(i)) : "color";
		if (!store_output_resource(*services, node, label, &stored.attachments[i], error)) {
			result.status = error;
			return false;
		}
	}
	result.executed_count = stored.attachments.size();
	result.status = "Render targets ready";
	return true;
}

void framebuffer_node::destroy(rv::graph_services& services, NE_Node& node) {
	destroy_current_owned_resource<runtime_detail::framebuffer_resources>(services, node);
}

} // namespace rv::nodes
