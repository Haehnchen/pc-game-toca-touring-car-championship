#include "scene/ground_scrape.hpp"

#include "simulation/fixed.hpp"
#include "simulation/source_transform.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
namespace {
using namespace simulation::fixed;

Vector corner(const Vector& half, std::size_t index) {
    return {index % 4 < 2 ? half[0] : neg(half[0]), index < 4 ? neg(half[1]) : half[1], index % 2 ? neg(half[2]) : half[2]};
}

Vector upper_offset(std::size_t index, bool roof) {
    // 4153c0's4bea38..64, selected by432210's quantized body-up angle.
    if (roof) {
        return {index < 6 ? 0x1999 : -0x9999, 0xa666, index % 2 ? -0x9999 : 0x9999};
    }
    return {index < 6 ? 0x1e666 : -0x21999, index < 6 ? 0 : 0x8ccc, index % 2 ? -0xcccc : 0xcccc};
}

bool roof_offset(const Vector& normal, const Vector& body_up) {
    const auto value = dot(normal, body_up);
    const auto index = std::uint32_t(std::min(abs(value), 0xffff)) >> 4;
    // 478b20's actual asin table: truncate radians*65536, then *651>>16.
    const auto radians = std::int32_t(std::asin(double(index) / 4095.0) * one);
    auto angle = mul(radians, 0x28b);
    if (value < 0) {
        angle = neg(angle);
    }
    return std::int16_t(1024 - angle) >= 0x601;
}
} // namespace

GroundScrapeRequests ground_scrape(const GroundScrapeInput& input) {
    if (input.queued_sparks > 10) {
        throw std::runtime_error("Ground scrape requires the original0..10 request queue count");
    }
    GroundScrapeRequests result;
    const auto& body = input.body;
    const bool all_airborne = std::none_of(input.active.begin(), input.active.end(), [](bool active) {
        return active;
    });
    if (input.actor_state != 0 || input.tyre_contact || input.rain || input.snow || !input.visible || input.queued_sparks == 10
        || (all_airborne && sub(body.contacts[0][1], body.surfaces[0].height) >= 0x8000)) {
        return result;
    }
    auto velocity = input.velocity;
    const auto normal_speed = dot(body.ground_normal, velocity);
    if (normal_speed > 0) {
        velocity = sub(velocity, scale(body.ground_normal, normal_speed));
    }
    const auto speed = length(velocity);
    if (speed <= 0x3333) {
        return result;
    }
    const auto intensity = std::min(mul(sub(speed, 0x3333), 0x8000), one);
    if (intensity <= 0x1999) {
        return result;
    }
    const auto direction = scale(normalized(velocity), -one);
    const auto half = car_half_dimensions(input.car_id);
    const auto roof = roof_offset(body.ground_normal, body.basis[1]);
    for (std::size_t index = 0; index < input.active.size() && input.queued_sparks + result.count < 10; ++index) {
        if (!input.active[index] || input.surface_ids[index] != 0) {
            continue;
        }
        auto position = body.contacts[index];
        position[1] = body.surfaces[index].height;
        if (index >= 4) {
            auto offset = simulation::source_transform::world(sub(upper_offset(index, roof), corner(half, index)), body.basis);
            offset = sub(offset, scale(body.ground_normal, dot(body.ground_normal, offset)));
            position = add(position, offset);
        }
        result.requests[result.count++] = {position, direction, velocity, intensity, true};
    }
    return result;
}
} // namespace toca::scene
