#pragma once

#include <cstdint>

namespace toca::simulation {
struct MotionState;
}

namespace toca {
struct GamepadRumble {
    std::uint16_t low = 0, high = 0;
    bool operator==(const GamepadRumble&) const = default;
};

// Native two-motor adaptation of actual local contact values, updated at25Hz.
// This does not change physics or emulate a directional force-feedback wheel.
class GamepadFeedback {
    std::int32_t impact_q16_ = 0;

public:
    GamepadRumble update(const simulation::MotionState& motion, std::int32_t local_car_impact_q16 = 0);
    void reset();
};
} // namespace toca
