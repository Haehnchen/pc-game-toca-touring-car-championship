#pragma once

#include "simulation/fixed.hpp"

namespace toca::simulation::source_transform {
// 47b150/47b250 use x87 products and one nearest-even fistp conversion.
// Q16 body axes keep the exact product sum within64 bits; unlike fixed::dot,
// individual products are not shifted or rounded before addition.
inline std::int32_t dot(const fixed::Vector& a, const fixed::Vector& axis) {
    const auto sum = std::int64_t(a[2]) * axis[2] + std::int64_t(a[0]) * axis[0] + std::int64_t(a[1]) * axis[1];
    auto quotient = sum / fixed::one;
    const auto remainder = sum % fixed::one;
    const auto magnitude = remainder < 0 ? -remainder : remainder;
    if (magnitude > fixed::one / 2 || (magnitude == fixed::one / 2 && (std::uint64_t(quotient) & 1u))) {
        quotient += sum < 0 ? -1 : 1;
    }
    if (quotient < INT32_MIN || quotient > INT32_MAX) {
        return INT32_MIN; // x87 integer-indefinite for an overflowing fistp.
    }
    return std::int32_t(quotient);
}

inline fixed::Vector local(const fixed::Vector& value, const fixed::Basis& basis) {
    return {dot(value, basis[0]), dot(value, basis[1]), dot(value, basis[2])};
}

inline fixed::Vector world(const fixed::Vector& value, const fixed::Basis& basis) {
    fixed::Vector result;
    for (std::size_t i = 0; i < 3; ++i) {
        result[i] = dot(value, {basis[0][i], basis[1][i], basis[2][i]});
    }
    return result;
}
} // namespace toca::simulation::source_transform
