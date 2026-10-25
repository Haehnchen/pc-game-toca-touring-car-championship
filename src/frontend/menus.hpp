#pragma once

#include "core/contracts.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace toca {
class AssetStore;
}

namespace toca::frontend {
class Controller;

inline constexpr std::uint32_t extras_icon = 0x100;
inline constexpr std::uint32_t fullscreen_icon = 0x101;
inline constexpr std::uint32_t vsync_icon = 0x102;
inline constexpr std::uint32_t fps_icon = 0x103;
inline constexpr std::uint32_t camera_icon = 0x104;
inline constexpr std::uint32_t unlocks_icon = 0x105;

struct MenuItem {
    std::string label, help;
    std::uint32_t icon = 0;
    bool adjustable = false;
    bool decrease_available = true, increase_available = true;
    std::string suffix{};
};

struct NetworkLabels {
    std::string title{}, host{}, join{}, host_help{}, join_help{}, back{}, continue_label{}, waiting_room{}, start{}, track{}, ok{}, session_name{}, player_name{}, car_boost{},
        computer_cars{}, session_lost{}, race_started_without_you{};
    std::uint32_t host_icon = 20, join_icon = 20;
    std::array<std::string, 6> weather_values{};
    std::array<std::string, 10> track_names{};
};

struct Menu {
    std::string title;
    std::vector<MenuItem> items;
    std::string notice;
    bool compact = false;

    struct RosterLine {
        std::string name, car, status;
        bool committed = false;
        std::string markers{};
    };

    bool waiting_room = false;
    std::vector<RosterLine> lines;
    std::string status;
    std::vector<std::string> details{};
};

struct Menus {
    std::array<Menu, 5> language, main, race, single, time_trial, championship, championship_options, difficulty, settings, sound, controller_players, controller, network_options;
    std::array<NetworkLabels, 5> network_labels;
    std::array<MenuItem, 5> showdown;

    struct ChampionshipLabels {
        std::string short_length, full_length, player, points, round, continue_label, back, table_title, no_saved_games, file_error;
        std::string eliminated, complete, qualifying, race;
        std::string save_prompt, select_slot, overwrite, saved, cancelled, cancel, empty, slot_format, short_label, full_label;
        std::array<std::string, 5> load_slot_help;
    };

    std::array<ChampionshipLabels, 5> championship_labels;
    std::array<std::string, 5> default_player_names;

    struct OptionLabels {
        std::string players, lap, race, qualify, yes, no, weather, load_best_lap;
        std::array<std::string, 6> weather_values;
    };

    std::array<OptionLabels, 5> options;
    std::string players_format, laps_format, qualify_format, weather_format;

    struct SoundLabels {
        std::string effects, music, volume, game_music, mono, stereo, on, off;
    };

    std::array<SoundLabels, 5> sound_labels;
    std::string sound_volume_format, sound_switch_format;

    struct ControllerLabels {
        std::string keyboard, configure_keyboard, configure_gamepad, sensitivity, select_help, configure_help, sensitivity_help, choose_key, choose_button, player, controls, ok;
        std::array<std::string, 3> sensitivity_values;
        std::array<std::string, 9> actions;
    };

    std::array<ControllerLabels, 5> controller_labels;

    struct GraphicsLabels {
        std::array<std::string, graphics_detail_count> detail_values;
        std::string detail_help, mega_help, split_horizontal, split_vertical, split_help;
    };

    std::array<GraphicsLabels, 5> graphics_labels;

    struct BackdropInset {
        int x = 0, y = 0, width = 0, height = 0;
    } backdrop_inset;
};

std::string championship_driver_label(std::string_view name);
Menus load_menus(const AssetStore& store);
Menu menu_for(const Menus& menus, const Controller& controller);
} // namespace toca::frontend
