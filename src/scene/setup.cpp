#include "scene/setup.hpp"

#include "core/contracts.hpp"
#include "core/random.hpp"

#include <array>
#include <stdexcept>

namespace toca::scene {
namespace {
void validate_selection(const SceneSelection& selection) {
    if (selection.track_id >= 10 || selection.car_id >= 10 || selection.weather_choice >= 6 || selection.detail >= graphics_detail_count
        || (selection.championship_meeting && *selection.championship_meeting >= 12)) {
        throw std::runtime_error("Scene selection requires track/car 0..9, weather 0..5 and detail 0..4");
    }
}
} // namespace

SceneSetup resolve_setup(const SceneSelection& selection, std::optional<std::uint32_t> cloudy_horizon) {
    validate_selection(selection);
    // 4512b0's bonus-track branch ignores the requested weather and uses LAVA03.
    if (selection.track_id == 9) {
        return {selection, 1, 0};
    }
    // 4512b0: special skies take priority over weather and rain codes.
    if (selection.stars || selection.toon) {
        return {selection, 1, selection.stars ? 5u : 6u};
    }
    if (selection.championship_meeting) {
        // 4512b0 selects by the meeting, including the three return visits.
        // Championship overrides the rain codes, after special-sky handling.
        constexpr std::array<std::uint32_t, 12> weather{2, 5, 2, 2, 1, 3, 2, 1, 2, 3, 5, 2};
        constexpr std::array<std::uint32_t, 12> horizon{1, 4, 2, 1, 0, 3, 1, 0, 2, 3, 4, 1};
        return {selection, weather[*selection.championship_meeting], horizon[*selection.championship_meeting]};
    }
    if (selection.rain_up || selection.cat_dog) {
        return {selection, 3, 3};
    }
    // FUN_004512b0's normal-mode track switch; choice 0 means track default.
    constexpr std::array<std::uint32_t, 9> defaults{2, 5, 2, 2, 1, 3, 2, 1, 2};
    SceneSetup result{selection, selection.weather_choice == 0 ? defaults[selection.track_id] : selection.weather_choice, 0};
    switch (result.weather) {
        case 1:
            result.horizon_index = 0;
            break;
        case 2:
            switch (selection.track_id) {
                case 0:
                case 3:
                    result.horizon_index = 1;
                    break;
                case 2:
                case 6:
                case 8:
                    result.horizon_index = 2;
                    break;
                default:
                    if (!cloudy_horizon || (*cloudy_horizon != 1 && *cloudy_horizon != 2)) {
                        throw std::runtime_error("This cloudy track requires an injected original horizon choice of 1 or 2");
                    }
                    result.horizon_index = *cloudy_horizon;
                    break;
            }
            break;
        case 3:
        case 4:
            result.horizon_index = 3;
            break;
        case 5:
            result.horizon_index = 4;
            break;
        default:
            throw std::logic_error("Unresolved scene weather");
    }
    return result;
}

SceneSetup resolve_setup(const SceneSelection& selection, SourceRandom& random) {
    validate_selection(selection);
    // Only these four normal tracks draw 44cb80(2) for explicit cloudy weather.
    const auto track = selection.track_id;
    if (!selection.championship_meeting && !selection.stars && !selection.toon && !selection.rain_up && !selection.cat_dog && selection.weather_choice == 2
        && (track == 1 || track == 4 || track == 5 || track == 7)) {
        return resolve_setup(selection, random.next() % 2 + 1);
    }
    return resolve_setup(selection);
}
} // namespace toca::scene
