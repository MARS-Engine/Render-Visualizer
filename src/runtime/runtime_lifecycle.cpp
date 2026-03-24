#include <render_visualizer/runtime/impl.hpp>

namespace rv {

std::string runtime_detail::state_name(bool running, bool dirty, bool has_error) {
	if (has_error)
		return "Error";
	if (!running)
		return "Stopped";
	if (dirty)
		return "Dirty";
	return "Running";
}

// Core runtime lifecycle

void graph_runtime::attach_graph_callbacks() {
	graph.on_node_spawned = [&](const NE_Node&) { mark_dirty(); };
	graph.on_node_removed = [&](int) { mark_dirty(); };
	graph.on_link_created = [&](const NE_Link&) { mark_dirty(); };
	graph.on_link_removed = [&](const NE_Link&) { mark_dirty(); };
	graph.on_node_moved = [&](int) {};
	graph.on_graph_dirty = [&]() { mark_dirty(); };
}

void graph_runtime::mark_dirty() { dirty = true; }

void graph_runtime::start() {
	running = true;
	dirty = true;
	has_error = false;
	setup_pending = true;
	last_error.clear();
}

void graph_runtime::stop() {
	mars::graphics::device_flush(device);
	destroy_all();
	running = false;
	dirty = true;
}

void graph_runtime::destroy_resources() {
	for (auto& [_, present] : present_by_pass)
		present.destroy(device);
	present_by_pass.clear();
	render.finish_record_state();
	for (auto it = owned_resources.rbegin(); it != owned_resources.rend(); ++it)
		it->reset();
	owned_resources.clear();
	for (auto& [_, resource] : shared_texture_slot_resources)
		resource.reset();
	shared_texture_slot_resources.clear();
	for (auto& node : graph.nodes)
		node.runtime_value.reset();
	plan = {};
	global_slot_values.clear();
	frames_by_function.clear();
	variable_values.clear();
	vm_outputs.clear();
	vm_evaluating.clear();
	pending_vm_event_types.clear();
	call_frames.clear();
	active_function_call_counts.clear();
}

void graph_runtime::destroy_all() {
	destroy_resources();
	steps.clear();
	last_error.clear();
	last_gpu_step_count = 0;
	last_exec_instance_count = 0;
	last_skipped_instance_count = 0;
	blackboard = {};
	setup_pending = true;
	has_error = false;
}

} // namespace rv
