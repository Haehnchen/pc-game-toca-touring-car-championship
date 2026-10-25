#include "app/gamepad_feedback.hpp"

#include "simulation/motion.hpp"

#include <algorithm>

namespace toca {
namespace {
std::int32_t strength(std::int32_t value) {
    return std::clamp(value, 0, 65536);
}

std::uint16_t motor(std::int32_t value) {
    const auto normalized = std::int64_t(strength(value));
    // Native presentation gain x*(2-x): lift weak contacts without clipping
    // strong impacts or flattening the existing four-step decay envelope.
    auto boosted = normalized * (131072 - normalized) / 65536;
    // A modest additional lift retains zero, full scale and monotonic decay.
    boosted += boosted * (65536 - boosted) / 262144;
    return std::uint16_t((boosted * 65535) / 65536);
}
} // namespace

GamepadRumble GamepadFeedback::update(const simulation::MotionState& motion, std::int32_t local_car_impact_q16) {
    if (motion.boundary != simulation::MotionBoundary::none) {
        reset();
        return {};
    }
    // An isolated impact decays to zero within160ms of active simulation time.
    impact_q16_ = std::max({std::max(0, impact_q16_ - 16384), strength(local_car_impact_q16), strength(motion.prop_impact_q16)});
    for (const auto& impact : motion.impacts) {
        if (impact.local_player) {
            impact_q16_ = std::max(impact_q16_, strength(impact.strength_q16));
        }
    }
    std::int32_t road = 0, curb_peak = 0, curb_sum = 0;
    if (motion.speed_integer > 0) {
        for (std::size_t wheel = 0; wheel < motion.rumble.size(); ++wheel) {
            if (motion.wheel_contact[wheel]) {
                const auto roughness = strength(motion.rumble_strength_q16[wheel]);
                if (motion.rumble[wheel]) {
                    curb_peak = std::max(curb_peak, roughness);
                    curb_sum += roughness;
                } else if (motion.surface_ids[wheel] == 2 || motion.surface_ids[wheel] == 3) {
                    road += roughness / 8;
                }
            }
        }
    }
    // Preserve a strong one-wheel curb contact instead of averaging it across
    // four wheels and then attenuating it again for each motor.
    const auto curb = strength(curb_peak / 2 + curb_sum / 8);
    const auto scrape = strength(motion.barrier_effects.scrape_strength_q16);
    return {motor(std::max({impact_q16_, road / 4, scrape / 4, curb / 2})), motor(std::max({impact_q16_ / 2, road * 3 / 4, scrape / 2, curb}))};
}

void GamepadFeedback::reset() {
    impact_q16_ = 0;
}
} // namespace toca
