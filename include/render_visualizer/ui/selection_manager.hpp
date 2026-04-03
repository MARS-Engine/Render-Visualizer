#pragma once

#include <mars/event/event.hpp>

#include <mars/meta/type_erased.hpp>

#include <cstddef>
#include <optional>

namespace rv {

struct graph_builder_node;

struct selection_event {
	void on_selection_changed(const mars::meta::type_erased_ptr& _selection);
};

class selection_manager : public mars::event<selection_event> {
public:
	void select_node(graph_builder_node* _node);
	void select_variable(std::size_t _index);
	void clear_selection();

	graph_builder_node* selected_node() const;
	std::optional<std::size_t> selected_variable() const;

private:
	graph_builder_node* m_selected_node = nullptr;
	std::optional<std::size_t> m_selected_variable = std::nullopt;
};

} // namespace rv
