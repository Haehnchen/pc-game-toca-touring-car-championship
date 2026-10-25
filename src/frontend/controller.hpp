#pragma once

#include "core/contracts.hpp"
#include "core/game_mode.hpp"
#include "frontend/cheats.hpp"
#include "frontend/network_lobby.hpp"
#include "frontend/records.hpp"
#include "race/championship.hpp"
#include "settings/championship.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace toca::frontend {
enum class Screen {
    title,
    copyright,
    language,
    main_menu,
    settings,
    graphics_options,
    sound_options,
    controller_players,
    controller_options,
    controller_auto_configure,
    controller_native_options,
    controller_keyboard_bindings,
    controller_gamepad_bindings,
    extras,
    extras_unlocks,
    extras_camera,
    extras_effects,
    extras_racing,
    race_menu,
    network_linkup,
    network_options,
    single_options,
    time_trial_options,
    championship_menu,
    championship_options,
    championship_overview,
    championship_load_slots,
    championship_save_prompt,
    championship_save_slots,
    championship_save_overwrite,
    championship_save_feedback,
    championship_honors,
    championship_finale_loading,
    championship_finale,
    time_trial_records,
    championship_records,
    difficulty,
    name_entry,
    car_selection,
    track_selection,
    scene_loading,
    scene
};
enum class Cue : std::uint32_t {
    navigation = 0,
    option = 1,
    confirm = 2,
    back = 2,
    character = 3,
    unavailable = 4,
    cheat_activated = 5
};

std::string_view screen_name(Screen screen);
int menu_row_y(std::size_t count, std::size_t index);
int settings_row_y(std::size_t index);
using Resolution = std::pair<std::uint32_t, std::uint32_t>;

enum class ChampionshipRequest {
    start,
    resume,
    advance,
    save
};

struct RaceSetup {
    GameMode mode = GameMode::single_race;
    bool ghost_enabled = false;
    bool championship_full = false;
    bool championship_showdown = false;
    std::uint32_t players = 1;
    std::uint32_t lap_choice = 2;
    std::uint32_t laps = 3;
    std::uint32_t weather = 0;
    bool qualifying = true;
    bool car_boost = true;
    std::uint32_t difficulty = 1;
};

class Controller {
    Screen screen_ = Screen::title;
    std::size_t selection_ = 0;
    std::uint32_t language_ = 0;
    std::uint64_t title_elapsed_ns_ = 0;
    std::uint64_t highlight_elapsed_ns_ = 0;
    std::uint64_t animation_time_ns_ = 0;
    std::uint64_t menu_entered_ns_ = 0;
    int requested_transition_direction_ = 1, menu_transition_direction_ = 1;
    bool quit_ = false;
    RaceSetup setup_;
    std::uint32_t single_lap_choice_ = 2;
    bool single_qualifying_ = true;
    std::optional<race::ChampionshipState> championship_;
    std::optional<ChampionshipRequest> championship_request_;
    std::string championship_error_;
    std::array<ChampionshipSlots, 2> championship_slots_;
    std::uint32_t championship_slot_ = 0;
    std::optional<race::ChampionshipHonors> championship_honors_;
    std::uint64_t championship_feedback_ns_ = 0;
    bool championship_saved_ = false;
    void finish_championship_save();
    std::optional<LapRecords> records_;
    std::optional<std::uint32_t> record_highlight_track_;
    std::size_t record_return_selection_ = 0;
    bool records_after_trial_ = false;
    std::optional<ChampionshipRecords> championship_records_;
    std::string championship_records_error_;
    std::uint64_t championship_record_fraction_ = 0;
    std::uint32_t championship_record_ticks_ = 0, championship_record_phase_ = 0;
    std::uint8_t championship_record_color_ = 0x61;
    std::array<std::string, 5> name_defaults_;
    std::array<std::string, 2> accepted_names_;
    std::string editing_name_;
    std::size_t selection_player_ = 0, controller_player_ = 0;
    bool single_player_qualifying_ = true;
    bool name_edited_ = false;
    // The original blink flag and the last actual cursor draw are distinct.
    bool name_cursor_phase_ = true, name_cursor_drawn_ = false;
    std::uint32_t name_cursor_ticks_ = 0;
    std::uint64_t name_cursor_fraction_ = 0;
    std::array<std::uint32_t, 2> car_ids_{}, accepted_car_ids_{};
    std::uint32_t pending_difficulty_ = 1;
    std::array<bool, 2> transmissions_manual_{};
    std::uint32_t track_id_ = 0, stored_track_id_ = 0, track_preview_ticks_ = 0;
    std::uint64_t track_preview_fraction_ = 0;
    Settings effective_settings_, editing_settings_;
    std::vector<Resolution> resolutions_{{640, 480}};
    std::vector<std::uint32_t> fps_choices_{0, 30, 60, 90, 120, 144, 165, 240, 360};
    std::optional<Settings> settings_request_;
    bool detail_settings_request_ = false, detail_save_failed_ = false;
    bool settings_saved_ = false;
    std::string settings_error_;
    std::vector<InputDevice> input_devices_;
    std::optional<InputDevice> automatic_gamepad_;
    std::array<PhysicalBinding, 9> captured_bindings_{};
    std::size_t binding_capture_index_ = 0;
    std::uint64_t binding_feedback_remaining_ns_ = 0;
    bool binding_capture_error_ = false;
    Screen binding_return_screen_ = Screen::controller_options;
    std::size_t binding_return_selection_ = 1;
    cheats::State cheats_;
    bool cheats_changed_ = false;
    std::string cheats_error_;
    NetworkLobby network_lobby_;
    network::SessionView network_view_;
    std::deque<NetworkLobbyCommand> network_requests_;
    bool network_selecting_ = false, network_connecting_ = false, network_waiting_commit_ = false, network_ai_ = false;
    void enter_network_waiting();
    network::LobbySettings network_settings() const;

    void enter(Screen screen, std::size_t selection = 0);
    void enter_name();
    void change_setting(bool right, bool cycle_volume);
    void request_settings(bool detail_only = false);
    void begin_binding_capture(bool keyboard);

public:
    std::vector<Cue> update(const InputSnapshot& input, const ClockStep& clock);
    void set_network_view(network::SessionView view);
    void network_error(std::string error);
    std::optional<NetworkLobbyCommand> take_network_request();
    Menu network_menu(const Menus& menus) const;
    bool network_hosting() const;
    bool network_ai_enabled() const;
    bool network_text_input() const;
    void begin_network_race(const network::SessionView& session, const network::RaceView& race);
    void return_network_lobby();
    void frontend_ready();
    void scene_ready();
    void restart_scene();
    void return_from_scene(bool championship_results = false);
    void set_championship_state(std::optional<race::ChampionshipState> state, std::string error = {});
    const std::optional<race::ChampionshipState>& championship_state() const;
    std::string_view championship_error() const;
    std::optional<ChampionshipRequest> take_championship_request();
    void show_championship_overview();
    void show_championship_finale();
    void championship_finale_ready();
    void return_from_championship_finale();
    void set_championship_slots(bool showdown, ChampionshipSlots slots);
    const ChampionshipSlots& championship_slots() const;
    std::uint32_t championship_slot() const;
    void show_championship_save_prompt(std::optional<race::ChampionshipHonors> honors = {});
    void championship_save_result(std::string error = {});
    bool championship_saved() const;
    void show_championship_honors(race::ChampionshipHonors honors);
    const std::optional<race::ChampionshipHonors>& championship_honors() const;
    void begin_championship_round(const race::ChampionshipState& state, std::uint32_t track, std::uint32_t laps, bool qualifying);
    void set_lap_records(LapRecords records, std::optional<std::uint32_t> improved_track = {});
    const std::optional<LapRecords>& records() const;
    std::optional<std::uint32_t> record_highlight_track() const;
    void set_championship_records(ChampionshipRecords records, std::string error = {});
    const std::optional<ChampionshipRecords>& championship_records() const;
    std::string_view championship_records_error() const;
    std::uint8_t championship_record_color() const;
    void set_name_defaults(std::array<std::string, 5> defaults);
    void set_settings(Settings effective, std::vector<Resolution> resolutions);
    void set_input_devices(std::vector<InputDevice> devices, std::optional<InputDevice> automatic_gamepad = {});
    const std::vector<InputDevice>& input_devices() const;
    std::string input_device_name() const;
    std::vector<std::string> input_device_choices() const;
    bool controller_gamepad_available() const;
    std::size_t binding_capture_index() const;
    const std::array<PhysicalBinding, 9>& binding_capture_values() const;
    bool binding_capture_error() const;
    bool binding_capture_feedback() const;
    std::optional<Settings> take_settings_request();
    void settings_applied(Settings effective, std::string error = {});
    const cheats::State& cheats_state() const;
    void set_cheats_state(cheats::State state);
    bool take_cheats_changed();
    void cheats_saved(std::string error = {});
    std::string_view cheats_error() const;
    const Settings& settings_values() const;
    bool settings_saved() const;
    std::string_view settings_error() const;
    const RaceSetup& setup() const;
    std::string_view player_name(std::size_t player = 0) const;
    bool name_cursor_visible() const;
    std::uint32_t car_id(std::size_t player = 0) const;
    bool transmission_manual(std::size_t player = 0) const;
    std::size_t selection_player() const;
    std::size_t controller_player() const;
    const InputSettings& controller_input() const;
    bool car_available() const;
    std::uint32_t stored_car_id(std::size_t player = 0) const;
    std::uint32_t track_id() const;
    std::uint32_t stored_track_id() const;
    std::uint32_t track_max_id() const;
    bool track_available() const;
    std::uint32_t track_preview_rotation() const;
    // The same source phase with its active30Hz fractional remainder.
    float track_preview_presentation_rotation() const;
    Screen screen() const;
    std::size_t selection() const;
    std::uint32_t language() const;
    bool quit_requested() const;
    bool item_available(std::size_t index) const;
    std::uint8_t highlight_color() const;
    // Active frontend time, independent of presentation frequency and menu input.
    std::uint64_t animation_time_ns() const;
    std::uint64_t menu_entered_ns() const;
    int menu_transition_direction() const;
};
} // namespace toca::frontend
