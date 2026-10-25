#include "fixtures.hpp"

#include <algorithm>
#include <limits>

namespace toca::test::input_devices {
void checks() {
    InputSettings settings;
    InputDevices devices(settings, InputDevices::DeviceScope::virtual_only);
    expect(devices.devices().front().id.empty(), "Device list must retain the explicit keyboard selection");
    settings.keyboard[0] = {BindingKind::key, SDL_SCANCODE_W};
    devices.apply(settings);
    const auto capture = devices.handle_event(key(SDL_SCANCODE_W, true));
    expect(capture == settings.keyboard[0] && (devices.state().driving_held & action_mask(Action::up)) != 0 && devices.state().menu_held == 0,
        "Rebound keyboard gas must affect gameplay without changing fixed menu navigation");
    expect(!devices.handle_event(key(SDL_SCANCODE_W, true)), "A held physical key must not produce repeated capture edges");
    devices.handle_event(key(SDL_SCANCODE_UP, true));
    expect((devices.state().menu_held & action_mask(Action::up)) != 0, "Fixed arrow navigation must survive keyboard rebinding");
    devices.set_focus(false);
    devices.set_focus(true);
    devices.handle_event(key(SDL_SCANCODE_W, true));
    expect(devices.state().driving_held == 0, "Focus/pause clear must block a previously held key until release");
    devices.handle_event(key(SDL_SCANCODE_W, false));
    devices.handle_event(key(SDL_SCANCODE_W, true));
    expect((devices.state().driving_held & action_mask(Action::up)) != 0, "Fresh physical key presses must rearm after focus return");
    devices.handle_event(key(SDL_SCANCODE_W, false));
    devices.handle_event(key(SDL_SCANCODE_UP, false));
    for (const auto [code, action] :
        std::array{std::pair{SDL_SCANCODE_Z, Action::camera_rear}, std::pair{SDL_SCANCODE_C, Action::camera_next}, std::pair{SDL_SCANCODE_1, Action::horn}}) {
        devices.handle_event(key(code, true));
        expect(devices.state().driving_held == action_mask(action) && devices.state().menu_held == 0,
            "All original extra keyboard actions must use configurable gameplay bindings independently of menus");
        devices.handle_event(key(code, false));
    }
    settings.keyboard[6] = {BindingKind::key, SDL_SCANCODE_R};
    settings.keyboard[7] = {BindingKind::key, SDL_SCANCODE_T};
    settings.keyboard[8] = {BindingKind::key, SDL_SCANCODE_H};
    devices.apply(settings);
    devices.handle_event(key(SDL_SCANCODE_C, true));
    devices.handle_event(key(SDL_SCANCODE_Z, true));
    expect(devices.state().driving_held == 0, "Rebinding cameras must replace C/Z rather than leave hidden fixed shortcuts");
    devices.handle_event(key(SDL_SCANCODE_C, false));
    devices.handle_event(key(SDL_SCANCODE_Z, false));
    for (const auto [code, action] : std::array{std::pair{SDL_SCANCODE_R, Action::camera_rear}, std::pair{SDL_SCANCODE_T, Action::camera_next},
             std::pair{SDL_SCANCODE_H, Action::horn}, std::pair{SDL_SCANCODE_F5, Action::camera_trackside}}) {
        devices.handle_event(key(code, true));
        expect(devices.state().driving_held == action_mask(action), "Rebound camera/horn and optional fixed trackside input must be delivered");
        devices.handle_event(key(code, false));
    }

    VirtualPad pad;
    (void)pump(devices);
    const auto guid = pad.guid();
    const auto list = devices.devices();
    expect(std::any_of(list.begin(), list.end(),
               [&](const InputDevice& device) {
                   return device.id == guid;
               }),
        "Hotplug must expose a connected model using its stable GUID preference id");
    settings.device_id = guid;
    settings.sensitivity = 3;
    settings.deadzone_percent = 10;
    devices.apply(settings);
    expect(!devices.rumble_supported() && !devices.rumble(100, 200), "A selected virtual pad without rumble must remain usable without claiming support");
    for (const auto [button, action] : std::array{
             std::pair{SDL_GAMEPAD_BUTTON_NORTH, Action::camera_rear}, std::pair{SDL_GAMEPAD_BUTTON_WEST, Action::camera_next}, std::pair{SDL_GAMEPAD_BUTTON_EAST, Action::horn}}) {
        pad.button(button, true);
        (void)pump(devices);
        expect((devices.state().driving_held & action_mask(action)) != 0 && devices.state().pause_held == 0,
            "Default gamepad camera/horn buttons must feed gameplay without opening pause");
        pad.button(button, false);
        (void)pump(devices);
    }
    expect(devices.state().analog.profile == 3 && devices.state().analog.steering_analog && devices.state().analog.throttle_analog,
        "Selected gamepad binding types must choose independent original analog flags and current sensitivity");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 2000);
    (void)pump(devices);
    expect(devices.state().analog.steering_q16 == 0, "Inside-deadzone axis events must remain exactly neutral");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, -24000);
    const auto left = pump(devices);
    expect(devices.state().analog.steering_q16 < -30000 && devices.state().analog.steering_q16 > -34000 && (devices.state().driving_held & action_mask(Action::left)) != 0,
        "Real SDL gamepad events must preserve left direction with a progressive magnitude after deadzone removal");
    expect(std::find(left.begin(), left.end(), PhysicalBinding{BindingKind::axis, SDL_GAMEPAD_AXIS_LEFTX, -1}) != left.end(),
        "Crossing the half-axis capture threshold must report its physical direction once");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, -25000);
    expect(pump(devices).empty(), "Axis motion while held above threshold must not repeat capture");
    pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    (void)pump(devices);
    expect(devices.state().analog.throttle_q16 > 0 && devices.state().analog.throttle_q16 < 65536, "Independent standardized trigger events must retain proportional gas");
    TimedActions timeline;
    timeline.reset(0, true);
    timeline.set_analog(50, devices.state().analog);
    expect(!timeline.take(40).analog && timeline.take(80).analog->throttle_q16 > 0,
        "Actual device values must enter their timestamped simulation deadline rather than earlier catch-up ticks");

    devices.set_focus(false);
    devices.set_focus(true);
    expect(devices.state().driving_held == 0 && devices.state().analog.steering_q16 == 0 && devices.state().analog.profile == 3,
        "Focus return must preserve profile while neutralizing physically held gamepad axes");
    SDL_Event duplicate{};
    duplicate.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    duplicate.gaxis.which = pad.id;
    duplicate.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    duplicate.gaxis.value = -25000;
    devices.handle_event(duplicate);
    expect(devices.state().analog.steering_q16 == 0, "An unchanged held-axis event must not reactivate after focus clear");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    (void)pump(devices);
    pad.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    (void)pump(devices);
    expect((devices.state().driving_held & action_mask(Action::gear_up)) != 0, "Mapped gamepad button must feed the existing gear edge path");
    devices.set_focus(false);
    devices.set_focus(true);
    expect(devices.state().driving_held == 0, "Held buttons must be cleared on pause/focus loss");
    pad.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
    (void)pump(devices);
    pad.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    (void)pump(devices);
    expect((devices.state().menu_held & action_mask(Action::confirm)) != 0, "Fixed gamepad South must navigate menus independently of driving bindings");
    pad.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    (void)pump(devices);
    settings.gamepad[0] = {BindingKind::button, SDL_GAMEPAD_BUTTON_EAST};
    settings.gamepad[8] = {BindingKind::button, SDL_GAMEPAD_BUTTON_SOUTH};
    devices.apply(settings);
    pad.button(SDL_GAMEPAD_BUTTON_EAST, true);
    (void)pump(devices);
    expect((devices.state().driving_held & action_mask(Action::up)) && (devices.state().menu_held & action_mask(Action::back)) && devices.state().pause_held == 0,
        "Pad East may drive a rebound action and navigate Back, without opening the driving pause menu");
    pad.button(SDL_GAMEPAD_BUTTON_EAST, false);
    pad.button(SDL_GAMEPAD_BUTTON_START, true);
    (void)pump(devices);
    expect(devices.state().pause_held == action_mask(Action::back), "Pad Start must retain a separate fixed driving pause action");
    pad.detach();
    (void)pump(devices);
    expect(devices.state().driving_held == 0 && devices.state().menu_held == 0 && devices.state().analog.throttle_q16 == 0,
        "Unplug must release all selected-device values immediately");
    VirtualPad replacement;
    (void)pump(devices);
    expect(replacement.guid() == guid && devices.state().analog.profile == 3 && devices.state().driving_held == 0,
        "Reconnection must retain the saved model selection without retaining held input");
}

void progressive_steering_checks() {
    VirtualPad pad;
    InputSettings settings;
    settings.device_id = pad.guid();
    settings.gamepad[4] = {BindingKind::axis, SDL_GAMEPAD_AXIS_RIGHTX};
    InputDevices devices(settings, InputDevices::DeviceScope::virtual_only);
    (void)pump(devices);
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 500);
    (void)pump(devices);
    expect(devices.state().analog.steering_q16 == 0, "Default steering deadzone must remain neutral");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 8684);
    (void)pump(devices);
    const auto quarter = devices.state().analog.steering_q16;
    expect(quarter >= 3800 && quarter <= 4400, "A quarter steering travel after deadzone must give gentle center response around six percent");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 16712);
    const auto captured = pump(devices);
    const auto half = devices.state().analog.steering_q16;
    expect(half >= 16000 && half <= 17000 && half > quarter, "Half steering travel after deadzone must retain progressive control around one-quarter lock");
    expect((devices.state().menu_held & action_mask(Action::right))
               && std::find(captured.begin(), captured.end(), PhysicalBinding{BindingKind::axis, SDL_GAMEPAD_AXIS_LEFTX, 1}) != captured.end(),
        "Steering response must not alter the original menu or physical-capture half-axis threshold");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, -16712);
    (void)pump(devices);
    expect(devices.state().analog.steering_q16 == -half && (devices.state().menu_held & action_mask(Action::left)),
        "Matching left and right deflections must give symmetric steering and unchanged menu directions");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 32767);
    (void)pump(devices);
    expect(devices.state().analog.steering_q16 >= 65520, "Full right travel must retain full steering lock within SDL's signed endpoint quantization");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, -32768);
    (void)pump(devices);
    expect(devices.state().analog.steering_q16 == -65536, "Full left travel must retain full steering lock");
    pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
    pad.axis(SDL_GAMEPAD_AXIS_RIGHTX, 20000);
    (void)pump(devices);
    expect(devices.state().analog.throttle_q16 >= 30000 && devices.state().analog.throttle_q16 <= 34000 && devices.state().analog.brake_q16 == devices.state().analog.throttle_q16
               && (devices.state().driving_held & action_mask(Action::gear_up)),
        "Half pedals and an analog gear binding must keep their linear magnitude and threshold");
    settings.gamepad[2] = {BindingKind::button, SDL_GAMEPAD_BUTTON_DPAD_LEFT};
    settings.gamepad[3] = {BindingKind::button, SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
    devices.apply(settings);
    pad.button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
    (void)pump(devices);
    expect(!devices.state().analog.steering_analog && devices.state().analog.steering_q16 == -65536, "Digital gamepad steering must retain full-strength button input");
    settings.device_id.clear();
    devices.apply(settings);
    devices.handle_event(key(SDL_SCANCODE_RIGHT, true));
    expect(!devices.state().analog.steering_analog && devices.state().analog.steering_q16 == 65536, "Keyboard steering must retain full-strength digital input");
}

void confirming_pad_checks() {
    RumbleTrace first, second, reconnected;
    VirtualPad other(&first, 0x5446), starter(&second, 0x5446);
    expect(other.id != starter.id && other.guid() == starter.guid(), "Two identical pads must have distinct live instances and the same saved GUID");
    InputSettings settings;
    InputDevices devices(settings, InputDevices::DeviceScope::virtual_only);
    const auto poll = [&] {
        devices.begin_poll();
        (void)pump(devices);
    };
    poll();
    other.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
    other.axis(SDL_GAMEPAD_AXIS_LEFTX, -24000);
    poll();
    expect((devices.state().menu_held & action_mask(Action::down)) && !devices.select_confirming_gamepad() && devices.state().driving_held == 0 && !devices.rumble(100, 200),
        "Navigation or axis activity alone must not replace the initial Keyboard driving selection");
    other.button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
    other.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    poll();

    starter.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    poll();
    expect((devices.state().menu_held & action_mask(Action::confirm)) != 0, "An unselected pad must still supply the real menu confirmation");
    devices.begin_poll();
    expect(!devices.select_confirming_gamepad(), "A confirmation from an earlier poll must never select a driving device");
    (void)pump(devices);
    expect(!devices.select_confirming_gamepad(), "A held South button must not create another confirmation candidate");
    starter.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    poll();
    for (const bool keyboard_first : {false, true}) {
        devices.begin_poll();
        if (keyboard_first) {
            devices.handle_event(key(SDL_SCANCODE_RETURN, true));
        }
        starter.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
        (void)pump(devices);
        if (!keyboard_first) {
            devices.handle_event(key(SDL_SCANCODE_RETURN, true));
        }
        expect(!devices.select_confirming_gamepad(), "A fresh keyboard confirmation must win over South within its poll in either event order");
        devices.handle_event(key(SDL_SCANCODE_RETURN, false));
        starter.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
        poll();
    }

    devices.set_focus(false);
    starter.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    poll();
    expect(!devices.select_confirming_gamepad(), "An unfocused pad confirmation must not select a device");
    devices.set_focus(true);
    poll();
    expect(!devices.select_confirming_gamepad(), "Focus regain must not replay an unfocused or held confirmation");
    starter.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    poll();

    starter.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    starter.axis(SDL_GAMEPAD_AXIS_LEFTX, -24000);
    starter.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    starter.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    poll();
    const auto selected = devices.select_confirming_gamepad();
    expect(selected == std::optional{starter.guid()} && !devices.select_confirming_gamepad() && devices.state().driving_held == 0 && devices.state().analog.throttle_q16 == 0
               && devices.state().analog.steering_q16 == 0,
        "A fresh South start must consume its actual instance and neutralize held inputs before entering the scene");
    settings.device_id = *selected;
    settings.sensitivity = 3;
    devices.apply(settings);
    expect(devices.rumble_supported() && devices.rumble(1234, 5678) && first.calls.empty() && second.calls.back() == std::pair<Uint16, Uint16>{1234, 5678},
        "Saving the selected GUID must preserve the confirming instance for rumble, not choose its identical sibling");

    other.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    poll();
    expect(devices.state().analog.throttle_q16 == 0 && !devices.select_confirming_gamepad(), "An identical nonselected pad's pedal must neither drive nor steal selection");
    starter.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    starter.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
    starter.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    starter.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    poll();
    starter.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    starter.axis(SDL_GAMEPAD_AXIS_LEFTX, -24000);
    starter.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    poll();
    const auto state = devices.state();
    expect(state.analog.throttle_q16 > 0 && state.analog.throttle_q16 < 65536 && state.analog.steering_q16 < -32768 && state.analog.profile == 3
               && (state.driving_held & action_mask(Action::gear_up)),
        "The same confirming pad must supply fresh proportional pedals, steering and gear input after neutral rearming");
    TimedActions timeline;
    timeline.reset(0, true);
    timeline.synchronize(20, state.driving_held, state.analog);
    expect(!timeline.take(10).analog, "Pad adoption must not deliver new controls before their simulation deadline");
    const auto tick = timeline.take(40);
    expect(tick.analog == state.analog && tick.held == state.driving_held && (tick.pressed & action_mask(Action::gear_up)),
        "Adopted instance controls must reach the ordinary timestamped simulation path");
    devices.begin_poll();
    devices.handle_event(key(SDL_SCANCODE_KP_ENTER, true));
    expect(!devices.select_confirming_gamepad() && devices.state().analog.throttle_q16 == state.analog.throttle_q16,
        "A keyboard start must preserve an explicitly selected pad rather than replace it");
    devices.handle_event(key(SDL_SCANCODE_KP_ENTER, false));

    devices.set_focus(false);
    expect(second.calls.back() == std::pair<Uint16, Uint16>{0, 0} && devices.state().driving_held == 0,
        "Focus loss must stop the confirming instance's rumble and clear its driving controls");
    devices.set_focus(true);
    poll();
    expect(devices.state().driving_held == 0 && devices.state().analog.throttle_q16 == 0 && second.calls.back() == std::pair<Uint16, Uint16>{0, 0},
        "Focus return must preserve the instance but require neutral input and a fresh vibration command");
    starter.button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
    starter.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    starter.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    poll();
    starter.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    poll();
    expect(devices.state().analog.throttle_q16 > 0 && devices.rumble(2000, 3000) && first.calls.empty(),
        "After focus rearming, input and rumble must still belong to the original confirming instance");
    starter.detach();
    poll();
    devices.apply(settings);
    expect(devices.state().driving_held == 0 && devices.state().analog.throttle_q16 == 0 && !devices.rumble_supported() && !devices.rumble(100, 200) && first.calls.empty(),
        "Disconnecting the pinned pad must not transfer gameplay or rumble to its already connected identical sibling, even after settings apply");
    VirtualPad replacement(&reconnected, 0x5446);
    poll();
    expect(devices.state().driving_held == 0 && devices.rumble_supported() && devices.rumble(4000, 5000) && first.calls.empty()
               && reconnected.calls.back() == std::pair<Uint16, Uint16>{4000, 5000},
        "A new hotplug of the selected model may rearm the saved device without activating its older sibling");
    replacement.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    poll();
    expect(devices.state().analog.throttle_q16 > 0, "The reconnected instance must supply fresh selected-device controls");
    settings.device_id.clear();
    devices.apply(settings);
    expect(!devices.rumble_supported() && reconnected.calls.back() == std::pair<Uint16, Uint16>{0, 0},
        "Explicitly selecting Keyboard must stop the reconnected pad and clear its pin");
    settings.device_id = other.guid();
    devices.apply(settings);
    expect(devices.rumble(6000, 7000) && first.calls.back() == std::pair<Uint16, Uint16>{6000, 7000},
        "A later explicit model selection must use the ordinary first instance after the old pin was cleared");
}

} // namespace toca::test::input_devices
