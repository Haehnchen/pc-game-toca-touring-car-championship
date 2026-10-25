#include "assets/store.hpp"
#include "frontend/font.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "image/decode.hpp"
#include "menu_mode_checks.hpp"
#include "menu_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace {
using namespace toca::frontend::test;

void corpus(const char* path) {
    const toca::AssetStore store(path);
    const auto transition = toca::frontend::load_menu_transition_resources();
    const auto& stable = transition.frames[0];
    require(stable[0].corners[0].x == 33 && stable[0].corners[0].y == 61 && stable[0].corners[1].y == 421 && stable[0].corners[2].y == 420 && stable[1].corners[0].x == 49
                && stable[1].corners[0].y == 14 && stable[2].corners[0].x == 57 && stable[2].corners[0].y == 441,
        "Compiled roll tables must retain the canonical stable windows and content-only prerender mutation");
    const auto& first = transition.frames[1][0];
    const auto& reverse_face = transition.frames[7][0];
    const auto& final = transition.frames[13][0];
    require(first.corners[0].x == 16 && first.corners[0].y == 51 && first.corners[1].x == 53 && first.corners[1].y == 403 && reverse_face.corners[0].x == 98
                && reverse_face.corners[0].y == 180 && reverse_face.corners[3].x == 543 && reverse_face.corners[3].y == 179 && final.corners[2].x == 612
                && final.corners[3].x == 604 && final.corners[3].y == 59,
        "All three source correction ranges must apply before content scanline preparation");
    const auto archive = store.open_archive("data/frontend.bfp");
    for (std::size_t index = 24; index < 28; ++index) {
        const auto font = toca::frontend::decode_font(archive.member(index).bytes());
        require(font.find('A') && font.find('Z') && font.text_width("SELECT LANGUAGE") > 0, "Original frontend font is incomplete");
    }
    const auto menus = toca::frontend::load_menus(store);
    constexpr std::array<std::string_view, 5> mega_labels{"MEGA DETAIL", "DETAIL MEGA", "MEGA-DETAIL", "DETTAGLIO MEGA", "DETALLE MEGA"};
    for (std::size_t language = 0; language < mega_labels.size(); ++language) {
        require(menus.graphics_labels[language].detail_values[toca::graphics_mega_detail] == mega_labels[language] && !menus.graphics_labels[language].mega_help.empty(),
            "Mega must have its own label and help in every localization table");
    }
    const auto records = toca::frontend::load_default_lap_records();
    const auto record_resources = toca::frontend::load_record_resources();
    constexpr std::array<std::string_view, 10> drivers{"ANDY", "LES", "RICH", "SCOTT", "PAT", "JON", "PHIL", "BRYAN", "IAIN", "FIRELORD"};
    constexpr std::array<std::uint32_t, 10> record_cars{0, 1, 2, 3, 4, 5, 6, 7, 0, 1};
    for (std::size_t track = 0; track < records.size(); ++track) {
        require(records[track] == toca::frontend::LapRecord{599990, std::string(drivers[track]), record_cars[track], ""}, "Record defaults must match the canonical table");
    }
    require(record_resources.labels[0].track == "TRACK" && record_resources.labels[1].time == "TEMPS" && record_resources.labels[2].player == "SPIELER"
                && record_resources.tracks[5] == "DON.S." && record_resources.tracks[9] == "LAVA...",
        "Time Trial headers and track abbreviations must retain their original source tables");
    require(toca::frontend::record_time(599990) == "9:59.99" && toca::frontend::record_time(64039) == "1:04.03", "Record time must truncate milliseconds to original hundredths");
    time_trial_controls(&menus);
    local_car_boost_controls(&menus);
    require(menus.language[0].items.size() == 3 && menus.main[0].items.size() == 4, "Canonical menu row counts differ");
    require(menus.language[0].items[2].label == "DEUTSCH" && menus.main[2].title == "HAUPTMENUE", "Compiled localization must use its own executable tables");
    require(menus.main[0].items[3].icon == 7 && menus.language[0].items[0].icon == 8, "Compiled menu icon identities differ");
    require(menus.main[0].items[0].label != menus.main[1].items[0].label, "Frontend localization must not collapse languages");
    auto settings_font = toca::frontend::decode_font(archive.member(25).bytes());
    const auto help_font = toca::frontend::decode_font(archive.member(26).bytes());
    const auto check_glyphs = [](const toca::frontend::Font& font, std::string_view text) {
        for (const unsigned char character : text) {
            if (character != ' ' && !font.find(character)) {
                throw std::runtime_error("Original frontend font is missing glyph '" + std::string(1, static_cast<char>(character)) + "' in settings text: " + std::string(text));
            }
        }
    };
    auto menu_font = toca::frontend::decode_font(archive.member(24).bytes());
    toca::frontend::add_menu_parentheses(menu_font, help_font);
    toca::frontend::add_menu_parentheses(settings_font, help_font);
    const auto record_title_font = toca::frontend::decode_font(archive.member(27).bytes());
    constexpr std::array<std::string_view, 5> championship_titles{"CHAMPIONSHIP", "COURSE CHAMPIONNAT", "MEISTERSCHAFTSRENNEN", "GARA DI CAMPIONATO", "CARR. CAMPEONATO"};
    constexpr std::array<std::string_view, 5> championship_rounds{"ROUND", "MANCHE", "RUNDE", "GARA", "RECORRIDO"};
    for (std::size_t language = 0; language < championship_titles.size(); ++language) {
        const auto& labels = record_resources.championship_labels[language];
        require(labels.title == championship_titles[language] && labels.round == championship_rounds[language] && !labels.player.empty() && !labels.points.empty(),
            "Championship record headings must use their original five executable localization tables");
        check_glyphs(record_title_font, labels.title);
        check_glyphs(menu_font, labels.player);
        check_glyphs(menu_font, labels.points);
        check_glyphs(menu_font, labels.round);
    }
    for (std::size_t tier = 0; tier < 4; ++tier) {
        const auto medal = toca::decode_bmp(archive.member(18 + tier).bytes());
        require(medal->w == 16 && medal->h == 16 && medal->format == SDL_PIXELFORMAT_INDEX8,
            "Championship scores must retain the original gray, bronze, silver and gold sixteen-pixel cups");
    }
    for (const unsigned char character : {'(', ')'}) {
        require(menu_font.find(character) && menu_font.find(character)->height == menu_font.line_height && settings_font.find(character)
                    && settings_font.find(character)->height == settings_font.line_height,
            "Aspect parentheses must match the original menu-font height in both text sizes");
    }
    constexpr std::array<int, 5> graphics_rows{76, 144, 212, 280, 348};
    for (std::size_t row = 0; row < graphics_rows.size(); ++row) {
        require(toca::frontend::menu_row_y(5, row) == graphics_rows[row], "Five graphics rows must retain the original menu coordinates on each page");
    }
    require(graphics_rows.back() + menu_font.line_height < 401, "Five original icon rows must fit above the notices");
    std::array<std::string, 3> apply_labels;
    std::string layout_errors;
    const auto check_width = [&](std::uint32_t language, std::string_view context, std::string_view text, int width, int limit) {
        if (width >= limit) {
            constexpr std::array<std::string_view, 3> names{"English", "French", "German"};
            layout_errors +=
                std::string(names.at(language)) + " " + std::string(context) + ": " + std::to_string(width) + "px >= " + std::to_string(limit) + "px: " + std::string(text) + "\n";
        }
    };
    for (std::uint32_t language = 0; language < 3; ++language) {
        toca::frontend::Controller settings;
        toca::Settings values;
        values.video.width = 3840;
        values.video.height = 2160;
        settings.set_settings(values, {});
        settings.update({}, {0, 5'000'000'000});
        settings.frontend_ready();
        const MenuInput press{settings};
        for (std::uint32_t index = 0; index < language; ++index) {
            press(toca::Action::down);
        }
        press(toca::Action::confirm);
        press(toca::Action::down);
        press(toca::Action::confirm);
        const auto hub = toca::frontend::menu_for(menus, settings);
        require(hub.title == menus.main[language].items[1].label && hub.items.size() == 4 && !hub.compact,
            "Game Options must retain the original localized heading and four category rows");
        constexpr std::array<std::uint32_t, 4> hub_icons{33, 58, 20, 16};
        constexpr std::array<std::uint32_t, 5> sound_icons{19, 18, 18, 14, 16};
        for (std::size_t index = 0; index < hub.items.size(); ++index) {
            require(hub.items[index].icon == hub_icons[index], "Game Options must retain original category icons");
            check_glyphs(menu_font, hub.items[index].label);
            check_glyphs(help_font, hub.items[index].help);
            check_width(language, "Options category", hub.items[index].label,
                std::min(menu_font.text_width(hub.items[index].label), settings_font.text_width(hub.items[index].label)), 405);
            check_width(language, "Options help", hub.items[index].help, help_font.text_width(hub.items[index].help), 558);
        }
        if (language == 0) {
            require(hub.items[0].label == "SOUND SETUP" && hub.items[1].label == "GRAPHICS SETUP" && hub.items[2].label == "CONTROLLER SETUP" && hub.items[3].label == "BACK",
                "The original English Game Options category order must be retained");
        }
        constexpr std::array option_screens{toca::frontend::Screen::sound_options, toca::frontend::Screen::graphics_options, toca::frontend::Screen::controller_players};
        constexpr std::array<std::size_t, 3> row_counts{5, 8, 3};
        for (std::size_t category = 0; category < option_screens.size(); ++category) {
            press(toca::Action::confirm);
            const auto menu = toca::frontend::menu_for(menus, settings);
            require(settings.screen() == option_screens[category] && menu.items.size() == row_counts[category] && menu.title == hub.items[category].label,
                "Each options category must open its own localized submenu");
            require(!menu.compact, "Every Game Options submenu must retain the original full icon rows");
            for (std::size_t index = 0; index < menu.items.size(); ++index) {
                const auto& item = menu.items[index];
                const auto label = item.label + (item.suffix.empty() ? "" : " " + item.suffix);
                check_glyphs(menu_font, label);
                check_glyphs(settings_font, label);
                check_glyphs(help_font, item.help);
                check_width(language, "Options label", label, std::min(menu_font.text_width(label), settings_font.text_width(label)), 405);
                check_width(language, item.label, item.help, help_font.text_width(item.help), 558);
                if (category == 0) {
                    require(item.icon == sound_icons[index], "Sound Setup must preserve original icons");
                    if (index < 2) {
                        require(item.decrease_available && !item.increase_available, "Full original volume must expose only the decrease arrow");
                    }
                }
            }
            if (category == 0 && language == 0) {
                require(menu.items[0].label == "EFFECTS VOLUME 100" && menu.items[1].label == "CD MUSIC VOLUME 100" && menu.items[2].label == "GAME MUSIC ON"
                            && menu.items[3].label == "STEREO SFX" && menu.items[4].label == "BACK",
                    "Frontend Sound Setup must use canonical source labels and value formatting");
            }
            if (category == 0) {
                auto silent = values;
                silent.audio.effects_volume = 0;
                silent.audio.music_volume = 0;
                silent.audio.game_music = false;
                silent.audio.stereo_effects = false;
                settings.settings_applied(silent);
                const auto disabled = toca::frontend::menu_for(menus, settings);
                for (std::size_t index = 0; index < 4; ++index) {
                    require(disabled.items[index].label != menu.items[index].label, "Original sound labels must reflect zero volumes, disabled game music and mono effects");
                    if (index < 2) {
                        require(!disabled.items[index].decrease_available && disabled.items[index].increase_available, "Zero original volume must expose only the increase arrow");
                    }
                    check_glyphs(menu_font, disabled.items[index].label);
                    check_width(language, "Disabled sound", disabled.items[index].label,
                        std::min(menu_font.text_width(disabled.items[index].label), settings_font.text_width(disabled.items[index].label)), 405);
                }
                if (language == 0) {
                    require(disabled.items[0].label == "EFFECTS OFF" && disabled.items[1].label == "CD MUSIC OFF" && disabled.items[2].label == "GAME MUSIC OFF"
                                && disabled.items[3].label == "MONO SFX",
                        "Sound Setup must retain original zero-volume, music-off and mono labels");
                }
            }
            if (category == 1) {
                constexpr std::array<std::uint32_t, 8> graphics_icons{59, toca::frontend::fullscreen_icon, toca::frontend::vsync_icon, toca::frontend::fps_icon, 29, 53, 6, 16};
                for (std::size_t index = 0; index < graphics_icons.size(); ++index) {
                    require(menu.items[index].icon == graphics_icons[index], "Graphics Setup must retain original row icons and distinct native display-option icons");
                }

                struct ResolutionLabelFixture {
                    std::uint32_t width, height;
                    std::string_view label, suffix;
                    bool large = true;
                };

                for (const auto& resolution : {ResolutionLabelFixture{640, 480, "640X480", "(4:3)"}, ResolutionLabelFixture{1920, 1080, "1920X1080", "(16:9)"},
                         ResolutionLabelFixture{1280, 800, "1280X800", "(16:10)"}, ResolutionLabelFixture{1900, 900, "1900X900", "(19:9)"},
                         ResolutionLabelFixture{1111, 777, "1111X777", "(1111:777)", false}, ResolutionLabelFixture{16384, 16383, "16384X16383", "(16384:16383)", false}}) {
                    auto resized = values;
                    resized.video.width = resolution.width;
                    resized.video.height = resolution.height;
                    settings.settings_applied(resized);
                    const auto item = toca::frontend::menu_for(menus, settings).items[0];
                    require(item.label == resolution.label && item.suffix == resolution.suffix, "Resolution rows must retain numeric dimensions and exact aspect ratios");
                    const auto label = item.label + " " + item.suffix;
                    check_glyphs(menu_font, label);
                    check_glyphs(settings_font, label);
                    const auto width = resolution.large ? menu_font.text_width(label) : std::min(menu_font.text_width(label), settings_font.text_width(label));
                    check_width(language, "Resolution and aspect in one font", label, width, 405);
                }
                apply_labels[language] = menu.items[4].label;
                check_width(language, "Large Apply / Save label", apply_labels[language], menu_font.text_width(apply_labels[language]), 405);
                constexpr std::array<std::string_view, 3> apply_text{"APPLY / SAVE", "APPLIQUER / SAUVER", "ANWENDEN / SPEICHERN"};
                require(apply_labels[language] == apply_text[language], "Apply / Save must retain its short localized large-font label");
                const auto& split = menu.items[6];
                require(settings.item_available(6) && split.adjustable && split.icon == 6 && !split.help.empty(),
                    "Split Screen must retain its original adjustable row, icon and help");
                auto vertical = values;
                vertical.graphics.split_vertical = true;
                settings.settings_applied(vertical);
                const auto vertical_split = toca::frontend::menu_for(menus, settings).items[6];
                require(vertical_split.label != split.label && vertical_split.help == split.help, "Split Screen must show the selected stored orientation with its original help");
                check_glyphs(menu_font, vertical_split.label);
                check_width(
                    language, "Vertical split", vertical_split.label, std::min(menu_font.text_width(vertical_split.label), settings_font.text_width(vertical_split.label)), 405);
                if (language == 0) {
                    require(split.label == "SPLIT: HORIZONTAL" && vertical_split.label == "SPLIT: VERTICAL", "Split Screen must use the original orientation labels");
                }
                constexpr std::array<std::string_view, 5> english_detail{"LOW DETAIL", "MEDIUM DETAIL", "HIGH DETAIL  ", "VERY HIGH DETAIL", "MEGA DETAIL"};
                for (std::uint32_t level = 0; level < toca::graphics_detail_count; ++level) {
                    auto detail = values;
                    detail.graphics.detail = level;
                    settings.settings_applied(detail);
                    const auto original_detail = toca::frontend::menu_for(menus, settings).items[5];
                    require(original_detail.adjustable && !original_detail.help.empty(), "Each detail preset must retain an adjustable row and help");
                    if (language == 0) {
                        require(original_detail.label == english_detail[level], "Detail presets must retain original labels and append Mega");
                    }
                    check_glyphs(menu_font, original_detail.label);
                    check_glyphs(help_font, original_detail.help);
                    check_width(language, "Detail preset", original_detail.label,
                        std::min(menu_font.text_width(original_detail.label), settings_font.text_width(original_detail.label)), 405);
                    check_width(language, "Detail help", original_detail.help, help_font.text_width(original_detail.help), 558);
                }
            }
            if (category != 2) {
                settings.settings_applied(values);
                const auto saved = toca::frontend::menu_for(menus, settings).notice;
                settings.settings_applied(values, "Write failed");
                const auto failed = toca::frontend::menu_for(menus, settings).notice;
                check_glyphs(help_font, saved);
                check_glyphs(help_font, failed);
                require(!saved.empty() && saved != failed && help_font.text_width(saved) < 558 && help_font.text_width(failed) < 558,
                    "Localized apply and save notices must be distinct and fit the screen");
                settings.settings_applied(values);
            } else {
                const auto& labels = menus.controller_labels[language];
                for (std::size_t player = 0; player < 2; ++player) {
                    require(menu.items[player].label == labels.player + " " + std::to_string(player + 1) + " " + labels.controls,
                        "Both local profiles must use localized player headings");
                }
                require(std::ranges::all_of(menu.items.begin(), menu.items.begin() + 2,
                            [](const auto& item) {
                                return item.icon == 51;
                            })
                            && settings.item_available(2) && !settings.item_available(3) && menu.notice.empty(),
                    "Keyboard profile icons, Back, and the empty default notice must be explicit");
                settings.settings_applied(values, "Write failed");
                const auto failure = toca::frontend::menu_for(menus, settings).notice;
                require(!failure.empty(), "A failed save must remain visible on the player selector");
                check_glyphs(help_font, failure);
                check_width(language, "Controller failure", failure, help_font.text_width(failure), 558);
                settings.settings_applied(values);
                press(toca::Action::confirm);
                const auto controls = toca::frontend::menu_for(menus, settings);
                require(settings.screen() == toca::frontend::Screen::controller_options && controls.title == menu.items[0].label && controls.items.size() == 5
                            && controls.items[0].label == labels.keyboard + " 1" && controls.items[1].label == labels.configure_keyboard && controls.items[0].icon == 51
                            && controls.items[1].icon == 57 && controls.items[2].icon == 20 && controls.items[3].icon == 56 && controls.items[4].icon == 16,
                    "Selected player controls must retain the original five-row order, device configuration, heading and icons");
                for (const auto& item : controls.items) {
                    check_glyphs(menu_font, item.label);
                    check_glyphs(help_font, item.help);
                    check_width(language, "Player controls label", item.label, std::min(menu_font.text_width(item.label), settings_font.text_width(item.label)), 405);
                    check_width(language, "Player controls help", item.help, help_font.text_width(item.help), 558);
                }
                press(toca::Action::down);
                press(toca::Action::confirm);
                const auto capture = toca::frontend::menu_for(menus, settings);
                require(settings.screen() == toca::frontend::Screen::controller_keyboard_bindings && capture.compact && capture.items.size() == 9
                            && capture.notice == labels.choose_key && capture.title.empty(),
                    "Keyboard capture must retain the original blank heading, all nine localized actions and the original key prompt");
                for (std::size_t action = 0; action < capture.items.size(); ++action) {
                    const auto& item = capture.items[action];
                    require(item.label.starts_with(labels.actions[action]), "Capture must preserve the original configuration action order");
                    check_glyphs(settings_font, item.label);
                    check_glyphs(help_font, item.help);
                    check_width(language, "Captured binding", item.label, std::min(settings_font.text_width(item.label), help_font.text_width(item.label)), 458);
                }
                toca::InputSnapshot accepted;
                accepted.physical_binding = toca::PhysicalBinding{toca::BindingKind::key, 4};
                require(settings.update(accepted, {0, 1}) == std::vector{toca::frontend::Cue::confirm}, "Accepted capture must play the original confirmation sample once");
                const auto feedback = toca::frontend::menu_for(menus, settings);
                require(feedback.status == labels.ok && feedback.status == "OK" && settings.selection() == 0 && settings.binding_capture_feedback(),
                    "Capture feedback must use canonical localized OK and retain the just-accepted action");
                check_glyphs(help_font, feedback.status);
                check_width(language, "Accepted capture feedback", feedback.status, help_font.text_width(feedback.status), 558);
                press(toca::Action::back);
                press(toca::Action::down);
                press(toca::Action::down);
                press(toca::Action::confirm);
                const auto native = toca::frontend::menu_for(menus, settings);
                require(settings.screen() == toca::frontend::Screen::controller_native_options && native.items.size() == 4 && native.title.starts_with(labels.player + " 1 - ")
                            && native.items[0].adjustable && native.items[2].adjustable,
                    "Native gamepad adjustments must be clearly separated from the original five-row controls page");
                for (const auto& item : native.items) {
                    check_glyphs(menu_font, item.label);
                    check_glyphs(help_font, item.help);
                    check_width(language, "Native controls label", item.label, std::min(menu_font.text_width(item.label), settings_font.text_width(item.label)), 405);
                    check_width(language, "Native controls help", item.help, help_font.text_width(item.help), 558);
                }
                auto wheel_values = values;
                wheel_values.input.device_id = "joystick:0123456789abcdef0123456789abcdef";
                wheel_values.input.gamepad = toca::default_joystick_bindings();
                settings.set_input_devices({{wheel_values.input.device_id, "TEST WHEEL", true}});
                settings.settings_applied(wheel_values);
                const auto wheel_options = toca::frontend::menu_for(menus, settings);
                require(wheel_options.items[2].label.starts_with("FORCE FEEDBACK"), "A raw wheel must expose directional force feedback in its options page");
                for (const auto& item : wheel_options.items) {
                    check_glyphs(menu_font, item.label);
                    check_glyphs(help_font, item.help);
                    check_width(language, "Wheel option", item.label, std::min(menu_font.text_width(item.label), settings_font.text_width(item.label)), 405);
                    check_width(language, "Wheel option help", item.help, help_font.text_width(item.help), 558);
                }
                press(toca::Action::back);
                const auto wheel_controls = toca::frontend::menu_for(menus, settings);
                check_glyphs(help_font, wheel_controls.items[1].help);
                check_width(language, "Wheel calibration help", wheel_controls.items[1].help, help_font.text_width(wheel_controls.items[1].help), 558);
                press(toca::Action::up);
                press(toca::Action::up);
                press(toca::Action::confirm);
                const auto wheel_capture = toca::frontend::menu_for(menus, settings);
                require(settings.screen() == toca::frontend::Screen::controller_gamepad_bindings && wheel_capture.notice != labels.choose_button,
                    "Wheel capture must explain full axis movement as well as button input");
                check_glyphs(help_font, wheel_capture.notice);
                check_width(language, "Wheel capture prompt", wheel_capture.notice, help_font.text_width(wheel_capture.notice), 558);
                press(toca::Action::back);
                press(toca::Action::down);
                press(toca::Action::down);
                press(toca::Action::confirm);
                settings.settings_applied(values);
                settings.set_input_devices({});
                press(toca::Action::back);
                for (int row = 0; row < 3; ++row) {
                    press(toca::Action::up);
                }
                press(toca::Action::right); // No pad is connected: Keyboard -> Auto.
                const auto auto_request = settings.take_settings_request();
                require(auto_request && auto_request->input.device_id == toca::automatic_input_device, "Auto must be explicitly selectable without a connected pad");
                settings.settings_applied(*auto_request);
                const auto automatic = toca::frontend::menu_for(menus, settings);
                require(automatic.items.size() == 5 && automatic.items[0].label == "AUTO" && automatic.items[0].decrease_available && !automatic.items[0].increase_available
                            && automatic.items[1].label != labels.configure_gamepad && settings.item_available(1),
                    "Auto must retain the original primary row count and expose a distinct configuration chooser");
                for (const auto& item : automatic.items) {
                    check_glyphs(menu_font, item.label);
                    check_glyphs(help_font, item.help);
                    check_width(language, "Automatic controls label", item.label, std::min(menu_font.text_width(item.label), settings_font.text_width(item.label)), 405);
                    check_width(language, "Automatic controls help", item.help, help_font.text_width(item.help), 558);
                }
                press(toca::Action::down);
                press(toca::Action::confirm);
                press(toca::Action::down);
                const auto chooser = toca::frontend::menu_for(menus, settings);
                require(settings.screen() == toca::frontend::Screen::controller_auto_configure && chooser.items.size() == 3 && !settings.item_available(1)
                            && chooser.items[0].label == labels.configure_keyboard && chooser.items[1].label == labels.configure_gamepad && !chooser.notice.empty(),
                    "Auto must offer localized keyboard/gamepad/Back rows and explain unavailable gamepad capture");
                for (const auto& item : chooser.items) {
                    check_glyphs(menu_font, item.label);
                    check_glyphs(help_font, item.help);
                    check_width(language, "Automatic configuration label", item.label, std::min(menu_font.text_width(item.label), settings_font.text_width(item.label)), 405);
                }
                check_glyphs(help_font, chooser.notice);
                check_width(language, "Automatic configuration notice", chooser.notice, help_font.text_width(chooser.notice), 558);
                settings.set_input_devices({{"pad-a", "TEST GAMEPAD"}}, toca::InputDevice{"pad-a", "TEST GAMEPAD"});
                press(toca::Action::confirm);
                require(
                    settings.screen() == toca::frontend::Screen::controller_gamepad_bindings, "Eligible Auto gamepad configuration must use the ordinary localized binding wizard");
                press(toca::Action::back);
                press(toca::Action::back);
                press(toca::Action::back);
            }
            press(toca::Action::back);
            require(settings.screen() == toca::frontend::Screen::settings && settings.selection() == category, "Options submenu Back must retain its parent selection");
            press(toca::Action::down);
        }
        press(toca::Action::confirm);
        for (int step = 0; step < 3; ++step) {
            press(toca::Action::up);
        }
        const auto main = toca::frontend::menu_for(menus, settings);
        require(main.items.size() == 5 && main.items[3].label == "EXTRAS" && main.items[4].label == menus.main[language].items[3].label,
            "Extras must precede the original localized Quit row");
        press(toca::Action::confirm);
        const auto extras = toca::frontend::menu_for(menus, settings);
        require(extras.items.size() == 5 && !extras.compact && extras.items[0].label != extras.items[1].label,
            "Extras must expose four effect categories and Back as five original icon rows");
        constexpr std::array<std::uint32_t, 5> extras_icons{toca::frontend::unlocks_icon, toca::frontend::camera_icon, 19, 3, 16};
        for (std::size_t index = 0; index < extras.items.size(); ++index) {
            const auto& item = extras.items[index];
            require(item.icon == extras_icons[index], "Extras categories must retain their distinct unlock, camera, effects, racing and Back icons");
            check_glyphs(menu_font, item.label);
            check_glyphs(help_font, item.help);
            check_width(language, "Extras category label", item.label, std::min(menu_font.text_width(item.label), settings_font.text_width(item.label)), 405);
            check_width(language, item.label, item.help, help_font.text_width(item.help), 558);
        }
        for (const auto category : {toca::frontend::cheats::Category::unlocks, toca::frontend::cheats::Category::camera, toca::frontend::cheats::Category::effects,
                 toca::frontend::cheats::Category::racing}) {
            press(toca::Action::confirm);
            const auto category_menu = toca::frontend::menu_for(menus, settings);
            const auto codes = toca::frontend::cheats::category_codes(category);
            require(category_menu.items.size() == codes.size() + 1 && category_menu.compact, "Each effect category must retain its compact direct switches and Back");
            check_glyphs(help_font, category_menu.notice);
            check_width(language, "Category notice", category_menu.notice, help_font.text_width(category_menu.notice), 558);
            for (std::size_t index = 0; index < category_menu.items.size(); ++index) {
                const auto& item = category_menu.items[index];
                check_glyphs(settings_font, item.label);
                check_glyphs(help_font, item.help);
                check_width(language, "Effect label", item.label, settings_font.text_width(item.label), 458);
                check_width(language, item.label, item.help, help_font.text_width(item.help), 558);
                if (index < codes.size()) {
                    require(
                        item.adjustable && category_menu.notice.empty() && item.label.find("--") == std::string::npos, "Every Extras row must be usable with a real Yes/No value");
                    require(item.label.find(toca::frontend::cheats::code_text(codes[index])) == std::string::npos, "Extras controls must name their effects with localized labels");
                }
            }
            press(toca::Action::back);
            press(toca::Action::down);
        }
        press(toca::Action::confirm);
        for (int step = 0; step < 3; ++step) {
            press(toca::Action::up);
        }
        press(toca::Action::confirm);
        require(settings.screen() == toca::frontend::Screen::race_menu && toca::frontend::menu_for(menus, settings).items.size() == 4,
            "Locked ToCA Showdown must remain absent from the normal race menu");
        auto unlocks = settings.cheats_state();
        toca::frontend::cheats::set_menu_active(unlocks, toca::frontend::cheats::Code::patscreem, true);
        settings.set_cheats_state(unlocks);
        const auto showdown = toca::frontend::menu_for(menus, settings);
        require(showdown.items.size() == 5 && showdown.items[4].label == menus.showdown[language].label && settings.item_available(4),
            "The Showdown switch must reveal the original localized playable mode");
        check_glyphs(help_font, showdown.items[4].help);
        check_width(language, "Showdown help", showdown.items[4].help, help_font.text_width(showdown.items[4].help), 558);
        press(toca::Action::up);
        toca::InputSnapshot showdown_confirm;
        showdown_confirm.pressed = toca::action_mask(toca::Action::confirm);
        require(settings.selection() == 4 && settings.update(showdown_confirm, {0, 1}) == std::vector{toca::frontend::Cue::confirm}
                    && settings.screen() == toca::frontend::Screen::championship_menu && settings.setup().championship_showdown,
            "Unlocked Showdown must reach the championship New/Load path and preserve its original subtype");
        require(toca::frontend::menu_for(menus, settings).title == menus.championship[language].title, "Showdown must use the shared original Championship New/Load title");
        press(toca::Action::back);
        require(settings.screen() == toca::frontend::Screen::race_menu && settings.selection() == 4, "Showdown Back must restore its race-menu row");
        toca::frontend::cheats::set_menu_active(unlocks, toca::frontend::cheats::Code::patscreem, false);
        settings.set_cheats_state(unlocks);
        require(toca::frontend::menu_for(menus, settings).items.size() == 4, "Turning unearned Showdown access off must hide its race-menu entry");
        toca::frontend::cheats::apply(unlocks, toca::frontend::cheats::Code::patscreem);
        toca::frontend::cheats::set_menu_active(unlocks, toca::frontend::cheats::Code::patscreem, false);
        settings.set_cheats_state(unlocks);
        require(toca::frontend::menu_for(menus, settings).items.size() == 5, "Turning the Showdown code off must preserve its earned unlock");
        toca::frontend::cheats::redeem(unlocks, "CMLOCK");
        settings.set_cheats_state(unlocks);
        require(toca::frontend::menu_for(menus, settings).items.size() == 4, "Typed CMLOCK must apply its original earned-progress reset");
    }
    if (!layout_errors.empty()) {
        throw std::runtime_error("Localized frontend text exceeds its original frame:\n" + layout_errors);
    }
    require(apply_labels[0] != apply_labels[1] && apply_labels[1] != apply_labels[2], "Native settings must have English, French and German labels");
    require(menus.backdrop_inset.x == 33 && menus.backdrop_inset.y == 60 && menus.backdrop_inset.width == 574 && menus.backdrop_inset.height == 360,
        "Stable frontend backdrop must use the original translated Audi2 quad");
    const auto game_images = store.open_archive("data/image640.bfp");
    const auto championship_caption = toca::frontend::decode_font(game_images.member(24).bytes());
    const auto title_font = toca::frontend::decode_font(archive.member(27).bytes());
    for (std::uint32_t language = 0; language < 5; ++language) {
        const auto& championship = menus.championship[language];
        const auto& options = menus.championship_options[language];
        const auto& labels = menus.championship_labels[language];
        require(championship.items.size() == 2 && championship.items[0].icon == 34 && championship.items[1].icon == 30 && options.items.size() == 2 && options.items[0].icon == 27
                    && options.items[1].icon == 13 && options.items[0].adjustable,
            "Championship must retain its original New/Load and length/Continue layouts and icons");
        require(!labels.table_title.empty() && !labels.eliminated.empty() && !labels.complete.empty() && labels.short_length != labels.full_length,
            "Championship table and end captions must come from localized original data");
        for (const auto& item : championship.items) {
            check_glyphs(menu_font, item.label);
            check_glyphs(help_font, item.help);
        }
        check_glyphs(menu_font, labels.short_length);
        check_glyphs(menu_font, labels.full_length);
        for (const auto& caption : {labels.eliminated, labels.complete, labels.save_prompt, labels.overwrite, labels.saved, labels.cancelled}) {
            check_glyphs(championship_caption, caption);
            int adjustment = 0;
            while (championship_caption.text_width(caption, adjustment, adjustment) >= 558 && adjustment > -32) {
                --adjustment;
            }
            require(championship_caption.text_width(caption, adjustment, adjustment) < 558 && 74 + championship_caption.line_height + 2 < 111,
                "Original game caption font must preserve punctuation and fit above Championship column headings");
        }
        auto slot_title = labels.select_slot;
        std::replace(slot_title.begin(), slot_title.end(), '^', ' ');
        check_glyphs(title_font, slot_title);
        for (const auto& label : {labels.cancel, labels.empty, labels.short_label, labels.full_label}) {
            check_glyphs(settings_font, label);
        }
        for (const auto& help : labels.load_slot_help) {
            check_glyphs(help_font, help);
        }
        for (const auto& text :
            {labels.table_title, labels.player, labels.points, labels.round, labels.race, labels.no_saved_games, labels.file_error, labels.continue_label + " / " + labels.back}) {
            const auto display = [&] {
                auto result = text;
                std::replace(result.begin(), result.end(), '^', ' ');
                return result;
            }();
            const auto& font = text == labels.table_title ? title_font : help_font;
            check_glyphs(font, display);
            int adjustment = 0;
            while (font.text_width(display, adjustment, adjustment) >= 558 && adjustment > -32) {
                --adjustment;
            }
            require(font.text_width(display, adjustment, adjustment) < 558, "Localized championship titles, headings, file errors, status and footer must fit the authored canvas");
        }
    }
    require(toca::frontend::championship_driver_label("G.Tarquini") == "G.TARQUINI", "Championship roster display must normalize original mixed-case driver names");
    const auto config = store.open_archive("data/config.bfp");
    const auto ai_member = config.member(0);
    const auto ai = ai_member.bytes();
    for (std::uint32_t bank = 1; bank <= 24; ++bank) {
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            const auto name = ai.subspan(124 + bank * 640 + actor * 40, 16);
            const auto end = std::find(name.begin(), name.end(), 0);
            const auto label = toca::frontend::championship_driver_label(std::string(name.begin(), end));
            check_glyphs(settings_font, label);
            require(settings_font.text_width(label) < 344, "Every original championship roster name must remain complete in its standings column");
        }
    }
    require(menus.race[0].items.size() == 4 && menus.race[0].items[2].icon == 17 && menus.single[0].items[4].icon == 13 && menus.difficulty[0].items[1].label == "MEDIUM",
        "Selection menu tables must preserve hidden Showdown and original icons");
    require(menus.network_options[0].items.size() == 5 && menus.network_options[0].items[4].label == "CONTINUE" && menus.network_labels[0].session_name == "ENTER SESSION NAME"
                && menus.network_labels[0].player_name == "ENTER PLAYER NAME" && menus.network_labels[2].waiting_room == "WARTERAUM",
        "Linkup must retain its original five options, distinct host/client prompts and localized Waiting Room");
    require(menus.network_labels[0].session_lost == "LINKUP SESSION LOST" && menus.network_labels[2].race_started_without_you == "RENNEN HAT OHNE SIE BEGONNEN",
        "Linkup failures must read the canonical localized session-lost and excluded-selector dialogs");
    toca::frontend::Controller route;
    route.set_name_defaults(menus.default_player_names);
    route.update({}, {0, 5'000'000'000});
    route.frontend_ready();
    toca::InputSnapshot confirm;
    confirm.pressed = toca::action_mask(toca::Action::confirm);
    for (int index = 0; index < 3; ++index) {
        route.update(confirm, {0, 1});
    }
    const auto single = toca::frontend::menu_for(menus, route);
    require(
        single.items[0].label == "PLAYERS : 1" && single.items[1].label == "3 LAP RACE" && single.items[2].label == "QUALIFY: YES" && single.items[3].label == "WEATHER : DEFAULT",
        "Dynamic selection labels must use canonical localized format strings");
    std::cout << "Canonical fonts, compiled selection tables and dynamic localized labels passed\n";
}
} // namespace

int main(int argc, char* argv[]) {
    return toca::frontend::test::run([&] {
        if (argc != 2) {
            throw std::runtime_error("Usage: frontend-menu-localization-tests canonical-root");
        }
        corpus(argv[1]);
    });
}
