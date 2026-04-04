#pragma once

#include <mars/math/vector3.hpp>
#include <mars/hash/meta.hpp>
#include <mars/meta/type_erased.hpp>
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
	mars::vector3<unsigned char> colour;
	std::size_t size;
	std::size_t alignment;
	void (*construct)(void*);
	void (*destroy)(void*);
	void (*copy_value)(mars::meta::type_erased_ptr, mars::meta::type_erased_ptr);
	mars::meta::type_erased_ptr (*erase_ptr)(void*);
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

inline std::span<const variable_type_desc> get_available_variable_types() {
	static const variable_type_desc types[] = {
		{ type_reflection<float>::name, mars::hash::type_fingerprint_v<float>, type_reflection<float>::colour, sizeof(float), alignof(float), &construct_default<float>, &destroy_default<float>, &copy_default<float>, &erase_ptr_default<float> },
		{ type_reflection<int>::name, mars::hash::type_fingerprint_v<int>, type_reflection<int>::colour, sizeof(int), alignof(int), &construct_default<int>, &destroy_default<int>, &copy_default<int>, &erase_ptr_default<int> },
		{ type_reflection<bool>::name, mars::hash::type_fingerprint_v<bool>, type_reflection<bool>::colour, sizeof(bool), alignof(bool), &construct_default<bool>, &destroy_default<bool>, &copy_default<bool>, &erase_ptr_default<bool> },
	};
	return types;
}

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

	void set_type(std::size_t _type_hash) {
		for (const auto& t : get_available_variable_types()) {
			if (t.type_hash == _type_hash) {
				if (memory) {
					if (type && type->destroy)
						type->destroy(memory);
					mars::aligned_free(memory);
				}
				type = &t;
				memory = mars::aligned_malloc(type->alignment, type->size);
				if (memory && type->construct)
					type->construct(memory);
				break;
			}
		}
	}
};

}