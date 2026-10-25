#pragma once

#include "app/input.hpp"
#include "app/input_devices.hpp"
#include "app/wheel_feedback.hpp"
#include "app/wheel_haptics.hpp"
#include "simulation/controls.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

namespace toca::test::input_devices {
void expect(bool valid, const char* message);
SDL_Event key(SDL_Scancode code, bool down);
std::vector<PhysicalBinding> pump(InputDevices& devices);

struct RumbleTrace {
    std::vector<std::pair<Uint16, Uint16>> calls;
};

struct VirtualPad {
    SDL_JoystickID id = 0;
    SDL_Joystick* handle = nullptr;

    static bool SDLCALL rumble(void* userdata, Uint16 low, Uint16 high);
    explicit VirtualPad(RumbleTrace* trace = nullptr, Uint16 product = 0x5443);
    ~VirtualPad();
    VirtualPad(const VirtualPad&) = delete;
    VirtualPad& operator=(const VirtualPad&) = delete;
    void detach();
    void axis(SDL_GamepadAxis code, Sint16 value);
    void button(SDL_GamepadButton code, bool down);
    std::string guid() const;
};

struct VirtualWheel {
    SDL_JoystickID id = 0;
    SDL_Joystick* handle = nullptr;

    explicit VirtualWheel(Uint16 product = 0x5460);
    ~VirtualWheel();
    VirtualWheel(const VirtualWheel&) = delete;
    VirtualWheel& operator=(const VirtualWheel&) = delete;
    void detach();
    void axis(int code, Sint16 value);
    void button(int code, bool down);
    std::string guid() const;
};

void checks();
void progressive_steering_checks();
void confirming_pad_checks();
void local_two_player_checks();
void local_hotplug_rearming_checks();
void automatic_input_checks();
void automatic_local_ownership_checks();
void raw_wheel_checks();
void routed_wheel_feedback_checks();
void rumble_checks();
} // namespace toca::test::input_devices
