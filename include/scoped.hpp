#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>

template<typename T>
concept Scopeable = std::equality_comparable<T> && std::is_default_constructible_v<T>;

// From https://cpp-gamedev.github.io/learn-vulkan/initialization/scoped_waiter.html
template<Scopeable T, typename Deleter>
class Scoped {

public:
	Scoped(Scoped const&) = delete;
	auto operator=(Scoped const&) = delete;

	Scoped() = default;

	constexpr Scoped(Scoped&& rhs) noexcept
		: t(std::exchange(rhs.t, T{}))
	{}

	constexpr Scoped& operator=(Scoped&& rhs) noexcept {
		if (&rhs != this) std::swap(t, rhs.t);
		return *this;
	}

	explicit(false) constexpr Scoped(T t) : t(std::move(t)) {}

	constexpr ~Scoped() {
		if (t == T{}) return;
		Deleter{}(t);
	}

	[[nodiscard]]
	constexpr T const& get() const {
		return t;
	}

	[[nodiscard]]
	constexpr T& get() {
		return t;
	}

private:
	T t{};

};

struct ScopedWaiterDeleter {
	void operator()(vk::Device dev) const noexcept {
		dev.waitIdle();
	}
};

using ScopedWaiter = Scoped<vk::Device, ScopedWaiterDeleter>;
