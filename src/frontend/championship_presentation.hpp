#pragma once

#include "assets/store.hpp"
#include "image/decode.hpp"
#include "race/championship.hpp"

#include <array>
#include <string>
#include <vector>

namespace toca::frontend {
struct ChampionshipHonorsLabels {
    std::string title, firsts, seconds, thirds, no_award;
    std::array<std::string, 4> tier; // Unused0, bronze1, silver2, gold3.
    std::string showdown_gold, secret_code, bonus_track, bonus_car;
};

struct ChampionshipPresentationResources {
    std::array<ChampionshipHonorsLabels, 5> honors;
};

struct ChampionshipPresentationLine {
    std::string text;
    std::uint32_t y_q16 = 0;
    bool heading = false;
};

// 42bee0 /427cb0: original GSTRINGS, source-normalized centers/Y positions.
// 449350: quarter0..3 selects COVER/FLAME/HILL/START at original widths.
Surface load_championship_honors_background(const AssetStore& store, std::uint32_t block, std::uint32_t width = 640);
ChampionshipPresentationResources load_championship_presentation(const AssetStore& store);
std::vector<ChampionshipPresentationLine> championship_honors_lines(
    const ChampionshipPresentationResources& resources, const race::ChampionshipHonors& honors, std::uint32_t language);
} // namespace toca::frontend
