#pragma once

#include "core/contracts.hpp"

#include <array>
#include <cstdint>

namespace toca::driving_input {
enum class DeviceKind {
    none,
    gamepad,
    joystick
};

struct BindingSample {
    std::int16_t axis = 0;
    bool button = false;
    bool armed = false;
};

// Samples follow the nine configured driving actions, not physical key/axis
// numbers. The device boundary owns focus blocking, axis rearming and selection.
// Missing axes have armed=false; blocked keys/buttons are already released.
struct Sample {
    DeviceKind device = DeviceKind::none;
    std::array<bool, 9> keys{};
    std::array<BindingSample, 9> bindings{};
};

struct State {
    std::uint32_t held = 0;
    AnalogControls analog;
    bool operator==(const State&) const = default;
};

// Callers validate settings first: deadzone0..30 and valid physical bindings.
std::int32_t axis_value(std::int16_t raw, std::uint32_t percent);
std::int32_t raw_axis_value(std::int16_t raw, const PhysicalBinding& binding, std::uint32_t percent);
State evaluate(const InputSettings& config, const Sample& sample);
} // namespace toca::driving_input
