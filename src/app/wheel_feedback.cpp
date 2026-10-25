#include "app/wheel_feedback.hpp"

#include "simulation/motion.hpp"

#include <algorithm>
#include <limits>

namespace toca {
std::vector<WheelFeedbackCommand> WheelFeedback::update(const simulation::MotionState& motion) {
    using Kind = WheelFeedbackCommand::Kind;
    if (motion.boundary != simulation::MotionBoundary::none) {
        return {reset()};
    }
    std::vector<WheelFeedbackCommand> commands;
    WheelFeedbackCommand impact{.kind = Kind::constant, .magnitude = motion.wheel_feedback_strength, .duration_us = 50000};
    switch (motion.wheel_feedback_side) {
        case 1:
            impact.direction_x = -100;
            break;
        case 2:
            impact.direction_x = 100;
            break;
        case 5:
            impact.direction_y = 100;
            break;
        case 6:
            impact.direction_y = -100;
            break;
        default:
            impact.kind = Kind::stop_all;
            break;
    }
    if (impact.kind == Kind::constant) {
        commands.push_back(impact);
        sine_cooldown_ = 15;
    }
    // The source suppresses sine updates, not further impacts. It also leaves
    // an already running sine effect intact throughout these fifteen calls.
    if (sine_cooldown_ > 0) {
        --sine_cooldown_;
        return commands;
    }
    const auto speed = motion.speed_q16;
    const bool moving = speed > 0x3333;
    const bool curb = moving && std::any_of(motion.rumble.begin(), motion.rumble.end(), [](bool active) {
        return active;
    });
    if (curb) {
        commands.push_back({.kind = Kind::sine, .magnitude = speed / 8, .duration_us = std::numeric_limits<std::uint32_t>::max(), .period_us = 160000 - speed});
        curb_active_ = true;
    }
    if (curb_active_) {
        if (!curb) {
            commands.push_back({.kind = Kind::stop_sine});
            curb_active_ = false;
        }
        if (curb_active_) {
            return commands;
        }
    }
    bool loose = false;
    for (std::size_t wheel = 0; wheel < motion.surface_ids.size(); ++wheel) {
        loose |= motion.wheel_contact[wheel] && (motion.surface_ids[wheel] == 2 || motion.surface_ids[wheel] == 3);
    }
    loose &= moving;
    if (loose) {
        commands.push_back({.kind = Kind::sine, .magnitude = speed / 32, .duration_us = std::numeric_limits<std::uint32_t>::max(), .period_us = 100000});
        loose_active_ = true;
    }
    // Keep the two original retained flags (dynamics+320/+324): transitions
    // can stop and then replace the shared sine effect within the same call.
    if (loose_active_ && !loose) {
        commands.push_back({.kind = Kind::stop_sine});
        loose_active_ = false;
    }
    return commands;
}

WheelFeedbackCommand WheelFeedback::reset() {
    sine_cooldown_ = 0;
    curb_active_ = loose_active_ = false;
    return {.kind = WheelFeedbackCommand::Kind::stop_all};
}
} // namespace toca
