#include "app/input_devices.hpp"

#include "app/driving_input.hpp"
#include "app/wheel_feedback.hpp"
#include "app/wheel_haptics.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <utility>

namespace toca {
namespace {
constexpr std::int32_t one = 65536;

void validate(const InputSettings& settings) {
    if (settings.sensitivity < 1 || settings.sensitivity > 3 || settings.deadzone_percent > 30) {
        throw std::runtime_error("Input requires sensitivity1..3 and deadzone0..30 percent");
    }
    for (std::size_t action = 0; action < settings.gamepad.size(); ++action) {
        if (!valid_keyboard_binding(settings.keyboard[action], action) || !valid_gamepad_binding(settings.gamepad[action])) {
            throw std::runtime_error("Input contains an invalid physical binding");
        }
    }
}
} // namespace

struct InputDevices::Impl {
    struct Pad {
        std::unique_ptr<SDL_Gamepad, decltype(&SDL_CloseGamepad)> handle{nullptr, SDL_CloseGamepad};
        std::unique_ptr<SDL_Joystick, decltype(&SDL_CloseJoystick)> joystick{nullptr, SDL_CloseJoystick};
        // Haptic effects must be destroyed before their underlying device.
        std::unique_ptr<WheelHaptics> haptics;
        bool haptics_attempted = false;
        std::string guid, name;
        std::vector<Sint16> axes, initial_axes;
        std::vector<bool> armed, capture_ready, buttons, blocked_buttons;
        bool rumble_active = false;
    };

    InputSettings settings;
    InputSettings settings2 = default_player2_input();
    DeviceState state, state2;
    std::uint32_t local_players = 1;
    std::size_t configuration_player = 0;
    std::array<bool, SDL_SCANCODE_COUNT> keys{}, blocked_keys{};

    struct Subsystems {
        bool gamepad = false, haptic = false;

        ~Subsystems() {
            if (haptic) {
                SDL_QuitSubSystem(SDL_INIT_HAPTIC);
            }
            if (gamepad) {
                SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            }
        }
    } subsystems; // Declared before pads: devices close before subsystem shutdown.

    std::map<SDL_JoystickID, Pad> pads;
    std::optional<SDL_JoystickID> instance_pin, instance_pin2, confirming_pad;
    bool keyboard_confirmation = false;
    bool focused = true;
    DeviceScope scope;
    HapticsFactory haptics_factory;

    explicit Impl(const InputSettings& initial, DeviceScope requested_scope, HapticsFactory factory)
        : settings(initial), scope(requested_scope), haptics_factory(std::move(factory)) {
        validate(settings);
        subsystems.gamepad = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        if (!subsystems.gamepad) {
            throw std::runtime_error(std::string("Initialize gamepad input: ") + SDL_GetError());
        }
        // SDL haptic initialization enumerates OS devices; virtual fixtures need only the injected sink test.
        subsystems.haptic = scope == DeviceScope::all && !haptics_factory && SDL_InitSubSystem(SDL_INIT_HAPTIC);
        {
            int count = 0;
            std::unique_ptr<SDL_JoystickID, decltype(&SDL_free)> ids(SDL_GetJoysticks(&count), SDL_free);
            if (!ids) {
                throw std::runtime_error(std::string("Enumerate input devices: ") + SDL_GetError());
            }
            for (int i = 0; i < count; ++i) {
                add(ids.get()[i]);
            }
        }
        rebuild();
    }

    ~Impl() {
        while (!pads.empty()) {
            const auto found = pads.begin();
            stop(found->second);
            pads.erase(found);
        }
    }

    void add(SDL_JoystickID id) {
        if (scope == DeviceScope::virtual_only && !SDL_IsJoystickVirtual(id)) {
            return;
        }
        if (pads.contains(id)) {
            return;
        }
        const bool raw = !SDL_IsGamepad(id);
        Pad pad;
        if (raw) {
            pad.joystick.reset(SDL_OpenJoystick(id));
            if (!pad.joystick) {
                return;
            }
        } else {
            pad.handle.reset(SDL_OpenGamepad(id));
            if (!pad.handle) {
                return;
            }
        }
        std::array<char, 33> guid{};
        SDL_GUIDToString(raw ? SDL_GetJoystickGUID(pad.joystick.get()) : SDL_GetGamepadGUIDForID(id), guid.data(), int(guid.size()));
        pad.guid = (raw ? "joystick:" : "") + std::string(guid.data());
        const auto* name = raw ? SDL_GetJoystickName(pad.joystick.get()) : SDL_GetGamepadName(pad.handle.get());
        pad.name = name ? name : (raw ? "Joystick / Wheel" : "Gamepad");
        const auto axis_count = raw ? std::clamp(SDL_GetNumJoystickAxes(pad.joystick.get()), 0, 32) : int(SDL_GAMEPAD_AXIS_COUNT);
        const auto button_count = raw ? std::clamp(SDL_GetNumJoystickButtons(pad.joystick.get()), 0, 128) : int(SDL_GAMEPAD_BUTTON_COUNT);
        pad.axes.resize(std::size_t(axis_count));
        pad.initial_axes.resize(pad.axes.size());
        pad.armed.resize(pad.axes.size());
        pad.capture_ready.resize(pad.axes.size());
        pad.buttons.resize(std::size_t(button_count));
        pad.blocked_buttons.resize(pad.buttons.size());
        for (int axis = 0; axis < axis_count; ++axis) {
            const auto index = std::size_t(axis);
            pad.axes[index] = raw ? SDL_GetJoystickAxis(pad.joystick.get(), axis) : SDL_GetGamepadAxis(pad.handle.get(), SDL_GamepadAxis(axis));
            pad.initial_axes[index] = pad.axes[index];
            const auto neutral = raw ? raw_neutral(pad, index) : driving_input::axis_value(pad.axes[index], std::min(settings.deadzone_percent, settings2.deadzone_percent)) == 0;
            pad.armed[index] = neutral;
            pad.capture_ready[index] = neutral;
        }
        for (int button = 0; button < button_count; ++button) {
            pad.blocked_buttons[std::size_t(button)] = raw ? SDL_GetJoystickButton(pad.joystick.get(), button) : SDL_GetGamepadButton(pad.handle.get(), SDL_GamepadButton(button));
        }
        const auto added = pads.emplace(id, std::move(pad)).first;
        // A disconnected pin does not transfer to an already connected twin.
        // A newly hotplugged instance of the saved model may reconnect it.
        if (settings.device_id != automatic_input_device && instance_pin && !pads.contains(*instance_pin) && added->second.guid == settings.device_id && instance_pin2 != id) {
            instance_pin = id;
        } else if ((settings.device_id != automatic_input_device || local_players == 2) && instance_pin2 && !pads.contains(*instance_pin2)
                   && added->second.guid == settings2.device_id && instance_pin != id) {
            instance_pin2 = id;
        } else if (settings.device_id == automatic_input_device && !added->second.joystick && instance_pin && !pads.contains(*instance_pin) && instance_pin2 != id
                   && (local_players != 2 || selected(1) != &added->second)) {
            instance_pin = id;
        }
        assign_players();
        if (added->second.joystick) {
            for (std::size_t axis = 0; axis < added->second.axes.size(); ++axis) {
                added->second.armed[axis] = raw_neutral(added->second, axis);
                added->second.capture_ready[axis] = added->second.armed[axis];
            }
        }
    }

    Pad* matching(std::size_t player, const Pad* other = nullptr) {
        auto& pin = player == 0 ? instance_pin : instance_pin2;
        const auto& config = player == 0 ? settings : settings2;
        if (config.device_id.empty()) {
            return nullptr;
        }
        if (pin) {
            const auto found = pads.find(*pin);
            return found != pads.end() && (found->second.guid == config.device_id || (config.device_id == automatic_input_device && !found->second.joystick))
                           && &found->second != other
                       ? &found->second
                       : nullptr;
        }
        for (auto& [id, pad] : pads) {
            if ((pad.guid == config.device_id || (config.device_id == automatic_input_device && !pad.joystick)) && &pad != other
                && (player != 0 || local_players == 1 || instance_pin2 != id)) {
                return &pad;
            }
        }
        return nullptr;
    }

    Pad* selected(std::size_t player = 0) {
        if (settings.device_id == automatic_input_device && local_players == 2) {
            auto* second = matching(1);
            return player == 1 ? second : matching(0, second);
        }
        return matching(player, player == 1 ? selected(0) : nullptr);
    }

    void assign_players() {
        if (local_players != 2 && settings.device_id != automatic_input_device) {
            return;
        }
        // An explicit second-player pad has priority over player one's auto pad.
        const bool automatic = settings.device_id == automatic_input_device;
        const std::array<std::size_t, 2> order = automatic ? std::array<std::size_t, 2>{1, 0} : std::array<std::size_t, 2>{0, 1};
        for (const auto player : order) {
            if (player >= local_players) {
                continue;
            }
            auto& pin = player == 0 ? instance_pin : instance_pin2;
            if (automatic && player == 0 && local_players == 2 && pin == instance_pin2 && pin) {
                pin.reset();
            }
            if (!pin) {
                const auto* chosen = selected(player);
                for (const auto& [id, pad] : pads) {
                    if (&pad == chosen) {
                        pin = id;
                        break;
                    }
                }
            }
        }
    }

    bool connected(const Pad& pad) const {
        return pad.joystick ? SDL_JoystickConnected(pad.joystick.get()) : SDL_GamepadConnected(pad.handle.get());
    }

    bool raw_neutral(const Pad& pad, std::size_t axis) {
        bool configured = false;
        for (std::size_t player = 0; player < local_players; ++player) {
            if (selected(player) != &pad) {
                continue;
            }
            const auto* config = player == 0 ? &settings : &settings2;
            for (const auto& binding : config->gamepad) {
                if (binding.kind == BindingKind::joystick_axis && std::size_t(binding.code) == axis) {
                    configured = true;
                    if (driving_input::raw_axis_value(pad.axes[axis], binding, config->deadzone_percent) > 0) {
                        return false;
                    }
                }
            }
        }
        return configured || std::abs(std::int32_t(pad.axes[axis]) - pad.initial_axes[axis]) <= 2048;
    }

    WheelHaptics* haptics(Pad& pad) {
        if ((!subsystems.haptic && !haptics_factory) || !connected(pad)) {
            return nullptr;
        }
        if (!pad.haptics_attempted) {
            pad.haptics_attempted = true;
            auto* joystick = pad.joystick ? pad.joystick.get() : SDL_GetGamepadJoystick(pad.handle.get());
            pad.haptics = haptics_factory ? haptics_factory(joystick) : WheelHaptics::open(joystick);
            if (pad.haptics && !pad.haptics->supported()) {
                pad.haptics.reset();
            }
        }
        return pad.haptics.get();
    }

    WheelHaptics* feedback_haptics(std::size_t player) {
        if (player > 1) {
            throw std::out_of_range("Input player");
        }
        auto* pad = selected(player);
        const auto& config = player == 0 ? settings : settings2;
        if (!focused || !config.rumble_enabled || !pad) {
            if (pad) {
                stop(*pad);
            }
            return nullptr;
        }
        return haptics(*pad);
    }

    bool supports_rumble(const Pad& pad) const {
        return pad.handle && SDL_GamepadConnected(pad.handle.get())
               && SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad.handle.get()), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
    }

    void stop_motors(Pad& pad) {
        if (pad.rumble_active) {
            (void)SDL_RumbleGamepad(pad.handle.get(), 0, 0, 0);
            pad.rumble_active = false;
        }
    }

    void stop(Pad& pad) {
        stop_motors(pad);
        if (pad.haptics) {
            pad.haptics->stop();
        }
    }

    void stop_rumble() {
        for (auto& [id, pad] : pads) {
            (void)id;
            stop(pad);
        }
    }

    bool rumble(std::uint16_t low, std::uint16_t high, std::uint32_t duration_ms, std::size_t player) {
        auto* pad = selected(player);
        const auto& config = player == 0 ? settings : settings2;
        if (!focused || !config.rumble_enabled || !pad || !supports_rumble(*pad)) {
            if (pad) {
                stop_motors(*pad);
            }
            return false;
        }
        if ((low == 0 && high == 0) || duration_ms == 0) {
            stop_motors(*pad);
            return true;
        }
        const bool result = SDL_RumbleGamepad(pad->handle.get(), low, high, std::min(duration_ms, 250u));
        if (!result) {
            stop_motors(*pad);
            return false;
        }
        pad->rumble_active = true;
        return result;
    }

    std::int32_t axis(const Pad& pad, std::size_t index, std::uint32_t deadzone) const {
        return pad.armed[index] ? driving_input::axis_value(pad.axes[index], deadzone) : 0;
    }

    void rebuild() {
        assign_players();
        state = {};
        state2 = {};
        state2.analog.profile = settings2.sensitivity;
        state.analog.profile = settings.sensitivity;
        if (!focused) {
            return;
        }
        if (keys[SDL_SCANCODE_F5]) {
            state.driving_held |= action_mask(Action::camera_trackside);
        }
        if (local_players == 2 && keys[SDL_SCANCODE_F6]) {
            state2.driving_held |= action_mask(Action::camera_trackside);
        }
        const auto menu_key = [&](SDL_Scancode code, Action action) {
            if (keys[std::size_t(code)]) {
                state.menu_held |= action_mask(action);
            }
        };
        menu_key(SDL_SCANCODE_UP, Action::up);
        menu_key(SDL_SCANCODE_DOWN, Action::down);
        menu_key(SDL_SCANCODE_LEFT, Action::left);
        menu_key(SDL_SCANCODE_RIGHT, Action::right);
        menu_key(SDL_SCANCODE_RETURN, Action::confirm);
        menu_key(SDL_SCANCODE_KP_ENTER, Action::confirm);
        menu_key(SDL_SCANCODE_ESCAPE, Action::back);
        if (keys[SDL_SCANCODE_ESCAPE]) {
            state.pause_held = action_mask(Action::back);
        }
        menu_key(SDL_SCANCODE_BACKSPACE, Action::backspace);
        for (const auto& [id, pad] : pads) {
            (void)id;
            if (pad.joystick) {
                // Raw button0/1 provide menu confirm/back; driving remains fully rebindable.
                if (!pad.buttons.empty() && pad.buttons[0]) {
                    state.menu_held |= action_mask(Action::confirm);
                }
                if (pad.buttons.size() > 1 && pad.buttons[1]) {
                    state.menu_held |= action_mask(Action::back);
                }
                continue;
            }
            const auto button = [&](SDL_GamepadButton code, Action action) {
                if (pad.buttons[std::size_t(code)]) {
                    state.menu_held |= action_mask(action);
                }
            };
            button(SDL_GAMEPAD_BUTTON_DPAD_UP, Action::up);
            button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, Action::down);
            button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, Action::left);
            button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Action::right);
            button(SDL_GAMEPAD_BUTTON_SOUTH, Action::confirm);
            button(SDL_GAMEPAD_BUTTON_EAST, Action::back);
            button(SDL_GAMEPAD_BUTTON_START, Action::back);
            if (pad.buttons[SDL_GAMEPAD_BUTTON_START]) {
                state.pause_held |= action_mask(Action::back);
            }
            const auto x = axis(pad, SDL_GAMEPAD_AXIS_LEFTX, settings.deadzone_percent), y = axis(pad, SDL_GAMEPAD_AXIS_LEFTY, settings.deadzone_percent);
            if (x <= -one / 2) {
                state.menu_held |= action_mask(Action::left);
            }
            if (x >= one / 2) {
                state.menu_held |= action_mask(Action::right);
            }
            if (y <= -one / 2) {
                state.menu_held |= action_mask(Action::up);
            }
            if (y >= one / 2) {
                state.menu_held |= action_mask(Action::down);
            }
        }
        for (std::size_t player = 0; player < local_players; ++player) {
            auto& target = player == 0 ? state : state2;
            const auto& config = player == 0 ? settings : settings2;
            const auto* pad = selected(player);
            driving_input::Sample sample;
            sample.device = !pad ? driving_input::DeviceKind::none : pad->joystick ? driving_input::DeviceKind::joystick : driving_input::DeviceKind::gamepad;
            for (std::size_t i = 0; i < sample.bindings.size(); ++i) {
                sample.keys[i] = keys[std::size_t(config.keyboard[i].code)];
                const auto code = std::size_t(config.gamepad[i].code);
                if (pad && code < pad->buttons.size()) {
                    sample.bindings[i].button = pad->buttons[code];
                }
                if (pad && code < pad->axes.size()) {
                    sample.bindings[i].axis = pad->axes[code];
                    sample.bindings[i].armed = pad->armed[code];
                }
            }
            const auto resolved = driving_input::evaluate(config, sample);
            target.driving_held |= resolved.held;
            target.analog = resolved.analog;
        }
    }

    void focus(bool value) {
        if (!value) {
            confirming_pad.reset();
            keyboard_confirmation = false;
            stop_rumble();
            for (std::size_t i = 0; i < keys.size(); ++i) {
                blocked_keys[i] = blocked_keys[i] || keys[i];
                keys[i] = false;
            }
            for (auto& [id, pad] : pads) {
                (void)id;
                for (std::size_t i = 0; i < pad.axes.size(); ++i) {
                    pad.armed[i] =
                        pad.joystick ? raw_neutral(pad, i) : driving_input::axis_value(pad.axes[i], std::min(settings.deadzone_percent, settings2.deadzone_percent)) == 0;
                    pad.capture_ready[i] = pad.armed[i];
                }
                for (std::size_t i = 0; i < pad.buttons.size(); ++i) {
                    pad.blocked_buttons[i] = pad.blocked_buttons[i] || pad.buttons[i];
                    pad.buttons[i] = false;
                }
            }
        }
        focused = value;
        rebuild();
    }

    std::optional<PhysicalBinding> event(const SDL_Event& event) {
        std::optional<PhysicalBinding> captured;
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            const auto code = std::size_t(event.key.scancode);
            if (code < keys.size() && !event.key.repeat) {
                if (!event.key.down) {
                    keys[code] = blocked_keys[code] = false;
                } else if (!focused) {
                    blocked_keys[code] = true;
                } else if (!blocked_keys[code] && !keys[code]) {
                    keys[code] = true;
                    if (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER) {
                        // The host merges a whole poll into one frontend action.
                        // An explicit keyboard confirmation must not pick a pad
                        // whose South event happened earlier in that same poll.
                        keyboard_confirmation = true;
                        confirming_pad.reset();
                    }
                    captured = PhysicalBinding{BindingKind::key, std::int32_t(code)};
                }
            }
        } else if (event.type == SDL_EVENT_JOYSTICK_ADDED) {
            if (!SDL_IsGamepad(event.jdevice.which)) {
                add(event.jdevice.which);
            }
        } else if (event.type == SDL_EVENT_JOYSTICK_REMOVED) {
            const auto found = pads.find(event.jdevice.which);
            if (found != pads.end() && found->second.joystick) {
                if (confirming_pad == event.jdevice.which) {
                    confirming_pad.reset();
                }
                stop(found->second);
                pads.erase(found);
            }
        } else if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || event.type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
            const auto found = pads.find(event.jbutton.which);
            const auto code = std::size_t(event.jbutton.button);
            if (found != pads.end() && found->second.joystick && code < found->second.buttons.size()) {
                auto& pad = found->second;
                if (!event.jbutton.down) {
                    pad.buttons[code] = pad.blocked_buttons[code] = false;
                } else if (!focused) {
                    pad.blocked_buttons[code] = true;
                } else if (!pad.blocked_buttons[code] && !pad.buttons[code]) {
                    pad.buttons[code] = true;
                    if (code == 0 && !keyboard_confirmation && !confirming_pad) {
                        confirming_pad = event.jbutton.which;
                    }
                    if (&pad == selected(configuration_player)) {
                        captured = PhysicalBinding{BindingKind::joystick_button, std::int32_t(code)};
                    }
                }
            }
        } else if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
            const auto found = pads.find(event.jaxis.which);
            const auto code = std::size_t(event.jaxis.axis);
            if (found != pads.end() && found->second.joystick && code < found->second.axes.size()) {
                auto& pad = found->second;
                pad.axes[code] = event.jaxis.value;
                const auto delta = std::int32_t(event.jaxis.value) - pad.initial_axes[code];
                if (raw_neutral(pad, code)) {
                    pad.armed[code] = true;
                } else if (!focused) {
                    pad.armed[code] = false;
                }
                if (std::abs(delta) < 8192) {
                    pad.capture_ready[code] = true;
                }
                if (focused && pad.armed[code] && pad.capture_ready[code] && std::abs(delta) >= 16384) {
                    if (&pad == selected(configuration_player)) {
                        captured = PhysicalBinding{BindingKind::joystick_axis, std::int32_t(code), delta < 0 ? -1 : 1, -32768, pad.initial_axes[code], 32767};
                    }
                    pad.capture_ready[code] = false;
                }
            }
        } else if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMAPPED) {
            if (event.type == SDL_EVENT_GAMEPAD_REMAPPED) {
                const auto found = pads.find(event.gdevice.which);
                if (found != pads.end()) {
                    stop(found->second);
                    pads.erase(found);
                }
            }
            add(event.gdevice.which);
        } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            if (confirming_pad == event.gdevice.which) {
                confirming_pad.reset();
            }
            const auto found = pads.find(event.gdevice.which);
            if (found != pads.end()) {
                stop(found->second);
                pads.erase(found);
            }
        } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
            const auto found = pads.find(event.gbutton.which);
            const auto code = std::size_t(event.gbutton.button);
            if (found != pads.end() && found->second.handle && code < found->second.buttons.size()) {
                auto& pad = found->second;
                if (!event.gbutton.down) {
                    pad.buttons[code] = pad.blocked_buttons[code] = false;
                } else if (!focused) {
                    pad.blocked_buttons[code] = true;
                } else if (!pad.blocked_buttons[code] && !pad.buttons[code]) {
                    if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH && (state.menu_held & action_mask(Action::confirm)) == 0 && !keyboard_confirmation && !confirming_pad) {
                        confirming_pad = event.gbutton.which;
                    }
                    pad.buttons[code] = true;
                    if (&pad == selected(configuration_player)) {
                        captured = PhysicalBinding{BindingKind::button, std::int32_t(code)};
                    }
                }
            }
        } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            const auto found = pads.find(event.gaxis.which);
            const auto code = std::size_t(event.gaxis.axis);
            if (found != pads.end() && found->second.handle && code < found->second.axes.size()) {
                auto& pad = found->second;
                pad.axes[code] = event.gaxis.value;
                const auto value = driving_input::axis_value(event.gaxis.value, std::min(settings.deadzone_percent, settings2.deadzone_percent));
                if (value == 0) {
                    pad.armed[code] = true;
                }
                if (!focused && value != 0) {
                    pad.armed[code] = false;
                }
                if (std::abs(value) < one / 2) {
                    pad.capture_ready[code] = true;
                }
                if (focused && pad.armed[code] && pad.capture_ready[code] && std::abs(value) >= one / 2) {
                    if (&pad == selected(configuration_player)) {
                        captured = PhysicalBinding{BindingKind::axis, std::int32_t(code), value < 0 ? -1 : 1};
                    }
                    pad.capture_ready[code] = false;
                }
            }
        }
        rebuild();
        return captured;
    }
};

InputDevices::InputDevices(const InputSettings& settings, DeviceScope scope, HapticsFactory haptics_factory)
    : impl_(std::make_unique<Impl>(settings, scope, std::move(haptics_factory))) {}

InputDevices::~InputDevices() = default;

void InputDevices::apply(const InputSettings& settings) {
    apply(settings, impl_->settings2);
}

void InputDevices::apply(const InputSettings& settings, const InputSettings& player2) {
    validate(settings);
    validate(player2);
    const auto focused = impl_->focused;
    if (settings.device_id != impl_->settings.device_id) {
        impl_->instance_pin.reset();
    }
    if (player2.device_id != impl_->settings2.device_id) {
        impl_->instance_pin2.reset();
    }
    impl_->settings = settings;
    impl_->settings2 = player2;
    impl_->focus(false);
    impl_->focus(focused);
}

void InputDevices::set_local_players(std::uint32_t players) {
    if (players < 1 || players > 2) {
        throw std::runtime_error("Local input supports one or two players");
    }
    if (players != impl_->local_players) {
        const auto focused = impl_->focused;
        impl_->focus(false);
        impl_->local_players = players;
        impl_->focus(focused);
    }
}

void InputDevices::set_configuration_player(std::size_t player) {
    if (player > 1) {
        throw std::out_of_range("Input player");
    }
    impl_->configuration_player = player;
}

void InputDevices::set_focus(bool focused) {
    impl_->focus(focused);
}

void InputDevices::begin_poll() {
    impl_->confirming_pad.reset();
    impl_->keyboard_confirmation = false;
}

void InputDevices::begin_binding_capture() {
    auto* pad = impl_->selected(impl_->configuration_player);
    if (!pad || !pad->joystick) {
        return;
    }
    // Snapshot the actual rest position, including inverted/end-rest pedals.
    pad->initial_axes = pad->axes;
    std::fill(pad->armed.begin(), pad->armed.end(), true);
    std::fill(pad->capture_ready.begin(), pad->capture_ready.end(), true);
}

std::optional<std::string> InputDevices::select_confirming_gamepad(std::size_t player) {
    if (player > 1) {
        throw std::out_of_range("Input player");
    }
    const auto candidate = impl_->confirming_pad;
    impl_->confirming_pad.reset();
    if (!candidate || !impl_->focused) {
        return std::nullopt;
    }
    const auto found = impl_->pads.find(*candidate);
    if (found == impl_->pads.end() || !impl_->connected(found->second)) {
        return std::nullopt;
    }
    const auto guid = found->second.guid;
    if (impl_->local_players == 2) {
        const auto* other = impl_->selected(1 - player);
        if (other == &found->second) {
            return std::nullopt;
        }
    }
    if (found->second.joystick && (player == 0 ? impl_->settings : impl_->settings2).device_id != guid) {
        return std::nullopt;
    }
    const auto selection = (player == 0 ? impl_->settings : impl_->settings2).device_id == automatic_input_device ? std::string(automatic_input_device) : guid;
    const auto focused = impl_->focused;
    impl_->focus(false);
    (player == 0 ? impl_->settings : impl_->settings2).device_id = selection;
    (player == 0 ? impl_->instance_pin : impl_->instance_pin2) = candidate;
    auto& other = player == 0 ? impl_->instance_pin2 : impl_->instance_pin;
    if (other == candidate) {
        other.reset();
    }
    impl_->focus(focused);
    return selection;
}

const DeviceState& InputDevices::state(std::size_t player) const {
    if (player > 1) {
        throw std::out_of_range("Input player");
    }
    return player == 0 ? impl_->state : impl_->state2;
}

std::vector<InputDevice> InputDevices::devices() const {
    std::vector<InputDevice> result{{"", "Keyboard"}};
    for (const auto& [id, pad] : impl_->pads) {
        (void)id;
        if (std::none_of(result.begin(), result.end(), [&](const InputDevice& device) {
                return device.id == pad.guid;
            })) {
            result.push_back({pad.guid, pad.name, pad.joystick != nullptr});
        }
    }
    return result;
}

std::optional<InputDevice> InputDevices::selected_device(std::size_t player) const {
    if (player > 1) {
        throw std::out_of_range("Input player");
    }
    const auto* pad = impl_->selected(player);
    return pad ? std::optional{InputDevice{pad->guid, pad->name, pad->joystick != nullptr}} : std::nullopt;
}

std::optional<PhysicalBinding> InputDevices::handle_event(const SDL_Event& event) {
    return impl_->event(event);
}

bool InputDevices::rumble_supported(std::size_t player) const {
    if (player > 1) {
        throw std::out_of_range("Input player");
    }
    const auto* pad = impl_->selected(player);
    return pad && impl_->supports_rumble(*pad);
}

bool InputDevices::rumble(std::uint16_t low, std::uint16_t high, std::uint32_t duration_ms, std::size_t player) {
    if (player > 1) {
        throw std::out_of_range("Input player");
    }
    return impl_->rumble(low, high, duration_ms, player);
}

bool InputDevices::wheel_feedback_supported(std::size_t player) const {
    if (player > 1) {
        throw std::out_of_range("Input player");
    }
    auto* pad = impl_->selected(player);
    return pad && impl_->haptics(*pad);
}

bool InputDevices::begin_wheel_feedback(std::size_t player) {
    auto* haptics = impl_->feedback_haptics(player);
    if (!haptics) {
        return false;
    }
    haptics->activate();
    return true;
}

bool InputDevices::wheel_feedback(const WheelFeedbackCommand& command, std::size_t player) {
    auto* haptics = impl_->feedback_haptics(player);
    return haptics && haptics->apply(command);
}

void InputDevices::stop_rumble() {
    impl_->stop_rumble();
}
} // namespace toca
