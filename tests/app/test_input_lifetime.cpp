#include "app/input_devices.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>

namespace allocation_failure {
// Only this thread's ordinary C++ allocations during one constructor attempt.
// The selected failure disarms itself before exception cleanup allocates.
thread_local bool armed = false, fired = false;
thread_local std::size_t remaining = 0;

struct Scope {
    explicit Scope(std::size_t fail_at) {
        remaining = fail_at;
        fired = false;
        armed = true;
    }

    ~Scope() {
        armed = false;
    }

    void disarm() {
        armed = false;
    }
};
} // namespace allocation_failure

void* operator new(std::size_t size) {
    if (allocation_failure::armed) {
        if (allocation_failure::remaining == 0) {
            allocation_failure::armed = false;
            allocation_failure::fired = true;
            throw std::bad_alloc();
        }
        --allocation_failure::remaining;
    }
    if (auto* result = std::malloc(size ? size : 1)) {
        return result;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
    }
}

struct VirtualDevice {
    SDL_JoystickID id;

    explicit VirtualDevice(bool raw) {
        SDL_VirtualJoystickDesc descriptor;
        SDL_INIT_INTERFACE(&descriptor);
        descriptor.type = raw ? SDL_JOYSTICK_TYPE_WHEEL : SDL_JOYSTICK_TYPE_GAMEPAD;
        descriptor.vendor_id = 0x1209;
        descriptor.product_id = raw ? 0x5460 : 0x5443;
        descriptor.name = raw ? "TOCA lifetime raw wheel fixture" : "TOCA lifetime standardized gamepad fixture";
        descriptor.naxes = raw ? 8 : SDL_GAMEPAD_AXIS_COUNT;
        descriptor.nbuttons = raw ? 32 : SDL_GAMEPAD_BUTTON_COUNT;
        if (!raw) {
            descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
            descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        }
        id = SDL_AttachVirtualJoystick(&descriptor);
        expect(id != 0, "Attach virtual lifetime device");
    }

    ~VirtualDevice() {
        SDL_DetachVirtualJoystick(id);
    }

    void configure(bool raw) const {
        std::unique_ptr<SDL_Joystick, decltype(&SDL_CloseJoystick)> handle(SDL_OpenJoystick(id), SDL_CloseJoystick);
        expect(bool(handle), "Open virtual lifetime fixture for neutral setup");
        expect(SDL_SetJoystickVirtualAxis(handle.get(), raw ? 1 : SDL_GAMEPAD_AXIS_LEFT_TRIGGER, raw ? 32767 : -32768), "Release first virtual pedal");
        expect(SDL_SetJoystickVirtualAxis(handle.get(), raw ? 2 : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, raw ? 32767 : -32768), "Release second virtual pedal");
        SDL_UpdateJoysticks();
        // No fixture-owned reference survives into constructor attempts.
    }
};

void checks() {
    VirtualDevice pad(false), wheel(true);
    pad.configure(false);
    wheel.configure(true);
    const auto released = [&] {
        expect(SDL_GetGamepadFromID(pad.id) == nullptr && SDL_GetJoystickFromID(pad.id) == nullptr && SDL_GetJoystickFromID(wheel.id) == nullptr,
            "InputDevices construction/destruction must release every gamepad and raw joystick handle");
        expect(SDL_WasInit(SDL_INIT_GAMEPAD) == 0 && SDL_WasInit(SDL_INIT_JOYSTICK) != 0, "InputDevices must balance GAMEPAD without releasing the caller's JOYSTICK subsystem");
        expect(SDL_IsJoystickVirtual(pad.id) && SDL_IsJoystickVirtual(wheel.id), "Lifetime checks must retain attached fixtures instead of hiding leaks by unplugging them");
    };
    released();
    toca::InputSettings settings;
    unsigned failures = 0;
    bool succeeded = false;
    for (std::size_t fail_at = 0; fail_at <= 128; ++fail_at) {
        try {
            allocation_failure::Scope failure(fail_at);
            toca::InputDevices devices(settings, toca::InputDevices::DeviceScope::virtual_only);
            failure.disarm();
            expect(!allocation_failure::fired && SDL_GetGamepadFromID(pad.id) && SDL_GetJoystickFromID(wheel.id), "A completed constructor must own both virtual device kinds");
            expect(devices.devices().size() == 3, "A completed constructor must rebuild keyboard, gamepad and raw-wheel choices");
            succeeded = true;
        } catch (const std::bad_alloc&) {
            expect(allocation_failure::fired, "Only the scoped one-shot allocation failure may interrupt this test");
            ++failures;
        }
        released();
        if (succeeded) {
            break;
        }
    }
    expect(succeeded && failures > 1, "Real InputDevices construction must recover after failures at multiple allocation stages");
    {
        toca::InputDevices retry(settings, toca::InputDevices::DeviceScope::virtual_only);
        expect(retry.devices().size() == 3, "A later unarmed retry must reopen both still-attached virtual devices");
    }
    released();
    std::cout << "PASS: " << failures << " real constructor allocation failures, device cleanup, subsystem balance and retry\n";
}
} // namespace

int main() {
    try {
        expect(SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT, "0x1209/0x5443,0x1209/0x5460", SDL_HINT_OVERRIDE), "Isolate virtual lifetime fixtures");
        expect(SDL_Init(SDL_INIT_JOYSTICK), "Initialize only the caller-owned joystick subsystem");
        checks();
        SDL_Quit();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
