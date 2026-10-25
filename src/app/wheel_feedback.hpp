#pragma once

#include <cstdint>
#include <vector>

namespace toca::simulation {
struct MotionState;
}

namespace toca {
// Original DirectInput units: Cartesian direction components, nominal magnitude
// 10000 and microseconds. The device backend converts these to SDL units.
struct WheelFeedbackCommand {
    enum class Kind {
        constant,
        sine,
        stop_sine,
        stop_all
    };
    Kind kind = Kind::stop_all;
    std::int32_t direction_x = 0, direction_y = 0;
    std::int32_t magnitude = 0;
    std::uint32_t duration_us = 0;
    // Keep the original signed 160000-speed result; a backend must constrain
    // nonpositive/sub-millisecond periods to its smallest supported period.
    std::int32_t period_us = 0;
    bool operator==(const WheelFeedbackCommand&) const = default;
};

// 40f5c0's original constant-force/sine consumer, once per physical update.
// Device assignment and pause/exit stops belong to the host, not physics.
class WheelFeedback {
    std::uint8_t sine_cooldown_ = 0;
    bool curb_active_ = false, loose_active_ = false;

public:
    std::vector<WheelFeedbackCommand> update(const simulation::MotionState& motion);
    WheelFeedbackCommand reset();
};
} // namespace toca
