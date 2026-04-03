#pragma once

#include <render_visualizer/runtime/function_instance.hpp>
#include <render_visualizer/type_reflection.hpp>

#include <mars/event/event.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rv {

class selection_manager;

struct execution_event {
	void on_start_requested();
	void on_stop_requested();
	void on_graph_inputs_changed();
};

class frame_executor : public mars::event<execution_event> {
public:
	frame_executor();

	// --- project-level state ---
	std::vector<std::unique_ptr<function_instance>>& functions();
	const std::vector<std::unique_ptr<function_instance>>& functions() const;
	std::size_t active_function_index() const;
	function_instance& active_function();
	const function_instance& active_function() const;
	std::vector<std::unique_ptr<variable>>& global_variables();
	const std::vector<std::unique_ptr<variable>>& global_variables() const;

	void create_function(std::string _name);
	void select_function(std::size_t _index);
	void delete_function(std::size_t _index);
	void create_variable(std::string _name);
	void delete_variable(std::size_t _index, selection_manager& _selection);
	bool remove_selected_node();

	// --- execution lifecycle ---
	bool start(graph_builder& _builder);
	bool start(graph_frame_build_result&& _build);
	void stop();
	void tick();

	void request_start();
	void request_stop();
	void mark_graph_inputs_changed();

	bool running() const;
	std::size_t source_revision() const;

private:
	std::vector<std::unique_ptr<function_instance>> m_functions;
	std::size_t m_active_function = 0;
	std::vector<std::unique_ptr<variable>> m_global_variables;

	frame_stack m_stack = {};
	std::vector<frame_execution_step> m_steps = {};
	std::size_t m_source_revision = 0;
	bool m_running = false;
};

} // namespace rv
