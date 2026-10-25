#include "frontend/builtin_data.hpp"
#include "settings/championship_records.hpp"

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
        using namespace toca::frontend;
        // These public APIs have no file-store parameter or original-data input.
        const auto selection = load_selection_resources();
        require(selection.cars[0].drive == builtin::front_wheel_drive && selection.cars[0].valves == 16 && selection.cars[1].drive == builtin::four_wheel_drive,
            "Native vehicle specifications must preserve the Honda/Audi drivetrains and valve count");
        const auto tracks = load_track_resources();
        require(tracks.tracks[0].metres == 4020 && tracks.tracks[1].metres == 3624 && tracks.tracks[0].lap_time == "9:59.99",
            "Native track distances and default lap times must retain their original units");
        require(builtin::translated(builtin::TextId::main_menu_title, 0) == "MAIN MENU" && builtin::translated(builtin::TextId::main_menu_title, 2) == "HAUPTMENUE",
            "Symbolic text IDs must select the requested original language");
        for (std::uint32_t language = 0; language < 5; ++language) {
            require(!selection.labels[language].name_prompt.empty() && !tracks.labels[language].help.empty(), "Every native selection language must have its prompts and help");
        }
        const auto records = load_default_lap_records();
        const auto championship = load_default_championship_records();
        require(records[0].driver == "ANDY" && records[0].lap_ms == 599990 && championship[0].driver == "PATRICK" && championship[0].points == 225,
            "Native default records must preserve driver, score and time data");
        require(load_record_resources().badges[0].width == 28 && load_menu_transition_resources().frames[0][0].corners[0].x == 33,
            "Named native geometry must expose the decoded pixel coordinates");
        std::cout << "PASS: symbolic translations, typed cars/tracks/records and menu geometry work without any reference installation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
