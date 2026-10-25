#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>

namespace toca::simulation {
// Diagnostic snapshot at415090 entry, after422f60 has mapped controls.
// Raw reference records stay outside the repository; pointer words are ignored.
struct SourceSnapshot {
    std::array<std::uint8_t, 0x53c> body{};
    std::array<std::uint8_t, 0x338> dynamics{};
    std::array<std::uint8_t, 0x124> gameplay{};
    // Optional actor-local4bdbd8 recovery record. Diagnostic writers relocate
    // pointer words+0c/+28 to BarrierData source indices before import.
    std::array<std::uint8_t, 0x34> barrier_recovery{};
    bool has_barrier_recovery = false;
    //Actor column from4c25a8 (17*16), plus actor row from4db8c8 (4*u32).
    std::array<std::uint8_t, 17> damage_levels{};
    std::array<std::uint8_t, 16> broken_features{};
    bool has_damage = false;
    bool limiter_active = false;
    std::int32_t limiter_load_q16 = 0;
    //Initial consumed4c5d80 player slot only; later launch state stays native.
    std::int32_t launch_tire_adjustment_q16 = 0;
    std::uint32_t roughness_cursor = 0, steering_profile = 2;
};

// Additional first-read actor context owned by fresh Motion/Controls, rather
// than an observed physical snapshot. Pose/support, calibration and controller
// handoff have their own authored grid writers. Block0 is BODY, block1 dynamics.
// Before-first-write storage (previous position/RPM, wheel axes, patch offsets,
// pending gear) and inactive support tails deliberately have no cold claim.
struct SourceColdActorField {
    const char* name;
    std::uint8_t block;
    std::size_t offset, size;
};

inline constexpr std::array<SourceColdActorField, 18> source_cold_actor_fields{{{"velocity", 0, 0xc8, 12}, {"speed", 0, 0xe0, 4}, {"angular_velocity", 0, 0xe4, 12},
    {"pending_force", 0, 0x1f0, 12}, {"pending_moment", 0, 0x1fc, 12}, {"wheel_speed", 1, 0xfc, 16}, {"longitudinal_slip", 1, 0x1e4, 16}, {"lateral_slip", 1, 0x1f4, 16},
    {"visual_angles", 1, 0x278, 4}, {"rumble", 1, 0x280, 16}, {"previous_rumble", 1, 0x290, 16}, {"rollover", 1, 0x2a0, 12}, {"patch_latch", 1, 0x2bc, 4},
    {"tire_effect_gate", 1, 0x2c0, 4}, {"player_righting", 1, 0x31c, 4}, {"rollover_ticks", 1, 0x331, 3}, {"throttle", 1, 0x11c, 4}, {"raw_axes", 1, 0x138, 16}}};

// Merge actual production publications. This is neither an import guard nor a
// raw-memory reset; observed-context validation belongs to the caller boundary.
inline void copy_source_cold_actor_context(SourceSnapshot& target, const SourceSnapshot& generated) {
    for (const auto& field : source_cold_actor_fields) {
        const auto source = field.block == 0 ? std::span<const std::uint8_t>(generated.body) : std::span<const std::uint8_t>(generated.dynamics);
        const auto destination = field.block == 0 ? std::span<std::uint8_t>(target.body) : std::span<std::uint8_t>(target.dynamics);
        std::copy_n(source.begin() + field.offset, field.size, destination.begin() + field.offset);
    }
    target.limiter_active = generated.limiter_active;
    target.limiter_load_q16 = generated.limiter_load_q16;
}

//Mapped drivetrain inputs after sequential initialization. Observed local
//prephase events are separate below; numeric filters/contacts stay native-owned.
struct SourceInput {
    std::int32_t throttle_q16 = 0, brake_q16 = 0, left_q16 = 0, right_q16 = 0;
    std::int8_t gear_edge = 0;
    std::int8_t forced_gear = -1; //403dd0 byte330 store; preserves pending334/30c.
    //402b10/402ac0 AI contact-grip output, validated at the diagnostic boundary.
    std::optional<std::int32_t> ai_tire_offset_q16 = std::nullopt;
    bool reset_reverse_pedal = false; //403dd0 exit writes304=0; entry retains304.
};

//Observed source pre-physics events, without later numeric launch/filter state.
struct SourceLocalInputBoundary {
    bool start_held = false;
    bool begin_launch = false, advance_launch = false, reset_player_tires = false;
};

inline std::uint32_t source_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("Captured original record is truncated");
    }
    return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset + 1]) << 8 | std::uint32_t(bytes[offset + 2]) << 16 | std::uint32_t(bytes[offset + 3]) << 24;
}

inline std::int32_t source_i32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<std::int32_t>(source_u32(bytes, offset));
}

//403dd0 branches on the byte at game+81. The int at+4 only marks
//an active route record; it does not select the primary/alternate line.
inline std::uint8_t source_ai_route_mode(std::span<const std::uint8_t> gameplay) {
    const auto mode = std::uint8_t(source_u32(gameplay, 0x80) >> 8);
    if (mode < 1 || mode > 5) {
        throw std::runtime_error("Unsupported source AI route mode at gameplay+129");
    }
    return mode;
}

inline std::uint16_t source_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw std::runtime_error("Captured original record is truncated");
    }
    return std::uint16_t(bytes[offset]) | std::uint16_t(bytes[offset + 1]) << 8;
}
} // namespace toca::simulation
