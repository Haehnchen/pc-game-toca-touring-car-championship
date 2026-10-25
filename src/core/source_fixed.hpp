#pragma once

#include <bit>
#include <cstdint>

namespace toca::source_fixed {
// Original signed32 arithmetic wraps before the next source operation.
inline std::int32_t bits(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

inline std::int32_t add(std::int32_t a, std::int32_t b) {
    return bits(std::uint32_t(a) + std::uint32_t(b));
}

inline std::int32_t sub(std::int32_t a, std::int32_t b) {
    return bits(std::uint32_t(a) - std::uint32_t(b));
}

inline std::int32_t mul(std::int32_t a, std::int32_t b) {
    return bits(std::uint32_t(std::uint64_t(std::int64_t(a) * b) >> 16));
}

inline std::uint32_t integer_sqrt(std::uint64_t value) {
    std::uint64_t result = 0, bit = UINT64_C(1) << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return std::uint32_t(result);
}

//478b20's midpoint lookup, including its source exponent/shift order.
inline std::int32_t sqrt(std::uint32_t value) {
    if (value == 0) {
        return 0;
    }
    int exponent = int(std::bit_width(value)) - 16;
    if (exponent % 2 != 0) {
        ++exponent;
    }
    const auto shift = exponent + 4;
    const auto index = shift < 0 ? value << unsigned(-shift) : value >> unsigned(shift);
    const auto entry = integer_sqrt((std::uint64_t(index) * 16 + 8) << 16);
    const auto half = exponent / 2;
    return bits(half < 0 ? entry >> unsigned(-half) : entry << unsigned(half));
}
} // namespace toca::source_fixed
