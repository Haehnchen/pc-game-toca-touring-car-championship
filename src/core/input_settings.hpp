#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace toca {
inline constexpr std::string_view automatic_input_device = "auto";

enum class BindingKind {
    key,
    button,
    axis,
    joystick_button,
    joystick_axis
};

struct PhysicalBinding {
    BindingKind kind = BindingKind::key;
    std::int32_t code = 0;
    std::int32_t direction = 1;
    // Raw-axis endpoints and released/centered position; pedals may rest at an endpoint.
    std::int32_t calibration_min = -32768, calibration_center = 0, calibration_max = 32767;
    bool operator==(const PhysicalBinding&) const = default;
};

constexpr bool same_physical_binding(const PhysicalBinding& a, const PhysicalBinding& b) {
    return a.kind == b.kind && a.code == b.code && a.direction == b.direction;
}

inline std::array<PhysicalBinding, 9> default_joystick_bindings() {
    return {{{BindingKind::joystick_axis, 1, -1, -32768, 32767, 32767}, {BindingKind::joystick_axis, 2, -1, -32768, 32767, 32767}, {BindingKind::joystick_axis, 0, -1},
        {BindingKind::joystick_axis, 0, 1}, {BindingKind::joystick_button, 4}, {BindingKind::joystick_button, 5}, {BindingKind::joystick_button, 3},
        {BindingKind::joystick_button, 2}, {BindingKind::joystick_button, 1}}};
}

// Active player-one actions, in original configuration order: gas, brake,
// left, right, gear up/down, rear camera, change camera, horn.
// Codes are SDL scancodes/gamepad enums.
struct InputSettings {
    // Empty selects Keyboard; player one may opt into keyboard plus gamepad.
    std::string device_id;
    std::array<PhysicalBinding, 9> keyboard{{{BindingKind::key, 82}, {BindingKind::key, 81}, {BindingKind::key, 80}, {BindingKind::key, 79}, {BindingKind::key, 9},
        {BindingKind::key, 25}, {BindingKind::key, 29}, {BindingKind::key, 6}, {BindingKind::key, 30}}};
    std::array<PhysicalBinding, 9> gamepad{{{BindingKind::axis, 5}, {BindingKind::axis, 4}, {BindingKind::axis, 0, -1}, {BindingKind::axis, 0}, {BindingKind::button, 10},
        {BindingKind::button, 9}, {BindingKind::button, 3}, {BindingKind::button, 2}, {BindingKind::button, 1}}};
    std::uint32_t sensitivity = 2;
    // SDL adaptation: one adjustable deadzone for standardized gamepad axes.
    std::uint32_t deadzone_percent = 2;
    bool rumble_enabled = true;
    bool operator==(const InputSettings&) const = default;
};

// Native shared-keyboard layout; the original second player used the numeric keypad.
inline InputSettings default_player2_input() {
    InputSettings result;
    constexpr std::array<std::int32_t, 9> codes{26, 22, 4, 7, 21, 8, 20, 43, 31};
    for (std::size_t index = 0; index < codes.size(); ++index) {
        result.keyboard[index] = {BindingKind::key, codes[index]};
    }
    return result;
}

struct InputDevice {
    std::string id, name;
    bool raw_joystick = false;
    bool operator==(const InputDevice&) const = default;
};

struct AnalogControls {
    std::int32_t steering_q16 = 0, throttle_q16 = 0, brake_q16 = 0;
    std::uint32_t profile = 2;
    bool steering_analog = false, throttle_analog = false, brake_analog = false;
    bool operator==(const AnalogControls&) const = default;
};

constexpr bool valid_keyboard_binding(const PhysicalBinding& binding, std::size_t action) {
    if (binding.kind != BindingKind::key || binding.direction != 1 || binding.code <= 0 || binding.code >= 512 || action >= 9) {
        return false;
    }
    const auto code = binding.code;
    // 475050 rejects function keys, NumLock and arrows belonging to another
    // action. Native menu/cancel keys and the F5 trackside shortcut stay reserved.
    if ((code >= 58 && code <= 69) || (code >= 104 && code <= 115) || code == 83 || code == 40 || code == 41 || code == 42 || code == 88) {
        return false;
    }
    return code < 79 || code > 82 || (action < 4 && code == 82 - static_cast<std::int32_t>(action));
}

constexpr bool valid_gamepad_binding(const PhysicalBinding& binding) {
    return (binding.kind == BindingKind::button && binding.code >= 0 && binding.code < 26 && binding.code != 6 && binding.direction == 1)
           || (binding.kind == BindingKind::axis && binding.code >= 0 && binding.code < 6 && (binding.direction == -1 || binding.direction == 1))
           || (binding.kind == BindingKind::joystick_button && binding.code >= 0 && binding.code < 128 && binding.direction == 1)
           || (binding.kind == BindingKind::joystick_axis && binding.code >= 0 && binding.code < 32 && (binding.direction == -1 || binding.direction == 1)
               && binding.calibration_min >= -32768 && binding.calibration_max <= 32767 && binding.calibration_min < binding.calibration_max
               && binding.calibration_center >= binding.calibration_min && binding.calibration_center <= binding.calibration_max
               && (binding.direction < 0 ? binding.calibration_center > binding.calibration_min : binding.calibration_center < binding.calibration_max));
}
} // namespace toca
