#pragma once

#include <mars/math/vector3.hpp>
#include <mars/hash/meta.hpp>
#include <mars/meta/type_erased.hpp>
#include <mars/parser/json/json.hpp>
#include <mars/utility/memory.hpp>

#include <malloc.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace rv {

template<typename T>
struct type_reflection {
	static constexpr std::string_view name = "Unknown";
	static constexpr mars::vector3<unsigned char> colour = { 128, 128, 128 };
};

template<>
struct type_reflection<float> {
	static constexpr std::string_view name = "Float";
	static constexpr mars::vector3<unsigned char> colour = { 173, 216, 230 };
};

template<>
struct type_reflection<int> {
	static constexpr std::string_view name = "Int";
	static constexpr mars::vector3<unsigned char> colour = { 144, 238, 144 };
};

template<>
struct type_reflection<bool> {
	static constexpr std::string_view name = "Bool";
	static constexpr mars::vector3<unsigned char> colour = { 255, 182, 193 };
};

struct variable_type_desc {
	std::string_view name;
	std::size_t type_hash;
	std::string type_key;
	mars::vector3<unsigned char> colour;
	std::size_t size;
	std::size_t alignment;
	void (*construct)(void*);
	void (*destroy)(void*);
	void (*copy_value)(mars::meta::type_erased_ptr, mars::meta::type_erased_ptr);
	mars::meta::type_erased_ptr (*erase_ptr)(void*);
	void (*json_stringify)(void*, std::string&);
	bool (*json_parse)(std::string_view, void*);
};

template<typename T>
void construct_default(void* _ptr) {
	new (_ptr) T();
}

template<typename T>
void destroy_default(void* _ptr) {
	static_cast<T*>(_ptr)->~T();
}

template<typename T>
void copy_default(mars::meta::type_erased_ptr _dest, mars::meta::type_erased_ptr _src) {
	if (T* dest = _dest.get<T>(); dest != nullptr) {
		if (T* src = _src.get<T>(); src != nullptr) {
			*dest = *src;
		}
	}
}

template<typename T>
mars::meta::type_erased_ptr erase_ptr_default(void* _ptr) {
	return mars::meta::type_erased_ptr(static_cast<T*>(_ptr));
}

template<typename T>
void json_stringify_default(void* _ptr, std::string& _out) {
	if (_ptr == nullptr)
		return;

	T& value = *static_cast<T*>(_ptr);
	mars::json::json_type_parser<T>::stringify(value, _out);
}

template<typename T>
bool json_parse_default(std::string_view _json, void* _ptr) {
	if (_ptr == nullptr)
		return false;

	T& value = *static_cast<T*>(_ptr);
	std::string wrapped_json(_json);
	wrapped_json += ',';

	const std::string_view wrapped_view = wrapped_json;
	const auto parse_end = mars::json::json_type_parser<T>::parse(wrapped_view, value);
	
	if (parse_end == wrapped_view.end())
		return false;

	const auto trailing = mars::parse::first_space<false>(parse_end, wrapped_view.end());
	return trailing != wrapped_view.end() && *trailing == ',';
}

// Forward-declared so variable can reference it in set_type without a circular include.
class type_registry;

struct variable {
	std::string name;
	const variable_type_desc* type = nullptr;
	void* memory = nullptr;

	~variable() {
		if (memory) {
			if (type && type->destroy)
				type->destroy(memory);
			mars::aligned_free(memory);
		}
	}

	void set_type(std::size_t _type_hash, const type_registry& _registry);
};

} // namespace rv
