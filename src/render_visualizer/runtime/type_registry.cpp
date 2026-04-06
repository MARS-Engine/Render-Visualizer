#include <render_visualizer/type_registry.hpp>

#include <mars/hash/meta.hpp>
#include <mars/utility/memory.hpp>

#include <algorithm>

rv::type_registry::type_auto_registrar::type_auto_registrar(registration_fn _register_fn) {
	type_registry::add_registration(_register_fn);
}

rv::type_registry::type_registry() {
	import_registration_catalog();
}

void rv::type_registry::add(variable_type_desc _entry) {
	m_entries.push_back(std::move(_entry));
}

std::span<const rv::variable_type_desc> rv::type_registry::registered_types() const {
	return m_entries;
}

const rv::variable_type_desc* rv::type_registry::find(std::size_t _type_hash) const {
	const auto it = std::ranges::find_if(m_entries, [&](const variable_type_desc& _entry) {
		return _entry.type_hash == _type_hash;
	});
	return it == m_entries.end() ? nullptr : &*it;
}

const rv::variable_type_desc* rv::type_registry::find_by_key(std::string_view _type_key) const {
	const auto it = std::ranges::find_if(m_entries, [&](const variable_type_desc& _entry) {
		return _entry.type_key == _type_key;
	});
	return it == m_entries.end() ? nullptr : &*it;
}

void rv::type_registry::add_registration(registration_fn _register_fn) {
	global_registration_catalog().push_back(_register_fn);
}

void rv::type_registry::import_registration_catalog() {
	for (registration_fn fn : global_registration_catalog())
		fn(*this);
}

std::vector<rv::type_registry::registration_fn>& rv::type_registry::global_registration_catalog() {
	static std::vector<registration_fn> catalog = {};
	return catalog;
}

void rv::variable::set_type(std::size_t _type_hash, const type_registry& _registry) {
	const variable_type_desc* new_type = _registry.find(_type_hash);
	if (new_type == nullptr)
		return;

	if (memory) {
		if (type && type->destroy)
			type->destroy(memory);
		mars::aligned_free(memory);
		memory = nullptr;
	}

	type = new_type;
	memory = mars::aligned_malloc(type->alignment, type->size);
	if (memory && type->construct)
		type->construct(memory);
}

namespace rv {
template<> inline const type_registry::type_auto_registrar auto_register_type_v<float>  { &type_registry::register_reflected<float>  };
template<> inline const type_registry::type_auto_registrar auto_register_type_v<int>    { &type_registry::register_reflected<int>    };
template<> inline const type_registry::type_auto_registrar auto_register_type_v<bool>   { &type_registry::register_reflected<bool>   };
} // namespace rv
