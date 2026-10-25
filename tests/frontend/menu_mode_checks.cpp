#include "menu_mode_checks.hpp"

#include "frontend/font.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "menu_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace toca::frontend::test {
void local_car_boost_controls(const toca::frontend::Menus* menus) {
    using namespace toca;
    using namespace toca::frontend;
    constexpr std::array<std::string_view, 3> enabled{"CAR BOOST : YES", "TURBO : OUI", "TEMPOSCHUB : JA"};
    constexpr std::array<std::string_view, 3> disabled{"CAR BOOST : NO", "TURBO : NON", "TEMPOSCHUB : NEIN"};
    constexpr std::array<std::string_view, 3> help{"SPEED UP SLOW PLAYER CARS?", "ACCELERER VOITURES LENTES?", "LANGSAME SP-WAGEN BESCHLEUNIGEN?"};
    for (std::uint32_t language = 0; language < enabled.size(); ++language) {
        Controller controller;
        const MenuInput press{controller};
        controller.update({}, {0, 5'000'000'000});
        controller.frontend_ready();
        for (std::uint32_t index = 0; index < language; ++index) {
            press(Action::down);
        }
        press(Action::confirm);
        press(Action::confirm);
        press(Action::confirm);
        Menu original;
        if (menus) {
            original = menu_for(*menus, controller);
        }
        press(Action::down); // Players.
        require(press(Action::right) == std::vector{Cue::option} && controller.setup().players == 2 && controller.setup().car_boost && !controller.setup().qualifying,
            "Local Car Boost must start enabled with qualifying bypassed");
        press(Action::down);
        press(Action::down); // Car Boost replaces Qualifying.
        if (menus) {
            const auto local = menu_for(*menus, controller);
            require(local.items[2].label == enabled[language], "Local Car Boost must use the original localized enabled label");
            require(local.items[2].help == help[language], "Local Car Boost must use the original localized catch-up help");
            require(local.items[2].icon == 1 && local.items[2].adjustable == original.items[2].adjustable,
                "Local Car Boost must preserve the Single Race row's original icon and presentation metadata");
        }
        bool boost = true;
        for (const auto action : {Action::left, Action::right, Action::confirm}) {
            boost = !boost;
            require(press(action) == std::vector{Cue::option} && controller.setup().car_boost == boost && !controller.setup().qualifying
                        && controller.screen() == Screen::single_options && controller.selection() == 2,
                "Left, Right and Enter must toggle local Car Boost with its original option cue");
            if (menus) {
                require(menu_for(*menus, controller).items[2].label == (boost ? enabled[language] : disabled[language]),
                    "The local Car Boost label must reflect each toggle in every enabled language");
            }
        }
        press(Action::up);
        press(Action::up);
        require(press(Action::left) == std::vector{Cue::option} && controller.setup().players == 1 && controller.setup().qualifying && !controller.setup().car_boost,
            "Returning to one player must restore Qualifying Yes and retain the shared Car Boost setting");
        if (menus) {
            const auto restored = menu_for(*menus, controller);
            require(restored.items[2].label == original.items[2].label && restored.items[2].help == original.items[2].help && restored.items[2].icon == original.items[2].icon,
                "Returning to one player must restore the original qualifying label, help and icon");
        }
        press(Action::down);
        press(Action::down);
        require(press(Action::confirm) == std::vector{Cue::option} && !controller.setup().qualifying && !controller.setup().car_boost,
            "Single-player Qualifying must toggle independently of Car Boost");
        press(Action::up);
        press(Action::up);
        press(Action::right);
        require(controller.setup().players == 2 && !controller.setup().car_boost, "Returning to local multiplayer must retain Car Boost No");
        press(Action::down);
        press(Action::down);
        press(Action::confirm);
        press(Action::up);
        press(Action::up);
        press(Action::left);
        require(controller.setup().players == 1 && !controller.setup().qualifying && controller.setup().car_boost,
            "Returning to one player must also restore Qualifying No after independently enabling Car Boost");
    }
}

void time_trial_controls(const toca::frontend::Menus* menus) {
    using namespace toca;
    using namespace toca::frontend;
    for (std::uint32_t language = 0; language < 3; ++language) {
        Controller route;
        route.set_name_defaults({"DRIVER 1", "PILOTE 1", "FAHRER 1", "PILOTA 1", "PILOTO 1"});
        const MenuInput press{route};
        route.update({}, {0, 5'000'000'000});
        route.frontend_ready();
        for (std::uint32_t index = 0; index < language; ++index) {
            press(Action::down);
        }
        press(Action::confirm);
        press(Action::confirm);
        press(Action::confirm); // Single Race options.
        press(Action::up); // Weather.
        press(Action::up); // Qualify.
        press(Action::confirm);
        press(Action::up); // Laps.
        press(Action::right);
        const auto single = route.setup();
        press(Action::back);
        press(Action::down);
        press(Action::down);
        require(route.selection() == 2 && route.item_available(2), "Time Trial must be playable from the ordinary race menu");
        require(press(Action::confirm) == std::vector{Cue::confirm} && route.screen() == Screen::time_trial_options && route.selection() == 2
                    && route.setup().mode == GameMode::time_trial && !route.setup().qualifying && route.setup().players == 1 && route.setup().laps == 1,
            "Time Trial must start at Continue with one player and no qualifying");
        if (menus) {
            const auto menu = menu_for(*menus, route);
            require(menu.title == menus->race[language].items[2].label && menu.items.size() == 3 && menu.items[0].icon == 28 && menu.items[1].icon == 30 && menu.items[2].icon == 13
                        && menu.items[0].adjustable && menu.items[1].adjustable && !menu.items[2].adjustable,
                "Time Trial must retain its three original localized rows and icons");
            constexpr std::array<std::string_view, 3> ghost_labels{"LOAD BEST LAP : NO", "CHARG. MEILLEUR TOUR : NON", "BESTE RUNDE LADEN : NEIN"};
            require(menu.items[1].label == ghost_labels[language], "Load Best Lap must use the canonical language and Yes/No format");
        }
        press(Action::up);
        require(press(Action::right) == std::vector{Cue::option} && route.setup().ghost_enabled, "Load Best Lap must enable saved-ghost playback");
        require(press(Action::left) == std::vector{Cue::option} && !route.setup().ghost_enabled, "Load Best Lap arrows must toggle loading independently of lap recording");
        press(Action::up);
        require(press(Action::right) == std::vector{Cue::option} && route.setup().weather == 1, "Time Trial must expose the shared original weather selector");
        press(Action::up);
        press(Action::confirm);
        require(route.screen() == Screen::name_entry, "Time Trial Continue must enter Name directly");
        press(Action::back);
        require(route.screen() == Screen::time_trial_options && route.selection() == 2 && !route.setup().ghost_enabled && route.setup().weather == 1,
            "Name Back must restore Time Trial Continue and retain its choices");
        press(Action::confirm);
        press(Action::confirm);
        require(route.screen() == Screen::car_selection, "Time Trial Name must lead to the ordinary garage");
        press(Action::right);
        press(Action::confirm);
        require(route.screen() == Screen::track_selection && route.car_id() == 1, "Time Trial must retain selected car through Track");
        press(Action::confirm);
        require(route.screen() == Screen::scene_loading && route.setup().mode == GameMode::time_trial && !route.setup().ghost_enabled,
            "Track confirmation must hand the real mode and ghost choice to scene loading");
        route.scene_ready();
        route.return_from_scene();
        require(route.screen() == Screen::time_trial_records && route.selection() == 0, "Time Trial exit must open the first original track-record page");
        require(press(Action::confirm) == std::vector{Cue::confirm} && route.selection() == 1, "Record Enter must advance to the second five-track page");
        require(press(Action::back) == std::vector{Cue::back} && route.selection() == 0, "Second record page Back must restore the first page");
        press(Action::confirm);
        press(Action::confirm);
        require(route.screen() == Screen::main_menu && route.selection() == 0, "Last post-trial record page must return to Main Start Race");
        press(Action::confirm);
        require(route.screen() == Screen::race_menu && route.selection() == 2, "Returning through Main must retain the previously chosen Time Trial mode");
        press(Action::up);
        press(Action::up);
        press(Action::confirm);
        require(route.screen() == Screen::single_options && route.setup().mode == GameMode::single_race && route.setup().lap_choice == single.lap_choice
                    && route.setup().laps == single.laps && route.setup().qualifying == single.qualifying && route.setup().weather == 1,
            "Returning to Single Race must restore its lap/qualifying choices while keeping shared weather");
        press(Action::back);
        press(Action::down);
        press(Action::down);
        press(Action::confirm);
        require(route.screen() == Screen::time_trial_options && !route.setup().ghost_enabled && route.setup().weather == 1,
            "Reentering Time Trial must retain ghost and weather selections");
    }
}
} // namespace toca::frontend::test
