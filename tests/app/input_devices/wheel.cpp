#include "fixtures.hpp"

#include <algorithm>
#include <limits>

namespace toca::test::input_devices {
void raw_wheel_checks() {
    VirtualWheel first, twin;
    twin.axis(1, -32768);
    SDL_UpdateJoysticks();
    InputSettings settings;
    settings.device_id = first.guid();
    settings.gamepad = default_joystick_bindings();
    settings.deadzone_percent = 0;
    InputSettings second = settings;
    second.gamepad[0] = {BindingKind::joystick_axis, 1, 1, -32768, -32768, 32767};
    InputDevices devices(settings, InputDevices::DeviceScope::virtual_only);
    devices.apply(settings, second);
    devices.set_local_players(2);
    (void)pump(devices);
    const auto list = devices.devices();
    expect(std::count_if(list.begin(), list.end(),
               [&](const InputDevice& device) {
                   return device.id == first.guid() && device.raw_joystick;
               })
               == 1,
        "Identical raw wheels share a stable distinct-from-gamepad GUID entry while retaining live instance ownership");
    expect(devices.selected_device(0) && devices.selected_device(1) && !devices.rumble_supported() && !devices.rumble(100, 200) && !devices.wheel_feedback_supported(),
        "Raw wheels are independently selectable and virtual nonhaptic devices never claim rumble or actual FFB");
    expect(devices.state().analog.throttle_q16 == 0 && devices.state().analog.brake_q16 == 0, "Pedals resting at +32767 must be released rather than full throttle/brake");
    first.axis(0, 16384);
    first.axis(1, 0);
    first.button(24, true);
    auto captured = pump(devices);
    expect(devices.state(0).analog.steering_q16 >= 32768 && devices.state(0).analog.steering_q16 <= 32770 && devices.state(0).analog.throttle_q16 >= 32767
               && devices.state(0).analog.throttle_q16 <= 32768 && devices.state(1).analog.steering_q16 == 0 && devices.state(1).analog.throttle_q16 == 0,
        "Wheel half-lock and inverted full-range half-pedal remain linear and independent of the identical second wheel");
    expect(std::any_of(captured.begin(), captured.end(),
               [](const PhysicalBinding& binding) {
                   return binding.kind == BindingKind::joystick_button && binding.code == 24;
               }),
        "Raw buttons beyond the standardized gamepad range remain bindable");
    first.axis(1, -32768);
    first.axis(2, -32768);
    twin.axis(0, -16384);
    twin.axis(1, 0);
    (void)pump(devices);
    expect(devices.state(0).analog.throttle_q16 == 65536 && devices.state(0).analog.brake_q16 == 65536 && devices.state(1).analog.steering_q16 == -32768,
        "Raw pedals retain their complete signed-axis travel and player two retains independent linear steering");
    devices.set_focus(false);
    devices.set_focus(true);
    expect(devices.state(0).driving_held == 0 && devices.state(1).driving_held == 0, "Focus loss clears both raw streams");
    first.axis(1, -30000);
    twin.axis(0, -15000);
    twin.axis(1, 1000);
    (void)pump(devices);
    expect(devices.state(0).analog.throttle_q16 == 0 && devices.state(1).analog.steering_q16 == 0 && devices.state(1).analog.throttle_q16 == 0,
        "Held raw pedals/wheel cannot reactivate after focus return until they pass their calibrated neutral");
    first.axis(0, 0);
    first.axis(1, 32767);
    first.axis(2, 32767);
    twin.axis(0, 0);
    twin.axis(1, -32768);
    (void)pump(devices);
    first.axis(1, 0);
    twin.axis(1, 0);
    (void)pump(devices);
    expect(devices.state(0).analog.throttle_q16 >= 32767 && devices.state(0).analog.throttle_q16 <= 32768 && devices.state(1).analog.throttle_q16 >= 32767
               && devices.state(1).analog.throttle_q16 <= 32769,
        "Identical wheel instances rearm using their own opposite pedal-rest calibration, without consulting the sibling profile");
    first.axis(1, 32767);
    twin.axis(1, -32768);
    (void)pump(devices);
    devices.begin_binding_capture();
    first.axis(1, -32768);
    captured = pump(devices);
    const auto pedal = std::find_if(captured.begin(), captured.end(), [](const PhysicalBinding& binding) {
        return binding.kind == BindingKind::joystick_axis && binding.code == 1;
    });
    expect(pedal != captured.end() && pedal->direction == -1 && pedal->calibration_center == 32767 && valid_gamepad_binding(*pedal),
        "Capture records the actual released endpoint and direction for inverted full-travel pedals");
    first.axis(1, 32767);
    first.axis(7, -32768);
    (void)pump(devices);
    devices.begin_binding_capture();
    first.axis(7, 32767);
    captured = pump(devices);
    const auto extra = std::find_if(captured.begin(), captured.end(), [](const PhysicalBinding& binding) {
        return binding.kind == BindingKind::joystick_axis && binding.code == 7;
    });
    expect(extra != captured.end() && extra->calibration_center == -32768 && extra->direction == 1, "Capture also supports extra raw axes with the opposite end-rest orientation");
    settings.gamepad[0] = *extra;
    devices.apply(settings, second);
    first.axis(7, -32768);
    (void)pump(devices);
    first.axis(7, 32767);
    (void)pump(devices);
    expect(devices.state().analog.throttle_q16 == 65536, "Captured raw calibration feeds the actual driving controls");
    first.detach();
    (void)pump(devices);
    expect(!devices.selected_device(0) && devices.selected_device(1) && devices.state(0).driving_held == 0,
        "Disconnect never steals an already-connected identical wheel from player two");
    VirtualWheel replacement;
    (void)pump(devices);
    expect(devices.selected_device(0) && devices.selected_device(1) && devices.state().analog.throttle_q16 == 0,
        "A newly hotplugged identical raw wheel reconnects only the missing owner with neutral input");
}

} // namespace toca::test::input_devices
