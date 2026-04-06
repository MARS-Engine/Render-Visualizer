#pragma once

#include <render_visualizer/node/node_reflection.hpp>

#include <mars/hash/meta.hpp>
#include <mars/meta/type_erased.hpp>

#include <render_visualizer/node/node_metadata.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rv {

using pin_draw_info_fn = void (*)(mars::meta::type_erased_ptr _instance, std::vector<pin_draw_data>& _inputs, std::vector<pin_draw_data>& _outputs);
struct node_instance_storage {
	std::shared_ptr<void> storage = {};
	mars::meta::type_erased_ptr ptr = {};
};

using node_instance_create_fn = node_instance_storage (*)();

namespace detail {

template<typename T>
node_instance_storage create_node_instance() {
	std::shared_ptr<T> instance = std::make_shared<T>();
	return {
		.storage = std::static_pointer_cast<void>(instance),
		.ptr = mars::meta::type_erased_ptr(instance.get())
	};
}

} // namespace detail

struct node_registry_entry {
	std::size_t type_hash = 0;
	std::string type_key = {};
	std::string_view name = {};
	bool hidden = false;
	node_metadata metadata = {};
	pin_draw_info_fn get_pin_draw_info = nullptr;
	node_instance_create_fn create_instance = nullptr;
};

class node_registry {
public:
	using registration_fn = void (*)(node_registry&);

	struct node_auto_registrar {
		explicit node_auto_registrar(registration_fn _register_fn);
	};

	node_registry();

	void add(node_registry_entry _entry);

	template<typename T>
	void register_node() {
		add({
			.type_hash = mars::hash::type_fingerprint_v<T>,
			.type_key = std::string(mars::hash::type_fingerprint_string<T>()),
			.name = node_reflection<T>::name,
			.hidden = node_reflection<T>::hidden,
			.metadata = node_reflection<T>::get_metadata(),
			.get_pin_draw_info = &node_reflection<T>::get_pin_draw_info,
			.create_instance = &detail::create_node_instance<T>
		});
	}

	template<typename T>
	static void register_reflected(node_registry& _registry) {
		_registry.template register_node<T>();
	}

	const std::vector<node_registry_entry>& registered_nodes() const;

	const node_registry_entry* find(std::size_t _type_hash) const;

	static void add_registration(registration_fn _register_fn);

private:
	void import_registration_catalog();

	static std::vector<registration_fn>& global_registration_catalog();

	std::vector<node_registry_entry> m_entries = {};
};

template<typename T>
inline const node_registry::node_auto_registrar auto_register_node_v { &node_registry::register_reflected<T> };

} // namespace rv
