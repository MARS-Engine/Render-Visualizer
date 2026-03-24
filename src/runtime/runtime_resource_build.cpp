#include <render_visualizer/runtime/impl.hpp>

namespace rv {
using namespace runtime_detail;

graph_build_context graph_runtime::make_build_context(const mars::vector2<size_t>& frame_size) {
	graph_build_context ctx;
	build_services.swapchain_size = swapchain.swapchain_size();
	build_services.frame_size = frame_size;
	ctx.services = &build_services;
	return ctx;
}

texture_slot_resources* graph_runtime::shared_texture_slot_resource(int slot_id) {
	auto it = shared_texture_slot_resources.find(slot_id);
	if (it == shared_texture_slot_resources.end())
		return nullptr;
	return &it->second.as<owned_shared_resource<texture_slot_resources>>().value;
}

const texture_slot_resources* graph_runtime::shared_texture_slot_resource(int slot_id) const {
	auto it = shared_texture_slot_resources.find(slot_id);
	if (it == shared_texture_slot_resources.end())
		return nullptr;
	return &it->second.as<owned_shared_resource<texture_slot_resources>>().value;
}

bool graph_runtime::ensure_texture_slot_resource(int slot_id, std::string& status) {
	nodes::texture_slot_state* slot = graph.find_texture_slot(slot_id);
	if (slot == nullptr) {
		status = "Texture slot is missing.";
		return false;
	}
	if (slot->path.empty()) {
		slot->status = "No file selected.";
		status = slot->status;
		return false;
	}

	texture_slot_resources* resources = shared_texture_slot_resource(slot_id);
	if (resources == nullptr) {
		NE_RuntimeValue owned = NE_RuntimeValue::make<owned_shared_resource<texture_slot_resources>>();
		auto& shared = owned.as<owned_shared_resource<texture_slot_resources>>();
		shared.device = &device;
		resources = &shared.value;
		shared_texture_slot_resources[slot_id] = std::move(owned);
	}
	const bool ok = load_texture_slot_from_file(*resources, slot->path, status);
	slot->status = status;
	return ok;
}

present_resources* graph_runtime::ensure_present_resources(const mars::render_pass& render_pass) {
	const void* render_pass_key = render_pass.data.get<void>();
	if (auto it = present_by_pass.find(render_pass_key); it != present_by_pass.end() && it->second.valid)
		return &it->second;

	static constexpr char shader_source_bytes[] = {
#embed "shaders/runtime_present.hlsl"
		,
		'\0'
	};
	static constexpr std::string_view shader_source(shader_source_bytes, sizeof(shader_source_bytes) - 1);

	std::vector<mars::shader_module> modules = {
		{
			.type = MARS_SHADER_TYPE_VERTEX,
			.path = {},
			.source = shader_source,
			.name = "runtime.present"
		},
		{
			.type = MARS_SHADER_TYPE_FRAGMENT,
			.path = {},
			.source = shader_source,
			.name = "runtime.present"
		},
	};

	present_resources present = {};
	present.shader = mars::graphics::shader_create(device, modules);

	mars::pipeline_setup setup;
	setup.pipeline_shader = present.shader;
	setup.push_constant_count = sizeof(present_push_constants) / sizeof(std::uint32_t);
	setup.push_constant_stage = MARS_PIPELINE_STAGE_FRAGMENT;
	present.pipeline = mars::graphics::pipeline_create(device, render_pass, setup);
	present.valid = present.pipeline.engine != nullptr;

	if (!present.valid) {
		present.error = "Failed to create the swapchain present pipeline.";
		present.destroy(device);
		return nullptr;
	}

	auto [it, _] = present_by_pass.emplace(render_pass_key, std::move(present));
	return &it->second;
}

} // namespace rv
