#pragma once

#include <any>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <render_visualizer/node_value_types.hpp>

#include <mars/hash/meta.hpp>

struct NE_Node;

namespace rv {
struct graph_services;
class graph_runtime;

struct graph_build_result {
	std::string kind;
	std::string status;
	size_t executed_count = 0;
};

struct graph_build_context {
	graph_services* services = nullptr;
};

template <typename T>
using execution_vector_traits = rv::detail::value_vector_traits<T>;

struct graph_execution_context {
	struct execution_item {
		size_t index = 0;
	};

	struct execution_batch {
		std::vector<std::any> items;

		size_t size() const { return items.size(); }
		bool empty() const { return items.empty(); }
	};

	float delta_time = 0.0f;
	float time = 0.0f;
	size_t current_item_index = 0;
	size_t current_item_count = 1;
	graph_runtime* runtime = nullptr;

	bool resolve_input_any(const NE_Node& node, std::string_view label, size_t expected_type_hash, bool expected_container, size_t item_index, size_t item_count, std::any& out_value, std::string& error) const;
	bool resolve_wildcard_input(const NE_Node& node, std::string_view label, NE_WildcardValue& value, std::string& error) const;
	bool read_function_input_any(const NE_Node& node, std::string_view label, std::any& out_value, std::string& error) const;
	bool write_function_output_any(const NE_Node& node, std::string_view label, std::any value, std::string& error) const;
	bool call_function(NE_Node& node, std::string& error) const;
	void begin_output(const NE_Node& node, std::string_view label) const;
	void push_output_any(const NE_Node& node, std::string_view label, std::any value) const;
	bool set_wildcard_output(const NE_Node& node, std::string_view label, const NE_WildcardValue& value, std::string& error) const;
	bool read_blackboard_any(std::string_view name, size_t expected_type_hash, std::any& out_value, std::string& error) const;
	bool write_blackboard_any(std::string_view name, size_t expected_type_hash, std::any value, std::string& error) const;
	bool read_variable_any(int variable_id, size_t expected_type_hash, bool expected_container, std::any& out_value, std::string& error) const;
	bool write_variable_input(const NE_Node& node, int variable_id, std::string_view input_label, std::string& error) const;

	template <typename T>
	bool resolve_input(const NE_Node& node, std::string_view label, T& value, std::string& error) const {
		using value_t = std::remove_cvref_t<T>;
		using element_t = typename execution_vector_traits<value_t>::element_t;
		std::any any_value;
		if (!resolve_input_any(
			node,
			label,
			rv::detail::pin_type_hash<element_t>(),
			execution_vector_traits<value_t>::is_vector,
			current_item_index,
			current_item_count,
			any_value,
			error
		))
			return false;
		if (!any_value.has_value() || any_value.type() != typeid(T)) {
			error = "Resolved input has the wrong type for '" + std::string(label) + "'.";
			return false;
		}
		value = std::any_cast<T>(any_value);
		return true;
	}

	template <typename T>
	void set_output(const NE_Node& node, std::string_view label, T value) const {
		push_output_any(node, label, std::any(std::move(value)));
	}

	template <typename T>
	bool read_function_input(const NE_Node& node, std::string_view label, T& value, std::string& error) const {
		std::any any_value;
		if (!read_function_input_any(node, label, any_value, error))
			return false;
		if (!any_value.has_value() || any_value.type() != typeid(T)) {
			error = "Function input '" + std::string(label) + "' resolved to the wrong type.";
			return false;
		}
		value = std::any_cast<T>(std::move(any_value));
		return true;
	}

	template <typename T>
	bool write_function_output(const NE_Node& node, std::string_view label, T value, std::string& error) const {
		return write_function_output_any(node, label, std::any(std::move(value)), error);
	}

	template <typename T>
	bool read_blackboard(std::string_view name, T& value, std::string& error) const {
		using value_t = std::remove_cvref_t<T>;
		using element_t = typename execution_vector_traits<value_t>::element_t;
		std::any any_value;
		if (!read_blackboard_any(
			name,
			rv::detail::pin_type_hash<element_t>(),
			any_value,
			error
		))
			return false;
		if (!any_value.has_value() || any_value.type() != typeid(T)) {
			error = "Blackboard value '" + std::string(name) + "' has the wrong type.";
			return false;
		}
		value = std::any_cast<T>(any_value);
		return true;
	}

	template <typename T>
	bool write_blackboard(std::string_view name, const T& value, std::string& error) const {
		using value_t = std::remove_cvref_t<T>;
		using element_t = typename execution_vector_traits<value_t>::element_t;
		return write_blackboard_any(
			name,
			rv::detail::pin_type_hash<element_t>(),
			std::any(value),
			error
		);
	}

	template <typename T>
	bool read_variable(int variable_id, T& value, std::string& error) const {
		using value_t = std::remove_cvref_t<T>;
		using element_t = typename execution_vector_traits<value_t>::element_t;
		std::any any_value;
		if (!read_variable_any(
			variable_id,
			rv::detail::pin_type_hash<element_t>(),
			execution_vector_traits<value_t>::is_vector,
			any_value,
			error
		))
			return false;
		if (!any_value.has_value() || any_value.type() != typeid(T)) {
			error = "Variable value has the wrong type.";
			return false;
		}
		value = std::any_cast<T>(any_value);
		return true;
	}
};

} // namespace rv
