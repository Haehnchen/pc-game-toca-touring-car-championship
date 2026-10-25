#include "app/driving_input.hpp"

#include <algorithm>

namespace toca::driving_input {
namespace {
constexpr std::int32_t one = 65536;
constexpr std::array<Action, 9> driving_actions{
    Action::up, Action::down, Action::left, Action::right, Action::gear_up, Action::gear_down, Action::camera_rear, Action::camera_next, Action::horn};
} // namespace

// 4926cd: remove the signed deadzone, rescale the remaining range, then
// double to Q16. Separate unipolar triggers and the configurable common
// percentage are native device adaptations.
std::int32_t axis_value(std::int16_t raw, std::uint32_t percent) {
    const auto value = std::int32_t(raw), magnitude = value < 0 ? -value : value;
    const auto deadzone = std::int32_t(percent * 32768 / 100);
    if (magnitude <= deadzone) {
        return 0;
    }
    const auto scaled = ((magnitude - deadzone) * 32768 / (32768 - deadzone)) * 2;
    return value < 0 ? -scaled : scaled;
}

std::int32_t raw_axis_value(std::int16_t raw, const PhysicalBinding& binding, std::uint32_t percent) {
    const auto delta = (std::int32_t(raw) - binding.calibration_center) * binding.direction;
    const auto range = binding.direction > 0 ? binding.calibration_max - binding.calibration_center : binding.calibration_center - binding.calibration_min;
    if (range <= 0) {
        return 0;
    }
    const auto deadzone = range * std::int32_t(percent) / 100;
    return delta <= deadzone ? 0 : std::int32_t(std::min<std::int64_t>(one, std::int64_t(delta - deadzone) * one / (range - deadzone)));
}

State evaluate(const InputSettings& config, const Sample& sample) {
    State result;
    result.analog.profile = config.sensitivity;
    std::array<std::int32_t, 9> values{};
    std::array<bool, 9> analog{};
    const bool automatic = config.device_id == automatic_input_device;
    const bool keyboard_enabled = config.device_id.empty() || automatic;
    const bool keyboard_steering = keyboard_enabled && (sample.keys[2] || sample.keys[3]);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto& binding = config.gamepad[i];
        if (!config.device_id.empty() && binding.kind == BindingKind::joystick_button) {
            values[i] = sample.device == DeviceKind::joystick && sample.bindings[i].button ? one : 0;
        } else if (!config.device_id.empty() && binding.kind == BindingKind::joystick_axis) {
            analog[i] = sample.device == DeviceKind::joystick;
            if (analog[i] && sample.bindings[i].armed) {
                values[i] = raw_axis_value(sample.bindings[i].axis, binding, config.deadzone_percent);
            }
        } else if (!config.device_id.empty() && binding.kind == BindingKind::button) {
            values[i] = sample.device == DeviceKind::gamepad && sample.bindings[i].button ? one : 0;
        } else if (!config.device_id.empty()) {
            analog[i] = !automatic || sample.device != DeviceKind::none;
            values[i] = sample.device == DeviceKind::gamepad
                            ? std::max(0, (sample.bindings[i].armed ? axis_value(sample.bindings[i].axis, config.deadzone_percent) : 0) * binding.direction)
                            : 0;
            if (i == 2 || i == 3) {
                // Gamepad thumbstick center response; raw wheel axes remain linear.
                values[i] = std::int32_t((std::int64_t(values[i]) * values[i]) / one);
            }
        }
        if (keyboard_enabled) {
            const auto keyboard_value = sample.keys[i] ? one : 0;
            if ((i == 2 || i == 3) && keyboard_steering) {
                values[i] = keyboard_value;
                analog[i] = false;
            } else if (i != 2 && i != 3 && keyboard_value > 0) {
                values[i] = std::max(values[i], keyboard_value);
                analog[i] = false;
            }
        }
        // Analog gear bindings are edges of the half-axis threshold;
        // pedals retain their full unquantized magnitude.
        if (values[i] > 0 && (i < 4 || values[i] >= one / 2)) {
            result.held |= action_mask(driving_actions[i]);
        }
    }
    // An idle analog gas axis must not bypass419630 when a digital
    // brake becomes reverse gas. Keep active proportional pedals direct.
    if (values[0] == 0 && values[1] != 0 && !analog[1]) {
        analog[0] = false;
    }
    if (values[2] != 0 && values[3] != 0) {
        result.held &= ~action_mask(Action::right); // 429cd/422f60 Left wins.
    }
    result.analog.steering_q16 = values[2] != 0 ? -values[2] : values[3];
    result.analog.throttle_q16 = values[0];
    result.analog.brake_q16 = values[1];
    result.analog.steering_analog = analog[2] || analog[3];
    result.analog.throttle_analog = analog[0];
    result.analog.brake_analog = analog[1];
    return result;
}
} // namespace toca::driving_input
