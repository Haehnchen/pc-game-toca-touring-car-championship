#include "scene/car_light_glow.hpp"

#include "simulation/fixed.hpp"
#include "simulation/source_transform.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
namespace {
using namespace simulation::fixed;
// 434f00's emitter centers, including its final longitudinal offsets.
constexpr std::array<Vector, 10> front_centers{{{150798, -4259, 32505}, {148897, -3276, 33619}, {149225, -2359, 35520}, {144703, -1703, 40370}, {149421, 720, 36175},
    {146997, -3932, 32964}, {150929, -1114, 35782}, {148307, -1048, 35848}, {13107, 0, 0}, {200277, 4980, 56623}}};
constexpr std::array<Vector, 10> rear_centers{{{-154795, 6422, 43253}, {-145489, 11927, 44761}, {-146603, 13172, 39387}, {-152174, 15204, 45481}, {-150339, 16252, 42991},
    {-151387, 4128, 43253}, {-151060, 5373, 48234}, {-146865, 6684, 42729}, {-13107, 0, 0}, {-189923, 27721, 64487}}};

std::int32_t q16(float value) {
    const auto scaled = double(value) * one;
    if (!std::isfinite(scaled) || scaled < INT32_MIN || scaled > INT32_MAX) {
        throw std::runtime_error("Car light transform requires finite source coordinates");
    }
    return std::int32_t(std::nearbyint(scaled));
}

Vector translation(const std::array<float, 16>& matrix) {
    return {q16(matrix[12]), q16(matrix[13]), q16(matrix[14])};
}

Basis axes(const std::array<float, 16>& matrix) {
    Basis result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (std::size_t component = 0; component < 3; ++component) {
            result[axis][component] = q16(matrix[axis * 4 + component]);
        }
    }
    return result;
}
} // namespace

CarLightGlowFrame car_light_glow_frame(std::uint32_t car, std::uint32_t weather, const CarLights& lights, const std::array<float, 16>& physical_root,
    const std::array<float, 16>& lamp_transform, const std::array<std::int32_t, 3>& camera_position, const CarLightGroundInput* ground) {
    if (car == 10) {
        car = 2;
    }
    if (car > 9 || weather < 1 || weather > 5) {
        throw std::runtime_error("Car light glow requires an original vehicle and resolved weather");
    }
    CarLightGlowFrame result;
    if (car == 8 || (!lights.headlights && !lights.tail && !lights.brake)) {
        return result;
    }
    const auto separation = sub(translation(physical_root), camera_position);
    const auto distance = length(separation);
    if (distance <= 0) {
        return result;
    }
    const auto direction = scale(separation, reciprocal(neg(distance)));
    const auto cosine = dot(axes(physical_root)[0], direction);
    const auto index = std::uint32_t(std::min(abs(cosine), 0xffff)) >> 4;
    const auto radians = std::int32_t(std::asin(double(float(index)) / 4095.0) * one);
    auto arc = mul(radians, 0x28b);
    if (cosine < 0) {
        arc = neg(arc);
    }
    const auto angle = (1024 - arc) * 0x1680;
    result.front = angle < 0x5a0000;
    result.angle_fade_q16 = mul(result.front ? angle : 0xb40000 - angle, 0x2d0);
    if (result.front ? !lights.headlights : (!lights.tail && !lights.brake)) {
        return result;
    }
    const auto brightness = result.front ? (weather < 3 ? 100 : weather == 5 ? 150 : 200) : lights.brake ? (weather < 3 ? 60 : 100) : (weather < 3 ? 30 : weather == 5 ? 60 : 70);
    const auto shade = std::uint8_t((brightness * one - mul(brightness * one, result.angle_fade_q16)) / one);
    const auto basis = axes(lamp_transform);
    const auto center = result.front ? front_centers[car] : rear_centers[car];
    const auto first = add(simulation::source_transform::world(center, basis), translation(lamp_transform));
    const auto second = add(first, scale(basis[2], mul(center[2], -0x20000)));
    const auto radius = result.front ? 0x8000 : 0x6666;
    // 434f00 keeps the reflected image's weather intensity separate from its
    // halo. 436c80 first truncates the angle-faded value to a descriptor byte.
    const auto ground_brightness = result.front ? (weather == 5 ? 90 : 150) : lights.brake ? (weather == 5 ? 80 : 150) : (weather == 5 ? 60 : 70);
    const auto ground_shade = std::uint8_t(sub(ground_brightness * one, mul(ground_brightness * one, result.angle_fade_q16)) / one);
    for (std::size_t side = 0; side < 2; ++side) {
        if (!lights.broken[(result.front ? 0u : 2u) + side]) {
            const auto emitter = side == 0 ? first : second;
            result.sprites[result.count++] = {emitter, {-radius, -radius, 0}, {radius, radius, 0}, shade, 0};
            const auto wheel = (result.front ? 0u : 2u) + side;
            if (!ground || weather < 3 || ground->surface[wheel] != 0) {
                continue;
            }
            auto reflected = emitter;
            auto reflected_shade = ground_shade;
            if (ground->distant_body) {
                reflected[1] = sub(emitter[1], mul(add(center[1], ground->body_height), 0x20000));
                if (!result.front) {
                    reflected[1] = add(reflected[1], 0x3333);
                }
            } else {
                const auto height = ground->ground_height[wheel];
                auto offset = sub(height, emitter[1]);
                if (!result.front) {
                    offset = std::min(add(offset, 0x3333), -0x4ccc);
                }
                reflected[1] = add(offset, height);
                const auto distance = sub(ground->wheel_position[wheel][1], height);
                if (distance > 0) {
                    // The source caps only the upper end: a wheel below the
                    // local lamp height can brighten the byte. Keep its fixed
                    // arithmetic and truncation, including byte wrapping.
                    const auto fade = std::min(sub(distance, center[1]), one);
                    reflected_shade = std::uint8_t(sub(ground_shade * one, mul(fade, ground_shade * one)) / one);
                }
            }
            const auto width = result.front ? 0xb328 : 0x8f56;
            const auto height = result.front ? 0x3ffc : 0x3331;
            result.ground_sprites[result.ground_count++] = {reflected, {-width, -height, 0}, {width, height, 0}, reflected_shade, 0};
        }
    }
    return result;
}
} // namespace toca::scene
