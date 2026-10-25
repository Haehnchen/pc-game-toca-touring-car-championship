#include "frontend/font.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "menu_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace {
using namespace toca::frontend::test;

void settings_controls() {
    using namespace toca;
    using namespace toca::frontend;
    Controller controller;
    Settings effective;
    effective.video = {1111, 777, false, true, 75};
    effective.graphics.detail = 3;
    effective.audio.master_gain = .65f;
    effective.audio.effects_volume = 95;
    effective.audio.music_volume = 5;
    controller.set_settings(effective, {{1280, 720}, {640, 480}, {640, 480}});
    const MenuInput press{controller};
    const auto accept_detail = [&] {
        const auto draft = controller.settings_values();
        auto immediate = effective;
        immediate.graphics.detail = draft.graphics.detail;
        require(controller.take_settings_request() == immediate && !controller.take_settings_request(),
            "Each detail edit must immediately request only quality, preserving effective display, split, audio and input settings");
        controller.settings_applied(immediate);
        effective = immediate;
        require(controller.settings_values() == draft && controller.settings_saved() && controller.settings_error().empty() && controller.selection() == 5,
            "Immediate detail acknowledgement must preserve pending display and split edits and the selected row");
    };
    controller.update({}, {0, 5'000'000'000});
    controller.frontend_ready();
    press(Action::confirm);
    press(Action::down);
    require(controller.item_available(1) && press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == Screen::settings,
        "Main Game Options must open the category hub");
    require(press(Action::left).empty() && press(Action::right).empty() && !controller.take_settings_request(), "The category hub must not change preferences");
    press(Action::up);
    require(controller.selection() == 3 && press(Action::confirm) == std::vector{Cue::back} && controller.screen() == Screen::main_menu && controller.selection() == 1,
        "The hub must wrap to Back and restore Main's Game Options row");
    press(Action::confirm);
    press(Action::down);
    require(press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == Screen::graphics_options, "Graphics must open from its hub category");
    press(Action::left);
    require(controller.settings_values().video.width == 640, "Resolution choices must include and sort the current arbitrary window size");
    press(Action::right);
    press(Action::right);
    require(controller.settings_values().video.width == 1280 && !controller.take_settings_request(), "Graphics edits must remain staged until Apply");
    for (int index = 0; index < 5; ++index) {
        press(Action::down);
    }
    require(press(Action::confirm) == std::vector{Cue::option} && controller.settings_values().graphics.detail == graphics_mega_detail,
        "Detail Enter must immediately select Mega above Very High");
    accept_detail();
    press(Action::down);
    require(press(Action::right) == std::vector{Cue::option} && controller.settings_values().graphics.split_vertical && !controller.take_settings_request(),
        "Split orientation must remain staged alongside display edits");
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::settings && controller.selection() == 1 && controller.settings_values() == effective,
        "Graphics Escape must discard display and split drafts while retaining immediately accepted quality");
    press(Action::confirm);
    press(Action::up);
    require(controller.selection() == 7, "Graphics navigation must wrap across eight rows");
    require(press(Action::confirm) == std::vector{Cue::back} && controller.screen() == Screen::settings, "Graphics Back must return to its hub");
    press(Action::confirm);
    press(Action::right);
    press(Action::down);
    press(Action::confirm);
    press(Action::down);
    press(Action::left);
    require(controller.settings_values().video.fullscreen && !controller.settings_values().video.vsync, "Enter and arrows must adjust display toggles");
    press(Action::down);
    press(Action::right);
    require(controller.settings_values().video.fps_limit == 90, "FPS choices must advance around a retained arbitrary limit");
    press(Action::left);
    require(controller.settings_values().video.fps_limit == 75, "An existing arbitrary FPS limit must remain selectable");
    press(Action::down);
    const auto before_apply = controller.settings_values();
    require(controller.selection() == 4 && press(Action::left).empty() && press(Action::right).empty() && controller.settings_values() == before_apply
                && !controller.take_settings_request(),
        "Apply must end the first graphics page and ignore value arrows without saving");
    press(Action::down);
    require(controller.selection() == 5 && controller.settings_values().graphics.detail == graphics_mega_detail,
        "Detail must start the second page and retain its immediately accepted preset");
    for (std::uint32_t detail = 0; detail < graphics_detail_count; ++detail) {
        require(press(Action::right) == std::vector{Cue::option} && controller.settings_values().graphics.detail == detail,
            "Detail Right must cycle through every preset from Mega back to Mega");
        accept_detail();
    }
    require(press(Action::left) == std::vector{Cue::option} && controller.settings_values().graphics.detail == 3, "Detail Left must return Mega to Very High");
    accept_detail();
    press(Action::right);
    accept_detail();
    press(Action::down);
    const auto before_split = controller.settings_values();
    require(controller.selection() == 6 && controller.item_available(6) && !before_split.graphics.split_vertical,
        "Split Screen must remain an adjustable row after Detail with its Horizontal default");
    bool vertical = false;
    for (const auto action : {Action::left, Action::right, Action::confirm}) {
        vertical = !vertical;
        auto expected = before_split;
        expected.graphics.split_vertical = vertical;
        require(press(action) == std::vector{Cue::option} && controller.selection() == 6 && controller.screen() == Screen::graphics_options
                    && controller.settings_values() == expected && !controller.take_settings_request(),
            "Split arrows and Enter must toggle only the staged orientation without applying settings");
    }
    press(Action::up);
    press(Action::left);
    accept_detail();
    press(Action::right);
    accept_detail();
    press(Action::down);
    const auto requested = controller.settings_values();
    press(Action::up);
    press(Action::up);
    require(press(Action::confirm) == std::vector{Cue::confirm} && controller.take_settings_request() == requested && !controller.take_settings_request()
                && controller.screen() == Screen::graphics_options && requested.audio == effective.audio,
        "Apply must emit one complete display/detail request, preserve audio and stay on Graphics");
    require(requested.graphics.detail == graphics_mega_detail && requested.graphics.split_vertical, "Apply must include Mega and the selected split orientation");
    effective = requested;
    effective.video.width = 1279;
    effective.video.height = 719;
    controller.settings_applied(effective);
    require(controller.settings_values() == effective && controller.settings_saved() && controller.settings_error().empty() && controller.selection() == 4,
        "Apply result must show actual display values and preserve the selected row");
    controller.settings_applied(effective, "Cannot write preferences");
    require(!controller.settings_saved() && controller.settings_error() == "Cannot write preferences" && controller.settings_values() == effective,
        "Failed saving must retain effective settings and expose the error");
    press(Action::down);
    press(Action::down);
    require(press(Action::confirm) == std::vector{Cue::option} && !controller.settings_values().graphics.split_vertical && controller.settings_error().empty()
                && !controller.settings_saved() && !controller.take_settings_request(),
        "A new split edit must clear stale save feedback and remain staged after Apply");
    press(Action::up);
    press(Action::confirm);
    require(controller.settings_values().graphics.detail == 0 && controller.settings_error().empty() && !controller.settings_saved(),
        "A new immediate detail edit must clear a stale result notice");
    effective.graphics.detail = 0;
    require(controller.take_settings_request() == effective && !controller.take_settings_request(), "Immediate detail must exclude the pending split edit");
    const auto failed_draft = controller.settings_values();
    controller.settings_applied(effective, "Cannot write quality");
    require(controller.settings_values() == failed_draft && controller.settings_error() == "Cannot write quality" && !controller.settings_saved(),
        "Failed immediate persistence must keep the accepted quality, pending split and visible error usable");
    press(Action::down);
    press(Action::down);
    press(Action::confirm);
    require(controller.settings_values() == effective && controller.settings_values().graphics.split_vertical && controller.take_settings_request() == effective
                && !controller.take_settings_request(),
        "Graphics Back must retain accepted quality, discard the split draft and retry failed persistence");
    controller.settings_applied(effective, "Still cannot write quality");
    press(Action::confirm);
    require(controller.settings_values() == effective && controller.settings_error() == "Still cannot write quality" && controller.take_settings_request() == effective
                && !controller.take_settings_request(),
        "Reentering Graphics must retain accepted quality and retry its failed save without restoring discarded drafts");
    controller.settings_applied(effective);
    InputSnapshot inactive;
    inactive.focused = false;
    inactive.pressed = action_mask(Action::right);
    require(controller.update(inactive, {0, 1}).empty() && controller.settings_values() == effective, "Unfocused settings input must be ignored");
    press(Action::left);
    require(controller.settings_values().video.width == 1111, "The effective display size must join the resolution choices after applying");
    press(Action::back);
    press(Action::up);
    require(press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == Screen::sound_options, "Sound must open from its hub category");
    const auto accept_sound = [&] {
        const auto values = controller.settings_values();
        require(controller.take_settings_request() == values && !controller.take_settings_request() && values.video == effective.video,
            "Each sound edit must request immediate application exactly once and preserve display settings");
        controller.settings_applied(values);
        effective = values;
    };
    require(press(Action::left) == std::vector{Cue::option} && controller.settings_values().audio.effects_volume == 80,
        "Original volume callbacks must use the truncated ten-point step counter for arbitrary saved values");
    accept_sound();
    press(Action::right);
    accept_sound();
    require(press(Action::right) == std::vector{Cue::option} && controller.settings_values().audio.effects_volume == 100, "Effects volume must clamp to 100");
    accept_sound();
    require(
        controller.selection() == 0 && press(Action::right).empty() && !controller.take_settings_request(), "Sound apply must preserve selection and clamped edits must not save");
    require(press(Action::confirm) == std::vector{Cue::option} && controller.settings_values().audio.effects_volume == 0, "Original volume Enter must cycle 100 to zero");
    accept_sound();
    require(press(Action::left).empty() && !controller.take_settings_request(), "Effects arrows must clamp at zero");
    for (int index = 0; index < 10; ++index) {
        press(Action::right);
    }
    accept_sound();
    press(Action::left);
    require(controller.settings_values().audio.effects_volume == 90, "Effects volume must step by ten original units");
    accept_sound();
    press(Action::down);
    press(Action::confirm);
    require(controller.settings_values().audio.music_volume == 10, "Original volume Enter must advance the truncated step for an arbitrary saved value");
    accept_sound();
    press(Action::left);
    require(controller.settings_values().audio.music_volume == 0, "Music volume must reach zero in its ten-point steps");
    accept_sound();
    require(press(Action::left).empty() && !controller.take_settings_request(), "Clamped music must not issue repeated save requests");
    press(Action::confirm);
    require(controller.settings_values().audio.music_volume == 10, "Enter must advance the selected sound value");
    accept_sound();
    press(Action::down);
    press(Action::right);
    require(!controller.settings_values().audio.game_music, "Sound must expose the separate in-game music switch");
    accept_sound();
    press(Action::down);
    press(Action::confirm);
    require(!controller.settings_values().audio.stereo_effects, "Sound must expose the original stereo/mono effects switch");
    accept_sound();
    require(controller.selection() == 3 && controller.settings_values().audio.master_gain == .65f && !controller.settings_values().audio.muted,
        "Original sound controls must preserve native master gain and mute preferences");
    controller.settings_applied(effective, "Disk full");
    require(
        press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::settings && controller.selection() == 0 && controller.take_settings_request() == effective,
        "Sound Escape must preserve immediate edits, retry failed persistence and restore its category");
    controller.settings_applied(effective, "Still cannot write preferences");
    press(Action::confirm);
    require(controller.screen() == Screen::sound_options && controller.settings_error() == "Still cannot write preferences" && controller.take_settings_request() == effective
                && !controller.take_settings_request(),
        "Reentering Sound must retain a failed retry notice and request persistence again with current values");
    controller.settings_applied(effective);
    press(Action::up);
    require(controller.selection() == 4 && press(Action::left).empty() && press(Action::right).empty(), "Sound Back must ignore value arrows");
    require(press(Action::confirm) == std::vector{Cue::back} && controller.selection() == 0 && !controller.take_settings_request(),
        "Sound Back must retain saved values without redundant writes");
    press(Action::down);
    press(Action::down);
    require(press(Action::confirm) == std::vector{Cue::confirm} && controller.screen() == Screen::controller_players && controller.selection() == 0,
        "Controller Setup must open the original preceding player selection");
    require(press(Action::up) == std::vector{Cue::navigation} && controller.selection() == 2 && !controller.take_settings_request(),
        "Controller player navigation must wrap to Back without changing settings");
    require(press(Action::confirm) == std::vector{Cue::back} && controller.screen() == Screen::settings && controller.selection() == 2,
        "Controller player Back must restore its hub category");
    press(Action::confirm);
    press(Action::confirm);
    require(controller.screen() == Screen::controller_options && controller.controller_player() == 0, "Player one must open its own controller options");
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::controller_players && controller.selection() == 0,
        "Player controls Escape must return to the player selection");
    require(press(Action::back) == std::vector{Cue::back} && controller.selection() == 2, "Controller player Escape must restore its hub category");
    require(press(Action::back) == std::vector{Cue::back} && controller.screen() == Screen::main_menu && controller.selection() == 1,
        "Hub Escape must restore Main's Game Options selection");
    require(screen_name(Screen::settings) == "settings" && screen_name(Screen::graphics_options) == "graphics_options" && screen_name(Screen::sound_options) == "sound_options"
                && screen_name(Screen::controller_players) == "controller_players" && screen_name(Screen::controller_options) == "controller_options"
                && screen_name(Screen::controller_native_options) == "controller_native_options",
        "Options screens must have distinct diagnostic names");
}
} // namespace

int main() {
    return toca::frontend::test::run([] {
        settings_controls();
    });
}
