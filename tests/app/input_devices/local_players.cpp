#include "fixtures.hpp"

#include <algorithm>
#include <limits>

namespace toca::test::input_devices {
void local_two_player_checks() {
    InputSettings first;
    auto second = default_player2_input();
    InputDevices devices(first, InputDevices::DeviceScope::virtual_only);
    devices.apply(first, second);
    devices.set_local_players(2);
    (void)pump(devices);
    devices.handle_event(key(SDL_SCANCODE_F5, true));
    expect(devices.state(0).driving_held == action_mask(Action::camera_trackside) && devices.state(1).driving_held == 0,
        "Player one's fixed F5 trackside shortcut must affect only player one");
    devices.handle_event(key(SDL_SCANCODE_F5, false));
    devices.handle_event(key(SDL_SCANCODE_F6, true));
    expect(devices.state(1).driving_held == action_mask(Action::camera_trackside) && devices.state(0).driving_held == 0,
        "Player two's fixed F6 trackside shortcut must affect only player two");
    devices.set_focus(false);
    devices.set_focus(true);
    devices.handle_event(key(SDL_SCANCODE_F6, true));
    expect(devices.state(1).driving_held == 0, "A held F6 shortcut must require release after focus return");
    devices.handle_event(key(SDL_SCANCODE_F6, false));
    devices.handle_event(key(SDL_SCANCODE_F6, true));
    expect(devices.state(1).driving_held == action_mask(Action::camera_trackside), "Fresh F6 must rearm player two's trackside shortcut after focus return");
    devices.handle_event(key(SDL_SCANCODE_F6, false));
    devices.handle_event(key(SDL_SCANCODE_UP, true));
    expect(devices.state(0).driving_held == action_mask(Action::up) && devices.state(1).driving_held == 0, "Player one's arrow keys must not drive player two");
    devices.handle_event(key(SDL_SCANCODE_W, true));
    devices.handle_event(key(SDL_SCANCODE_D, true));
    expect(devices.state(0).driving_held == action_mask(Action::up) && devices.state(1).driving_held == (action_mask(Action::up) | action_mask(Action::right)),
        "Shared keyboard must independently deliver both players' simultaneous controls");
    devices.set_focus(false);
    devices.set_focus(true);
    expect(devices.state(0).driving_held == 0 && devices.state(1).driving_held == 0, "Focus loss must clear both local input streams");
    devices.handle_event(key(SDL_SCANCODE_UP, false));
    devices.handle_event(key(SDL_SCANCODE_W, false));
    devices.handle_event(key(SDL_SCANCODE_D, false));
    RumbleTrace motors1, motors2, replacement_motors;
    VirtualPad pad1(&motors1, 0x5450), pad2(&motors2, 0x5450);
    (void)pump(devices);
    second.device_id = pad2.guid();
    devices.apply(first, second);
    devices.handle_event(key(SDL_SCANCODE_UP, true));
    pad1.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    (void)pump(devices);
    expect(devices.state(0).analog.throttle_q16 == 65536 && devices.state(1).analog.throttle_q16 > 0,
        "Keyboard plus gamepad must remain independent even when the pad is assigned to player two");
    devices.handle_event(key(SDL_SCANCODE_UP, false));
    pad1.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    (void)pump(devices);
    first.device_id = pad1.guid();
    devices.apply(first, second);
    // Player two already owns pad1. Player one must take the other concrete
    // instance even though both devices have the same saved model GUID.
    pad2.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    (void)pump(devices);
    expect(devices.state(0).analog.throttle_q16 > 65000 && devices.state(1).analog.throttle_q16 == 0, "Two identical saved gamepad models must resolve to distinct live instances");
    pad1.axis(SDL_GAMEPAD_AXIS_LEFTX, -24000);
    (void)pump(devices);
    expect(devices.state(1).analog.steering_q16 < 0 && devices.state(0).analog.steering_q16 == 0, "A second gamepad's steering must affect only its assigned car");
    expect(devices.rumble(1111, 2222, 80, 0) && devices.rumble(3333, 4444, 80, 1) && motors2.calls.back() == std::pair<Uint16, Uint16>{1111, 2222}
               && motors1.calls.back() == std::pair<Uint16, Uint16>{3333, 4444},
        "Each local player's feedback must reach that player's assigned gamepad");
    pad1.detach();
    (void)pump(devices);
    expect(devices.state(1).driving_held == 0 && devices.state(1).analog.steering_q16 == 0 && devices.state(0).analog.throttle_q16 > 65000,
        "Unplugging one local pad must release only its car without transferring ownership");
    VirtualPad replacement(&replacement_motors, 0x5450);
    (void)pump(devices);
    replacement.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    (void)pump(devices);
    expect(devices.state(1).analog.throttle_q16 > 0 && devices.state(0).analog.throttle_q16 > 65000 && devices.rumble(5555, 6666, 80, 1)
               && replacement_motors.calls.back() == std::pair<Uint16, Uint16>{5555, 6666},
        "A newly connected twin must rearm the disconnected player without stealing the active pad");
    devices.set_focus(false);
    devices.set_focus(true);
    expect(devices.state(0).driving_held == 0 && devices.state(1).driving_held == 0 && motors2.calls.back() == std::pair<Uint16, Uint16>{0, 0}
               && replacement_motors.calls.back() == std::pair<Uint16, Uint16>{0, 0},
        "Both gamepad controls and both feedback motors must clear together at focus boundaries");
    devices.set_local_players(1);
    devices.handle_event(key(SDL_SCANCODE_F6, true));
    devices.handle_event(key(SDL_SCANCODE_W, true));
    expect(devices.state(1).driving_held == 0 && devices.state(1).analog.throttle_q16 == 0, "Returning to one player must stop the inactive input stream");
}

void local_hotplug_rearming_checks() {
    InputSettings first;
    first.deadzone_percent = 20;
    auto second = default_player2_input();
    InputDevices devices(first, InputDevices::DeviceScope::virtual_only);
    devices.apply(first, second);
    devices.set_local_players(2);
    (void)pump(devices);
    VirtualPad held(nullptr, 0x5451);
    held.axis(SDL_GAMEPAD_AXIS_LEFTX, 4000);
    SDL_UpdateJoysticks();
    second.device_id = held.guid();
    devices.apply(first, second);
    (void)pump(devices);
    expect(devices.state(1).analog.steering_q16 == 0, "Hotplug must not arm player two's held axis merely because it is inside player one's larger deadzone");
    held.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    (void)pump(devices);
    held.axis(SDL_GAMEPAD_AXIS_LEFTX, 4000);
    (void)pump(devices);
    expect(devices.state(1).analog.steering_q16 > 0 && devices.state(0).analog.steering_q16 == 0,
        "Neutral return must rearm the connected player's own deadzone and preserve independent input");
}

void automatic_input_checks() {
    InputSettings settings;
    settings.device_id = automatic_input_device;
    const std::array<SDL_Scancode, 9> codes{
        SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_E, SDL_SCANCODE_Q, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_H};
    for (std::size_t i = 0; i < codes.size(); ++i) {
        settings.keyboard[i] = {BindingKind::key, codes[i]};
    }
    const std::array<SDL_GamepadButton, 5> buttons{
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_WEST};
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        settings.gamepad[i + 4] = {BindingKind::button, buttons[i]};
    }
    InputDevices devices(settings, InputDevices::DeviceScope::virtual_only);
    (void)pump(devices);
    devices.handle_event(key(SDL_SCANCODE_W, true));
    expect(devices.state().analog.throttle_q16 == 65536 && !devices.state().analog.throttle_analog && !devices.selected_device() && !devices.rumble(1, 2),
        "Auto without a connected pad must retain the configured keyboard and digital pedals");
    RumbleTrace motors;
    VirtualPad pad(&motors, 0x5452);
    (void)pump(devices);
    expect(devices.selected_device() && devices.selected_device()->id == pad.guid(), "Auto must detect and select a hotplugged eligible pad without a menu change");
    devices.begin_poll();
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 24000);
    pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
    devices.handle_event(key(SDL_SCANCODE_A, true));
    (void)pump(devices);
    auto state = devices.state();
    expect(state.analog.throttle_q16 == 65536 && !state.analog.throttle_analog && state.analog.brake_q16 > 0 && state.analog.brake_analog && state.analog.steering_q16 == -65536
               && !state.analog.steering_analog,
        "Same-poll keyboard and pad input must merge pedals by maximum while bound keyboard steering keeps the original digital response");
    TimedActions timeline;
    timeline.reset(0, true);
    timeline.synchronize(20, state.driving_held, state.analog);
    const auto tick = timeline.take(40);
    expect(tick.held == state.driving_held && tick.analog == state.analog && (tick.held & action_mask(Action::left)) && !(tick.held & action_mask(Action::right)),
        "The normal timestamped path must deliver the merged keyboard steering and both independent pedals together");
    devices.handle_event(key(SDL_SCANCODE_W, false));
    devices.handle_event(key(SDL_SCANCODE_A, false));
    state = devices.state();
    expect(state.analog.throttle_q16 > 0 && state.analog.throttle_q16 < 65536 && state.analog.throttle_analog && state.analog.steering_q16 > 30000
               && state.analog.steering_q16 < 36000 && state.analog.steering_analog,
        "Releasing the keyboard must restore the still-held pad's proportional pedals and progressive steering immediately");
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    pad.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    pad.axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    (void)pump(devices);
    devices.handle_event(key(SDL_SCANCODE_S, true));
    state = devices.state();
    expect(state.driving_held == action_mask(Action::down) && state.analog.brake_q16 == 65536 && state.analog.throttle_q16 == 0,
        "An idle automatic pad must preserve the configured keyboard's held brake without adding a throttle press");
    timeline.synchronize(100, state.driving_held, state.analog);
    const auto reverse_tick = timeline.take(120);
    expect(reverse_tick.held == action_mask(Action::down) && reverse_tick.analog == state.analog,
        "The normal timestamped input path must preserve digital reverse gas with keyboard brake and an idle pad");
    simulation::Controls reverse(0);
    const simulation::ControlsInput reverse_input{.brake = bool(reverse_tick.held & action_mask(Action::down)), .analog = reverse_tick.analog};
    reverse.update(reverse_input, {});
    reverse.update(reverse_input, {});
    expect(reverse.state().gear == 7 && reverse.state().reverse_pedal && reverse.state().throttle_q16 > 0 && reverse.state().brake_q16 == 0,
        "Actual automatic controls must select reverse and produce reverse gas from keyboard brake with an idle automatic pad");
    devices.handle_event(key(SDL_SCANCODE_S, false));
    devices.handle_event(key(SDL_SCANCODE_W, true));
    state = devices.state();
    timeline.synchronize(140, state.driving_held, state.analog);
    const auto forward_tick = timeline.take(160);
    const simulation::ControlsInput forward_input{.throttle = bool(forward_tick.held & action_mask(Action::up)), .analog = forward_tick.analog};
    reverse.update(forward_input, {});
    reverse.update(forward_input, {});
    expect(reverse.state().gear == 1 && !reverse.state().reverse_pedal && reverse.state().throttle_q16 > 0,
        "Releasing keyboard brake and pressing gas with an idle automatic pad must restore actual forward controls");
    devices.handle_event(key(SDL_SCANCODE_W, false));
    devices.handle_event(key(SDL_SCANCODE_D, true));
    expect(devices.state().analog.steering_q16 == 65536 && !devices.state().analog.steering_analog, "An idle connected pad must not turn keyboard steering into analogue steering");
    devices.handle_event(key(SDL_SCANCODE_D, false));
    const std::array<Action, 5> actions{Action::gear_up, Action::gear_down, Action::camera_rear, Action::camera_next, Action::horn};
    for (std::size_t i = 0; i < actions.size(); ++i) {
        devices.handle_event(key(codes[i + 4], true));
        pad.button(buttons[i], true);
        (void)pump(devices);
        expect(devices.state().driving_held == action_mask(actions[i]) && devices.state().pause_held == 0,
            "Auto must OR rebound keyboard gears, cameras and horn with their pad bindings without adding pause");
        devices.handle_event(key(codes[i + 4], false));
        expect(devices.state().driving_held == action_mask(actions[i]), "Releasing either source must preserve the other source's held action");
        pad.button(buttons[i], false);
        (void)pump(devices);
        expect(devices.state().driving_held == 0, "Merged actions must release only when both physical sources release");
    }
    devices.handle_event(key(SDL_SCANCODE_H, true));
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 24000);
    (void)pump(devices);
    expect(devices.rumble(1000, 2000), "Auto feedback must reach its concrete selected pad");
    devices.set_focus(false);
    devices.set_focus(true);
    devices.handle_event(key(SDL_SCANCODE_H, true));
    (void)pump(devices);
    expect(devices.state().driving_held == 0 && devices.state().analog.steering_q16 == 0 && motors.calls.back() == std::pair<Uint16, Uint16>{0, 0},
        "Focus boundaries must stop auto feedback and require both held keyboard and pad inputs to rearm");
    devices.handle_event(key(SDL_SCANCODE_H, false));
    devices.handle_event(key(SDL_SCANCODE_H, true));
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, 0);
    (void)pump(devices);
    pad.axis(SDL_GAMEPAD_AXIS_LEFTX, -24000);
    (void)pump(devices);
    expect((devices.state().driving_held & action_mask(Action::horn)) && devices.state().analog.steering_q16 < 0,
        "Fresh keyboard and neutral-rearmed pad input must resume together in auto");
    pad.detach();
    (void)pump(devices);
    expect(devices.state().driving_held == action_mask(Action::horn) && !devices.selected_device() && !devices.rumble(1, 2),
        "Unplugging the automatic pad must preserve already-held keyboard controls and discard pad feedback");
}

void automatic_local_ownership_checks() {
    RumbleTrace first_motors, second_motors, confirming_motors, replacement_motors;
    VirtualPad first(&first_motors, 0x5453);
    InputSettings settings;
    settings.device_id = automatic_input_device;
    auto player2 = default_player2_input();
    player2.device_id = first.guid();
    InputDevices devices(settings, InputDevices::DeviceScope::virtual_only);
    devices.apply(settings, player2);
    devices.set_local_players(2);
    (void)pump(devices);
    devices.handle_event(key(SDL_SCANCODE_UP, true));
    first.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    (void)pump(devices);
    expect(!devices.selected_device(0) && devices.selected_device(1) && devices.state(0).analog.throttle_q16 == 65536 && devices.state(1).analog.throttle_q16 > 0
               && !devices.rumble(1, 2, 80, 0) && devices.rumble(3, 4, 80, 1),
        "A lone pad reserved for player two must leave player one's auto on keyboard alone");
    devices.handle_event(key(SDL_SCANCODE_UP, false));
    VirtualPad second(&second_motors, 0x5453);
    (void)pump(devices);
    second.axis(SDL_GAMEPAD_AXIS_LEFTX, -24000);
    const auto captured = pump(devices);
    expect(devices.state(0).analog.steering_q16 < 0 && devices.state(1).analog.steering_q16 == 0
               && std::find(captured.begin(), captured.end(), PhysicalBinding{BindingKind::axis, SDL_GAMEPAD_AXIS_LEFTX, -1}) != captured.end() && devices.rumble(11, 12, 80, 0)
               && second_motors.calls.back() == std::pair<Uint16, Uint16>{11, 12} && first_motors.calls.back() == std::pair<Uint16, Uint16>{3, 4},
        "An identical-model twin must give auto its own input, capture and feedback instance without touching player two");
    devices.begin_poll();
    first.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    (void)pump(devices);
    expect(!devices.select_confirming_gamepad(0), "Auto race confirmation must never steal player two's reserved pad");
    first.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    (void)pump(devices);
    VirtualPad starter(&confirming_motors, 0x5453);
    (void)pump(devices);
    devices.begin_poll();
    starter.button(SDL_GAMEPAD_BUTTON_SOUTH, true);
    (void)pump(devices);
    expect(devices.select_confirming_gamepad() == std::optional{std::string(automatic_input_device)},
        "An eligible race-confirming pad must pin its instance while retaining auto mode");
    devices.apply(settings, player2);
    expect(devices.rumble(21, 22, 80, 0) && confirming_motors.calls.back() == std::pair<Uint16, Uint16>{21, 22} && second_motors.calls.back() == std::pair<Uint16, Uint16>{0, 0},
        "Applying the unchanged auto setting must preserve the race-confirming live instance for feedback");
    starter.button(SDL_GAMEPAD_BUTTON_SOUTH, false);
    starter.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    devices.handle_event(key(SDL_SCANCODE_UP, true));
    (void)pump(devices);
    expect(devices.state().analog.throttle_q16 == 65536, "The race-confirming auto pin must leave the configured keyboard active");
    starter.detach();
    (void)pump(devices);
    expect(!devices.selected_device(0) && devices.state().analog.throttle_q16 == 65536 && !devices.rumble(1, 2, 80, 0) && devices.state(1).analog.throttle_q16 == 0,
        "Disconnecting a pinned auto pad must preserve keyboard and player-two ownership without adopting an already-held twin");
    VirtualPad replacement(&replacement_motors, 0x5454);
    (void)pump(devices);
    expect(devices.selected_device(0) && devices.selected_device(0)->id == replacement.guid() && devices.rumble(31, 32, 80, 0)
               && replacement_motors.calls.back() == std::pair<Uint16, Uint16>{31, 32},
        "A newly hotplugged eligible model must reconnect auto independently of the disconnected pad's GUID");
    first.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    (void)pump(devices);
    first.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    (void)pump(devices);
    expect(devices.state(1).analog.throttle_q16 > 0 && devices.rumble(41, 42, 80, 1) && first_motors.calls.back() == std::pair<Uint16, Uint16>{41, 42},
        "Player two must retain its own rearmed input and motor while player one's automatic pad reconnects");
    devices.set_focus(false);
    expect(devices.state(0).driving_held == 0 && devices.state(1).driving_held == 0 && replacement_motors.calls.back() == std::pair<Uint16, Uint16>{0, 0}
               && first_motors.calls.back() == std::pair<Uint16, Uint16>{0, 0},
        "Focus loss must clear both auto and explicit streams and stop both owned motor callbacks");
    devices.set_focus(true);
    devices.set_local_players(1);
    first.detach();
    replacement.detach();
    (void)pump(devices);
    RumbleTrace solo_motors;
    VirtualPad solo(&solo_motors, 0x5453);
    (void)pump(devices);
    solo.axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
    solo.axis(SDL_GAMEPAD_AXIS_LEFTX, 24000);
    const auto solo_capture = pump(devices);
    expect(devices.selected_device(0) && devices.selected_device(0)->id == solo.guid() && devices.state(0).analog.throttle_q16 > 0 && devices.state(0).analog.steering_q16 > 0
               && devices.state(1).driving_held == 0 && devices.state(1).analog.throttle_q16 == 0
               && std::find(solo_capture.begin(), solo_capture.end(), PhysicalBinding{BindingKind::axis, SDL_GAMEPAD_AXIS_LEFTX, 1}) != solo_capture.end()
               && devices.rumble(51, 52, 80, 0) && solo_motors.calls.back() == std::pair<Uint16, Uint16>{51, 52},
        "After returning to one player, inactive player two's stale model reservation must not consume auto's hotplugged input, capture or motors");
}

} // namespace toca::test::input_devices
