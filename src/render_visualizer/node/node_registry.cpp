#include <render_visualizer/node/node_registry.hpp>

rv::node_registry::node_auto_registrar::node_auto_registrar(registration_fn _register_fn) {
	node_registry::add_registration(_register_fn);
}

rv::node_registry::node_registry() {
	import_registration_catalog();
}

void rv::node_registry::add(node_registry_entry _entry) {
	m_entries.push_back(_entry);
}

const std::vector<rv::node_registry_entry>& rv::node_registry::registered_nodes() const {
	return m_entries;
}

const rv::node_registry_entry* rv::node_registry::find(std::size_t _type_hash) const {
	for (const node_registry_entry& entry : m_entries)
		if (entry.type_hash == _type_hash)
			return &entry;
	return nullptr;
}

void rv::node_registry::add_registration(registration_fn _register_fn) {
	global_registration_catalog().push_back(_register_fn);
}

void rv::node_registry::import_registration_catalog() {
	for (registration_fn register_fn : global_registration_catalog())
		register_fn(*this);
}

std::vector<rv::node_registry::registration_fn>& rv::node_registry::global_registration_catalog() {
	static std::vector<registration_fn> catalog = {};
	return catalog;
}
