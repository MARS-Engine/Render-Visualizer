#include <render_visualizer/ui/selection_manager.hpp>

namespace rv {

void selection_manager::select_node(graph_builder_node* _node) {
	m_selected_node = _node;
	m_selected_variable = std::nullopt;
	broadcast<&selection_event::on_selection_changed>(mars::meta::type_erased_ptr(_node));
}

void selection_manager::select_variable(std::size_t _index) {
	m_selected_node = nullptr;
	m_selected_variable = _index;
	broadcast<&selection_event::on_selection_changed>(mars::meta::type_erased_ptr(&*m_selected_variable));
}

void selection_manager::clear_selection() {
	m_selected_node = nullptr;
	m_selected_variable = std::nullopt;
	broadcast<&selection_event::on_selection_changed>(mars::meta::type_erased_ptr());
}

graph_builder_node* selection_manager::selected_node() const {
	return m_selected_node;
}

std::optional<std::size_t> selection_manager::selected_variable() const {
	return m_selected_variable;
}

} // namespace rv
