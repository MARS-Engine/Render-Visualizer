#include <render_visualizer/runtime/frame_executor.hpp>

namespace rv {

bool frame_executor::start(graph_frame_build_result&& _build) {
	if (!_build.valid)
		return false;

	stop();

	m_stack = std::move(_build.stack);
	m_execute_members = std::move(_build.execute_members);
	m_steps = std::move(_build.steps);
	m_source_revision = _build.source_revision;
	m_running = true;
	return true;
}

void frame_executor::stop() {
	m_running = false;
	m_source_revision = 0;
	m_execute_members.clear();
	m_steps.clear();
	m_stack.clear();
}

void frame_executor::tick() {
	if (!m_running)
		return;

	for (const frame_execution_step& step : m_steps) {
		for (const frame_pin_copy_operation& copy_operation : step.copy_operations) {
			if (copy_operation.source_resolve == nullptr || copy_operation.target_resolve == nullptr || copy_operation.copy_value == nullptr)
				continue;

			mars::meta::type_erased_ptr source_value = copy_operation.source_resolve(m_stack.entry_ptr(copy_operation.source_stack_index), copy_operation.source_member_handle);
			mars::meta::type_erased_ptr target_value = copy_operation.target_resolve(m_stack.entry_ptr(copy_operation.target_stack_index), copy_operation.target_member_handle);
			copy_operation.copy_value(target_value, source_value);
		}

		if (step.invoke == nullptr || step.execute_member_index >= m_execute_members.size())
			continue;
		step.invoke(m_stack.entry_ptr(step.stack_index), m_execute_members[step.execute_member_index]);
	}
}

bool frame_executor::running() const {
	return m_running;
}

std::size_t frame_executor::source_revision() const {
	return m_source_revision;
}

} // namespace rv
