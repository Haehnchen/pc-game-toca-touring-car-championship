#include "assets/store.hpp"
#include "scene/pause.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

toca::scene::PauseUpdate press(toca::scene::PauseController& controller, toca::Action action) {
    return controller.update({.pressed = toca::action_mask(action), .text_input = {}});
}

void controller() {
    using namespace toca;
    using namespace toca::scene;
    PauseController pause;
    pause.open(true, {});
    expect(pause.active() && pause.row_count() == 4 && pause.selection() == 0, "Qualifying pause must expose its original fourth row");
    auto result = press(pause, Action::up);
    expect(pause.selection() == 3 && result.cues == std::vector{PauseCue::navigation}, "Original pause navigation must wrap and play sample78");
    result = press(pause, Action::confirm);
    expect(pause.page() == PausePage::race_again && pause.selection() == 0 && result.action == PauseAction::none, "End Race must open original RACE AGAIN? with YES selected");
    result = press(pause, Action::back);
    expect(result.action == PauseAction::none && result.cues.empty() && pause.active() && pause.page() == PausePage::race_again && pause.selection() == 0,
        "RACE AGAIN must require an explicit Yes/No choice, preserving its selection on Escape");
    result = press(pause, Action::confirm);
    expect(result.action == PauseAction::restart_race && !pause.active(), "RACE AGAIN YES must request a session restart");
    pause.open(false, {});
    expect(pause.row_count() == 3, "Grid pause must omit End Qualifying Session");
    press(pause, Action::up);
    press(pause, Action::confirm);
    press(pause, Action::down);
    expect(press(pause, Action::confirm).action == PauseAction::end_race, "RACE AGAIN NO must leave the session");
    pause.open(true, {}, GameMode::linkup);
    expect(!pause.qualifying() && pause.row_count() == 3, "Linkup never exposes qualifying controls");
    press(pause, Action::up);
    result = press(pause, Action::confirm);
    expect(result.action == PauseAction::end_race && !pause.active(), "Linkup End Race must leave directly without offering a local restart");
    pause.open(true, {});
    press(pause, Action::down);
    press(pause, Action::down);
    expect(press(pause, Action::confirm).action == PauseAction::end_qualifying && !pause.active(), "End Qualifying must hand off immediately without a confirmation");
    pause.open(true, {.master_gain = .75f, .muted = true, .effects_volume = 100, .music_volume = 50, .game_music = true});
    press(pause, Action::down);
    press(pause, Action::confirm);
    expect(pause.page() == PausePage::sound && pause.selection() == 0, "Sound Options must enter the effects row");
    expect(!press(pause, Action::right).audio_changed, "Maximum effects volume must remain bounded");
    for (int index = 0; index < 10; ++index) {
        result = press(pause, Action::left);
        expect(result.audio_changed && result.cues == std::vector{PauseCue::option}, "Every effective ten-point adjustment must play sample80");
    }
    expect(pause.audio().effects_volume == 0 && !press(pause, Action::left).audio_changed, "Effects volume must reach OFF without underflow");
    press(pause, Action::down);
    press(pause, Action::right);
    expect(pause.audio().music_volume == 60 && pause.audio().effects_volume == 0 && pause.audio().master_gain == .75f && pause.audio().muted,
        "Music and effects must adjust independently while preserving native master/mute settings");
    press(pause, Action::down);
    expect(press(pause, Action::left).audio_changed && !pause.audio().game_music, "Left must toggle original in-game music OFF");
    expect(press(pause, Action::right).audio_changed && pause.audio().game_music, "Right must toggle original in-game music ON");
    expect(!press(pause, Action::confirm).audio_changed, "Original Enter must leave an adjustment row unchanged");
    press(pause, Action::down);
    press(pause, Action::confirm);
    expect(pause.page() == PausePage::main && pause.selection() == 0, "DONE must return to Continue");
    press(pause, Action::down);
    press(pause, Action::confirm);
    result = press(pause, Action::back);
    expect(result.action == PauseAction::resume && result.cues.empty(), "Escape must resume directly from a submenu without a menu sound");
    pause.open(false, {});
    expect(pause.update({.pressed = action_mask(Action::down), .text_input = {}, .focused = false}).action == PauseAction::none && pause.selection() == 0,
        "An inactive window must not change the paused menu");
    pause.open(true, {}, GameMode::time_trial);
    expect(pause.mode() == GameMode::time_trial && !pause.qualifying() && pause.row_count() == 3, "Time Trial must omit End Qualifying even if the caller supplies qualifying");
    press(pause, Action::up);
    result = press(pause, Action::confirm);
    expect(pause.page() == PausePage::quit_time_trial && pause.selection() == 1 && result.action == PauseAction::none && result.cues == std::vector{PauseCue::confirm},
        "END TIME TRIAL must open its quit confirmation with NO selected and sample79");
    result = press(pause, Action::back);
    expect(pause.active() && pause.page() == PausePage::quit_time_trial && pause.selection() == 1 && result.action == PauseAction::none && result.cues.empty(),
        "QUIT TIME TRIAL must require an explicit Yes/No choice");
    result = press(pause, Action::confirm);
    expect(pause.active() && pause.page() == PausePage::main && pause.selection() == 0 && result.action == PauseAction::none && result.cues == std::vector{PauseCue::confirm},
        "QUIT TIME TRIAL NO must restore Continue while retaining the actual session");
    press(pause, Action::up);
    press(pause, Action::confirm);
    press(pause, Action::up);
    result = press(pause, Action::confirm);
    expect(result.action == PauseAction::end_time_trial && !pause.active(), "QUIT TIME TRIAL YES must request saving and leaving the trial");
    for (const bool qualifying : {false, true}) {
        pause.open(qualifying, {}, GameMode::championship);
        expect(pause.mode() == GameMode::championship && pause.qualifying() == qualifying && pause.row_count() == (qualifying ? 4u : 3u),
            "Championship pause must retain qualifying skip only during qualifying");
        press(pause, Action::up);
        result = press(pause, Action::confirm);
        expect(pause.page() == PausePage::quit_championship && pause.selection() == 1 && result.action == PauseAction::none && result.cues == std::vector{PauseCue::confirm},
            "END CHAMPIONSHIP must open its original confirmation with No selected and sample79");
        result = press(pause, Action::back);
        expect(pause.active() && pause.page() == PausePage::quit_championship && pause.selection() == 1 && result.action == PauseAction::none && result.cues.empty(),
            "Championship confirmation must require an explicit choice on Escape");
        result = press(pause, Action::confirm);
        expect(pause.active() && pause.page() == PausePage::main && pause.selection() == 0 && result.action == PauseAction::none,
            "Championship No must retain its current session and restore Continue");
        press(pause, Action::up);
        press(pause, Action::confirm);
        press(pause, Action::up);
        expect(press(pause, Action::confirm).action == PauseAction::end_championship && !pause.active(),
            "Championship Yes must request a checkpoint-preserving exit, never Race Again");
    }
    pause.open(true, {}, GameMode::championship);
    press(pause, Action::down);
    press(pause, Action::down);
    expect(press(pause, Action::confirm).action == PauseAction::end_qualifying && !pause.active(),
        "Championship End Qualifying must use the real classification transition without a quit confirmation");
    pause.open(false, {}, GameMode::time_trial);
    expect(press(pause, Action::back).action == PauseAction::resume, "Escape from Time Trial root must resume its current attempt");
}

void reference(const char* root) {
    using namespace toca;
    using namespace toca::scene;
    const AssetStore store(root);
    const auto english = load_pause_labels(store, 0), french = load_pause_labels(store, 1), german = load_pause_labels(store, 2);
    PauseController pause;
    pause.open(true, {});
    const auto main = pause_menu(english, pause);
    expect(main.title == "GAME PAUSED" && main.rows == std::vector<std::string>{"CONTINUE", "SOUND OPTIONS", "END QUALIFYING SESSION", "END RACE"},
        "Original English pause labels or order changed");
    expect(pause_menu(french, pause).title == "PARTIE EN PAUSE" && pause_menu(german, pause).rows[2] == "QUALIFIKATION BEENDEN", "Original pause localization changed");
    pause.open(false, {.effects_volume = 0, .music_volume = 30, .game_music = false});
    press(pause, Action::down);
    press(pause, Action::confirm);
    expect(pause_menu(english, pause).rows == std::vector<std::string>{"SOUND EFFECTS OFF", "MUSIC VOLUME 30", "IN GAME MUSIC OFF", "DONE"},
        "Original effects-off/music settings labels changed");
    press(pause, Action::down);
    press(pause, Action::down);
    press(pause, Action::right);
    expect(pause_menu(english, pause).rows[2] == "IN GAME MUSIC ON", "Enabled music must use original GSTRINGS82");
    for (std::uint32_t language = 0; language < 5; ++language) {
        const auto labels = load_pause_labels(store, language);
        expect(!labels.lines[0].empty(), "All original languages must retain pause labels");
        pause.open(false, {}, GameMode::time_trial);
        expect(pause_menu(labels, pause).rows == std::vector<std::string>{labels.lines[1], labels.lines[2], labels.lines[4]},
            "Time Trial pause must use GSTRINGS28 instead of END RACE");
        press(pause, Action::up);
        press(pause, Action::confirm);
        expect(pause_menu(labels, pause).title == labels.quit_time_trial && !labels.quit_time_trial.empty(), "QUIT TIME TRIAL must use the localized GSTRINGS123");
        pause.open(true, {}, GameMode::championship);
        expect(pause_menu(labels, pause).rows == std::vector<std::string>{labels.lines[1], labels.lines[2], labels.lines[3], labels.lines[5]},
            "Championship qualifying pause must use localized GSTRINGS27 and29");
        press(pause, Action::up);
        press(pause, Action::confirm);
        const auto championship_quit = pause_menu(labels, pause);
        expect(championship_quit.title == labels.quit_championship && !labels.quit_championship.empty()
                   && championship_quit.rows == std::vector<std::string>{labels.lines[12], labels.lines[13]},
            "Championship confirmation must use GSTRINGS125 and the original localized Yes/No labels");
        if (language == 0) {
            expect(labels.lines[5] == "END CHAMPIONSHIP" && championship_quit.title == "QUIT CHAMPIONSHIP?", "English championship pause strings changed");
        }
        if (language == 0) {
            expect(labels.lines[4] == "END TIME TRIAL" && labels.quit_time_trial == "QUIT TIME TRIAL?", "English Time Trial pause strings changed");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        controller();
        if (argc == 2) {
            reference(argv[1]);
        }
        std::cout << "PASS: original pause actions, nested menus, separate audio controls and localized labels\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
