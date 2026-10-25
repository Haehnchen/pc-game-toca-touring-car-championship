#include "app/input.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace toca;
        const auto confirm = action_mask(Action::confirm);
        const auto left = action_mask(Action::left);
        InputState input;
        input.begin_poll(100);
        input.key_pressed();
        require(input.snapshot().any_key_pressed, "Unbound keyboard presses must reach the title input condition");
        input.text_input("da");
        input.text_input("niel");
        require(input.snapshot().text_input == "daniel", "Text events in one poll must retain their order and ownership");
        input.set_action(Action::confirm, true);
        input.set_action(Action::confirm, true);
        require(input.snapshot().held == confirm && input.snapshot().pressed == confirm, "Key repeat must not add a new edge");
        input.set_action(Action::confirm, false);
        require(input.snapshot().held == 0 && input.snapshot().pressed == confirm && input.snapshot().released == confirm, "Quick taps must preserve both edges");
        input.begin_poll(200);
        require(input.snapshot().timestamp_ns == 200 && input.snapshot().pressed == 0 && input.snapshot().released == 0 && !input.snapshot().any_key_pressed
                    && input.snapshot().text_input.empty(),
            "A new poll consumes previous edges");
        input.set_action(Action::left, true);
        input.begin_poll(300);
        input.set_action(Action::left, true);
        require(input.snapshot().held == left && input.snapshot().pressed == 0, "Held keys must survive polling without repeated presses");
        input.text_input("pending");
        input.set_focus(false);
        require(input.snapshot().held == 0 && input.snapshot().released == left && !input.snapshot().focused && input.snapshot().text_input.empty(),
            "Focus loss must release held actions and pending text");
        input.set_action(Action::confirm, true);
        input.key_pressed();
        input.text_input("ignored");
        require(input.snapshot().held == 0 && !input.snapshot().any_key_pressed && input.snapshot().text_input.empty(), "Inactive windows must not acquire input");
        input.set_focus(true);
        InputState refocused;
        refocused.begin_poll(300);
        refocused.set_action(Action::confirm, true);
        refocused.key_pressed();
        refocused.set_focus(false);
        refocused.set_focus(true);
        require(refocused.snapshot().focused && refocused.snapshot().held == 0 && refocused.snapshot().pressed == 0 && refocused.snapshot().released == confirm
                    && !refocused.snapshot().any_key_pressed,
            "Focus loss/return in one poll must cancel pending confirmation/title presses while retaining releases");
        refocused.set_action(Action::confirm, true);
        refocused.key_pressed();
        require(refocused.snapshot().held == confirm && refocused.snapshot().pressed == confirm && refocused.snapshot().any_key_pressed,
            "A fresh confirmation after focus return must still be accepted within the same poll");
        input.set_pointer(320, 240, true);
        input.request_quit();
        require(input.snapshot().focused && input.snapshot().pointer_inside && input.snapshot().pointer_x == 320 && input.snapshot().pointer_y == 240,
            "Mapped pointer coordinates must enter the shared snapshot");
        require(input.snapshot().quit_requested, "Quit must be delivered in the shared snapshot");
        TimedActions timed;
        timed.reset(0, true);
        timed.set_action(50, Action::up, true);
        timed.set_action(60, Action::gear_up, true);
        timed.set_action(70, Action::gear_up, false);
        require(timed.take(40).held == 0, "New host input must not affect earlier catch-up ticks");
        const auto tick = timed.take(80);
        require(tick.held == action_mask(Action::up) && (tick.pressed & action_mask(Action::gear_up)) && (tick.released & action_mask(Action::gear_up)),
            "A gear tap between deadlines must retain its edge once");
        require(timed.take(120).held == action_mask(Action::up) && timed.take(120).pressed == 0, "Presentation polls must not replay consumed gear edges");
        timed.set_focus(130, false);
        timed.set_action(135, Action::up, true);
        timed.set_focus(140, true);
        require(timed.take(160).held == 0, "Focus loss must clear held gameplay actions and ignore inactive presses");
        timed.set_action(150, Action::down, true);
        require(timed.take(200).held == action_mask(Action::down), "Late events must apply to the next deadline without rewriting earlier input");
        timed.reset(1000, true);
        require(timed.take(1040).held == 0, "A fresh scene/resume must not retain prior pending actions");
        const AnalogControls half{.steering_q16 = -32768, .throttle_q16 = 32768, .profile = 3, .steering_analog = true, .throttle_analog = true};
        input.begin_poll(400);
        input.set_physical_binding({BindingKind::key, 26});
        input.set_physical_binding({BindingKind::key, 4});
        input.set_analog(half);
        require(input.snapshot().physical_binding->code == 26, "Capture consumes the first fresh physical event in a poll");
        input.begin_poll(500);
        require(!input.snapshot().physical_binding && input.snapshot().analog == half, "Physical capture edges reset, while analog held values persist between polls");
        timed.set_analog(1100, half);
        require(!timed.take(1080).analog && timed.take(1120).analog == half && timed.take(1160).analog == half,
            "Analog events must affect their own deadline, persist and never rewrite earlier catch-up ticks");
        timed.set_focus(1170, false);
        timed.set_analog(1180, half);
        timed.set_focus(1190, true);
        require(!timed.take(1200).analog, "Focus loss must neutralize analog state and reject inactive axis events");
        input.set_focus(false);
        input.set_physical_binding({BindingKind::key, 26});
        input.set_analog(half);
        require(!input.snapshot().analog && !input.snapshot().physical_binding, "Inactive physical/analog events must not rearm the input state");
        timed.reset(2000, true);
        timed.synchronize(2010, action_mask(Action::up), half);
        require(timed.take(2040).held == action_mask(Action::up) && timed.take(2040).analog == half, "Complete device synchronization must deliver both held actions and axes");
        timed.synchronize(2050, action_mask(Action::up), half);
        require(timed.take(2080).pressed == 0, "Repeated identical synchronization must not create another press");
        timed.reset(3000, true);
        timed.synchronize(3010, action_mask(Action::up), half);
        const auto restarted = timed.take(3040);
        require(restarted.held == action_mask(Action::up) && restarted.pressed == action_mask(Action::up) && restarted.analog == half,
            "A queue reset must redeliver unchanged physical held/analog values without an outer cache");
        timed.set_action(3050, Action::gear_up, true);
        timed.set_action(3060, Action::gear_up, false);
        timed.synchronize(3070, action_mask(Action::up), half);
        const auto tap = timed.take(3080);
        require((tap.pressed & action_mask(Action::gear_up)) && (tap.released & action_mask(Action::gear_up)),
            "Direct diagnostic/tap APIs and synchronization must share delivery state without losing an edge");
        timed.synchronize(3090, 0, std::nullopt);
        const auto cleared = timed.take(3120);
        require(cleared.held == 0 && (cleared.released & action_mask(Action::up)) && !cleared.analog,
            "Physical clear without a session reset must queue actual releases and neutral analog input");
        timed.set_focus(3130, false);
        timed.set_action(3140, Action::up, true);
        timed.set_analog(3140, half);
        timed.set_focus(3150, true);
        timed.synchronize(3160, action_mask(Action::up), half);
        const auto resumed = timed.take(3200);
        require(resumed.held == action_mask(Action::up) && resumed.pressed == action_mask(Action::up) && resumed.analog == half,
            "Ignored inactive events must not contaminate the queued delivery cache after focus return");
        const auto extras = action_mask(Action::camera_rear) | action_mask(Action::camera_next) | action_mask(Action::horn);
        timed.synchronize(3210, extras, std::nullopt);
        const auto extra_press = timed.take(3240);
        require(extra_press.held == extras && (extra_press.pressed & extras) == extras,
            "Device synchronization must preserve configurable camera and held horn actions at their deadline");
        timed.set_focus(3250, false);
        const auto extra_release = timed.take(3280);
        require(extra_release.held == 0 && (extra_release.released & extras) == extras, "Focus loss must release both cameras and horn instead of retaining an active sound");
        std::cout << "Input edges, focus and snapshot ownership passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
