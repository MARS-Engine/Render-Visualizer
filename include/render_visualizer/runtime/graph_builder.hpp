#pragma once

#include <render_visualizer/node/node_registry.hpp>
#include <render_visualizer/runtime/frame_stack.hpp>

#include <mars/math/vector2.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mars/meta/type_erased.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rv {

struct graph_builder_pin_link_target {
	std::uint16_t node_id = 0;
	std::string pin_name = {};
};

struct graph_builder_pin_links {
	std::string name = {};
	std::vector<graph_builder_pin_link_target> targets = {};
};

struct graph_builder_node {
	std::uint16_t id = 0;
	std::size_t type_hash = 0;
	std::string type_key = {};
	std::string_view name = {};
	node_metadata metadata = {};
	pin_draw_info_fn get_pin_draw_info = nullptr;
	std::shared_ptr<void> instance = {};
	mars::meta::type_erased_ptr instance_ptr = {};
	bool selected = false;
	mars::vector2<float> position = {};
	mars::vector2<float> size = {};
	std::vector<graph_builder_pin_links> links = {};
};

struct frame_pin_copy_operation {
	mars::meta::type_erased_ptr source_ptr = {};
	mars::meta::type_erased_ptr target_ptr = {};
	pin_value_copy_fn copy_value = nullptr;
};

struct frame_execution_step {
	mars::meta::type_erased_ptr instance_ptr = {};
	node_execute_invoke_fn invoke = nullptr;
	std::uint16_t node_id = 0;
	std::vector<frame_pin_copy_operation> copy_operations = {};
};

struct graph_frame_build_result {
	frame_stack stack = {};
	std::vector<frame_execution_step> steps = {};
	std::size_t source_revision = 0;
	bool valid = false;
	std::string error_message = {};
};

class graph_builder {
public:
	using container_type = std::vector<graph_builder_node>;
	using iterator = container_type::iterator;
	using const_iterator = container_type::const_iterator;

	graph_builder();

	iterator begin() { return m_nodes.begin(); }
	iterator end() { return m_nodes.end(); }
	const_iterator begin() const { return m_nodes.begin(); }
	const_iterator end() const { return m_nodes.end(); }
	const_iterator cbegin() const { return m_nodes.cbegin(); }
	const_iterator cend() const { return m_nodes.cend(); }

	void clear();

	graph_builder_node& add(std::size_t _type_hash, std::string_view _name, pin_draw_info_fn _get_pin_draw_info, const mars::vector2<float>& _position);
	graph_builder_node& add(const node_registry_entry& _entry, const mars::vector2<float>& _position);

	template<typename T>
	graph_builder_node& add(const mars::vector2<float>& _position) {
		return add(node_registry_entry{
			.type_hash = mars::hash::type_fingerprint_v<T>,
			.type_key = std::string(mars::hash::type_fingerprint_string<T>()),
			.name = node_reflection<T>::name,
			.hidden = node_reflection<T>::hidden,
			.metadata = node_reflection<T>::get_metadata(),
			.get_pin_draw_info = &node_reflection<T>::get_pin_draw_info,
			.create_instance = &detail::create_node_instance<T>
		}, _position);
	}

	graph_builder_node* find_node(std::uint16_t _id);
	const graph_builder_node* find_node(std::uint16_t _id) const;
	graph_builder_node* selected_node();
	const graph_builder_node* selected_node() const;
	const graph_builder_node* start_node() const;

	bool remove_node(std::uint16_t _id);
	bool remove_selected_node();
	void clear_selection();

	static void collect_pins(const graph_builder_node& _node, std::vector<pin_draw_data>& _inputs, std::vector<pin_draw_data>& _outputs);
	static std::optional<pin_draw_data> find_pin(const graph_builder_node& _node, std::string_view _pin_name, bool _is_output);

	bool add_link(std::uint16_t _from_node_id, std::string_view _from_pin_name, std::uint16_t _to_node_id, std::string_view _to_pin_name);
	void mark_runtime_dirty();
	std::size_t runtime_revision() const;
	graph_frame_build_result build_frame() const;

private:
	static void start_node_pin_draw_info(mars::meta::type_erased_ptr _instance, std::vector<pin_draw_data>& _inputs, std::vector<pin_draw_data>& _outputs);
	static node_metadata start_node_metadata();
	static std::vector<graph_builder_pin_links> make_pin_links(pin_draw_info_fn _get_pin_draw_info);

	void reset_with_start_node();
	bool input_has_source(std::uint16_t _node_id, std::string_view _pin_name) const;

	std::vector<graph_builder_node> m_nodes = {};
	std::uint16_t m_next_node_id = 0;
	std::size_t m_runtime_revision = 0;
};

} // namespace rv
