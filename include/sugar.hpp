#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usz = size_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using isz = ptrdiff_t;

using flt = float;
using dbl = double;

template <typename T, typename U>
[[nodiscard]] inline T cast(U value) {
	if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
		assert(value >= 0 && "Attempting to cast negative value to unsigned type");
	}
	if constexpr (std::numeric_limits<U>::max() > std::numeric_limits<T>::max()) {
		assert(value <= std::numeric_limits<T>::max() && "Value exceeds target type max");
	}
	if constexpr (std::is_signed_v<T> && std::numeric_limits<U>::min() < std::numeric_limits<T>::min()) {
		assert(value >= std::numeric_limits<T>::min() && "Value below target type min");
	}
	return static_cast<T>(value);
}
