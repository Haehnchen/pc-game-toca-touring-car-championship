#include "audio/spatial.hpp"

#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>

namespace toca::audio {
namespace {
using namespace simulation::fixed;

std::int32_t asin_angle(std::int32_t value) {
    const auto index = std::uint32_t(std::min(abs(value), 0xffff)) >> 4;
    // 478b20: truncate asin(index/4095)*65536 before multiplication by651.
    const auto radians = std::int32_t(std::asin(double(index) / 4095.0) * one);
    const auto angle = mul(radians, 0x28b);
    return value < 0 ? neg(angle) : angle;
}

std::int32_t horizontal_distance(const SpatialVector& delta) {
    // 423fb0's missing x87 expression was recovered at423fc5..423fe8:
    // arithmetic >>16 before squaring; truncate fsqrt before the final <<16.
    const auto x = delta[0] >> 16, z = delta[2] >> 16;
    const auto square = bits(std::uint32_t(x * x) + std::uint32_t(z * z));
    // Only the wrapped(-32768,-32768) square is negative. The original x87
    // indefinite conversion leaves zero after its final sixteen-bit shift.
    const auto distance = square < 0 ? 0 : std::int32_t(std::sqrt(double(square)));
    return bits(std::uint32_t(distance) << 16);
}
} // namespace

std::uint32_t source_pan(std::uint16_t angle) {
    // 424ac0, including its different centres68/59 and signed truncations.
    auto folded = std::int32_t(angle);
    if (angle > 0x800) {
        folded -= 0x800;
    }
    if (folded > 0x400) {
        folded = 0x800 - folded;
    }
    const auto amount = std::int16_t(mul(folded, 0xd999));
    const auto offset = std::int32_t(amount) * 0x40 / 1024;
    if (angle < 0x801) {
        return std::uint32_t(std::min(offset + 0x44, 0x7f));
    }
    return std::uint32_t(std::max(0x3b - offset, 0));
}

std::int16_t source_listener_heading(const SpatialVector& camera_axis_q16) {
    const auto direction = normalized(Vector{camera_axis_q16[0], 0, camera_axis_q16[2]});
    auto heading = 0x400 - asin_angle(direction[0]);
    if (heading < 0) {
        heading += 0x1000;
    }
    if (direction[2] < 0) {
        heading = 0x1000 - heading;
    }
    return std::int16_t(std::uint32_t(neg(heading)) & 0xfff);
}

SourceSpatial source_spatial(const SpatialVector& source_q16, const SpatialListener& listener, bool half_distance) {
    SourceSpatial result;
    const auto delta = sub(source_q16, listener.position_q16);
    result.distance_q16 = horizontal_distance(delta);
    if (half_distance) {
        result.distance_q16 = mul(result.distance_q16, 0x8000); //424070's event27 override precedes the cutoff and fade.
    }
    if (result.distance_q16 > 40 * one) {
        return result;
    }
    result.in_range = true;
    const auto fade = abs(mul(mul(abs(result.distance_q16), 0x666), 0x640000));
    result.fade_q16 = std::max(sub(100 * one, fade), 0);
    if (!listener.stereo) {
        result.angle = 0x40;
        return result;
    }
    if (delta[0] == 0 && delta[2] == 0) {
        result.pan = source_pan(0);
        return result;
    }
    const auto direction = normalized(Vector{delta[0], 0, delta[2]});
    auto angle = 0x400 - asin_angle(direction[0]);
    if (direction[2] > 0) {
        angle = 0xfff - angle;
    }
    angle = (angle & 0xfff) - std::clamp(std::int32_t(listener.inverse_heading), 0, 0xfff);
    if (angle < 0) {
        angle += 0x1000;
    }
    result.angle = std::uint16_t(angle);
    result.pan = source_pan(result.angle);
    return result;
}
} // namespace toca::audio
