#include "simulation/car_boost.hpp"

#include "simulation/fixed.hpp"

namespace toca::simulation {
namespace {
std::int32_t approach(std::int32_t current, std::int32_t target) {
    using namespace fixed;
    const auto delta = sub(target, current);
    if (abs(delta) < 0x28f) {
        return target;
    }
    return add(current, delta < 1 ? -0x28f : 0x28f);
}
} // namespace

CarBoostResult car_boost_step(
    CarBoostState previous, std::int32_t base_throttle, const std::array<std::int32_t, 3>& position, const std::array<std::int32_t, 3>& leader_position, bool is_leader) {
    using namespace fixed;
    CarBoostResult result;
    if (!is_leader) {
        const auto delta = sub(leader_position, position);
        std::int32_t distance_squared;
        if (abs(delta[0]) > 0x640000 || abs(delta[1]) > 0x640000 || abs(delta[2]) > 0x640000) {
            // The source far sentinel is2500Q16, not a direct full-factor clamp.
            distance_squared = 0x9c40000;
        } else {
            distance_squared = add(add(mul(delta[2], delta[2]), mul(delta[0], delta[0])), mul(delta[1], delta[1]));
        }
        result.distance_factor_q16 = mul(distance_squared, 0x1a);
        if (result.distance_factor_q16 > one) {
            result.distance_factor_q16 = one;
        }
    }
    result.target_throttle_q16 = add(base_throttle, mul(result.distance_factor_q16, 0x1999));
    //428410 initializes both per-human tire bases/current offsets to zero.
    result.target_tire_q16 = mul(result.distance_factor_q16, 0x1eb8);
    result.state.maximum_throttle_q16 = approach(previous.maximum_throttle_q16, result.target_throttle_q16);
    result.state.tire_offset_q16 = approach(previous.tire_offset_q16, result.target_tire_q16);
    result.tire_offsets_q16.fill(result.state.tire_offset_q16);
    result.tire_offsets_q16[4] = add(result.tire_offsets_q16[4], 0xccc);
    return result;
}
} // namespace toca::simulation
