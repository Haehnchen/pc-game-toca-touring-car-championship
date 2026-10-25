#pragma once

#include "scene/body.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace toca::simulation {
enum class BodyMode : std::uint8_t {
    full = 0,
    distant = 1,
    crashed = 2,
    crashing = 3,
    removed = 4
};

// 4102e0 dynamics+2a0/+2a4/+2a8, counters+331/+332/+333,
// and40caf0's player-righting latch at+31c. These are not barrier flags.
struct RolloverState {
    bool upside_down = false, sideways = false, pitched = false, player_righting = false;
    std::array<std::uint8_t, 3> ticks{};
    void observe(const std::array<std::int32_t, 3>& ground_dot_q16);
};

// 414e20's first pass; caller computes force_full from human ownership,
// player identity and the source first-tyre threshold. Neighbor promotions
// happen afterward and promote only distant actors.
BodyMode select_body_mode(BodyMode previous, const RolloverState&, bool force_full, std::uint32_t contacts, std::int32_t normal_up_q16);

struct BodySelectionActor {
    BodyMode previous = BodyMode::distant;
    RolloverState rollover;
    bool human_control = false;
    std::int32_t player_identity = -1;
    std::uint32_t support_count = 0;
    std::int32_t first_tire_q16 = 0, normal_up_q16 = 0;
    std::uint32_t ahead = 0, behind = 0;
    std::int32_t forward_gap_q16 = 0;
};

struct BodySelectionContext {
    std::uint32_t local_actor = 0, local_players = 1, full_budget = 4;
    std::int32_t tire_base_q16 = 0;
    bool alternate_direction = false;
};

struct BodySelectionResult {
    std::array<BodyMode, 16> modes;
    std::uint32_t full_count = 0;
};

//414e20 ascending retained-body pass, then414ef0's two exact ring walks.
//Uses retained body+b4 rather than recomputing after ground stabilization.
BodySelectionResult select_fleet_body_modes(std::span<const BodySelectionActor, 16>, const BodySelectionContext&);
// 415090 returns true for the player upside-down dispatcher early return.
bool prepare_rollover(RolloverState&, bool player_identity, const scene::BodyPose&, std::array<std::int32_t, 3>& angular_velocity_q16);
// 40caf0's gradual physical player recovery; never changes pose directly.
void advance_player_righting(RolloverState&, std::int32_t normal_up_q16, std::int32_t side_y_q16, std::array<std::int32_t, 3>& angular_velocity_q16);
// 445360: retain inverted/side orientation and horizontal position; settle on
// the support plane and lower the original side contact set by0.8 units.
void settle_rollover_pose(scene::BodyPose&, std::uint32_t car_id, std::int32_t surface_height_q16, const RolloverState&);
} // namespace toca::simulation
