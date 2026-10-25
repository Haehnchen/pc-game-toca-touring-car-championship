#include "core/random.hpp"
#include "scene/setup.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char* message) {
    try {
        function();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        using namespace toca::scene;
        const auto donington = resolve_setup({});
        require(donington.weather == 2 && donington.horizon_index == 1 && donington.selection.weather_choice == 0,
            "Default Donington must retain the requested default and resolve its overcast horizon");
        constexpr std::array<std::uint32_t, 10> weather{2, 5, 2, 2, 1, 3, 2, 1, 2, 1};
        constexpr std::array<std::uint32_t, 10> horizon{1, 4, 2, 1, 0, 3, 2, 0, 2, 0};
        for (std::uint32_t track = 0; track < weather.size(); ++track) {
            const SceneSelection selection{track, 7, 0, 0};
            const auto resolved = resolve_setup(selection);
            require(resolved.selection == selection && resolved.weather == weather[track] && resolved.horizon_index == horizon[track],
                "Normal-track defaults or source selection identity changed");
        }
        for (std::uint32_t choice = 1; choice < 6; ++choice) {
            const auto resolved = resolve_setup({0, 1, choice, 2});
            constexpr std::array<std::uint32_t, 5> explicit_horizon{0, 1, 3, 3, 4};
            require(resolved.weather == choice && resolved.horizon_index == explicit_horizon[choice - 1] && resolved.selection.car_id == 1 && resolved.selection.detail == 2,
                "Explicit weather must override the track default without replacing car/detail");
        }
        toca::SourceRandom very_high_random(8123), mega_random(8123);
        const auto very_high = resolve_setup({1, 7, 2, 3}, very_high_random);
        const auto mega = resolve_setup({1, 7, 2, 4}, mega_random);
        require(mega.selection == SceneSelection{1, 7, 2, 4} && mega.weather == very_high.weather && mega.horizon_index == very_high.horizon_index
                    && mega_random.next() == very_high_random.next(),
            "Mega must retain its selection while sharing Very High weather, horizon and RNG behavior");
        for (const auto track : {1u, 4u, 5u, 7u}) {
            const SceneSelection cloudy{track, 0, 2, 3};
            rejects(
                [&] {
                    (void)resolve_setup(cloudy);
                },
                "Ambiguous cloudy horizons must not invent a random result");
            for (const auto variant : {1u, 2u}) {
                require(resolve_setup(cloudy, variant).horizon_index == variant, "Injected original horizon choice was ignored");
            }
            for (const auto variant : {0u, 3u}) {
                rejects(
                    [&] {
                        (void)resolve_setup(cloudy, variant);
                    },
                    "Original cloudy horizon choice must be one or two");
            }
        }
        for (std::uint32_t track = 0; track < 10; ++track) {
            for (std::uint32_t choice = 0; choice < 6; ++choice) {
                toca::SourceRandom actual(1234), expected(1234);
                const SceneSelection selection{track, 0, choice, 3};
                const auto resolved = resolve_setup(selection, actual);
                const auto randomized = choice == 2 && (track == 1 || track == 4 || track == 5 || track == 7);
                if (randomized) {
                    require(resolved.horizon_index == expected.next() % 2 + 1, "Runtime cloudy choice must consume the original RNG result");
                }
                require(actual.next() == expected.next(), "Scene setup changed the source RNG draw count");
                if (track == 9) {
                    require(resolved.selection == selection && resolved.weather == 1 && resolved.horizon_index == 0,
                        "Lavaland must retain the requested selection but force source weather1 and LAVA03");
                }
            }
        }
        constexpr std::array<std::uint32_t, 12> championship_tracks{0, 1, 2, 3, 4, 5, 6, 7, 8, 2, 3, 1};
        constexpr std::array<std::uint32_t, 12> championship_weather{2, 5, 2, 2, 1, 3, 2, 1, 2, 3, 5, 2};
        constexpr std::array<std::uint32_t, 12> championship_horizons{1, 4, 2, 1, 0, 3, 1, 0, 2, 3, 4, 1};
        for (std::uint32_t meeting = 0; meeting < championship_tracks.size(); ++meeting) {
            // Deliberately conflicting free-weather/rain settings must not
            // override the authored meeting, including its return visits.
            SceneSelection selection{.track_id = championship_tracks[meeting], .weather_choice = 2, .rain_up = true, .cat_dog = true, .championship_meeting = meeting};
            toca::SourceRandom actual(8123), expected(8123);
            auto championship = resolve_setup(selection, actual);
            require(championship.selection == selection && championship.weather == championship_weather[meeting] && championship.horizon_index == championship_horizons[meeting],
                "Every championship meeting must use its authored weather/horizon independently of free-weather or rain codes");
            require(actual.next() == expected.next(), "Championship's fixed cloudy horizons must not consume a source random draw");
            selection.stars = true;
            championship = resolve_setup(selection);
            require(championship.weather == 1 && championship.horizon_index == 5, "Original star sky must retain precedence over championship weather");
            selection.stars = false;
            selection.toon = true;
            championship = resolve_setup(selection);
            require(championship.weather == 1 && championship.horizon_index == 6, "Original cartoon sky must retain precedence over championship weather");
        }
        for (const auto bonus : {8u, 9u}) {
            require(resolve_setup({0, bonus, 0, 3}).selection.car_id == bonus, "Both unlocked bonus cars must reach scene setup");
        }
        for (const auto track : {0u, 1u, 9u}) {
            toca::SourceRandom actual(321), expected(321);
            auto selection = SceneSelection{track, 0, 2, 3, true, true, true, true};
            auto special = resolve_setup(selection, actual);
            require(special.weather == 1 && special.horizon_index == (track == 9 ? 0u : 5u), "Lavaland then CMSTARS must win over cartoon and rain flags");
            selection.stars = false;
            special = resolve_setup(selection, actual);
            require(special.horizon_index == (track == 9 ? 0u : 6u), "CMTOON must retain its original horizon member");
            selection.toon = false;
            special = resolve_setup(selection, actual);
            require(special.weather == (track == 9 ? 1u : 3u) && special.horizon_index == (track == 9 ? 0u : 3u), "Rain codes must force original wet weather");
            require(actual.next() == expected.next(), "Special skies and rain flags must bypass cloudy RNG draws");
        }
        for (const SceneSelection invalid : {SceneSelection{10, 0, 0, 3}, SceneSelection{0, 10, 0, 3}, SceneSelection{0, 0, 6, 3}, SceneSelection{0, 0, 0, 5}}) {
            rejects(
                [&] {
                    (void)resolve_setup(invalid);
                },
                "Unsupported source selection must fail before resource indexing");
        }
        rejects(
            [] {
                (void)resolve_setup({.championship_meeting = 12});
            },
            "Invalid championship meeting must fail before indexing fixed weather tables");
        std::cout << "PASS: scene defaults, explicit weather, bounded selections, Lavaland override and exact cloudy RNG consumption\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
