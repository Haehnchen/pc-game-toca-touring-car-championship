#include "app/scene_session.hpp"

#include <algorithm>

namespace toca {
void SceneSession::update_camera_audio() {
    const auto motor_view = camera.trackside() ? 3u : camera.selected_view();
    const auto spatial_view = camera.trackside() ? 0u : camera.selected_view();
    const bool changed_bank = engine->set_camera(motor_view);
    if (changed_bank && rival_audio) {
        rival_audio->stop(); // 4145c0 calls426360 before reselecting the motor bank.
    }
    driving->set_camera(spatial_view, changed_bank, camera.trackside());
    for (auto& channel : rival_wheel_audio) {
        if (channel) {
            channel->set_camera(spatial_view, false, camera.trackside());
        }
    }
    impact->set_camera(spatial_view);
    scrape->set_camera(spatial_view);
    if (second) {
        const auto view = second->camera.trackside() ? 0u : second->camera.selected_view();
        const auto changed = second->engine->set_camera(second->camera.trackside() ? 3u : view);
        second->driving->set_camera(view, changed, second->camera.trackside());
        second->impact->set_camera(view);
        second->scrape->set_camera(view);
        second->horn->set_camera(view);
    }
}

std::uint32_t SceneSession::actor_count() const {
    return linkup ? linkup->count() : rivals ? 16 : second ? 2 : 1;
}

const simulation::Motion& SceneSession::actor_motion(std::uint32_t actor) const {
    if (linkup) {
        return linkup->motion(actor, motion);
    }
    return second ? (actor == 0 ? motion : second->motion) : (!rivals || actor == rivals->local_actor()) ? motion : rivals->motion(actor);
}

std::uint32_t SceneSession::local_wheel_actor(std::uint32_t player) const {
    return rivals || linkup ? qualifying_data.local_actor : player;
}

std::uint8_t SceneSession::car_effect_visibility(std::uint32_t actor, std::uint8_t eligibility) const {
    //446b50 skips unobserved rivals. Distant bodies retain full-body
    //wheel heights/slip, so those values cannot produce new road marks.
    if ((rivals && rivals->body_mode(actor) != simulation::BodyMode::full) || (linkup && linkup->body_mode(actor) != simulation::BodyMode::full)) {
        return 0;
    }
    const bool human = linkup ? linkup->human(actor) : second || actor == local_wheel_actor();
    // Single-player446e50 reads manual model+140, not PVS output+138.
    // Keep the existing Linkup view policy until its multiplayer gate is owned.
    return human ? 1 : linkup ? scene_context.actors[actor].render_mask : eligibility;
}

std::uint8_t SceneSession::wheel_effect_visibility(std::uint32_t actor) const {
    return car_effect_visibility(actor, scene_context.actors[actor].wheel_effect_mask);
}

std::uint8_t SceneSession::engine_effect_visibility(std::uint32_t actor) const {
    return car_effect_visibility(actor, scene_context.actors[actor].engine_effect_mask);
}

void SceneSession::initialize_wheel_audio(bool reset_totals) {
    wheel_queues = {};
    if (reset_totals) {
        wheel_queue_updates = wheel_queue_events = wheel_audio_events = rival_wheel_audio_events = 0;
    }
    const audio::EngineConfig config{resources.setup.selection.car_id, resources.setup.weather, camera.selected_view()};
    for (std::uint32_t actor = 0; actor < actor_count(); ++actor) {
        wheel_queues[actor].identity = std::uint8_t(actor);
        if ((rivals || linkup) && actor != local_wheel_actor() && !rival_wheel_audio[actor]) {
            rival_wheel_audio[actor] = std::make_unique<audio::Driving>(audio_service, config, *wheel_audio_data);
        }
    }
}

void SceneSession::prepare_wheel_audio() {
    //4608b0 publishes raw brake pedals before446b50's view0 lamp
    //pass. This pass precedes visibility/bodymode/effect eligibility.
    //AI lamps remain owned by the existing405530 driver latch.
    if (rivals) {
        rivals->publish_frame_brake_pedals(motion);
    } else {
        for (auto actor = actor_count(); actor-- > 0;) {
            if (linkup) {
                linkup->motion(actor, motion).publish_frame_brake_pedal();
            } else {
                (second && actor != 0 ? second->motion : motion).publish_frame_brake_pedal();
            }
        }
    }
    //The complete4608b0 publication precedes446b50's separate pass.
    for (std::uint32_t actor = 0; actor < actor_count(); ++actor) {
        if (linkup) {
            linkup->motion(actor, motion).publish_frame_brake_lights();
        } else if (second) {
            (actor == 0 ? motion : second->motion).publish_frame_brake_lights();
        } else if (!rivals || actor == rivals->local_actor()) {
            motion.publish_frame_brake_lights();
        }
    }
    const auto actors = std::span(wheel_queues).first(actor_count());
    for (std::uint32_t actor = 0; actor < actors.size(); ++actor) {
        auto& next = actors[actor];
        const auto& state = actor_motion(actor).state();
        next.human = linkup ? linkup->human(actor) : second || actor == local_wheel_actor();
        next.visibility = wheel_effect_visibility(actor);
        for (std::size_t wheel = 0; wheel < 4; ++wheel) {
            next.input.longitudinal_slip[wheel] = state.longitudinal_slip_q16[wheel];
            next.input.lateral_slip[wheel] = state.lateral_slip_q16[wheel];
            next.input.airborne[wheel] = std::uint8_t(!state.wheel_contact[wheel]);
        }
    }
    const auto players = second ? 2u : 1u;
    for (std::uint32_t player = 0; player < players; ++player) {
        (void)audio::prepare_source_wheel_queues(actors, {.view = player, .local_players = players});
    }
    ++wheel_queue_updates;
    for (auto& actor : actors) {
        wheel_queue_events += std::ranges::count_if(actor.queue.events, [](auto kind) {
            return kind != -1;
        });
        if (auto* channel = rival_wheel_audio[actor.identity].get()) {
            channel->settle_skid_mute(actor.pending_mute);
        }
    }
}

void SceneSession::update_wheel_contacts() {
    if (motion.state().boundary == simulation::MotionBoundary::none) {
        driving->update_contacts(driving_inputs());
    }
    if (second && second->motion.state().boundary == simulation::MotionBoundary::none) {
        second->driving->update_contacts(driving_inputs(1));
    }
}

void SceneSession::update_wheel_audio() {
    for (std::uint32_t actor = 0; actor < actor_count(); ++actor) {
        const auto& state = actor_motion(actor).state();
        if (state.boundary != simulation::MotionBoundary::none) {
            continue;
        }
        const bool local = actor == local_wheel_actor();
        auto* channel = local ? driving.get() : second ? second->driving.get() : rival_wheel_audio[actor].get();
        if (!channel) {
            continue;
        }
        auto input = local ? driving_inputs() : second ? driving_inputs(1) : audio::DrivingInputs{};
        input.listener = local || !second ? audio_listener() : audio_listener(1);
        input.random = &random;
        input.local_player = wheel_queues[actor].human || local || bool(second);
        input.skid_queue = wheel_queues[actor].queue;
        if (!local && !second) {
            for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                input.wheels[wheel] = {
                    state.body.contacts[wheel], false, 0, !state.wheel_contact[wheel], state.surface_ids[wheel], state.longitudinal_slip_q16[wheel], state.lateral_slip_q16[wheel]};
            }
        }
        const auto events = std::ranges::count_if(input.skid_queue.events, [](auto kind) {
            return kind != -1;
        });
        wheel_audio_events += events;
        if (!local && !second) {
            rival_wheel_audio_events += events;
        }
        channel->update_queued_skids(input);
    }
}

void SceneSession::update_exhaust_audio() {
    if (engine_effects.frame().sounds.empty()) {
        return;
    }
    std::array<audio::SourceContactListener, 2> listeners;
    std::size_t count = 0;
    audio::Impact* sink = nullptr;
    if (motion.state().boundary == simulation::MotionBoundary::none) {
        listeners[count++] = {audio_listener(), std::int32_t(view_camera().view)};
        sink = impact.get();
    }
    if (second && second->motion.state().boundary == simulation::MotionBoundary::none) {
        listeners[count++] = {audio_listener(1), std::int32_t(second_camera().view)};
        if (!sink) {
            sink = second->impact.get();
        }
    }
    if (!sink) {
        return;
    }
    for (const auto& event : engine_effects.frame().sounds) {
        //420be0 retains human identity after AI handoff; Linkup admits
        //only this peer as local.424b50 selects one voice for all views.
        const bool local = linkup ? event.actor == local_wheel_actor() : event.local_player;
        const auto previous = sink->played_count();
        sink->update_views({event.position, event.kind, event.strength_q16, local}, std::span(listeners).first(count));
        if (sink->played_count() != previous) {
            ++exhaust_plays;
            exhaust_sample = sink->last_sample();
        }
    }
}

void SceneSession::update_engine_audio() {
    if (second && second->motion.state().boundary == simulation::MotionBoundary::none) {
        second->engine->update(engine_inputs(1), random);
    }
    if (motion.state().boundary == simulation::MotionBoundary::none) {
        engine->update(audio_inputs().engine, random);
        driving->update_wind(driving_inputs());
    }
    if (second && second->motion.state().boundary == simulation::MotionBoundary::none) {
        second->driving->update_wind(driving_inputs(1));
    }
}

void SceneSession::update_contact_audio(std::span<const scene::PropSoundEvent> prop_sounds) {
    if (second && !finishing() && second->motion.state().boundary == simulation::MotionBoundary::none) {
        const auto listener = audio_listener(1);
        for (const auto& event : second->motion.state().impacts) {
            second->impact->update({event.position_q16, event.kind, event.strength_q16, true}, listener, &random);
        }
        const auto& effect = second->motion.state().barrier_effects;
        second->scrape->update(effect.position_q16, effect.scrape_strength_q16, listener, random);
        second->horn->update((second->input.held & action_mask(Action::horn)) != 0 && race.released() && !second->motion.ai_controlled());
        for (const auto& event : local_contacts.effects().audio) {
            if (event.action == simulation::CarContactAudioAction::impact) {
                impact->update({event.position, event.event_kind, event.strength_q16, true}, audio_listener(), &random);
            } else if (event.action == simulation::CarContactAudioAction::reset_pair) {
                scrape->reset_pair(event.first, event.second);
            } else {
                scrape->pair_event(event.first, event.second, event.event_kind, event.position, event.strength_q16, audio_listener(), random);
            }
        }
    }
    if (motion.state().boundary == simulation::MotionBoundary::none) {
        const auto listener = audio_listener();
        const auto play_motion_impacts = [&](const simulation::Motion& body, bool landing) {
            for (const auto& event : body.state().impacts) {
                if ((event.kind == 5) == landing) {
                    impact->update({event.position_q16, event.kind, event.strength_q16, event.local_player}, listener, &random);
                }
            }
        };
        const auto play_wall = [&](const simulation::Motion& body, bool local) {
            play_motion_impacts(body, false);
            if (local) {
                const auto& effect = body.state().barrier_effects;
                scrape->update(effect.position_q16, effect.scrape_strength_q16, listener, random);
            }
        };
        if (rivals || linkup) {
            const auto order = linkup ? linkup->integration_order() : rivals->integration_order();
            for (const auto actor : order) {
                play_wall(actor_motion(actor), actor == qualifying_data.local_actor);
            }
            const auto& effects = linkup ? linkup->contact_effects() : rivals->contact_effects();
            for (const auto& event : effects.audio) {
                if (event.action == simulation::CarContactAudioAction::reset_pair) {
                    scrape->reset_pair(event.first, event.second);
                } else if (event.action == simulation::CarContactAudioAction::impact) {
                    impact->update({event.position, event.event_kind, event.strength_q16, event.local_player}, listener, &random);
                } else {
                    scrape->pair_event(event.first, event.second, event.event_kind, event.position, event.strength_q16, listener, random);
                }
            }
        } else {
            play_wall(motion, true);
        }
        const auto play_prop = [&](const scene::PropSoundEvent& event) {
            const auto previous = impact->played_count();
            impact->update({event.position_q16, event.event_kind, event.strength_q16, event.local_player}, listener, &random);
            if (impact->played_count() != previous) {
                ++prop_impact_plays;
                prop_impact_sample = impact->last_sample();
            }
        };
        for (const auto& event : prop_sounds) {
            play_prop(event);
        }
        for (std::uint32_t actor = 0; actor < actor_count(); ++actor) {
            if (rivals && rivals->body_mode(actor) == simulation::BodyMode::removed) {
                continue;
            }
            play_motion_impacts(actor_motion(actor), true);
        }
    }
}

std::uint32_t SceneSession::rival_skid_voices() const {
    return std::uint32_t(std::ranges::count_if(rival_wheel_audio, [](const auto& channel) {
        return channel && channel->skid_playing();
    }));
}

LocalPlayerAudio SceneSession::player_audio() {
    return {engine, driving, impact, scrape};
}

void SceneSession::stop_player_audio(std::uint32_t player) {
    if (player == 1 && second && second->engine) {
        second->engine->stop();
        second->driving->stop();
        second->impact->stop();
        second->scrape->stop();
        second->horn->stop();
        second->pit_audio->stop();
        wheel_queues[1].state = {};
        wheel_queues[1].queue = {};
        return;
    }
    if (rival_audio) {
        rival_audio->stop();
    }
    pit_audio->stop();
    horn->stop();
    start_audio->stop();
    impact->stop();
    scrape->stop();
    driving->stop();
    engine->stop();
    for (std::size_t actor = 0; actor < wheel_queues.size(); ++actor) {
        if (rival_wheel_audio[actor]) {
            rival_wheel_audio[actor]->stop();
        }
        wheel_queues[actor].state = {};
        wheel_queues[actor].queue = {};
        wheel_queues[actor].pending_mute = 0;
    }
}

void SceneSession::stop_driving_audio() {
    stop_player_audio(0);
    if (second) {
        stop_player_audio(1);
    }
}

void SceneSession::restart_grid_audio(std::uint64_t now) {
    update_camera_audio();
    finish_audio->fade();
    player_audio().start();
    start_audio->start();
    if (game_music) {
        audio_service.start_music(scene::track_content(resources.setup.selection.track_id).music_track, 1, now);
    }
}

audio::EngineInputs SceneSession::engine_inputs(std::uint32_t player) const {
    const auto& state = player == 0 ? motion.controls() : second->motion.controls();
    return audio::engine_inputs(state.engine_speed_q16, state.rpm_scale_q16, state.throttle_q16, state.gear);
}

audio::StartAudioInputs SceneSession::audio_inputs() const {
    return audio::session_audio_inputs(engine_inputs(), motion.state().speed_integer, race.state());
}

audio::SpatialListener SceneSession::audio_listener(std::uint32_t player) const {
    // 4313d0/426190 use the actual camera world forward axis,
    // including View3's pitch/yaw, rather than the anchor yaw.
    const auto world = scene::weather_camera((player == 0 ? view_camera() : second_camera()).world_to_view);
    return {world.position, audio::source_listener_heading(world.forward)};
}

audio::DrivingInputs SceneSession::driving_inputs(std::uint32_t player) const {
    const auto& state = player == 0 ? motion.state() : second->motion.state();
    audio::DrivingInputs input;
    //44a6f0 feeds its audio-only grid speed through414240 to414780 in
    //single-player mode. The split branch returns before that consumer.
    input.speed_integer = second ? state.speed_integer : audio_inputs().speed_integer;
    input.shift_ticks = player == 0 ? engine->shift_ticks() : second->engine->shift_ticks();
    input.listener = audio_listener(player);
    for (std::size_t wheel = 0; wheel < input.wheels.size(); ++wheel) {
        input.wheels[wheel] = {state.body.contacts[wheel], state.rumble[wheel], state.rumble_strength_q16[wheel], !state.wheel_contact[wheel], state.surface_ids[wheel],
            state.longitudinal_slip_q16[wheel], state.lateral_slip_q16[wheel]};
    }
    input.random = &random;
    return input;
}

void SceneSession::set_finish_audio_focus(bool focused) {
    if (mode == GameMode::single_race) {
        //441680 stops driving voices, but does not own sample83's handle.
        //Keep that loop through overlays; real focus loss remains silent.
        finish_audio->set_active(focused);
    }
}
} // namespace toca
