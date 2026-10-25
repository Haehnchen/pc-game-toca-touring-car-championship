#include "frontend/menus.hpp"

#include "assets/store.hpp"
#include "frontend/builtin_data.hpp"
#include "frontend/controller.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <numeric>
#include <span>
#include <stdexcept>

namespace toca::frontend {
namespace {
std::string format_label(std::string_view format, std::initializer_list<std::string_view> values) {
    std::string result;
    auto value = values.begin();
    for (std::size_t index = 0; index < format.size(); ++index) {
        if (format[index] != '%') {
            result.push_back(format[index]);
            continue;
        }
        if (++index == format.size() || (format[index] != 's' && format[index] != 'd') || value == values.end()) {
            throw std::runtime_error("Invalid compiled menu label format");
        }
        result += *value++;
    }
    if (value != values.end()) {
        throw std::runtime_error("Missing compiled menu label substitution");
    }
    return result;
}

std::string aspect_suffix(std::uint32_t width, std::uint32_t height) {
    const auto divisor = std::gcd(width, height);
    const auto horizontal = width / divisor, vertical = height / divisor;
    const auto aspect = horizontal == 8 && vertical == 5 ? "16:10" : std::to_string(horizontal) + ":" + std::to_string(vertical);
    return "(" + aspect + ")";
}

char menu_character(unsigned char character) {
    if (character >= 'a' && character <= 'z') {
        character = character - 'a' + 'A';
    }
    // Intersection of the three canonical frontend menu/help font atlases.
    return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || std::string_view(" /-:',.").find(char(character)) != std::string_view::npos
               ? char(character)
               : ' ';
}

std::string device_label(std::string_view name) {
    std::string result;
    for (const auto character : name) {
        const auto display = menu_character(static_cast<unsigned char>(character));
        if (display != ' ' || (!result.empty() && result.back() != ' ')) {
            result.push_back(display);
        }
    }
    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result.empty() ? "GAMEPAD" : result;
}

std::string binding_name(const PhysicalBinding& binding) {
    if (binding.kind == BindingKind::key) {
        const auto* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(binding.code));
        if (name && *name) {
            std::string result;
            for (const unsigned char character : std::string_view(name)) {
                const auto display = menu_character(character);
                if (display == ' ' && character != ' ') {
                    return "KEY " + std::to_string(binding.code);
                }
                result.push_back(display);
            }
            return result;
        }
        return "KEY " + std::to_string(binding.code);
    }
    if (binding.kind == BindingKind::axis || binding.kind == BindingKind::joystick_axis) {
        return "AXIS " + std::to_string(binding.code + 1) + (binding.direction < 0 ? " NEG" : " POS");
    }
    return "BUTTON " + std::to_string(binding.code + 1);
}
} // namespace

std::string championship_driver_label(std::string_view name) {
    std::string result(name);
    for (auto& character : result) {
        if (character >= 'a' && character <= 'z') {
            character = char(character - 'a' + 'A');
        }
    }
    return result;
}

Menus load_menus(const AssetStore& store) {
    constexpr std::array<builtin::TextId, 4> labels{builtin::TextId::start_race, builtin::TextId::game_options, builtin::TextId::high_scores, builtin::TextId::quit};
    constexpr std::array<builtin::TextId, 4> help{
        builtin::TextId::select_and_start_race, builtin::TextId::sound_graphics_and_controller_setup, builtin::TextId::view_the_high_scores, builtin::TextId::end_the_game};
    Menus result;
    const auto config = store.open_archive("data/config.bfp");
    result.players_format = builtin::players_format;
    result.laps_format = builtin::laps_format;
    result.qualify_format = builtin::qualifying_format;
    result.weather_format = builtin::weather_format;
    result.sound_volume_format = builtin::sound_volume_format;
    result.sound_switch_format = builtin::sound_switch_format;
    result.backdrop_inset = builtin::menu_backdrop;
    for (std::uint32_t language = 0; language < 5; ++language) {
        const auto local = [&](builtin::TextId text) {
            return builtin::translated(text, language);
        };
        auto& network = result.network_labels[language];
        network.title = local(builtin::TextId::linkup_title);
        network.host = local(builtin::TextId::host_a_new_game);
        network.join = local(builtin::TextId::join_game);
        network.host_help = local(builtin::TextId::host_new_game_help);
        network.join_help = local(builtin::TextId::join_game_in_progress);
        network.back = local(builtin::TextId::back);
        network.continue_label = local(builtin::TextId::continue_label);
        network.waiting_room = local(builtin::TextId::waiting_room);
        network.start = local(builtin::TextId::start_race);
        network.track = local(builtin::TextId::track);
        network.ok = local(builtin::TextId::ok);
        network.session_name = local(builtin::TextId::enter_session_name);
        network.player_name = local(builtin::TextId::enter_player_name);
        network.car_boost = local(builtin::TextId::car_boost);
        network.computer_cars = local(builtin::TextId::computer_cars);
        // 466956/46731c pass these canonical dialogs to 466b05.
        network.session_lost = local(builtin::TextId::linkup_session_lost);
        network.race_started_without_you = local(builtin::TextId::the_race_has_started_without_you);
        network.host_icon = builtin::host_icon;
        network.join_icon = builtin::join_icon;
        for (std::uint32_t index = 0; index < network.weather_values.size(); ++index) {
            network.weather_values[index] = local(builtin::weather_texts[index]);
        }
        for (std::uint32_t index = 0; index < network.track_names.size(); ++index) {
            network.track_names[index] = builtin::tracks()[index].name;
        }
        auto& network_options = result.network_options[language];
        network_options.title = network.title;
        // 469bb7: the five original Linkup options, before garage and track.
        constexpr std::array<builtin::TextId, 5> network_help{builtin::TextId::select_the_number_of_laps_to_race, builtin::TextId::choose_the_weather_for_the_race,
            builtin::TextId::speed_up_slow_player_cars, builtin::TextId::choose_to_compete_with_computer_cars, builtin::TextId::progress_to_the_next_options_screen};
        for (std::uint32_t index = 0; index < network_help.size(); ++index) {
            network_options.items.push_back({index == 4 ? network.continue_label : std::string{}, local(network_help[index]), builtin::network_options_icons[index], index < 4});
        }
        auto& options = result.options[language];
        options.players = local(builtin::TextId::players);
        options.lap = local(builtin::TextId::lap);
        options.race = local(builtin::TextId::race);
        options.qualify = local(builtin::TextId::qualify);
        options.yes = local(builtin::TextId::yes);
        options.no = local(builtin::TextId::no);
        options.weather = local(builtin::TextId::weather);
        options.load_best_lap = local(builtin::TextId::load_best_lap);
        // 469d0c authors the Game Options hub; 475361 authors its separate
        // five-row Sound Setup, including the original stereo/mono switch.
        auto& settings = result.settings[language];
        settings.title = local(builtin::TextId::options_title);
        constexpr std::array<builtin::TextId, 4> settings_labels{
            builtin::TextId::sound_setup_item, builtin::TextId::graphics_setup, builtin::TextId::controller_setup, builtin::TextId::back};
        constexpr std::array<builtin::TextId, 4> settings_help{
            builtin::TextId::set_up_the_sound, builtin::TextId::set_up_the_graphics, builtin::TextId::set_up_the_controller, builtin::TextId::go_back_to_previous_screen};
        for (std::uint32_t index = 0; index < settings_labels.size(); ++index) {
            settings.items.push_back({local(settings_labels[index]), local(settings_help[index]), builtin::settings_icons[index]});
        }
        auto& sound = result.sound[language];
        sound.title = local(builtin::TextId::sound_setup);
        constexpr std::array<builtin::TextId, 5> sound_help{builtin::TextId::adjust_the_sound_effects_volume, builtin::TextId::adjust_the_music_volume,
            builtin::TextId::toggle_in_game_music, builtin::TextId::select_stereo_or_mono_sound, builtin::TextId::go_back_to_previous_screen};
        for (std::uint32_t index = 0; index < sound_help.size(); ++index) {
            sound.items.push_back({index == 4 ? local(builtin::TextId::back) : std::string{}, local(sound_help[index]), builtin::sound_icons[index], index < 4});
        }
        result.sound_labels[language] = {local(builtin::TextId::effects), local(builtin::TextId::cd_music), local(builtin::TextId::volume), local(builtin::TextId::game_music),
            local(builtin::TextId::mono_sfx), local(builtin::TextId::stereo_sfx), local(builtin::TextId::on), local(builtin::TextId::off)};
        auto& graphics = result.graphics_labels[language];
        constexpr std::array<builtin::TextId, 4> detail_labels{
            builtin::TextId::low_detail, builtin::TextId::medium_detail, builtin::TextId::high_detail, builtin::TextId::very_high_detail};
        for (std::uint32_t index = 0; index < detail_labels.size(); ++index) {
            graphics.detail_values[index] = local(detail_labels[index]);
        }
        constexpr std::array<std::string_view, 5> mega_labels{"MEGA DETAIL", "DETAIL MEGA", "MEGA-DETAIL", "DETTAGLIO MEGA", "DETALLE MEGA"};
        constexpr std::array<std::string_view, 5> mega_help{"USE UPGRADED CAR MODELS AND TEXTURES", "MODELES ET TEXTURES DE VOITURES AMELIORES",
            "VERBESSERTE AUTOMODELLE UND TEXTUREN", "MODELLI E TEXTURE AUTO MIGLIORATI", "MODELOS Y TEXTURAS DE COCHES MEJORADOS"};
        graphics.detail_values[graphics_mega_detail] = mega_labels[language];
        graphics.mega_help = mega_help[language];
        graphics.detail_help = local(builtin::TextId::adjust_the_graphical_detail);
        // 46a337 combines the localized split label and orientation.
        graphics.split_horizontal = format_label(builtin::qualifying_format, {local(builtin::TextId::split), local(builtin::TextId::horizontal)});
        graphics.split_vertical = format_label(builtin::qualifying_format, {local(builtin::TextId::split), local(builtin::TextId::vertical)});
        graphics.split_help = local(builtin::TextId::change_the_split_screen_type_in_2p_mode);
        auto& input = result.controller_labels[language];
        input.player = local(builtin::TextId::player);
        input.controls = local(builtin::TextId::controls);
        input.keyboard = local(builtin::TextId::keyboard);
        input.configure_keyboard = local(builtin::TextId::configure_keyboard);
        input.configure_gamepad = local(builtin::TextId::configure_joystick);
        input.sensitivity = local(builtin::TextId::sensitivity);
        input.select_help = local(builtin::TextId::select_your_controller);
        input.configure_help = local(builtin::TextId::configure_your_controller);
        input.sensitivity_help = local(builtin::TextId::set_the_sensitivity_for_this_controller);
        input.ok = local(builtin::TextId::ok);
        input.choose_key = local(builtin::TextId::choose_key_for);
        input.choose_button = local(builtin::TextId::choose_button_or_key_for);
        auto& players = result.controller_players[language];
        players.title = local(builtin::TextId::controller_setup);
        for (std::uint32_t index = 0; index < 2; ++index) {
            players.items.push_back({format_label(builtin::player_controls_format, {input.player, std::to_string(index + 1), input.controls}), input.configure_help,
                builtin::controller_players_icons[index]});
        }
        players.items.push_back({local(builtin::TextId::back), local(builtin::TextId::go_back_to_previous_screen), builtin::controller_players_icons[2]});
        auto& controller = result.controller[language];
        // 4730a0 / 4b93c0: one configuration row follows the selected device.
        controller.items.push_back({{}, input.select_help, 51, true});
        controller.items.push_back({input.configure_keyboard, input.configure_help, 57});
        controller.items.push_back({{}, input.sensitivity_help, builtin::controller_sensitivity_icon, true});
        controller.items.push_back({{}, {}, builtin::controller_calibration_icon});
        controller.items.push_back({local(builtin::TextId::back), local(builtin::TextId::go_back_to_previous_screen), builtin::controller_back_icon});
        constexpr std::array<builtin::TextId, 3> sensitivity_labels{builtin::TextId::low, builtin::TextId::medium, builtin::TextId::high};
        constexpr std::array<builtin::TextId, 9> action_labels{builtin::TextId::accelerate, builtin::TextId::brake, builtin::TextId::turn_left, builtin::TextId::turn_right,
            builtin::TextId::gear_up, builtin::TextId::gear_down, builtin::TextId::rear_camera, builtin::TextId::change_camera, builtin::TextId::play_horn};
        for (std::size_t index = 0; index < sensitivity_labels.size(); ++index) {
            input.sensitivity_values[index] = local(sensitivity_labels[index]);
        }
        for (std::size_t index = 0; index < action_labels.size(); ++index) {
            input.actions[index] = local(action_labels[index]);
        }
        for (std::uint32_t index = 0; index < 6; ++index) {
            options.weather_values[index] = local(builtin::weather_texts[index]);
        }
        result.default_player_names[language] = local(builtin::TextId::player) + " 1";
        auto& race = result.race[language];
        race.title = local(builtin::TextId::race_menu);
        result.showdown[language] = {local(builtin::TextId::toca_showdown), local(builtin::TextId::the_final_showdown), builtin::race_icons[2]};
        constexpr std::array<builtin::TextId, 4> race_labels{
            builtin::TextId::single_race, builtin::TextId::championship, builtin::TextId::time_trial, builtin::TextId::linkup_game};
        constexpr std::array<builtin::TextId, 4> race_help{builtin::TextId::start_single_race, builtin::TextId::start_autotrader_rac_championship_race,
            builtin::TextId::start_practice_race, builtin::TextId::start_a_multiplayer_linkup_game};
        constexpr std::array<std::uint32_t, 4> race_slots{0, 1, 3, 4};
        for (std::size_t index = 0; index < race_labels.size(); ++index) {
            race.items.push_back({local(race_labels[index]), local(race_help[index]), builtin::race_icons[race_slots[index]]});
        }
        auto& single = result.single[language];
        single.title = local(builtin::TextId::single_race);
        constexpr std::array<builtin::TextId, 5> single_help{builtin::TextId::select_the_number_of_players, builtin::TextId::select_the_number_of_laps_to_race,
            builtin::TextId::decide_if_you_want_to_do_a_qualifying_lap, builtin::TextId::choose_the_weather_for_the_race, builtin::TextId::progress_to_the_next_options_screen};
        for (std::uint32_t index = 0; index < 5; ++index) {
            single.items.push_back({index == 4 ? local(builtin::TextId::continue_label) : std::string{}, local(single_help[index]), builtin::single_race_icons[index]});
        }
        // 4698f7 / 4660c7: weather, LOAD BEST LAP : YES/NO, Continue.
        auto& trial = result.time_trial[language];
        trial.title = local(builtin::TextId::time_trial);
        constexpr std::array<builtin::TextId, 3> trial_help{
            builtin::TextId::choose_the_weather_for_the_race, builtin::TextId::load_best_lap_help, builtin::TextId::progress_to_the_next_options_screen};
        for (std::uint32_t index = 0; index < trial_help.size(); ++index) {
            trial.items.push_back({index == 2 ? local(builtin::TextId::continue_label) : std::string{}, local(trial_help[index]), builtin::time_trial_icons[index], index < 2});
        }
        // 465e59: New/Load. 469a28: championship length and Continue only.
        auto& championship = result.championship[language];
        championship.title = local(builtin::TextId::championship);
        championship.items.push_back({local(builtin::TextId::new_game), local(builtin::TextId::start_a_new_championship), builtin::championship_icons[0]});
        championship.items.push_back({local(builtin::TextId::load_game), local(builtin::TextId::load_a_saved_championship_position), builtin::championship_icons[1]});
        auto& championship_options = result.championship_options[language];
        championship_options.title = championship.title;
        championship_options.items.push_back({{}, local(builtin::TextId::select_the_length_of_championship_to_race), builtin::championship_options_icons[0], true});
        championship_options.items.push_back(
            {local(builtin::TextId::continue_label), local(builtin::TextId::progress_to_the_next_options_screen), builtin::championship_options_icons[1]});
        auto& championship_labels = result.championship_labels[language];
        championship_labels.short_length = local(builtin::TextId::short_championship);
        championship_labels.full_length = local(builtin::TextId::full_championship);
        championship_labels.player = local(builtin::TextId::player);
        championship_labels.points = local(builtin::TextId::points);
        championship_labels.round = local(builtin::TextId::round);
        championship_labels.continue_label = local(builtin::TextId::continue_label);
        championship_labels.back = local(builtin::TextId::back);
        championship_labels.no_saved_games = local(builtin::TextId::no_saved_games);
        championship_labels.short_label = local(builtin::TextId::short_label);
        championship_labels.full_label = local(builtin::TextId::full);
        for (std::uint32_t slot = 0; slot < 5; ++slot) {
            championship_labels.load_slot_help[slot] = local(builtin::load_slot_help_texts[slot]);
        }
        constexpr std::array<std::string_view, 5> file_errors{
            "CHAMPIONSHIP FILE ERROR", "ERREUR FICHIER CHAMPIONNAT", "MEISTERSCHAFT DATEIFEHLER", "ERRORE FILE CAMPIONATO", "ERROR ARCHIVO CAMPEONATO"};
        championship_labels.file_error = file_errors[language];
        // 42bee0 splits the original GSTRINGS at runs of CR/LF.
        const auto strings_member = config.member(2 + language);
        const auto strings = strings_member.bytes();
        std::size_t begin = 0;
        for (std::uint32_t line = 0; line <= 105; ++line) {
            auto end = begin;
            while (end < strings.size() && strings[end] != '\r' && strings[end] != '\n') {
                ++end;
            }
            if (end == begin || end == strings.size()) {
                throw std::runtime_error("Championship labels are missing from original GSTRINGS");
            }
            const std::string value(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
            if (line == 10) {
                championship_labels.eliminated = value;
            }
            if (line == 41) {
                championship_labels.race = value;
            }
            if (line == 50) {
                championship_labels.qualifying = value;
            }
            if (line == 58) {
                championship_labels.table_title = value;
            }
            if (line == 90) {
                championship_labels.complete = value;
            }
            if (line == 43) {
                championship_labels.select_slot = value;
            }
            if (line == 45) {
                championship_labels.saved = value;
            }
            if (line == 46) {
                championship_labels.save_prompt = value;
            }
            if (line == 68) {
                championship_labels.slot_format = value;
            }
            if (line == 92) {
                championship_labels.cancel = value;
            }
            if (line == 93) {
                championship_labels.empty = value;
            }
            if (line == 96) {
                championship_labels.cancelled = value;
            }
            if (line == 105) {
                championship_labels.overwrite = value;
            }
            begin = end;
            while (begin < strings.size() && (strings[begin] == '\r' || strings[begin] == '\n')) {
                ++begin;
            }
        }
        auto& difficulty = result.difficulty[language];
        difficulty.title = local(builtin::TextId::difficulty);
        constexpr std::array<builtin::TextId, 3> difficulty_labels{builtin::TextId::easy, builtin::TextId::medium, builtin::TextId::hard};
        for (std::uint32_t index = 0; index < 3; ++index) {
            difficulty.items.push_back({local(difficulty_labels[index]), local(builtin::TextId::choose_the_difficulty_level), builtin::difficulty_icons[index]});
        }
        auto& language_menu = result.language[language];
        language_menu.title = builtin::translated(builtin::TextId::select_language, language);
        for (std::uint32_t index = 0; index < 3; ++index) {
            language_menu.items.push_back(
                {std::string(builtin::language_names[index]), builtin::translated(builtin::TextId::select_your_language, language), builtin::language_icons[index]});
        }
        auto& main = result.main[language];
        main.title = builtin::translated(builtin::TextId::main_menu_title, language);
        for (std::size_t index = 0; index < labels.size(); ++index) {
            main.items.push_back({local(labels[index]), local(help[index]), builtin::main_icons[index]});
        }
    }
    return result;
}

Menu menu_for(const Menus& menus, const Controller& controller) {
    const auto language = controller.language();
    if (language >= menus.main.size()) {
        throw std::logic_error("Invalid frontend language");
    }
    switch (controller.screen()) {
        case Screen::network_linkup:
            return controller.network_menu(menus);
        case Screen::extras:
        case Screen::extras_unlocks:
        case Screen::extras_camera:
        case Screen::extras_effects:
        case Screen::extras_racing: {
            struct Labels {
                std::string_view back, yes, no, failed, back_help;
                std::array<std::string_view, 4> categories, category_help;
            };

            constexpr std::array<Labels, 3> labels{{
                {"BACK", "YES", "NO", "COULD NOT SAVE EXTRAS, BACK RETRIES", "ENTER OR ESC: BACK", {"UNLOCKS", "CAMERA", "GRAPHICS AND EFFECTS", "RACING"},
                    {"CIRCUITS, BONUS CARS AND TOCA SHOWDOWN", "DRIVING VIEWS AND COCKPIT", "SKIES, RAIN AND PICTURE EFFECTS", "SPEED, OPPONENTS AND COLLISIONS"}},
                {"RETOUR", "OUI", "NON", "ECHEC SAUVEGARDE, RETOUR REESSAIE", "ENTREE OU ECHAP: RETOUR", {"DEBLOCAGES", "CAMERA", "GRAPHISMES ET EFFETS", "COURSE"},
                    {"CIRCUITS, VOITURES BONUS ET TOCA SHOWDOWN", "VUES DE CONDUITE ET HABITACLE", "CIEL, PLUIE ET EFFETS VISUELS", "VITESSE, ADVERSAIRES ET COLLISIONS"}},
                {"ZURUECK", "JA", "NEIN", "SPEICHERN FEHLGESCHLAGEN, ZURUECK WIEDERHOLT", "EINGABE ODER ESC: ZURUECK",
                    {"FREISCHALTUNGEN", "KAMERA", "GRAFIK UND EFFEKTE", "RENNEN"},
                    {"STRECKEN, BONUSWAGEN UND TOCA SHOWDOWN", "FAHRANSICHTEN UND COCKPIT", "HIMMEL, REGEN UND BILDEFFEKTE", "TEMPO, GEGNER UND KOLLISIONEN"}},
            }};

            struct Effect {
                cheats::Code code;
                std::array<std::string_view, 3> label, help;
            };

            constexpr std::array<Effect, 19> effects{{
                {cheats::Code::all_tracks, {"ALL CIRCUITS", "TOUS LES CIRCUITS", "ALLE STRECKEN"},
                    {"OPEN EVERY CIRCUIT, INCLUDING LAVALAND", "OUVRIR TOUS LES CIRCUITS, DONT LAVALAND", "ALLE STRECKEN EINSCHLIESSLICH LAVALAND OEFFNEN"}},
                {cheats::Code::bonus_cars, {"BONUS CARS", "VOITURES BONUS", "BONUSWAGEN"},
                    {"BOTH BONUS CARS, NO KEEPS EARNED CARS", "DEUX VOITURES BONUS, NON GARDE LES ACQUIS", "BEIDE BONUSWAGEN, NEIN BEHAELT ERSPIELTE WAGEN"}},
                {cheats::Code::patscreem, {"TOCA SHOWDOWN", "TOCA SHOWDOWN", "TOCA SHOWDOWN"},
                    {"OPEN TOCA SHOWDOWN, NO KEEPS EARNED ACCESS", "OUVRIR TOCA SHOWDOWN, NON GARDE LES ACQUIS", "TOCA SHOWDOWN OEFFNEN, ERSPIELTER ZUGANG BLEIBT"}},
                {cheats::Code::micro, {"OVERHEAD CAMERA", "CAMERA EN HAUTEUR", "ERHOEHTE KAMERA"},
                    {"RAISED, ANGLED VIEW OF YOUR CAR", "VUE PLONGEANTE EN BIAIS SUR VOTRE VOITURE", "ERHOEHTER, SCHRAEGER BLICK AUF DEIN AUTO"}},
                {cheats::Code::chun, {"COCKPIT VIEW", "VUE HABITACLE", "COCKPITANSICHT"},
                    {"CHANGE THE COCKPIT VISIBLE IN THE INTERIOR VIEW", "CHANGER LE COCKPIT VISIBLE EN VUE INTERIEURE", "DAS SICHTBARE COCKPIT IN DER INNENANSICHT AENDERN"}},
                {cheats::Code::handy, {"LARGE DRIVER HANDS", "GRANDES MAINS", "GROSSE FAHRERHAENDE"},
                    {"ENLARGE THE DRIVER HANDS IN THE COCKPIT", "AGRANDIR LES MAINS DU PILOTE DANS LE COCKPIT", "DIE HAENDE DES FAHRERS IM COCKPIT VERGROESSERN"}},
                {cheats::Code::follow, {"FOLLOW CAMERA", "CAMERA POURSUITE", "VERFOLGERKAMERA"},
                    {"ADD TRACKSIDE VIEWS, F5 TOGGLES THEM", "AJOUTER LES VUES DE PISTE, F5 LES ACTIVE", "STRECKENKAMERAS ERGAENZEN, F5 SCHALTET UM"}},
                {cheats::Code::copter, {"HELICOPTER VIEW", "VUE HELICOPTERE", "HELIKOPTERANSICHT"},
                    {"CAMERA CONTROL SELECTS HELICOPTER IN DRY WEATHER", "COMMANDE CAMERA : HELICOPTERE PAR TEMPS SEC", "KAMERAWECHSEL: HELIKOPTER BEI TROCKENHEIT"}},
                {cheats::Code::stars, {"FANTASY SKY", "CIEL FANTAISISTE", "FANTASIEHIMMEL"},
                    {"REPLACE THE NORMAL SKY WITH A FANTASY HORIZON", "REMPLACER LE CIEL PAR UN HORIZON FANTAISISTE", "DEN HIMMEL DURCH EINE FANTASIEKULISSE ERSETZEN"}},
                {cheats::Code::toon, {"CARTOON SKY", "CIEL DESSIN ANIME", "CARTOON-HIMMEL"},
                    {"USE A CARTOON HORIZON, FANTASY SKY TAKES PRIORITY", "CIEL DESSIN ANIME, LE CIEL FANTAISISTE PRIME", "CARTOON-KULISSE, FANTASIEHIMMEL HAT VORRANG"}},
                {cheats::Code::upside, {"UPSIDE-DOWN VIEW", "VUE A L ENVERS", "KOPFSTEHENDE ANSICHT"},
                    {"FLIP THE DRIVING VIEW AND REVERSE STEERING", "RENVERSER LA VUE DE CONDUITE ET LA DIRECTION", "RENNBILD AUF DEN KOPF STELLEN, LENKUNG UMKEHREN"}},
                {cheats::Code::low_gravity, {"LIGHT PARTICLES", "PARTICULES LEGERES", "LEICHTE PARTIKEL"},
                    {"MAKE PARTICLES FALL MORE SLOWLY", "RALENTIR LA CHUTE DES PARTICULES", "PARTIKEL LANGSAMER FALLEN LASSEN"}},
                {cheats::Code::rain_up, {"RAIN FALLS UP", "PLUIE VERS LE HAUT", "REGEN NACH OBEN"},
                    {"MAKE RAIN FALL UPWARDS IN WET WEATHER", "FAIRE MONTER LA PLUIE PAR TEMPS HUMIDE", "REGEN BEI NASSWETTER NACH OBEN FALLEN LASSEN"}},
                {cheats::Code::cat_dog, {"RAINING CATS AND DOGS", "PLUIE DE CHATS ET CHIENS", "HUNDE/KATZENREGEN"},
                    {"REPLACE RAIN WITH CATS AND DOGS", "REMPLACER LA PLUIE PAR DES CHATS ET CHIENS", "REGENTROPFEN DURCH HUNDE UND KATZEN ERSETZEN"}},
                {cheats::Code::boost, {"DOUBLE GAME SPEED", "VITESSE DU JEU DOUBLEE", "DOPPELTES SPIELTEMPO"},
                    {"RUN THE DRIVING SIMULATION AT DOUBLE SPEED", "DOUBLER LA VITESSE DE CONDUITE DU JEU", "DIE FAHRSIMULATION MIT DOPPELTEM TEMPO SPIELEN"}},
                {cheats::Code::mayhem, {"SLIPPERY AI CARS", "VOITURES IA GLISSANTES", "RUTSCHIGE KI-AUTOS"},
                    {"LESS TIRE GRIP FOR COMPUTER-CONTROLLED CARS", "MOINS D ADHERENCE POUR LES VOITURES IA", "WENIGER HAFTUNG FUER KI-GESTEUERTE AUTOS"}},
                {cheats::Code::no_hits, {"NO CAR COLLISIONS", "SANS COLLISIONS AUTOS", "KEINE AUTOKOLLISIONEN"},
                    {"PASS THROUGH OTHER CARS, BARRIERS STILL COLLIDE", "TRAVERSER LES AUTOS, LES BARRIERES RESTENT SOLIDES", "DURCH ANDERE AUTOS FAHREN, BARRIEREN BLEIBEN FEST"}},
                {cheats::Code::codies, {"PLAYER NAME RECORDS", "NOMS DES JOUEURS", "SPIELERNAMEN"},
                    {"USE ENTERED HUMAN NAMES, ALREADY ACTIVE HERE", "NOMS DES JOUEURS, DEJA UTILISES ICI", "EINGEGEBENE SPIELERNAMEN, HIER BEREITS AKTIV"}},
                {cheats::Code::impossible, {"HARDER OPPONENTS", "ADVERSAIRES PLUS FORTS", "STAERKERE GEGNER"},
                    {"FASTER OPPONENTS WITH MORE GRIP, MAYHEM WINS", "ADVERSAIRES PLUS RAPIDES, GLISSANTS PRIORITAIRE", "SCHNELLERE KI-AUTOS MIT MEHR HAFTUNG"}},
            }};
            const auto& local = labels.at(language);
            Menu result;
            result.compact = controller.screen() != Screen::extras;
            if (controller.screen() == Screen::extras) {
                result.title = "EXTRAS";
                constexpr std::array icons{unlocks_icon, camera_icon, std::uint32_t(19), std::uint32_t(3)};
                for (std::size_t index = 0; index < local.categories.size(); ++index) {
                    result.items.push_back({std::string(local.categories[index]), std::string(local.category_help[index]), icons[index]});
                }
            } else {
                const auto category = controller.screen() == Screen::extras_unlocks   ? cheats::Category::unlocks
                                      : controller.screen() == Screen::extras_camera  ? cheats::Category::camera
                                      : controller.screen() == Screen::extras_effects ? cheats::Category::effects
                                                                                      : cheats::Category::racing;
                result.title = local.categories[static_cast<std::size_t>(category)];
                for (const auto code : cheats::category_codes(category)) {
                    const auto effect = std::ranges::find(effects, code, &Effect::code);
                    if (effect == effects.end()) {
                        throw std::logic_error("Missing Extras effect label");
                    }
                    const bool active = cheats::menu_active(controller.cheats_state(), code);
                    result.items.push_back(
                        {std::string(effect->label.at(language)) + " : " + std::string(active ? local.yes : local.no), std::string(effect->help.at(language)), 0, true});
                }
            }
            result.items.push_back({std::string(local.back), std::string(local.back_help), result.compact ? 0u : 16u});
            if (!controller.cheats_error().empty()) {
                result.notice = local.failed;
            }
            return result;
        }
        case Screen::settings: {
            auto result = menus.settings[language];
            if (!controller.settings_error().empty()) {
                constexpr std::array<std::string_view, 3> failed{
                    "COULD NOT SAVE SETTINGS, SOUND SETUP RETRIES", "ECHEC SAUVEGARDE, OPTIONS SON POUR REESSAYER", "SPEICHERN FEHLGESCHLAGEN, SOUNDOPTIONEN WIEDERHOLEN"};
                result.notice = failed.at(language);
            }
            return result;
        }
        case Screen::sound_options: {
            auto result = menus.sound[language];
            const auto& local = menus.sound_labels[language];
            const auto& audio = controller.settings_values().audio;
            if (result.items.size() != 5) {
                throw std::logic_error("Invalid Sound Setup menu");
            }
            const auto volume = [&](std::string_view label, std::uint32_t value) {
                return value == 0 ? format_label(menus.sound_switch_format, {label, local.off})
                                  : format_label(menus.sound_volume_format, {label, local.volume, std::to_string(value)});
            };
            result.items[0].label = volume(local.effects, audio.effects_volume);
            result.items[1].label = volume(local.music, audio.music_volume);
            result.items[0].decrease_available = audio.effects_volume > 0;
            result.items[0].increase_available = audio.effects_volume < 100;
            result.items[1].decrease_available = audio.music_volume > 0;
            result.items[1].increase_available = audio.music_volume < 100;
            result.items[2].label = format_label(menus.sound_switch_format, {local.game_music, audio.game_music ? local.on : local.off});
            result.items[3].label = audio.stereo_effects ? local.stereo : local.mono;
            constexpr std::array<std::string_view, 3> saved{"SETTINGS SAVED", "OPTIONS SAUVEGARDEES", "OPTIONEN GESPEICHERT"};
            constexpr std::array<std::string_view, 3> failed{"COULD NOT SAVE SETTINGS", "ECHEC SAUVEGARDE OPTIONS", "SPEICHERN FEHLGESCHLAGEN"};
            if (!controller.settings_error().empty()) {
                result.notice = failed.at(language);
            } else if (controller.settings_saved()) {
                result.notice = saved.at(language);
            }
            return result;
        }
        case Screen::controller_keyboard_bindings:
        case Screen::controller_gamepad_bindings: {
            const auto& local = menus.controller_labels[language];
            const bool keyboard = controller.screen() == Screen::controller_keyboard_bindings;
            constexpr std::array<std::string_view, 3> cancel{"ESC: CANCEL", "ECHAP: ANNULER", "ESC: ABBRECHEN"};
            constexpr std::array<std::string_view, 3> invalid{
                "KEY OR INPUT ALREADY USED / RESERVED", "TOUCHE OU ENTREE DEJA UTILISEE / RESERVEE", "TASTE ODER EINGABE BELEGT / RESERVIERT"};
            constexpr std::array<std::string_view, 3> wheel_prompt{
                "MOVE AXIS FULLY OR PRESS BUTTON", "BOUGER UN AXE A FOND OU APPUYER SUR UN BOUTON", "ACHSE GANZ BEWEGEN ODER TASTE DRUECKEN"};
            Menu result;
            result.compact = true;
            for (std::size_t index = 0; index < local.actions.size(); ++index) {
                result.items.push_back({local.actions[index] + " : " + binding_name(controller.binding_capture_values()[index]), std::string(cancel.at(language))});
            }
            result.notice = controller.binding_capture_error()                                 ? invalid.at(language)
                            : keyboard                                                         ? local.choose_key
                            : controller.controller_input().device_id.starts_with("joystick:") ? wheel_prompt.at(language)
                                                                                               : local.choose_button;
            if (controller.binding_capture_feedback()) {
                result.status = local.ok;
            }
            return result;
        }
        case Screen::controller_auto_configure: {
            const auto& labels = menus.controller_labels[language];
            Menu result;
            result.title = labels.player + " 1 " + labels.controls;
            result.items = {{labels.configure_keyboard, labels.configure_help, 57}, {labels.configure_gamepad, labels.configure_help, 20}, menus.controller[language].items.back()};
            constexpr std::array<std::string_view, 3> absent{"CONNECT A GAMEPAD TO CONFIGURE IT", "CONNECTER UNE MANETTE POUR LA CONFIGURER", "GAMEPAD ZUM EINRICHTEN VERBINDEN"};
            constexpr std::array<std::string_view, 3> failed{"COULD NOT SAVE, BACK RETRIES", "ECHEC SAUVEGARDE, RETOUR REESSAIE", "SPEICHERN FEHLGESCHLAGEN, ZURUECK WIEDERHOLT"};
            if (!controller.settings_error().empty()) {
                result.notice = failed.at(language);
            } else if (controller.selection() == 1 && !controller.controller_gamepad_available()) {
                result.notice = absent.at(language);
            }
            return result;
        }
        case Screen::controller_players: {
            auto result = menus.controller_players[language];
            constexpr std::array<std::string_view, 3> failed{"COULD NOT SAVE, BACK RETRIES", "ECHEC SAUVEGARDE, RETOUR REESSAIE", "SPEICHERN FEHLGESCHLAGEN, ZURUECK WIEDERHOLT"};
            if (!controller.settings_error().empty()) {
                result.notice = failed.at(language);
            }
            for (std::size_t index = 0; index < 2; ++index) {
                const auto& settings = controller.settings_values();
                const auto& input = index == 0 ? settings.input : settings.input_player2;
                result.items[index].icon = input.device_id == automatic_input_device ? 20 : input.device_id.empty() ? 51 : 21;
            }
            return result;
        }
        case Screen::controller_options:
        case Screen::controller_native_options: {
            const bool native = controller.screen() == Screen::controller_native_options;
            auto result = menus.controller[language];
            const auto& local = menus.controller_labels[language];
            const auto& input = controller.controller_input();
            const bool automatic = input.device_id == automatic_input_device;
            const bool wheel = input.device_id.starts_with("joystick:");
            constexpr std::array<std::string_view, 3> automatic_help{"KEYBOARD AND GAMEPAD TOGETHER", "CLAVIER ET MANETTE ENSEMBLE", "TASTATUR UND GAMEPAD GLEICHZEITIG"};
            constexpr std::array<std::string_view, 3> configure_both{"CONFIGURE CONTROLS", "CONFIGURER COMMANDES", "STEUERUNG EINRICHTEN"};
            constexpr std::array<std::string_view, 3> disconnected{"DISCONNECTED", "DECONNECTE", "GETRENNT"};
            constexpr std::array<std::string_view, 3> select_gamepad{"SELECT A CONNECTED GAMEPAD FIRST", "CHOISIR UNE MANETTE CONNECTEE", "ZUERST VERBUNDENES GAMEPAD WAEHLEN"};
            constexpr std::array<std::string_view, 3> native_title{"GAMEPAD OPTIONS", "OPTIONS MANETTE", "GAMEPAD-OPTIONEN"};
            constexpr std::array<std::string_view, 3> wheel_title{"WHEEL OPTIONS", "OPTIONS VOLANT", "LENKRAD-OPTIONEN"};
            constexpr std::array<std::string_view, 3> wheel_help{
                "AXIS DEADZONE, FORCE FEEDBACK AND DEFAULTS", "ZONE MORTE, RETOUR DE FORCE ET DEFAUTS", "TOTZONE, FORCE FEEDBACK UND STANDARDWERTE"};
            constexpr std::array<std::string_view, 3> force_feedback_help{
                "DIRECTIONAL WHEEL FORCE FEEDBACK ON OR OFF", "ACTIVER LE RETOUR DE FORCE DU VOLANT", "LENKRAD-FORCE-FEEDBACK EIN ODER AUS"};
            constexpr std::array<std::string_view, 3> wheel_configure_help{
                "CENTER WHEEL AND RELEASE PEDALS BEFORE CONFIGURE", "CENTRER LE VOLANT ET RELACHER LES PEDALES", "VORHER LENKRAD MITTIG UND PEDALE LOSLASSEN"};
            constexpr std::array<std::string_view, 3> native_help{
                "DEADZONE, VIBRATION AND DEFAULTS", "ZONE MORTE, VIBRATION ET REINITIALISATION", "TOTZONE, VIBRATION UND STANDARDWERTE"};
            constexpr std::array<std::string_view, 3> deadzone{"DEADZONE", "ZONE MORTE", "TOTZONE"};
            constexpr std::array<std::string_view, 3> deadzone_help{
                "GAMEPAD AXIS DEADZONE IN PERCENT", "ZONE MORTE DES AXES EN POUR CENT", "GAMEPAD-TOTZONE IN PROZENT EINSTELLEN"};
            constexpr std::array<std::string_view, 3> defaults{"RESET DEFAULTS", "VALEURS PAR DEFAUT", "STANDARDWERTE"};
            constexpr std::array<std::string_view, 3> defaults_help{
                "RESTORE KEYBOARD AND GAMEPAD DEFAULTS", "RETABLIR CLAVIER ET MANETTE PAR DEFAUT", "STANDARD FUER TASTATUR UND GAMEPAD"};
            constexpr std::array<std::string_view, 3> vibration_help{"GAMEPAD VIBRATION ON OR OFF", "ACTIVER LA VIBRATION MANETTE", "GAMEPAD-VIBRATION EIN ODER AUS"};
            constexpr std::array<std::string_view, 3> saved{"SETTINGS SAVED", "OPTIONS SAUVEGARDEES", "OPTIONEN GESPEICHERT"};
            constexpr std::array<std::string_view, 3> failed{"COULD NOT SAVE, BACK RETRIES", "ECHEC SAUVEGARDE, RETOUR REESSAIE", "SPEICHERN FEHLGESCHLAGEN, ZURUECK WIEDERHOLT"};
            result.title = local.player + " " + std::to_string(controller.controller_player() + 1) + " " + local.controls;
            if (native) {
                const auto back = result.items.back();
                result.title = local.player + " " + std::to_string(controller.controller_player() + 1) + " - " + std::string((wheel ? wheel_title : native_title).at(language));
                result.items = {{std::string(deadzone.at(language)) + " : " + std::to_string(input.deadzone_percent), std::string(deadzone_help.at(language)), 20, true,
                                    input.deadzone_percent > 0, input.deadzone_percent < 30},
                    {std::string(defaults.at(language)), std::string(defaults_help.at(language)), 29},
                    {std::string(wheel ? "FORCE FEEDBACK : " : "VIBRATION : ") + (input.rumble_enabled ? menus.sound_labels[language].on : menus.sound_labels[language].off),
                        std::string((wheel ? force_feedback_help : vibration_help).at(language)), 20, true},
                    back};
            } else {
                auto name = automatic                 ? "AUTO"
                            : input.device_id.empty() ? local.keyboard + " " + std::to_string(controller.controller_player() + 1)
                                                      : controller.input_device_name();
                if (name.empty()) {
                    name = disconnected.at(language);
                }
                result.items[0].label = device_label(name);
                result.items[0].icon = automatic ? 20 : input.device_id.empty() ? 51 : 21;
                if (automatic) {
                    result.items[0].help = automatic_help.at(language);
                }
                const auto choices = controller.input_device_choices();
                const auto selected = std::ranges::find(choices, input.device_id);
                result.items[0].decrease_available = !input.device_id.empty();
                result.items[0].increase_available = selected == choices.end() || selected + 1 != choices.end();
                result.items[1].label = automatic ? configure_both.at(language) : input.device_id.empty() ? local.configure_keyboard : local.configure_gamepad;
                result.items[1].icon = input.device_id.empty() ? 57 : 20;
                if (wheel) {
                    result.items[1].help = wheel_configure_help.at(language);
                }
                result.items[2].label = local.sensitivity + " : " + local.sensitivity_values.at(input.sensitivity - 1);
                result.items[3].label = (wheel ? wheel_title : native_title).at(language);
                result.items[3].help = (wheel ? wheel_help : native_help).at(language);
            }
            if (!controller.settings_error().empty()) {
                result.notice = failed.at(language);
            } else if (!native && controller.selection() == 1 && !controller.item_available(1)) {
                result.notice = select_gamepad.at(language);
            } else if (!automatic && !input.device_id.empty() && controller.input_device_name().empty()) {
                result.notice = disconnected.at(language);
            } else if (controller.settings_saved()) {
                result.notice = saved.at(language);
            }
            return result;
        }
        case Screen::graphics_options: {
            // The native display service retains modern modes and pacing,
            // behind the original Graphics Setup entry.
            struct Labels {
                std::string_view fullscreen, vsync, fps, apply, yes, no, uncapped, change_help, apply_help, back_help, saved, failed;
            };

            constexpr std::array<Labels, 3> labels{{
                {"FULLSCREEN", "VSYNC", "FPS LIMIT", "APPLY / SAVE", "YES", "NO", "UNCAPPED", "LEFT, RIGHT OR ENTER TO CHANGE", "APPLY SETTINGS AND SAVE PREFERENCES",
                    "DISCARD CHANGES AND RETURN", "SETTINGS SAVED", "COULD NOT APPLY OR SAVE SETTINGS"},
                {"PLEIN ECRAN", "SYNCHRO VERT.", "LIMITE FPS", "APPLIQUER / SAUVER", "OUI", "NON", "SANS LIMITE", "GAUCHE, DROITE OU ENTREE POUR CHANGER",
                    "APPLIQUER ET SAUVER LES OPTIONS", "ANNULER LES CHANGEMENTS ET REVENIR", "OPTIONS SAUVEGARDEES", "ECHEC APPLICATION OU SAUVEGARDE"},
                {"VOLLBILD", "VSYNC", "FPS-LIMIT", "ANWENDEN / SPEICHERN", "JA", "NEIN", "UNBEGRENZT", "LINKS, RECHTS ODER EINGABE ZUM AENDERN", "OPTIONEN ANWENDEN UND SPEICHERN",
                    "AENDERUNGEN VERWERFEN UND ZURUECK", "OPTIONEN GESPEICHERT", "ANWENDEN ODER SPEICHERN FEHLGESCHLAGEN"},
            }};
            const auto& local = labels.at(language);
            const auto& settings = controller.settings_values();
            Menu result;
            result.title = menus.settings[language].items.at(1).label;
            result.compact = false;
            const auto option = [&](std::string_view name, std::string value, std::uint32_t icon) {
                result.items.push_back({std::string(name) + " : " + value, std::string(local.change_help), icon, true});
            };
            const auto enabled = [&](bool value) {
                return std::string(value ? local.yes : local.no);
            };
            result.items.push_back({std::to_string(settings.video.width) + "X" + std::to_string(settings.video.height), std::string(local.change_help), 59, true, true, true,
                aspect_suffix(settings.video.width, settings.video.height)});
            option(local.fullscreen, enabled(settings.video.fullscreen), fullscreen_icon);
            option(local.vsync, enabled(settings.video.vsync), vsync_icon);
            option(local.fps, settings.video.fps_limit == 0 ? std::string(local.uncapped) : std::to_string(settings.video.fps_limit), fps_icon);
            result.items.push_back({std::string(local.apply), std::string(local.apply_help), 29});
            const auto& graphics = menus.graphics_labels[language];
            result.items.push_back(
                {graphics.detail_values.at(settings.graphics.detail), settings.graphics.detail == graphics_mega_detail ? graphics.mega_help : graphics.detail_help, 53, true});
            result.items.push_back({settings.graphics.split_vertical ? graphics.split_vertical : graphics.split_horizontal, graphics.split_help, 6, true});
            result.items.push_back({menus.settings[language].items.at(3).label, std::string(local.back_help), 16});
            if (!controller.settings_error().empty()) {
                result.notice = local.failed;
            } else if (controller.settings_saved()) {
                result.notice = local.saved;
            }
            return result;
        }
        case Screen::language:
            return menus.language[language];
        case Screen::main_menu: {
            auto result = menus.main[language];
            constexpr std::array<std::string_view, 3> help{
                "UNLOCKS, CAMERAS AND RACING EFFECTS", "DEBLOCAGES, CAMERAS ET EFFETS DE COURSE", "FREISCHALTUNGEN, KAMERAS UND RENNEFFEKTE"};
            result.items.insert(result.items.begin() + 3, {"EXTRAS", std::string(help.at(language)), extras_icon});
            return result;
        }
        case Screen::race_menu: {
            auto result = menus.race[language];
            if (cheats::showdown_available(controller.cheats_state())) {
                result.items.push_back(menus.showdown[language]);
            }
            return result;
        }
        case Screen::championship_menu: {
            auto result = menus.championship[language];
            if (!controller.championship_error().empty()) {
                result.notice = menus.championship_labels[language].file_error;
            }
            return result;
        }
        case Screen::championship_load_slots:
        case Screen::championship_save_slots: {
            const bool save = controller.screen() == Screen::championship_save_slots;
            const auto& labels = menus.championship_labels[language];
            const auto& slots = controller.championship_slots();
            Menu result;
            result.title = save ? labels.select_slot : menus.championship[language].items[1].label;
            result.compact = true;
            const bool any = std::ranges::any_of(slots, [](const auto& slot) {
                return slot.state.has_value();
            });
            if (!save && !any) {
                result.items.push_back({labels.no_saved_games, menus.championship[language].items[1].help});
            } else {
                for (std::size_t index = 0; index < slots.size(); ++index) {
                    const auto& slot = slots[index];
                    std::string label = std::to_string(index + 1) + " " + (slot.error.empty() ? labels.empty : labels.file_error);
                    std::string help = labels.load_slot_help[index];
                    if (slot.state) {
                        const auto& state = *slot.state;
                        const auto round = std::to_string(state.meeting * 2 + state.race + 1);
                        const auto points = std::to_string(state.points[state.local_actor]);
                        label = save ? format_label(labels.slot_format, {std::to_string(index + 1), state.driver, round, points}) : std::to_string(index + 1) + " " + state.driver;
                        help = (state.full_length ? labels.full_label : labels.short_label) + " - " + labels.round + " " + round + " - " + labels.points + " " + points;
                    }
                    result.items.push_back({std::move(label), std::move(help)});
                }
            }
            if (save) {
                result.items.push_back({labels.cancel, labels.cancel});
            }
            if (!controller.championship_error().empty() || std::ranges::any_of(slots, [](const auto& slot) {
                    return !slot.error.empty();
                })) {
                result.notice = labels.file_error;
            }
            return result;
        }
        case Screen::championship_save_prompt:
        case Screen::championship_save_overwrite:
        case Screen::championship_save_feedback: {
            const auto& labels = menus.championship_labels[language];
            Menu result;
            result.compact = true;
            if (controller.screen() == Screen::championship_save_feedback) {
                result.title = controller.championship_saved() ? labels.saved : labels.cancelled;
                result.items.push_back({{}, {}});
            } else {
                result.title = controller.screen() == Screen::championship_save_prompt ? labels.save_prompt : labels.overwrite;
                result.items.push_back({menus.options[language].yes, {}});
                result.items.push_back({menus.options[language].no, {}});
            }
            return result;
        }
        case Screen::championship_options: {
            auto result = menus.championship_options[language];
            const auto& labels = menus.championship_labels[language];
            result.items[0].label = controller.setup().championship_full ? labels.full_length : labels.short_length;
            return result;
        }
        case Screen::difficulty:
            return menus.difficulty[language];
        case Screen::time_trial_options: {
            auto result = menus.time_trial[language];
            const auto& option = menus.options[language];
            const auto& setup = controller.setup();
            result.items[0].label = format_label(menus.weather_format, {option.weather, option.weather_values.at(setup.weather)});
            result.items[1].label = format_label(menus.weather_format, {option.load_best_lap, setup.ghost_enabled ? option.yes : option.no});
            return result;
        }
        case Screen::network_options: {
            auto result = menus.network_options[language];
            const auto& labels = menus.network_labels[language];
            const auto& option = menus.options[language];
            const auto& setup = controller.setup();
            result.items[0].label = format_label(menus.laps_format, {std::to_string(setup.laps), option.lap, option.race});
            result.items[1].label = format_label(menus.weather_format, {option.weather, option.weather_values.at(setup.weather)});
            result.items[2].label = format_label(menus.weather_format, {labels.car_boost, setup.car_boost ? option.yes : option.no});
            result.items[3].label = format_label(menus.weather_format, {labels.computer_cars, controller.network_ai_enabled() ? option.yes : option.no});
            return result;
        }
        case Screen::single_options: {
            auto result = menus.single[language];
            const auto& option = menus.options[language];
            const auto& setup = controller.setup();
            if (setup.weather >= option.weather_values.size() || result.items.size() != 5) {
                throw std::logic_error("Invalid single race menu state");
            }
            result.items[0].label = format_label(menus.players_format, {option.players, std::to_string(setup.players)});
            result.items[1].label = format_label(menus.laps_format, {std::to_string(setup.laps), option.lap, option.race});
            if (setup.players > 1) {
                // 4694f0 keeps the Single Race row's icon while sharing Linkup's boost label/help.
                result.items[2].label = format_label(menus.weather_format, {menus.network_labels[language].car_boost, setup.car_boost ? option.yes : option.no});
                result.items[2].help = menus.network_options[language].items[2].help;
            } else {
                result.items[2].label = format_label(menus.qualify_format, {option.qualify, setup.qualifying ? option.yes : option.no});
            }
            result.items[3].label = format_label(menus.weather_format, {option.weather, option.weather_values[setup.weather]});
            return result;
        }
        default:
            throw std::logic_error("Frontend screen has no generic menu");
    }
}

} // namespace toca::frontend
