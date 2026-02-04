#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cassert>
#include <span>

#include "sugar.hpp"

struct Arena {
	explicit Arena(usz size) {
		// TODO: use a std::make_unique<std::byte[]>(size);
		this->buf.resize(size);
		this->start_ptr = this->buf.data();
	}

	Arena(const Arena&) = delete;
	Arena& operator=(const Arena&) = delete;

	Arena(Arena&&) = default;
	Arena& operator=(Arena&&) = default;

	// This does not call destructors!
	void reset() {
		this->ofs = 0;
	}

	template<typename T, typename... Args>
	[[nodiscard]]
	T* alloc(Args&&... args) {
		return new (this->alloc_raw(sizeof(T), alignof(T))) T(std::forward<Args>(args)...);
	}

	template<typename T>
	[[nodiscard]]
	std::span<T> alloc_array(usz count) {
		if (count == 0) return {};
		void* ptr = this->alloc_raw(sizeof(T) * count, alignof(T));
		// default construct array elements
		T* t_ptr = new (ptr) T[count];
		return std::span<T>(t_ptr, count);
	}

private:
	std::vector<std::byte> buf;
	std::byte* start_ptr = nullptr;
	usz ofs = 0;

	void* alloc_raw(usz size, usz alignment) {
		void* ptr = this->start_ptr + this->ofs;
		usz space = this->buf.size() - this->ofs;
		if (std::align(alignment, size, ptr, space)) {
			usz padding = (static_cast<std::byte*>(ptr) - (this->start_ptr + this->ofs));
			this->ofs += padding + size;
			return ptr;
		}
		throw std::runtime_error("OOM in arena");
	}
};
