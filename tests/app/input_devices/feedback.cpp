#include "fixtures.hpp"

#include <algorithm>
#include <limits>

namespace toca::test::input_devices {
struct RoutedHaptics {
    std::vector<SDL_HapticEffect> created;
    std::vector<int> run, stopped, destroyed, autocenter;
    bool closed = false;
};

class RoutedHapticSink final : public WheelHapticSink {
    std::shared_ptr<RoutedHaptics> trace_;

public:
    explicit RoutedHapticSink(std::shared_ptr<RoutedHaptics> trace) : trace_(std::move(trace)) {}

    ~RoutedHapticSink() override {
        trace_->closed = true;
    }

    Uint32 features() const override {
        return SDL_HAPTIC_CONSTANT | SDL_HAPTIC_SINE | SDL_HAPTIC_AUTOCENTER;
    }

    int axes() const override {
        return 1;
    }

    bool set_autocenter(int value) override {
        trace_->autocenter.push_back(value);
        return true;
    }

    int create(const SDL_HapticEffect& effect) override {
        trace_->created.push_back(effect);
        return int(trace_->created.size()) - 1;
    }

    bool update(int, const SDL_HapticEffect&) override {
        return true;
    }

    bool run(int id) override {
        trace_->run.push_back(id);
        return true;
    }

    void stop(int id) override {
        trace_->stopped.push_back(id);
    }

    void destroy(int id) override {
        trace_->destroyed.push_back(id);
    }
};

void routed_wheel_feedback_checks() {
    VirtualWheel first, second;
    auto first_trace = std::make_shared<RoutedHaptics>(), second_trace = std::make_shared<RoutedHaptics>();
    InputSettings settings;
    settings.device_id = first.guid();
    settings.gamepad = default_joystick_bindings();
    InputSettings player2 = settings;
    const WheelFeedbackCommand impact{WheelFeedbackCommand::Kind::constant, 100, 0, 1000, 50000, 0};
    const WheelFeedbackCommand rough{WheelFeedbackCommand::Kind::sine, 0, 0, 2000, std::numeric_limits<std::uint32_t>::max(), 100000};
    {
        InputDevices devices(settings, InputDevices::DeviceScope::virtual_only, [&](SDL_Joystick* joystick) {
            const auto id = SDL_GetJoystickID(joystick);
            expect(id == first.id || id == second.id, "Injected haptics must never open a physical or unrelated joystick");
            return std::make_unique<WheelHaptics>(std::make_unique<RoutedHapticSink>(id == first.id ? first_trace : second_trace));
        });
        devices.apply(settings, player2);
        devices.set_local_players(2);
        (void)pump(devices);
        expect(devices.wheel_feedback_supported(0) && devices.wheel_feedback_supported(1) && first_trace->autocenter.empty() && second_trace->autocenter.empty(),
            "Real selected-instance routing can query both fake device capabilities without starting any force");
        expect(devices.begin_wheel_feedback(0) && devices.begin_wheel_feedback(0) && devices.begin_wheel_feedback(1) && first_trace->autocenter == std::vector<int>{100}
                   && second_trace->autocenter == std::vector<int>{100},
            "Active driving enables each selected wheel's own original autocenter once, even without a contact effect");
        expect(devices.wheel_feedback(impact, 0) && devices.wheel_feedback(rough, 1) && first_trace->created.size() == 1 && first_trace->created[0].type == SDL_HAPTIC_CONSTANT
                   && second_trace->created.size() == 1 && second_trace->created[0].type == SDL_HAPTIC_SINE,
            "Player-one impacts and player-two road forces reach their distinct selected hardware sinks");
        expect(devices.wheel_feedback(rough, 0) && devices.wheel_feedback(impact, 1) && first_trace->created.size() == 2 && first_trace->created[1].type == SDL_HAPTIC_SINE
                   && second_trace->created.size() == 2 && second_trace->created[1].type == SDL_HAPTIC_CONSTANT,
            "Both players independently own constant and sine effects instead of sharing a global effect device");
        settings.rumble_enabled = false;
        devices.apply(settings, player2);
        const auto first_runs = first_trace->run.size();
        expect(first_trace->autocenter.back() == 0 && first_trace->stopped.size() >= 2 && !devices.begin_wheel_feedback(0) && !devices.wheel_feedback(impact, 0)
                   && first_trace->run.size() == first_runs && devices.begin_wheel_feedback(1) && devices.wheel_feedback(rough, 1),
            "Disabling one profile stops its effects/autocenter and rejects new force while the other profile remains usable");
        settings.rumble_enabled = true;
        devices.apply(settings, player2);
        expect(devices.begin_wheel_feedback(0) && devices.begin_wheel_feedback(1), "Reenabled profiles resume their own autocenter");
        devices.set_focus(false);
        expect(first_trace->autocenter.back() == 0 && second_trace->autocenter.back() == 0 && !devices.begin_wheel_feedback(0) && !devices.begin_wheel_feedback(1)
                   && !devices.wheel_feedback(impact, 0),
            "Focus loss stops both effect devices and autocenters immediately and prevents new force");
        devices.set_focus(true);
        expect(
            first_trace->autocenter.back() == 0 && second_trace->autocenter.back() == 0, "Focus return alone does not replay forces or turn on autocenter before active driving");
        expect(devices.begin_wheel_feedback(0) && devices.begin_wheel_feedback(1) && devices.wheel_feedback(impact, 0) && devices.wheel_feedback(rough, 1),
            "Fresh active driving restores both owned devices after focus return");
        const auto second_stops = second_trace->stopped.size();
        first.detach();
        (void)pump(devices);
        expect(first_trace->closed && first_trace->autocenter.back() == 0 && first_trace->destroyed.size() == 2 && !devices.begin_wheel_feedback(0)
                   && !devices.wheel_feedback(impact, 0) && second_trace->stopped.size() == second_stops && devices.begin_wheel_feedback(1) && devices.wheel_feedback(rough, 1),
            "Unplug releases only the disconnected owner's effects and never routes its output to an already-owned twin");
    }
    expect(second_trace->closed && second_trace->autocenter.back() == 0 && second_trace->destroyed.size() == 2,
        "InputDevices destruction disables autocenter and destroys the remaining wheel's owned constant and sine effects");
}

void rumble_checks() {
    RumbleTrace first, second;
    VirtualPad pad(&first, 0x5444), other(&second, 0x5445);
    InputSettings settings;
    {
        InputDevices devices(settings, InputDevices::DeviceScope::virtual_only);
        (void)pump(devices);
        expect(
            !devices.rumble_supported() && !devices.rumble(100, 200) && first.calls.empty() && second.calls.empty(), "Keyboard selection must never vibrate any connected gamepad");
        settings.device_id = pad.guid();
        devices.apply(settings);
        expect(devices.rumble_supported() && devices.rumble(1234, 5678) && first.calls.back() == std::pair<Uint16, Uint16>{1234, 5678} && second.calls.empty(),
            "Rumble must reach only the selected capable device through SDL's real virtual callback");
        devices.set_focus(false);
        expect(first.calls.back() == std::pair<Uint16, Uint16>{0, 0} && !devices.rumble(12, 34), "Focus loss must stop both motors and reject new commands");
        devices.set_focus(true);
        expect(first.calls.back() == std::pair<Uint16, Uint16>{0, 0}, "Focus regain must not replay the previous vibration");
        expect(devices.rumble(2000, 3000), "Fresh focused rumble must start");
        settings.rumble_enabled = false;
        devices.apply(settings);
        expect(first.calls.back() == std::pair<Uint16, Uint16>{0, 0} && !devices.rumble(100, 200), "Disabling vibration must immediately stop owned motors");
        settings.rumble_enabled = true;
        devices.apply(settings);
        expect(devices.rumble(4000, 5000), "Reenabled vibration must accept a new command");
        settings.device_id = other.guid();
        devices.apply(settings);
        expect(first.calls.back() == std::pair<Uint16, Uint16>{0, 0} && second.calls.empty(), "Changing selected device must stop the old pad without transferring its effect");
        expect(devices.rumble(6000, 7000, 10000), "A capable pad must accept a bounded-duration command");
        devices.stop_rumble();
        expect(second.calls.back() == std::pair<Uint16, Uint16>{0, 0}, "Explicit pause/scene stop must silence owned vibration");
        expect(devices.rumble(10000, 11000), "Effect before detach must start");
        other.detach();
        (void)pump(devices);
        expect(!devices.rumble_supported() && !devices.rumble(100, 200) && first.calls.back() == std::pair<Uint16, Uint16>{0, 0},
            "Unplug must discard the selected rumble owner without selecting another connected pad");
        settings.device_id = pad.guid();
        devices.apply(settings);
        expect(devices.rumble(12000, 13000), "Effect before destruction must start");
    }
    expect(first.calls.back() == std::pair<Uint16, Uint16>{0, 0}, "InputDevices destruction must stop its active effect before closing the gamepad");
}

} // namespace toca::test::input_devices
