#include <render_visualizer/runtime/frame_executor.hpp>

#include <render_visualizer/type_registry.hpp>
#include <render_visualizer/ui/selection_manager.hpp>

#include <mars/debug/logger.hpp>
#include <mars/hash/meta.hpp>

namespace rv {

namespace {
mars::log_channel g_executor_log("executor");
}

frame_executor::frame_executor() {
	auto initial = std::make_unique<function_instance>();
	initial->name = "Main";
	m_functions.push_back(std::move(initial));
}

std::vector<std::unique_ptr<function_instance>>& frame_executor::functions() { return m_functions; }
const std::vector<std::unique_ptr<function_instance>>& frame_executor::functions() const { return m_functions; }
std::size_t frame_executor::active_function_index() const { return m_active_function; }
function_instance& frame_executor::active_function() { return *m_functions[m_active_function]; }
const function_instance& frame_executor::active_function() const { return *m_functions[m_active_function]; }
std::vector<std::unique_ptr<variable>>& frame_executor::global_variables() { return m_global_variables; }
const std::vector<std::unique_ptr<variable>>& frame_executor::global_variables() const { return m_global_variables; }

void frame_executor::create_function(std::string _name) {
	auto f = std::make_unique<function_instance>();
	f->name = std::move(_name);
	m_functions.push_back(std::move(f));
}

void frame_executor::select_function(std::size_t _index) {
	if (_index < m_functions.size())
		m_active_function = _index;
}

void frame_executor::delete_function(std::size_t _index) {
	if (m_functions.size() <= 1 || _index >= m_functions.size())
		return;
	m_functions.erase(m_functions.begin() + _index);
	if (m_active_function >= m_functions.size())
		m_active_function = m_functions.size() - 1;
	else if (m_active_function > _index)
		--m_active_function;
}

void frame_executor::create_variable(std::string _name, const type_registry& _type_registry) {
	auto v = std::make_unique<variable>();
	v->name = std::move(_name);
	v->set_type(mars::hash::type_fingerprint_v<float>, _type_registry);
	m_global_variables.push_back(std::move(v));
}

void frame_executor::delete_variable(std::size_t _index, selection_manager& _selection) {
	if (m_global_variables.empty() || _index >= m_global_variables.size())
		return;
	m_global_variables.erase(m_global_variables.begin() + _index);
	const auto sel = _selection.selected_variable();
	if (!sel.has_value())
		return;
	if (*sel == _index)
		_selection.clear_selection();
	else if (*sel > _index)
		_selection.select_variable(*sel - 1);
	else if (*sel >= m_global_variables.size() && !m_global_variables.empty())
		_selection.select_variable(m_global_variables.size() - 1);
}

bool frame_executor::remove_selected_node() {
	if (m_functions.empty())
		return false;
	return m_functions[m_active_function]->graph.remove_selected_node();
}

bool frame_executor::start(graph_builder& _builder) {
	return start(_builder.build_frame());
}

bool frame_executor::start(graph_frame_build_result&& _build) {
	if (!_build.valid)
		return false;
	stop();
	m_stack = std::move(_build.stack);
	m_steps = std::move(_build.steps);
	m_source_revision = _build.source_revision;
	m_running = true;
	return true;
}

void frame_executor::stop() {
	m_running = false;
	m_source_revision = 0;
	m_steps.clear();
	m_stack.clear();
}

void frame_executor::tick() {
	if (m_running && active_function().graph.runtime_revision() != m_source_revision) {
		if (!start(active_function().graph)) {
			mars::logger::error(g_executor_log, "Failed to rebuild graph execution");
			stop();
		}
	}

	if (!m_running)
		return;

	for (const frame_execution_step& step : m_steps) {
		for (const frame_pin_copy_operation& copy_operation : step.copy_operations) {
			if (copy_operation.copy_value == nullptr)
				continue;
			copy_operation.copy_value(copy_operation.target_ptr, copy_operation.source_ptr);
		}
		if (step.invoke == nullptr)
			continue;
		step.invoke(step.instance_ptr, nullptr, 0);
	}
}

void frame_executor::request_start() { broadcast<&execution_event::on_start_requested>(); }
void frame_executor::request_stop() { broadcast<&execution_event::on_stop_requested>(); }
void frame_executor::mark_graph_inputs_changed() { broadcast<&execution_event::on_graph_inputs_changed>(); }
bool frame_executor::running() const { return m_running; }
std::size_t frame_executor::source_revision() const { return m_source_revision; }

} // namespace rv
