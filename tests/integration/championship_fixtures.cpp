#include "race/championship.hpp"
#include "settings/championship.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
toca::race::ChampionshipState result(std::string name, std::uint32_t meeting, bool showdown = false) {
    using namespace toca::race;
    auto state = new_championship(false, 0, std::move(name), showdown);
    state.meeting = meeting;
    state.race = 1;
    state.phase = ChampionshipPhase::results;
    state.has_won = true;
    state.podiums = {4, 1, 1};
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        state.drivers[actor].driver = actor == 0 ? state.driver : "DRIVER" + std::to_string(actor);
        state.drivers[actor].time_ms = 90000 + actor * 100;
        state.points[actor] = actor == 0 ? (meeting + 1) * 26 : (meeting + 1) * (16 - actor);
    }
    for (std::uint32_t block = 0; block < meeting / 3; ++block) {
        state.awards[block] = 3;
    }
    validate_championship(state);
    return state;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2 && (argc != 3 || std::string_view(argv[2]) != "--records")) {
            throw std::runtime_error("Usage: championship-fixtures STATE_ROOT [--records]");
        }
        const std::filesystem::path root(argv[1]);
        if (argc == 3) {
            toca::save_championship_slot(root, 0, result("CHAMP", 11));
            auto abandoned = result("ABANDON", 10);
            abandoned.phase = toca::race::ChampionshipPhase::qualifying;
            abandoned.race = 0;
            abandoned.points[0] = 300;
            abandoned.awards = {3, 2, 1, 0};
            toca::save_championship_slot(root, 1, abandoned);
            auto eliminated = result("ELIMINATE", 10);
            eliminated.points[0] = 199;
            toca::save_championship_slot(root, 2, eliminated);
            toca::save_championship_slot(root, 0, result("SHOWDOWN", 11, true));
            std::cout << "Native championship record fixtures ready\n";
            return 0;
        }
        // Test-owned season boundaries exercise real slot IO and frontend flow.
        // They do not replace the separately accepted physical race test.
        toca::save_championship_slot(root, 0, result("GOLD", 2));
        toca::save_championship_slot(root, 1, result("KEEP", 0));
        toca::save_championship_slot(root, 3, result("FINAL", 11));
        std::ofstream damaged(toca::championship_slot_path(root, false, 2), std::ios::binary);
        damaged << "damaged private test slot";
        damaged.close();
        toca::save_championship_slot(root, 0, result("SHOWDOWN", 11, true));
        toca::save_championship_slot(root, 1, result("BONUS", 5, true));
        std::cout << "Native championship presentation fixtures ready\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
