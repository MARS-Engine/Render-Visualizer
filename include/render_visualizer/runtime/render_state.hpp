#pragma once

#include <render_visualizer/runtime/types.hpp>

namespace rv::runtime_detail {

struct render_state {
	const mars::command_buffer* active_record_cmd = nullptr;
	size_t active_record_frame = 0;
	const render_pass_resources* active_render_pass = nullptr;

	void reset_record_state(const mars::command_buffer& cmd, size_t current_frame);
	void finish_record_state();
};

} // namespace rv::runtime_detail
