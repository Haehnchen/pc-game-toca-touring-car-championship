#include "app/scene_session.hpp"
#include "scene/presentation.hpp"
#include "scene/shadow_support.hpp"
#include "scene/split_screen.hpp"

#include <algorithm>

namespace toca {
scene::CameraInputs SceneSession::view_camera(std::uint32_t width, std::uint32_t height) const {
    const auto& body = motion.state().body;
    const bool authored = race.finish_camera_active() || camera.trackside();
    const auto view_id = race.finish_camera_active() ? 2u : camera.trackside() ? 0u : camera.selected_view();
    const auto* actor_camera = !authored && view_id == 0 ? &horizon_actors.frame().camera_zero.value() : nullptr;
    std::array<float, 16> camera_world{};
    scene::CameraInputs result{authored       ? finish_camera.world_to_view(body.position, &surface, race.finish_camera_active() && race.state() == 13, &camera_world)
                               : actor_camera ? actor_camera->world_to_view
                                              : camera.world_to_view(body, &camera_world),
        second ? scene::split_view_projection(resources.setup.selection.detail, width ? width : visibility_width, height ? height : visibility_height, split_vertical)
               : scene::selected_view_projection(resources.setup.selection.detail, width ? width : visibility_width, height ? height : visibility_height,
                     camera.projection_view()),
        authored       ? std::optional{finish_camera.position(body.position, &surface, race.finish_camera_active() && race.state() == 13)}
        : actor_camera ? std::optional{actor_camera->anchor_position_q16}
                       : std::nullopt,
        std::uint16_t(view_id), frontend::cheats::active(cheats, frontend::cheats::Code::chun), frontend::cheats::active(cheats, frontend::cheats::Code::handy)};
    result.published_world_position = actor_camera ? actor_camera->published_world_position : std::array{camera_world[12], camera_world[13], camera_world[14]};
    result.canonical_single_player = noqual_grid.has_value();
    return result;
}

scene::CameraInputs SceneSession::second_camera(std::uint32_t width, std::uint32_t height) const {
    const auto& body = second->motion.state().body;
    const auto& session = race_progress->participant(1);
    const bool authored = session.finish_camera_active() || second->camera.trackside();
    const auto view_id = session.finish_camera_active() ? 2u : second->camera.trackside() ? 0u : second->camera.selected_view();
    const auto* actor_camera = !authored && view_id == 0 ? &horizon_actors.frame().camera_zero.value() : nullptr;
    std::array<float, 16> camera_world{};
    scene::CameraInputs result{authored       ? second->finish_camera.world_to_view(body.position, &surface, session.state() == 13, &camera_world)
                               : actor_camera ? actor_camera->world_to_view
                                              : second->camera.world_to_view(body, &camera_world),
        scene::split_view_projection(resources.setup.selection.detail, width ? width : second_width, height ? height : second_height, split_vertical),
        authored       ? std::optional{second->finish_camera.position(body.position, &surface, session.state() == 13)}
        : actor_camera ? std::optional{actor_camera->anchor_position_q16}
                       : std::nullopt,
        std::uint16_t(view_id), frontend::cheats::active(cheats, frontend::cheats::Code::chun), frontend::cheats::active(cheats, frontend::cheats::Code::handy)};
    result.published_world_position = actor_camera ? actor_camera->published_world_position : std::array{camera_world[12], camera_world[13], camera_world[14]};
    return result;
}

void SceneSession::set_viewport(std::uint32_t width, std::uint32_t height, bool vertical) {
    const auto panes = second ? scene::split_viewports(width, height, vertical) : std::array<gpu::Viewport, 2>{};
    const auto first_width = second ? panes[0].width : width;
    const auto first_height = second ? panes[0].height : height;
    if (first_width != visibility_width || first_height != visibility_height || vertical != split_vertical
        || (second && (panes[1].width != second_width || panes[1].height != second_height))) {
        presentation_valid = false;
    }
    split_vertical = vertical;
    if (second) {
        camera.set_split_screen(vertical);
        second->camera.set_split_screen(vertical);
        visibility_width = panes[0].width;
        visibility_height = panes[0].height;
        second_width = panes[1].width;
        second_height = panes[1].height;
    } else {
        visibility_width = width;
        visibility_height = height;
    }
}

std::vector<scene::RivalModel> SceneSession::rival_model_bindings() const {
    std::vector<scene::RivalModel> bindings;
    if (second) {
        return {{1, &second->car}};
    }
    if (linkup) {
        for (std::uint32_t actor = 0; actor < linkup->count(); ++actor) {
            if (actor != linkup->local_actor()) {
                bindings.push_back({actor, &models.car(linkup->car_id(actor)), false});
            }
        }
        return bindings;
    }
    if (mode != GameMode::time_trial) {
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            if (actor != qualifying_data.local_actor) {
                // 44df6e/49a780 pair fleet drivers by their team actor.
                // 4437f0's split players keep the personal-model default.
                bindings.push_back({actor, &models.car(scene::rival_model_id(actor, qualifying_data.drivers[actor].car_id)), (actor & 1u) != 0});
            }
        }
    }
    return bindings;
}

scene::CarLights SceneSession::car_lights(const simulation::Motion& body) const {
    const bool wet = resources.setup.weather >= 3;
    //434f00 sets weather lamps;446b50/404ea0 select human/AI brakes.
    return {body.ai_controlled() ? body.ai_headlights() : wet, wet, body.brake_lights(), body.damage().broken_features};
}

std::span<const scene::RivalPose> SceneSession::rival_poses(std::span<scene::RivalPose, 15> result) const {
    std::size_t count = 0;
    if (second) {
        result[count++] = {1, car_pose(1)};
    }
    if (rivals) {
        for (std::uint32_t actor = 0; actor < 16; ++actor) {
            if (actor == rivals->local_actor() || rivals->body_mode(actor) == simulation::BodyMode::removed) {
                continue;
            }
            const auto& state = rivals->actor(actor);
            auto setup = resources.setup;
            setup.selection.car_id = rivals->car_id(actor);
            result[count++] = {
                actor, {state.body.object_to_world, rivals->controls(actor).steering_angle, rival_wheel_angles[actor],
                           scene::car_shadow_footprint(scene::car_shadow_input(state.body, setup)), state.damage.levels, state.car_visual_angles, false,
                           car_lights(rivals->motion(actor)), rivals->motion(actor).car_light_ground_input(rivals->body_mode(actor) == simulation::BodyMode::distant)}};
        }
    }
    if (linkup) {
        for (std::uint32_t actor = 0; actor < linkup->count(); ++actor) {
            if (actor == linkup->local_actor() || linkup->body_mode(actor) == simulation::BodyMode::removed) {
                continue;
            }
            const auto& body = linkup->motion(actor, motion);
            auto setup = resources.setup;
            setup.selection.car_id = linkup->car_id(actor);
            result[count++] = {actor, {body.state().body.object_to_world, body.controls().steering_angle, rival_wheel_angles[actor],
                                          scene::car_shadow_footprint(scene::car_shadow_input(body.state().body, setup)), body.damage().levels, body.state().car_visual_angles,
                                          false, car_lights(body), body.car_light_ground_input(linkup->body_mode(actor) == simulation::BodyMode::distant)}};
            result[count - 1].car.remap_livery = false;
        }
    }
    return result.first(count);
}

scene::CarPose SceneSession::car_pose(std::uint32_t player) const {
    const auto& body = player == 0 ? motion : second->motion;
    auto setup = resources.setup;
    setup.selection.car_id = player == 0 ? resources.setup.selection.car_id : second->car_id;
    scene::CarPose result{body.state().body.object_to_world, body.controls().steering_angle, player == 0 ? wheel_angles : second->wheel_angles,
        scene::car_shadow_footprint(scene::car_shadow_input(body.state().body, setup)), body.damage().levels, body.state().car_visual_angles, false, car_lights(body),
        body.car_light_ground_input()};
    result.actor_id = player != 0 ? player : rivals ? rivals->local_actor() : linkup ? linkup->local_actor() : 0u;
    // 44e2a0 uses4437f0 for Time Trial, split and Linkup; only the
    // ordinary one-player race uses44df6e/49a780's paired team materials.
    result.remap_livery = mode != GameMode::time_trial && !second && !linkup;
    return result;
}

SceneSession::PresentationState SceneSession::capture_presentation() const {
    PresentationState result;
    for (std::uint32_t player = 0; player < (second ? 2u : 1u); ++player) {
        result.cameras[player] = player == 0 ? view_camera() : second_camera();
        result.cars[player] = car_pose(player);
        const auto& follow = player == 0 ? camera : second->camera;
        const auto& finish = player == 0 ? finish_camera : second->finish_camera;
        const auto& session = player == 0 ? race : race_progress->participant(1);
        const bool authored = session.finish_camera_active() || follow.trackside();
        // Authored camera selection is a cut even when its public view ID
        // stays unchanged. Entering the close results camera is also a cut.
        result.camera_cuts[player] =
            std::uint64_t(authored) << 32 | std::uint64_t(session.finish_camera_active()) << 33 | std::uint64_t(session.state() == 13) << 34 | (authored ? finish.selected() : 0);
    }
    result.rival_count = rival_poses(result.rivals).size();
    result.ghost = ghost_pose();
    result.ghost_lap_ms = race.lap_time_ms();
    return result;
}

void SceneSession::capture_presentation_tick(std::uint64_t deadline) {
    auto next = capture_presentation();
    bool continuous = presentation_valid;
    for (std::uint32_t player = 0; player < (second ? 2u : 1u); ++player) {
        continuous = continuous && next.camera_cuts[player] == current_presentation.camera_cuts[player]
                     && scene::camera_continuous(current_presentation.cameras[player], next.cameras[player]);
    }
    previous_presentation = continuous ? current_presentation : next;
    current_presentation = std::move(next);
    presentation_tick_ns = deadline;
    presentation_valid = true;
    presentation_continuous = continuous;
}

const SceneSession::Presentation& SceneSession::presentation_frame(std::uint64_t now, bool exact_tick) {
    if (!presentation_valid) {
        capture_presentation_tick(now);
    }
    // One tick of render latency brackets real poses instead of predicting
    // physics. Catch-up never extrapolates beyond the latest consumed tick.
    presentation_alpha =
        exact_tick || !active || !presentation_continuous
            ? 1.f
            : float(std::min(now > presentation_tick_ns ? now - presentation_tick_ns : 0, simulation::LiveSchedule::interval_ns)) / float(simulation::LiveSchedule::interval_ns);
    auto& result = rendered_presentation;
    static_cast<PresentationState&>(result) = current_presentation;
    result.horizon = horizon_actors.presentation_frame(presentation_alpha);
    for (std::uint32_t player = 0; player < (second ? 2u : 1u); ++player) {
        result.cameras[player] = scene::interpolate_camera(previous_presentation.cameras[player], current_presentation.cameras[player], presentation_alpha);
        result.cars[player] = scene::interpolate_car_pose(previous_presentation.cars[player], current_presentation.cars[player], presentation_alpha);
        const auto& follow = player == 0 ? camera : second->camera;
        const auto& session = player == 0 ? race : race_progress->participant(1);
        if (follow.selected_view() == 0 && !follow.trackside() && !session.finish_camera_active() && result.horizon.camera_zero) {
            // Keep the attached camera on the same rendered actor pose,
            // including the original route's end-to-start camera cut.
            result.cameras[player].world_to_view = result.horizon.camera_zero->world_to_view;
            result.cameras[player].registered_camera_position = result.horizon.camera_zero->anchor_position_q16;
            result.cameras[player].published_world_position = result.horizon.camera_zero->published_world_position;
        }
    }
    for (std::size_t index = 0; index < result.rival_count; ++index) {
        for (std::size_t previous = 0; previous < previous_presentation.rival_count; ++previous) {
            if (result.rivals[index].actor_id == previous_presentation.rivals[previous].actor_id) {
                result.rivals[index].car = scene::interpolate_car_pose(previous_presentation.rivals[previous].car, result.rivals[index].car, presentation_alpha);
                break;
            }
        }
    }
    if (result.ghost && previous_presentation.ghost && result.ghost_lap_ms >= previous_presentation.ghost_lap_ms) {
        result.ghost = scene::interpolate_car_pose(*previous_presentation.ghost, *result.ghost, presentation_alpha);
    }
    std::array<scene::EffectPose, 16> effect_poses;
    std::size_t pose_count = 0;
    const auto local_actor = linkup ? linkup->local_actor() : rivals ? rivals->local_actor() : 0u;
    effect_poses[pose_count++] = scene::effect_pose(std::uint8_t(local_actor), current_presentation.cars[0].object_to_world, result.cars[0].object_to_world);
    for (std::size_t index = 0; index < result.rival_count; ++index) {
        const auto& rival = result.rivals[index];
        // Network corrections may arrive between simulation ticks. Effects
        // still belong to the captured tick, just like the displayed car.
        effect_poses[pose_count++] = scene::effect_pose(std::uint8_t(rival.actor_id), current_presentation.rivals[index].car.object_to_world, rival.car.object_to_world);
    }
    const auto poses = std::span(effect_poses).first(pose_count);
    result.props = props.presentation_frame(presentation_alpha);
    result.fragments = fragments.presentation_triangles(presentation_alpha);
    weather->presentation_frame_into(result.weather[0], presentation_alpha);
    if (second) {
        second->weather->presentation_frame_into(result.weather[1], presentation_alpha);
    }
    result.parts = damage.parts().presentation_parts(presentation_alpha);
    result.sparks = sparks.presentation_lines(presentation_alpha);
    surface_effects.presentation_frame_into(result.surface_effects, presentation_alpha, poses);
    skid_marks.presentation_frame_into(result.skid_marks, poses);
    result.engine_effects = engine_effects.presentation_frame(poses);
    presentation_weather_position = result.weather[0].particles.empty() ? scene::WeatherVector{} : result.weather[0].particles.front();
    presentation_surface_position = {};
    for (const auto& batch : result.surface_effects.batches) {
        if (!batch.sprites.empty()) {
            presentation_surface_position = batch.sprites.front().center;
            break;
        }
    }
    for (std::size_t axis = 0; axis < presentation_body_position.size(); ++axis) {
        presentation_body_position[axis] = result.cars[0].object_to_world[12 + axis];
    }
    if (!resources.moving_horizon.moving_roots.empty()) {
        const auto& transform = result.horizon.moving.transforms.at(resources.moving_horizon.moving_roots.front());
        std::copy_n(transform.begin() + 12, 3, presentation_horizon_position.begin());
    }
    if (resources.moving_horizon.rotorcraft_root != UINT32_MAX) {
        const auto& transform = result.horizon.rotorcraft.transforms.at(resources.moving_horizon.rotorcraft_root);
        std::copy_n(transform.begin() + 12, 3, presentation_rotorcraft_position.begin());
    }
    return result;
}

void SceneSession::refresh_visibility() {
    if (view) {
        const auto ghost = ghost_pose();
        std::array<scene::RivalPose, 15> rival_storage;
        view->update_visibility(view_camera(), {motion.state().body.object_to_world, motion.controls().steering_angle}, horizon_actors.frame(), props.frame(),
            ghost ? &*ghost : nullptr, rival_poses(rival_storage));
        scene_context = view->scene_context(local_wheel_actor());
        motion.set_damage_registration(scene_context.actors[local_wheel_actor()].physical.sector);
        if (second && second->view) {
            const std::array others{scene::RivalPose{0, car_pose(0)}};
            second->view->update_visibility(second_camera(), car_pose(1), horizon_actors.frame(), props.frame(), nullptr, others);
            second->scene_context = second->view->scene_context(1);
            second->motion.set_damage_registration(second->scene_context.actors[1].physical.sector);
        }
        if (rivals || linkup) {
            if (linkup) {
                linkup->set_scene_context(scene_context);
            } else {
                rivals->set_scene_context(scene_context);
            }
        }
        engine_visibility_masks.fill(0);
        for (std::uint32_t actor = 0; actor < actor_count(); ++actor) {
            engine_visibility_masks[actor] = engine_effect_visibility(actor);
        }
    }
}

std::optional<scene::CarPose> SceneSession::ghost_pose() const {
    if (!time_trial) {
        return std::nullopt;
    }
    const auto playback = time_trial->playback();
    if (!playback) {
        return std::nullopt;
    }
    scene::CarPose result{};
    result.remap_livery = false; // 4437f0 loads the ghost without49a780's team-pair remap.
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.object_to_world[row * 4 + axis] = float(playback->basis_q16[row][axis]) / 65536.f;
        }
        result.object_to_world[12 + row] = float(playback->position_q16[row]) / 65536.f;
    }
    result.object_to_world[15] = 1;
    return result;
}

void SceneSession::update_hud() {
    hud->update(motion.state().speed_integer, engine_inputs().rpm, motion.controls().gear, race.start_phase());
    const auto with_split = [this](scene::HudSession values, std::uint32_t player) {
        if (race_progress) {
            const auto& split = race_progress->checkpoint_display(player);
            values.split_active = split.active != 0 && !race_progress->finished(player == 0 ? qualifying_data.local_actor : 1);
            values.split_ticks = split.ticks;
            values.split_text = split.text.data();
        }
        return values;
    };
    hud->set_session(with_split({qualifying, race.lap(), lap_limit, race.lap_display_ms(), time_trial ? time_trial->best_lap_ms() : race.best_lap_ms(), mode,
                                    race_progress ? race_progress->position(qualifying_data.local_actor) : 0},
        0));
    if (second) {
        const auto& session = race_progress->participant(1);
        second->hud->update(second->motion.state().speed_integer, engine_inputs(1).rpm, second->motion.controls().gear, session.start_phase());
        second->hud->set_session(with_split({false, session.lap(), lap_limit, session.lap_display_ms(), session.best_lap_ms(), mode, race_progress->position(1)}, 1));
    }
}
} // namespace toca
