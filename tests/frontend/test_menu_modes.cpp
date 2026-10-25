#include "frontend/font.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "menu_mode_checks.hpp"
#include "menu_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace {
using namespace toca::frontend::test;

void local_two_player_controls() {
    using namespace toca;
    using namespace toca::frontend;
    Controller controller;
    controller.set_name_defaults({"DRIVER 1", "PILOTE 1", "FAHRER 1", "PILOTA 1", "PILOTO 1"});
    const MenuInput press{controller};
    controller.update({}, {0, 5'000'000'000});
    controller.frontend_ready();
    for (int index = 0; index < 3; ++index) {
        press(Action::confirm);
    }
    press(Action::down);
    press(Action::right);
    require(
        controller.setup().players == 2 && !controller.setup().qualifying && controller.item_available(2), "Local two-player races must expose Car Boost and bypass qualifying");
    press(Action::down);
    press(Action::down);
    require(press(Action::confirm) == std::vector{Cue::option} && !controller.setup().car_boost && !controller.setup().qualifying,
        "Car Boost must toggle in the local two-player menu while qualifying remains off");
    press(Action::down);
    press(Action::down);
    press(Action::confirm);
    press(Action::confirm);
    press.text("FIRST");
    press(Action::confirm);
    press(Action::right);
    press(Action::up);
    press(Action::confirm);
    require(controller.screen() == Screen::name_entry && controller.selection_player() == 1 && controller.player_name(0) == "FIRST" && controller.car_id(0) == 1
                && controller.transmission_manual(0),
        "Player one's committed selection must survive entry to player two's editor");
    press.text("SECOND");
    press(Action::confirm);
    press(Action::right);
    press(Action::right);
    require(controller.car_id() == 1 && controller.car_id(1) == 2 && !controller.transmission_manual(1), "Player two must have an independent car and transmission");
    press(Action::confirm);
    require(controller.screen() == Screen::track_selection && controller.selection_player() == 0 && controller.player_name(1) == "SECOND" && controller.stored_car_id(0) == 1
                && controller.stored_car_id(1) == 2,
        "Both driver selections must be committed before selecting the shared track");
    press(Action::back);
    require(controller.screen() == Screen::car_selection && controller.selection_player() == 1, "Track Back must reopen player two's garage");
    press(Action::back);
    press(Action::back);
    require(
        controller.screen() == Screen::car_selection && controller.selection_player() == 0 && controller.car_id() == 1, "Player two's name Back must restore player one's garage");
    press(Action::confirm);
    press(Action::confirm);
    press(Action::confirm);
    press(Action::confirm);
    require(controller.screen() == Screen::scene_loading && controller.setup().players == 2 && !controller.setup().qualifying, "The normal local flow must load a two-human race");
}

void extras_controls() {
    using namespace toca;
    using namespace toca::frontend;
    Controller controller;
    controller.set_name_defaults({"DRIVER 1", "PILOTE 1", "FAHRER 1", "PILOTA 1", "PILOTO 1"});
    const MenuInput press{controller};
    controller.update({}, {0, 5'000'000'000});
    controller.frontend_ready();
    press(Action::confirm);
    press(Action::down);
    press(Action::down);
    require(controller.selection() == 2 && press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == Screen::time_trial_records && press(Action::left).empty()
                && press(Action::right).empty() && !controller.take_cheats_changed(),
        "Main High Scores must open the original Time Trial table without changing Extras");
    press(Action::back);
    require(controller.screen() == Screen::main_menu && controller.selection() == 2, "Record Back must restore Main High Scores");
    press(Action::up);
    press(Action::up);
    press(Action::up);
    require(controller.selection() == 4, "Quit must follow Extras as the final main-menu row");
    press(Action::up);
    require(controller.selection() == 3 && controller.item_available(3), "Extras must precede Quit in the normal main menu");
    press(Action::confirm);
    require(controller.screen() == Screen::extras && !controller.take_cheats_changed(), "Opening Extras must not write preferences");
    constexpr std::array screens{Screen::extras_unlocks, Screen::extras_camera, Screen::extras_effects, Screen::extras_racing};
    auto state = controller.cheats_state();
    state.progress = {true, true, false, 7};
    controller.set_cheats_state(state);
    for (std::size_t category = 0; category < screens.size(); ++category) {
        require(controller.selection() == category && press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == screens[category],
            "Each category must open from its named Extras row");
        const auto codes = cheats::category_codes(static_cast<cheats::Category>(category));
        for (const auto code : codes) {
            require(cheats::effect_ready(code) && controller.item_available(controller.selection()), "Every listed Extras effect must be available");
            require(press(Action::right) == std::vector{Cue::option} && cheats::menu_active(controller.cheats_state(), code) && controller.take_cheats_changed()
                        && !controller.take_cheats_changed(),
                "YES must enable a ready effect with a normal option cue and one save notification");
            if (code == cheats::Code::all_tracks) {
                require(controller.track_max_id() == 9 && cheats::track_available(controller.cheats_state(), 9), "Tracks YES must include Lavaland");
            }
            if (code == cheats::Code::bonus_cars) {
                require(cheats::car_available(controller.cheats_state(), 8) && cheats::car_available(controller.cheats_state(), 9), "Cars YES must expose both bonus cars");
            }
            require(press(Action::right).empty() && !controller.take_cheats_changed(), "Repeated YES must not toggle or save");
            require(press(Action::left) == std::vector{Cue::option} && !cheats::menu_active(controller.cheats_state(), code) && controller.take_cheats_changed(),
                "NO must disable the selected effect with an option cue");
            require(press(Action::left).empty() && !controller.take_cheats_changed(), "Repeated NO must preserve state");
            require(press(Action::confirm) == std::vector{Cue::option} && cheats::menu_active(controller.cheats_state(), code), "Enter must toggle effects");
            press(Action::confirm);
            controller.take_cheats_changed();
            press(Action::down);
        }
        require(controller.selection() == codes.size() && press(Action::left).empty() && press(Action::right).empty(), "Back must ignore value arrows");
        require(press(Action::confirm) == std::vector{Cue::back} && controller.screen() == Screen::extras && controller.selection() == category,
            "Category Back must restore its parent row");
        press(Action::confirm);
        require(press(Action::back) == std::vector{Cue::back} && controller.selection() == category, "Escape must restore the remembered category");
        press(Action::down);
    }
    require(controller.cheats_state().progress == state.progress, "Extras controls must preserve earned progress");
    controller.cheats_saved("Disk full");
    require(controller.cheats_error() == "Disk full", "Extras must expose a save failure");
    require(press(Action::confirm) == std::vector{Cue::back} && controller.screen() == Screen::main_menu && controller.selection() == 3 && controller.take_cheats_changed(),
        "Leaving Extras must retry a failed save and restore its main-menu row");
    press(Action::confirm);
    press(Action::confirm);
    controller.cheats_saved("Disk full");
    require(press(Action::back) == std::vector{Cue::back} && controller.take_cheats_changed(), "Leaving a category must retry failed persistence");
    press(Action::back);
    for (int index = 0; index < 3; ++index) {
        press(Action::up);
    }
    for (int index = 0; index < 4; ++index) {
        press(Action::confirm);
    }
    require(controller.screen() == Screen::name_entry, "Typed codes must remain available through the ordinary driver editor");
    InputSnapshot text;
    text.text_input = "JHAMMO";
    controller.update(text, {0, 1});
    require(press(Action::confirm) == std::vector{Cue::cheat_activated} && controller.screen() == Screen::name_entry && controller.player_name().empty()
                && controller.cheats_state().overrides.all_tracks && controller.track_max_id() == 8 && controller.take_cheats_changed(),
        "Typed JHAMMO must retain original speech and the original Lavaland browse gate");
    controller.update(text, {0, 1});
    require(press(Action::confirm) == std::vector{Cue::option} && !controller.cheats_state().overrides.all_tracks && controller.player_name().empty(),
        "Typed toggles must consume their names and disable without activation speech");
    text.text_input = "CMLOCK";
    controller.update(text, {0, 1});
    require(press(Action::confirm) == std::vector{Cue::cheat_activated} && controller.screen() == Screen::name_entry && controller.player_name().empty()
                && controller.cheats_state().progress == cheats::Progress{},
        "Explicit CMLOCK redemption must execute the original reset and consume its name");
}

void typed_code_controls() {
    using namespace toca;
    using namespace toca::frontend;
    for (const auto& entry : cheats::catalog()) {
        Controller controller;
        controller.set_name_defaults({"DRIVER 1", "PILOTE 1", "FAHRER 1", "PILOTA 1", "PILOTO 1"});
        controller.update({}, {0, 5'000'000'000});
        controller.frontend_ready();
        InputSnapshot confirm;
        confirm.pressed = action_mask(Action::confirm);
        for (int step = 0; step < 5; ++step) {
            controller.update(confirm, {0, 1});
        }
        InputSnapshot text;
        text.text_input = entry.text;
        controller.update(text, {0, 1});
        if (entry.code == cheats::Code::disco) {
            require(controller.update(confirm, {0, 1}) == std::vector{Cue::confirm} && controller.screen() == Screen::car_selection && controller.player_name() == entry.text
                        && !controller.take_cheats_changed(),
                "Rejected original codes must remain ordinary driver names");
        } else {
            cheats::State expected;
            (void)cheats::apply(expected, entry.code);
            require(controller.update(confirm, {0, 1}) == std::vector{Cue::cheat_activated} && controller.screen() == Screen::name_entry && controller.player_name().empty()
                        && controller.cheats_state() == expected && controller.take_cheats_changed(),
                "Accepted typed codes must preserve original state and activation speech");
            controller.update(text, {0, 1});
            (void)cheats::apply(expected, entry.code);
            require(controller.update(confirm, {0, 1}) == std::vector{Cue::option} && controller.cheats_state() == expected,
                "The second typed redemption must preserve original side effects without speech");
        }
    }
}

void championship_controls() {
    using namespace toca;
    using namespace toca::frontend;
    Controller controller;
    controller.set_name_defaults({"DRIVER 1", "PILOTE 1", "FAHRER 1", "PILOTA 1", "PILOTO 1"});
    const MenuInput press{controller};
    controller.update({}, {0, 5'000'000'000});
    controller.frontend_ready();
    press(Action::confirm);
    press(Action::confirm);
    press(Action::down);
    require(controller.item_available(1) && press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == Screen::championship_menu,
        "Championship must be reachable through the ordinary race menu");
    press(Action::down);
    require(controller.item_available(1) && press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == Screen::championship_load_slots
                && !controller.take_championship_request(),
        "Load without saved games must open the original empty-save presentation without a resume request");
    press(Action::confirm);
    require(controller.screen() == Screen::championship_menu && controller.selection() == 1, "No Saved Games must return to the Load Game row");
    press(Action::up);
    press(Action::confirm);
    require(controller.screen() == Screen::championship_options && controller.selection() == 1 && !controller.setup().championship_full,
        "New championship must show the original Short/Full and Continue options");
    press(Action::up);
    require(press(Action::right) == std::vector{Cue::option} && controller.setup().championship_full, "Championship length must use ordinary option cues");
    press(Action::down);
    press(Action::confirm);
    require(controller.screen() == Screen::name_entry, "Championship must skip Single Race's difficulty screen");
    controller.update({.text_input = "CMMICRO"}, {0, 1});
    require(
        press(Action::confirm) == std::vector{Cue::cheat_activated} && controller.screen() == Screen::name_entry && cheats::active(controller.cheats_state(), cheats::Code::micro),
        "Championship must accept the original permitted camera code");
    press(Action::back);
    require(controller.screen() == Screen::championship_options && controller.selection() == 1, "Name Escape must restore championship options");
    press(Action::confirm);
    press(Action::confirm);
    require(controller.screen() == Screen::car_selection, "Championship driver confirmation must enter the original garage");
    auto extras = controller.cheats_state();
    cheats::set_menu_active(extras, cheats::Code::bonus_cars, true);
    controller.set_cheats_state(extras);
    for (int car = 0; car < 8; ++car) {
        press(Action::right);
    }
    require(controller.car_id() == 8 && !controller.car_available() && press(Action::confirm) == std::vector{Cue::unavailable},
        "Championship garage must reject a saved GONGOGO override that is forbidden in mode1");
    extras.progress.bonus_car_8 = true;
    controller.set_cheats_state(extras);
    require(controller.car_available(), "Championship must retain genuinely earned bonus cars");
    for (int car = 0; car < 8; ++car) {
        press(Action::left);
    }
    press(Action::right);
    press(Action::confirm);
    require(controller.screen() == Screen::car_selection && controller.take_championship_request() == ChampionshipRequest::start && !controller.take_championship_request()
                && controller.stored_car_id() == 1,
        "Garage confirmation must request a scheduled championship round instead of free track selection");
    race::ChampionshipState saved;
    saved.full_length = true;
    saved.transmission_manual = true;
    saved.player_car = 1;
    saved.local_actor = 2;
    saved.driver = "DRIVER 1";
    saved.meeting = 6;
    saved.race = 1;
    controller.begin_championship_round(saved, 6, 25, true);
    require(controller.screen() == Screen::scene_loading && controller.track_id() == 6 && controller.setup().laps == 25 && controller.setup().qualifying
                && controller.setup().mode == GameMode::championship && controller.car_id() == 1 && controller.transmission_manual() && controller.player_name() == "DRIVER 1",
        "The saved championship must determine scene, track, laps, car and driver");
    controller.scene_ready();
    saved.phase = race::ChampionshipPhase::results;
    saved.points[2] = 16;
    controller.set_championship_state(saved);
    controller.return_from_scene(true);
    require(controller.screen() == Screen::championship_overview && controller.championship_state()->points[2] == 16,
        "Completed championship races must show actual accumulated points");
    require(press(Action::right) == std::vector{Cue::navigation} && controller.selection() == 1, "Standings must expose all sixteen drivers across two pages");
    press(Action::confirm);
    require(controller.take_championship_request() == ChampionshipRequest::advance, "Standings Continue must request the next championship phase");
    saved.phase = race::ChampionshipPhase::eliminated;
    controller.set_championship_state(saved);
    press(Action::confirm);
    require(controller.screen() == Screen::main_menu && !controller.take_championship_request(), "Elimination must end the championship without offering a retry");
    press(Action::confirm);
    require(controller.selection() == 1, "Race menu must retain Championship as the chosen mode");
    press(Action::confirm);
    press(Action::down);
    ChampionshipSlots slots;
    saved.phase = race::ChampionshipPhase::race;
    slots[1].state = saved;
    slots[2].error = "CRC32 mismatch";
    auto other = saved;
    other.driver = "OTHER";
    slots[4].state = other;
    controller.set_championship_slots(false, slots);
    press(Action::confirm);
    require(controller.screen() == Screen::championship_load_slots && controller.selection() == 4, "Load Game must select the last occupied slot as in the original frontend");
    press(Action::up);
    require(controller.selection() == 1 && !controller.item_available(2), "Load navigation must skip empty and damaged slots without losing independent valid saves");
    press(Action::confirm);
    require(controller.take_championship_request() == ChampionshipRequest::resume && controller.championship_slot() == 1 && controller.championship_state() == saved,
        "Load confirmation must expose the selected slot's state instead of the previously active championship");
    controller.begin_championship_round(saved, 6, 25, true);
    controller.scene_ready();
    controller.return_from_scene();
    require(controller.screen() == Screen::main_menu && !controller.take_championship_request(), "Aborted championships must return to Main without awarding progression");
    saved.phase = race::ChampionshipPhase::qualifying;
    saved.podiums.fill(0);
    controller.set_championship_state(saved);
    race::ChampionshipHonors honors;
    honors.block = 1;
    honors.tier = 3;
    honors.podiums = {4, 1, 1};
    controller.show_championship_save_prompt(honors);
    require(controller.selection() == 0 && controller.screen() == Screen::championship_save_prompt, "Original Save Game prompt must default to Yes");
    press(Action::confirm);
    require(controller.screen() == Screen::championship_save_slots && controller.selection() == 1, "Save must retain the last selected named slot");
    press(Action::confirm);
    require(controller.screen() == Screen::championship_save_overwrite && controller.selection() == 1 && !controller.take_championship_request(),
        "An occupied slot must require overwrite confirmation with No selected before requesting a write");
    press(Action::confirm);
    require(controller.screen() == Screen::championship_save_feedback && !controller.championship_saved() && !controller.take_championship_request(),
        "Overwrite No must display original Save Cancelled without changing a slot");
    controller.update({.text_input = {}, .focused = false}, {0, 10'000'000'000});
    require(controller.screen() == Screen::championship_save_feedback, "Unfocused time must not consume save feedback or continue the championship");
    controller.update({}, {0, 1'999'999'999});
    require(controller.screen() == Screen::championship_save_feedback, "Save Cancelled must retain its full two active seconds");
    controller.update({}, {0, 1});
    require(controller.screen() == Screen::championship_save_prompt, "Save Cancelled must return to the original Save Game question");
    press(Action::confirm);
    press(Action::up); // occupied slot1 -> empty slot0.
    press(Action::confirm);
    require(controller.take_championship_request() == ChampionshipRequest::save && controller.championship_slot() == 0,
        "An empty slot must request saving directly without an overwrite prompt");
    controller.championship_save_result("disk full");
    require(controller.screen() == Screen::championship_save_slots && controller.selection() == 0 && controller.championship_state() == saved
                && !controller.championship_error().empty(),
        "Failed writes must retain the unsaved championship and selected slot for retry");
    press(Action::confirm);
    require(controller.take_championship_request() == ChampionshipRequest::save, "The same failed empty slot must remain retryable");
    controller.championship_save_result();
    require(controller.screen() == Screen::championship_save_feedback && controller.championship_saved() && controller.championship_slots()[0].state == saved,
        "Successful saving must show Game Saved OK and update only the named selected slot");
    controller.update({}, {0, 2'999'999'999});
    require(controller.screen() == Screen::championship_save_feedback, "Game Saved OK must retain its full three active seconds");
    controller.update({}, {0, 1});
    require(controller.screen() == Screen::championship_honors && controller.championship_honors() == honors && controller.championship_state()->podiums[0] == 0
                && !controller.take_championship_request(),
        "Save feedback must preserve the completed block's honors snapshot after the next block counters were reset");
    press(Action::confirm);
    require(controller.take_championship_request() == ChampionshipRequest::advance, "Closing honors must continue the championship after saving");
    controller.show_championship_save_prompt();
    press(Action::down);
    press(Action::confirm);
    require(controller.take_championship_request() == ChampionshipRequest::advance && controller.championship_slots()[1].state == slots[1].state,
        "Save Game No must continue directly while preserving existing named saves");
    controller.show_championship_save_prompt();
    press(Action::confirm);
    press(Action::confirm); // slot0 now occupied.
    press(Action::up);
    press(Action::confirm);
    require(controller.take_championship_request() == ChampionshipRequest::save, "Only explicit overwrite Yes may request replacing an occupied slot");
    controller.championship_save_result();
    controller.show_championship_save_prompt();
    press(Action::confirm);
    press(Action::up); // slot0 -> Cancel row5.
    require(controller.selection() == 5, "Save selection must include all five slots and an explicit Cancel row");
    press(Action::confirm);
    require(controller.screen() == Screen::championship_save_feedback && !controller.championship_saved() && !controller.take_championship_request(),
        "Explicit Cancel must show its original timed message without requesting a write");
    saved.phase = race::ChampionshipPhase::complete;
    saved.meeting = 11;
    saved.race = 1;
    controller.set_championship_state(saved);
    controller.show_championship_finale();
    require(controller.screen() == Screen::championship_finale_loading && press(Action::confirm).empty() && !controller.take_championship_request(),
        "Finale loading must not score another race or consume menu confirmation");
    controller.championship_finale_ready();
    require(controller.screen() == Screen::championship_finale && press(Action::back).empty() && controller.screen() == Screen::championship_finale,
        "The scene must own finale input timing rather than the ordinary menu");
    controller.return_from_championship_finale();
    require(controller.screen() == Screen::main_menu && controller.championship_state() == saved, "Leaving the finale must retain the completed season and return to Main");
}
} // namespace

int main() {
    return toca::frontend::test::run([] {
        local_two_player_controls();
        toca::frontend::test::local_car_boost_controls();
        extras_controls();
        typed_code_controls();
        toca::frontend::test::time_trial_controls();
        championship_controls();
    });
}
