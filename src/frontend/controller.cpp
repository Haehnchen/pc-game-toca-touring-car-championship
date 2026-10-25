#include "frontend/controller.hpp"

#include "scene/catalog.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace toca::frontend {
std::string_view screen_name(Screen screen) {
    switch (screen) {
        case Screen::title:
            return "title";
        case Screen::copyright:
            return "copyright";
        case Screen::language:
            return "language";
        case Screen::main_menu:
            return "main_menu";
        case Screen::settings:
            return "settings";
        case Screen::graphics_options:
            return "graphics_options";
        case Screen::sound_options:
            return "sound_options";
        case Screen::controller_players:
            return "controller_players";
        case Screen::controller_options:
            return "controller_options";
        case Screen::controller_auto_configure:
            return "controller_auto_configure";
        case Screen::controller_native_options:
            return "controller_native_options";
        case Screen::controller_keyboard_bindings:
            return "controller_keyboard_bindings";
        case Screen::controller_gamepad_bindings:
            return "controller_gamepad_bindings";
        case Screen::extras:
            return "extras";
        case Screen::extras_unlocks:
            return "extras_unlocks";
        case Screen::extras_camera:
            return "extras_camera";
        case Screen::extras_effects:
            return "extras_effects";
        case Screen::extras_racing:
            return "extras_racing";
        case Screen::race_menu:
            return "race_menu";
        case Screen::network_linkup:
            return "network_linkup";
        case Screen::network_options:
            return "network_options";
        case Screen::single_options:
            return "single_options";
        case Screen::time_trial_options:
            return "time_trial_options";
        case Screen::championship_load_slots:
            return "championship_load_slots";
        case Screen::championship_save_prompt:
            return "championship_save_prompt";
        case Screen::championship_save_slots:
            return "championship_save_slots";
        case Screen::championship_save_overwrite:
            return "championship_save_overwrite";
        case Screen::championship_save_feedback:
            return "championship_save_feedback";
        case Screen::championship_honors:
            return "championship_honors";
        case Screen::championship_finale_loading:
            return "championship_finale_loading";
        case Screen::championship_finale:
            return "championship_finale";
        case Screen::time_trial_records:
            return "time_trial_records";
        case Screen::championship_records:
            return "championship_records";
        case Screen::championship_menu:
            return "championship_menu";
        case Screen::championship_options:
            return "championship_options";
        case Screen::championship_overview:
            return "championship_overview";
        case Screen::difficulty:
            return "difficulty";
        case Screen::name_entry:
            return "name_entry";
        case Screen::car_selection:
            return "car_selection";
        case Screen::track_selection:
            return "track_selection";
        case Screen::scene_loading:
            return "scene_loading";
        case Screen::scene:
            return "scene";
    }
    throw std::logic_error("Unknown frontend screen");
}

int menu_row_y(std::size_t count, std::size_t index) {
    // FUN_0046e427 with FUN_0046ee06(0): offset (33,15), row step 0x44.
    return 224 - (static_cast<int>(count) * 68 - 14) / 2 + 15 + static_cast<int>(index) * 68;
}

int settings_row_y(std::size_t index) {
    return 88 + static_cast<int>(index) * 38;
}

std::vector<Cue> Controller::update(const InputSnapshot& input, const ClockStep& clock) {
    std::vector<Cue> cues;
    if (input.quit_requested) {
        quit_ = true;
        return cues;
    }
    if (quit_) {
        return cues;
    }
    if (screen_ == Screen::title) {
        constexpr std::uint64_t duration = 5'000'000'000;
        title_elapsed_ns_ += std::min(clock.elapsed_ns, duration - title_elapsed_ns_);
        if ((input.focused && (input.any_key_pressed || (input.pressed & ~action_mask(Action::pointer_primary)) != 0)) || title_elapsed_ns_ == duration) {
            screen_ = Screen::copyright;
        }
        return cues;
    }
    if (screen_ == Screen::scene_loading && setup_.mode == GameMode::linkup && input.focused && (input.pressed & action_mask(Action::back))) {
        network_requests_.push_back({.kind = NetworkLobbyCommandKind::leave});
        return {}; // Application cleans partially constructed resources and reloads frontend audio before any cue.
    }
    if (screen_ == Screen::copyright || screen_ == Screen::scene_loading || screen_ == Screen::scene || screen_ == Screen::championship_finale_loading
        || screen_ == Screen::championship_finale) {
        return cues;
    }
    if (!input.focused) {
        return cues;
    }
    animation_time_ns_ += clock.elapsed_ns;
    requested_transition_direction_ = (input.pressed & action_mask(Action::back)) != 0 ? -1 : 1;
    constexpr std::uint64_t flash_period = 533'333'334;
    highlight_elapsed_ns_ = (highlight_elapsed_ns_ + clock.elapsed_ns % flash_period) % flash_period;
    const auto pressed = [&](Action action) {
        return (input.pressed & action_mask(action)) != 0;
    };
    if (screen_ == Screen::network_linkup) {
        const auto result = network_lobby_.update(input, network_view_);
        selection_ = network_lobby_.selection();
        if (result.command) {
            network_requests_.push_back(*result.command);
            if (result.command->kind == NetworkLobbyCommandKind::leave) {
                network_connecting_ = network_waiting_commit_ = network_selecting_ = false;
            }
        }
        if (result.enter_name) {
            network_selecting_ = true;
            enter_name();
        } else if (result.withdraw) {
            network_waiting_commit_ = false;
            network_requests_.push_back({.kind = NetworkLobbyCommandKind::ready, .ready = false});
            if (network_lobby_.hosting()) {
                track_id_ = stored_track_id_ <= track_max_id() ? stored_track_id_ : 0;
                track_preview_ticks_ = 0;
                track_preview_fraction_ = 0;
                enter(Screen::track_selection);
            } else {
                car_ids_[0] = accepted_car_ids_[0];
                enter(Screen::car_selection);
            }
        } else if (result.back) {
            network_selecting_ = false;
            enter(Screen::race_menu, 3);
        }
        if (result.cue) {
            return {static_cast<Cue>(*result.cue)};
        }
        return cues;
    }
    if (screen_ == Screen::controller_keyboard_bindings || screen_ == Screen::controller_gamepad_bindings) {
        const bool keyboard = screen_ == Screen::controller_keyboard_bindings;
        if (pressed(Action::back)) {
            binding_capture_error_ = false;
            binding_feedback_remaining_ns_ = 0;
            enter(binding_return_screen_, binding_return_selection_);
            return {Cue::back};
        }
        if (binding_feedback_remaining_ns_ != 0) {
            binding_feedback_remaining_ns_ -= std::min(clock.elapsed_ns, binding_feedback_remaining_ns_);
            if (binding_feedback_remaining_ns_ != 0 && !input.physical_binding) {
                return cues;
            }
            // 473d59 shows OK for at most seven nominal30Hz calls. Native
            // fresh edges shorten that feedback without losing the next input.
            binding_feedback_remaining_ns_ = 0;
            if (binding_capture_index_ == captured_bindings_.size()) {
                auto& selected_input = controller_player_ == 0 ? editing_settings_.input : editing_settings_.input_player2;
                auto& destination = keyboard ? selected_input.keyboard : selected_input.gamepad;
                destination = captured_bindings_;
                settings_saved_ = false;
                settings_error_.clear();
                request_settings();
                enter(binding_return_screen_, binding_return_selection_);
                return cues;
            }
            selection_ = binding_capture_index_;
        }
        if (!input.physical_binding) {
            return cues;
        }
        const auto binding = *input.physical_binding;
        const bool valid = keyboard ? valid_keyboard_binding(binding, binding_capture_index_) : valid_gamepad_binding(binding);
        const auto end = captured_bindings_.begin() + binding_capture_index_;
        if (!valid || std::find_if(captured_bindings_.begin(), end, [&](const auto& other) {
                return same_physical_binding(other, binding);
            }) != end) {
            binding_capture_error_ = true;
            return {Cue::unavailable};
        }
        captured_bindings_[binding_capture_index_++] = binding;
        binding_capture_error_ = false;
        binding_feedback_remaining_ns_ = 233'333'334; // ceil(7/30s), independent of presentation FPS.
        selection_ = binding_capture_index_ - 1;
        return {Cue::confirm};
    }
    if (screen_ == Screen::championship_honors) {
        if (pressed(Action::confirm) || pressed(Action::back)) {
            championship_request_ = ChampionshipRequest::advance;
            return {pressed(Action::confirm) ? Cue::confirm : Cue::back};
        }
        return cues;
    }
    if (screen_ == Screen::championship_save_feedback) {
        const std::uint64_t duration = championship_saved_ ? 3'000'000'000 : 2'000'000'000;
        championship_feedback_ns_ += std::min(clock.elapsed_ns, duration - championship_feedback_ns_);
        if (championship_feedback_ns_ == duration) {
            if (championship_saved_) {
                finish_championship_save();
            } else {
                enter(Screen::championship_save_prompt);
            }
        }
        return cues;
    }
    if (screen_ == Screen::championship_load_slots) {
        const auto& slots = championship_slots();
        const bool any = std::ranges::any_of(slots, [](const auto& slot) {
            return slot.state.has_value();
        });
        if (pressed(Action::back) || (!any && pressed(Action::confirm))) {
            enter(Screen::championship_menu, 1);
            return {Cue::back};
        }
        if (pressed(Action::confirm)) {
            if (!item_available(selection_)) {
                return {Cue::unavailable};
            }
            championship_slot_ = static_cast<std::uint32_t>(selection_);
            championship_ = slots[selection_].state;
            championship_error_.clear();
            championship_request_ = ChampionshipRequest::resume;
            return {Cue::confirm};
        }
        if (any && (pressed(Action::up) || pressed(Action::down))) {
            do {
                selection_ = (selection_ + (pressed(Action::up) ? slots.size() - 1 : 1)) % slots.size();
            } while (!slots[selection_].state);
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        return cues;
    }
    if (screen_ == Screen::championship_save_prompt || screen_ == Screen::championship_save_overwrite) {
        const bool overwrite = screen_ == Screen::championship_save_overwrite;
        if (pressed(Action::back)) {
            if (overwrite) {
                championship_saved_ = false;
                championship_feedback_ns_ = 0;
                enter(Screen::championship_save_feedback);
            } else {
                finish_championship_save();
            }
            return {Cue::back};
        }
        if (pressed(Action::up) || pressed(Action::down) || pressed(Action::left) || pressed(Action::right)) {
            selection_ = 1 - selection_;
            return {Cue::navigation};
        }
        if (pressed(Action::confirm)) {
            if (selection_ == 1) {
                if (overwrite) {
                    championship_saved_ = false;
                    championship_feedback_ns_ = 0;
                    enter(Screen::championship_save_feedback);
                } else {
                    finish_championship_save();
                }
            } else if (overwrite) {
                championship_request_ = ChampionshipRequest::save;
            } else {
                enter(Screen::championship_save_slots, championship_slot_);
            }
            return {Cue::confirm};
        }
        return cues;
    }
    if (screen_ == Screen::championship_save_slots) {
        if (pressed(Action::back) || (pressed(Action::confirm) && selection_ == championship_slot_count)) {
            championship_saved_ = false;
            championship_feedback_ns_ = 0;
            championship_error_.clear();
            enter(Screen::championship_save_feedback);
            return {Cue::back};
        }
        if (pressed(Action::confirm)) {
            championship_slot_ = static_cast<std::uint32_t>(selection_);
            const auto& slot = championship_slots()[selection_];
            if (slot.state || !slot.error.empty()) {
                enter(Screen::championship_save_overwrite, 1);
            } else {
                championship_request_ = ChampionshipRequest::save;
            }
            return {Cue::confirm};
        }
        if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? championship_slot_count : 1)) % (championship_slot_count + 1);
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        return cues;
    }
    if (screen_ == Screen::championship_overview) {
        if (pressed(Action::back)) {
            enter(Screen::main_menu);
            return {Cue::back};
        }
        if (pressed(Action::confirm)) {
            if (!championship_ || championship_->phase == race::ChampionshipPhase::eliminated || championship_->phase == race::ChampionshipPhase::complete) {
                enter(Screen::main_menu);
            } else {
                championship_request_ = ChampionshipRequest::advance;
            }
            return {Cue::confirm};
        }
        if (pressed(Action::left) || pressed(Action::right)) {
            selection_ = 1 - selection_;
            return {Cue::navigation};
        }
        return cues;
    }
    if (screen_ == Screen::time_trial_records) {
        // 46bd22/46bf63 consume Enter and Escape only, not arrow navigation.
        if (pressed(Action::confirm)) {
            if (selection_ == 0) {
                enter(Screen::time_trial_records, 1);
            } else if (records_after_trial_) {
                records_after_trial_ = false;
                enter(Screen::main_menu, record_return_selection_);
            } else {
                enter(Screen::championship_records);
            }
            return {Cue::confirm};
        }
        if (pressed(Action::back)) {
            if (selection_ == 1) {
                enter(Screen::time_trial_records);
            } else {
                enter(Screen::main_menu, record_return_selection_);
            }
            return {Cue::back};
        }
        return cues;
    }
    if (screen_ == Screen::championship_records) {
        //46c534/46c5a1: the first row changes after17 active30Hz calls.
        //Its initial draw uses97; global flash state initially starts at40.
        constexpr std::uint64_t second = 1'000'000'000;
        const auto fraction = championship_record_fraction_ + (clock.elapsed_ns % second) * 30;
        const auto ticks = championship_record_ticks_ + clock.elapsed_ns / second * 30 + fraction / second;
        championship_record_fraction_ = fraction % second;
        championship_record_ticks_ = std::uint32_t(ticks % 17);
        if (ticks >= 17) {
            championship_record_phase_ = std::uint32_t((championship_record_phase_ + ticks / 17) % 3);
            constexpr std::array<std::uint8_t, 3> colors{0x28, 0x61, 0x5d};
            championship_record_color_ = colors[championship_record_phase_];
        }
        if (pressed(Action::confirm)) {
            enter(Screen::main_menu, record_return_selection_);
            return {Cue::confirm};
        }
        if (pressed(Action::back)) {
            enter(Screen::time_trial_records, 1);
            return {Cue::back};
        }
        return cues;
    }
    if (screen_ == Screen::name_entry) {
        // FUN_004780ab toggles after ten active frontend iterations. Preserve
        // exact 30 Hz fractions without tying those iterations to presentation.
        constexpr std::uint64_t second = 1'000'000'000;
        const auto fraction = (clock.elapsed_ns % second) * 30 + name_cursor_fraction_;
        const auto ticks = (clock.elapsed_ns / second) * 30 + fraction / second + name_cursor_ticks_;
        name_cursor_fraction_ = fraction % second;
        name_cursor_ticks_ = static_cast<std::uint32_t>(ticks % 10);
        if (ticks >= 10) {
            if ((ticks / 10) % 2 != 0) {
                name_cursor_phase_ = !name_cursor_phase_;
            }
            name_cursor_drawn_ = name_cursor_phase_;
        }
        // The editor's Escape precedence differs from the generic menu adapter.
        if (pressed(Action::back)) {
            if (setup_.mode == GameMode::linkup) {
                network_requests_.push_back({.kind = NetworkLobbyCommandKind::leave});
                network_connecting_ = network_waiting_commit_ = false;
                network_lobby_.show_endpoint();
                enter(Screen::network_linkup, network_lobby_.selection());
            } else if (selection_player_ == 1) {
                selection_player_ = 0;
                car_ids_[0] = accepted_car_ids_[0];
                enter(Screen::car_selection);
            } else if (setup_.mode == GameMode::championship) {
                enter(Screen::championship_options, 1);
            } else {
                enter(setup_.mode == GameMode::time_trial ? Screen::time_trial_options : Screen::difficulty, setup_.mode == GameMode::time_trial ? 2 : setup_.difficulty);
            }
            cues.push_back(Cue::back);
        } else if (pressed(Action::confirm)) {
            const auto result = cheats::redeem(cheats_, editing_name_, static_cast<std::uint32_t>(setup_.mode));
            if (result.accepted) {
                // 476bfb consumes accepted codes and clears the stored name,
                // leaving the editor open for the driver's actual name.
                accepted_names_[selection_player_].clear();
                editing_name_.clear();
                name_edited_ = true;
                name_cursor_drawn_ = true;
                cheats_changed_ |= result.changed;
                cheats_error_.clear();
                return {result.activated ? Cue::cheat_activated : Cue::option};
            }
            if (setup_.mode == GameMode::single_race) {
                setup_.difficulty = pending_difficulty_;
            }
            accepted_names_[selection_player_] = editing_name_.empty() ? name_defaults_[language_] : editing_name_;
            editing_name_ = accepted_names_[selection_player_];
            car_ids_[selection_player_] = accepted_car_ids_[selection_player_];
            if (setup_.mode == GameMode::linkup) {
                const auto local = std::ranges::find(network_view_.roster.players, network_view_.local_player, &network::LobbyPlayer::player);
                if (network_view_.phase == network::SessionPhase::lobby && local != network_view_.roster.players.end() && local->name == editing_name_) {
                    enter(network_lobby_.hosting() ? Screen::network_options : Screen::car_selection, network_lobby_.hosting() ? 4 : 0);
                } else {
                    if (network_view_.phase != network::SessionPhase::idle) {
                        network_requests_.push_back({.kind = NetworkLobbyCommandKind::leave});
                    }
                    network_requests_.push_back(network_lobby_.connect(editing_name_, static_cast<std::uint8_t>(car_id()), transmission_manual()));
                    network_connecting_ = true;
                    enter(Screen::network_linkup);
                }
            } else {
                enter(Screen::car_selection);
            }
            cues.push_back(Cue::confirm);
        } else if (pressed(Action::backspace)) {
            name_edited_ = true;
            name_cursor_drawn_ = true; // FUN_00478354 also draws for an empty name.
            if (!editing_name_.empty()) {
                editing_name_.pop_back();
                cues.push_back(Cue::character);
            }
        } else {
            for (unsigned char character : input.text_input) {
                if (character >= 'a' && character <= 'z') {
                    character = character - 'a' + 'A';
                }
                if (!((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == ' ')) {
                    continue;
                }
                if (!name_edited_) {
                    editing_name_.clear();
                    name_edited_ = true;
                    name_cursor_ticks_ = 0; // FUN_00477f27, only on first accepted text.
                    name_cursor_fraction_ = 0;
                }
                name_cursor_drawn_ = true; // FUN_004783c1 draws even at the length bound.
                if (editing_name_.size() < 9) {
                    editing_name_.push_back(static_cast<char>(character));
                    cues.push_back(Cue::character);
                }
            }
        }
        return cues;
    }
    if (screen_ == Screen::track_selection) {
        // FUN_0046d433 rotates the miniature by -34/4096 turns per active
        // frontend iteration. Keep the original phase across track changes.
        constexpr std::uint64_t second = 1'000'000'000;
        const auto fraction = (clock.elapsed_ns % second) * 30 + track_preview_fraction_;
        const auto ticks = (clock.elapsed_ns / second) * 30 + fraction / second;
        track_preview_fraction_ = fraction % second;
        track_preview_ticks_ = static_cast<std::uint32_t>((track_preview_ticks_ + ticks) % 2048);
    }
    constexpr auto directions = action_mask(Action::left) | action_mask(Action::right) | action_mask(Action::up) | action_mask(Action::down);
    if (!pressed(Action::confirm) && !pressed(Action::back) && std::popcount(input.pressed & directions) > 1) {
        return cues;
    }
    constexpr std::array option_screens{Screen::sound_options, Screen::graphics_options, Screen::controller_players};
    if (screen_ == Screen::settings) {
        if (pressed(Action::back) || (pressed(Action::confirm) && selection_ == option_screens.size())) {
            enter(Screen::main_menu, 1);
            return {Cue::back};
        }
        if (pressed(Action::confirm)) {
            editing_settings_ = effective_settings_;
            settings_saved_ = false;
            if (selection_ == 0 && !settings_error_.empty()) {
                request_settings();
            } else if (selection_ == 1 && detail_save_failed_) {
                request_settings(true);
            } else {
                settings_error_.clear();
            }
            enter(option_screens[selection_]);
            return {Cue::confirm};
        }
        if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? option_screens.size() : 1)) % (option_screens.size() + 1);
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        return cues;
    }
    if (screen_ == Screen::graphics_options || screen_ == Screen::sound_options) {
        const bool sound = screen_ == Screen::sound_options;
        const std::size_t back_row = sound ? 4 : 7;
        if (pressed(Action::back) || (pressed(Action::confirm) && selection_ == back_row)) {
            if (sound) {
                // Sound changes are immediate; leaving retries failed persistence.
                if (!settings_error_.empty()) {
                    request_settings();
                }
            } else {
                if (detail_save_failed_) {
                    request_settings(true);
                }
                editing_settings_ = effective_settings_;
                if (!detail_settings_request_) {
                    settings_request_.reset();
                }
                settings_saved_ = false;
                if (!detail_save_failed_) {
                    settings_error_.clear();
                }
            }
            enter(Screen::settings, sound ? 0 : 1);
            return {Cue::back};
        }
        if (!sound && pressed(Action::confirm) && selection_ == 4) {
            request_settings();
            return {Cue::confirm};
        }
        if (selection_ < back_row && (sound || selection_ != 4) && (pressed(Action::confirm) || pressed(Action::left) || pressed(Action::right))) {
            const auto before = editing_settings_;
            change_setting(pressed(Action::confirm) || pressed(Action::right), sound && pressed(Action::confirm));
            if (before != editing_settings_) {
                settings_saved_ = false;
                const bool detail = !sound && selection_ == 5;
                if (sound || detail || !detail_save_failed_) {
                    settings_error_.clear();
                }
                if (sound || detail) {
                    request_settings(detail);
                }
                highlight_elapsed_ns_ = 0;
                return {Cue::option};
            }
        } else if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? back_row : 1)) % (back_row + 1);
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        return cues;
    }
    if (screen_ == Screen::controller_players) {
        if (pressed(Action::back) || (pressed(Action::confirm) && selection_ == 2)) {
            if (!settings_error_.empty()) {
                request_settings();
            }
            enter(Screen::settings, 2);
            return {Cue::back};
        }
        if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? 2 : 1)) % 3;
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        if (pressed(Action::confirm)) {
            if (!item_available(selection_)) {
                return {Cue::unavailable};
            }
            controller_player_ = selection_;
            enter(Screen::controller_options);
            return {Cue::confirm};
        }
        return cues;
    }
    if (screen_ == Screen::controller_auto_configure) {
        if (pressed(Action::back) || (pressed(Action::confirm) && selection_ == 2)) {
            if (!settings_error_.empty()) {
                request_settings();
            }
            enter(Screen::controller_options, 1);
            return {Cue::back};
        }
        if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? 2 : 1)) % 3;
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        if (pressed(Action::confirm)) {
            if (!item_available(selection_)) {
                return {Cue::unavailable};
            }
            begin_binding_capture(selection_ == 0);
            return {Cue::confirm};
        }
        return cues;
    }
    if (screen_ == Screen::controller_options || screen_ == Screen::controller_native_options) {
        const bool native = screen_ == Screen::controller_native_options;
        auto& selected_input = controller_player_ == 0 ? editing_settings_.input : editing_settings_.input_player2;
        const std::size_t back_row = native ? 3 : 4;
        if (pressed(Action::back) || (pressed(Action::confirm) && selection_ == back_row)) {
            if (!settings_error_.empty()) {
                request_settings();
            }
            enter(native ? Screen::controller_options : Screen::controller_players, native ? 3 : controller_player_);
            return {Cue::back};
        }
        if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? back_row : 1)) % (back_row + 1);
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        if (!pressed(Action::confirm) && !pressed(Action::left) && !pressed(Action::right)) {
            return cues;
        }
        const bool right = pressed(Action::confirm) || pressed(Action::right);
        if (!native && selection_ == 1) {
            if (!pressed(Action::confirm)) {
                return cues;
            }
            if (!item_available(selection_)) {
                return {Cue::unavailable};
            }
            if (selected_input.device_id == automatic_input_device) {
                enter(Screen::controller_auto_configure);
            } else {
                begin_binding_capture(selected_input.device_id.empty());
            }
            return {Cue::confirm};
        }
        if (!native && selection_ == 3) {
            if (pressed(Action::confirm)) {
                enter(Screen::controller_native_options);
                return {Cue::confirm};
            }
            return cues;
        }
        const auto before = selected_input;
        if (!native && selection_ == 0) {
            const auto choices = input_device_choices();
            const auto found = std::ranges::find(choices, selected_input.device_id);
            if (found == choices.end()) {
                // A saved disconnected pad can always be replaced with Keyboard.
                selected_input.device_id = choices[right && choices.size() > 1 ? 1 : 0];
            } else if (right && found + 1 != choices.end()) {
                selected_input.device_id = *(found + 1);
            } else if (!right && found != choices.begin()) {
                selected_input.device_id = *(found - 1);
            }
            const bool raw_bindings = std::ranges::any_of(selected_input.gamepad, [](const auto& binding) {
                return binding.kind == BindingKind::joystick_axis || binding.kind == BindingKind::joystick_button;
            });
            if (selected_input.device_id.starts_with("joystick:") && !raw_bindings) {
                selected_input.gamepad = default_joystick_bindings();
            } else if (!selected_input.device_id.empty() && !selected_input.device_id.starts_with("joystick:") && raw_bindings) {
                selected_input.gamepad = InputSettings{}.gamepad;
            }
        } else if (!native && selection_ == 2) {
            selected_input.sensitivity = 1 + (selected_input.sensitivity - 1 + (right ? 1 : 2)) % 3;
        } else if (native && selection_ == 0) {
            auto& deadzone = selected_input.deadzone_percent;
            deadzone = right ? std::min(deadzone + 1, 30u) : deadzone == 0 ? 0 : deadzone - 1;
        } else if (native && selection_ == 1 && pressed(Action::confirm)) {
            selected_input = controller_player_ == 0 ? InputSettings{} : default_player2_input();
        } else if (native && selection_ == 2) {
            selected_input.rumble_enabled = !selected_input.rumble_enabled;
        }
        if (before != selected_input) {
            settings_saved_ = false;
            settings_error_.clear();
            request_settings();
            highlight_elapsed_ns_ = 0;
            return {Cue::option};
        }
        return cues;
    }
    constexpr std::array category_screens{Screen::extras_unlocks, Screen::extras_camera, Screen::extras_effects, Screen::extras_racing};
    const auto category_screen = std::ranges::find(category_screens, screen_);
    if (category_screen != category_screens.end()) {
        const auto category_index = static_cast<std::size_t>(category_screen - category_screens.begin());
        const auto codes = cheats::category_codes(static_cast<cheats::Category>(category_index));
        if (pressed(Action::back) || (pressed(Action::confirm) && selection_ == codes.size())) {
            cheats_changed_ |= !cheats_error_.empty();
            enter(Screen::extras, category_index);
            return {Cue::back};
        }
        if (selection_ < codes.size() && (pressed(Action::confirm) || pressed(Action::left) || pressed(Action::right))) {
            const auto code = codes[selection_];
            const bool enabled = pressed(Action::confirm) ? !cheats::menu_active(cheats_, code) : pressed(Action::right);
            const auto result = cheats::set_menu_active(cheats_, code, enabled);
            if (!result.accepted) {
                return {Cue::unavailable};
            }
            if (result.changed) {
                cheats_changed_ = true;
                cheats_error_.clear();
                highlight_elapsed_ns_ = 0;
                return {Cue::option};
            }
        } else if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? codes.size() : 1)) % (codes.size() + 1);
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        return cues;
    }
    if (screen_ == Screen::extras) {
        if (pressed(Action::back) || (pressed(Action::confirm) && selection_ == category_screens.size())) {
            // A failed automatic save can be retried by leaving Extras.
            cheats_changed_ |= !cheats_error_.empty();
            enter(Screen::main_menu, 3);
            return {Cue::back};
        }
        if (pressed(Action::confirm)) {
            enter(category_screens[selection_]);
            return {Cue::confirm};
        }
        if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? category_screens.size() : 1)) % (category_screens.size() + 1);
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        return cues;
    }
    if (screen_ == Screen::car_selection) {
        if (pressed(Action::confirm)) {
            if (!car_available()) {
                return {Cue::unavailable};
            }
            // FUN_00477586 commits the car before entering Track, not RaceLoad.
            accepted_car_ids_[selection_player_] = car_ids_[selection_player_];
            if (setup_.mode == GameMode::championship) {
                championship_request_ = ChampionshipRequest::start;
                return {Cue::confirm};
            }
            if (setup_.players == 2 && selection_player_ == 0) {
                selection_player_ = 1;
                enter_name();
                return {Cue::confirm};
            }
            selection_player_ = 0;
            editing_name_ = accepted_names_[0];
            if (setup_.mode == GameMode::linkup && !network_lobby_.hosting()) {
                enter_network_waiting();
                return {Cue::confirm};
            }
            track_id_ = stored_track_id_ <= track_max_id() ? stored_track_id_ : 0;
            track_preview_ticks_ = 0;
            track_preview_fraction_ = 0;
            enter(Screen::track_selection);
            cues.push_back(Cue::confirm);
        } else if (pressed(Action::back)) {
            if (setup_.mode == GameMode::linkup && network_lobby_.hosting()) {
                enter(Screen::network_options, 4);
            } else {
                enter_name();
            }
            cues.push_back(Cue::back);
        } else if (pressed(Action::left)) {
            if (car_ids_[selection_player_] > 0) {
                --car_ids_[selection_player_];
                cues.push_back(Cue::navigation);
            }
        } else if (pressed(Action::right)) {
            if (car_ids_[selection_player_] < 9) {
                ++car_ids_[selection_player_];
                cues.push_back(Cue::navigation);
            }
        } else if (pressed(Action::up) || pressed(Action::down)) {
            transmissions_manual_[selection_player_] = !transmissions_manual_[selection_player_];
            cues.push_back(Cue::navigation);
        }
        return cues;
    }
    if (screen_ == Screen::track_selection) {
        if (pressed(Action::confirm)) {
            if (track_available()) {
                // 0046cb8a commits +1c before state7 hands off to scene loading.
                stored_track_id_ = track_id_;
                if (setup_.mode == GameMode::linkup) {
                    enter_network_waiting();
                } else {
                    enter(Screen::scene_loading);
                }
                cues.push_back(Cue::confirm);
            } else {
                cues.push_back(Cue::unavailable);
            }
        } else if (pressed(Action::back)) {
            // FUN_0046cb8a stores even a locked preview on Back.
            stored_track_id_ = track_id_;
            selection_player_ = setup_.players - 1;
            car_ids_[selection_player_] = accepted_car_ids_[selection_player_];
            enter(Screen::car_selection);
            cues.push_back(Cue::back);
        } else if (pressed(Action::left)) {
            if (track_id_ > 0) {
                --track_id_;
                cues.push_back(Cue::navigation);
            }
        } else if (pressed(Action::right)) {
            if (track_id_ < track_max_id()) {
                ++track_id_;
                cues.push_back(Cue::navigation);
            }
        }
        return cues;
    }
    if (screen_ == Screen::network_options) {
        if (pressed(Action::back)) {
            enter_name();
            return {Cue::back};
        }
        if (pressed(Action::confirm) && selection_ == 4) {
            car_ids_[0] = accepted_car_ids_[0];
            enter(Screen::car_selection);
            return {Cue::confirm};
        }
        if ((pressed(Action::confirm) || pressed(Action::left) || pressed(Action::right)) && selection_ < 4) {
            const bool right = pressed(Action::confirm) || pressed(Action::right);
            if (selection_ == 0) {
                setup_.lap_choice = (setup_.lap_choice + (right ? 1 : 6)) % 7;
                setup_.laps = setup_.lap_choice < 3 ? setup_.lap_choice + 1 : (setup_.lap_choice - 2) * 5;
            } else if (selection_ == 1) {
                setup_.weather = (setup_.weather + (right ? 1 : 5)) % 6;
            } else if (selection_ == 2) {
                setup_.car_boost = !setup_.car_boost;
            } else {
                network_ai_ = !network_ai_;
            }
            highlight_elapsed_ns_ = 0;
            return {Cue::option};
        }
        if (pressed(Action::up) || pressed(Action::down)) {
            selection_ = (selection_ + (pressed(Action::up) ? 4 : 1)) % 5;
            highlight_elapsed_ns_ = 0;
            return {Cue::navigation};
        }
        return cues;
    }
    const bool confirm = pressed(Action::confirm);
    if (!confirm && pressed(Action::back)) {
        switch (screen_) {
            case Screen::race_menu:
                enter(Screen::main_menu);
                break;
            case Screen::championship_menu:
                enter(Screen::race_menu, setup_.championship_showdown && cheats::showdown_available(cheats_) ? 4 : 1);
                break;
            case Screen::championship_options:
                enter(Screen::championship_menu);
                break;
            case Screen::single_options:
            case Screen::time_trial_options:
                enter(Screen::race_menu, setup_.mode == GameMode::championship ? (setup_.championship_showdown && cheats::showdown_available(cheats_) ? 4 : 1)
                                         : setup_.mode == GameMode::time_trial ? 2
                                                                               : 0);
                break;
            case Screen::difficulty:
                enter(Screen::single_options, 4);
                break;
            default:
                return cues;
        }
        return {Cue::back};
    }
    const auto change_option = [&](bool right) {
        if (screen_ == Screen::championship_options) {
            if (selection_ == 0) {
                setup_.championship_full = !setup_.championship_full;
                cues.push_back(Cue::option);
                highlight_elapsed_ns_ = 0;
            }
            return;
        }
        if (screen_ == Screen::time_trial_options) {
            if (selection_ == 0) {
                setup_.weather = (setup_.weather + (right ? 1 : 5)) % 6;
            } else if (selection_ == 1) {
                setup_.ghost_enabled = !setup_.ghost_enabled;
            } else {
                return;
            }
            cues.push_back(Cue::option);
            highlight_elapsed_ns_ = 0;
            return;
        }
        if (selection_ == 0) {
            if (setup_.players == 1) {
                single_player_qualifying_ = setup_.qualifying;
                setup_.players = 2;
                setup_.qualifying = false; // 44f0a3 forces NoQual for local multiplayer.
            } else {
                setup_.players = 1;
                setup_.qualifying = single_player_qualifying_;
            }
        } else if (selection_ == 1) {
            setup_.lap_choice = (setup_.lap_choice + (right ? 1 : 6)) % 7;
            setup_.laps = setup_.lap_choice < 3 ? setup_.lap_choice + 1 : (setup_.lap_choice - 2) * 5;
        } else if (selection_ == 2) {
            if (setup_.players > 1) {
                setup_.car_boost = !setup_.car_boost; // 469386 toggles config +2c for local multiplayer and Linkup.
            } else {
                setup_.qualifying = !setup_.qualifying;
            }
        } else if (selection_ == 3) {
            setup_.weather = (setup_.weather + (right ? 1 : 5)) % 6;
        } else {
            return;
        }
        cues.push_back(Cue::option);
        highlight_elapsed_ns_ = 0;
    };
    if (confirm) {
        if (!item_available(selection_)) {
            return {Cue::unavailable};
        }
        switch (screen_) {
            case Screen::language:
                language_ = static_cast<std::uint32_t>(selection_);
                enter(Screen::main_menu);
                break;
            case Screen::main_menu:
                if (selection_ == 4) {
                    quit_ = true;
                } else if (selection_ == 1) {
                    enter(Screen::settings);
                } else if (selection_ == 3) {
                    enter(Screen::extras);
                } else if (selection_ == 2) {
                    record_return_selection_ = 2;
                    records_after_trial_ = false;
                    record_highlight_track_.reset();
                    enter(Screen::time_trial_records);
                } else {
                    // 4691a9 restores the previously chosen race mode.
                    enter(Screen::race_menu, setup_.mode == GameMode::championship ? (setup_.championship_showdown && cheats::showdown_available(cheats_) ? 4 : 1)
                                             : setup_.mode == GameMode::time_trial ? 2
                                             : setup_.mode == GameMode::linkup     ? 3
                                                                                   : 0);
                }
                break;
            case Screen::race_menu:
                selection_player_ = 0;
                if (selection_ == 3) {
                    if (setup_.mode == GameMode::single_race) {
                        single_lap_choice_ = setup_.lap_choice;
                        single_qualifying_ = setup_.qualifying;
                    }
                    network_lobby_.reset();
                    network_selecting_ = true;
                    network_connecting_ = network_waiting_commit_ = false;
                    setup_.mode = GameMode::linkup;
                    setup_.players = 1;
                    setup_.qualifying = false;
                    enter(Screen::network_linkup);
                } else if (selection_ == 1 || selection_ == 4) {
                    setup_.championship_showdown = selection_ == 4;
                    if (setup_.mode == GameMode::single_race) {
                        single_lap_choice_ = setup_.lap_choice;
                        single_qualifying_ = setup_.qualifying;
                    }
                    setup_.mode = GameMode::championship;
                    setup_.players = 1;
                    setup_.qualifying = true;
                    setup_.weather = 0;
                    enter(Screen::championship_menu);
                } else if (selection_ == 2) {
                    if (setup_.mode == GameMode::single_race) {
                        single_lap_choice_ = setup_.lap_choice;
                        single_qualifying_ = setup_.qualifying;
                    }
                    setup_.mode = GameMode::time_trial;
                    setup_.players = 1;
                    setup_.lap_choice = 0;
                    setup_.laps = 1;
                    setup_.qualifying = false;
                    enter(Screen::time_trial_options, 2);
                } else {
                    if (setup_.mode != GameMode::single_race) {
                        setup_.lap_choice = single_lap_choice_;
                        setup_.laps = setup_.lap_choice < 3 ? setup_.lap_choice + 1 : (setup_.lap_choice - 2) * 5;
                        setup_.qualifying = single_qualifying_;
                    }
                    setup_.mode = GameMode::single_race;
                    enter(Screen::single_options, 4);
                }
                break;
            case Screen::championship_menu:
                if (selection_ == 0) {
                    enter(Screen::championship_options, 1);
                } else {
                    const auto& slots = championship_slots();
                    std::size_t last = 0;
                    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
                        if (slots[slot].state) {
                            last = slot;
                        }
                    }
                    enter(Screen::championship_load_slots, last);
                }
                break;
            case Screen::championship_options:
                if (selection_ == 0) {
                    change_option(true);
                    return cues;
                }
                // 4697a1 only shows difficulty for Single Race. Championship
                // difficulty comes from its meeting progression.
                enter_name();
                break;
            case Screen::time_trial_options:
                if (selection_ < 2) {
                    change_option(true);
                    return cues;
                }
                enter_name();
                break;
            case Screen::single_options:
                if (selection_ < 4) {
                    change_option(true);
                    return cues;
                }
                enter(Screen::difficulty, setup_.difficulty);
                break;
            case Screen::difficulty:
                pending_difficulty_ = static_cast<std::uint32_t>(selection_);
                enter_name();
                break;
            default:
                throw std::logic_error("Unhandled frontend confirmation");
        }
        return {Cue::confirm};
    }
    const std::size_t count = screen_ == Screen::championship_menu || screen_ == Screen::championship_options                                                              ? 2
                              : screen_ == Screen::language || screen_ == Screen::difficulty || screen_ == Screen::time_trial_options                                      ? 3
                              : screen_ == Screen::single_options || screen_ == Screen::main_menu || (screen_ == Screen::race_menu && cheats::showdown_available(cheats_)) ? 5
                                                                                                                                                                           : 4;
    if (pressed(Action::left) || pressed(Action::right)) {
        if (screen_ == Screen::single_options || screen_ == Screen::time_trial_options || screen_ == Screen::championship_options) {
            change_option(pressed(Action::right));
        } else if (!item_available(selection_)) {
            cues.push_back(Cue::unavailable);
        }
    } else if (pressed(Action::up) || pressed(Action::down)) {
        selection_ = (selection_ + (pressed(Action::up) ? count - 1 : 1)) % count;
        cues.push_back(Cue::navigation);
        highlight_elapsed_ns_ = 0;
    }
    return cues;
}

void Controller::enter(Screen screen, std::size_t selection) {
    menu_transition_direction_ = requested_transition_direction_;
    menu_entered_ns_ = animation_time_ns_;
    screen_ = screen;
    selection_ = selection;
    highlight_elapsed_ns_ = 0;
    if (screen == Screen::championship_records) {
        championship_record_color_ = 0x61;
    }
}

std::uint64_t Controller::animation_time_ns() const {
    return animation_time_ns_;
}

std::uint64_t Controller::menu_entered_ns() const {
    return menu_entered_ns_;
}

int Controller::menu_transition_direction() const {
    return menu_transition_direction_;
}

void Controller::enter_name() {
    editing_name_ = accepted_names_[selection_player_].empty() ? name_defaults_[language_] : accepted_names_[selection_player_];
    if (selection_player_ == 1 && accepted_names_[1].empty()) {
        if (editing_name_.ends_with('1')) {
            editing_name_.back() = '2';
        } else {
            editing_name_ = editing_name_.substr(0, 8) + "2";
        }
    }
    name_edited_ = false;
    name_cursor_ticks_ = 0;
    name_cursor_fraction_ = 0;
    name_cursor_drawn_ = false; // FUN_00477f84 copies the name without drawing its cursor.
    enter(Screen::name_entry);
}

void Controller::set_name_defaults(std::array<std::string, 5> defaults) {
    for (const auto& name : defaults) {
        if (name.empty() || name.size() > 9) {
            throw std::runtime_error("Invalid localized default player name");
        }
    }
    name_defaults_ = std::move(defaults);
}

void Controller::set_settings(Settings effective, std::vector<Resolution> resolutions) {
    effective_settings_ = editing_settings_ = effective;
    resolutions.push_back({effective.video.width, effective.video.height});
    std::sort(resolutions.begin(), resolutions.end());
    resolutions.erase(std::unique(resolutions.begin(), resolutions.end()), resolutions.end());
    resolutions_ = std::move(resolutions);
    fps_choices_ = {0, 30, 60, 90, 120, 144, 165, 240, 360, effective.video.fps_limit};
    std::sort(fps_choices_.begin(), fps_choices_.end());
    fps_choices_.erase(std::unique(fps_choices_.begin(), fps_choices_.end()), fps_choices_.end());
}

void Controller::set_input_devices(std::vector<InputDevice> devices, std::optional<InputDevice> automatic_gamepad) {
    std::vector<std::string> seen;
    for (const auto& device : devices) {
        if (device.name.empty() || std::ranges::find(seen, device.id) != seen.end()) {
            throw std::runtime_error("Controller devices require unique IDs and nonempty names");
        }
        seen.push_back(device.id);
    }
    input_devices_ = std::move(devices);
    automatic_gamepad_ = std::move(automatic_gamepad);
    if (screen_ == Screen::controller_gamepad_bindings && !controller_gamepad_available()) {
        binding_capture_error_ = false;
        binding_feedback_remaining_ns_ = 0;
        enter(binding_return_screen_, binding_return_selection_);
    }
}

const std::vector<InputDevice>& Controller::input_devices() const {
    return input_devices_;
}

std::string Controller::input_device_name() const {
    const auto& id = controller_input().device_id;
    if (id.empty()) {
        return {};
    }
    const auto found = std::ranges::find(input_devices_, id, &InputDevice::id);
    return found == input_devices_.end() ? std::string{} : found->name;
}

std::vector<std::string> Controller::input_device_choices() const {
    std::vector<std::string> result{std::string{}};
    for (const auto& device : input_devices_) {
        if (!device.id.empty()) {
            result.push_back(device.id);
        }
    }
    if (controller_player_ == 0) {
        result.emplace_back(automatic_input_device);
    }
    return result;
}

bool Controller::controller_gamepad_available() const {
    const auto& id = controller_input().device_id;
    if (id == automatic_input_device) {
        return automatic_gamepad_ && !automatic_gamepad_->id.empty() && std::ranges::find(input_devices_, automatic_gamepad_->id, &InputDevice::id) != input_devices_.end();
    }
    return !id.empty() && std::ranges::find(input_devices_, id, &InputDevice::id) != input_devices_.end();
}

void Controller::begin_binding_capture(bool keyboard) {
    binding_return_screen_ = screen_;
    binding_return_selection_ = screen_ == Screen::controller_auto_configure ? (keyboard ? 0 : 1) : 1;
    binding_capture_index_ = 0;
    binding_feedback_remaining_ns_ = 0;
    binding_capture_error_ = false;
    captured_bindings_ = keyboard ? controller_input().keyboard : controller_input().gamepad;
    enter(keyboard ? Screen::controller_keyboard_bindings : Screen::controller_gamepad_bindings);
}

std::size_t Controller::binding_capture_index() const {
    return binding_capture_index_;
}

const std::array<PhysicalBinding, 9>& Controller::binding_capture_values() const {
    return captured_bindings_;
}

bool Controller::binding_capture_error() const {
    return binding_capture_error_;
}

bool Controller::binding_capture_feedback() const {
    return binding_feedback_remaining_ns_ != 0;
}

void Controller::change_setting(bool right, bool cycle_volume) {
    auto& video = editing_settings_.video;
    auto& audio = editing_settings_.audio;
    const auto next = [right](const auto& choices, const auto& value) {
        const auto found = std::find(choices.begin(), choices.end(), value);
        const auto index = static_cast<std::size_t>(found - choices.begin());
        return choices[(index + (right ? 1 : choices.size() - 1)) % choices.size()];
    };
    if (screen_ == Screen::sound_options) {
        switch (selection_) {
            case 0:
            case 1: {
                auto& volume = selection_ == 0 ? audio.effects_volume : audio.music_volume;
                // 47591d initializes the callbacks' step counters with value/10.
                const auto step = volume / 10;
                volume = cycle_volume && step == 10 ? 0 : right ? std::min(10u, step + 1) * 10 : (step > 0 ? step - 1 : 0) * 10;
                break;
            }
            case 2:
                audio.game_music = !audio.game_music;
                break;
            case 3:
                audio.stereo_effects = !audio.stereo_effects;
                break;
        }
        return;
    }
    switch (selection_) {
        case 0: {
            const auto [width, height] = next(resolutions_, Resolution{video.width, video.height});
            video.width = width;
            video.height = height;
            break;
        }
        case 1:
            video.fullscreen = !video.fullscreen;
            break;
        case 2:
            video.vsync = !video.vsync;
            break;
        case 3:
            video.fps_limit = next(fps_choices_, video.fps_limit);
            break;
        case 5:
            // 46a393 cycles the original presets; Mega extends the same row.
            editing_settings_.graphics.detail = (editing_settings_.graphics.detail + (right ? 1u : graphics_detail_count - 1)) % graphics_detail_count;
            break;
        case 6:
            editing_settings_.graphics.split_vertical = !editing_settings_.graphics.split_vertical;
            break;
    }
}

void Controller::request_settings(bool detail_only) {
    settings_request_ = detail_only ? effective_settings_ : editing_settings_;
    if (detail_only) {
        settings_request_->graphics.detail = editing_settings_.graphics.detail;
    }
    detail_settings_request_ = detail_only;
}

std::optional<Settings> Controller::take_settings_request() {
    return std::exchange(settings_request_, std::nullopt);
}

void Controller::settings_applied(Settings effective, std::string error) {
    const auto draft_video = editing_settings_.video;
    const auto draft_split = editing_settings_.graphics.split_vertical;
    const bool preserve_draft = detail_settings_request_ && screen_ == Screen::graphics_options;
    set_settings(effective, resolutions_);
    if (preserve_draft) {
        // Detail saves use the effective display settings; their acknowledgement
        // must not overwrite the separate display/split draft awaiting Apply.
        editing_settings_.video = draft_video;
        editing_settings_.graphics.split_vertical = draft_split;
    }
    detail_save_failed_ = !error.empty() && (detail_settings_request_ || detail_save_failed_);
    detail_settings_request_ = false;
    settings_saved_ = error.empty();
    settings_error_ = std::move(error);
}

const cheats::State& Controller::cheats_state() const {
    return cheats_;
}

void Controller::set_cheats_state(cheats::State state) {
    cheats_ = state;
    cheats_changed_ = false;
    cheats_error_.clear();
}

bool Controller::take_cheats_changed() {
    return std::exchange(cheats_changed_, false);
}

void Controller::cheats_saved(std::string error) {
    cheats_error_ = std::move(error);
}

std::string_view Controller::cheats_error() const {
    return cheats_error_;
}

const Settings& Controller::settings_values() const {
    return editing_settings_;
}

bool Controller::settings_saved() const {
    return settings_saved_;
}

std::string_view Controller::settings_error() const {
    return settings_error_;
}

const RaceSetup& Controller::setup() const {
    return setup_;
}

std::string_view Controller::player_name(std::size_t player) const {
    return player == selection_player_ ? editing_name_ : accepted_names_.at(player);
}

bool Controller::name_cursor_visible() const {
    return screen_ == Screen::name_entry && name_cursor_drawn_;
}

std::uint32_t Controller::car_id(std::size_t player) const {
    return car_ids_.at(player);
}

bool Controller::transmission_manual(std::size_t player) const {
    return transmissions_manual_.at(player);
}

std::size_t Controller::selection_player() const {
    return selection_player_;
}

std::size_t Controller::controller_player() const {
    return controller_player_;
}

const InputSettings& Controller::controller_input() const {
    return controller_player_ == 0 ? editing_settings_.input : editing_settings_.input_player2;
}

bool Controller::car_available() const {
    return cheats::car_available(cheats::for_mode(cheats_, static_cast<std::uint32_t>(setup_.mode)), car_ids_[selection_player_]);
}

std::uint32_t Controller::stored_car_id(std::size_t player) const {
    return accepted_car_ids_.at(player);
}

std::uint32_t Controller::track_id() const {
    return track_id_;
}

std::uint32_t Controller::stored_track_id() const {
    return stored_track_id_;
}

std::uint32_t Controller::track_max_id() const {
    return cheats::track_max_id(cheats::for_mode(cheats_, static_cast<std::uint32_t>(setup_.mode)));
}

bool Controller::track_available() const {
    return scene::track_supported(track_id_) && cheats::track_available(cheats::for_mode(cheats_, static_cast<std::uint32_t>(setup_.mode)), track_id_);
}

std::uint32_t Controller::track_preview_rotation() const {
    return (0x800u - 34u * track_preview_ticks_) & 0xfffu;
}

float Controller::track_preview_presentation_rotation() const {
    const auto angle = double(track_preview_rotation()) - 34 * double(track_preview_fraction_) / 1'000'000'000;
    return float(angle < 0 ? angle + 4096 : angle);
}

void Controller::frontend_ready() {
    if (screen_ != Screen::copyright) {
        throw std::logic_error("Frontend initialization must follow the copyright loading frame");
    }
    enter(Screen::language, language_);
    selection_ = language_;
}

void Controller::scene_ready() {
    if (screen_ != Screen::scene_loading) {
        throw std::logic_error("Scene entry must follow resource loading");
    }
    enter(Screen::scene);
}

void Controller::return_from_scene(bool championship_results) {
    if (screen_ != Screen::scene) {
        throw std::logic_error("Scene return requires an active scene");
    }
    record_return_selection_ = 0;
    records_after_trial_ = setup_.mode == GameMode::time_trial;
    enter(championship_results && setup_.mode == GameMode::championship ? Screen::championship_overview
          : setup_.mode == GameMode::time_trial                         ? Screen::time_trial_records
                                                                        : Screen::main_menu);
}

void Controller::set_championship_state(std::optional<race::ChampionshipState> state, std::string error) {
    championship_ = std::move(state);
    championship_error_ = std::move(error);
}

const std::optional<race::ChampionshipState>& Controller::championship_state() const {
    return championship_;
}

std::string_view Controller::championship_error() const {
    return championship_error_;
}

std::optional<ChampionshipRequest> Controller::take_championship_request() {
    return std::exchange(championship_request_, std::nullopt);
}

void Controller::show_championship_overview() {
    if (!championship_) {
        throw std::logic_error("Championship standings require a saved championship");
    }
    setup_.mode = GameMode::championship;
    enter(Screen::championship_overview);
}

void Controller::show_championship_finale() {
    if (!championship_ || championship_->phase != race::ChampionshipPhase::complete) {
        throw std::logic_error("Championship finale requires a completed season");
    }
    enter(Screen::championship_finale_loading);
}

void Controller::championship_finale_ready() {
    if (screen_ != Screen::championship_finale_loading) {
        throw std::logic_error("Championship finale is not loading");
    }
    enter(Screen::championship_finale);
}

void Controller::return_from_championship_finale() {
    if (screen_ != Screen::championship_finale) {
        throw std::logic_error("Championship finale is not active");
    }
    enter(Screen::main_menu);
}

void Controller::set_championship_slots(bool showdown, ChampionshipSlots slots) {
    championship_slots_[std::size_t(showdown)] = std::move(slots);
}

const ChampionshipSlots& Controller::championship_slots() const {
    return championship_slots_[std::size_t(setup_.championship_showdown)];
}

std::uint32_t Controller::championship_slot() const {
    return championship_slot_;
}

void Controller::show_championship_save_prompt(std::optional<race::ChampionshipHonors> honors) {
    if (!championship_) {
        throw std::logic_error("Championship save prompt requires an active championship");
    }
    setup_.championship_showdown = championship_->showdown;
    championship_honors_ = std::move(honors);
    championship_error_.clear();
    enter(Screen::championship_save_prompt); // 423be0: original Save Game defaults Yes.
}

void Controller::championship_save_result(std::string error) {
    championship_error_ = std::move(error);
    if (!championship_error_.empty()) {
        enter(Screen::championship_save_slots, championship_slot_);
        return;
    }
    championship_slots_[std::size_t(setup_.championship_showdown)][championship_slot_] = {championship_, {}};
    championship_saved_ = true;
    championship_feedback_ns_ = 0;
    enter(Screen::championship_save_feedback);
}

bool Controller::championship_saved() const {
    return championship_saved_;
}

void Controller::finish_championship_save() {
    if (championship_honors_) {
        enter(Screen::championship_honors);
    } else {
        championship_request_ = ChampionshipRequest::advance;
    }
}

void Controller::show_championship_honors(race::ChampionshipHonors honors) {
    championship_honors_ = std::move(honors);
    enter(Screen::championship_honors);
}

const std::optional<race::ChampionshipHonors>& Controller::championship_honors() const {
    return championship_honors_;
}

void Controller::begin_championship_round(const race::ChampionshipState& state, std::uint32_t track, std::uint32_t laps, bool qualifying) {
    championship_ = state;
    selection_player_ = 0;
    setup_.mode = GameMode::championship;
    setup_.players = 1;
    setup_.championship_full = state.full_length;
    setup_.championship_showdown = state.showdown;
    setup_.laps = laps;
    setup_.qualifying = qualifying;
    setup_.weather = 0;
    setup_.ghost_enabled = false;
    setup_.difficulty = 0; // Championship uses its own round-indexed AIBIN bank.
    accepted_names_[selection_player_] = editing_name_ = state.driver;
    car_ids_[selection_player_] = accepted_car_ids_[selection_player_] = state.player_car;
    transmissions_manual_[selection_player_] = state.transmission_manual;
    track_id_ = stored_track_id_ = track;
    enter(Screen::scene_loading);
}

void Controller::set_lap_records(LapRecords records, std::optional<std::uint32_t> improved_track) {
    if (improved_track && *improved_track >= records.size()) {
        throw std::runtime_error("Improved Time Trial record track is outside0..9");
    }
    for (const auto& record : records) {
        if (record.lap_ms == 0 || record.lap_ms >= 999999 || record.driver.size() >= 12 || record.car_id >= 10 || record.code.size() > 15) {
            throw std::runtime_error("Invalid Time Trial frontend record");
        }
    }
    records_ = std::move(records);
    record_highlight_track_ = improved_track;
}

const std::optional<LapRecords>& Controller::records() const {
    return records_;
}

std::optional<std::uint32_t> Controller::record_highlight_track() const {
    return record_highlight_track_;
}

void Controller::set_championship_records(ChampionshipRecords records, std::string error) {
    championship_records_ = std::move(records);
    championship_records_error_ = std::move(error);
}

const std::optional<ChampionshipRecords>& Controller::championship_records() const {
    return championship_records_;
}

std::string_view Controller::championship_records_error() const {
    return championship_records_error_;
}

std::uint8_t Controller::championship_record_color() const {
    return championship_record_color_;
}

void Controller::restart_scene() {
    if (screen_ != Screen::scene) {
        throw std::logic_error("Scene restart requires an active scene");
    }
    enter(Screen::scene_loading);
}

Screen Controller::screen() const {
    return screen_;
}

std::size_t Controller::selection() const {
    return selection_;
}

std::uint32_t Controller::language() const {
    return language_;
}

bool Controller::quit_requested() const {
    return quit_;
}

bool Controller::item_available(std::size_t index) const {
    switch (screen_) {
        case Screen::network_linkup:
            return network_lobby_.item_available(index, network_view_);
        case Screen::controller_players:
            return index < 3;
        case Screen::controller_options:
            return index != 1 || controller_input().device_id.empty() || controller_input().device_id == automatic_input_device || controller_gamepad_available();
        case Screen::controller_auto_configure:
            return index != 1 || controller_gamepad_available();
        case Screen::main_menu:
            return index < 5;
        case Screen::race_menu:
            return index < 4 || (index == 4 && cheats::showdown_available(cheats_));
        case Screen::championship_menu:
            return index < 2;
        case Screen::championship_load_slots:
            return index < championship_slot_count && championship_slots()[index].state.has_value();
        case Screen::single_options:
            return index < 5;
        case Screen::car_selection:
            return car_available();
        case Screen::track_selection:
            return track_available();
        case Screen::extras_unlocks:
        case Screen::extras_camera:
        case Screen::extras_effects:
        case Screen::extras_racing: {
            const auto category = screen_ == Screen::extras_unlocks   ? cheats::Category::unlocks
                                  : screen_ == Screen::extras_camera  ? cheats::Category::camera
                                  : screen_ == Screen::extras_effects ? cheats::Category::effects
                                                                      : cheats::Category::racing;
            const auto codes = cheats::category_codes(category);
            return index == codes.size() || (index < codes.size() && (cheats::effect_ready(codes[index]) || cheats::menu_active(cheats_, codes[index])));
        }
        default:
            return true;
    }
}

std::uint8_t Controller::highlight_color() const {
    return highlight_elapsed_ns_ < 266'666'667 ? 0x61 : 0x90;
}

network::LobbySettings Controller::network_settings() const {
    auto settings = network_view_.settings;
    settings.track = static_cast<std::uint8_t>(stored_track_id_);
    settings.weather = static_cast<std::uint8_t>(setup_.weather);
    settings.laps = static_cast<std::uint8_t>(setup_.laps);
    settings.ai = network_ai_;
    settings.car_boost = setup_.car_boost;
    return settings;
}

void Controller::enter_network_waiting() {
    network_requests_.push_back({.kind = NetworkLobbyCommandKind::select_car, .car = static_cast<std::uint8_t>(accepted_car_ids_[0]), .manual = transmissions_manual_[0]});
    if (network_lobby_.hosting()) {
        network_requests_.push_back({.kind = NetworkLobbyCommandKind::set_settings, .settings = network_settings()});
    }
    network_waiting_commit_ = true;
    network_lobby_.show_waiting();
    enter(Screen::network_linkup);
}

void Controller::set_network_view(network::SessionView view) {
    network_view_ = std::move(view);
    if (network_selecting_ && network_view_.phase == network::SessionPhase::failed) {
        network_error(network_view_.error);
        return;
    }
    if (network_connecting_ && network_view_.phase == network::SessionPhase::lobby) {
        network_connecting_ = false;
        enter(network_lobby_.hosting() ? Screen::network_options : Screen::car_selection, network_lobby_.hosting() ? 4 : 0);
    }
    if (!network_waiting_commit_ || network_view_.phase != network::SessionPhase::lobby) {
        return;
    }
    const auto local = std::ranges::find(network_view_.roster.players, network_view_.local_player, &network::LobbyPlayer::player);
    if (local == network_view_.roster.players.end() || local->car != accepted_car_ids_[0] || local->manual != transmissions_manual_[0]) {
        return;
    }
    if (network_lobby_.hosting()) {
        const auto expected = network_settings();
        const auto& actual = network_view_.settings;
        if (actual.track != expected.track || actual.weather != expected.weather || actual.laps != expected.laps || actual.ai != expected.ai
            || actual.car_boost != expected.car_boost) {
            return;
        }
    }
    network_requests_.push_back({.kind = NetworkLobbyCommandKind::ready, .ready = true});
    network_waiting_commit_ = false;
}

void Controller::network_error(std::string error) {
    network_requests_.clear();
    network_selecting_ = network_connecting_ = network_waiting_commit_ = false;
    network_lobby_.set_error(std::move(error));
    if (setup_.mode == GameMode::linkup && screen_ != Screen::scene && screen_ != Screen::scene_loading) {
        enter(Screen::network_linkup);
    }
}

std::optional<NetworkLobbyCommand> Controller::take_network_request() {
    if (network_requests_.empty()) {
        return {};
    }
    auto command = std::move(network_requests_.front());
    network_requests_.pop_front();
    return command;
}

Menu Controller::network_menu(const Menus& menus) const {
    return network_lobby_.menu(language_, network_view_, &menus.network_labels.at(language_));
}

bool Controller::network_hosting() const {
    return network_lobby_.hosting();
}

bool Controller::network_ai_enabled() const {
    return network_ai_;
}

bool Controller::network_text_input() const {
    return screen_ == Screen::network_linkup && network_lobby_.entering_text(network_view_);
}

void Controller::begin_network_race(const network::SessionView& session, const network::RaceView& race) {
    const auto player = std::ranges::find(session.roster.players, session.local_player, &network::LobbyPlayer::player);
    if (player == session.roster.players.end() || race.generation != session.generation || race.grid.empty()) {
        throw std::logic_error("Linkup start requires the committed local roster and race generation");
    }
    setup_.mode = GameMode::linkup;
    setup_.players = 1;
    setup_.qualifying = false;
    setup_.weather = session.settings.weather;
    setup_.laps = session.settings.laps;
    setup_.difficulty = 1;
    setup_.car_boost = session.settings.car_boost;
    selection_player_ = 0;
    accepted_names_[0] = editing_name_ = player->name;
    accepted_car_ids_[0] = car_ids_[0] = player->car;
    transmissions_manual_[0] = player->manual;
    stored_track_id_ = track_id_ = session.settings.track;
    enter(Screen::scene_loading);
}

void Controller::return_network_lobby() {
    network_requests_.clear();
    network_lobby_.reset();
    network_selecting_ = network_connecting_ = network_waiting_commit_ = false;
    // 44a370 -> 4638bb -> 46381a restores frontend Main (state2).
    enter(Screen::main_menu);
}
} // namespace toca::frontend
