#pragma once

#include "core/source_fixed.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace toca::simulation::fixed {
using Vector = std::array<std::int32_t, 3>;
using Basis = std::array<Vector, 3>;
inline constexpr std::int32_t one = 65536;

using source_fixed::add;
using source_fixed::bits;
using source_fixed::integer_sqrt;
using source_fixed::mul;
using source_fixed::sqrt;
using source_fixed::sub;

inline std::int32_t neg(std::int32_t value) {
    return sub(0, value);
}

inline std::int32_t abs(std::int32_t value) {
    return value < 0 ? neg(value) : value;
}

inline std::int32_t reciprocal(std::int32_t value) {
    if (value == 0) {
        throw std::runtime_error("Original fixed reciprocal has a zero divisor");
    }
    return bits(std::uint32_t(INT64_C(0x100000000) / value));
}

inline Vector add(const Vector& a, const Vector& b) {
    return {add(a[0], b[0]), add(a[1], b[1]), add(a[2], b[2])};
}

inline Vector sub(const Vector& a, const Vector& b) {
    return {sub(a[0], b[0]), sub(a[1], b[1]), sub(a[2], b[2])};
}

inline Vector scale(const Vector& v, std::int32_t factor) {
    return {mul(v[0], factor), mul(v[1], factor), mul(v[2], factor)};
}

inline std::int32_t dot(const Vector& a, const Vector& b) {
    return add(add(mul(a[2], b[2]), mul(a[0], b[0])), mul(a[1], b[1]));
}

inline Vector cross(const Vector& a, const Vector& b) {
    return {sub(mul(a[1], b[2]), mul(a[2], b[1])), sub(mul(a[2], b[0]), mul(a[0], b[2])), sub(mul(a[0], b[1]), mul(a[1], b[0]))};
}

inline std::int32_t length(const Vector& v) {
    return sqrt(std::uint32_t(dot(v, v)));
}

inline Vector normalized(const Vector& v) {
    const auto magnitude = length(v);
    return magnitude ? scale(v, reciprocal(magnitude)) : Vector{};
}

inline Vector project(const Vector& v, const Vector& normal) {
    return sub(v, scale(normal, dot(v, normal)));
}

inline Vector world(const Vector& local, const Basis& basis) {
    Vector result{};
    for (std::size_t i = 0; i < 3; ++i) {
        result[i] = add(add(mul(local[2], basis[2][i]), mul(local[0], basis[0][i])), mul(local[1], basis[1][i]));
    }
    return result;
}

inline Vector local(const Vector& value, const Basis& basis) {
    return {dot(value, basis[0]), dot(value, basis[1]), dot(value, basis[2])};
}

inline std::int32_t sine(std::int32_t angle) {
    // Preserve the scalar formula and truncation for every wrapped 12-bit angle.
    static const auto values = [] {
        std::array<std::int32_t, 4096> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            result[i] = std::int32_t(std::sin(double(i) / 4096.0 * 0x1.921fb54442d18p+2) * one);
        }
        return result;
    }();
    return values[std::uint32_t(angle) & 4095];
}

// 478b20's 512-entry atan table, plus its adjacent cos[0] low word at 512.
inline std::int32_t atan_ratio(std::int32_t numerator, std::int32_t denominator) {
    if (numerator == 0) {
        return 0;
    }
    if (denominator == 0) {
        return 1024;
    }
    const bool swapped = denominator < numerator;
    const auto small = swapped ? denominator : numerator, large = swapped ? numerator : denominator;
    const auto index = std::uint32_t(std::int64_t(small) * one / large) >> 7;
    const auto value = index == 512 ? 0 : std::int32_t(std::atan(double(index) / 511.0) * one) * 651 >> 16;
    return swapped ? 1024 - value : value;
}

inline void rotate(Basis& basis, const std::array<std::int16_t, 3>& angles) {
    for (const auto axis : {1u, 2u, 0u}) {
        const auto angle = angles[axis];
        if (angle == 0) {
            continue;
        }
        const auto u = basis[axis];
        const auto sine_value = sine(-std::int32_t(angle)), cosine = sine(1024 - angle);
        Basis rotation{};
        for (std::size_t i = 0; i < 3; ++i) {
            rotation[i][i] = add(mul(sub(one, mul(u[i], u[i])), cosine), mul(u[i], u[i]));
        }
        const auto xy = mul(mul(u[0], u[1]), sub(one, cosine)), xz = mul(mul(u[0], u[2]), sub(one, cosine)), yz = mul(mul(u[1], u[2]), sub(one, cosine));
        rotation[0][1] = sub(xy, mul(sine_value, u[2]));
        rotation[1][0] = add(xy, mul(sine_value, u[2]));
        rotation[0][2] = add(xz, mul(sine_value, u[1]));
        rotation[2][0] = sub(xz, mul(sine_value, u[1]));
        rotation[1][2] = sub(yz, mul(sine_value, u[0]));
        rotation[2][1] = add(yz, mul(sine_value, u[0]));
        for (std::size_t i = 0; i < 3; ++i) {
            if (i != axis) {
                basis[i] = normalized(world(basis[i], rotation));
            }
        }
    }
}
} // namespace toca::simulation::fixed
