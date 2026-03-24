#include <render_visualizer/nodes/camera_node.hpp>

namespace rv::nodes {

void camera_node::configure(NodeTypeInfo& info) {
	info.meta.vm_reexecute_each_tick = true;
}

bool camera_node::execute(rv::graph_execution_context& ctx, NE_Node& node, std::string& error) {
	mars::matrix4<float> value(1.0f);
	if (!ctx.read_blackboard<mars::matrix4<float>>("camera_view_proj", value, error))
		return false;
	ctx.set_output(node, "view_proj", value);
	return true;
}

} // namespace rv::nodes
