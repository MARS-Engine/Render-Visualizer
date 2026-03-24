#include <render_visualizer/runtime/services.hpp>

namespace rv {

graph_services* require_build_services(graph_build_context& ctx, std::string& error) {
	if (ctx.services == nullptr) {
		error = "Build context has no runtime services.";
		return nullptr;
	}
	return ctx.services;
}

} // namespace rv
