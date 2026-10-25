#pragma once

#include <cstdint>
#include <optional>

namespace toca {
class SourceRandom;
}

namespace toca::scene {
// Parameters consumed by the single-player resource-loader branch.
// Session/profile state and unlock effects belong to later runtime consumers.
struct SceneSelection {
    std::uint32_t track_id = 0;
    std::uint32_t car_id = 0;
    std::uint32_t weather_choice = 0;
    std::uint32_t detail = 3;
    bool stars = false, toon = false, rain_up = false, cat_dog = false;
    std::optional<std::uint32_t> championship_meeting = std::nullopt;

    bool operator==(const SceneSelection&) const = default;
};

struct SceneSetup {
    SceneSelection selection;
    std::uint32_t weather = 0;
    std::uint32_t horizon_index = 0;
};

// FUN_004512b0 calls FUN_0044cb80(2) only for ambiguous cloudy horizons.
// Supply its resolved result (1 or 2) explicitly for those paths; this layer
// neither substitutes a random generator nor consumes a hidden random stream.
SceneSetup resolve_setup(const SceneSelection& selection, std::optional<std::uint32_t> cloudy_horizon = std::nullopt);
// Runtime overload uses the explicitly owned source stream and consumes a draw
// only on the original randomized cloudy branches.
SceneSetup resolve_setup(const SceneSelection& selection, SourceRandom& random);
} // namespace toca::scene
