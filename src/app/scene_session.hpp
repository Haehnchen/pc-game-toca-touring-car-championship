#pragma once

#include "app/gamepad_feedback.hpp"
#include "app/input.hpp"
#include "app/local_player_audio.hpp"
#include "app/network.hpp"
#include "app/paths.hpp"
#include "app/session_models.hpp"
#include "app/wheel_feedback.hpp"
#include "assets/store.hpp"
#include "audio/championship.hpp"
#include "audio/contact_state.hpp"
#include "audio/driving.hpp"
#include "audio/engine.hpp"
#include "audio/finish.hpp"
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
#include "frontend/car_name.hpp"
#include "frontend/controller.hpp"
#include "race/race_progress.hpp"
#include "render/gpu_renderer.hpp"
#include "scene/barriers.hpp"
#include "scene/body.hpp"
#include "scene/catalog.hpp"
#include "scene/contact_effects.hpp"
#include "scene/engine_effects.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/fleet_scene.hpp"
#include "scene/follow_camera.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/hud.hpp"
#include "scene/pause_view.hpp"
#include "scene/prop_fragments.hpp"
#include "scene/qualifying.hpp"
#include "scene/qualifying_view.hpp"
#include "scene/skid_marks.hpp"
#include "scene/surface_effects.hpp"
#include "scene/view.hpp"
#include "scene/weather.hpp"
#include "simulation/car_boost.hpp"
#include "simulation/car_contacts.hpp"
#include "simulation/cold_grid.hpp"
#include "simulation/linkup_fleet.hpp"
#include "simulation/motion.hpp"
#include "simulation/pit_lane.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/rivals.hpp"
#include "simulation/session.hpp"
#include "simulation/time_trial.hpp"

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace toca {
class AudioTimeline;

struct SecondPlayer {
    std::uint32_t car_id;
    const frontend::CarModels& car;
    const scene::CockpitResources& cockpit;
    simulation::Motion motion;
    scene::FollowCamera camera;
    scene::FinishCamera finish_camera;
    simulation::PitLane pit;
    TimedActions actions;
    InputSnapshot input;
    std::array<std::int16_t, 4> wheel_angles{};
    std::unique_ptr<scene::SceneView> view;
    scene::FleetSceneContext scene_context;
    std::unique_ptr<scene::Hud> hud;
    std::unique_ptr<audio::Engine> engine;
    std::unique_ptr<audio::Driving> driving;
    std::unique_ptr<audio::Impact> impact;
    std::unique_ptr<audio::Scrapes> scrape;
    std::unique_ptr<audio::Horn> horn;
    std::unique_ptr<audio::Pit> pit_audio;
    std::optional<scene::Weather> weather;

    bool finish_handoff = false;
    std::uint64_t pit_entries = 0, pit_exits = 0;
    std::uint64_t pit_notice_ns = 0;

    LocalPlayerAudio player_audio();

    SecondPlayer(const AssetStore& assets, const frontend::Controller& menu, const scene::SceneResources& resources, SessionModels& models, const HpcSurface& surface,
        simulation::DamageContext& damage, simulation::SurfaceRoughness& roughness, const scene::BarrierData& barriers, bool actor_camera_available);
};

struct SceneSession {
    const frontend::cheats::State cheats;
    const GameMode mode;
    race::ChampionshipState* championship;
    bool championship_dirty = false, finish_speech_started = false;
    audio::Service& audio_service;
    SourceRandom& random;
    simulation::SurfaceRoughness& roughness;
    simulation::DamagePersistentState& previous_damage;
    std::optional<scene::WeatherPersistentState>& previous_weather;
    std::optional<scene::SkidMarkPersistentState>& previous_skids;
    scene::SceneResources resources;
    SessionModels models;
    frontend::CarNameFont name_font;
    bool name_glyphs = true;
    std::string rendered_ghost_name;
    scene::FlagAnimation flags;
    scene::HorizonActors horizon_actors;
    scene::PropFragmentTables fragment_tables;
    scene::PropFragments fragments;
    scene::ContactSparks sparks;
    scene::SurfaceEffectResources surface_effect_resources;
    scene::SurfaceEffects surface_effects;
    scene::EngineEffectResources engine_effect_resources;
    scene::EngineEffects engine_effects;
    scene::FleetSceneContext scene_context;
    //446e50 publishes ed968 after emission, for the following update.
    std::array<std::uint8_t, 16> engine_visibility_masks{};
    std::size_t ground_scrape_requests = 0;
    scene::WeatherResources weather_resources;
    std::optional<scene::Weather> weather;
    scene::QualifyingData qualifying_data;
    std::optional<scene::QualifyingResults> qualifying_results;

    struct NoQualGridContext {
        simulation::ColdGridInitInputs configuration;
        simulation::RaceGrid grid;
        std::array<std::int16_t, 16> points{};
        std::array<std::uint8_t, 3> podiums{};
        std::array<std::uint8_t, 16> driver_indices{};
        std::uint32_t pole_actor = 0;
    };

    std::optional<NoQualGridContext> noqual_grid;
    std::optional<simulation::ColdGridInitInputs> qualifying_configuration;
    HpcSurface surface;
    scene::BarrierData barriers;
    scene::SkidMarks skid_marks;
    simulation::PropContacts props;
    simulation::DamageContext damage;
    simulation::Motion motion;
    simulation::Session race;
    std::unique_ptr<SecondPlayer> second;
    bool local_car_boost = false;
    std::array<simulation::CarBoostState, 2> local_boost_states{};
    simulation::CarContacts local_contacts;
    std::vector<simulation::CarContact> local_contact_events;
    bool split_vertical = false, local_finish_started = false;
    std::optional<simulation::Rivals> rivals;
    std::optional<std::array<scene::BodyPose, 16>> first_step_bodies;
    bool first_qualifying_step = false;
    NetworkRuntime* network_runtime = nullptr;
    network::Generation network_generation = 0;
    std::optional<simulation::LinkupFleet> linkup;
    std::array<bool, 8> finish_proposed{};
    bool network_results_applied = false;
    std::optional<race::RaceProgress> race_progress;
    std::array<std::array<std::int16_t, 4>, 16> rival_wheel_angles{};
    bool race_return_requested = false;
    std::optional<simulation::TimeTrial> time_trial;
    const frontend::CarModels* loaded_ghost_car = nullptr;
    std::string ghost_error;
    simulation::PitLane pit;
    bool qualifying;
    const bool qualifying_selected;
    const std::array<std::uint32_t, 2> steering_profiles;
    bool game_music = true;
    scene::PauseController pause;
    const std::uint32_t lap_limit;
    scene::FollowCamera camera;
    scene::FinishCamera finish_camera;
    std::uint32_t camera_phase = 0;
    std::array<std::int16_t, 4> wheel_angles{};
    std::unique_ptr<scene::SceneView> view;
    std::unique_ptr<scene::Hud> hud;
    std::unique_ptr<scene::QualifyingView> qualifying_view;
    std::unique_ptr<scene::PauseView> pause_view;
    std::unique_ptr<audio::Pause> pause_audio;
    std::array<gpu::Frame, 2> presentation_frames;
    gpu::Frame combined_frame, overlay_frame;
    TimedActions actions;
    simulation::LiveSchedule schedule;
    std::unique_ptr<audio::Engine> engine;
    std::unique_ptr<audio::Rivals> rival_audio;
    std::uint64_t car_contacts = 0;
    GamepadRumble gamepad_rumble;
    std::array<std::vector<WheelFeedbackCommand>, 2> wheel_feedback_commands;
    std::unique_ptr<audio::Driving> driving;
    std::optional<audio::DrivingData> wheel_audio_data;
    std::array<std::unique_ptr<audio::Driving>, 16> rival_wheel_audio;
    std::array<audio::SourceWheelQueueActor, 16> wheel_queues;
    std::uint64_t wheel_queue_updates = 0, wheel_queue_events = 0, wheel_audio_events = 0, rival_wheel_audio_events = 0;
    std::unique_ptr<audio::Impact> impact;
    std::unique_ptr<audio::Scrapes> scrape;
    std::unique_ptr<audio::Start> start_audio;
    std::unique_ptr<audio::Pit> pit_audio;
    std::unique_ptr<audio::Horn> horn;
    std::unique_ptr<audio::Weather> weather_audio;
    std::unique_ptr<audio::Finish> finish_audio;
    std::unique_ptr<audio::Championship> championship_audio;
    std::uint64_t steps = 0, simulation_ns = 0, session_epoch_ns = 0, presentation_due_ns = 0;
    std::uint64_t exhaust_plays = 0;
    std::uint32_t exhaust_sample = 0, visibility_width = 640, visibility_height = 480, second_width = 640, second_height = 480;
    std::uint64_t prop_impact_plays = 0;
    std::uint32_t prop_impact_sample = 0;
    std::uint64_t pit_entries = 0, pit_exits = 0;
    std::uint64_t pit_notice_ns = 0;
    bool started = false, active = false;

    // Presentation owns these copies; simulation, visibility/contact registration
    // and audio continue to consume the exact 40 ms states above.
    struct PresentationState {
        std::array<scene::CameraInputs, 2> cameras{};
        std::array<scene::CarPose, 2> cars{};
        std::array<std::uint64_t, 2> camera_cuts{};
        std::array<scene::RivalPose, 15> rivals{};
        std::size_t rival_count = 0;
        std::optional<scene::CarPose> ghost;
        std::uint32_t ghost_lap_ms = 0;
    };

    struct Presentation : PresentationState {
        scene::HorizonFrame horizon;
        scene::PropFrame props;
        std::vector<scene::PropFragmentTriangle> fragments;
        std::array<scene::WeatherFrame, 2> weather;
        std::array<simulation::DetachedPart, 10> parts;
        std::vector<scene::ContactSparkLine> sparks;
        scene::SurfaceEffectFrame surface_effects;
        scene::SkidMarkFrame skid_marks;
        scene::EngineEffectFrame engine_effects;
    };

    // Tick snapshots hold only poses; render-owned effect vectors retain their
    // capacity across presentation calls and warm restarts.
    PresentationState previous_presentation{}, current_presentation{};
    Presentation rendered_presentation{};
    std::uint64_t presentation_tick_ns = 0;
    bool presentation_valid = false, presentation_continuous = false;
    float presentation_alpha = 1;
    std::array<float, 3> presentation_body_position{};
    std::array<std::int32_t, 3> presentation_horizon_position{}, presentation_rotorcraft_position{};
    std::array<std::int32_t, 3> presentation_weather_position{}, presentation_surface_position{};

    SceneSession(const AssetStore& assets, const frontend::Controller& menu, SourceRandom& random_source, simulation::SurfaceRoughness& surface_roughness, audio::Service& audio,
        std::optional<scene::WeatherPersistentState>& weather_state, std::optional<scene::SkidMarkPersistentState>& skid_state, simulation::DamagePersistentState& damage_state,
        std::uint32_t detail, const std::filesystem::path& state_root, race::ChampionshipState* championship_state, NetworkRuntime* network = nullptr);

    ~SceneSession();

    scene::CameraInputs view_camera(std::uint32_t width = 0, std::uint32_t height = 0) const;

    scene::CameraInputs second_camera(std::uint32_t width = 0, std::uint32_t height = 0) const;

    void set_viewport(std::uint32_t width, std::uint32_t height, bool vertical);

    void update_camera_audio();

    std::vector<scene::RivalModel> rival_model_bindings() const;

    scene::CarLights car_lights(const simulation::Motion& body) const;

    std::span<const scene::RivalPose> rival_poses(std::span<scene::RivalPose, 15> result) const;

    scene::CarPose car_pose(std::uint32_t player) const;

    PresentationState capture_presentation() const;

    void capture_presentation_tick(std::uint64_t deadline);

    const Presentation& presentation_frame(std::uint64_t now, bool exact_tick);

    struct PreparedFrame {
        gpu::Frame& frame;
        std::size_t scene_vertices;
    };

    // Reuses session-owned frames; the returned reference is valid until the
    // next preparation or session destruction. Rendering/pacing stays with the host.
    PreparedFrame prepare_frame(gpu::Renderer& renderer, std::uint32_t width, std::uint32_t height, bool vertical, std::uint64_t now, bool exact_tick, std::uint32_t language);

    race::RacePositions race_positions() const;

    std::uint32_t actor_count() const;

    const simulation::Motion& actor_motion(std::uint32_t actor) const;

    std::uint32_t local_wheel_actor(std::uint32_t player = 0) const;

    std::uint8_t car_effect_visibility(std::uint32_t actor, std::uint8_t eligibility) const;
    std::uint8_t wheel_effect_visibility(std::uint32_t actor) const;
    std::uint8_t engine_effect_visibility(std::uint32_t actor) const;

    void initialize_wheel_audio(bool reset_totals = true);

    void prepare_wheel_audio();

    void update_wheel_contacts();

    void update_wheel_audio();

    void update_exhaust_audio();

    void update_engine_audio();

    void update_contact_audio(std::span<const scene::PropSoundEvent> prop_sounds);

    std::uint32_t rival_skid_voices() const;

    void initialize_rivals(const simulation::ColdGridInitResult* startup = nullptr, const simulation::RaceGrid* retained_grid = nullptr);

    void initialize_qualifying_rivals(const simulation::QualifyingStartInitResult& startup);

    void refresh_visibility();

    void prepare_driver();

    void initialize_two_player_progress();

    LocalPlayerAudio player_audio();

    bool finishing() const;

    race::RaceResults race_results() const;

    bool sync_network();

    void publish_network();

    std::optional<scene::CarPose> ghost_pose() const;

    bool save_time_trial(const ApplicationPaths& paths, frontend::Controller& menu);

    void stop_player_audio(std::uint32_t player);

    void stop_driving_audio();

    void begin_qualifying_finish();

    void begin_race(std::uint64_t deadline, const simulation::ColdGridInitResult* warm_grid = nullptr);

    bool can_restart_warm() const;

    void reset_grid_world();

    void restart_qualifying(const AssetStore& assets, std::uint64_t now);

    void restart_two_players(std::uint64_t now);

    void restart_grid_audio(std::uint64_t now);

    void restart_warm(const AssetStore& assets, std::uint64_t now, bool focused);

    audio::EngineInputs engine_inputs(std::uint32_t player = 0) const;

    audio::StartAudioInputs audio_inputs() const;

    void update_hud();

    audio::SpatialListener audio_listener(std::uint32_t player = 0) const;

    audio::DrivingInputs driving_inputs(std::uint32_t player = 0) const;

    void set_finish_audio_focus(bool focused);

    void activate(std::uint64_t now, bool focused);

    // The observer runs synchronously after each complete tick. The bridge keeps
    // the application callback allocation-free while compiling the tick once.
    template <class Record> void advance(std::uint64_t now, AudioTimeline& audio, Record record) {
        advance_recorded(now, audio, std::addressof(record), [](void* context, std::uint64_t deadline, const InputSnapshot& input) {
            (*static_cast<Record*>(context))(deadline, input);
        });
    }

private:
    void advance_recorded(std::uint64_t now, AudioTimeline& audio, void* context, void (*record)(void*, std::uint64_t, const InputSnapshot&));
};
} // namespace toca
