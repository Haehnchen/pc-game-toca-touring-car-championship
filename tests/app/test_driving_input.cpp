#include "app/driving_input.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::driving_input;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Sample gamepad_sample() {
    Sample sample;
    sample.device = DeviceKind::gamepad;
    for (auto& binding : sample.bindings) {
        binding.armed = true;
    }
    return sample;
}

void signed_deadzone() {
    expect(axis_value(0, 0) == 0 && axis_value(16384, 0) == 32768 && axis_value(-16384, 0) == -32768,
        "Standard axes must retain exact zero and signed half travel before steering shaping");
    expect(axis_value(std::numeric_limits<std::int16_t>::min(), 0) == -65536 && axis_value(std::numeric_limits<std::int16_t>::max(), 0) == 65534,
        "Signed SDL endpoints must retain their original one-unit asymmetry");
    expect(axis_value(3276, 10) == 0 && axis_value(-3276, 10) == 0 && axis_value(3277, 10) == 2 && axis_value(-3277, 10) == -2,
        "The truncated ten-percent deadzone must include its boundary and preserve the first active signed step");
    expect(axis_value(9830, 30) == 0 && axis_value(-9830, 30) == 0 && axis_value(9831, 30) == 2 && axis_value(-9831, 30) == -2,
        "The maximum supported deadzone must retain the same neutral and first-step rules");
    expect(axis_value(-32768, 30) == -65536 && axis_value(32767, 30) == 65532, "Deadzone rescaling must preserve full negative travel and original positive truncation");
}

void calibrated_axes() {
    PhysicalBinding binding{BindingKind::joystick_axis, 0, -1, -10000, 20000, 30000};
    expect(raw_axis_value(20000, binding, 0) == 0 && raw_axis_value(25000, binding, 0) == 0 && raw_axis_value(5000, binding, 0) == 32768,
        "A calibrated negative half-axis must use its own rest and directional range");
    expect(raw_axis_value(-10000, binding, 0) == 65536 && raw_axis_value(-32768, binding, 0) == 65536, "Calibrated travel must saturate at and beyond its captured endpoint");
    expect(raw_axis_value(17000, binding, 10) == 0 && raw_axis_value(3500, binding, 10) == 32768,
        "Raw-axis deadzones must scale the calibrated half range rather than the signed device range");
    binding.direction = 1;
    expect(raw_axis_value(25000, binding, 0) == 32768 && raw_axis_value(30000, binding, 0) == 65536 && raw_axis_value(32767, binding, 0) == 65536,
        "Asymmetric positive travel must use its independent endpoint and saturation");
    expect(raw_axis_value(21000, binding, 10) == 0 && raw_axis_value(25500, binding, 10) == 32768,
        "Positive calibrated deadzones must preserve the exact midpoint of the remaining travel");
    binding = default_joystick_bindings()[0];
    expect(raw_axis_value(32767, binding, 0) == 0 && raw_axis_value(-1, binding, 0) == 32768 && raw_axis_value(-32768, binding, 0) == 65536,
        "Endpoint-resting pedals must remain unipolar with exact released, half and full travel");
}

void keyboard_actions() {
    InputSettings config;
    config.sensitivity = 3;
    auto sample = gamepad_sample();
    sample.bindings[0].axis = 32767;
    sample.bindings[8].button = true;
    expect(evaluate(config, sample) == State{0, {.profile = 3}}, "Explicit Keyboard must ignore all connected-pad values");
    constexpr std::array actions{Action::up, Action::down, Action::left, Action::right, Action::gear_up, Action::gear_down, Action::camera_rear, Action::camera_next, Action::horn};
    for (std::size_t index = 0; index < actions.size(); ++index) {
        sample.keys.fill(false);
        sample.keys[index] = true;
        const auto result = evaluate(config, sample);
        expect(result.held == action_mask(actions[index]), "Each configured keyboard action must emit only its own driving bit");
        expect(!result.analog.steering_analog && !result.analog.throttle_analog && !result.analog.brake_analog && result.analog.profile == 3,
            "Keyboard actions must remain digital while retaining their selected sensitivity");
    }
    sample.keys.fill(true);
    const auto result = evaluate(config, sample);
    expect((result.held & action_mask(Action::left)) != 0 && (result.held & action_mask(Action::right)) == 0 && result.analog.steering_q16 == -65536,
        "Simultaneous keyboard steering must retain Left priority");
    expect(result.analog.throttle_q16 == 65536 && result.analog.brake_q16 == 65536, "Simultaneous keyboard pedals must remain independent full-strength values");
}

void standardized_controls() {
    InputSettings config;
    config.device_id = "pad";
    config.deadzone_percent = 0;
    config.sensitivity = 1;
    auto sample = gamepad_sample();
    sample.bindings[0].axis = 16384;
    sample.bindings[2].axis = sample.bindings[3].axis = -16384;
    const auto result = evaluate(config, sample);
    expect(result.held == (action_mask(Action::up) | action_mask(Action::left)) && result.analog.throttle_q16 == 32768 && result.analog.steering_q16 == -16384,
        "Standard half-stick steering must be squared while half-trigger gas stays linear");
    expect(result.analog.profile == 1 && result.analog.steering_analog && result.analog.throttle_analog && result.analog.brake_analog,
        "Neutral standardized analog bindings must retain the selected analog control paths");
    sample.keys.fill(true);
    expect(evaluate(config, sample) == result, "An explicit pad selection must ignore keyboard driving actions");
    sample.bindings[0].armed = sample.bindings[2].armed = sample.bindings[3].armed = false;
    const auto blocked = evaluate(config, sample);
    expect(blocked.held == 0 && blocked.analog.steering_q16 == 0 && blocked.analog.throttle_q16 == 0 && blocked.analog.steering_analog && blocked.analog.throttle_analog,
        "Unarmed standardized axes must stay neutral without changing their analog binding classification");
    sample = {};
    const auto disconnected = evaluate(config, sample);
    expect(disconnected.held == 0 && disconnected.analog.steering_analog && disconnected.analog.throttle_analog && disconnected.analog.brake_analog,
        "A disconnected explicit pad must preserve its original analog mode flags with zero values");
    config.gamepad[0] = {BindingKind::button, 1};
    sample = gamepad_sample();
    sample.bindings[0] = {32767, true, false};
    const auto button = evaluate(config, sample);
    expect(button.held == action_mask(Action::up) && button.analog.throttle_q16 == 65536 && !button.analog.throttle_analog,
        "A digital pedal binding must use its button without consulting axis values or rearming");
}

void raw_controls() {
    InputSettings config;
    config.device_id = "wheel";
    config.gamepad = default_joystick_bindings();
    config.deadzone_percent = 0;
    auto sample = gamepad_sample();
    sample.device = DeviceKind::joystick;
    sample.bindings[0].axis = -1;
    sample.bindings[1].axis = 32767;
    sample.bindings[2].axis = sample.bindings[3].axis = -16384;
    sample.bindings[4].button = true;
    const auto result = evaluate(config, sample);
    expect(result.held == (action_mask(Action::up) | action_mask(Action::left) | action_mask(Action::gear_up)) && result.analog.steering_q16 == -32768
               && result.analog.throttle_q16 == 32768 && result.analog.brake_q16 == 0,
        "Raw half-wheel steering must stay linear and endpoint-resting pedals must keep their calibrated range");
    expect(result.analog.steering_analog && result.analog.throttle_analog && result.analog.brake_analog, "Raw analog bindings must remain separate from raw button actions");
    sample.bindings[0].armed = sample.bindings[2].armed = sample.bindings[3].armed = false;
    const auto blocked = evaluate(config, sample);
    expect(blocked.held == action_mask(Action::gear_up) && blocked.analog.steering_q16 == 0 && blocked.analog.throttle_q16 == 0 && blocked.analog.steering_analog,
        "Unarmed or missing raw axes must stay neutral while independent raw buttons still work");
    sample.device = DeviceKind::gamepad;
    expect(evaluate(config, sample) == State{}, "Raw bindings must reject values from a standardized gamepad even when its sample has matching indices");
    sample.device = DeviceKind::none;
    expect(evaluate(config, sample) == State{}, "Disconnected raw bindings must remain digital-neutral rather than claiming analog device availability");
}

void automatic_mixing() {
    InputSettings config;
    config.device_id = automatic_input_device;
    config.deadzone_percent = 0;
    auto sample = gamepad_sample();
    sample.bindings[0].axis = 16384;
    sample.bindings[2].axis = sample.bindings[3].axis = -16384;
    sample.keys[3] = true;
    auto result = evaluate(config, sample);
    expect(result.analog.steering_q16 == 65536 && !result.analog.steering_analog && result.analog.throttle_q16 == 32768 && result.analog.throttle_analog,
        "Auto keyboard steering must override both stick halves without changing proportional gamepad gas");
    sample.keys[0] = true;
    result = evaluate(config, sample);
    expect(result.analog.throttle_q16 == 65536 && !result.analog.throttle_analog, "Auto keyboard gas must select the digital path at full strength");
    sample.keys[0] = false;
    sample.keys[1] = true;
    sample.bindings[0].axis = 0;
    result = evaluate(config, sample);
    expect(result.analog.throttle_q16 == 0 && !result.analog.throttle_analog && result.analog.brake_q16 == 65536 && !result.analog.brake_analog,
        "Auto digital brake with idle analog gas must preserve the original digital reverse-gas path");
    sample.bindings[0].axis = 16384;
    result = evaluate(config, sample);
    expect(result.analog.throttle_q16 == 32768 && result.analog.throttle_analog && !result.analog.brake_analog,
        "An active proportional pedal must remain analog while the other pedal is digital");
    sample.keys[2] = true;
    result = evaluate(config, sample);
    expect(result.analog.steering_q16 == -65536 && (result.held & action_mask(Action::right)) == 0, "Auto simultaneous keyboard directions must retain Left priority");
    sample.keys.fill(false);
    config.gamepad[3].code = 1;
    sample.bindings[3].axis = 32767;
    result = evaluate(config, sample);
    expect(result.analog.steering_q16 == -16384 && (result.held & action_mask(Action::right)) == 0 && result.analog.steering_analog,
        "Left must also win over a stronger independently bound analog Right input");
    sample = {};
    expect(evaluate(config, sample) == State{}, "Auto without a gamepad must fall back to digital-neutral keyboard controls");
    sample.keys[8] = true;
    expect(evaluate(config, sample).held == action_mask(Action::horn), "Auto keyboard actions must remain available after the pad disconnects");
}

void analog_action_thresholds() {
    constexpr std::array actions{Action::gear_up, Action::gear_down, Action::camera_rear, Action::camera_next, Action::horn};
    for (std::size_t index = 0; index < actions.size(); ++index) {
        InputSettings config;
        config.device_id = "pad";
        config.deadzone_percent = 0;
        config.gamepad[4 + index] = {BindingKind::axis, 1};
        auto sample = gamepad_sample();
        sample.bindings[4 + index].axis = 16383;
        expect(evaluate(config, sample).held == 0, "Analog discrete actions must stay released immediately below half travel");
        sample.bindings[4 + index].axis = 16384;
        expect(evaluate(config, sample).held == action_mask(actions[index]), "Every analog discrete action must activate exactly at half travel");
        sample.bindings[4 + index].axis = -32768;
        expect(evaluate(config, sample).held == 0, "The opposite half-axis must not activate a discrete action");
    }
    InputSettings config;
    config.device_id = "wheel";
    config.gamepad = default_joystick_bindings();
    config.gamepad[4] = {BindingKind::joystick_axis, 3, -1};
    config.deadzone_percent = 0;
    auto sample = gamepad_sample();
    sample.device = DeviceKind::joystick;
    sample.bindings[0].axis = sample.bindings[1].axis = 32767;
    sample.bindings[4].axis = -16383;
    expect(evaluate(config, sample).held == 0, "Calibrated raw discrete actions must stay released immediately below half travel");
    sample.bindings[4].axis = -16384;
    expect(evaluate(config, sample).held == action_mask(Action::gear_up), "Calibrated raw discrete actions must also activate at their exact half travel");
}
} // namespace

int main() {
    try {
        signed_deadzone();
        calibrated_axes();
        keyboard_actions();
        standardized_controls();
        raw_controls();
        automatic_mixing();
        analog_action_thresholds();
        std::cout << "Driving input normalization, bindings and keyboard/pad mixing passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
