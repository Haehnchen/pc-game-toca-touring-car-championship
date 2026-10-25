#include "input_devices/fixtures.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    using namespace toca::test::input_devices;

    struct Case {
        std::string_view suite;
        void (*run)();
    };

    constexpr std::array cases{Case{"keyboard-pad", checks}, Case{"wheel", raw_wheel_checks}, Case{"feedback", routed_wheel_feedback_checks}, Case{"feedback", rumble_checks},
        Case{"keyboard-pad", confirming_pad_checks}, Case{"keyboard-pad", progressive_steering_checks}, Case{"local-players", local_two_player_checks},
        Case{"local-players", local_hotplug_rearming_checks}, Case{"local-players", automatic_input_checks}, Case{"local-players", automatic_local_ownership_checks}};
    std::string_view suite = "all";
    if (argc != 1) {
        if (argc != 3 || std::string_view(argv[1]) != "--suite") {
            std::cerr << "Usage: toca-input-device-tests [--suite all|keyboard-pad|local-players|wheel|feedback]\n";
            return 1;
        }
        suite = argv[2];
        if (suite != "all" && !std::ranges::any_of(cases, [&](const auto& entry) {
                return entry.suite == suite;
            })) {
            std::cerr << "Unknown input-device test suite: " << suite << '\n';
            return 1;
        }
    }
    // Auto must enumerate only this process's virtual fixtures, never the
    // user's connected controllers or their physical vibration motors.
    if (!SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,
            "0x1209/0x5443,0x1209/0x5444,0x1209/0x5445,0x1209/0x5446,0x1209/0x5450,0x1209/0x5451,0x1209/0x5452,0x1209/0x5453,0x1209/0x5454,0x1209/0x5460", SDL_HINT_OVERRIDE)) {
        std::cerr << "Could not isolate virtual gamepad fixtures\n";
        return 1;
    }
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    try {
        for (const auto& entry : cases) {
            if (suite == "all" || suite == entry.suite) {
                entry.run();
            }
        }
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        expect(SDL_WasInit(SDL_INIT_GAMEPAD) == 0, "InputDevices scopes must release their extra GAMEPAD subsystem references");
        SDL_Quit();
        std::cout << "PASS: input-device suite " << suite << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
