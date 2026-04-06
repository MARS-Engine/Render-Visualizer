#pragma once

#include <render_visualizer/type_reflection.hpp>

#include <mars/hash/meta.hpp>

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace rv {

class type_registry {
public:
	using registration_fn = void (*)(type_registry&);

	struct type_auto_registrar {
		explicit type_auto_registrar(registration_fn _register_fn);
	};

	type_registry();

	void add(variable_type_desc _entry);

	template<typename T>
	void register_type() {
		add({
			.name = type_reflection<T>::name,
			.type_hash = mars::hash::type_fingerprint_v<T>,
			.type_key = std::string(mars::hash::type_fingerprint_string<T>()),
			.colour = type_reflection<T>::colour,
			.size = sizeof(T),
			.alignment = alignof(T),
			.construct = &construct_default<T>,
			.destroy = &destroy_default<T>,
			.copy_value = &copy_default<T>,
			.erase_ptr = &erase_ptr_default<T>,
			.json_stringify = &json_stringify_default<T>,
			.json_parse = &json_parse_default<T>
		});
	}

	template<typename T>
	static void register_reflected(type_registry& _registry) {
		_registry.template register_type<T>();
	}

	std::span<const variable_type_desc> registered_types() const;

	const variable_type_desc* find(std::size_t _type_hash) const;
	const variable_type_desc* find_by_key(std::string_view _type_key) const;

	static void add_registration(registration_fn _register_fn);

private:
	void import_registration_catalog();
	static std::vector<registration_fn>& global_registration_catalog();

	std::vector<variable_type_desc> m_entries = {};
};

template<typename T>
inline const type_registry::type_auto_registrar auto_register_type_v { &type_registry::register_reflected<T> };

} // namespace rv
