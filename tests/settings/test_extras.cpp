#include "frontend/cheats.hpp"
#include "settings/extras.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace toca;
        namespace cheats = frontend::cheats;
        const auto path = std::filesystem::current_path() / "extras-state.cfg";
        require(!std::filesystem::exists(path), "Test needs isolated storage");
        auto state = load_extras(path);
        require(state == cheats::State{} && !cheats::track_available(state, 2), "Missing state must preserve original fresh locks");
        state.progress = {true, true, false, 7};
        cheats::set_menu_active(state, cheats::Code::all_tracks, true);
        cheats::set_menu_active(state, cheats::Code::bonus_cars, true);
        for (const auto& entry : cheats::catalog()) {
            if (entry.code != cheats::Code::lock && entry.code != cheats::Code::bonus_cars && entry.code != cheats::Code::all_tracks) {
                cheats::apply(state, entry.code);
            }
        }

        save_extras(path, state);
        require(load_extras(path) == state, "Overrides, action flags and earned progress must survive restart");
        cheats::set_menu_active(state, cheats::Code::all_tracks, false);
        cheats::set_menu_active(state, cheats::Code::bonus_cars, false);
        save_extras(path, state);
        auto restored = load_extras(path);
        require(
            restored == state && restored.progress.track_threshold == 7 && restored.progress.special_championship && restored.progress.bonus_car_8 && restored.progress.bonus_car_9,
            "Saved unlock switches must preserve independent progress and other code effects");
        cheats::apply(restored, cheats::Code::lock);
        save_extras(path, restored);
        require(load_extras(path).progress == cheats::Progress{}, "Explicit CMLOCK must persist original fresh progress");
        for (const auto text : {"version=1\nearned_track_threshold=10\n", "version=1\nbonus_cars=yes\n", "version=1\nall_tracks=on\nall_tracks=off\n"}) {
            bool failed = false;
            try {
                std::istringstream input(text);
                (void)read_extras(input);
            } catch (const std::exception&) {
                failed = true;
            }
            require(failed, "Malformed Extras state must not silently grant unlocks");
        }
        for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
            require(!entry.path().filename().string().starts_with("extras-state.cfg.tmp-"), "State write left a temporary file");
        }
        std::cout << "PASS: Extras persistence, separate earned progress, reset and validated state\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
