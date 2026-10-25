#include "app/trace.hpp"

#include "app/finale_session.hpp"
#include "app/runtime_resources.hpp"
#include "app/scene_session.hpp"

#include <algorithm>

namespace toca {
namespace {
static constexpr std::string_view session_columns =
    ",session_state,start_phase,start_held,start_plays,start_sample,session_lap,completed_laps,lap_time_ms,best_lap_ms,next_point,"
    "hud_lap,hud_lap_limit,hud_lap_time_ms,hud_best_lap_ms,qualifying_phase,ai_controlled,finish_plays,finish_playing,finish_volume,finish_camera,qualifying_rank,"
    "cone_count,post_count,sign_count,flag_count,flag_updates,flag_frame,"
    "horizon_updates,horizon_moving_count,horizon_x,horizon_y,horizon_z,rotorcraft_enabled,rotorcraft_state,rotorcraft_x,rotorcraft_y,rotorcraft_z,"
    "prop_updates,prop_contacts,prop_missing_surface,prop_impact_plays,prop_impact_sample,prop_feedback_q16,gamepad_rumble_low,gamepad_rumble_high,"
    "wheel_constant_commands,wheel_sine_commands,wheel_stop_commands,wheel_impact_x,wheel_impact_y,wheel_impact_magnitude,wheel_sine_magnitude,wheel_sine_period_us,contact_"
    "fragments,"
    "pit_active,pit_point,pit_barriers,pit_entries,pit_exits,pit_plays,pause_active,pause_page,pause_selection,"
    "weather_updates,weather_particles,weather_first_x,weather_first_y,weather_first_z,weather_lightning,thunder_plays,thunder_sample,damage_parts,damage_sum,damage_"
    "detached,contact_sparks,scrape_plays,scrape_playing,scrape_volume,impact_pan,scrape_pan,rumble_pan,skid_pan,rolling_pan,tail_pan,ground_scrape_requests,"
    "surface_updates,surface_particles,surface_trails,surface_emitted,surface_kind_mask,surface_audio_sample,surface_audio_volume,"
    "skid_updates,skid_persistent,skid_live,skid_emitted,skid_cursor,"
    "horn_playing,horn_plays,game_mode,ghost_best_ms,ghost_samples,ghost_recording_samples,ghost_visible,ghost_car,ghost_x,ghost_y,ghost_z,ghost_dirty,ghost_error,"
    "engine_effect_updates,exhaust_sprites,exhaust_emitted,exhaust_cooldown,exhaust_phase,exhaust_plays,exhaust_sample,visibility_sectors,visibility_roots,"
    "rival_count,rival_moving,rival_laps,rival_finished,rival_boundaries,race_position,race_classified,race_total_ms,race_result_ms,race_winner_ms,"
    "rival_voices,rival_volume,car_contacts,camera_view,camera_trackside,camera_render_view,engine_camera_view,listener_x,listener_y,listener_z,listener_heading,actor_camera_"
    "available,actor_camera_rotorcraft,cockpit_shift_phase,gear_committed,championship_phase,championship_meeting,championship_race,championship_points,championship_target,"
    "surface_roughness_cursor,local_players,p2_car,p2_x,p2_y,p2_z,p2_speed,p2_throttle,p2_brake,p2_gear,p2_boundary,p2_camera,p2_lap,p2_completed_laps,p2_lap_ms,"
    "p2_best_ms,p2_position,p2_engine_active,p2_hud_gear,view1_width,view1_height,view2_width,view2_height,p2_trackside,"
    "p2_ai_controlled,p2_next_point,p2_heading,p2_forward_x,p2_forward_z,p2_pit_active,p2_pit_point,p2_pit_barriers,p2_pit_entries,p2_pit_exits,p2_pit_plays,"
    "p2_session_state,p2_total_ms,p2_result_ms,race_result_count,race_result_first,race_result_second,"
    "p2_hud_lap,p2_hud_lap_time_ms,p2_hud_best_lap_ms,network_actor,network_generation,network_race_phase,network_paused,network_actors,network_finish_order,network_actor_"
    "states,hud_split_active,hud_split_ticks,hud_split_text,base_throttle,maximum_throttle,tire_offset,p2_base_throttle,p2_maximum_throttle,p2_tire_offset,release_counter,"
    "brake_lights,"
    "wheel_queue_updates,wheel_queue_events,wheel_audio_events,rival_wheel_audio_events,rival_skid_voices,source_rng_state,damage_cycle,fragment_cursor,rival_actors\n";
}

void Trace::session_values(std::ostream& stream, const SceneSession* scene, const race::ChampionshipState* championship) {
    if (scene && scene->championship) {
        championship = scene->championship;
    }
    const auto* horizon = scene ? &scene->horizon_actors.frame() : nullptr;
    std::array<std::int32_t, 3> moving_position{};
    if (scene && !scene->resources.moving_horizon.moving_roots.empty()) {
        const auto& transform = horizon->moving.transforms.at(scene->resources.moving_horizon.moving_roots.front());
        std::copy_n(transform.begin() + 12, 3, moving_position.begin());
    }
    std::uint32_t wheel_constants = 0, wheel_sines = 0, wheel_stops = 0;
    WheelFeedbackCommand wheel_impact, wheel_sine;
    if (scene) {
        for (const auto& command : scene->wheel_feedback_commands[0]) {
            switch (command.kind) {
                case WheelFeedbackCommand::Kind::constant:
                    ++wheel_constants;
                    wheel_impact = command;
                    break;
                case WheelFeedbackCommand::Kind::sine:
                    ++wheel_sines;
                    wheel_sine = command;
                    break;
                case WheelFeedbackCommand::Kind::stop_sine:
                case WheelFeedbackCommand::Kind::stop_all:
                    ++wheel_stops;
                    break;
            }
        }
    }
    stream << ',' << (scene ? scene->race.state() : 0) << ',' << (scene ? scene->race.start_phase() : 0) << ',' << (scene && scene->motion.start_held()) << ','
           << (scene && scene->start_audio ? scene->start_audio->played_count() : 0) << ',' << (scene && scene->start_audio ? scene->start_audio->last_sample() : 0) << ','
           << (scene ? scene->race.lap() : 0) << ',' << (scene ? scene->race.completed_laps() : 0) << ',' << (scene ? scene->race.lap_time_ms() : 0) << ','
           << (scene ? scene->race.best_lap_ms() : 0) << ',' << (scene ? scene->race.next_point() : 0) << ',' << (scene && scene->hud ? scene->hud->values().session().lap : 0)
           << ',' << (scene && scene->hud ? scene->hud->values().session().lap_limit : 0) << ',' << (scene && scene->hud ? scene->hud->values().session().lap_time_ms : 0) << ','
           << (scene && scene->hud ? scene->hud->values().session().best_lap_ms : 0) << ',' << (scene && scene->qualifying) << ',' << (scene && scene->motion.ai_controlled())
           << ',' << (scene && scene->finish_audio ? scene->finish_audio->played_count() : 0) << ',' << (scene && scene->finish_audio && scene->finish_audio->playing()) << ','
           << (scene && scene->finish_audio ? scene->finish_audio->volume() : 0) << ','
           << (scene && scene->race.finish_camera_active() ? std::int64_t(scene->finish_camera.selected()) : -1) << ','
           << (scene && scene->qualifying_results ? scene->qualifying_results->local_rank + 1 : 0) << ',' << (scene ? scene->resources.cones.actors.size() : 0) << ','
           << (scene ? scene->resources.posts.actors.size() : 0) << ',' << (scene ? scene->resources.signs.actors.size() : 0) << ',' << (scene ? scene->flags.actor_count() : 0)
           << ',' << (scene ? scene->flags.updates() : 0) << ',' << (scene && scene->flags.actor_count() ? int(scene->flags.first_frame()) : -1) << ','
           << (horizon ? horizon->updates : 0) << ',' << (horizon ? horizon->moving_count : 0) << ',' << moving_position[0] << ',' << moving_position[1] << ','
           << moving_position[2] << ',' << (horizon && !horizon->rotorcraft.transforms.empty()) << ',' << (horizon ? int(horizon->rotorcraft_state) : 0) << ','
           << (horizon ? horizon->rotorcraft_position[0] : 0) << ',' << (horizon ? horizon->rotorcraft_position[1] : 0) << ',' << (horizon ? horizon->rotorcraft_position[2] : 0)
           << ',' << (scene ? scene->props.frame().updates : 0) << ',' << (scene ? scene->props.frame().contacts : 0) << ',' << (scene && scene->props.frame().missing_surface)
           << ',' << (scene ? scene->prop_impact_plays : 0) << ',' << (scene ? scene->prop_impact_sample : 0) << ',' << (scene ? scene->motion.state().prop_impact_q16 : 0) << ','
           << (scene ? scene->gamepad_rumble.low : 0) << ',' << (scene ? scene->gamepad_rumble.high : 0) << ',' << wheel_constants << ',' << wheel_sines << ',' << wheel_stops
           << ',' << wheel_impact.direction_x << ',' << wheel_impact.direction_y << ',' << wheel_impact.magnitude << ',' << wheel_sine.magnitude << ',' << wheel_sine.period_us
           << ',' << (scene ? scene->fragments.size() : 0) << ',' << (scene && scene->pit.active()) << ',' << (scene ? scene->pit.point() : 0) << ','
           << (!scene || scene->pit.collision_enabled()) << ',' << (scene ? scene->pit_entries : 0) << ',' << (scene ? scene->pit_exits : 0) << ','
           << (scene && scene->pit_audio ? scene->pit_audio->played_count() : 0) << ',' << (scene && scene->pause.active()) << ',' << (scene ? int(scene->pause.page()) : 0) << ','
           << (scene ? scene->pause.selection() : 0);
    const auto* weather = scene && scene->weather ? &scene->weather->frame() : nullptr;
    const auto particle = weather && !weather->particles.empty() ? weather->particles.front() : scene::WeatherVector{};
    stream << ',' << (weather ? weather->updates : 0) << ',' << (weather ? weather->particles.size() : 0) << ',' << particle[0] << ',' << particle[1] << ',' << particle[2] << ','
           << (weather ? int(weather->lightning_alpha) : 0) << ',' << (scene && scene->weather_audio ? scene->weather_audio->played_count() : 0) << ','
           << (scene && scene->weather_audio ? scene->weather_audio->last_sample() : 0);
    std::uint32_t damaged = 0, damage_sum = 0, detached = 0;
    if (scene) {
        for (const auto level : scene->motion.damage().levels) {
            damaged += level != 0;
            damage_sum += level;
        }
        for (const auto& part : scene->damage.parts().parts()) {
            detached += part.active;
        }
    }
    stream << ',' << damaged << ',' << damage_sum << ',' << detached << ',' << (scene ? scene->sparks.size() : 0) << ','
           << (scene && scene->scrape ? scene->scrape->played_count() : 0) << ',' << (scene && scene->scrape && scene->scrape->playing()) << ','
           << (scene && scene->scrape ? scene->scrape->volume() : 0) << ',' << (scene && scene->impact ? scene->impact->last_pan() : 64) << ','
           << (scene && scene->scrape ? scene->scrape->pan() : 64) << ',' << (scene && scene->driving ? scene->driving->rumble_pan() : 64) << ','
           << (scene && scene->driving ? scene->driving->skid_pan() : 64) << ',' << (scene && scene->driving ? scene->driving->rolling_pan() : 64) << ','
           << (scene && scene->driving ? scene->driving->tail_pan() : 64) << ',' << (scene ? scene->ground_scrape_requests : 0);
    const auto* effects = scene ? &scene->surface_effects.frame() : nullptr;
    stream << ',' << (effects ? effects->updates : 0) << ',' << (effects ? effects->particles : 0) << ',' << (effects ? effects->plumes : 0) << ','
           << (effects ? effects->emitted : 0) << ',' << (effects ? effects->kind_mask : 0) << ',' << (scene && scene->driving ? scene->driving->rolling_sample() : 0) << ','
           << (scene && scene->driving ? scene->driving->rolling_volume() : 0);
    const auto* marks = scene ? &scene->skid_marks.frame() : nullptr;
    stream << ',' << (marks ? marks->updates : 0) << ',' << (marks ? marks->persistent : 0) << ',' << (marks ? marks->active : 0) << ',' << (marks ? marks->emitted : 0) << ','
           << (scene ? scene->skid_marks.cursor() : 0);
    stream << ',' << (scene && scene->horn && scene->horn->playing()) << ',' << (scene && scene->horn ? scene->horn->played_count() : 0);
    const auto* trial = scene && scene->time_trial ? &*scene->time_trial : nullptr;
    const auto ghost = trial ? trial->playback() : std::nullopt;
    const auto position = ghost ? ghost->position_q16 : std::array<std::int32_t, 3>{};
    stream << ',' << (scene ? int(scene->mode) : 0) << ',' << (trial ? trial->best_lap_ms() : 0) << ',' << (trial ? trial->sample_count() : 0) << ','
           << (trial ? trial->recording_samples() : 0) << ',' << bool(ghost) << ',' << (trial ? trial->best_car_id() : 0) << ',' << position[0] << ',' << position[1] << ','
           << position[2] << ',' << (trial && trial->dirty()) << ',' << (scene && !scene->ghost_error.empty());
    const auto* exhaust = scene ? &scene->engine_effects.frame() : nullptr;
    const auto* visibility = scene && scene->view ? &scene->view->visibility() : nullptr;
    stream << ',' << (exhaust ? exhaust->updates : 0) << ',' << (exhaust ? exhaust->sprites.size() : 0) << ',' << (exhaust ? exhaust->emitted : 0) << ','
           << (exhaust ? exhaust->cooldown : 0) << ',' << (exhaust ? unsigned(exhaust->phase) : 0) << ',' << (scene ? scene->exhaust_plays : 0) << ','
           << (scene ? scene->exhaust_sample : 0) << ',' << (visibility ? visibility->ordered_sectors.size() : 0) << ','
           << (visibility ? std::ranges::count_if(visibility->draws,
                                [](const auto& entry) {
                                    return entry.dynamic;
                                })
                          : 0);
    std::uint32_t rival_count = 0, moving = 0, laps = 0, finished = 0, boundaries = 0, local_time = 0, winner_time = 0, second_time = 0, result_count = 0;
    std::int32_t result_first = -1, result_second = -1;
    if (scene && (scene->rivals || scene->linkup)) {
        for (std::uint32_t actor = 0; actor < scene->actor_count(); ++actor) {
            if (actor == scene->qualifying_data.local_actor || (scene->rivals && scene->rivals->body_mode(actor) == simulation::BodyMode::removed)
                || (scene->linkup && scene->linkup->body_mode(actor) == simulation::BodyMode::removed)) {
                continue;
            }
            ++rival_count;
            moving += scene->actor_motion(actor).state().speed_q16 > 655;
            boundaries += scene->actor_motion(actor).state().boundary != simulation::MotionBoundary::none;
            laps += scene->race_progress->completed_laps(actor);
            finished += scene->race_progress->finished(actor);
        }
    }
    if (scene && scene->race_progress && scene->race_progress->classified()) {
        const auto results = scene->race_results();
        local_time = results.rows[results.local_rank].time_ms;
        winner_time = results.rows[0].time_ms;
        result_count = results.count;
        result_first = std::int32_t(results.rows[0].actor_id);
        result_second = std::int32_t(results.rows[1].actor_id);
        for (const auto& entry : std::span(results.rows).first(results.count)) {
            if (scene->second && entry.actor_id == 1) {
                second_time = entry.time_ms;
            }
        }
    }
    stream << ',' << rival_count << ',' << moving << ',' << laps << ',' << finished << ',' << boundaries << ','
           << (scene && scene->race_progress ? scene->race_progress->position(scene->qualifying_data.local_actor) : 0) << ','
           << (scene && scene->race_progress && scene->race_progress->classified()) << ',' << (scene ? scene->race.race_time_ms() : 0) << ',' << local_time << ',' << winner_time
           << ',' << (scene && scene->rival_audio ? scene->rival_audio->playing_count() : 0) << ',' << (scene && scene->rival_audio ? scene->rival_audio->maximum_volume() : 0)
           << ',' << (scene ? scene->car_contacts : 0) << ',' << (scene ? scene->camera.selected_view() : 0) << ',' << (scene && scene->camera.trackside()) << ','
           << (scene ? scene->view_camera().view : 0) << ',' << (scene && scene->engine ? scene->engine->camera_view() : 0);
    const auto listener = scene ? scene->audio_listener() : audio::SpatialListener{};
    stream << ',' << listener.position_q16[0] << ',' << listener.position_q16[1] << ',' << listener.position_q16[2] << ',' << listener.inverse_heading << ','
           << (horizon && horizon->camera_zero.has_value()) << ',' << (horizon && horizon->camera_zero && horizon->camera_zero->rotorcraft) << ','
           << (scene && scene->view ? int(scene->view->cockpit_shift_phase()) : 0) << ',' << (scene && scene->motion.controls().gear_committed) << ','
           << (championship ? int(championship->phase) : -1) << ',' << (championship ? championship->meeting : 0) << ',' << (championship ? championship->race : 0) << ','
           << (championship ? championship->points[championship->local_actor] : 0) << ',' << (championship ? race::championship_target(*championship) : 0) << ','
           << roughness_.current();
    const auto* second = scene ? scene->second.get() : nullptr;
    stream << ',' << (second ? 2 : 1) << ',' << (second ? second->car_id : 0);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        stream << ',' << (second ? second->motion.state().body.position[axis] : 0);
    }
    const auto* session2 = second ? &scene->race_progress->participant(1) : nullptr;
    stream << ',' << (second ? second->motion.state().speed_q16 : 0) << ',' << (second ? second->motion.controls().throttle_q16 : 0) << ','
           << (second ? second->motion.controls().brake_q16 : 0) << ',' << (second ? int(second->motion.controls().gear) : 0) << ','
           << (second ? int(second->motion.state().boundary) : 0) << ',' << (second ? second->camera.selected_view() : 0) << ',' << (session2 ? session2->lap() : 0) << ','
           << (session2 ? session2->completed_laps() : 0) << ',' << (session2 ? session2->lap_time_ms() : 0) << ',' << (session2 ? session2->best_lap_ms() : 0) << ','
           << (second ? scene->race_progress->position(1) : 0) << ',' << (second && second->engine && second->engine->active()) << ','
           << (second && second->hud ? second->hud->values().gear() : 0) << ',' << (scene ? scene->visibility_width : 0) << ',' << (scene ? scene->visibility_height : 0) << ','
           << (second ? scene->second_width : 0) << ',' << (second ? scene->second_height : 0) << ',' << (second && second->camera.trackside()) << ','
           << (second && second->motion.ai_controlled()) << ',' << (session2 ? session2->next_point() : 0) << ',' << (second ? second->camera.heading() : 0) << ','
           << (second ? second->motion.state().body.basis[0][0] : 0) << ',' << (second ? second->motion.state().body.basis[0][2] : 0) << ',' << (second && second->pit.active())
           << ',' << (second ? second->pit.point() : 0) << ',' << (!second || second->pit.collision_enabled()) << ',' << (second ? second->pit_entries : 0) << ','
           << (second ? second->pit_exits : 0) << ',' << (second && second->pit_audio ? second->pit_audio->played_count() : 0) << ',' << (session2 ? session2->state() : 0) << ','
           << (session2 ? session2->race_time_ms() : 0) << ',' << second_time << ',' << result_count << ',' << result_first << ',' << result_second;
    const auto* hud2 = second && second->hud ? &second->hud->values().session() : nullptr;
    stream << ',' << (hud2 ? hud2->lap : 0) << ',' << (hud2 ? hud2->lap_time_ms : 0) << ',' << (hud2 ? hud2->best_lap_ms : 0);
    const auto* shared = scene && scene->linkup ? scene->network_runtime->session().race_view() : nullptr;
    stream << ',' << (shared ? std::int32_t(scene->linkup->local_actor()) : -1) << ',' << (shared ? shared->generation : 0) << ',' << (shared ? std::int32_t(shared->phase) : -1)
           << ',' << (shared && shared->paused) << ',' << (shared ? shared->actors.size() : 0) << ',';
    if (shared) {
        for (const auto& finish : shared->finish_order) {
            stream << unsigned(finish.actor) << ':' << finish.time_centiseconds << ':' << finish.ordinal << '/';
        }
    }
    stream << ',';
    if (shared) {
        for (const auto& actor : shared->actors) {
            const auto& body = scene->actor_motion(actor.actor);
            stream << unsigned(actor.actor) << ':' << actor.active_human << ':' << actor.finished << ':' << body.state().body.position[0] << ':' << body.state().body.position[1]
                   << ':' << body.state().body.position[2] << ':' << body.state().speed_q16 << ':' << body.controls().raw_throttle_q16 << ':' << body.controls().raw_brake_q16
                   << ':' << unsigned(body.controls().gear) << '/';
        }
    }
    const auto* hud = scene && scene->hud ? &scene->hud->values().session() : nullptr;
    stream << ',' << (hud && hud->split_active) << ',' << (hud ? hud->split_ticks : 0) << ',' << (hud ? hud->split_text : "") << ','
           << (scene ? scene->motion.controls().base_throttle_q16 : 0) << ',' << (scene ? scene->motion.controls().maximum_throttle_q16 : 0) << ','
           << (scene ? scene->motion.controls().tire_offsets_q16[0] : 0) << ',' << (second ? second->motion.controls().base_throttle_q16 : 0) << ','
           << (second ? second->motion.controls().maximum_throttle_q16 : 0) << ',' << (second ? second->motion.controls().tire_offsets_q16[0] : 0) << ','
           << (scene ? scene->race.release_counter() : 0) << ',' << (scene && scene->current_presentation.cars[0].lights.brake) << ',' << (scene ? scene->wheel_queue_updates : 0)
           << ',' << (scene ? scene->wheel_queue_events : 0) << ',' << (scene ? scene->wheel_audio_events : 0) << ',' << (scene ? scene->rival_wheel_audio_events : 0) << ','
           << (scene ? scene->rival_skid_voices() : 0) << ',' << (scene ? scene->random.state() : 0) << ',' << (scene ? unsigned(scene->damage.cycle()) : 0) << ','
           << (scene ? scene->fragment_tables.cursor : 0) << ',';
    if (scene && scene->rivals) {
        const auto next = scene->race_progress->next_points(scene->race);
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            const auto& position = scene->actor_motion(actor).state().body.position;
            if (actor) {
                stream << '/';
            }
            stream << actor << ':' << int(scene->rivals->body_mode(actor)) << ':' << position[0] << ':' << position[1] << ':' << position[2] << ':' << next[actor] << ':'
                   << (actor == scene->qualifying_data.local_actor ? scene->race.completed_laps() : scene->race_progress->completed_laps(actor));
        }
    }
    stream << '\n';
}

Trace::Trace(const std::filesystem::path& path, const simulation::SurfaceRoughness& roughness) : roughness_(roughness) {
    if (!path.empty()) {
        create_output_parent(path);
        output_.open(path);
        if (!output_) {
            throw std::runtime_error("Cannot open diagnostic trace: " + path.string());
        }
        simulation_.open(path.string() + ".simulation.csv");
        if (!simulation_) {
            throw std::runtime_error("Cannot open simulation trace");
        }
        simulation_ << "timestamp_ns,step,simulation_ns,held,pressed,throttle,brake,steering,gear,engine_speed,engine_torque,rpm,throttle_percent,low_hz,low_volume,high_hz,"
                       "high_volume,car_hz,car_volume,body_x,body_y,body_z,speed,signed_speed,heading,steering_angle,motion_boundary,contacts,speed_integer,whirr_hz,whirr_"
                       "volume,whine_hz,whine_volume,whine_active,rumble_sample,rumble_volume,barrier_contacts,impacts,impact_plays,impact_sample,impact_volume,hud_rpm,hud_"
                       "gear,barrier_sector,wheel_drive,"
                       "recovery_active,recovery_head,recovery_secondary,recovery_entries,recovery_exits,velocity_x,velocity_y,velocity_z,yaw_rate,"
                       "surface_0,surface_1,surface_2,surface_3,longitudinal_slip_0,longitudinal_slip_1,longitudinal_slip_2,longitudinal_slip_3,"
                       "lateral_slip_0,lateral_slip_1,lateral_slip_2,lateral_slip_3,body_forward_x,body_forward_z,skid_sample,skid_volume"
                    << session_columns;
        output_ << "poll,frame,presented,timestamp_ns,elapsed_ns,width,height,held,pressed,released,pointer_x,pointer_y,pointer_inside,focused,minimized,fps_limit,vsync,audio_"
                   "active,audio_events,screen,selection,language,frontend_cues,audio_voices,music_track,music_playing,music_starts,player_name,car_id,manual_transmission,"
                   "laps,qualifying,car_boost,weather,difficulty,track_id,stored_track_id,stored_car_id,track_available,track_rotation,menu_animation_ns,menu_entered_ns,loading_"
                   "step,scene_vertices,body_x,body_y,"
                   "body_z,"
                   "simulation_steps,simulation_ns,engine_rpm,engine_gear,scene_active,speed,motion_boundary,camera_heading,impacts,impact_plays,hud_rpm,hud_gear,hud_speed,"
                   "resolved_weather,horizon_index,fullscreen,volume,mute,settings_requests,settings_saved,settings_error,"
                   "draft_width,draft_height,draft_fullscreen,draft_vsync,draft_fps_limit,draft_volume,draft_mute,"
                   "extras_all_tracks,extras_bonus_cars,extras_bonus_track,earned_track_threshold,earned_car_8,earned_car_9,extras_saved,extras_error,cheat_announcements,"
                   "cheat_sample,car_available,code_flags,scene_code_flags,code_delta,effects_volume,music_volume,game_music,stereo_effects,graphics_detail,draft_graphics_detail,"
                   "scene_detail,"
                   "split_vertical,draft_split_vertical,players,selection_player,p2_name,p2_selected_car,p2_manual,record_lap_ms,record_car,record_driver,record_code,record_"
                   "highlight,champ_record_driver,champ_record_points,champ_record_round,champ_record_awards,champ_records_error,championship_error,"
                   "controller_player,input_sensitivity,input_deadzone,input_gamepad,input_auto,binding_capture_index,physical_binding_kind,physical_binding_code,physical_binding_"
                   "direction,championship_"
                   "speech_plays,championship_speech_playing,finale_active_ms,finale_phase,finale_credit_page,finale_can_exit,finale_pace_x,finale_pace_z,finale_truck_x,"
                   "finale_truck_z,finale_model_triangles,finale_credit_glyphs,presentation_alpha,presentation_body_x,presentation_body_y,presentation_body_z,"
                   "presentation_horizon_x,presentation_horizon_y,presentation_horizon_z,presentation_rotorcraft_x,presentation_rotorcraft_y,presentation_rotorcraft_z,"
                   "presentation_weather_x,presentation_weather_y,presentation_weather_z,presentation_surface_x,presentation_surface_y,presentation_surface_z,"
                   "finale_presentation_alpha,finale_presentation_x,finale_presentation_y,finale_presentation_z,"
                   "frame_prepare_ns,gpu_wait_ns,gpu_upload_ns,gpu_encode_ns,gpu_submit_ns,gpu_draw_calls,gpu_buffer_growths,gpu_samples,gpu_mipmapped_draw_calls,gpu_world_draw_"
                   "calls,gpu_model_draw_calls,gpu_model_instances,gpu_instanced_draw_calls"
                << session_columns;
    }
}

void Trace::record(std::uint64_t poll, std::uint64_t frame, bool presented, const ClockStep& clock, platform::Extent drawable, const InputSnapshot& input, bool minimized,
    const Settings& settings, const audio::Service& audio, const frontend::Controller& frontend, std::uint64_t frontend_cues, std::uint32_t loading_step,
    std::size_t scene_vertices, const SceneSession* scene, const SettingsChanges& changes, const ExtrasChanges& extras, const audio::Championship* championship_commentary,
    const FinaleSession* finale, const gpu::FrameTiming& frame_timing, std::uint64_t frame_prepare_ns) {
    if (!output_.is_open()) {
        return;
    }
    output_ << poll << ',' << frame << ',' << presented << ',' << clock.timestamp_ns << ',' << clock.elapsed_ns << ',' << drawable.width << ',' << drawable.height << ','
            << input.held << ',' << input.pressed << ',' << input.released << ',' << input.pointer_x << ',' << input.pointer_y << ',' << input.pointer_inside << ','
            << input.focused << ',' << minimized << ',' << settings.video.fps_limit << ',' << settings.video.vsync << ',' << audio.active() << ',' << audio.consumed_events() << ','
            << frontend::screen_name(frontend.screen()) << ',' << frontend.selection() << ',' << frontend.language() << ',' << frontend_cues << ',' << audio.active_voices() << ','
            << audio.music_track() << ',' << audio.music_playing() << ',' << audio.music_starts() << ',' << frontend.player_name() << ',' << frontend.car_id() << ','
            << frontend.transmission_manual() << ',' << frontend.setup().laps << ',' << frontend.setup().qualifying << ',' << frontend.setup().car_boost << ','
            << frontend.setup().weather << ',' << frontend.setup().difficulty << ',' << frontend.track_id() << ',' << frontend.stored_track_id() << ',' << frontend.stored_car_id()
            << ',' << frontend.track_available() << ',' << frontend.track_preview_rotation() << ',' << frontend.animation_time_ns() << ',' << frontend.menu_entered_ns() << ','
            << loading_step << ',' << scene_vertices;
    for (std::size_t index = 0; index < 3; ++index) {
        output_ << ',' << (scene ? scene->motion.state().body.position[index] : 0);
    }
    output_ << ',' << (scene ? scene->steps : 0) << ',' << (scene ? scene->simulation_ns : 0) << ',' << (scene ? scene->engine_inputs().rpm : 0) << ','
            << (scene ? int(scene->motion.controls().gear) : 0) << ',' << (scene && scene->active) << ',' << (scene ? scene->motion.state().speed_q16 : 0) << ','
            << (scene ? int(scene->motion.state().boundary) : 0) << ',' << (scene ? scene->camera.heading() : 0) << ',' << (scene ? scene->motion.state().impact_count : 0) << ','
            << (scene && scene->impact ? scene->impact->played_count() : 0) << ',' << (scene && scene->hud ? scene->hud->values().rpm() : 0) << ','
            << (scene && scene->hud ? scene->hud->values().gear() : 0) << ',' << (scene && scene->hud ? scene->hud->values().speed(frontend.language() != 0) : 0) << ','
            << (scene ? scene->resources.setup.weather : 0) << ',' << (scene ? scene->resources.setup.horizon_index : 0);
    const auto& draft = frontend.settings_values();
    output_ << ',' << settings.video.fullscreen << ',' << settings.audio.master_gain << ',' << settings.audio.muted << ',' << changes.requests << ',' << changes.saved << ','
            << changes.error << ',' << draft.video.width << ',' << draft.video.height << ',' << draft.video.fullscreen << ',' << draft.video.vsync << ',' << draft.video.fps_limit
            << ',' << draft.audio.master_gain << ',' << draft.audio.muted;
    const auto& cheats = frontend.cheats_state();
    output_ << ',' << cheats.overrides.all_tracks << ',' << cheats.overrides.bonus_cars << ',' << cheats.overrides.bonus_track << ',' << cheats.progress.track_threshold << ','
            << cheats.progress.bonus_car_8 << ',' << cheats.progress.bonus_car_9 << ',' << extras.saved << ',' << !frontend.cheats_error().empty() << ',' << extras.announcements
            << ',' << extras.sample << ',' << frontend.car_available();
    std::uint32_t code_flags = 0, scene_code_flags = 0;
    for (const auto& entry : frontend::cheats::catalog()) {
        if (frontend::cheats::active(cheats, entry.code)) {
            code_flags |= 1u << static_cast<std::uint32_t>(entry.code);
        }
        if (scene && frontend::cheats::active(scene->cheats, entry.code)) {
            scene_code_flags |= 1u << static_cast<std::uint32_t>(entry.code);
        }
    }
    output_ << ',' << code_flags << ',' << scene_code_flags << ','
            << simulation::boosted_step({}, frontend::cheats::active(scene ? scene->cheats : cheats, frontend::cheats::Code::boost)).delta_q16 << ','
            << settings.audio.effects_volume << ',' << settings.audio.music_volume << ',' << settings.audio.game_music << ',' << settings.audio.stereo_effects << ','
            << settings.graphics.detail << ',' << draft.graphics.detail << ',' << (scene ? std::int32_t(scene->resources.setup.selection.detail) : -1) << ','
            << settings.graphics.split_vertical << ',' << draft.graphics.split_vertical << ',' << frontend.setup().players << ',' << frontend.selection_player() << ','
            << frontend.player_name(1) << ',' << frontend.car_id(1) << ',' << frontend.transmission_manual(1);
    const auto& record = frontend.records()->at(frontend.stored_track_id());
    output_ << ',' << record.lap_ms << ',' << record.car_id << ',' << record.driver << ',' << record.code << ','
            << (frontend.record_highlight_track() ? std::int32_t(*frontend.record_highlight_track()) : -1);
    const auto& championship_record = frontend.championship_records()->front();
    std::uint32_t record_awards = 0;
    for (std::size_t index = 0; index < championship_record.awards.size(); ++index) {
        record_awards |= championship_record.awards[index] << (index * 2);
    }
    output_ << ',' << championship_record.driver << ',' << championship_record.points << ',' << championship_record.round << ',' << record_awards << ','
            << !frontend.championship_records_error().empty() << ',' << !frontend.championship_error().empty();
    output_ << ',' << frontend.controller_player() << ',' << settings.input.sensitivity << ',' << settings.input.deadzone_percent << ',' << !settings.input.device_id.empty() << ','
            << (settings.input.device_id == automatic_input_device) << ',' << frontend.binding_capture_index() << ','
            << (input.physical_binding ? int(input.physical_binding->kind) : -1) << ',' << (input.physical_binding ? input.physical_binding->code : -1) << ','
            << (input.physical_binding ? input.physical_binding->direction : 0) << ',' << (championship_commentary ? championship_commentary->played_count() : 0) << ','
            << (championship_commentary && championship_commentary->playing());
    const auto ending = finale ? finale->flow.frame() : scene::ChampionshipFinaleFrame{};
    std::uint32_t triangles = 0;
    if (finale && finale->view) {
        for (const auto count : finale->view->draw_counts().model_triangles) {
            triangles += count;
        }
    }
    output_ << ',' << (finale ? finale->flow.active_time_ms() : 0) << ',' << ending.camera_phase << ',' << ending.credit_page << ',' << (finale && finale->flow.can_exit()) << ','
            << ending.pace.object_to_world[12] << ',' << ending.pace.object_to_world[14] << ',' << ending.transporter.object_to_world[12] << ','
            << ending.transporter.object_to_world[14] << ',' << triangles << ',' << (finale && finale->view ? finale->view->draw_counts().credit_glyphs : 0);
    output_ << ',' << (scene ? scene->presentation_alpha : 1.f);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        output_ << ',' << (scene ? scene->presentation_body_position[axis] : 0.f);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        output_ << ',' << (scene ? scene->presentation_horizon_position[axis] : 0);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        output_ << ',' << (scene ? scene->presentation_rotorcraft_position[axis] : 0);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        output_ << ',' << (scene ? scene->presentation_weather_position[axis] : 0);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        output_ << ',' << (scene ? scene->presentation_surface_position[axis] : 0);
    }
    output_ << ',' << (finale ? finale->presentation_alpha : 1.f);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        output_ << ',' << (finale ? finale->presentation_pace_position[axis] : 0.f);
    }
    output_ << ',' << frame_prepare_ns << ',' << frame_timing.wait_ns << ',' << frame_timing.upload_ns << ',' << frame_timing.encode_ns << ',' << frame_timing.submit_ns << ','
            << frame_timing.draw_calls << ',' << frame_timing.buffer_growths << ',' << frame_timing.samples << ',' << frame_timing.mipmapped_draw_calls << ','
            << frame_timing.world_draw_calls << ',' << frame_timing.model_draw_calls << ',' << frame_timing.model_instances << ',' << frame_timing.instanced_draw_calls;
    session_values(output_, scene, frontend.championship_state() ? &*frontend.championship_state() : nullptr);
    output_.flush();
    if (!output_) {
        throw std::runtime_error("Writing diagnostic trace failed");
    }
}

void Trace::record_simulation(std::uint64_t timestamp, const SceneSession& session, const InputSnapshot& input) {
    if (!simulation_.is_open()) {
        return;
    }
    const auto& state = session.motion.controls();
    const auto motor = session.engine_inputs();
    simulation_ << timestamp << ',' << session.steps << ',' << session.simulation_ns << ',' << input.held << ',' << input.pressed << ',' << state.throttle_q16 << ','
                << state.brake_q16 << ',' << state.steering_q16 << ',' << int(state.gear) << ',' << state.engine_speed_q16 << ',' << state.engine_torque_q16 << ',' << motor.rpm
                << ',' << motor.throttle_percent;
    for (const auto& layer : session.engine->last_controls()) {
        simulation_ << ',' << layer.rate_hz << ',' << layer.volume;
    }
    const auto& motion = session.motion.state();
    for (const auto coordinate : motion.body.position) {
        simulation_ << ',' << coordinate;
    }
    simulation_ << ',' << motion.speed_q16 << ',' << motion.signed_speed_q16 << ',' << session.camera.heading() << ',' << state.steering_angle << ',' << int(motion.boundary) << ','
                << motion.contact_count << ',' << motion.speed_integer << ',' << session.driving->wind_control().rate_hz << ',' << session.driving->wind_control().volume << ','
                << session.engine->whine_control().rate_hz << ',' << session.engine->whine_control().volume << ',' << session.engine->whine_active() << ','
                << session.driving->rumble_sample() << ',' << session.driving->rumble_volume() << ',' << motion.barrier_contact_count << ',' << motion.impact_count << ','
                << session.impact->played_count() << ',' << session.impact->last_sample() << ',' << session.impact->last_volume() << ',' << session.hud->values().rpm() << ','
                << session.hud->values().gear() << ',' << motion.barrier_sector << ',' << state.wheel_drive_q16[0] << ',' << motion.barrier_recovery.active << ','
                << (motion.barrier_recovery.active ? std::int64_t(motion.barrier_recovery.head) : -1) << ','
                << (motion.barrier_recovery.has_secondary ? std::int64_t(motion.barrier_recovery.secondary_segment) : -1) << ',' << motion.recovery_entry_count << ','
                << motion.recovery_exit_count;
    for (const auto velocity : motion.velocity_q16) {
        simulation_ << ',' << velocity;
    }
    simulation_ << ',' << motion.angular_velocity_q16[1];
    for (const auto surface : motion.surface_ids) {
        simulation_ << ',' << unsigned(surface);
    }
    for (const auto slip : motion.longitudinal_slip_q16) {
        simulation_ << ',' << slip;
    }
    for (const auto slip : motion.lateral_slip_q16) {
        simulation_ << ',' << slip;
    }
    simulation_ << ',' << motion.body.basis[0][0] << ',' << motion.body.basis[0][2] << ',' << session.driving->skid_sample() << ',' << session.driving->skid_volume();
    session_values(simulation_, &session);
    simulation_.flush();
    if (!simulation_) {
        throw std::runtime_error("Writing simulation trace failed");
    }
}
} // namespace toca
