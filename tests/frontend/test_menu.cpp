#include "frontend/font.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "menu_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace {
using namespace toca::frontend::test;

void menu_transition_controls() {
    using namespace toca;
    using namespace toca::frontend;
    for (const auto rate : {30u, 60u, 144u}) {
        Controller controller;
        controller.update({}, {0, 5'000'000'000});
        require(controller.animation_time_ns() == 0, "Title and copyright must not advance the active frontend animation clock");
        controller.frontend_ready();
        std::uint64_t previous = 0;
        for (std::uint64_t sample = 1; sample <= rate; ++sample) {
            const auto now = sample * 1'000'000'000 / rate;
            controller.update({}, {now, now - previous});
            previous = now;
        }
        require(controller.animation_time_ns() == 1'000'000'000, "Frontend animation time must retain exact elapsed time at every presentation rate");
        controller.update({.text_input = {}, .focused = false}, {0, 9'000'000'000});
        require(controller.animation_time_ns() == 1'000'000'000, "Unfocused menus must freeze their animation clock");
        const MenuInput press{controller};
        press(Action::confirm);
        require(controller.screen() == Screen::main_menu && controller.menu_transition_direction() == 1, "Initial language confirmation must select the original forward roll");
        press(Action::confirm);
        require(controller.screen() == Screen::race_menu && controller.menu_transition_direction() == 1,
            "Immediate subsequent confirmation must retain menu input and select the forward roll");
        press(Action::back);
        require(controller.screen() == Screen::main_menu && controller.menu_transition_direction() == -1, "Escape must select the reverse menu roll without delaying input");
        const auto entered = controller.menu_entered_ns();
        require(entered == controller.animation_time_ns(), "A page turn must start at the input event time rather than its first rendered frame");
        press(Action::down);
        require(controller.menu_transition_direction() == -1 && controller.menu_entered_ns() == entered,
            "Selection-only navigation must not replace the last page transition direction or restart its clock");
    }
    for (const int direction : {-1, 1}) {
        for (std::uint32_t step = 0; step < 14; ++step) {
            const auto elapsed = (std::uint64_t(step) * 1'000'000'000 + 29) / 30;
            const auto phase = menu_transition_phase(elapsed, direction);
            require(phase.active && phase.frame == std::uint32_t((direction * int(step) + 14) % 14), "Every forward and reverse roll phase must follow the original14-frame cycle");
        }
        require(menu_transition_phase(466'666'666, direction).active && !menu_transition_phase(466'666'667, direction).active,
            "A complete roll must finish at fourteen exact30Hz ticks");
    }
    require(menu_transition_phase(0, 1, true).frame == 5 && menu_transition_phase(299'999'999, 1, true).active && !menu_transition_phase(300'000'000, 1, true).active,
        "The first frontend roll must start at frame5 and finish after nine30Hz ticks");
    require(menu_transition_phase(UINT64_MAX, 1).frame == 0 && !menu_transition_phase(UINT64_MAX, 1).active,
        "An expired roll must remain stable even after an arbitrarily long elapsed interval");
    rejects(
        [] {
            (void)menu_transition_phase(0, 0);
        },
        "A zero roll direction must be rejected");
    const MenuQuad trapezoid{{{{10, 20}, {30, 100}, {170, 300}, {210, 400}}}};
    const auto center = menu_transition_point(trapezoid, .5f, .5f);
    const auto left = menu_transition_point(trapezoid, 0, .25f);
    require(center.x == 105 && center.y == 60 && left.x == 15 && left.y == 40,
        "Roll contents must interpolate horizontal scanline edges while taking destination y only from the left edge");
}

void synthetic() {
    using namespace toca;
    using namespace toca::frontend;
    menu_transition_controls();
    LapRecords saved;
    for (auto& record : saved) {
        record = {599990, "DEFAULT", 0, ""};
    }
    saved[3] = {95019, "DANIEL", 1, "ABC123"};
    Controller records;
    records.set_lap_records(saved, 3);
    require(records.records() && *records.records() == saved && records.record_highlight_track() == 3,
        "Frontend records must retain persisted time, driver, badge and verification code with the improved track");
    records.update({}, {0, 5'000'000'000});
    records.frontend_ready();
    const MenuInput record_press{records};
    record_press(Action::confirm);
    record_press(Action::down);
    record_press(Action::down);
    record_press(Action::confirm);
    require(records.screen() == Screen::time_trial_records && records.selection() == 0 && !records.record_highlight_track() && *records.records() == saved,
        "Main High Scores must preserve saved records and clear the previous race's transient highlight");
    for (const auto action : {Action::up, Action::down, Action::left, Action::right}) {
        require(record_press(action).empty() && records.selection() == 0, "Record pages must ignore source-unhandled arrows");
    }
    record_press(Action::confirm);
    require(records.selection() == 1 && records.screen() == Screen::time_trial_records, "Record Enter must expose tracks five through nine");
    record_press(Action::confirm);
    require(records.screen() == Screen::championship_records && records.selection() == 0 && screen_name(records.screen()) == "championship_records",
        "Main's second Time Trial page must continue to the original nine-row Championship table");
    ChampionshipRecords championship_records;
    for (auto& record : championship_records) {
        record = {"DEFAULT", 1, 1, {}};
    }
    championship_records[0] = {"GOLD", 321, 24, {3, 2, 1, 0}};
    records.set_championship_records(championship_records, "read failed");
    require(records.championship_records() && *records.championship_records() == championship_records && records.championship_records_error() == "read failed",
        "Championship frontend must retain the shared table and its explicit storage error without inventing a verification code");
    records.set_championship_records(championship_records);
    require(records.championship_records_error().empty(), "A repaired championship table must clear its prior load notice");
    for (const auto action : {Action::up, Action::down, Action::left, Action::right}) {
        require(record_press(action).empty() && records.selection() == 0, "Championship records must ignore source-unhandled arrows");
    }
    require(records.championship_record_color() == 0x61, "The first Championship row must start with the original initial highlight");
    records.update({}, {0, 1'133'333'329}); // Four arrow polls above contribute4ns.
    require(records.championship_record_color() == 0x61, "The first row must retain97 until the second seventeen-call color update");
    records.update({}, {0, 1});
    require(records.championship_record_color() == 0x5d, "The second source flash update must select palette93");
    records.update({.text_input = {}, .focused = false}, {0, 1'700'000'000});
    require(records.championship_record_color() == 0x5d, "Unfocused championship records must freeze their source highlight timing");
    records.update({}, {0, 566'666'666});
    require(records.championship_record_color() == 0x28, "The third source flash update must select palette40 independently of presentation frequency");
    require(record_press(Action::back) == std::vector{Cue::back} && records.screen() == Screen::time_trial_records && records.selection() == 1,
        "Championship records Escape must return to Time Trial page two with the ordinary back cue");
    require(record_press(Action::confirm) == std::vector{Cue::confirm} && records.screen() == Screen::championship_records && records.championship_record_color() == 0x61,
        "Reopening Championship records must retain the original initial row draw and confirmation cue");
    require(record_press(Action::confirm) == std::vector{Cue::confirm} && records.screen() == Screen::main_menu && records.selection() == 2,
        "Championship record completion must restore Main High Scores");
    Controller controller;
    rejects(
        [&] {
            controller.frontend_ready();
        },
        "Loading acknowledgement must follow copyright");
    InputSnapshot input;
    input.focused = false;
    input.any_key_pressed = true;
    controller.update(input, {0, 1});
    require(controller.screen() == Screen::title, "Inactive title input must not dismiss the title");
    input.focused = true;
    input.any_key_pressed = true;
    require(controller.update(input, {10, 1}).empty() && controller.screen() == Screen::copyright, "Any keyboard key must leave the silent title for copyright");
    input.any_key_pressed = false;
    input.pressed = action_mask(Action::confirm);
    require(controller.update(input, {20, 10}).empty() && controller.screen() == Screen::copyright, "Loading input must not skip resource initialization");
    controller.frontend_ready();
    require(controller.screen() == Screen::language && controller.selection() == 0, "Language must follow completed frontend initialization");
    input.pressed = action_mask(Action::up);
    require(controller.update(input, {30, 10}) == std::vector{Cue::navigation} && controller.selection() == 2, "Language navigation must wrap only the three enabled rows");
    input.pressed = 0;
    input.held = action_mask(Action::up);
    require(controller.update(input, {40, 10}).empty() && controller.selection() == 2, "Held input must not repeat menu edges");
    input.pressed = action_mask(Action::confirm);
    require(controller.update(input, {50, 10}) == std::vector{Cue::confirm} && controller.screen() == Screen::main_menu && controller.language() == 2,
        "Language confirmation must enter the localized main menu once");
    input.pressed = action_mask(Action::confirm) | action_mask(Action::up);
    require(controller.update(input, {60, 10}) == std::vector{Cue::confirm} && controller.screen() == Screen::race_menu && controller.selection() == 0,
        "Confirm must precede navigation and enter the race menu");
    input.pressed = action_mask(Action::back);
    require(controller.update(input, {65, 10}) == std::vector{Cue::back} && controller.screen() == Screen::main_menu, "Race menu must return to main");
    input.pressed = action_mask(Action::back);
    require(controller.update(input, {70, 10}).empty() && controller.screen() == Screen::main_menu, "Main menu source back-state must remain the main menu");
    input.pressed = action_mask(Action::up);
    controller.update(input, {80, 10});
    input.pressed = action_mask(Action::confirm);
    require(controller.update(input, {90, 10}) == std::vector{Cue::confirm} && controller.quit_requested(), "Original Quit must exit directly");
    Controller route;
    route.set_name_defaults({"DRIVER 1", "PILOTE 1", "FAHRER 1", "PILOTA 1", "PILOTO 1"});
    const MenuInput press{route};
    route.update({}, {0, 5'000'000'000});
    route.frontend_ready();
    press(Action::confirm); // language -> main
    press(Action::confirm); // main -> race
    for (std::size_t index = 1; index < 4; ++index) {
        press(Action::down);
        if (index == 1 || index == 2) {
            require(route.item_available(index), "Championship and Time Trial must be available race modes");
            continue;
        }
        require(route.selection() == index && press(Action::confirm) == std::vector{Cue::confirm} && route.screen() == Screen::network_linkup,
            "Linkup must open the native host/join menu");
        require(press(Action::back) == std::vector{Cue::back} && route.screen() == Screen::race_menu && route.selection() == 3, "Linkup Back must retain the race menu selection");
    }
    press(Action::down);
    press(Action::confirm);
    require(route.screen() == Screen::single_options && route.selection() == 4 && route.setup().laps == 3 && route.setup().qualifying && route.setup().weather == 0,
        "Single race must start on Continue with canonical defaults");
    press(Action::down); // players
    require(press(Action::right) == std::vector{Cue::option} && route.setup().players == 2 && !route.setup().qualifying,
        "Two players must disable qualifying using the original local race rule");
    require(press(Action::left) == std::vector{Cue::option} && route.setup().players == 1 && route.setup().qualifying,
        "Returning to one player must restore the selected qualifying choice");
    press(Action::down); // laps
    require(press(Action::left) == std::vector{Cue::option} && route.setup().laps == 2, "Lap adjustment must use source choices");
    for (int index = 0; index < 6; ++index) {
        press(Action::right);
    }
    require(route.setup().laps == 1, "Lap choices must wrap from twenty to one");
    press(Action::down); // qualify
    require(press(Action::confirm) == std::vector{Cue::option} && !route.setup().qualifying && route.screen() == Screen::single_options,
        "Confirm on an option must change it rather than advance");
    press(Action::down); // weather
    press(Action::left);
    require(route.setup().weather == 5, "Weather must wrap across six source choices");
    press(Action::down);
    press(Action::confirm);
    require(route.screen() == Screen::difficulty && route.selection() == 1, "Canonical difficulty default must be Medium");
    press(Action::up);
    press(Action::confirm);
    require(route.screen() == Screen::name_entry && route.setup().difficulty == 1 && route.player_name() == "DRIVER 1",
        "Difficulty confirmation must enter the localized name editor before committing difficulty at Car entry");
    press(Action::backspace);
    require(route.player_name() == "DRIVER ", "First Backspace must erase only the last character");
    InputSnapshot text;
    text.text_input = "a!2";
    require(route.update(text, {0, 1}) == std::vector{Cue::character, Cue::character} && route.player_name() == "DRIVER A2",
        "Name editor must filter, uppercase and bound text to nine characters");
    press(Action::back); // cancel edits
    press(Action::confirm); // re-enter name
    require(route.player_name() == "DRIVER 1", "Cancelling the editor must discard unaccepted text");
    text.text_input = "ab12!?";
    require(route.update(text, {0, 1}).size() == 4 && route.player_name() == "AB12", "First accepted text must replace the old name");
    press(Action::confirm);
    require(route.screen() == Screen::car_selection && route.car_id() == 0 && !route.transmission_manual(), "Car selection must start at Honda with automatic transmission");
    press(Action::left);
    require(route.car_id() == 0, "Car selection must not wrap left");
    press(Action::right);
    press(Action::up);
    require(route.car_id() == 1 && route.transmission_manual(), "Car and transmission must change independently");
    for (int index = 0; index < 10; ++index) {
        press(Action::right);
    }
    require(route.car_id() == 9 && !route.car_available() && press(Action::confirm) == std::vector{Cue::unavailable}, "Locked bonus cars must not advance");
    press(Action::back);
    require(route.screen() == Screen::name_entry && route.player_name() == "AB12", "Garage Back must return to accepted player name");
    for (int index = 0; index < 4; ++index) {
        press(Action::backspace);
    }
    press(Action::confirm);
    require(route.player_name() == "DRIVER 1" && route.car_id() == 0, "Empty confirmation must restore localized default and uncommitted car must be discarded");
    press(Action::right);
    require(route.stored_car_id() == 0 && route.car_id() == 1, "Browsing must not commit the selected car");
    require(press(Action::confirm) == std::vector{Cue::confirm} && route.screen() == Screen::track_selection && route.stored_car_id() == 1,
        "Car confirmation must commit before entering Track");
    require(route.track_id() == 0 && route.stored_track_id() == 0 && route.track_available() && route.track_preview_rotation() == 0x800,
        "Track must start at available Donington with its source orientation");
    require(press(Action::left).empty() && route.track_id() == 0 && press(Action::up).empty(), "Track navigation must not wrap or interpret Up as a selection");
    for (const auto hz : {60u, 144u}) {
        auto preview = route;
        const auto start = preview.track_preview_presentation_rotation();
        auto last = start;
        std::uint64_t previous_ns = 0;
        for (std::uint32_t image = 1; image <= 4 * hz; ++image) {
            const auto now = std::uint64_t(image) * 1'000'000'000 / hz;
            preview.update({}, {now, now - previous_ns});
            const auto rendered = preview.track_preview_presentation_rotation();
            const auto expected = std::fmod(double(start) - 1020 * double(now) / 1'000'000'000 + 4096, 4096);
            require(std::abs(double(rendered) - expected) < .001, "Track miniature must rotate smoothly at the source speed between30Hz ticks at60/144Hz");
            auto difference = rendered - last;
            if (difference > 2048) {
                difference -= 4096;
            }
            require(
                std::abs(difference + 1020.f / float(hz)) < .001, "Track miniature must retain constant angular displacement through presentation frames and source-angle wrap");
            require(preview.track_preview_presentation_rotation() == rendered, "Track miniature rendering must not advance its source phase");
            last = rendered;
            previous_ns = now;
        }
        require(preview.track_preview_rotation() == ((0x800u - 34u * 120u) & 0xfffu), "Fractional preview drawing must preserve exact30Hz source ticks");
        preview.update({.text_input = {}, .focused = false}, {0, 10'000'000'000});
        require(preview.track_preview_presentation_rotation() == last, "Unfocused track preview must freeze its fractional render angle");
    }
    route.update({}, {0, 1'000'000'000});
    require(route.track_preview_rotation() == ((0x800u - 34u * 30u) & 0xfffu), "Preview rotation must advance at the source 30 Hz rate");
    const auto rotation = route.track_preview_rotation();
    InputSnapshot track_inactive;
    track_inactive.focused = false;
    route.update(track_inactive, {0, 1'000'000'000});
    require(route.track_preview_rotation() == rotation, "Unfocused dispatch must pause the track miniature");
    auto unlocked = route.cheats_state();
    cheats::set_menu_active(unlocked, cheats::Code::all_tracks, true);
    route.set_cheats_state(unlocked);
    for (std::uint32_t track = 1; track < 10; ++track) {
        require(press(Action::right) == std::vector{Cue::navigation} && route.track_id() == track && route.track_available() && route.track_preview_rotation() == rotation,
            "The direct tracks switch must make every implemented track available without changing preview rotation");
    }
    require(press(Action::right).empty() && route.track_id() == 9 && route.track_max_id() == 9, "Track navigation must stop at Lavaland");
    require(press(Action::back) == std::vector{Cue::back} && route.screen() == Screen::car_selection && route.stored_track_id() == 9 && route.car_id() == 1,
        "Track Back must store the preview and restore the committed car");
    press(Action::right); // browse another car without accepting it
    press(Action::back);
    press(Action::confirm);
    require(route.car_id() == 1 && route.stored_car_id() == 1, "Name-to-Car must restore the last committed car after unconfirmed browsing");
    press(Action::confirm);
    require(route.screen() == Screen::track_selection && route.track_id() == 9 && route.track_available() && route.track_preview_rotation() == 0x800,
        "Track re-entry must restore its stored preview and reset the source orientation");
    press(Action::back);
    press(Action::back);
    press(Action::back);
    press(Action::back);
    require(route.screen() == Screen::single_options && route.selection() == 4, "Back chain must return through Name and Difficulty to Continue");
    press(Action::back);
    press(Action::back);
    require(route.screen() == Screen::main_menu, "Single options and Race Back must reach Main");
    for (int index = 0; index < 6; ++index) {
        press(Action::confirm);
    }
    require(route.screen() == Screen::track_selection, "Scene route must pass through the normal menus");
    for (int index = 0; index < 9; ++index) {
        press(Action::left);
    }
    require(press(Action::confirm) == std::vector{Cue::confirm} && route.screen() == Screen::scene_loading && route.stored_track_id() == 0,
        "Implemented Donington scene must commit the selected track and enter loading");
    require(press(Action::back).empty() && route.screen() == Screen::scene_loading, "Frontend input must not bypass the actual loading boundary");
    route.scene_ready();
    require(route.screen() == Screen::scene && press(Action::confirm).empty(), "Scene input must not re-enter frontend confirmation");
    route.return_from_scene();
    require(route.screen() == Screen::main_menu && route.stored_car_id() == 1 && route.stored_track_id() == 0 && route.language() == 0,
        "Scene teardown must restore the main menu without discarding player choices");
    for (std::uint32_t track = 1; track < 10; ++track) {
        for (int index = 0; index < 6; ++index) {
            press(Action::confirm);
        }
        require(route.screen() == Screen::track_selection && route.track_id() == track - 1 && route.track_available(), "Scene return must retain the committed track selection");
        press(Action::right);
        require(press(Action::confirm) == std::vector{Cue::confirm} && route.screen() == Screen::scene_loading && route.stored_track_id() == track,
            "Every track must commit and enter the shared scene loader");
        route.scene_ready();
        route.return_from_scene();
    }
    Controller cursor;
    cursor.set_name_defaults({"DRIVER 1", "PILOTE 1", "FAHRER 1", "PILOTA 1", "PILOTO 1"});
    cursor.update({}, {0, 5'000'000'000});
    cursor.frontend_ready();
    InputSnapshot edit_confirm;
    edit_confirm.pressed = action_mask(Action::confirm);
    for (int index = 0; index < 5; ++index) {
        cursor.update(edit_confirm, {0, 0});
    }
    require(cursor.screen() == Screen::name_entry && !cursor.name_cursor_visible(), "Original Name entry copies text without initially drawing a cursor");
    cursor.update({}, {0, 666'666'666});
    require(!cursor.name_cursor_visible(), "Initial cursor must remain hidden until the second ten-tick toggle");
    cursor.update({}, {0, 1});
    require(cursor.name_cursor_visible(), "Cursor must become visible after exactly twenty 30 Hz ticks");
    InputSnapshot inactive;
    inactive.focused = false;
    cursor.update(inactive, {0, 1'000'000'000});
    require(cursor.name_cursor_visible(), "Inactive dispatch must not advance the editor blink");
    cursor.update({}, {0, 333'333'333});
    require(!cursor.name_cursor_visible(), "Cursor must hide at thirty active ticks independently of frame chunk sizes");
    InputSnapshot edit;
    edit.text_input = "X";
    cursor.update(edit, {0, 0});
    require(cursor.name_cursor_visible(), "First accepted edit must draw the cursor immediately");
    cursor.update({}, {0, 333'333'334});
    require(cursor.name_cursor_visible(), "Editing while the blink flag is hidden must not reset that flag");
    cursor.update({}, {0, 333'333'333});
    require(!cursor.name_cursor_visible(), "The following original blink transition must hide the edited cursor");
    edit.text_input.clear();
    edit.pressed = action_mask(Action::backspace);
    cursor.update(edit, {0, 0});
    require(cursor.player_name().empty() && cursor.name_cursor_visible(), "Backspace must draw the cursor after deleting the last character");
    cursor.update(edit_confirm, {0, 0});
    require(!cursor.name_cursor_visible(), "Cursor must never draw outside Name entry");
    Controller timed;
    input = {};
    timed.update(input, {0, 4'999'999'999});
    require(timed.screen() == Screen::title, "Original title must remain before five seconds");
    timed.update(input, {1, 1});
    require(timed.screen() == Screen::copyright, "Original title timeout must be five seconds");
    Controller inactive_timeout;
    input.focused = false;
    inactive_timeout.update(input, {0, 5'000'000'000});
    require(inactive_timeout.screen() == Screen::copyright, "Title timeout must follow elapsed time while unfocused");
    std::cout << "Frontend startup, single race selection/back/editing passed\n";
}
} // namespace

int main() {
    return toca::frontend::test::run([] {
        synthetic();
    });
}
