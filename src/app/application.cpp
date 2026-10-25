#include "app/application.hpp"

#include "app/audio_timeline.hpp"
#include "app/finale_session.hpp"
#include "app/gamepad_feedback.hpp"
#include "app/input.hpp"
#include "app/input_devices.hpp"
#include "app/network.hpp"
#include "app/options.hpp"
#include "app/paths.hpp"
#include "app/runtime_resources.hpp"
#include "app/scene_session.hpp"
#include "app/step_input.hpp"
#include "app/trace.hpp"
#include "app/wheel_feedback.hpp"
#include "assets/store.hpp"
#include "audio/championship.hpp"
#include "audio/driving.hpp"
#include "audio/engine.hpp"
#include "audio/finish.hpp"
#include "audio/frontend.hpp"
#include "audio/horn.hpp"
#include "audio/impact.hpp"
#include "audio/pause.hpp"
#include "audio/pit.hpp"
#include "audio/rivals.hpp"
#include "audio/scrape.hpp"
#include "audio/service.hpp"
#include "audio/start.hpp"
#include "audio/weather.hpp"
#include "core/random.hpp"
#include "frontend/controller.hpp"
#include "frontend/network_status.hpp"
#include "frontend/view.hpp"
#include "platform/display.hpp"
#include "render/gpu_renderer.hpp"
#include "scene/catalog.hpp"
#include "scene/championship_finale_view.hpp"
#include "scene/hud.hpp"
#include "scene/loading.hpp"
#include "scene/pause_view.hpp"
#include "scene/qualifying_view.hpp"
#include "scene/skid_marks.hpp"
#include "scene/view.hpp"
#include "scene/weather.hpp"
#include "settings/championship.hpp"
#include "settings/championship_records.hpp"
#include "settings/extras.hpp"
#include "settings/lap_records.hpp"
#include "settings/preferences.hpp"
#include "simulation/motion.hpp"
#include "simulation/race_world.hpp"
#include "simulation/session.hpp"
#include "simulation/time_trial.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <iostream>
#include <optional>
#include <vector>

namespace toca {
namespace {
class SdlRuntime {
public:
    explicit SdlRuntime(const Options& options) {
#ifdef _WIN32
        SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, "vulkan", SDL_HINT_DEFAULT);
#endif
        if (!options.gpu_driver.empty()) {
            gpu::check(SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, options.gpu_driver.c_str(), SDL_HINT_OVERRIDE), "Select GPU driver");
        }
        gpu::check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD), "Initialize SDL");
    }

    ~SdlRuntime() {
        SDL_Quit();
    }
};

audio::FrontendCue audio_cue(frontend::Cue cue) {
    switch (cue) {
        case frontend::Cue::navigation:
            return audio::FrontendCue::navigation;
        case frontend::Cue::option:
            return audio::FrontendCue::option;
        case frontend::Cue::confirm:
            return audio::FrontendCue::confirm;
        case frontend::Cue::character:
            return audio::FrontendCue::character;
        case frontend::Cue::unavailable:
            return audio::FrontendCue::unavailable;
        case frontend::Cue::cheat_activated:
            return audio::FrontendCue::cheat_activated;
    }
    throw std::logic_error("Unknown frontend cue");
}

std::vector<frontend::Resolution> settings_resolutions(const platform::Display& display) {
    std::vector<frontend::Resolution> result{{640, 480}, {800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1280, 960}, {1440, 900}, {1600, 900}, {1600, 1200}, {1680, 1050},
        {1920, 1080}, {1920, 1200}, {2560, 1440}, {2560, 1600}, {3840, 2160}};
    for (const auto mode : display.fullscreen_modes()) {
        result.emplace_back(mode.width, mode.height);
    }
    const auto current = display.settings();
    result.emplace_back(current.width, current.height);
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

int application(const Options& options) {
    const auto resources = resource_root();
    const auto bundled_data = resources / "original-data";
    const auto paths = application_paths(options, std::filesystem::is_directory(bundled_data) ? bundled_data : std::filesystem::path{});
    auto settings = resolve_settings(load_preferences(paths.config), options);
    SdlRuntime runtime(options);
    platform::Display display("TOCA Touring Car Championship", settings.video);
    settings.video = display.settings();
    gpu::Renderer renderer(display.window());
    settings.video.vsync = renderer.set_vsync(settings.video.vsync);
    audio::Service audio(settings.audio, options.audio_offline);
    AudioTimeline audio_time(audio);
    // 0046846b seeds from time(0)>>1, then selects one of eight menu cars.
    SourceRandom random(std::uint32_t(std::time(nullptr)) >> 1);
    const auto drawn_background = random.next() & 7u;
    const auto menu_background = options.menu_background.value_or(drawn_background);
    //42e6f0 retains4d5634 for the process, including new tracks and grid restarts.
    auto race_world_process = simulation::fresh_race_world_process();
    auto& surface_roughness = race_world_process.roughness;
    const AssetStore assets(paths.data_root, paths.bundled_data_root);
    frontend::Controller frontend;
    frontend.set_lap_records(load_lap_records(paths.state_root / "time-trial" / "records.dat", frontend::load_default_lap_records()));
    const auto championship_records_path = paths.state_root / "championship" / "records.dat";
    const auto championship_record_defaults = load_default_championship_records();
    const auto refresh_championship_records = [&] {
        try {
            frontend.set_championship_records(load_championship_records(championship_records_path, championship_record_defaults));
        } catch (const std::exception& failure) {
            frontend.set_championship_records(championship_record_defaults, "Championship high scores could not be loaded.");
            std::cerr << "Championship records: " << failure.what() << '\n';
        }
    };
    refresh_championship_records();
    const auto extras_path = paths.state_root / "extras.cfg";
    frontend.set_cheats_state(load_extras(extras_path));
    std::optional<race::ChampionshipState> championship;
    bool championship_record_submitted = false;
    std::optional<ChampionshipRecords> pending_championship_records;
    for (const bool showdown : {false, true}) {
        frontend.set_championship_slots(showdown, load_championship_slots(paths.state_root, showdown));
    }
    // Named slots change only after the source Save Game / Replace prompts.
    // Session boundaries retain current state in memory and merge earned unlocks.
    const auto sync_championship = [&] {
        if (!championship) {
            return true;
        }
        try {
            auto cheats = frontend.cheats_state();
            if (race::championship_unlocks(*championship, cheats.progress)) {
                require_writable_outside_data(extras_path, paths.data_root, paths.bundled_data_root);
                save_extras(extras_path, cheats);
                frontend.set_cheats_state(std::move(cheats));
            }
            frontend.set_championship_state(championship);
            return true;
        } catch (const std::exception& failure) {
            frontend.set_championship_state(championship, "Championship progress could not be saved. Check free space and try again.");
            std::cerr << "Championship: " << failure.what() << '\n';
            return false;
        }
    };
    const auto submit_championship_record = [&] {
        if (!championship || championship_record_submitted) {
            return true;
        }
        try {
            if (!pending_championship_records) {
                const auto candidate = championship_record_candidate(*championship, frontend::cheats::active(scene_cheats(frontend), frontend::cheats::Code::boost));
                if (candidate) {
                    // Never replace a corrupt table with startup defaults.
                    auto records = load_championship_records(championship_records_path, championship_record_defaults);
                    if (insert_championship_record(records, *candidate)) {
                        pending_championship_records = std::move(records);
                    } else {
                        frontend.set_championship_records(std::move(records));
                    }
                }
            }
            if (pending_championship_records) {
                // Keep the prepared table across errors, even if rename
                // succeeded before a directory-fsync failure. Retrying must
                // not insert this same terminal event a second time.
                require_writable_outside_data(championship_records_path, paths.data_root, paths.bundled_data_root);
                save_championship_records(championship_records_path, *pending_championship_records);
                frontend.set_championship_records(*pending_championship_records);
                pending_championship_records.reset();
            }
            championship_record_submitted = true;
            return true;
        } catch (const std::exception& failure) {
            frontend.set_championship_state(championship, "Championship high scores could not be saved. Check storage and try again.");
            std::cerr << "Championship records: " << failure.what() << '\n';
            return false;
        }
    };
    std::unique_ptr<audio::Championship> championship_commentary;
    ExtrasChanges extras_changes;
    frontend.set_settings(settings, settings_resolutions(display));
    SettingsChanges settings_changes;
    auto view = std::make_unique<frontend::View>(assets, renderer, menu_background);
    std::unique_ptr<scene::LoadingView> loading;
    std::optional<scene::WeatherPersistentState> weather_state;
    std::optional<scene::SkidMarkPersistentState> skid_state;
    auto& damage_state = race_world_process.damage;
    std::unique_ptr<SceneSession> session;
    std::unique_ptr<FinaleSession> finale;
    std::int16_t finale_initial_heading = 0;
    std::uint32_t finale_audio_view = 2;
    std::uint32_t loading_step = 0;
    std::size_t scene_vertices = 0;
    Trace trace(options.trace, surface_roughness);
    NetworkRuntime network(assets, options);
    std::optional<frontend::NetworkLobbyCommand> pending_network_connect;
    InputState input;
    InputDevices input_devices(settings.input);
    input_devices.apply(settings.input, settings.input_player2);
    std::array<GamepadFeedback, 2> gamepad_feedback;
    std::array<WheelFeedback, 2> wheel_feedback;
    const auto refresh_input_devices = [&] {
        frontend.set_input_devices(input_devices.devices(), input_devices.selected_device());
    };
    refresh_input_devices();
    std::uint32_t input_players = 1;
    bool escape_down = false;
    StepInputClock app_clock;
    const auto app_now = [&] {
        return app_clock.now(SDL_GetTicksNS());
    };
    input.set_focus((SDL_GetWindowFlags(display.window()) & SDL_WINDOW_INPUT_FOCUS) != 0);
    input_devices.set_focus(input.snapshot().focused);
    bool minimized = (SDL_GetWindowFlags(display.window()) & SDL_WINDOW_MINIMIZED) != 0;
    bool text_input_active = false;
    std::uint64_t event_sequence = 0;
    auto emit = [&](GameEventKind kind, std::uint32_t value = 0) {
        audio.consume({++event_sequence, app_now(), kind, value});
    };
    emit(GameEventKind::startup);
    if (options.save_settings) {
        save_preferences(paths.config, settings);
    }
    const auto apply_settings = [&](const Settings& requested) {
        ++settings_changes.requests;
        const auto previous = settings;
        std::string error;
        try {
            validate_settings(requested);
            if (requested.video != previous.video) {
                display.apply(requested.video);
                settings.video = display.settings();
                settings.video.vsync = renderer.set_vsync(requested.video.vsync);
            }
            audio.apply(requested.audio);
            settings.audio = requested.audio;
            settings.graphics = requested.graphics;
            settings.input = requested.input;
            settings.input_player2 = requested.input_player2;
            input_devices.apply(settings.input, settings.input_player2);
        } catch (const std::exception& failure) {
            error = failure.what();
            // A failed display/audio change must leave the menu usable. Saving
            // happens separately so a storage failure retains effective values.
            try {
                if (requested.video != previous.video) {
                    display.apply(previous.video);
                    settings.video = display.settings();
                    settings.video.vsync = renderer.set_vsync(previous.video.vsync);
                }
                audio.apply(previous.audio);
                settings.audio = previous.audio;
                settings.graphics = previous.graphics;
                settings.input = previous.input;
                settings.input_player2 = previous.input_player2;
                input_devices.apply(settings.input, settings.input_player2);
            } catch (const std::exception& restore_failure) {
                error += "; restore: ";
                error += restore_failure.what();
            }
        }
        if (error.empty()) {
            try {
                // Recheck the path at the actual write boundary, including symlinks.
                require_writable_outside_data(paths.config, paths.data_root, paths.bundled_data_root);
                save_preferences(paths.config, settings);
                ++settings_changes.saved;
            } catch (const std::exception& failure) {
                error = failure.what();
            }
        }
        settings_changes.error = !error.empty();
        frontend.settings_applied(settings, error);
        refresh_input_devices();
        if (!error.empty()) {
            std::cerr << "Settings: " << error << '\n';
        }
    };
    std::cout << "TOCA native startup: " << settings.video.width << 'x' << settings.video.height << " fullscreen=" << settings.video.fullscreen << " vsync=" << settings.video.vsync
              << " fps_limit=" << settings.video.fps_limit << " volume=" << settings.audio.master_gain << " mute=" << settings.audio.muted << " audio_active=" << audio.active()
              << " offline=" << options.audio_offline << '\n'
              << "Data: " << paths.data_root.string() << "\nResources: " << resources.string() << "\nPreferences: " << paths.config.string()
              << "\nSave data: " << paths.state_root.string() << '\n';
    std::cout.flush();
    auto previous_time = app_now();
    auto previous_frontend_time = previous_time;
    audio_time.reset(previous_time);
    std::uint64_t presented_frames = 0, poll = 0, frontend_cues = 0, next_music_check = 0;
    std::optional<std::uint64_t> copyright_presented_ns;
    bool frontend_resources_loaded = false;
    std::optional<std::uint64_t> previous_step_ready;
    std::uint32_t previous_step_completed_laps = 0;
    bool step_input_finished = false;
    const auto feedback_active = [&](std::uint32_t player = 0) {
        return frontend.screen() == frontend::Screen::scene && session->active && !session->pause.active() && !session->finishing() && (player == 0 || session->second)
               && !(player == 0 ? session->motion : session->second->motion).ai_controlled()
               && (player == 0 ? session->motion : session->second->motion).state().boundary == simulation::MotionBoundary::none && input.snapshot().focused && !minimized
               && (player == 0 ? settings.input : settings.input_player2).rumble_enabled;
    };
    const auto stop_feedback = [&] {
        input_devices.stop_rumble();
        for (auto& feedback : gamepad_feedback) {
            feedback.reset();
        }
        for (std::size_t player = 0; player < wheel_feedback.size(); ++player) {
            const auto stop = wheel_feedback[player].reset();
            if (session) {
                session->wheel_feedback_commands[player] = {stop};
            }
        }
        if (session) {
            session->gamepad_rumble = {};
        }
    };
    const auto advance_scene = [&](std::uint64_t timestamp) {
        if (frontend.screen() == frontend::Screen::championship_finale) {
            finale->advance(timestamp, audio_time);
        }
        if (frontend.screen() == frontend::Screen::scene) {
            int width = 0, height = 0;
            gpu::check(SDL_GetWindowSizeInPixels(display.window(), &width, &height), "Read visibility viewport");
            if (width > 0 && height > 0) {
                session->set_viewport(std::uint32_t(width), std::uint32_t(height), settings.graphics.split_vertical);
            }
            session->advance(timestamp, audio_time, [&](std::uint64_t deadline, const InputSnapshot& actions) {
                if (feedback_active()) {
                    std::int32_t car_impact = 0;
                    if (session->rivals) {
                        for (const auto& contact : session->rivals->contacts()) {
                            if (contact.first == session->rivals->local_actor() || contact.second == session->rivals->local_actor()) {
                                car_impact = std::max(car_impact, contact.strength_q16);
                            }
                        }
                    }
                    if (session->second) {
                        for (const auto& contact : session->local_contact_events) {
                            car_impact = std::max(car_impact, contact.strength_q16);
                        }
                    }
                    const bool wheel_output = input_devices.begin_wheel_feedback();
                    session->gamepad_rumble = wheel_output ? GamepadRumble{} : gamepad_feedback[0].update(session->motion.state(), car_impact);
                    if (!wheel_output) {
                        input_devices.rumble(session->gamepad_rumble.low, session->gamepad_rumble.high);
                    }
                    session->wheel_feedback_commands[0] = wheel_feedback[0].update(session->motion.state());
                    for (const auto& command : session->wheel_feedback_commands[0]) {
                        input_devices.wheel_feedback(command);
                    }
                } else {
                    input_devices.rumble(0, 0);
                    gamepad_feedback[0].reset();
                    session->wheel_feedback_commands[0] = {wheel_feedback[0].reset()};
                    input_devices.wheel_feedback(session->wheel_feedback_commands[0].front());
                    session->gamepad_rumble = {};
                }
                if (feedback_active(1)) {
                    std::int32_t car_impact = 0;
                    for (const auto& contact : session->local_contact_events) {
                        car_impact = std::max(car_impact, contact.strength_q16);
                    }
                    if (!input_devices.begin_wheel_feedback(1)) {
                        const auto rumble = gamepad_feedback[1].update(session->second->motion.state(), car_impact);
                        input_devices.rumble(rumble.low, rumble.high, 80, 1);
                    }
                    session->wheel_feedback_commands[1] = wheel_feedback[1].update(session->second->motion.state());
                    for (const auto& command : session->wheel_feedback_commands[1]) {
                        input_devices.wheel_feedback(command, 1);
                    }
                } else if (session->second) {
                    input_devices.rumble(0, 0, 80, 1);
                    gamepad_feedback[1].reset();
                    session->wheel_feedback_commands[1] = {wheel_feedback[1].reset()};
                    input_devices.wheel_feedback(session->wheel_feedback_commands[1].front(), 1);
                }
                trace.record_simulation(deadline, *session, actions);
            });
        }
    };
    const auto sync_device_input = [&](std::uint64_t timestamp) {
        const auto& state = input_devices.state();
        const bool capture = frontend.screen() == frontend::Screen::controller_keyboard_bindings || frontend.screen() == frontend::Screen::controller_gamepad_bindings;
        auto menu_held = capture ? (escape_down ? action_mask(Action::back) : 0u) : state.menu_held;
        if (frontend.screen() == frontend::Screen::scene && !session->pause.active()) {
            menu_held = (menu_held & ~action_mask(Action::back)) | state.pause_held | (session->second ? input_devices.state(1).pause_held : 0u);
        }
        constexpr std::array actions{Action::left, Action::right, Action::up, Action::down, Action::confirm, Action::back, Action::backspace, Action::gear_up, Action::gear_down,
            Action::camera_next, Action::camera_rear, Action::camera_trackside, Action::horn};
        const bool diagnostic_controls = options.step_input && !step_input_finished && frontend.screen() == frontend::Screen::scene && !session->pause.active();
        for (const auto action : actions) {
            if (diagnostic_controls
                && (action == Action::left || action == Action::right || action == Action::up || action == Action::down || action == Action::gear_up
                    || action == Action::gear_down)) {
                continue;
            }
            input.set_action(action, (menu_held & action_mask(action)) != 0);
        }
        if (frontend.screen() == frontend::Screen::scene && session->active) {
            const auto game_held = (diagnostic_controls ? input.snapshot().held : state.driving_held) | (state.menu_held & action_mask(Action::confirm));
            session->actions.synchronize(timestamp, game_held, diagnostic_controls ? std::nullopt : std::optional(state.analog));
            if (session->second && !diagnostic_controls) {
                const auto& state2 = input_devices.state(1);
                session->second->actions.synchronize(timestamp, state2.driving_held, state2.analog);
            }
        }
    };
    const auto clear_device_input = [&] {
        input_devices.set_focus(false);
        input_devices.set_focus(input.snapshot().focused && !minimized);
        escape_down = false;
    };
    const auto scene_focus = [&](std::uint64_t timestamp, bool active) {
        if (frontend.screen() == frontend::Screen::championship_finale) {
            finale->activate(timestamp, active, audio_time);
        }
        if (frontend.screen() == frontend::Screen::scene) {
            advance_scene(timestamp);
            audio_time.advance(timestamp);
            if (session->linkup) {
                (void)network.session().publish_race(network::Pause{!active || session->pause.active()});
            }
            const bool shared_pause = session->linkup && network.session().race_view() && network.session().race_view()->paused;
            session->set_finish_audio_focus(active);
            session->activate(timestamp, active && !session->pause.active() && !shared_pause);
        }
    };
    const auto step_keys = [&](std::uint32_t held, std::uint64_t deadline) {
        constexpr std::array actions{Action::left, Action::right, Action::up, Action::down, Action::gear_up, Action::gear_down};
        const auto previous = input.snapshot().held;
        for (const auto action : actions) {
            const auto mask = action_mask(action);
            if ((previous & mask) != (held & mask)) {
                const bool down = (held & mask) != 0;
                input.set_action(action, down);
                session->actions.set_action(deadline, action, down);
            }
        }
    };
    // 00463410 checks after 61 frontend iterations at 2/60 seconds. Keep that
    // wall-clock interval independent of the presentation rate.
    constexpr std::uint64_t music_check_interval = 61'000'000'000 / 30;
    const auto reload_frontend = [&] {
        view = std::make_unique<frontend::View>(assets, renderer, menu_background);
        view->load_frontend();
        audio::load_frontend_samples(audio, assets);
        const auto restarted = app_now();
        audio.start_music(2, 1, restarted);
        next_music_check = restarted + music_check_interval;
        previous_frontend_time = restarted;
        audio_time.reset(restarted);
    };
    const auto return_network_scene = [&] {
        stop_feedback();
        audio.stop_music();
        session.reset();
        audio.clear_samples();
        loading.reset();
        loading_step = 0;
        scene_vertices = 0;
        reload_frontend();
        clear_device_input();
        frontend.return_network_lobby();
        frontend.set_network_view(network.session().view());
        if (network.session().view().phase == network::SessionPhase::failed) {
            frontend.network_error(network.session().view().error);
        }
    };
    // Use the same event mapping while synchronous scene resources are read.
    // Asset callbacks only interrupt construction; cleanup happens after unwind.
    const auto poll_events = [&] {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            const auto event_time = event.common.timestamp == 0 ? app_now() : app_clock.event_time(event.common.timestamp);
            if (const auto physical = input_devices.handle_event(event)) {
                input.set_physical_binding(*physical);
                input.key_pressed();
            }
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    input.request_quit();
                    break;
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                    if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat) {
                        break;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN) {
                        input.key_pressed();
                    }
                    if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                        escape_down = event.type == SDL_EVENT_KEY_DOWN;
                    }
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                case SDL_EVENT_GAMEPAD_REMOVED:
                case SDL_EVENT_GAMEPAD_REMAPPED:
                case SDL_EVENT_JOYSTICK_ADDED:
                case SDL_EVENT_JOYSTICK_REMOVED:
                    refresh_input_devices();
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        input.set_action(Action::pointer_primary, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                    }
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    if (text_input_active) {
                        input.text_input(event.text.text);
                    }
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    input.set_focus(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
                    input_devices.set_focus(input.snapshot().focused && !minimized);
                    escape_down = false;
                    scene_focus(event_time, input.snapshot().focused && !minimized);
                    emit(GameEventKind::focus_changed, input.snapshot().focused ? 1u : 0u);
                    break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                    minimized = true;
                    input_devices.set_focus(false);
                    scene_focus(event_time, false);
                    break;
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    if (minimized) {
                        minimized = false;
                        clear_device_input();
                    }
                    scene_focus(event_time, input.snapshot().focused);
                    break;
                default:
                    break;
            }
            sync_device_input(event_time);
        }
    };
    const auto loading_back_cue = [&] {
        audio::play_frontend_cue(audio, audio::FrontendCue::back, frontend.language());
        ++frontend_cues;
    };

    struct NetworkLoadInterrupted {
        bool cancelled = false;
    };

    std::uint64_t next_loading_present = 0;
    const auto service_loading = [&] {
        network.pump_load();
        poll_events();
        const bool linkup = frontend.setup().mode == GameMode::linkup;
        if (linkup) {
            if (input.snapshot().quit_requested || (input.snapshot().focused && (input.snapshot().pressed & action_mask(Action::back)) != 0)) {
                (void)network.session().abort_loading(network::LoadingAbortReason::cancelled);
                throw NetworkLoadInterrupted{true};
            }
            if (!network.session().race_view()) {
                throw NetworkLoadInterrupted{};
            }
        }
        const auto loading_now = SDL_GetTicksNS();
        if (loading && !minimized && loading_now >= next_loading_present) {
            const auto status =
                linkup ? frontend::network_loading_status(frontend.language(), network.session().view(), network.session().race_view()) : std::vector<std::string>{};
            if (renderer.draw(loading->frame(loading_step, 2, status))) {
                ++presented_frames;
            }
            next_loading_present = loading_now + 50'000'000;
        }
    };
    const auto run_loading_stage = [&](auto&& work) {
        try {
            work();
            return true;
        } catch (const NetworkLoadInterrupted& interruption) {
            return_network_scene();
            if (interruption.cancelled) {
                loading_back_cue();
            }
        } catch (const std::exception& failure) {
            if (frontend.setup().mode != GameMode::linkup) {
                throw;
            }
            std::cerr << "Linkup loading: " << failure.what() << '\n';
            if (network.session().race_view()) {
                (void)network.session().abort_loading(network::LoadingAbortReason::load_failed);
            }
            return_network_scene();
        }
        return false;
    };
    while (!input.snapshot().quit_requested && (options.frames == 0 || presented_frames < options.frames)) {
        network.pump();
        const auto wall_now = SDL_GetTicksNS();
        const auto now = app_clock.now(wall_now);
        if (frontend.setup().mode == GameMode::linkup && (frontend.screen() == frontend::Screen::scene || frontend.screen() == frontend::Screen::scene_loading)) {
            const bool lost = !network.session().race_view() || (session && session->linkup && !session->sync_network());
            if (lost) {
                return_network_scene();
            } else if (session && session->linkup && frontend.screen() == frontend::Screen::scene) {
                session->activate(now, input.snapshot().focused && !minimized && !session->pause.active() && !network.session().race_view()->paused);
            }
        }
        const ClockStep clock{now, now - previous_time};
        const ClockStep frontend_clock{now, now - previous_frontend_time};
        previous_time = now;
        previous_frontend_time = now;
        if (frontend.screen() != frontend::Screen::scene && frontend.screen() != frontend::Screen::championship_finale) {
            audio_time.advance(now);
        }
        input.begin_poll(now);
        if (input_players != frontend.setup().players) {
            input_players = frontend.setup().players;
            input_devices.set_local_players(input_players);
            refresh_input_devices();
        }
        input_devices.set_configuration_player(frontend.controller_player());
        input_devices.begin_poll();
        poll_events();
        auto dispatch_time = app_now();
        sync_device_input(dispatch_time);
        advance_scene(dispatch_time);
        sync_device_input(dispatch_time);
        audio_time.advance(dispatch_time);
        float pointer_x = 0, pointer_y = 0;
        SDL_GetMouseState(&pointer_x, &pointer_y);
        int pixel_width = 0, pixel_height = 0;
        gpu::check(SDL_GetWindowSizeInPixels(display.window(), &pixel_width, &pixel_height), "Read current input viewport");
        const auto pointer = display.map_pointer(pointer_x, pointer_y, {std::uint32_t(std::max(pixel_width, 0)), std::uint32_t(std::max(pixel_height, 0))});
        input.set_pointer(pointer.x, pointer.y, pointer.inside && input.snapshot().focused && !minimized);
        if (frontend.screen() == frontend::Screen::main_menu && frontend.selection() == 1 && input.snapshot().focused
            && (input.snapshot().pressed & action_mask(Action::confirm)) != 0) {
            const bool vsync = settings.video.vsync;
            settings.video = display.settings();
            settings.video.vsync = vsync;
            frontend.set_settings(settings, settings_resolutions(display));
        }
        const auto previous_screen = frontend.screen();
        frontend.set_network_view(network.session().view());
        for (const auto cue : frontend.update(input.snapshot(), frontend_clock)) {
            const auto voice = audio::play_frontend_cue(audio, audio_cue(cue), frontend.language(), extras_changes.announcement_voice);
            if (cue == frontend::Cue::cheat_activated && voice != 0) {
                extras_changes.announcement_voice = voice;
                ++extras_changes.announcements;
                extras_changes.sample = audio::frontend_sample_id(audio_cue(cue), frontend.language());
            }
            ++frontend_cues;
        }
        if (frontend.screen() == frontend::Screen::controller_gamepad_bindings && previous_screen != frontend::Screen::controller_gamepad_bindings) {
            input_devices.set_configuration_player(frontend.controller_player());
            input_devices.begin_binding_capture();
        }
        if (frontend.screen() == frontend::Screen::championship_records && previous_screen != frontend::Screen::championship_records) {
            refresh_championship_records();
        }
        if (session && session->championship_dirty) {
            session->championship_dirty = false;
            if (!sync_championship()) {
                session->ghost_error = std::string(frontend.championship_error());
                session->pause.open(false, settings.audio, session->mode);
                session->activate(dispatch_time, false);
            } else {
                session->ghost_error.clear();
            }
        }
        if (const auto request = frontend.take_championship_request()) {
            bool save_prompt = false;
            std::optional<race::ChampionshipHonors> honors;
            std::optional<race::ChampionshipState> advance_from;
            if (*request == frontend::ChampionshipRequest::save) {
                if (championship) {
                    try {
                        require_writable_outside_data(
                            championship_slot_path(paths.state_root, championship->showdown, frontend.championship_slot()), paths.data_root, paths.bundled_data_root);
                        save_championship_slot(paths.state_root, frontend.championship_slot(), *championship);
                        frontend.set_championship_slots(championship->showdown, load_championship_slots(paths.state_root, championship->showdown));
                        frontend.championship_save_result();
                    } catch (const std::exception& failure) {
                        frontend.championship_save_result("Championship could not be saved. Check free space and try again.");
                        std::cerr << "Championship: " << failure.what() << '\n';
                    }
                }
            } else {
                if (*request == frontend::ChampionshipRequest::start) {
                    championship_record_submitted = false;
                    pending_championship_records.reset();
                    finale_initial_heading = 0;
                    finale_audio_view = 2;
                    championship = race::new_championship(
                        frontend.setup().championship_full, frontend.stored_car_id(), std::string(frontend.player_name()), frontend.setup().championship_showdown);
                    championship->transmission_manual = frontend.transmission_manual();
                } else if (*request == frontend::ChampionshipRequest::resume) {
                    championship_record_submitted = false;
                    pending_championship_records.reset();
                    finale_initial_heading = 0;
                    finale_audio_view = 2;
                    championship = frontend.championship_state();
                } else if (championship && *request == frontend::ChampionshipRequest::advance && championship->phase == race::ChampionshipPhase::results) {
                    // 44a0c0/4491b0: only a completed meeting offers Save Game.
                    // Race1 proceeds to Qualifying2; final/eliminated states do not save.
                    advance_from = championship;
                    const bool meeting_finished = championship->race == 1;
                    honors = race::championship_honors(*championship);
                    race::championship_advance(*championship);
                    save_prompt = meeting_finished && championship->phase == race::ChampionshipPhase::qualifying;
                }
                //449de0 submits immediately on elimination entry. Ordinary
                //race transitions, abandon and host shutdown do not submit.
                if (championship && sync_championship() && (championship->phase != race::ChampionshipPhase::eliminated || submit_championship_record())) {
                    if (save_prompt) {
                        championship_commentary.reset();
                        frontend.show_championship_save_prompt(std::move(honors));
                    } else if (honors) {
                        championship_commentary.reset();
                        frontend.show_championship_honors(std::move(*honors));
                    } else if (championship->phase == race::ChampionshipPhase::qualifying || championship->phase == race::ChampionshipPhase::race) {
                        championship_commentary.reset();
                        frontend.begin_championship_round(*championship, race::championship_track(*championship), race::championship_laps(*championship),
                            championship->phase == race::ChampionshipPhase::qualifying);
                    } else if (previous_screen == frontend::Screen::championship_honors && championship->phase == race::ChampionshipPhase::complete) {
                        //449790 records after the last honors, before loading
                        //the convoy. A failed write leaves honors retryable.
                        if (submit_championship_record()) {
                            championship_commentary.reset();
                            frontend.show_championship_finale();
                        }
                    } else {
                        frontend.show_championship_overview();
                        championship_commentary = std::make_unique<audio::Championship>(audio, assets, frontend.language());
                        const auto cue = championship->phase == race::ChampionshipPhase::eliminated ? audio::ChampionshipCue::eliminated
                                         : championship->phase == race::ChampionshipPhase::complete ? audio::ChampionshipCue::complete
                                                                                                    : audio::ChampionshipCue::standings;
                        championship_commentary->play(cue, *championship, random);
                    }
                } else if (advance_from) {
                    // Keep the unconsumed result if earned-progress storage fails:
                    // retry must still offer saving and retain the block's podiums.
                    const auto error = std::string(frontend.championship_error());
                    championship = std::move(advance_from);
                    frontend.set_championship_state(championship, error);
                }
            }
        }
        if (frontend.screen() == frontend::Screen::championship_honors && previous_screen != frontend::Screen::championship_honors && frontend.championship_honors()->showdown
            && frontend.championship_honors()->block == 3) {
            championship_commentary = std::make_unique<audio::Championship>(audio, assets, frontend.language());
            championship_commentary->play(audio::ChampionshipCue::showdown_final, *championship, random);
        }
        if (championship_commentary) {
            if (frontend.screen() != frontend::Screen::championship_overview && frontend.screen() != frontend::Screen::championship_honors) {
                championship_commentary.reset();
            } else {
                championship_commentary->set_active(input.snapshot().focused && !minimized);
                championship_commentary->update();
            }
        }
        for (;;) {
            auto command = frontend.take_network_request();
            if (!command && pending_network_connect && !network.session().draining()) {
                command = std::exchange(pending_network_connect, std::nullopt);
            }
            if (!command) {
                break;
            }
            try {
                if (command->kind == frontend::NetworkLobbyCommandKind::host || command->kind == frontend::NetworkLobbyCommandKind::join) {
                    // A changed session name destroys/recreates the endpoint.
                    // Let reliable Leave release its bound port before rehosting.
                    if (network.session().draining()) {
                        pending_network_connect = *command;
                        continue;
                    }
                    pending_network_connect.reset();
                }
                switch (command->kind) {
                    case frontend::NetworkLobbyCommandKind::host: {
                        const auto address = network::parse_address("127.0.0.1:" + command->endpoint);
                        network.host(address.port, command->name, command->car, command->manual, static_cast<std::uint8_t>(settings.input.sensitivity));
                        break;
                    }
                    case frontend::NetworkLobbyCommandKind::join:
                        network.join(command->endpoint, command->name, command->car, command->manual, static_cast<std::uint8_t>(settings.input.sensitivity));
                        break;
                    case frontend::NetworkLobbyCommandKind::leave:
                        pending_network_connect.reset();
                        if (frontend.screen() == frontend::Screen::scene_loading) {
                            (void)network.session().abort_loading(network::LoadingAbortReason::cancelled);
                            return_network_scene();
                            loading_back_cue();
                        } else {
                            network.session().leave();
                        }
                        break;
                    case frontend::NetworkLobbyCommandKind::select_car:
                        (void)network.session().select_car(command->car, command->manual);
                        break;
                    case frontend::NetworkLobbyCommandKind::ready:
                        (void)network.session().ready(command->ready);
                        break;
                    case frontend::NetworkLobbyCommandKind::set_settings:
                        (void)network.session().set_settings(command->settings);
                        break;
                    case frontend::NetworkLobbyCommandKind::start: {
                        const auto& lobby = network.session().view();
                        const auto humans = std::uint32_t(std::count_if(lobby.roster.players.begin(), lobby.roster.players.end(), [](const auto& player) {
                            return player.ready;
                        }));
                        auto grid = simulation::make_linkup_grid(humans, lobby.settings.ai ? 8 : humans, random);
                        const auto seed = std::uint32_t(SDL_GetTicks());
                        (void)network.session().start_race(seed, std::move(grid));
                        break;
                    }
                }
            } catch (const std::exception& error) {
                pending_network_connect.reset();
                frontend.network_error(error.what());
            }
            frontend.set_network_view(network.session().view());
        }
        if (frontend.screen() == frontend::Screen::network_linkup && network.session().view().phase == network::SessionPhase::loading && network.session().race_view()) {
            frontend.begin_network_race(network.session().view(), *network.session().race_view());
        }
        if (frontend.take_cheats_changed()) {
            std::string error;
            try {
                require_writable_outside_data(extras_path, paths.data_root, paths.bundled_data_root);
                save_extras(extras_path, frontend.cheats_state());
                ++extras_changes.saved;
            } catch (const std::exception& failure) {
                error = failure.what();
                std::cerr << "Extras: " << error << '\n';
            }
            frontend.cheats_saved(error);
        }
        if (const auto requested = frontend.take_settings_request()) {
            apply_settings(*requested);
        }
        if (frontend.setup().players == 1 && frontend.screen() == frontend::Screen::scene_loading && previous_screen != frontend::Screen::scene_loading
            && previous_screen != frontend::Screen::scene) {
            // Pin the confirming pad for driving/vibration. Auto keeps both
            // input maps enabled; keyboard starts retain the configured mode.
            if (const auto device = input_devices.select_confirming_gamepad()) {
                if (*device != settings.input.device_id) {
                    auto requested = settings;
                    requested.input.device_id = *device;
                    apply_settings(requested);
                } else {
                    refresh_input_devices();
                }
            }
        }
        if (frontend.screen() == frontend::Screen::scene && input.snapshot().focused && !minimized) {
            scene::PauseUpdate update;
            if (session->pause.active()) {
                update = session->pause.update(input.snapshot());
            } else if ((input.snapshot().pressed & action_mask(Action::back)) != 0) {
                session->pause.open(session->qualifying && !session->finishing(), settings.audio, session->mode);
                if (session->linkup) {
                    (void)network.session().publish_race(network::Pause{true});
                }
                session->activate(dispatch_time, false);
            }
            if (update.audio_changed) {
                const auto previous_music = settings.audio.game_music;
                settings.audio = session->pause.audio();
                audio.apply(settings.audio);
                session->game_music = settings.audio.game_music;
                if (previous_music != session->game_music) {
                    if (session->game_music) {
                        audio.start_music(scene::track_content(session->resources.setup.selection.track_id).music_track, 1, dispatch_time);
                    } else {
                        audio.stop_music();
                    }
                }
                ++settings_changes.requests;
                try {
                    require_writable_outside_data(paths.config, paths.data_root, paths.bundled_data_root);
                    save_preferences(paths.config, settings);
                    ++settings_changes.saved;
                    settings_changes.error = false;
                } catch (const std::exception& failure) {
                    settings_changes.error = true;
                    std::cerr << "Pause settings: " << failure.what() << '\n';
                }
            }
            for (const auto cue : update.cues) {
                session->pause_audio->play(static_cast<std::uint32_t>(cue));
            }
            if (session->race_return_requested) {
                session->race_return_requested = false;
                if (session->linkup) {
                    // 44a370 -> 4638bb -> 49414f closes the Linkup session
                    // when its results are acknowledged.
                    network.session().leave();
                    return_network_scene();
                    continue;
                } else if (session->championship) {
                    update.action = scene::PauseAction::end_championship;
                } else {
                    session->pause.open_race_again(settings.audio);
                    session->activate(dispatch_time, false);
                }
            }
            if (update.action == scene::PauseAction::end_qualifying && session->race.end_qualifying()) {
                session->motion.handoff_to_ai();
                session->begin_qualifying_finish();
                session->refresh_visibility();
                session->update_hud();
            }
            if (session->championship && !session->ghost_error.empty() && update.action == scene::PauseAction::resume && !sync_championship()) {
                session->pause.open(false, settings.audio, session->mode);
                update.action = scene::PauseAction::none;
            }
            if (update.action == scene::PauseAction::resume || update.action == scene::PauseAction::end_qualifying) {
                if (session->championship) {
                    session->ghost_error.clear();
                }
                clear_device_input();
                if (session->linkup) {
                    (void)network.session().publish_race(network::Pause{false});
                }
                session->activate(dispatch_time, !session->linkup || !network.session().race_view()->paused);
                sync_device_input(dispatch_time);
            } else if (update.action == scene::PauseAction::end_championship && !sync_championship()) {
                session->ghost_error = std::string(frontend.championship_error());
                session->pause.open(false, settings.audio, session->mode);
                session->activate(dispatch_time, false);
            } else if (update.action == scene::PauseAction::end_time_trial && !session->save_time_trial(paths, frontend)) {
                session->pause.open(false, settings.audio, session->mode);
            } else if (update.action == scene::PauseAction::restart_race || update.action == scene::PauseAction::end_race || update.action == scene::PauseAction::end_time_trial
                       || update.action == scene::PauseAction::end_championship) {
                if (session->linkup) {
                    network.session().leave();
                    return_network_scene();
                    continue;
                }
                if (update.action == scene::PauseAction::restart_race && session->can_restart_warm()) {
                    clear_device_input();
                    session->restart_warm(assets, dispatch_time, input.snapshot().focused && !minimized);
                    //Grid reconstruction may outlast a physical interval.
                    //Activate only after it completes, then present/trace this
                    //grounded grid before its first ordinary deadline.
                    dispatch_time = app_now();
                    session->capture_presentation_tick(dispatch_time);
                    dispatch_time = app_now();
                    session->schedule.reset(dispatch_time);
                    session->actions.reset(dispatch_time, session->active);
                    if (session->second) {
                        session->second->actions.reset(dispatch_time, session->active);
                    }
                    session->presentation_due_ns = dispatch_time;
                    sync_device_input(dispatch_time);
                    audio_time.reset(dispatch_time);
                } else {
                    const bool championship_results = session->championship && session->race.state() == 13;
                    if (championship_results && session->championship->meeting == 11 && session->championship->race == 1) {
                        finale_initial_heading = session->camera.heading();
                        finale_audio_view = session->camera.selected_view();
                    }
                    audio.stop_music();
                    session.reset();
                    audio.clear_samples();
                    loading.reset();
                    loading_step = 0;
                    scene_vertices = 0;
                    if (update.action == scene::PauseAction::restart_race) {
                        frontend.restart_scene();
                    } else {
                        reload_frontend();
                        frontend.return_from_scene(championship_results);
                        if (championship_results) {
                            championship_commentary = std::make_unique<audio::Championship>(audio, assets, frontend.language());
                            championship_commentary->play(audio::ChampionshipCue::standings, *championship, random);
                        }
                    }
                }
            }
        }
        if (!feedback_active() && !feedback_active(1)) {
            stop_feedback();
        }
        if (frontend.screen() == frontend::Screen::championship_finale && finale->active && finale->flow.can_exit()
            && (input.snapshot().any_key_pressed || (input.snapshot().pressed & ~action_mask(Action::pointer_primary)) != 0)) {
            audio.stop_music();
            finale.reset();
            loading.reset();
            loading_step = 0;
            scene_vertices = 0;
            audio.clear_samples();
            reload_frontend();
            clear_device_input();
            frontend.return_from_championship_finale();
        }
        if ((frontend.screen() == frontend::Screen::scene_loading || frontend.screen() == frontend::Screen::championship_finale_loading) && !loading) {
            audio.stop_music();
            audio.clear_samples();
            extras_changes.announcement_voice = 0;
            next_music_check = 0;
            loading_step = 0;
            next_loading_present = 0;
            if (!run_loading_stage([&] {
                    const auto loading_assets = assets.for_loading(service_loading);
                    const bool finale_loading = frontend.screen() == frontend::Screen::championship_finale_loading;
                    const auto track = finale_loading ? 1 : frontend.stored_track_id();
                    std::optional<scene::LoadingStatistics> statistics;
                    if (!finale_loading) {
                        statistics = scene::LoadingStatistics{.language = frontend.language(),
                            .laps = frontend.setup().laps,
                            .time_trial = frontend.setup().mode == GameMode::time_trial,
                            .record = frontend.records()->at(track),
                            .ghost = {}};
                        if (statistics->time_trial && frontend.setup().ghost_enabled) {
                            try {
                                simulation::TimeTrial ghost(track, frontend.stored_car_id(), frontend.setup().weather, std::string(frontend.player_name()));
                                if (ghost.load(paths.state_root)) {
                                    statistics->ghost =
                                        frontend::LapRecord{.lap_ms = ghost.best_lap_ms(), .driver = ghost.best_driver(), .car_id = ghost.best_car_id(), .code = {}};
                                }
                            } catch (const std::exception&) {
                                // Session startup reports invalid/unreadable ghost saves.
                                // The loading screen retains the real track record.
                            }
                        }
                    }
                    loading = std::make_unique<scene::LoadingView>(loading_assets, renderer, track, settings.graphics.detail == graphics_mega_detail, resources / "ui", statistics);
                    view.reset();
                })) {
                continue;
            }
        }
        if (frontend.quit_requested()) {
            input.request_quit();
        }
        const bool needs_text_input =
            (frontend.screen() == frontend::Screen::name_entry || frontend.network_text_input()) && input.snapshot().focused && !frontend.quit_requested();
        if (needs_text_input != text_input_active) {
            gpu::check(needs_text_input ? SDL_StartTextInput(display.window()) : SDL_StopTextInput(display.window()), "Change driver name text input");
            text_input_active = needs_text_input;
        }
        if (input.snapshot().focused && next_music_check != 0 && now >= next_music_check) {
            audio.update_music(now, true);
            next_music_check += ((now - next_music_check) / music_check_interval + 1) * music_check_interval;
        }
        if (input.snapshot().focused && (frontend.screen() == frontend::Screen::scene || frontend.screen() == frontend::Screen::championship_finale)) {
            // 0044a1d0 calls 0047a3b4(0): race CD playback is finite, unlike
            // the frontend's explicit restart polling.
            audio.update_music(now, false);
        }
        const auto& snapshot = input.snapshot();
        bool presented = false;
        gpu::FrameTiming frame_timing{};
        std::uint64_t frame_prepare_ns = 0;
        const bool step_player_scene =
            options.step_input && !step_input_finished && frontend.screen() == frontend::Screen::scene && session->active && !minimized
            && (session->race.state() == 6 || session->race.state() == 7 || session->race.state() == 8 || session->race.state() == 10 || session->race.state() == 13);
        // Diagnostic presentation sampling only. Every physics/audio/HUD update
        // and trace still runs, including the seven updates between images.
        const bool step_milestone =
            step_player_scene && (!previous_step_ready || session->race.completed_laps() != previous_step_completed_laps || session->race.lap_time_ms() == 0);
        const bool step_frame_due = step_player_scene && (step_milestone || session->steps % 8 == 0);
        const bool scene_due = step_player_scene ? step_frame_due
                               : frontend.screen() == frontend::Screen::championship_finale
                                   ? settings.video.fps_limit == 0 || dispatch_time >= finale->presentation_due_ns
                                   : frontend.screen() != frontend::Screen::scene || settings.video.fps_limit == 0 || dispatch_time >= session->presentation_due_ns;
        if (!minimized && pixel_width > 0 && pixel_height > 0 && scene_due) {
            const auto render_start = SDL_GetTicksNS();
            if (frontend.screen() == frontend::Screen::scene) {
                const auto prepared = session->prepare_frame(renderer, std::uint32_t(pixel_width), std::uint32_t(pixel_height), settings.graphics.split_vertical, dispatch_time,
                    options.step_input && !step_input_finished, frontend.language());
                scene_vertices = prepared.scene_vertices;
                presented = renderer.draw(prepared.frame, false);
                if (settings.video.fps_limit != 0) {
                    session->presentation_due_ns = dispatch_time + 1'000'000'000 / settings.video.fps_limit;
                }
            } else if (frontend.screen() == frontend::Screen::championship_finale) {
                const auto presentation = finale->presentation_frame();
                auto& frame = finale->rendered_frame;
                finale->view->frame_into(frame, presentation, finale->flags.variants(), finale->horizon.presentation_frame(finale->presentation_alpha), std::uint32_t(pixel_width),
                    std::uint32_t(pixel_height), finale->mirror, finale->upside);
                scene_vertices = frame.world_vertex_count();
                presented = renderer.draw(frame, false);
                if (settings.video.fps_limit != 0) {
                    finale->presentation_due_ns = dispatch_time + 1'000'000'000 / settings.video.fps_limit;
                }
            } else if (frontend.screen() == frontend::Screen::scene_loading || frontend.screen() == frontend::Screen::championship_finale_loading) {
                const auto status = frontend.setup().mode == GameMode::linkup
                                        ? frontend::network_loading_status(frontend.language(), network.session().view(), network.session().race_view())
                                        : std::vector<std::string>{};
                presented = renderer.draw(loading->frame(loading_step, 2, status));
            } else {
                presented = renderer.draw(view->frame(frontend));
            }
            if (presented) {
                frame_timing = renderer.last_frame_timing();
                const auto render_ns = SDL_GetTicksNS() - render_start;
                const auto gpu_ns = frame_timing.wait_ns + frame_timing.upload_ns + frame_timing.encode_ns + frame_timing.submit_ns;
                frame_prepare_ns = render_ns > gpu_ns ? render_ns - gpu_ns : 0;
            }
        }
        if (presented) {
            ++presented_frames;
        }
        trace.record(++poll, presented_frames, presented, clock, renderer.drawable_extent(), snapshot, minimized, settings, audio, frontend, frontend_cues, loading_step,
            scene_vertices, session.get(), settings_changes, extras_changes, championship_commentary.get(), finale.get(), frame_timing, frame_prepare_ns);
        if (snapshot.quit_requested) {
            break;
        }
        if (presented && frontend.screen() == frontend::Screen::copyright) {
            if (!copyright_presented_ns) {
                // Count the visible loading interval from its first successful
                // presentation, including synchronous resource loading below.
                copyright_presented_ns = app_now();
            }
            if (!frontend_resources_loaded) {
                view->load_frontend();
                frontend.set_name_defaults(view->name_defaults());
                audio::load_frontend_samples(audio, assets);
                audio.load_music(2, resources / "music/track02.ogg");
                frontend_resources_loaded = true;
                // Poll once more on copyright so input queued during a slow
                // synchronous load cannot confirm the language menu.
            } else if (const auto started = app_now(); started - *copyright_presented_ns >= 3'000'000'000
                                                       || (snapshot.focused && (snapshot.any_key_pressed || (snapshot.pressed & ~action_mask(Action::pointer_primary)) != 0))) {
                audio.start_music(2, 1, started);
                next_music_check = started + music_check_interval;
                // Loading elapsed before music began. The offline sink starts
                // at the menu handoff instead of mixing that interval retroactively.
                previous_frontend_time = started;
                audio_time.reset(started);
                frontend.frontend_ready();
            }
        } else if (presented && frontend.screen() == frontend::Screen::championship_finale_loading) {
            const auto loading_assets = assets.for_loading();
            if (loading_step == 0) {
                finale = std::make_unique<FinaleSession>(loading_assets, frontend, random, surface_roughness, settings.graphics.detail, finale_initial_heading, finale_audio_view);
                loading_step = 1;
            } else if (loading_step == 1) {
                finale->view = std::make_unique<scene::ChampionshipFinaleView>(renderer, finale->resources, finale->finale_resources, frontend.language());
                finale->engine =
                    std::make_unique<audio::Engine>(audio, loading_assets, audio::EngineConfig{championship->player_car, finale->resources.setup.weather, finale_audio_view});
                for (auto& wheel_audio : finale->driving) {
                    wheel_audio = std::make_unique<audio::Driving>(audio, loading_assets, audio::EngineConfig{0, finale->resources.setup.weather, finale_audio_view});
                }
                finale->rival_audio = std::make_unique<audio::Rivals>(audio, loading_assets, finale->resources.setup.weather);
                audio.load_music(4, resources / "music/track04.ogg");
                loading_step = 2;
            } else {
                const auto started = app_now();
                // 449c60 reads Championship's CD4 entry and starts at volume100.
                // The finale caller is independent of the in-race music switch.
                audio.start_music(4, 1, started);
                previous_frontend_time = started;
                audio_time.reset(started);
                clear_device_input();
                finale->activate(started, input.snapshot().focused && !minimized, audio_time);
                frontend.championship_finale_ready();
                sync_device_input(started);
            }
        } else if (presented && frontend.screen() == frontend::Screen::scene_loading) {
            // Share archive bytes only within this synchronous loading stage.
            // Consumers own decoded resources before the temporary store dies.
            if (!run_loading_stage([&] {
                    const auto loading_assets = assets.for_loading(service_loading);
                    service_loading();
                    if (loading_step == 0) {
                        audio.set_local_players(frontend.setup().players);
                        if (frontend.setup().mode == GameMode::linkup) {
                            if (!network.session().race_view()) {
                                throw NetworkLoadInterrupted{};
                            }
                            //447950→43a5c0(1) seeds fresh scene loading; grid staging
                            //does not reseed the stream after resource setup draws.
                            random = SourceRandom(network.session().race_view()->seed);
                        }
                        session = std::make_unique<SceneSession>(loading_assets, frontend, random, surface_roughness, audio, weather_state, skid_state, damage_state,
                            settings.graphics.detail, paths.state_root, frontend.setup().mode == GameMode::championship ? &*championship : nullptr,
                            frontend.setup().mode == GameMode::linkup ? &network : nullptr);
                        loading_step = 1;
                    } else if (loading_step == 1) {
                        session->view = std::make_unique<scene::SceneView>(renderer, session->resources, &session->weather_resources, &session->surface_effect_resources,
                            &session->engine_effect_resources, session->rival_model_bindings(), nullptr, nullptr, !session->second && session->mode != GameMode::time_trial);
                        if (session->second) {
                            const std::array others{scene::RivalModel{0, &session->resources.car}};
                            session->second->view = std::make_unique<scene::SceneView>(renderer, session->resources, &session->weather_resources,
                                &session->surface_effect_resources, &session->engine_effect_resources, others, &session->second->car, &session->second->cockpit, false,
                                session->view->model_textures(), session->view->model_meshes());
                        }
                        if (session->loaded_ghost_car) {
                            session->view->set_ghost_model(renderer, *session->loaded_ghost_car);
                            session->loaded_ghost_car = nullptr;
                        }
                        int name_width = 0, name_height = 0;
                        gpu::check(SDL_GetWindowSizeInPixels(display.window(), &name_width, &name_height), "Read car name rendering width");
                        session->name_glyphs = name_width != 320;
                        const auto local_name = session->name_font.render(frontend.player_name(), session->name_glyphs);
                        session->view->set_player_name(renderer, local_name.get());
                        if (session->second) {
                            const auto second_name = session->name_font.render(frontend.player_name(1), session->name_glyphs);
                            session->second->view->set_player_name(renderer, second_name.get());
                            session->view->set_rival_name(renderer, 1, second_name.get());
                            session->second->view->set_rival_name(renderer, 0, local_name.get());
                        }
                        if (session->linkup) {
                            const auto& players = network.session().view().roster.players;
                            for (std::uint32_t actor = 0; actor < players.size(); ++actor) {
                                if (actor != session->linkup->local_actor()) {
                                    const auto name = session->name_font.render(players[actor].name, session->name_glyphs);
                                    session->view->set_rival_name(renderer, actor, name.get());
                                }
                            }
                        }
                        if (session->time_trial) {
                            session->rendered_ghost_name = session->time_trial->best_driver();
                            const auto name = session->name_font.render(session->rendered_ghost_name, session->name_glyphs);
                            session->view->set_ghost_name(renderer, name.get());
                        }
                        session->hud = std::make_unique<scene::Hud>(loading_assets, renderer, frontend.language(), settings.graphics.detail == graphics_mega_detail);
                        session->qualifying_view =
                            std::make_unique<scene::QualifyingView>(loading_assets, renderer, frontend.language(), settings.graphics.detail == graphics_mega_detail);
                        session->pause_view = std::make_unique<scene::PauseView>(loading_assets, renderer, frontend.language(), settings.graphics.detail == graphics_mega_detail);
                        session->pause_audio = std::make_unique<audio::Pause>(audio, loading_assets, session->resources.setup.weather);
                        session->game_music = settings.audio.game_music;
                        session->player_audio().load(audio, loading_assets,
                            {session->resources.setup.selection.car_id, session->resources.setup.weather, session->camera.selected_view()}, &session->wheel_audio_data);
                        if (session->linkup) {
                            session->linkup->set_scrape_handle_query([&](std::uint32_t first, std::uint32_t other) {
                                return session->scrape->pair_handle(first, other);
                            });
                        }
                        session->start_audio = std::make_unique<audio::Start>(audio, loading_assets,
                            audio::EngineConfig{session->resources.setup.selection.car_id, session->resources.setup.weather, session->camera.selected_view()});
                        session->finish_audio = std::make_unique<audio::Finish>(audio, loading_assets,
                            audio::EngineConfig{session->resources.setup.selection.car_id, session->resources.setup.weather, session->camera.selected_view()});
                        session->horn = std::make_unique<audio::Horn>(audio, loading_assets,
                            audio::EngineConfig{session->resources.setup.selection.car_id, session->resources.setup.weather, session->camera.selected_view()});
                        session->pit_audio = std::make_unique<audio::Pit>(audio, loading_assets,
                            audio::EngineConfig{session->resources.setup.selection.car_id, session->resources.setup.weather, session->camera.selected_view()});
                        session->weather_audio = std::make_unique<audio::Weather>(audio, loading_assets,
                            audio::EngineConfig{session->resources.setup.selection.car_id, session->resources.setup.weather, session->camera.selected_view()});
                        if (session->championship) {
                            session->championship_audio = std::make_unique<audio::Championship>(audio, assets, frontend.language());
                        }
                        if (session->mode != GameMode::time_trial && !session->second) {
                            session->rival_audio = std::make_unique<audio::Rivals>(audio, loading_assets, session->resources.setup.weather);
                        }
                        if (session->second) {
                            auto& player = *session->second;
                            player.hud = std::make_unique<scene::Hud>(loading_assets, renderer, frontend.language(), settings.graphics.detail == graphics_mega_detail);
                            const audio::EngineConfig config{player.car_id, session->resources.setup.weather, player.camera.selected_view()};
                            player.player_audio().load(audio, loading_assets, config);
                            player.horn = std::make_unique<audio::Horn>(audio, loading_assets, config);
                            player.pit_audio = std::make_unique<audio::Pit>(audio, loading_assets, config);
                        }
                        session->initialize_wheel_audio();
                        session->set_viewport(settings.video.width, settings.video.height, settings.graphics.split_vertical);
                        session->refresh_visibility();
                        session->update_hud();
                        // 00426ec0's Single Race CD table is shared with track resources.
                        const auto music = scene::track_content(session->resources.setup.selection.track_id).music_track;
                        audio.load_music(music, resources / ("music/track0" + std::to_string(music) + ".ogg"));
                        // Driver pipeline compilation belongs to loading, before
                        // music, simulation clocks and the first visible race frame.
                        const auto prepared = session->prepare_frame(
                            renderer, std::uint32_t(name_width), std::uint32_t(name_height), settings.graphics.split_vertical, app_now(), true, frontend.language());
                        renderer.prepare_pipelines(prepared.frame, false, service_loading);
                        if (input.snapshot().quit_requested) {
                            return;
                        }
                        if (session->linkup) {
                            (void)network.session().loaded();
                        }
                        loading_step = 2;
                    } else {
                        if (session->linkup) {
                            if (!session->sync_network()) {
                                throw NetworkLoadInterrupted{};
                            }
                            if (network.session().view().phase == network::SessionPhase::loading) {
                                network.wait_until(SDL_GetTicksNS() + 5'000'000);
                                return;
                            }
                        }
                        const auto started = app_now();
                        if (session->game_music) {
                            audio.start_music(scene::track_content(session->resources.setup.selection.track_id).music_track, 1, started);
                        }
                        previous_frontend_time = started;
                        audio_time.reset(started);
                        clear_device_input();
                        session->set_finish_audio_focus(input.snapshot().focused && !minimized);
                        session->activate(started, input.snapshot().focused && !minimized);
                        frontend.scene_ready();
                        if (session->linkup) {
                            (void)network.session().publish_race(network::Pause{!input.snapshot().focused || minimized});
                            session->activate(started, input.snapshot().focused && !minimized && !network.session().race_view()->paused);
                        }
                        sync_device_input(started);
                        if (!session->ghost_error.empty()) {
                            session->pause.open(false, settings.audio, session->mode);
                            session->activate(started, false);
                        }
                    }
                })) {
                continue;
            }
        }
        if (options.step_input && !step_input_finished) {
            const bool player_scene =
                frontend.screen() == frontend::Screen::scene && session->active && !minimized
                && (session->race.state() == 6 || session->race.state() == 7 || session->race.state() == 8 || session->race.state() == 10 || session->race.state() == 13);
            if (app_clock.stepping() && !player_scene) {
                throw std::runtime_error("Step input lost the active player scene");
            }
            if (player_scene) {
                if (step_frame_due && !presented) {
                    throw std::runtime_error("Step input requires its diagnostic presentation sample");
                }
                if (previous_step_ready && session->steps != *previous_step_ready + 1) {
                    throw std::runtime_error("Step input did not consume exactly one simulation deadline");
                }
                previous_step_ready = session->steps;
                previous_step_completed_laps = session->race.completed_laps();
                write_step_ready(std::cout, session->steps);
                const auto command = read_step_input(std::cin);
                const auto deadline = session->schedule.next_deadline_ns();
                step_keys(command.held, deadline);
                if (session->second) {
                    session->second->actions.synchronize(deadline, command.held_player2, std::nullopt);
                }
                if (command.live) {
                    step_input_finished = true;
                    app_clock.resume(SDL_GetTicksNS());
                } else {
                    app_clock.advance_to(deadline);
                }
                continue;
            }
        }
        if (frontend.screen() == frontend::Screen::scene && session->active && !minimized) {
            // Keep gameplay polling/ticks awake even when presentation is capped
            // below 25 FPS. GPU/VSync stalls are repaid by the same deadlines.
            const auto deadline = settings.video.fps_limit == 0 ? app_now() : std::min(session->schedule.next_deadline_ns(), session->presentation_due_ns);
            const auto current = app_now();
            if (deadline > current) {
                SDL_DelayNS(deadline - current);
            }
        } else if (frontend.screen() == frontend::Screen::championship_finale && finale->active && !minimized) {
            const auto current = app_now();
            const auto tick_due = finale->previous_ns + simulation::LiveSchedule::interval_ns - finale->pending_ns;
            const auto deadline = settings.video.fps_limit == 0 ? current : std::min(tick_due, finale->presentation_due_ns);
            if (deadline > current) {
                SDL_DelayNS(deadline - current);
            }
        } else if (!presented || !snapshot.focused) {
            SDL_Delay(network.session().view().phase == network::SessionPhase::idle ? 50 : 5);
        } else {
            if (network.session().view().phase != network::SessionPhase::idle && settings.video.fps_limit) {
                network.wait_until(wall_now + 1'000'000'000ull / settings.video.fps_limit);
            } else {
                display.pace(wall_now);
            }
        }
    }
    network.shutdown();
    stop_feedback();
    if (!options.screenshot.empty()) {
        if (presented_frames == 0) {
            throw std::runtime_error("No presented frame is available for the requested screenshot");
        }
        create_output_parent(options.screenshot);
        renderer.save_screenshot(options.screenshot.string());
    }
    if (session && !session->save_time_trial(paths, frontend)) {
        throw std::runtime_error(session->ghost_error);
    }
    if (championship && !sync_championship()) {
        throw std::runtime_error(std::string(frontend.championship_error()));
    }
    championship_commentary.reset();
    emit(GameEventKind::shutdown);
    const auto shutdown_time = app_now();
    input.begin_poll(shutdown_time);
    trace.record(++poll, presented_frames, false, {shutdown_time, shutdown_time - previous_time}, renderer.drawable_extent(), input.snapshot(), minimized, settings, audio,
        frontend, frontend_cues, loading_step, scene_vertices, session.get(), settings_changes, extras_changes, championship_commentary.get(), finale.get());
    return 0;
}
} // namespace

int run_application(int argc, char* argv[]) {
    try {
        std::vector<std::string_view> arguments;
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
        const auto options = parse_options(arguments);
        if (options.help) {
            std::cout << option_help();
            return 0;
        }
        return application(options);
    } catch (const std::exception& error) {
        std::cerr << "TOCA: " << error.what() << '\n';
        return 1;
    }
}
} // namespace toca
