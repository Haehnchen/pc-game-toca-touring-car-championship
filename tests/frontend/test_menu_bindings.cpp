#include "frontend/font.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "menu_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace {
using namespace toca::frontend::test;

void controller_bindings() {
    using namespace toca;
    using namespace toca::frontend;
    Controller controller;
    Settings effective;
    controller.set_settings(effective, {{640, 480}});
    controller.set_input_devices({{"", "Keyboard"}, {"pad-a", "TEST GAMEPAD"}});
    const MenuInput press{controller};
    const auto apply = [&] {
        const auto request = controller.take_settings_request();
        require(request.has_value(), "Completed controller changes must request persistence");
        effective = *request;
        controller.settings_applied(effective);
    };
    controller.update({}, {0, 5'000'000'000});
    controller.frontend_ready();
    press(Action::confirm);
    press(Action::down);
    press(Action::confirm);
    press(Action::down);
    press(Action::down);
    press(Action::confirm);
    require(controller.screen() == Screen::controller_players && controller.item_available(0) && controller.item_available(1) && controller.item_available(2)
                && !controller.item_available(3),
        "Controller Setup must contain two local profiles and Back");
    press(Action::up);
    require(controller.selection() == 2 && press(Action::down) == std::vector{Cue::navigation} && controller.selection() == 0,
        "Controller Setup must wrap directly between Back and player one");
    press(Action::confirm);
    press(Action::down);
    press(Action::confirm);
    require(controller.screen() == Screen::controller_keyboard_bindings && controller.binding_capture_index() == 0,
        "Selected Keyboard configuration must enter a fresh complete nine-action capture");
    require(press(Action::confirm).empty() && controller.binding_capture_index() == 0, "Menu confirmation must not become a binding without a fresh physical key");
    require(press.capture({BindingKind::key, 80}) == std::vector{Cue::unavailable} && controller.binding_capture_error(), "A steering arrow must not be assigned to acceleration");
    require(press.capture({BindingKind::key, 58}) == std::vector{Cue::unavailable}, "Reserved function keys must be rejected");
    require(press.capture({BindingKind::key, 4}, false).empty() && controller.binding_capture_index() == 0, "Unfocused capture must not alter staged bindings");
    require(press.capture({BindingKind::key, 4}) == std::vector{Cue::confirm} && controller.binding_capture_index() == 1 && !controller.take_settings_request(),
        "Each accepted key must advance once with the original confirmation cue and remain staged");
    require(controller.binding_capture_feedback() && controller.selection() == 0, "Accepted input feedback must retain the just-accepted action");
    require(press.wait(1'000'000'000, false).empty() && controller.binding_capture_feedback() && controller.selection() == 0,
        "Focus loss must pause accepted-input feedback without advancing its action");
    require(press.wait(233'333'333).empty() && controller.binding_capture_feedback() && controller.selection() == 0,
        "Accepted-input feedback must remain visible until the seven-update duration expires");
    require(press.wait(1).empty() && !controller.binding_capture_feedback() && controller.selection() == 1 && controller.binding_capture_index() == 1,
        "Elapsed feedback must advance the prompt without replaying the confirmation cue or binding");
    require(press.capture({BindingKind::key, 4}) == std::vector{Cue::unavailable} && controller.binding_capture_index() == 1,
        "Duplicate keys must preserve the current capture action");
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::controller_options && controller.selection() == 1
                && controller.settings_values() == effective && !controller.take_settings_request(),
        "Escape must discard the complete partial binding transaction and return to the sole Configure row");
    press(Action::confirm);
    for (std::int32_t index = 0; index < 9; ++index) {
        press.capture({BindingKind::key, 4 + index});
    }
    require(controller.binding_capture_index() == 9 && controller.binding_capture_feedback() && controller.selection() == 8 && controller.settings_values() == effective
                && !controller.take_settings_request(),
        "The ninth accepted action must show its feedback before the complete map is installed");
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::controller_options && !controller.binding_capture_feedback()
                && controller.settings_values() == effective && !controller.take_settings_request(),
        "Escape during the final feedback must discard the staged complete map");
    press(Action::confirm);
    for (std::int32_t index = 0; index < 9; ++index) {
        require(press.capture({BindingKind::key, 4 + index}) == std::vector{Cue::confirm}, "All nine unique keys must accept the original confirmation cue");
        require(controller.binding_capture_index() == std::size_t(index + 1) && controller.binding_capture_feedback() && controller.selection() == std::size_t(index),
            "A fresh candidate must shorten prior feedback and be accepted exactly once for the next action");
        require(!controller.take_settings_request() && controller.settings_values() == effective, "Staged capture must never reach runtime or persistence before final feedback");
    }
    require(press.wait(1'000'000'000, false).empty() && controller.screen() == Screen::controller_keyboard_bindings && !controller.take_settings_request(),
        "Focus loss must also pause the final feedback and atomic commit");
    require(press.wait(233'333'333).empty() && controller.screen() == Screen::controller_keyboard_bindings && !controller.take_settings_request(),
        "The final binding must remain staged throughout its feedback duration");
    require(press.wait(1).empty() && !controller.binding_capture_feedback() && controller.screen() == Screen::controller_options && controller.selection() == 1,
        "Expired final feedback must atomically complete capture and return to Configure without an extra cue");
    const auto request = controller.take_settings_request();
    require(request && request->input.keyboard[0].code == 4 && request->input.keyboard[8].code == 12, "Complete capture must commit all nine bindings together");
    effective = *request;
    controller.settings_applied(effective, "Disk full");
    press(Action::back);
    require(controller.screen() == Screen::controller_players && controller.take_settings_request() == std::optional{effective},
        "Leaving player controls must retry failed persistence with the completed binding set");
    controller.settings_applied(effective);
    press(Action::confirm);
    require(press(Action::left).empty() && !controller.take_settings_request(), "Device Left must stop at the first available device");
    press(Action::right);
    apply();
    press(Action::right);
    apply();
    require(effective.input.device_id == automatic_input_device && press(Action::right).empty() && press(Action::confirm).empty() && !controller.take_settings_request(),
        "Player one's appended Auto choice must be the final bounded device option");
    press(Action::left);
    apply();
    require(effective.input.device_id == "pad-a" && controller.item_available(1), "Device selection must preserve the live device's stable ID");
    press(Action::down);
    press(Action::confirm);
    require(controller.screen() == Screen::controller_gamepad_bindings, "The same Configure row must follow the selected gamepad device");
    require(press.capture({BindingKind::button, 6}) == std::vector{Cue::unavailable} && controller.binding_capture_index() == 0,
        "The fixed Start pause button must remain reserved during gamepad capture");
    require(press.capture({BindingKind::axis, 0, -1}) == std::vector{Cue::confirm}, "Negative axis direction must be capturable");
    require(press.capture({BindingKind::axis, 0, 1}) == std::vector{Cue::confirm}, "Opposite halves of the same axis must remain distinct bindings");
    controller.set_input_devices({{"", "Keyboard"}});
    require(controller.screen() == Screen::controller_options && controller.selection() == 1 && controller.settings_values() == effective && !controller.take_settings_request()
                && !controller.item_available(1),
        "Unplugging a captured device must discard partial bindings while retaining its saved device identity");
    require(press(Action::confirm) == std::vector{Cue::unavailable}, "An absent selected pad must not open configuration");
    controller.set_input_devices({{"pad-a", "RECONNECTED GAMEPAD"}});
    require(controller.item_available(1) && controller.input_device_name() == "RECONNECTED GAMEPAD", "Reconnection must restore the selected stable device");
    press(Action::confirm);
    for (const auto button : std::array<std::int32_t, 9>{0, 1, 2, 3, 4, 5, 7, 8, 11}) {
        require(press.capture({BindingKind::button, button}) == std::vector{Cue::confirm}, "Gamepad buttons including menu button positions must remain capturable");
    }
    require(controller.binding_capture_index() == 9 && controller.binding_capture_feedback() && !controller.take_settings_request(),
        "Gamepad capture must show feedback for its ninth binding before commit");
    require(press.capture({BindingKind::button, 12}).empty() && controller.screen() == Screen::controller_options && !controller.binding_capture_feedback(),
        "A fresh candidate after the final binding must finish feedback without an extra binding or confirmation cue");
    apply();
    press(Action::down);
    press(Action::right);
    apply();
    require(effective.input.sensitivity == 3, "Sensitivity must retain the original three-level choice in row two");
    press(Action::down);
    require(press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == Screen::controller_native_options,
        "The original external panel position must lead to explicitly native gamepad settings");
    for (unsigned step = effective.input.deadzone_percent; step < 30; ++step) {
        press(Action::right);
        apply();
    }
    require(press(Action::right).empty() && !controller.take_settings_request(), "Native deadzone must stop at its declared30-percent bound");
    press(Action::down);
    press(Action::down);
    require(press(Action::confirm) == std::vector{Cue::option}, "Vibration must toggle with the normal option cue");
    apply();
    require(!effective.input.rumble_enabled, "The vibration switch must persist independently of selected device");
    press(Action::up);
    press(Action::confirm);
    apply();
    require(effective.input == InputSettings{}, "Reset Defaults must restore all nine keyboard/gamepad bindings, sensitivity and vibration together");
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::controller_options && controller.selection() == 3,
        "Native settings Back must restore its original parent row");
    press(Action::back);
    press(Action::down);
    require(press(Action::confirm) == std::vector{Cue::confirm} && controller.controller_player() == 1 && !controller.take_settings_request(),
        "The preceding selector must open player two without saving unrelated settings");
    press(Action::right);
    apply();
    require(effective.input == InputSettings{} && effective.input_player2.device_id == "pad-a", "Selecting player two's device must preserve player one's configuration");
    press(Action::left);
    apply();
    press(Action::down);
    press(Action::confirm);
    for (const auto& binding : default_player2_input().keyboard) {
        press.capture(binding);
    }
    press.wait(233'333'334);
    apply();
    require(effective.input == InputSettings{} && effective.input_player2.keyboard == default_player2_input().keyboard,
        "The complete player-two capture must save only player two's map");
    press(Action::down);
    press(Action::right);
    apply();
    require(effective.input.sensitivity == 2 && effective.input_player2.sensitivity == 3, "Original sensitivity must also remain independently editable for Keyboard");
    press(Action::right);
    apply();
    require(effective.input_player2.sensitivity == 1, "Original sensitivity Right must wrap High to Low");
    press(Action::left);
    apply();
    require(effective.input_player2.sensitivity == 3, "Original sensitivity Left must wrap Low to High");
    press(Action::confirm);
    apply();
    require(effective.input_player2.sensitivity == 1, "Original sensitivity Enter must advance like Right");
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::controller_players && controller.selection() == 1,
        "Player two Back must restore the player-two selector row");
}

void wheel_controller_bindings() {
    using namespace toca;
    using namespace toca::frontend;
    Controller controller;
    Settings effective;
    const std::string wheel_id = "joystick:0123456789abcdef0123456789abcdef";
    controller.set_settings(effective, {{640, 480}});
    controller.set_input_devices({{"", "Keyboard"}, {wheel_id, "TEST WHEEL", true}, {"pad-a", "TEST GAMEPAD"}});
    const MenuInput press{controller};
    const auto apply = [&] {
        const auto request = controller.take_settings_request();
        require(request.has_value(), "Wheel options must request the complete saved profile");
        effective = *request;
        controller.settings_applied(effective);
    };
    controller.update({}, {0, 5'000'000'000});
    controller.frontend_ready();
    press(Action::confirm);
    press(Action::down);
    press(Action::confirm);
    press(Action::down);
    press(Action::down);
    press(Action::confirm);
    press(Action::confirm);
    press(Action::right);
    apply();
    require(effective.input.device_id == wheel_id && effective.input.gamepad == default_joystick_bindings(),
        "Choosing a raw wheel must install raw pedal and steering bindings rather than standardized gamepad axes");
    press(Action::down);
    press(Action::confirm);
    auto bindings = default_joystick_bindings();
    bindings[0] = {BindingKind::joystick_axis, 7, -1, -32768, 32000, 32767};
    require(press.capture(bindings[0]) == std::vector{Cue::confirm}, "Raw pedals must enter the existing atomic capture transaction with their released calibration");
    auto duplicate = bindings[0];
    duplicate.calibration_center = 31000;
    require(press.capture(duplicate) == std::vector{Cue::unavailable} && controller.binding_capture_index() == 1,
        "Changing calibration must not allow the same physical wheel direction to bind two actions");
    for (std::size_t index = 1; index < bindings.size(); ++index) {
        require(press.capture(bindings[index]) == std::vector{Cue::confirm}, "All raw wheel axes and buttons must be accepted by the ordinary Configure flow");
    }
    controller.update({}, {0, 233'333'334});
    apply();
    require(
        effective.input.gamepad == bindings && controller.screen() == Screen::controller_options, "Completing wheel capture must save all calibrated axes and buttons together");
    press(Action::up);
    press(Action::left);
    apply();
    press(Action::right);
    apply();
    require(effective.input.gamepad == bindings, "Temporarily choosing Keyboard must preserve the saved wheel calibration");
    press(Action::right);
    apply();
    require(effective.input.device_id == "pad-a" && effective.input.gamepad == InputSettings{}.gamepad,
        "Switching to a standardized gamepad must replace incompatible raw wheel bindings");
    press(Action::left);
    apply();
    for (unsigned row = 0; row < 3; ++row) {
        press(Action::down);
    }
    press(Action::confirm);
    press(Action::down);
    press(Action::down);
    press(Action::confirm);
    apply();
    require(
        !effective.input.rumble_enabled && effective.input.device_id == wheel_id, "The wheel feedback switch must persist without changing the selected wheel or its input map");
}

void automatic_controller_bindings() {
    using namespace toca;
    using namespace toca::frontend;
    Controller controller;
    Settings effective;
    effective.input.keyboard[0] = {BindingKind::key, 26};
    effective.input.gamepad[4] = {BindingKind::button, 0};
    const auto original_keyboard = effective.input.keyboard;
    const auto original_gamepad = effective.input.gamepad;
    const std::vector<InputDevice> devices{{"", "Keyboard"}, {"pad-a", "TEST GAMEPAD"}};
    const InputDevice pad{"pad-a", "TEST GAMEPAD"};
    controller.set_settings(effective, {{640, 480}});
    controller.set_input_devices(devices);
    const MenuInput press{controller};
    const auto apply = [&] {
        const auto request = controller.take_settings_request();
        require(request.has_value(), "Automatic controller changes must request persistence");
        effective = *request;
        controller.settings_applied(effective);
    };
    controller.update({}, {0, 5'000'000'000});
    controller.frontend_ready();
    press(Action::confirm);
    press(Action::down);
    press(Action::confirm);
    press(Action::down);
    press(Action::down);
    press(Action::confirm);
    press(Action::confirm);
    require(controller.input_device_choices() == std::vector<std::string>{"", "pad-a", "auto"}, "Auto must append to player one's existing device choices");
    press(Action::right);
    apply();
    press(Action::right);
    apply();
    require(effective.input.device_id == automatic_input_device && effective.input.keyboard == original_keyboard && effective.input.gamepad == original_gamepad,
        "Opting into Auto must preserve both configured input maps");
    require(press(Action::right).empty() && press(Action::confirm).empty() && !controller.take_settings_request(), "Auto must retain bounded device navigation");
    press(Action::down);
    press(Action::confirm);
    require(controller.screen() == Screen::controller_auto_configure && controller.selection() == 0 && controller.item_available(0) && !controller.item_available(1),
        "Auto Configure must separate keyboard and unavailable gamepad configuration without changing the main page");
    press(Action::down);
    require(
        press(Action::confirm) == std::vector{Cue::unavailable} && !controller.take_settings_request(), "Auto must not configure a pad until the host assigns an eligible device");
    controller.set_input_devices(devices, pad);
    require(controller.item_available(1) && controller.controller_gamepad_available(), "An eligible automatic pad must enable the gamepad wizard");
    press(Action::confirm);
    press.capture({BindingKind::button, 0});
    controller.set_input_devices(devices); // The connected model can be reserved by player two.
    require(controller.screen() == Screen::controller_auto_configure && controller.selection() == 1 && !controller.binding_capture_feedback()
                && controller.settings_values() == effective && !controller.take_settings_request(),
        "Losing the eligible Auto pad must cancel staged gamepad capture even when its model remains connected");
    controller.set_input_devices(devices, pad);
    press(Action::confirm);
    for (const auto button : std::array<std::int32_t, 9>{0, 1, 2, 3, 4, 5, 7, 8, 11}) {
        press.capture({BindingKind::button, button});
    }
    controller.update({}, {0, 233'333'334});
    require(controller.screen() == Screen::controller_auto_configure && controller.selection() == 1, "Completed Auto gamepad capture must return to the gamepad chooser row");
    apply();
    const auto rebound_gamepad = effective.input.gamepad;
    require(effective.input.device_id == automatic_input_device && effective.input.keyboard == original_keyboard && rebound_gamepad != original_gamepad,
        "Auto gamepad capture must preserve Auto selection and the configured keyboard map");
    press(Action::up);
    press(Action::confirm);
    press.capture({BindingKind::key, 4});
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::controller_auto_configure && controller.selection() == 0
                && controller.settings_values() == effective && !controller.take_settings_request(),
        "Cancelled Auto keyboard capture must preserve both maps and return to its chooser row");
    press(Action::confirm);
    for (std::int32_t index = 0; index < 9; ++index) {
        press.capture({BindingKind::key, 4 + index});
    }
    controller.update({}, {0, 233'333'334});
    require(controller.screen() == Screen::controller_auto_configure && controller.selection() == 0, "Completed Auto keyboard capture must return to the keyboard chooser row");
    apply();
    require(effective.input.device_id == automatic_input_device && effective.input.keyboard[0].code == 4 && effective.input.gamepad == rebound_gamepad,
        "Auto keyboard capture must preserve Auto selection and the rebound gamepad map");
    controller.settings_applied(effective, "Disk full");
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::controller_options && controller.selection() == 1
                && controller.take_settings_request() == effective,
        "Auto chooser Back must restore Configure and retry failed persistence");
    controller.settings_applied(effective);
    press(Action::back);
    press(Action::down);
    press(Action::confirm);
    require(
        controller.controller_player() == 1 && controller.input_device_choices() == std::vector<std::string>{"", "pad-a"}, "Player two must retain explicit devices and omit Auto");
    press(Action::right);
    apply();
    require(press(Action::right).empty() && !controller.take_settings_request() && effective.input_player2.device_id == "pad-a",
        "Player two must stop at its explicit gamepad rather than entering Auto");
    press(Action::back);
    press(Action::up);
    press(Action::confirm);
    controller.set_input_devices(devices, pad); // A distinct identical instance may still be eligible for Auto.
    require(controller.controller_gamepad_available(), "Auto availability must use the assigned instance and allow two identical gamepad models");
    press(Action::down);
    press(Action::confirm);
    press(Action::confirm);
    controller.set_input_devices({{"", "Keyboard"}});
    require(controller.screen() == Screen::controller_keyboard_bindings, "Auto keyboard capture must remain usable while every gamepad is disconnected");
    press(Action::back);
    require(controller.screen() == Screen::controller_auto_configure && !controller.item_available(1) && controller.item_available(0),
        "Auto must keep keyboard configuration available after gamepad unplugging");
}
} // namespace

int main() {
    return toca::frontend::test::run([] {
        controller_bindings();
        wheel_controller_bindings();
        automatic_controller_bindings();
    });
}
