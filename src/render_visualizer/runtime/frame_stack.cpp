#include <render_visualizer/runtime/frame_stack.hpp>

#include <algorithm>
#include <new>
#include <utility>

namespace {

std::size_t align_up(std::size_t _value, std::size_t _alignment) {
	if (_alignment <= 1)
		return _value;
	const std::size_t remainder = _value % _alignment;
	if (remainder == 0)
		return _value;
	return _value + (_alignment - remainder);
}

void move_from(rv::frame_stack& _destination, rv::frame_stack&& _source) {
	_destination.data = _source.data;
	_destination.data_size = _source.data_size;
	_destination.data_alignment = _source.data_alignment;
	_destination.entries = std::move(_source.entries);

	_source.data = nullptr;
	_source.data_size = 0;
	_source.data_alignment = alignof(std::max_align_t);
	_source.entries.clear();
}

} // namespace

rv::frame_stack::frame_stack(frame_stack&& _other) noexcept {
	move_from(*this, std::move(_other));
}

rv::frame_stack& rv::frame_stack::operator=(frame_stack&& _other) noexcept {
	if (this == &_other)
		return *this;
	clear();
	move_from(*this, std::move(_other));
	return *this;
}

rv::frame_stack::~frame_stack() {
	clear();
}

void rv::frame_stack::clear() {
	for (std::size_t entry_index = entries.size(); entry_index > 0; --entry_index) {
		stack_entry& entry = entries[entry_index - 1];
		if (!entry.constructed || entry.destroy == nullptr)
			continue;
		entry.destroy(entry_ptr(entry_index - 1));
		entry.constructed = false;
	}

	if (data != nullptr)
		::operator delete(data, std::align_val_t(data_alignment));

	data = nullptr;
	data_size = 0;
	data_alignment = alignof(std::max_align_t);
	entries.clear();
}

void rv::frame_stack::initialize(const std::vector<frame_type_info>& _types) {
	clear();

	entries.reserve(_types.size());

	std::size_t current_offset = 0;
	for (const frame_type_info& type : _types) {
		const std::size_t type_alignment = std::max<std::size_t>(1, type.alignment);
		current_offset = align_up(current_offset, type_alignment);
		entries.push_back({
			.offset = current_offset,
			.size = type.size,
			.alignment = type_alignment,
			.node_id = type.node_id,
			.type_hash = type.type_hash,
			.name = type.name,
			.destroy = type.destroy
		});
		data_alignment = std::max(data_alignment, type_alignment);
		current_offset += type.size;
	}

	data_size = current_offset;
	if (data_size == 0)
		return;

	data = static_cast<std::byte*>(::operator new(data_size, std::align_val_t(data_alignment)));
	for (std::size_t type_index = 0; type_index < _types.size(); ++type_index) {
		const frame_type_info& type = _types[type_index];
		if (type.copy_construct == nullptr || type.source_instance == nullptr)
			continue;
		type.copy_construct(entry_ptr(type_index), type.source_instance);
		entries[type_index].constructed = true;
	}
}

void* rv::frame_stack::entry_ptr(std::size_t _index) {
	if (data == nullptr || _index >= entries.size())
		return nullptr;
	return data + entries[_index].offset;
}

const void* rv::frame_stack::entry_ptr(std::size_t _index) const {
	if (data == nullptr || _index >= entries.size())
		return nullptr;
	return data + entries[_index].offset;
}

void rv::frame_stack_builder::clear() {
	types.clear();
}

void rv::frame_stack_builder::add(frame_type_info _type) {
	types.push_back(_type);
}

rv::frame_stack rv::frame_stack_builder::build() const {
	frame_stack stack = {};
	stack.initialize(types);
	return stack;
}
