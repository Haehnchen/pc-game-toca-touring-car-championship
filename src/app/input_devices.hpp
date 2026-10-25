#pragma once

#include "core/contracts.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_joystick.h>

#include <functional>
#include <memory>
#include <vector>

namespace toca {
struct WheelFeedbackCommand;
class WheelHaptics;

struct DeviceState {
    std::uint32_t menu_held = 0, driving_held = 0, pause_held = 0;
    AnalogControls analog;
};

// SDL is confined to the device boundary. Event timestamps stay with the host;
// its TimedActions queue decides which simulation deadline consumes each value.
class InputDevices {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    enum class DeviceScope {
        all,
        virtual_only
    };
    using HapticsFactory = std::function<std::unique_ptr<WheelHaptics>(SDL_Joystick*)>;
    explicit InputDevices(const InputSettings& settings, DeviceScope scope = DeviceScope::all, HapticsFactory haptics_factory = {});
    ~InputDevices();
    InputDevices(const InputDevices&) = delete;
    InputDevices& operator=(const InputDevices&) = delete;
    void apply(const InputSettings& settings);
    void apply(const InputSettings& settings, const InputSettings& player2);
    void set_local_players(std::uint32_t players);
    void set_configuration_player(std::size_t player);
    void set_focus(bool focused);
    // Track only this poll's fresh fixed-menu confirmation source. Navigation,
    // axes and binding capture do not select a driving device.
    void begin_poll();
    // Call with the wheel centered and pedals released before requesting a binding.
    void begin_binding_capture();
    // Call only when frontend confirmation actually enters scene loading.
    // Consumes the candidate and pins its connected instance; auto mode retains
    // its keyboard/pad setting. Clear held input/rumble like apply().
    std::optional<std::string> select_confirming_gamepad(std::size_t player = 0);
    const DeviceState& state(std::size_t player = 0) const;
    // Keyboard first (empty id), then connected GUID models. Identical models
    // share one preference id; scene confirmation can pin a specific instance.
    std::vector<InputDevice> devices() const;
    std::optional<InputDevice> selected_device(std::size_t player = 0) const;
    std::optional<PhysicalBinding> handle_event(const SDL_Event& event);
    // Native dual-motor gamepad vibration, not steering-wheel force feedback.
    // Each command replaces the previous one and expires within 250ms.
    bool rumble_supported(std::size_t player = 0) const;
    bool rumble(std::uint16_t low, std::uint16_t high, std::uint32_t duration_ms = 80, std::size_t player = 0);
    // Directed SDL Haptic effects on the selected joystick (including mapped wheels).
    bool wheel_feedback_supported(std::size_t player = 0) const;
    // Start the original device autocenter for active driving, including effect-free asphalt.
    bool begin_wheel_feedback(std::size_t player = 0);
    bool wheel_feedback(const WheelFeedbackCommand&, std::size_t player = 0);
    // Stop both gamepad vibration and wheel effects.
    void stop_rumble();
};
} // namespace toca
