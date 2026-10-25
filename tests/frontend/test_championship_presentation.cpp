#include "frontend/championship_presentation.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void check(const toca::frontend::ChampionshipPresentationResources& resources) {
    using namespace toca;
    const std::array<std::array<std::uint32_t, 3>, 4> counts{{{0, 0, 3}, {1, 1, 2}, {1, 3, 0}, {4, 0, 0}}};
    for (std::uint32_t language = 0; language < 5; ++language) {
        for (std::uint32_t block = 0; block < 4; ++block) {
            for (std::uint32_t tier = 0; tier < 4; ++tier) {
                auto state = race::new_championship(false, 0, "PLAYER");
                state.phase = race::ChampionshipPhase::results;
                state.meeting = block * 3 + 2;
                state.race = 1;
                state.points[0] = race::championship_target(state);
                state.podiums = counts[tier];
                const auto honors = race::championship_honors(state).value();
                const auto lines = frontend::championship_honors_lines(resources, honors, language);
                expect(lines.size() == 5 + tier && lines[0].heading && lines[0].y_q16 == 0xf5c && lines[1].y_q16 == 0x2e14 && lines[2].y_q16 == 0x4000 && lines[3].y_q16 == 0x51eb,
                    "Honors must retain original quarter/counter positions and cumulative award rows");
                expect(lines[0].text.find(std::to_string(block + 1)) != std::string::npos, "Quarter caption must use the completed source block");
                for (const auto& line : lines) {
                    expect(!line.text.empty() && line.text.find("%d") == std::string::npos && line.y_q16 < 65536,
                        "Every localized honors line must have resolved source text and remain on screen");
                }
                if (tier != 0) {
                    expect(lines[5].text.find(honors.rewards[0].name) != std::string::npos && lines[5].y_q16 == 38611,
                        "First reward must follow BIG640's36-pixel heading and original39-pixel advance");
                }
                state.showdown = true;
                const auto showdown = race::championship_honors(state).value();
                const auto special = frontend::championship_honors_lines(resources, showdown, language);
                expect(special.size() == (tier == 3 ? 6u : 5u), "Showdown must show no secret reward below four wins");
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            const toca::AssetStore store(argv[1]);
            const auto resources = toca::frontend::load_championship_presentation(store);
            expect(resources.honors[0].title == "QUARTER %d" && resources.honors[1].title == "SESSION %d" && resources.honors[2].title == "VIERTELS %d"
                       && resources.honors[3].title == "SESSIONE %d" && resources.honors[4].title == "SESION %d",
                "All five actual GSTRINGS quarter captions must retain their original language mapping");
            check(resources);
            for (std::uint32_t block = 0; block < 4; ++block) {
                for (const auto width : {640u, 512u, 320u}) {
                    const auto background = toca::frontend::load_championship_honors_background(store, block, width);
                    expect(background->w == int(width) && background->h == int(width * 3 / 4), "All twelve CSCREENS variants must retain original authored dimensions");
                }
            }
        } else if (argc == 1) {
            toca::frontend::ChampionshipPresentationResources resources;
            for (auto& labels : resources.honors) {
                labels = {"QUARTER %d", "1ST PLACES : %d", "2ND PLACES : %d", "3RD PLACES : %d", "SORRY. NO CHEATS WON.",
                    {"", "BRONZE CHEAT %d", "SILVER CHEATS %d", "GOLD CHEATS %d"}, "SHOWDOWN %d", "ENTER NAME AS", "TRACK UNLOCKED", "VEHICLE UNLOCKED"};
            }
            check(resources);
        } else {
            throw std::runtime_error("Usage: toca-championship-presentation-tests [canonical-root]");
        }
        std::cout << "PASS: source honors codes, localized rows and original quarter backgrounds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
