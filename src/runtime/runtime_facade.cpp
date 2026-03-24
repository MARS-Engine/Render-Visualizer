#include <render_visualizer/runtime/impl.hpp>

#include <mars/debug/logger.hpp>

namespace rv {

namespace {

mars::log_channel g_app_log_channel("app");

}

graph_runtime::graph_runtime(
	NodeGraph& graph_ref,
	const NodeRegistry& registry_ref,
	const mars::device& device_ref,
	mars::graphics::object::main_pass_object<main_pass_desc>& main_pass_ref,
	mars::graphics::object::swapchain& swapchain_ref
) : graph(graph_ref),
    registry(registry_ref),
    device(device_ref),
    main_pass(main_pass_ref),
    swapchain(swapchain_ref) {
	builder.owner = this;
	executor.owner = this;
	build_services.graph = &graph;
	build_services.device = &device;
	build_services.runtime = this;
	build_services.swapchain_size = swapchain.swapchain_size();
}

graph_runtime::~graph_runtime() {
	mars::graphics::device_flush(device);
	destroy_all();
}

bool graph_runtime::set_blackboard_value(std::string_view name, size_t type_hash, std::any value) {
	std::string error;
	const bool ok = write_blackboard_value(name, type_hash, value, error);
	if (!ok && !error.empty())
		mars::logger::error(g_app_log_channel, "Failed to set blackboard '{}': {}", name, error);
	return ok;
}

} // namespace rv
