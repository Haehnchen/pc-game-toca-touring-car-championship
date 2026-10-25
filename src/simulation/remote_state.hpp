#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>

namespace toca::simulation {
// Original43b2d0/43b1e0 mapped axes. Signed steering distinguishes the human
// direct-axis convention from keyboard controls. flags retains source word
// bits20 and22..28; bits24/25/26 select throttle/brake/steering direct filters.
struct RemoteControls {
    std::int32_t left_q16 = 0, right_q16 = 0;
    std::uint32_t throttle_q16 = 0, brake_q16 = 0, source_flags = 0;
    bool operator==(const RemoteControls&) const = default;
};

// Typed original76-byte car body (43c5a0/43b7d0), independent of transport.
// No ownership, clock or SDL state belongs here. Session/race routing validates
// generation/sender/actor before Motion applies a correction at a tick boundary.
struct RemoteMotionState {
    std::array<std::int32_t, 3> position{}, velocity{}, angular_velocity{};
    std::array<std::array<std::int32_t, 3>, 3> basis{};
    std::array<std::int16_t, 2> visual_angles{};
    std::uint16_t steering_angle = 0;
    std::uint32_t engine_speed_q16 = 0;
    std::optional<std::uint16_t> throttle_q16;
    RemoteControls controls;
    bool operator==(const RemoteMotionState&) const = default;
};

inline void validate_remote_controls(const RemoteControls& input) {
    if (input.left_q16 < -65536 || input.left_q16 > 65536 || input.right_q16 < -65536 || input.right_q16 > 65536 || input.throttle_q16 > 65536 || input.brake_q16 > 65536
        || (input.source_flags & ~0x1fd00000U) != 0) {
        throw std::runtime_error("Network controls exceed original packed field ranges");
    }
}

inline void validate_remote_motion(const RemoteMotionState& input) {
    validate_remote_controls(input.controls);
    if (input.steering_angle > 0xfff || input.engine_speed_q16 > 0xfffff) {
        throw std::runtime_error("Network car controls exceed original packed field ranges");
    }
    for (const auto angle : input.visual_angles) {
        if (angle < -32 || angle > 31) {
            throw std::runtime_error("Network visual angle exceeds signed six-bit range");
        }
    }
    for (const auto& row : input.basis) {
        for (const auto value : row) {
            if (value < -65536 || value > 65536) {
                throw std::runtime_error("Network orientation exceeds clamped signed Q16 range");
            }
        }
    }
}

inline std::array<std::int32_t, 3> correct_remote_position(std::array<std::int32_t, 3> old, std::array<std::int32_t, 3> received) {
    const auto distance_x = std::int64_t(received[0]) - old[0], distance_z = std::int64_t(received[2]) - old[2];
    if (distance_x < -0x40000 || distance_x > 0x40000 || distance_z < -0x40000 || distance_z > 0x40000) {
        return received;
    }
    for (std::size_t i = 0; i < old.size(); ++i) {
        old[i] = std::int32_t((std::int64_t(received[i]) + 2 * std::int64_t(old[i])) / 3);
    }
    return old;
}
} // namespace toca::simulation
