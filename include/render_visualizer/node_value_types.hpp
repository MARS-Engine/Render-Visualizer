#pragma once

#include <imgui.h>

#include <render_visualizer/graph_model_types.hpp>
#include <render_visualizer/node_annotations.hpp>
#include <render_visualizer/utils/type_erased_storage.hpp>

#include <mars/graphics/backend/pipeline.hpp>
#include <mars/imgui/struct_editor.hpp>
#include <mars/math/vector2.hpp>
#include <mars/math/vector4.hpp>
#include <mars/meta.hpp>
#include <mars/parser/json/json.hpp>
#include <mars/parser/parser.hpp>

#include <algorithm>
#include <functional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

struct NE_Node;
struct NodeGraph;
struct NodeTypeInfo;
namespace rv { struct graph_execution_context; }
namespace rv { struct graph_build_context; }
namespace rv { struct graph_build_result; }
namespace rv { struct graph_services; }

namespace rv::detail {
	template <typename T>
	consteval size_t pin_type_hash();

	template <typename T>
	struct value_vector_traits {
		static constexpr bool is_vector = false;
		using element_t = T;
	};

	template <typename T, typename Allocator>
	struct value_vector_traits<std::vector<T, Allocator>> {
		static constexpr bool is_vector = true;
		using element_t = T;
	};

	inline thread_local std::string g_processor_status_message;

	inline void clear_processor_status_message() {
		g_processor_status_message.clear();
	}

	inline void set_processor_status_message(std::string message) {
		g_processor_status_message = std::move(message);
	}

	inline std::string take_processor_status_message() {
		std::string result = std::move(g_processor_status_message);
		g_processor_status_message.clear();
		return result;
	}

	template <typename T>
	inline std::string generic_json_stringify(T value) {
		std::string output;
		mars::json::json_type_parser<T>::stringify(value, output);
		return output;
	}

	template <typename T>
	inline bool generic_json_parse(std::string_view json, T& value) {
		if (json.empty())
			return false;
		std::string_view::iterator it = mars::json::json_type_parser<T>::parse(json, value);
		if (it == json.begin())
			return false;
		it = mars::parse::first_space<false>(it, json.end());
		return it == json.end();
	}

}

enum class NE_PinKind {
	data,
	exec
};

struct NE_WildcardTypeInfo {
	size_t type_hash = 0;
	bool is_container = false;
	bool has_virtual_struct = false;
	std::string virtual_struct_name;
	size_t virtual_struct_layout_fingerprint = 0;

	bool valid() const {
		return type_hash != 0;
	}
};

struct NE_WildcardValueView {
	const void* storage = nullptr;
	NE_WildcardTypeInfo type;

	template <typename T>
	bool is() const {
		using value_t = std::remove_cvref_t<T>;
		return storage != nullptr &&
			   type.type_hash == rv::detail::pin_type_hash<value_t>() &&
			   !type.is_container;
	}

	template <typename T>
	const T& as() const {
		return *static_cast<const T*>(storage);
	}
};

struct NE_WildcardValue : rv::detail::type_erased_storage {
	NE_WildcardTypeInfo type;

	NE_WildcardValue() = default;
	NE_WildcardValue(const NE_WildcardValue&) = delete;
	NE_WildcardValue& operator=(const NE_WildcardValue&) = delete;

	NE_WildcardValue(NE_WildcardValue&& other) noexcept
		: type_erased_storage(std::move(other)),
		  type(std::move(other.type)) {
		other.type = {};
	}

	NE_WildcardValue& operator=(NE_WildcardValue&& other) noexcept {
		if (this == &other)
			return *this;
		type_erased_storage::operator=(std::move(other));
		type = std::move(other.type);
		other.type = {};
		return *this;
	}

	template <typename T>
	static NE_WildcardValue make(T value) {
		NE_WildcardValue result;
		result.template emplace<T>(std::move(value));
		return result;
	}

	void reset() {
		type_erased_storage::reset();
		type = {};
	}
};

struct NE_Pin {
	int id = -1;
	std::string label;
	std::string display_label;
	size_t type_hash = 0;
	bool is_container = false;
	bool required = true;
	bool generated = false;
	NE_PinKind kind = NE_PinKind::data;
	bool has_virtual_struct = false;
	std::string virtual_struct_name;
	size_t virtual_struct_layout_fingerprint = 0;
	size_t template_base_type_hash = 0;
	size_t template_value_hash = 0;
	std::string template_display_name;
	bool is_wildcard = false;
	std::string wildcard_group;
	bool wildcard_resolved = false;
};

struct NE_ProcessorParamDescriptor {
	int id = -1;
	std::string label;
	size_t type_hash = 0;
	bool is_container = false;
};

struct NE_ProcessorParamValue : rv::detail::type_erased_storage {
	int id = -1;
	std::string label;
	size_t type_hash = 0;
	bool is_container = false;
	bool (*render)(const char*, void*) = nullptr;

	NE_ProcessorParamValue() = default;
	NE_ProcessorParamValue(const NE_ProcessorParamValue&) = delete;
	NE_ProcessorParamValue& operator=(const NE_ProcessorParamValue&) = delete;

	NE_ProcessorParamValue(NE_ProcessorParamValue&& other) noexcept
		: type_erased_storage(std::move(other)),
		  id(other.id),
		  label(std::move(other.label)),
		  type_hash(other.type_hash),
		  is_container(other.is_container),
		  render(other.render) {
		other.render = nullptr;
	}

	NE_ProcessorParamValue& operator=(NE_ProcessorParamValue&& other) noexcept {
		if (this == &other)
			return *this;

		type_erased_storage::operator=(std::move(other));
		id = other.id;
		label = std::move(other.label);
		type_hash = other.type_hash;
		is_container = other.is_container;
		render = other.render;
		other.render = nullptr;
		return *this;
	}

	template <typename T>
	static NE_ProcessorParamValue make(int param_id, std::string param_label, bool container = false) {
		NE_ProcessorParamValue value;
		value.id = param_id;
		value.label = std::move(param_label);
		value.type_hash = rv::detail::pin_type_hash<T>();
		value.is_container = container;
		value.template emplace<T>();
		value.render = [](const char* label, void* ptr) {
			T& value_ref = *static_cast<T*>(ptr);
			mars::imgui::struct_editor<T> editor(value_ref);
			return editor.render(label);
		};
		return value;
	}

	bool render_editor() {
		if (render != nullptr && storage != nullptr)
			return render(label.c_str(), storage);
		return false;
	}

	void reset() {
		type_erased_storage::reset();
		render = nullptr;
	}
};

struct NE_InlineInputValue {
	std::string label;
	std::string json;
	bool enabled = false;
};

struct NE_RuntimeValue : rv::detail::type_erased_storage {
	using type_erased_storage::type_erased_storage;
	using type_erased_storage::operator=;

	template <typename T>
	static NE_RuntimeValue make() {
		NE_RuntimeValue value;
		value.template emplace<T>();
		return value;
	}
};

struct NE_CustomState : rv::detail::type_erased_storage {
	using type_erased_storage::type_erased_storage;
	using type_erased_storage::operator=;

	template <typename T>
	static NE_CustomState make() {
		NE_CustomState value;
		value.template emplace<T>();
		return value;
	}
};

namespace rv::detail {
	template <typename T>
	consteval size_t pin_type_hash() {
		using value_t = std::remove_cvref_t<T>;
		return mars::hash::type_fingerprint_v<value_t>;
	}
} // namespace rv::detail
