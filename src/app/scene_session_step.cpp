#include "app/audio_timeline.hpp"
#include "app/scene_session.hpp"
#include "scene/shadow_support.hpp"
#include "simulation/finish_classification.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <bit>

namespace toca {
void SceneSession::advance_recorded(std::uint64_t now, AudioTimeline& audio, void* context, void (*record)(void*, std::uint64_t, const InputSnapshot&)) {
    if (!started || !active) {
        return;
    }
    if (!presentation_valid) {
        capture_presentation_tick(now);
    }
    for (;;) {
        const auto due = schedule.take_due(now);
        if (due.empty()) {
            break;
        }
        for (const auto deadline : due) {
            if (first_step_bodies) {
                //Adopt generated physical setup at the first deadline.
                if (first_qualifying_step) {
                    if (rivals) {
                        rivals->prepare_first_qualifying_step(*first_step_bodies, motion);
                    } else {
                        motion.prepare_first_qualifying_step((*first_step_bodies)[qualifying_data.local_actor]);
                    }
                    first_qualifying_step = false;
                } else {
                    rivals->prepare_first_race_step(*first_step_bodies, motion);
                }
                first_step_bodies.reset();
            }
            if (linkup && (!sync_network() || network_runtime->session().race_view()->paused)) {
                schedule.reset(now);
                presentation_valid = false;
                return;
            }
            audio.advance(deadline);
            if (championship_audio) {
                championship_audio->update();
                race.set_finish_speech(finish_speech_started, championship_audio->playing());
            }
            const auto input = actions.take(deadline);
            if (second) {
                second->input = second->actions.take(deadline);
                auto& session = race_progress->participant(1);
                const bool complete = race.completed_laps() >= lap_limit && session.completed_laps() >= lap_limit;
                race.set_finish_group_complete(complete);
                session.set_finish_group_complete(complete);
                if (!finishing()) {
                    second->camera.set_rear_view((second->input.held & action_mask(Action::camera_rear)) != 0);
                    if ((second->input.pressed & action_mask(Action::camera_next)) != 0) {
                        second->camera.cycle_view();
                    }
                    if ((second->input.pressed & action_mask(Action::camera_trackside)) != 0) {
                        second->camera.toggle_trackside();
                    }
                }
                const auto transition = second->pit.update(second->motion.state().body.position, !second->motion.ai_controlled());
                if (transition.crossed_entry) {
                    session.enter_pit();
                }
                if (transition.entered) {
                    second->pit_notice_ns = simulation_ns;
                    second->motion.handoff_to_ai(false);
                    second->pit_audio->enter();
                    ++second->pit_entries;
                }
                if (transition.exited) {
                    if (session.completed_laps() < lap_limit) {
                        second->motion.restore_player_control();
                    }
                    ++second->pit_exits;
                }
                second->motion.set_pit_target(second->pit.target());
                second->motion.set_barrier_contacts_enabled(second->pit.collision_enabled());
            }
            const auto held = [&](Action action) {
                return (input.held & action_mask(action)) != 0;
            };
            const auto pressed = [&](Action action) {
                return (input.pressed & action_mask(action)) != 0;
            };
            if (!finishing()) {
                camera.set_rear_view(held(Action::camera_rear));
                if (pressed(Action::camera_next)) {
                    camera.cycle_view();
                }
                if (pressed(Action::camera_trackside)) {
                    camera.toggle_trackside();
                }
                update_camera_audio();
            }
            bool qualifying_finished = false, race_finished = false;
            if (motion.state().boundary == simulation::MotionBoundary::none || finishing() || second) {
                // 403dd0 processes PITS before ordinary POINTS/lap gates.
                const auto pit_transition = pit.update(motion.state().body.position, !motion.ai_controlled());
                if (pit_transition.crossed_entry) {
                    race.enter_pit();
                }
                if (pit_transition.entered) {
                    pit_notice_ns = simulation_ns;
                    motion.handoff_to_ai(finishing());
                    pit_audio->enter();
                    ++pit_entries;
                }
                if (pit_transition.exited) {
                    if (!finishing() && (!second || race.completed_laps() < lap_limit)) {
                        motion.restore_player_control();
                    }
                    ++pit_exits;
                }
                motion.set_pit_target(pit.target());
                motion.set_barrier_contacts_enabled(pit.collision_enabled());
                // 421f40/403dd0 precede the new body step; start release
                // affects this tick's clutch and State4 camera rule.
                race.set_human_control(!motion.ai_controlled());
                if (second) {
                    race_progress->participant(1).set_human_control(!second->motion.ai_controlled());
                }
                const auto events =
                    race.update((simulation_ns + simulation::LiveSchedule::interval_ns - session_epoch_ns) / 1'000'000, motion.state().body.position, pressed(Action::confirm));
                if (time_trial) {
                    time_trial->update((simulation_ns + simulation::LiveSchedule::interval_ns - session_epoch_ns) / 1'000'000,
                        {motion.state().body.position, motion.state().body.basis}, events, race.last_lap_ms());
                }
                if (race_progress) {
                    race_progress->update((simulation_ns + simulation::LiveSchedule::interval_ns - session_epoch_ns) / 1'000'000, race, race_positions());
                    if (race_progress->stop_horns_requested()) {
                        //421390 calls425950 when an actor completes the race.
                        horn->stop();
                        if (second) {
                            second->horn->stop();
                        }
                    }
                }
                if (!qualifying && mode != GameMode::time_trial) {
                    //448130 starts timing at green;401260 reconnects the
                    //clutch only when the next primary enters state6.
                    motion.set_source_start_hold(race.grid_held());
                }
                if (second) {
                    auto& session = race_progress->participant(1);
                    if (session.released() && session.grid_held() && second->motion.start_held()) {
                        second->motion.prepare_source_local_input({.start_held = true, .begin_launch = true});
                    }
                    second->motion.set_source_start_hold(session.grid_held());
                    second->motion.set_ai_route_point(session.next_point());
                    if (session.completed_laps() >= lap_limit && !second->finish_handoff) {
                        second->motion.handoff_to_ai();
                        second->finish_handoff = true;
                    }
                    if (race_progress->classified() && !local_finish_started) {
                        local_finish_started = true;
                        race.set_finish_group_complete(true);
                        session.set_finish_group_complete(true);
                        race_finished = true;
                    }
                    if (session.finish_camera_active() || second->camera.trackside()) {
                        second->finish_camera.update(second->motion.state().body.position, session.state() == 13);
                    } else {
                        second->camera.update(second->motion.state().body, false);
                    }
                }
                motion.set_ai_route_point(race.next_point());
                for (const auto event : events) {
                    switch (event) {
                        case simulation::SessionEvent::red:
                            start_audio->update(audio::StartCue::red);
                            break;
                        case simulation::SessionEvent::go:
                            start_audio->update(audio::StartCue::go);
                            break;
                        case simulation::SessionEvent::released:
                            if (linkup) {
                                (void)network_runtime->session().begin_racing();
                            }
                            if ((qualifying || mode == GameMode::time_trial) && !pit.active()) {
                                // 421390 restores the player at the first ordered
                                // start-line crossing, before this body update.
                                if (qualifying && mode != GameMode::time_trial) {
                                    motion.restore_qualifying_control();
                                } else {
                                    motion.restore_player_control();
                                }
                            }
                            if (!qualifying && mode != GameMode::time_trial) {
                                //The source launch scalar is initialized
                                //while the grid clutch is still held.
                                motion.prepare_source_local_input({.start_held = race.grid_held(), .begin_launch = true});
                            } else {
                                motion.set_start_hold(false);
                            }
                            break;
                        case simulation::SessionEvent::lap_complete:
                        case simulation::SessionEvent::attempt_restarted:
                            break;
                        case simulation::SessionEvent::results_entered:
                            if (championship_audio && race.state() == 13) {
                                championship_audio->play(audio::ChampionshipCue::results, *championship, random);
                            }
                            break;
                        case simulation::SessionEvent::qualifying_finished:
                            //421390 changes ownership but retains game+54 during qualifying.
                            motion.handoff_to_ai(false);
                            qualifying_finished = true;
                            break;
                        case simulation::SessionEvent::race_finished:
                            motion.handoff_to_ai();
                            race_finished = race_finished || (!second && !linkup);
                            break;
                        case simulation::SessionEvent::race_return_requested:
                            race_return_requested = true;
                            break;
                        case simulation::SessionEvent::race_requested:
                            begin_race(deadline);
                            break;
                    }
                }
                if (camera_phase != race.state()) {
                    finish_camera.reset_selection_cache();
                    camera_phase = race.state();
                    if (camera_phase == 4) {
                        camera.initialize_heading(scene::qualifying_camera_heading(resources.setup.selection.track_id));
                    } else if (camera_phase == 7) {
                        finish_camera.update(motion.state().body.position); //448450's first PRE selector.
                    }
                }
                if (race.finish_camera_active() || camera.trackside()) {
                    if (camera_phase != 7 && camera_phase != 8) {
                        finish_camera.update(motion.state().body.position, race.state() == 13);
                    }
                } else if (camera_phase != 4) {
                    camera.update(motion.state().body, false);
                }
            }
            // 422f60 swaps steering when horizontal and vertical mirror flags differ.
            const auto upside = frontend::cheats::active(cheats, frontend::cheats::Code::upside) != (championship && championship->showdown);
            const auto previous_prop_updates = props.frame().updates;
            auto analog = input.analog;
            if (analog && upside) {
                analog->steering_q16 = -analog->steering_q16;
            }
            const auto previous_velocity = motion.state().velocity_q16;
            const simulation::ControlsInput controls{held(Action::up), held(Action::down), held(upside ? Action::right : Action::left), held(upside ? Action::left : Action::right),
                pressed(Action::gear_up), pressed(Action::gear_down), analog};
            const auto step = simulation::boosted_step({}, frontend::cheats::active(cheats, frontend::cheats::Code::boost));
            if (linkup) {
                std::array<bool, 16> finished{};
                for (const auto& actor : network_runtime->session().race_view()->actors) {
                    finished[actor.actor] = actor.finished;
                }
                linkup->set_order(race_progress->spatial_order());
                for (std::uint32_t actor = 0; actor < linkup->count(); ++actor) {
                    if (race_progress->position(actor) == 1) {
                        linkup->set_race_leader(actor);
                        break;
                    }
                }
                linkup->set_route_distances(race_progress->route_distances(), race_progress->total_route_distance());
                linkup->update(step, motion, controls, race_progress->next_points(race), finished, motion.start_held(), &props);
                car_contacts += linkup->contacts().size();
                for (std::uint32_t actor = 0; actor < linkup->count(); ++actor) {
                    if (actor == linkup->local_actor()) {
                        continue;
                    }
                    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                        const auto delta = (std::int64_t(linkup->motion(actor, motion).state().wheel_speed_q16[wheel]) * 0x28b) >> 16;
                        auto& angle = rival_wheel_angles[actor][wheel];
                        angle = std::bit_cast<std::int16_t>(std::uint16_t(std::int64_t(angle) - delta));
                    }
                }
            } else if (rivals) {
                std::array<bool, 16> finished{};
                for (std::uint32_t actor = 0; actor < 16; ++actor) {
                    finished[actor] = race_progress->finished(actor);
                }
                rivals->set_order(race_progress->spatial_order());
                rivals->set_route_distances(race_progress->route_distances(), race_progress->total_route_distance());
                rivals->set_release_counter(race.release_counter());
                rivals->update(step, motion, race_progress->next_points(race), finished, motion.start_held(), controls, &props);
                for (std::uint32_t actor = 0; actor < 16; ++actor) {
                    if (rivals->body_mode(actor) == simulation::BodyMode::removed) {
                        race_progress->remove(actor);
                    }
                }
                car_contacts += rivals->contacts().size();
                for (std::uint32_t actor = 0; actor < 16; ++actor) {
                    if (actor == rivals->local_actor() || rivals->body_mode(actor) == simulation::BodyMode::removed) {
                        continue;
                    }
                    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                        const auto delta = (std::int64_t(rivals->actor(actor).wheel_speed_q16[wheel]) * 0x28b) >> 16;
                        auto& angle = rival_wheel_angles[actor][wheel];
                        angle = std::bit_cast<std::int16_t>(std::uint16_t(std::int64_t(angle) - delta));
                    }
                }
            } else if (second) {
                auto analog2 = second->input.analog;
                if (analog2 && upside) {
                    analog2->steering_q16 = -analog2->steering_q16;
                }
                const auto held2 = [&](Action action) {
                    return (second->input.held & action_mask(action)) != 0;
                };
                const auto pressed2 = [&](Action action) {
                    return (second->input.pressed & action_mask(action)) != 0;
                };
                const simulation::ControlsInput controls2{held2(Action::up), held2(Action::down), held2(upside ? Action::right : Action::left),
                    held2(upside ? Action::left : Action::right), pressed2(Action::gear_up), pressed2(Action::gear_down), analog2};
                const auto previous2 = second->motion.state().velocity_q16;
                motion.begin_tick();
                second->motion.begin_tick();
                if (local_car_boost && !motion.start_held()) {
                    //415090/428440: all local human slots share the same
                    //pre-update leader pose, including pit/finish AI handoffs.
                    const std::array bodies{&motion, &second->motion};
                    const auto leader = race_progress->position(0) == 1 ? 0U : 1U;
                    const auto leader_position = bodies[leader]->state().body.position;
                    for (std::size_t player = 0; player < bodies.size(); ++player) {
                        auto& body = *bodies[player];
                        auto& boost = local_boost_states[player];
                        boost.maximum_throttle_q16 = body.controls().maximum_throttle_q16;
                        const auto parameters = simulation::car_boost_step(boost, body.controls().base_throttle_q16, body.state().body.position, leader_position, player == leader);
                        boost = parameters.state;
                        body.apply_car_boost(parameters);
                    }
                }
                if (second->motion.prepare_body_update(true)) {
                    second->motion.update(controls2, step, true);
                }
                if (motion.prepare_body_update(true)) {
                    motion.update(controls, step, true);
                }
                props.advance();
                const std::array bodies{simulation::CarContactActor{&motion, resources.setup.selection.car_id, true, true, true, true},
                    simulation::CarContactActor{&second->motion, second->car_id, true, true, true, true}};
                const std::array candidates{
                    simulation::ContactCandidateActor{scene_context.actors[0].physical}, simulation::ContactCandidateActor{second->scene_context.actors[1].physical}};
                local_contact_events = local_contacts.resolve(bodies, simulation::contact_candidate_groups(candidates),
                    {.scrape_handle =
                            [this](auto first, auto other) {
                                return scrape->pair_handle(first, other);
                            },
                        .enabled = !frontend::cheats::active(cheats, frontend::cheats::Code::no_hits)});
                car_contacts += local_contact_events.size();
                const std::array vehicles{simulation::PropVehicle{motion.state().body, motion.state().velocity_q16, resources.setup.selection.car_id, true},
                    simulation::PropVehicle{second->motion.state().body, second->motion.state().velocity_q16, second->car_id, true}};
                const auto responses = props.contact(vehicles);
                motion.apply_prop_response(responses[0]);
                second->motion.apply_prop_response(responses[1]);
                motion.finish_ground();
                second->motion.finish_ground();
                second->camera.update_cockpit(second->motion.state().body, previous2, second->motion.state().velocity_q16, second->motion.state().angular_velocity_q16[1]);
                if (second->motion.controls().gear_committed) {
                    second->view->gear_changed();
                }
                for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                    const auto delta = (std::int64_t(second->motion.state().wheel_speed_q16[wheel]) * 0x28b) >> 16;
                    second->wheel_angles[wheel] = std::bit_cast<std::int16_t>(std::uint16_t(std::int64_t(second->wheel_angles[wheel]) - delta));
                }
            } else {
                motion.update(controls, step, true);
                props.advance();
                motion.apply_prop_response(props.contact({motion.state().body, motion.state().velocity_q16, resources.setup.selection.car_id, true}));
                motion.finish_ground();
            }
            camera.update_cockpit(motion.state().body, previous_velocity, motion.state().velocity_q16, motion.state().angular_velocity_q16[1]);
            if (camera_phase == 4) {
                camera.update(motion.state().body, true); //4480e0→45eb60 follows primary physics.
            } else if ((camera_phase == 7 || camera_phase == 8) && race.finish_camera_active()) {
                finish_camera.update(motion.state().body.position); //Qualifying renderer uses the completed target.
            }
            if (motion.controls().gear_committed && motion.state().boundary == simulation::MotionBoundary::none) {
                view->gear_changed();
            }
            const bool props_advanced = props.frame().updates != previous_prop_updates;
            std::vector<scene::PropSoundEvent> prop_sounds;
            if (props_advanced) {
                prop_sounds = props.frame().sounds;
                for (const auto& event : props.frame().breaks) {
                    if (auto sound = fragments.break_sign(event.root_transform, event.vehicle_velocity_q16)) {
                        sound->ordinal = event.ordinal;
                        prop_sounds.push_back(*sound);
                    }
                }
                std::ranges::sort(prop_sounds, {}, &scene::PropSoundEvent::ordinal);
            }
            // 421aa0 advances45b8c0 once per active update, including the
            // finish continuation. Presentation and focus pauses do not.
            flags.update();
            // 446a80 advances431b20,416150 and461bb0 before432210 emits
            // from the current wheel contacts. All share one table cursor.
            std::array<scene::SurfaceEffectVehicle, 16> effect_storage;
            std::size_t effect_count = 0;
            for (std::uint32_t actor = 0; actor < actor_count(); ++actor) {
                if (rivals && rivals->body_mode(actor) == simulation::BodyMode::removed) {
                    continue;
                }
                auto vehicle = actor_motion(actor).surface_effect_vehicle();
                vehicle.actor = std::uint8_t(rivals || second || linkup ? actor : 0);
                vehicle.visibility = 1;
                effect_storage[effect_count++] = vehicle;
            }
            const auto effect_vehicles = std::span(effect_storage).first(effect_count);
            engine_effects.advance();
            surface_effects.advance(effect_vehicles);
            fragments.update();
            sparks.update();
            const auto thunder = weather->advance();
            if (second) {
                (void)second->weather->advance();
            }
            if (motion.state().boundary == simulation::MotionBoundary::none) {
                surface_effects.emit(effect_vehicles);
            }
            //415e10 consumes the shared ten-slot queues after446a80.
            //409c30 wall requests precede428b50 pairs;432210 ground comes last.
            const auto& contact_effects = motion.state().barrier_effects;
            std::array<scene::ImpactFragmentRequest, 10> impact_requests;
            std::array<scene::ScrapeSparkRequest, 10> spark_requests;
            std::size_t impact_count = 0, spark_count = 0;
            if (linkup) {
                impact_count = std::min(linkup->fragments().size(), impact_requests.size());
                spark_count = std::min(linkup->sparks().size(), spark_requests.size());
                std::ranges::copy(linkup->fragments().first(impact_count), impact_requests.begin());
                std::ranges::copy(linkup->sparks().first(spark_count), spark_requests.begin());
            } else if (rivals) {
                impact_count = rivals->fragments().size();
                spark_count = rivals->sparks().size();
                std::ranges::copy(rivals->fragments(), impact_requests.begin());
                std::ranges::copy(rivals->sparks(), spark_requests.begin());
            } else {
                const auto palette = std::uint8_t(resources.setup.selection.car_id);
                if (contact_effects.fragment_strength_q16 > 0) {
                    impact_requests[impact_count++] = {contact_effects.position_q16, {}, contact_effects.fragment_strength_q16, {palette, palette}};
                }
                if (contact_effects.spark_strength_q16 > 0) {
                    spark_requests[spark_count++] = {
                        contact_effects.position_q16, contact_effects.spark_direction_q16, contact_effects.tangent_velocity_q16, contact_effects.spark_strength_q16};
                }
            }
            if (second) {
                const auto& effect = second->motion.state().barrier_effects;
                const auto palette = std::uint8_t(second->car_id);
                if (effect.fragment_strength_q16 > 0 && impact_count < impact_requests.size()) {
                    impact_requests[impact_count++] = {effect.position_q16, {}, effect.fragment_strength_q16, {palette, palette}};
                }
                if (effect.spark_strength_q16 > 0 && spark_count < spark_requests.size()) {
                    spark_requests[spark_count++] = {effect.position_q16, effect.spark_direction_q16, effect.tangent_velocity_q16, effect.spark_strength_q16};
                }
                for (const auto& request : local_contacts.effects().fragments) {
                    if (impact_count < impact_requests.size()) {
                        impact_requests[impact_count++] = request;
                    }
                }
                for (const auto& request : local_contacts.effects().sparks) {
                    if (spark_count < spark_requests.size()) {
                        spark_requests[spark_count++] = request;
                    }
                }
            }
            ground_scrape_requests = 0;
            for (std::uint32_t actor = 0; actor < actor_count(); ++actor) {
                if (rivals && rivals->body_mode(actor) == simulation::BodyMode::removed) {
                    continue;
                }
                auto input = actor_motion(actor).ground_scrape_input();
                if (rivals) {
                    input.actor_state = std::uint8_t(rivals->body_mode(actor));
                }
                input.queued_sparks = std::uint32_t(spark_count);
                const auto requests = scene::ground_scrape(input);
                ground_scrape_requests += requests.count;
                std::ranges::copy(std::span(requests.requests).first(requests.count), spark_requests.begin() + std::ptrdiff_t(spark_count));
                spark_count += requests.count;
            }
            horizon_actors.update();
            for (const auto& request : std::span(impact_requests).first(impact_count)) {
                fragments.impact(request);
            }
            if (resources.setup.weather <= 2) {
                for (const auto& request : std::span(spark_requests).first(spark_count)) {
                    sparks.emit(request);
                }
            }
            damage.advance();
            // 4608b0 emits before the new view selection. Local single-player
            // effect masks retain bit1 from4469a0/4271d0.
            std::array<scene::EngineEffectVehicle, 16> exhaust_storage;
            const auto exhaust_vehicles = std::span(exhaust_storage).first(effect_vehicles.size());
            for (std::size_t index = 0; index < effect_vehicles.size(); ++index) {
                const auto actor = effect_vehicles[index].actor;
                auto vehicle = actor_motion(actor).engine_effect_vehicle();
                vehicle.actor = std::uint8_t(rivals || second || linkup ? actor : 0);
                vehicle.visibility = engine_visibility_masks[actor];
                exhaust_vehicles[index] = vehicle;
            }
            engine_effects.emit(exhaust_vehicles);
            // 202b0 selects after motion/emission, before446b50/446d30.
            // Detached-part callbacks above consume the previous snapshot.
            refresh_visibility();
            for (auto& vehicle : effect_vehicles) {
                vehicle.visibility = wheel_effect_visibility(vehicle.actor);
            }
            skid_marks.update(effect_vehicles);
            // Native view callbacks advance once per active25Hz deadline.
            weather->update_view(scene::weather_camera(view_camera().world_to_view));
            if (second) {
                second->weather->update_view(scene::weather_camera(second_camera().world_to_view));
            }
            if (qualifying_finished) {
                // Finish-tick AI target jitter precedes 448450/444270's
                // classification draws from the same original RNG.
                begin_qualifying_finish();
            }
            if (race_finished) {
                if (noqual_grid) {
                    const auto results = race_results();
                    std::array<std::uint32_t, 16> order;
                    for (std::size_t rank = 0; rank < order.size(); ++rank) {
                        order[rank] = results.rows[rank].actor_id;
                    }
                    simulation::award_race_classification(noqual_grid->points, noqual_grid->podiums, order, qualifying_data.local_actor, 0);
                }
                if (championship) {
                    const auto results = race_results();
                    championship_audio->play(audio::ChampionshipCue::race_finish, *championship, random, &results);
                    finish_speech_started = championship_audio->playing();
                    race::championship_apply_race(*championship, results);
                    championship_dirty = true;
                }
                audio_service.stop_music();
                stop_driving_audio();
                finish_audio->start();
            }
            horn->set_camera(camera.trackside() ? 0u : camera.selected_view());
            horn->update(held(Action::horn) && race.state() == 6 && !motion.ai_controlled() && motion.state().boundary == simulation::MotionBoundary::none
                         && (qualifying || mode == GameMode::time_trial || race.start_phase() != 3));
            finish_audio->update();
            if (second && second->motion.state().boundary != simulation::MotionBoundary::none) {
                stop_player_audio(1);
            }
            if (!finishing()) {
                //415090/421980 contact audio precedes446a80's tire dispatch.
                update_contact_audio(prop_sounds);
                update_wheel_contacts();
                update_wheel_audio();
            }
            //446a80 sends thunder after44c540, before the frame's WHINE.
            if (thunder) {
                weather_audio->thunder(*thunder, random);
            }
            if (!finishing()) {
                update_engine_audio();
            }
            if (motion.state().boundary == simulation::MotionBoundary::none) {
                for (std::size_t wheel = 0; wheel < wheel_angles.size(); ++wheel) {
                    // 4608b0's wheel phase, scheduled here independently of FPS.
                    const auto delta = (std::int64_t(motion.state().wheel_speed_q16[wheel]) * 0x28b) >> 16;
                    wheel_angles[wheel] = std::bit_cast<std::int16_t>(std::uint16_t(std::int64_t(wheel_angles[wheel]) - delta));
                }
                if (!finishing()) {
                    const auto listener = audio_listener();
                    if (rivals || linkup) {
                        std::array<audio::RivalEngineInput, 15> motors;
                        std::size_t motor_count = 0;
                        for (std::uint32_t actor = 0; actor < actor_count(); ++actor) {
                            if (actor == qualifying_data.local_actor || (rivals && rivals->body_mode(actor) == simulation::BodyMode::removed)
                                || (linkup && linkup->body_mode(actor) == simulation::BodyMode::removed)) {
                                continue;
                            }
                            const auto& state = actor_motion(actor).state();
                            const auto& control = actor_motion(actor).controls();
                            const auto motor = audio::engine_inputs(control.engine_speed_q16, control.rpm_scale_q16, control.throttle_q16, control.gear);
                            motors[motor_count++] = {actor, linkup ? linkup->physics_car_id(actor) : rivals->car_id(actor), state.body.position, motor.rpm};
                        }
                        rival_audio->update(std::span(motors).first(motor_count), listener, camera.trackside() ? 0u : camera.selected_view());
                    }
                }
                update_hud();
            } else {
                // One local coverage stop must not silence the other driver.
                stop_player_audio(0);
                if (second) {
                    update_hud();
                }
            }
            if (!finishing()) {
                update_exhaust_audio();
            }
            if (linkup) {
                publish_network();
            }
            if (!finishing()) {
                prepare_wheel_audio();
            }
            ++steps;
            simulation_ns += simulation::LiveSchedule::interval_ns;
            capture_presentation_tick(deadline);
            record(context, deadline, input);
        }
    }
}
} // namespace toca
