#include "app/scene_session.hpp"

#include "app/runtime_resources.hpp"
#include "race/linkup_results.hpp"
#include "settings/lap_records.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/race_world.hpp"

#include <algorithm>
#include <iostream>
#include <type_traits>

namespace toca {
LocalPlayerAudio SecondPlayer::player_audio() {
    return {engine, driving, impact, scrape};
}

SecondPlayer::SecondPlayer(const AssetStore& assets, const frontend::Controller& menu, const scene::SceneResources& resources, SessionModels& models, const HpcSurface& surface,
    simulation::DamageContext& damage, simulation::SurfaceRoughness& roughness, const scene::BarrierData& barriers, bool actor_camera_available)
    : car_id(menu.stored_car_id(1)), car(models.car(assets, car_id)), cockpit(models.cockpit(assets, car_id)),
      motion(surface, scene::initial_body_pose(surface, scene::race_grid_start(resources.grid, 1), car_id), car_id, menu.transmission_manual(1), resources.setup.weather, false,
          damage, roughness, &barriers, frontend::cheats::active(menu.cheats_state(), frontend::cheats::Code::low_gravity)),
      camera(resources.grid[1].heading, frontend::cheats::active(menu.cheats_state(), frontend::cheats::Code::micro), car_id,
          frontend::cheats::active(menu.cheats_state(), frontend::cheats::Code::follow), resources.setup.selection.detail,
          frontend::cheats::active(menu.cheats_state(), frontend::cheats::Code::copter), actor_camera_available),
      finish_camera(resources.cameras), pit(resources.pits, resources.setup.selection.track_id) {
    motion.set_start_hold(true);
    motion.set_actor_identity(1, true);
    motion.set_damage_feedback_enabled(true);
    motion.set_model_detail(std::uint8_t(scene::local_car_model_index(resources.setup.selection.detail)));
}

SceneSession::SceneSession(const AssetStore& assets, const frontend::Controller& menu, SourceRandom& random_source, simulation::SurfaceRoughness& surface_roughness,
    audio::Service& audio, std::optional<scene::WeatherPersistentState>& weather_state, std::optional<scene::SkidMarkPersistentState>& skid_state,
    simulation::DamagePersistentState& damage_state, std::uint32_t detail, const std::filesystem::path& state_root, race::ChampionshipState* championship_state,
    NetworkRuntime* network)
    : cheats(scene_cheats(menu)), mode(menu.setup().mode), championship(championship_state), audio_service(audio), random(random_source), roughness(surface_roughness),
      previous_damage(damage_state), previous_weather(weather_state), previous_skids(skid_state),
      resources(scene::load_scene_resources(assets,
          scene::resolve_setup({menu.stored_track_id(), menu.stored_car_id(), menu.setup().weather, detail, frontend::cheats::active(cheats, frontend::cheats::Code::stars),
                                   frontend::cheats::active(cheats, frontend::cheats::Code::toon), frontend::cheats::active(cheats, frontend::cheats::Code::rain_up),
                                   frontend::cheats::active(cheats, frontend::cheats::Code::cat_dog), championship ? std::optional(championship->meeting) : std::nullopt},
              random),
          mega_model_directory(detail))),
      models(resources.car, resources.cockpit, mega_model_directory(detail)), name_font(assets, detail == graphics_mega_detail), flags(resources.flags, random),
      horizon_actors(resources.moving_horizon), fragment_tables(scene::make_prop_fragment_tables(random)),
      fragments(fragment_tables, resources.setup.weather, frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity)), sparks(fragment_tables),
      surface_effect_resources(scene::load_surface_effect_resources(assets, resources.setup)),
      surface_effects(fragment_tables, resources.setup, frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity)),
      engine_effect_resources(scene::load_engine_effect_resources(assets)), engine_effects(fragment_tables, mode), weather_resources(scene::load_weather_resources(assets)),
      qualifying_data(scene::load_qualifying_data(assets, resources.setup, menu.setup().difficulty, menu.player_name(),
          frontend::cheats::active(cheats, frontend::cheats::Code::mayhem), frontend::cheats::active(cheats, frontend::cheats::Code::impossible),
          championship ? std::optional(championship->meeting * 2 + championship->race) : std::nullopt, championship && championship->showdown)),
      surface(resources.collision_hpc), barriers(resources.route_points, resources.ground.sector_centers), skid_marks(
                                                                                                               resources.setup,
                                                                                                               [this](const scene::SkidMarkVector& point) {
                                                                                                                   return std::uint16_t(barriers.sector(point).value_or(0xffff));
                                                                                                               },
                                                                                                               previous_skids),
      props(resources.cones, resources.posts, resources.signs, resources.ground.sector_centers, surface, frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity)),
      damage(surface, &barriers, frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity), previous_damage),
      motion(surface,
          scene::initial_body_pose(surface,
              menu.setup().qualifying || mode == GameMode::time_trial ? scene::single_qualifying_start(resources.route_points)
                                                                      : scene::race_grid_start(resources.grid, menu.setup().players == 2 ? 0 : 15),
              menu.stored_car_id()),
          menu.stored_car_id(), menu.transmission_manual(), resources.setup.weather, menu.setup().qualifying || mode == GameMode::time_trial, damage, roughness, &barriers,
          frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity)),
      race(resources.route_points, menu.setup().qualifying, mode, mode != GameMode::time_trial ? menu.setup().laps : 0), pit(resources.pits, resources.setup.selection.track_id),
      qualifying(mode != GameMode::time_trial && menu.setup().qualifying), qualifying_selected(menu.setup().qualifying && menu.setup().players == 1),
      steering_profiles{menu.settings_values().input.sensitivity, menu.settings_values().input_player2.sensitivity},
      lap_limit(mode == GameMode::time_trial ? 0 : menu.setup().laps),
      camera(menu.setup().qualifying || mode == GameMode::time_trial ? scene::qualifying_camera_heading(menu.stored_track_id())
                                                                     : resources.grid.at(menu.setup().players == 2 ? 0 : 15).heading,
          frontend::cheats::active(cheats, frontend::cheats::Code::micro), menu.stored_car_id(), frontend::cheats::active(cheats, frontend::cheats::Code::follow), detail,
          frontend::cheats::active(cheats, frontend::cheats::Code::copter), horizon_actors.frame().camera_zero.has_value()),
      finish_camera(resources.cameras) {
    engine_visibility_masks.fill(1); //4469a0, before the setup4608b0 call.
    simulation::reset_grid_world(local_contacts, damage, props);
    network_runtime = network;
    if (network_runtime) {
        const auto& lobby = network_runtime->session().view();
        const auto* shared = network_runtime->session().race_view();
        if (!shared) {
            throw std::runtime_error("Linkup loading lost its shared race");
        }
        network_generation = shared->generation;
        simulation::LinkupRoster roster;
        roster.humans = std::uint32_t(lobby.roster.players.size());
        roster.hosting = lobby.hosting;
        roster.grid = shared->grid;
        for (const auto& actor : shared->actors) {
            if (actor.player == lobby.local_player) {
                roster.local_actor = actor.actor;
            }
            if (actor.actor < roster.humans) {
                const auto& player = lobby.roster.players[actor.actor];
                roster.drivers.push_back({player.car, player.sensitivity, player.manual});
                qualifying_data.drivers[actor.actor].name = player.name;
                qualifying_data.drivers[actor.actor].car_id = player.car;
            } else {
                roster.drivers.push_back({actor.actor, 2, false});
                qualifying_data.drivers[actor.actor].car_id = actor.actor;
            }
        }
        qualifying_data.local_actor = roster.local_actor;
        const auto rank = std::find(roster.grid.begin(), roster.grid.end(), roster.local_actor) - roster.grid.begin();
        const auto placement = scene::race_grid_start(resources.grid, std::uint32_t(rank));
        motion.reset_for_race(scene::initial_body_pose(surface, placement, resources.setup.selection.car_id));
        motion.set_steering_profile(roster.drivers[roster.local_actor].sensitivity);
        camera = scene::FollowCamera(placement.heading, frontend::cheats::active(cheats, frontend::cheats::Code::micro), resources.setup.selection.car_id, false, detail, false,
            horizon_actors.frame().camera_zero.has_value());
        linkup.emplace(
            surface, resources.grid, resources.route_points, qualifying_data, resources.setup.weather, random, roster, damage, roughness, &barriers, menu.setup().car_boost);
        std::array<std::uint32_t, 16> grid{};
        std::copy(roster.grid.begin(), roster.grid.end(), grid.begin());
        race_progress.emplace(resources.route_points, lap_limit, roster.local_actor, grid, linkup->count(), GameMode::linkup);
        race.set_network_finish_authority(true);
        race.set_finish_group_complete(false);
        for (std::uint32_t actor = 0; actor < linkup->count(); ++actor) {
            if (actor != linkup->local_actor()) {
                race_progress->participant(actor).set_network_finish_authority(network_runtime->session().owns_actor(std::uint8_t(actor)));
                race_progress->participant(actor).set_finish_group_complete(false);
                const auto car = linkup->car_id(actor);
                (void)models.car(assets, car);
            }
        }
    }
    if (!menu.setup().qualifying && mode != GameMode::time_trial) {
        motion.set_start_hold(true);
    }
    if (mode == GameMode::time_trial) {
        time_trial.emplace(menu.stored_track_id(), menu.stored_car_id(), resources.setup.weather, std::string(menu.player_name()));
        try {
            if (time_trial->load(state_root, menu.setup().ghost_enabled) && menu.setup().ghost_enabled && time_trial->best_car_id() != menu.stored_car_id()) {
                loaded_ghost_car = &models.car(assets, time_trial->best_car_id());
            }
        } catch (const std::exception& failure) {
            ghost_error = "Ghost could not be loaded. Check the save file.";
            std::cerr << "Ghost: " << failure.what() << '\n';
            time_trial.emplace(menu.stored_track_id(), menu.stored_car_id(), resources.setup.weather, std::string(menu.player_name()));
        }
    }
    if (championship && !qualifying) {
        qualifying_results.emplace();
        qualifying_results->local_actor = championship->local_actor;
        for (std::uint32_t rank = 0; rank < 16; ++rank) {
            const auto actor = championship->grid[rank];
            qualifying_results->rows[rank] = championship->drivers[actor];
            if (actor == championship->local_actor) {
                qualifying_results->local_rank = rank;
            }
        }
        const auto placement = scene::race_grid_start(resources.grid, qualifying_results->local_rank);
        motion.reset_for_race(scene::initial_body_pose(surface, placement, resources.setup.selection.car_id));
        camera = scene::FollowCamera(placement.heading, frontend::cheats::active(cheats, frontend::cheats::Code::micro), resources.setup.selection.car_id,
            frontend::cheats::active(cheats, frontend::cheats::Code::follow), detail, frontend::cheats::active(cheats, frontend::cheats::Code::copter),
            horizon_actors.frame().camera_zero.has_value());
    }
    if (menu.setup().players == 2) {
        //44f0a3 uses only the two local actors and forces NoQual.
        local_car_boost = menu.setup().car_boost;
        qualifying_data.local_actor = 0;
        qualifying_data.drivers[0].name = menu.player_name();
        qualifying_data.drivers[0].car_id = menu.stored_car_id();
        qualifying_data.drivers[1].name = menu.player_name(1);
        qualifying_data.drivers[1].car_id = menu.stored_car_id(1);
        second = std::make_unique<SecondPlayer>(assets, menu, resources, models, surface, damage, roughness, barriers, horizon_actors.frame().camera_zero.has_value());
        second->motion.prepare_ai(simulation::AiDriver(resources.route_points, qualifying_data.drivers[1].raw, qualifying_data.distributions,
            qualifying_data.weather_speed_penalty_q16, qualifying_data.tire_offset_q16, random, qualifying_data.point_speed_boost));
        initialize_two_player_progress();
    }
    std::optional<simulation::ColdGridInitResult> startup;
    std::optional<simulation::QualifyingStartInitResult> qualifying_startup;
    if (mode == GameMode::single_race && !second && !linkup && !championship && resources.setup.selection.car_id < 8
        && std::ranges::none_of(
            cheats.flags,
            [](bool enabled) {
                return enabled;
            })) {
        simulation::ColdGridInitInputs entry;
        entry.setup = resources.setup;
        entry.difficulty = menu.setup().difficulty;
        entry.steering_profile = menu.settings_values().input.sensitivity;
        entry.manual = menu.transmission_manual();
        entry.player_name = menu.player_name();
        entry.seed = random.state();
        if (qualifying) {
            //Loading and the first State4 wrapper both run443f60.
            qualifying_configuration = entry;
            qualifying_startup.emplace(simulation::initialize_qualifying_start(assets, entry));
            startup.emplace(qualifying_startup->repeated);
            first_qualifying_step = true;
        } else {
            //Loading and first State9 entry each classify the grid.
            startup.emplace(simulation::initialize_start_grid(assets, entry).repeated);
            noqual_grid.emplace();
            noqual_grid->configuration = entry;
            noqual_grid->grid = startup->grid;
            noqual_grid->points = startup->points;
            noqual_grid->pole_actor = startup->pole_actor;
            for (std::size_t actor = 0; actor < startup->actors.size(); ++actor) {
                noqual_grid->driver_indices[actor] = startup->actors[actor].driver_index;
            }
        }
        qualifying_data = startup->data;
        random = SourceRandom(startup->rng_final);
        first_step_bodies.emplace();
        for (std::size_t actor = 0; actor < first_step_bodies->size(); ++actor) {
            (*first_step_bodies)[actor] = startup->actors[actor].body;
        }
        //Keep the pre-deadline preview grounded. The generated physical
        //poses enter at the first regular tick, before progression/AI;
        //its ground pass completes before the next presentation capture.
        if (qualifying) {
            motion.reset_for_qualifying(scene::initial_body_pose(surface, scene::single_qualifying_start(resources.route_points), resources.setup.selection.car_id));
        } else {
            motion.reset_for_race(simulation::cold_grid_body(surface, resources.grid, startup->grid, qualifying_data.local_actor, resources.setup.selection.car_id));
        }
        motion.set_steering_profile(entry.steering_profile);
        motion.prepare_ai(
            simulation::make_race_grid_driver(simulation::initialize_ai_route_heights(resources.route_points, surface), qualifying_data, qualifying_data.local_actor, random));
    } else {
        prepare_driver();
    }
    race.configure_route_recovery(qualifying_data.drivers.at(qualifying_data.local_actor).raw[19]);
    if (mode != GameMode::time_trial && !second && !linkup) {
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            if (actor == qualifying_data.local_actor) {
                continue;
            }
            const auto car_id = scene::rival_model_id(actor, qualifying_data.drivers[actor].car_id);
            (void)models.car(assets, car_id);
        }
        if (qualifying_startup) {
            initialize_qualifying_rivals(*qualifying_startup);
        } else if (!qualifying) {
            initialize_rivals(startup ? &*startup : nullptr);
        }
    }
    if (race.qualifying_rollout()) {
        // 443f60 assigns the qualifying approach to 402be0's AI controls.
        motion.handoff_to_ai(false);
        if (startup) {
            motion.restore_generated_grid_controls(startup->actors[qualifying_data.local_actor].snapshot);
        }
        // 45eb60 initializes the map angle and applies the first State4
        // follow step before presenting the initial approach frame.
        camera.update(motion.state().body, true);
    }
    weather.emplace(fragment_tables, resources.setup, random, scene::weather_camera(view_camera().world_to_view),
        frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity), previous_weather);
    if (second) {
        second->weather.emplace(
            fragment_tables, resources.setup, random, scene::weather_camera(second_camera().world_to_view), frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity));
    }
    // 4469a0 installs initial view masks;44ffe1 then invokes4608b0 once.
    const std::array initial_effects{motion.engine_effect_vehicle()};
    engine_effects.emit(initial_effects);
    motion.set_actor_identity(qualifying_data.local_actor, true);
    // Keep feedback generation independent of hardware and its hotplug state.
    motion.set_damage_feedback_enabled(true);
    motion.set_model_detail(std::uint8_t(scene::local_car_model_index(detail)));
    damage.set_visibility([this](std::uint32_t sector) {
        return sector <= 0xffff
               && (!view || scene_context.visibility.sector_visible(std::uint16_t(sector))
                   || (second && second->view && second->scene_context.visibility.sector_visible(std::uint16_t(sector))));
    });
}

SceneSession::~SceneSession() {
    previous_damage = damage.persistent_state();
    previous_skids = skid_marks.persistent_state();
    if (weather) {
        previous_weather = weather->persistent_state();
    }
}

race::RacePositions SceneSession::race_positions() const {
    if (linkup) {
        return linkup->positions(motion.state().body);
    }
    if (rivals) {
        return rivals->positions(motion.state().body);
    }
    race::RacePositions result{};
    result[0] = motion.state().body.position;
    result[1] = second->motion.state().body.position;
    return result;
}

void SceneSession::initialize_rivals(const simulation::ColdGridInitResult* startup, const simulation::RaceGrid* retained_grid) {
    const auto grid = startup         ? startup->grid
                      : retained_grid ? *retained_grid
                                      : simulation::make_race_grid(qualifying_data, random, qualifying_results ? &*qualifying_results : nullptr);
    if (startup) {
        rivals.emplace(surface, resources.grid, resources.route_points, *startup, resources.setup.weather, random, damage, roughness, &barriers);
    } else {
        rivals.emplace(surface, resources.grid, resources.route_points, qualifying_data, resources.setup.weather, random, grid, damage, roughness, &barriers,
            frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity), qualifying_data.point_speed_boost);
    }
    rivals->set_car_contacts_enabled(!frontend::cheats::active(cheats, frontend::cheats::Code::no_hits));
    rivals->set_scrape_handle_query([this](std::uint32_t first, std::uint32_t second) {
        return scrape && scrape->pair_handle(first, second);
    });
    race_progress.emplace(resources.route_points, lap_limit, qualifying_data.local_actor, grid.rank_to_actor);
    rival_wheel_angles = {};
}

void SceneSession::initialize_qualifying_rivals(const simulation::QualifyingStartInitResult& startup) {
    rivals.emplace(surface, resources.grid, resources.route_points, startup, resources.setup.weather, random, damage, roughness, &barriers,
        frontend::cheats::active(cheats, frontend::cheats::Code::low_gravity));
    rivals->set_car_contacts_enabled(!frontend::cheats::active(cheats, frontend::cheats::Code::no_hits));
    rivals->set_scrape_handle_query([this](std::uint32_t first, std::uint32_t second) {
        return scrape && scrape->pair_handle(first, second);
    });
    race_progress.emplace(resources.route_points, lap_limit, startup);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        if (actor != qualifying_data.local_actor) {
            race_progress->participant(actor).configure_route_recovery(qualifying_data.drivers[actor].raw[19]);
        }
    }
    rival_wheel_angles = {};
}

void SceneSession::prepare_driver() {
    motion.prepare_ai(simulation::AiDriver(resources.route_points, qualifying_data.drivers.at(qualifying_data.local_actor).raw, qualifying_data.distributions,
        qualifying_data.weather_speed_penalty_q16, qualifying_data.tire_offset_q16, random, qualifying_data.point_speed_boost));
}

void SceneSession::initialize_two_player_progress() {
    std::array<std::uint32_t, 16> grid{};
    grid[1] = 1;
    race_progress.emplace(resources.route_points, lap_limit, 0, grid, 2);
    race.configure_local_players(2);
    race.set_finish_group_complete(false);
    race_progress->participant(1).set_finish_group_complete(false);
    race_progress->participant(1).configure_route_recovery(qualifying_data.drivers[1].raw[19]);
}

bool SceneSession::finishing() const {
    if (linkup) {
        const auto* shared = network_runtime->session().race_view();
        return shared && shared->phase == network::RacePhase::results;
    }
    if (second) {
        return race_progress->classified();
    }
    return race.state() == 7 || race.state() == 8 || race.state() == 10 || race.state() == 13;
}

race::RaceResults SceneSession::race_results() const {
    const auto classification = race_progress->results(qualifying_data);
    if (!linkup) {
        return classification;
    }
    std::array<bool, 8> active{};
    for (const auto& actor : network_runtime->session().race_view()->actors) {
        active[actor.actor] = actor.active_human;
    }
    return race::linkup_results(classification, active);
}

bool SceneSession::sync_network() {
    if (!linkup) {
        return true;
    }
    network_runtime->pump();
    auto& session = network_runtime->session();
    const auto* shared = session.race_view();
    if (!shared || shared->generation != network_generation) {
        return false;
    }
    // Install reliable ownership changes before consuming cross-channel
    // state. A new owner's early state is already discarded by RaceState.
    while (auto event = session.take_race_event()) {
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, network::ActorStatus>) {
                    if (!value.active_human && value.actor != linkup->local_actor()) {
                        linkup->set_active_human(value.actor, false, motion);
                        race_progress->participant(value.actor).set_network_finish_authority(session.owns_actor(value.actor));
                    }
                } else if constexpr (std::is_same_v<T, network::Gear>) {
                    if (!linkup->owns(value.actor)) {
                        linkup->apply_gear(value.actor, value.gear, value.reverse, motion);
                    }
                } else if constexpr (std::is_same_v<T, network::DamageVisual>) {
                    if (!linkup->owns(value.actor)) {
                        linkup->apply_damage(value.actor, value.variants, motion);
                    }
                } else if constexpr (std::is_same_v<T, network::Finish>) {
                    race_progress->apply_network_finish(value.actor, value.time_centiseconds, value.ordinal, race);
                }
            },
            *event);
    }
    for (const auto& input : session.take_race_inputs()) {
        if (!linkup->owns(input.actor) && linkup->human(input.actor)) {
            linkup->apply_input(input.actor, input.controls, motion);
        }
    }
    for (const auto& state : session.take_race_states()) {
        if (!linkup->owns(state.actor)) {
            linkup->apply_state(state.actor, state.state, motion);
        }
    }
    if (shared->phase == network::RacePhase::results && !network_results_applied) {
        race_progress->finish_network_classification(race);
        race.set_finish_group_complete(true);
        network_results_applied = true;
        if (finish_audio) {
            audio_service.stop_music();
            stop_driving_audio();
            finish_audio->start();
        }
    }
    return true;
}

void SceneSession::publish_network() {
    auto& session = network_runtime->session();
    const auto* shared = session.race_view();
    if (!shared || shared->phase == network::RacePhase::loading || shared->phase == network::RacePhase::results) {
        return;
    }
    for (std::uint32_t actor = 0; actor < linkup->count(); ++actor) {
        const auto id = std::uint8_t(actor);
        if (!session.owns_actor(id)) {
            continue;
        }
        const auto& body = linkup->motion(actor, motion);
        if (actor == linkup->local_actor()) {
            (void)session.publish_controls_changed(id, body.network_controls());
        }
        (void)session.publish_state_due(id, body.network_state(), SDL_GetTicks());
        const auto& progress = actor == linkup->local_actor() ? race : race_progress->participant(actor);
        if (!finish_proposed[actor] && progress.completed_laps() >= lap_limit) {
            finish_proposed[actor] = session.publish_race(network::Finish{id, progress.race_time_ms() / 10, 0});
        }
    }
    for (const auto& event : linkup->events()) {
        if (event.kind == simulation::LinkupActorEvent::Kind::gear) {
            (void)session.publish_race(network::Gear{event.actor, event.gear, event.reverse});
        } else {
            (void)session.publish_race(network::DamageVisual{event.actor, event.variants});
        }
    }
}

bool SceneSession::save_time_trial(const ApplicationPaths& paths, frontend::Controller& menu) {
    if (!time_trial) {
        return true;
    }
    try {
        require_writable_outside_data(paths.state_root / "time-trial", paths.data_root, paths.bundled_data_root);
        time_trial->save(paths.state_root);
        auto records = menu.records().value();
        const auto track = resources.setup.selection.track_id;
        const bool improved = improve_lap_record(
            records, track, menu.player_name(), resources.setup.selection.car_id, race.best_lap_ms(), frontend::cheats::active(cheats, frontend::cheats::Code::boost));
        if (improved) {
            const auto path = paths.state_root / "time-trial" / "records.dat";
            require_writable_outside_data(path, paths.data_root, paths.bundled_data_root);
            save_lap_records(path, records);
        }
        menu.set_lap_records(std::move(records), improved ? std::optional(track) : std::nullopt);
        ghost_error.clear();
        return true;
    } catch (const std::exception& failure) {
        ghost_error = "Time Trial could not be saved. Check free space and try again.";
        std::cerr << "Time Trial: " << failure.what() << '\n';
        return false;
    }
}

void SceneSession::begin_qualifying_finish() {
    qualifying_results = scene::classify_qualifying(qualifying_data, race.best_lap_ms(), random);
    if (championship) {
        race::championship_apply_qualifying(*championship, *qualifying_results);
        championship_dirty = true;
    }
    audio_service.stop_music();
    stop_driving_audio();
    finish_audio->start();
}

void SceneSession::begin_race(std::uint64_t deadline, const simulation::ColdGridInitResult* warm_grid) {
    presentation_valid = false;
    const auto placement = scene::race_grid_start(resources.grid, warm_grid ? warm_grid->grid.rank(qualifying_data.local_actor) : qualifying_results->local_rank);
    motion.reset_for_race(scene::initial_body_pose(surface, placement, resources.setup.selection.car_id));
    if (warm_grid) {
        motion.set_steering_profile(noqual_grid->configuration.steering_profile);
        motion.prepare_ai(
            simulation::make_race_grid_driver(simulation::initialize_ai_route_heights(resources.route_points, surface), qualifying_data, qualifying_data.local_actor, random));
        motion.restore_generated_grid_controls(warm_grid->actors[qualifying_data.local_actor].snapshot);
        first_step_bodies.emplace();
        for (std::size_t actor = 0; actor < first_step_bodies->size(); ++actor) {
            (*first_step_bodies)[actor] = warm_grid->actors[actor].body;
        }
    } else {
        prepare_driver();
    }
    reset_grid_world();
    const std::array initial_effects{motion.engine_effect_vehicle()};
    engine_effects.emit(initial_effects);
    race = simulation::Session(resources.route_points, false, mode, lap_limit);
    race.configure_route_recovery(qualifying_data.drivers.at(qualifying_data.local_actor).raw[19]);
    initialize_rivals(warm_grid);
    if (warm_grid) {
        rivals->restore_generated_grid_controls(*warm_grid);
    }
    initialize_wheel_audio(!warm_grid);
    // Seed the new grid standings before its first physical/traffic update.
    race_progress->update(0, race, rivals->positions(motion.state().body));
    if (!warm_grid) {
        view->reset_contact_registration();
        refresh_visibility();
    }
    qualifying = false;
    session_epoch_ns = simulation_ns + simulation::LiveSchedule::interval_ns;
    //460170 reseeds the retained camera after qualifying or a warm grid.
    camera.initialize_heading(placement.heading);
    if (warm_grid) {
        finish_camera.reset_session();
    }
    camera_phase = 0;
    if (warm_grid) {
        view->reset_contact_registration();
        refresh_visibility();
    }
    update_camera_audio();
    wheel_angles.fill(0);
    actions.reset(deadline, active);
    finish_audio->fade();
    player_audio().start_engine_and_driving();
    for (auto& channel : rival_wheel_audio) {
        if (channel) {
            channel->start(false);
        }
    }
    player_audio().start_contacts();
    start_audio->start();
    if (game_music) {
        audio_service.start_music(scene::track_content(resources.setup.selection.track_id).music_track, 1, deadline);
    }
}

bool SceneSession::can_restart_warm() const {
    return mode == GameMode::single_race && !championship && !linkup && resources.setup.selection.car_id < 8 && (!second || second->car_id < 8)
           && std::ranges::none_of(
               cheats.flags,
               [](bool enabled) {
                   return enabled;
               })
           && (noqual_grid || qualifying_selected || second);
}

void SceneSession::reset_grid_world() {
    pit.reset();
    pit_audio->stop();
    simulation::reset_grid_world(local_contacts, damage, props);
    scrape->reset_pairs();
    fragments.clear();
    sparks.clear();
    surface_effects.reset();
    skid_marks.reset();
    engine_effects.reset();
    engine_visibility_masks.fill(1);
    ground_scrape_requests = 0;
}

void SceneSession::restart_qualifying(const AssetStore& assets, std::uint64_t now) {
    qualifying = true;
    rivals.reset();
    race_progress.reset();
    motion.reset_for_qualifying(scene::initial_body_pose(surface, scene::single_qualifying_start(resources.route_points), resources.setup.selection.car_id));
    motion.set_steering_profile(steering_profiles[0]);
    prepare_driver();
    motion.handoff_to_ai(false);
    race = simulation::Session(resources.route_points, true, mode, lap_limit);
    race.configure_route_recovery(qualifying_data.drivers.at(qualifying_data.local_actor).raw[19]);
    if (qualifying_configuration) {
        auto entry = *qualifying_configuration;
        entry.seed = random.state();
        const auto startup = simulation::initialize_qualifying_start(assets, entry);
        qualifying_data = startup.repeated.data;
        random = SourceRandom(startup.repeated.rng_final);
        motion.restore_generated_grid_controls(startup.repeated.actors[qualifying_data.local_actor].snapshot);
        initialize_qualifying_rivals(startup);
        first_step_bodies.emplace();
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            (*first_step_bodies)[actor] = startup.repeated.actors[actor].body;
        }
        first_qualifying_step = true;
    }
    reset_grid_world();
    initialize_wheel_audio(false);
    camera.initialize_heading(scene::qualifying_camera_heading(resources.setup.selection.track_id));
    camera.update(motion.state().body, true); //Grounded preview; first State4 render reseeds below.
    camera_phase = 0;
    finish_camera.reset_session();
    view->reset_contact_registration();
    refresh_visibility();
    wheel_angles.fill(0);
    const std::array initial_effects{motion.engine_effect_vehicle()};
    engine_effects.emit(initial_effects);
    restart_grid_audio(now);
}

void SceneSession::restart_two_players(std::uint64_t now) {
    qualifying = false;
    //The native local grid keeps its authored slots. The source's cached
    //caller-clock permutation remains outside this fixed-deadline policy.
    motion.reset_for_race(scene::initial_body_pose(surface, scene::race_grid_start(resources.grid, 0), resources.setup.selection.car_id));
    motion.set_steering_profile(steering_profiles[0]);
    prepare_driver();
    second->motion.reset_for_race(scene::initial_body_pose(surface, scene::race_grid_start(resources.grid, 1), second->car_id));
    second->motion.set_steering_profile(steering_profiles[1]);
    second->motion.prepare_ai(simulation::AiDriver(resources.route_points, qualifying_data.drivers[1].raw, qualifying_data.distributions, qualifying_data.weather_speed_penalty_q16,
        qualifying_data.tire_offset_q16, random, qualifying_data.point_speed_boost));
    second->finish_handoff = false;
    second->pit.reset();
    second->pit_audio->stop();
    second->pit_entries = second->pit_exits = second->pit_notice_ns = 0;
    second->input = {};
    second->wheel_angles = {};
    local_boost_states = {};
    race = simulation::Session(resources.route_points, false, mode, lap_limit);
    race.configure_route_recovery(qualifying_data.drivers[0].raw[19]);
    initialize_two_player_progress();
    reset_grid_world();
    second->scrape->reset_pairs();
    initialize_wheel_audio(false);
    camera = scene::FollowCamera(resources.grid[0].heading, frontend::cheats::active(cheats, frontend::cheats::Code::micro), resources.setup.selection.car_id,
        frontend::cheats::active(cheats, frontend::cheats::Code::follow), resources.setup.selection.detail, frontend::cheats::active(cheats, frontend::cheats::Code::copter),
        horizon_actors.frame().camera_zero.has_value());
    second->camera = scene::FollowCamera(resources.grid[1].heading, frontend::cheats::active(cheats, frontend::cheats::Code::micro), second->car_id,
        frontend::cheats::active(cheats, frontend::cheats::Code::follow), resources.setup.selection.detail, frontend::cheats::active(cheats, frontend::cheats::Code::copter),
        horizon_actors.frame().camera_zero.has_value());
    finish_camera = scene::FinishCamera(resources.cameras);
    second->finish_camera = scene::FinishCamera(resources.cameras);
    view->reset_contact_registration();
    second->view->reset_contact_registration();
    refresh_visibility();
    wheel_angles.fill(0);
    const std::array initial_effects{motion.engine_effect_vehicle()};
    engine_effects.emit(initial_effects);
    restart_grid_audio(now);
    second->player_audio().start();
}

void SceneSession::restart_warm(const AssetStore& assets, std::uint64_t now, bool focused) {
    stop_driving_audio();
    qualifying_results.reset();
    first_step_bodies.reset();
    first_qualifying_step = false;
    race_return_requested = local_finish_started = finish_speech_started = false;
    pit_entries = pit_exits = pit_notice_ns = 0;
    local_contact_events.clear();
    presentation_continuous = false;
    previous_presentation = {};
    current_presentation = {};
    pause.close();
    started = true;
    active = focused;
    if (second) {
        restart_two_players(now);
    } else if (qualifying_selected) {
        restart_qualifying(assets, now);
    } else {
        simulation::WarmGridInitInputs input;
        input.configuration = noqual_grid->configuration;
        input.data = qualifying_data;
        input.points = noqual_grid->points;
        for (std::size_t actor = 0; actor < input.actors.size(); ++actor) {
            auto& snapshot = input.actors[actor];
            actor_motion(std::uint32_t(actor)).publish_warm_grid_context(snapshot);
            snapshot.steering_profile = input.configuration.steering_profile;
            snapshot.gameplay[0x8d] = noqual_grid->driver_indices[actor];
        }
        const auto grid = simulation::initialize_warm_grid(assets, input, random);
        noqual_grid->grid = grid.grid;
        noqual_grid->points = grid.points;
        noqual_grid->pole_actor = grid.pole_actor;
        qualifying_data = grid.data;
        begin_race(now, &grid);
    }
    //Keep the process tick sequence and start a fresh relative session
    //clock. The caller anchors deadlines after reconstruction completes.
    session_epoch_ns = simulation_ns;
    finish_audio->set_active(focused);
    update_hud();
}

void SceneSession::activate(std::uint64_t now, bool focused) {
    if (started && active == focused) {
        return;
    }
    started = true;
    active = focused;
    presentation_valid = false;
    if (!active) {
        camera.set_rear_view(false);
        weather_audio->stop();
    }
    schedule.reset(now);
    actions.reset(now, focused);
    if (second) {
        second->actions.reset(now, focused);
        second->camera.set_rear_view(false);
    }
    if (mode != GameMode::single_race) {
        finish_audio->set_active(active);
    }
    if (championship_audio) {
        championship_audio->set_active(active);
    }
    if (active && finishing()) {
        stop_driving_audio();
        finish_audio->start();
    } else if (active && motion.state().boundary == simulation::MotionBoundary::none) {
        update_camera_audio();
        player_audio().resume(audio_inputs().engine, random, [this] {
            return driving_inputs();
        });
        for (auto& channel : rival_wheel_audio) {
            if (channel) {
                channel->start(false);
            }
        }
        player_audio().start_contacts();
        start_audio->start();
    } else {
        stop_driving_audio();
    }
    if (second && active && !finishing() && second->motion.state().boundary == simulation::MotionBoundary::none) {
        update_camera_audio();
        second->player_audio().resume(engine_inputs(1), random, [this] {
            return driving_inputs(1);
        });
        second->player_audio().start_contacts();
    }
}
} // namespace toca
