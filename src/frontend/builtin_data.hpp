#pragma once

#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "frontend/records.hpp"
#include "frontend/selection_resources.hpp"
#include "frontend/track_resources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace toca::frontend::builtin {
// Native, named 1.1a frontend data; no original executable is needed to build.
enum class TextId {
    continue_label,
    ok,
    back,
    go_back_to_previous_screen,
    yes,
    no,
    main_menu_title,
    start_race,
    high_scores,
    toca_showdown,
    game_options,
    linkup_game,
    quit,
    select_and_start_race,
    view_the_high_scores,
    the_final_showdown,
    sound_graphics_and_controller_setup,
    start_a_multiplayer_linkup_game,
    end_the_game,
    race_menu,
    single_race,
    championship,
    time_trial,
    start_single_race,
    start_autotrader_rac_championship_race,
    start_practice_race,
    load_game,
    new_game,
    load_best_lap,
    no_saved_games,
    round,
    short_label,
    full,
    start_a_new_championship,
    load_a_saved_championship_position,
    load_game_from_slot_1,
    load_game_from_slot_2,
    load_game_from_slot_3,
    load_game_from_slot_4,
    load_game_from_slot_5,
    load_best_lap_help,
    players,
    select_the_number_of_players,
    progress_to_the_next_options_screen,
    lap,
    race,
    short_championship,
    full_championship,
    select_the_number_of_laps_to_race,
    select_the_length_of_championship_to_race,
    weather,
    default_label,
    sunny,
    rain,
    snow,
    cloudy,
    storm,
    overcast,
    choose_the_weather_for_the_race,
    qualify,
    car_boost,
    computer_cars,
    choose_to_compete_with_computer_cars,
    difficulty,
    easy,
    hard,
    choose_the_difficulty_level,
    decide_if_you_want_to_do_a_qualifying_lap,
    speed_up_slow_player_cars,
    sound_setup,
    effects,
    cd_music,
    volume,
    game_music,
    mono_sfx,
    stereo_sfx,
    on,
    off,
    adjust_the_sound_effects_volume,
    adjust_the_music_volume,
    select_stereo_or_mono_sound,
    toggle_in_game_music,
    low_detail,
    medium_detail,
    high_detail,
    very_high_detail,
    split,
    horizontal,
    vertical,
    adjust_the_graphical_detail,
    change_the_split_screen_type_in_2p_mode,
    player,
    controls,
    keyboard,
    configure_keyboard,
    configure_joystick,
    sensitivity,
    low,
    medium,
    high,
    choose_button_or_key_for,
    choose_key_for,
    accelerate,
    brake,
    turn_left,
    turn_right,
    gear_up,
    gear_down,
    rear_camera,
    change_camera,
    play_horn,
    configure_your_controller,
    select_your_controller,
    set_the_sensitivity_for_this_controller,
    linkup_title,
    host_a_new_game,
    join_game,
    host_new_game_help,
    join_game_in_progress,
    enter_session_name,
    enter_player_name,
    waiting_room,
    options_title,
    sound_setup_item,
    graphics_setup,
    controller_setup,
    set_up_the_sound,
    set_up_the_graphics,
    set_up_the_controller,
    linkup_session_lost,
    the_race_has_started_without_you,
    select_car,
    enter_name,
    drive,
    front_wheel_drive,
    rear_wheel_drive,
    four_wheel_drive,
    valves,
    bhp,
    rpm,
    press_up_to_toggle_transmission,
    manual,
    auto_label,
    unknown_vehicle,
    you_cannot_select_this_car,
    select_track,
    length,
    location,
    select_your_track,
    you_cannot_select_this_track_yet,
    select_language,
    select_your_language,
    championship_records_title,
    track,
    time,
    points,
    fastest_lap,
    by,
    in,
    count
};

std::string translated(TextId text, std::uint32_t language);

inline constexpr std::string_view players_format = "%s : %d";
inline constexpr std::string_view laps_format = "%d %s %s";
inline constexpr std::string_view qualifying_format = "%s: %s";
inline constexpr std::string_view weather_format = "%s : %s";
inline constexpr std::string_view sound_volume_format = "%s %s %d";
inline constexpr std::string_view sound_switch_format = "%s %s";
inline constexpr std::string_view player_controls_format = "%s %d %s";
inline constexpr std::array<std::uint32_t, 5> network_options_icons{27, 28, 1, 31, 13};
inline constexpr std::array<std::uint32_t, 4> settings_icons{33, 58, 20, 16};
inline constexpr std::array<std::uint32_t, 5> sound_icons{19, 18, 18, 14, 16};
inline constexpr std::array<std::uint32_t, 3> controller_players_icons{24, 25, 16};
inline constexpr std::uint32_t controller_sensitivity_icon = 20;
inline constexpr std::uint32_t controller_calibration_icon = 56;
inline constexpr std::uint32_t controller_back_icon = 16;
inline constexpr std::array<std::uint32_t, 5> race_icons{31, 2, 32, 17, 50};
inline constexpr std::array<std::uint32_t, 5> single_race_icons{25, 27, 1, 28, 13};
inline constexpr std::array<std::uint32_t, 3> time_trial_icons{28, 30, 13};
inline constexpr std::array<std::uint32_t, 2> championship_icons{34, 30};
inline constexpr std::array<std::uint32_t, 2> championship_options_icons{27, 13};
inline constexpr std::array<std::uint32_t, 3> difficulty_icons{7, 7, 7};
inline constexpr std::array<std::uint32_t, 3> language_icons{8, 9, 10};
inline constexpr std::array<std::uint32_t, 4> main_icons{3, 26, 36, 7};
inline constexpr std::uint32_t host_icon = 50;
inline constexpr std::uint32_t join_icon = 49;
inline constexpr std::array<TextId, 6> weather_texts{TextId::default_label, TextId::sunny, TextId::cloudy, TextId::rain, TextId::snow, TextId::storm};
inline constexpr std::array<TextId, 3> drive_type_texts{TextId::front_wheel_drive, TextId::rear_wheel_drive, TextId::four_wheel_drive};
inline constexpr std::array<TextId, 5> load_slot_help_texts{
    TextId::load_game_from_slot_1, TextId::load_game_from_slot_2, TextId::load_game_from_slot_3, TextId::load_game_from_slot_4, TextId::load_game_from_slot_5};
inline constexpr std::array<std::string_view, 3> language_names{"ENGLISH", "FRANCAIS", "DEUTSCH"};
inline constexpr Menus::BackdropInset menu_backdrop{.x = 33, .y = 60, .width = 574, .height = 360};

enum DriveType : std::uint32_t {
    front_wheel_drive,
    rear_wheel_drive,
    four_wheel_drive
};

enum Weather : std::uint32_t {
    default_weather,
    sunny,
    cloudy,
    rain,
    snow,
    storm
};

inline constexpr std::string_view miles_unit = "MILES";
inline constexpr std::string_view kilometres_unit = "KM";

const std::array<CarInformation, 10>& cars();
const std::array<TrackInformation, 10>& tracks();
const LapRecords& lap_records();
const std::array<std::string_view, 10>& record_track_names();
const std::array<RecordResources::Badge, 10>& record_badges();
const MenuTransitionResources& menu_transition();
} // namespace toca::frontend::builtin
