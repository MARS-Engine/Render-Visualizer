#pragma once

#include <render_visualizer/node/node_reflection.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace rv {

struct frame_type_info {
	std::size_t size = 0;
	std::size_t alignment = alignof(std::max_align_t);
	std::string_view name = {};
	std::uint16_t node_id = 0;
	std::size_t type_hash = 0;
	mars::meta::type_erased_ptr source_instance = {};
	node_instance_copy_construct_fn copy_construct = nullptr;
	node_instance_destroy_fn destroy = nullptr;
};

struct stack_entry {
	std::size_t offset = 0;
	std::size_t size = 0;
	std::size_t alignment = alignof(std::max_align_t);
	std::uint16_t node_id = 0;
	std::size_t type_hash = 0;
	std::string_view name = {};
	node_instance_destroy_fn destroy = nullptr;
	bool constructed = false;
};

struct frame_stack {
	frame_stack() = default;
	frame_stack(frame_stack&& _other) noexcept;
	frame_stack& operator=(frame_stack&& _other) noexcept;
	frame_stack(const frame_stack&) = delete;
	frame_stack& operator=(const frame_stack&) = delete;
	~frame_stack();

	void clear();
	void initialize(const std::vector<frame_type_info>& _types);

	void* entry_ptr(std::size_t _index);
	const void* entry_ptr(std::size_t _index) const;

	template<typename T>
	T* entry_ptr(std::size_t _index) {
		return static_cast<T*>(entry_ptr(_index));
	}

	template<typename T>
	const T* entry_ptr(std::size_t _index) const {
		return static_cast<const T*>(entry_ptr(_index));
	}

	std::byte* data = nullptr;
	std::size_t data_size = 0;
	std::size_t data_alignment = alignof(std::max_align_t);
	std::vector<stack_entry> entries = {};
};

struct frame_stack_builder {
	void clear();
	void add(frame_type_info _type);

	template<typename T>
	void add() {
		add({
			.size = sizeof(T),
			.alignment = alignof(T),
			.name = mars::meta::display_name<T>()
		});
	}

	frame_stack build() const;

	std::vector<frame_type_info> types = {};
};

} // namespace rv
