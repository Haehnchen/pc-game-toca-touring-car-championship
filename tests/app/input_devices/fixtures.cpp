#include "fixtures.hpp"

#include <stdexcept>

namespace toca::test::input_devices {
namespace {
void detach_joystick(SDL_Joystick*& handle, SDL_JoystickID& id) {
    if (handle) {
        SDL_CloseJoystick(handle);
        handle = nullptr;
    }
    if (id) {
        SDL_DetachVirtualJoystick(id);
        id = 0;
    }
}

std::string joystick_guid(SDL_Joystick* handle) {
    char text[33];
    SDL_GUIDToString(SDL_GetJoystickGUID(handle), text, int(sizeof(text)));
    return text;
}
} // namespace

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(std::string(message) + ": " + SDL_GetError());
    }
}

SDL_Event key(SDL_Scancode code, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = code;
    event.key.down = down;
    return event;
}

std::vector<PhysicalBinding> pump(InputDevices& devices) {
    SDL_UpdateJoysticks();
    std::vector<PhysicalBinding> captured;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (const auto binding = devices.handle_event(event)) {
            captured.push_back(*binding);
        }
    }
    return captured;
}

bool SDLCALL VirtualPad::rumble(void* userdata, Uint16 low, Uint16 high) {
    static_cast<RumbleTrace*>(userdata)->calls.emplace_back(low, high);
    return true;
}

VirtualPad::VirtualPad(RumbleTrace* trace, Uint16 product) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = 0x1209;
    desc.product_id = product;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "TOCA Controller Fixture";
    desc.userdata = trace;
    desc.Rumble = trace ? rumble : nullptr;
    id = SDL_AttachVirtualJoystick(&desc);
    expect(id != 0, "Attach virtual gamepad");
    handle = SDL_OpenJoystick(id);
    expect(handle != nullptr, "Open virtual gamepad");
    // Lower-level virtual trigger axes are bipolar; SDL's standardized
    // Gamepad events convert them to the documented0..32767 range.
    expect(SDL_SetJoystickVirtualAxis(handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768), "Release left trigger");
    expect(SDL_SetJoystickVirtualAxis(handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768), "Release right trigger");
    SDL_UpdateJoysticks();
}

VirtualPad::~VirtualPad() {
    detach();
}

void VirtualPad::detach() {
    detach_joystick(handle, id);
}

void VirtualPad::axis(SDL_GamepadAxis code, Sint16 value) {
    expect(SDL_SetJoystickVirtualAxis(handle, int(code), value), "Set virtual axis");
}

void VirtualPad::button(SDL_GamepadButton code, bool down) {
    expect(SDL_SetJoystickVirtualButton(handle, int(code), down), "Set virtual button");
}

std::string VirtualPad::guid() const {
    return joystick_guid(handle);
}

VirtualWheel::VirtualWheel(Uint16 product) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_WHEEL;
    desc.vendor_id = 0x1209;
    desc.product_id = product;
    desc.naxes = 8;
    desc.nbuttons = 32;
    desc.name = "TOCA Raw Wheel Fixture";
    id = SDL_AttachVirtualJoystick(&desc);
    expect(id != 0 && !SDL_IsGamepad(id), "Raw wheel must remain outside SDL's standardized gamepad mapping");
    handle = SDL_OpenJoystick(id);
    expect(handle != nullptr, "Open virtual raw wheel");
    axis(1, 32767);
    axis(2, 32767);
    SDL_UpdateJoysticks();
}

VirtualWheel::~VirtualWheel() {
    detach();
}

void VirtualWheel::detach() {
    detach_joystick(handle, id);
}

void VirtualWheel::axis(int code, Sint16 value) {
    expect(SDL_SetJoystickVirtualAxis(handle, code, value), "Move raw wheel/pedal axis");
}

void VirtualWheel::button(int code, bool down) {
    expect(SDL_SetJoystickVirtualButton(handle, code, down), "Press raw wheel button");
}

std::string VirtualWheel::guid() const {
    return "joystick:" + joystick_guid(handle);
}
} // namespace toca::test::input_devices
