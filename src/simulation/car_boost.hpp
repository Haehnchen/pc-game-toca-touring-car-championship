#pragma once

#include <array>
#include <cstdint>

namespace toca::simulation {
struct CarBoostState {
    std::int32_t maximum_throttle_q16 = 0;
    std::int32_t tire_offset_q16 = 0;
};

struct CarBoostResult {
    CarBoostState state;
    std::array<std::int32_t, 8> tire_offsets_q16{};
    std::int32_t distance_factor_q16 = 0;
    std::int32_t target_throttle_q16 = 0, target_tire_q16 = 0;
};

//428410 resets tire_offset at session entry/restart; maximum_throttle follows
//the current Controls value.428440 runs once per physical tick after the start
//counter clears, for every original local/Linkup human, including AI handoffs.
//Positions and parameters use signed Q16. The caller supplies race rank1,
//independently of spatial order, using positions before the body update batch.
CarBoostResult car_boost_step(CarBoostState previous, std::int32_t base_throttle_q16, const std::array<std::int32_t, 3>& position_q16,
    const std::array<std::int32_t, 3>& leader_position_q16, bool is_leader);
} // namespace toca::simulation
