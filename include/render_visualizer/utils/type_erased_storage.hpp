#pragma once

#include <utility>

namespace rv::detail {
	struct type_erased_storage {
		void* storage = nullptr;
		void (*destroy)(void*) = nullptr;

		type_erased_storage() = default;
		type_erased_storage(const type_erased_storage&) = delete;
		type_erased_storage& operator=(const type_erased_storage&) = delete;

		type_erased_storage(type_erased_storage&& other) noexcept
			: storage(other.storage),
			  destroy(other.destroy) {
			other.storage = nullptr;
			other.destroy = nullptr;
		}

		type_erased_storage& operator=(type_erased_storage&& other) noexcept {
			if (this == &other)
				return *this;
			reset();
			storage = other.storage;
			destroy = other.destroy;
			other.storage = nullptr;
			other.destroy = nullptr;
			return *this;
		}

		~type_erased_storage() {
			reset();
		}

		template <typename T, typename... Args>
		void emplace(Args&&... args) {
			reset();
			storage = new T(std::forward<Args>(args)...);
			destroy = [](void* ptr) { delete static_cast<T*>(ptr); };
		}

		template <typename T>
		T& as() {
			return *static_cast<T*>(storage);
		}

		template <typename T>
		const T& as() const {
			return *static_cast<const T*>(storage);
		}

		void* data() { return storage; }
		const void* data() const { return storage; }

		void reset() {
			if (destroy != nullptr && storage != nullptr)
				destroy(storage);
			storage = nullptr;
			destroy = nullptr;
		}
	};
} // namespace rv::detail
