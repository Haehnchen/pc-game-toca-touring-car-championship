#pragma once

#include "scene/barriers.hpp"
#include "scene/body.hpp"
#include "simulation/damage.hpp"

#include <vector>

namespace toca::simulation {
struct BarrierImpact {
    std::array<std::int32_t, 3> position_q16{};
    std::uint32_t kind = 1;
    std::int32_t strength_q16 = 0;
    bool local_player = true;
    std::uint32_t barrier_index = 0;
};

// Actor-local 4bdbd8+actor*34 state. Indices refer to the borrowing BarrierData.
struct BarrierRecovery {
    bool active = false, has_secondary = false;
    std::uint32_t head = UINT32_MAX, secondary_segment = UINT32_MAX;
    std::array<std::int32_t, 3> primary_push{}, saved_velocity{}, secondary_push{};
};

// 409fa0's one selected static contact. The local-car caller produces at
// most one request of each kind per tick (original queues hold ten).
struct BarrierEffects {
    std::array<std::int32_t, 3> position_q16{}, tangent_velocity_q16{}, spark_direction_q16{};
    std::int32_t fragment_strength_q16 = 0, spark_strength_q16 = 0, scrape_strength_q16 = 0;
};

struct BarrierContactResult {
    BarrierEffects effects;
    std::array<std::int32_t, 3> angular_impulse_q16{};
    std::vector<DamageContact> damage_contacts;
    std::uint32_t contacts = 0;
    bool recovery_entered = false, recovery_exited = false;
};

// 409c30: ordinary static kinds0/1, then linked kind2/3 recovery.
// Position and velocity change immediately; angular impulse enters next step.
BarrierContactResult resolve_barriers(const scene::BarrierData&, scene::BodyPose&, std::array<std::int32_t, 3>& velocity_q16, const std::array<std::int32_t, 3>& previous_position,
    std::uint32_t car_id, BarrierRecovery& recovery, bool reverse_gear);
} // namespace toca::simulation
