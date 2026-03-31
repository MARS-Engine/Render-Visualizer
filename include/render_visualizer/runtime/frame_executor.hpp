#pragma once

#include <render_visualizer/runtime/graph_builder.hpp>

#include <cstddef>
#include <vector>

namespace rv {

class frame_executor {
public:
	bool start(graph_frame_build_result&& _build);
	void stop();
	void tick();

	bool running() const;
	std::size_t source_revision() const;

private:
	frame_stack m_stack = {};
	std::vector<const void*> m_execute_members = {};
	std::vector<frame_execution_step> m_steps = {};
	std::size_t m_source_revision = 0;
	bool m_running = false;
};

} // namespace rv
