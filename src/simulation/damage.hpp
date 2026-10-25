#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace toca::simulation {
struct DamageContact {
    std::array<std::int32_t, 3> impulse_q16{}, local_contact_q16{};
    std::array<std::int32_t, 3> position_q16{};
    std::uint32_t barrier_index = 0;
};

struct DamageSelection {
    std::uint8_t node_id = 0, rule = 0, slot = 0;
};

struct DamageState {
    std::array<std::uint8_t, 17> levels{};
    std::array<bool, 4> broken_features{};
    std::array<DamageSelection, 5> selection{};
    std::uint8_t cycle = 0, feedback_side = 0;
    std::uint16_t feedback_strength = 0;
};

struct DamageInput {
    DamageContact contact;
    std::array<std::int32_t, 3> inverse_half_dimensions_q16{};
    std::uint32_t car_id = 0;
    bool feedback_enabled = true;
    std::uint32_t debris_count = 0;
    bool debris_blocked_by_recent_part = false;

    struct DebrisStatus {
        std::uint32_t count = 0;
        bool blocked_by_recent_part = false;
    };

    std::function<DebrisStatus()> debris_status;
    std::function<void(std::uint8_t)> detach;
};

struct DamageResult {
    std::int32_t strength_q16 = 0;
    std::uint32_t event_kind = 0;
    std::uint8_t severity = 0;
};

// 419ad0: source severity, dominant contact side and per-part variant changes.
DamageResult apply_damage(DamageState& state, const DamageInput& input);
void reset_damage(DamageState& state);
//Actor-local visual damage only; shared cycle/selection belongs to DamageContext.
void restore_source_damage(DamageState&, std::span<const std::uint8_t, 17> levels, std::span<const std::uint8_t, 16> broken_features);
} // namespace toca::simulation
