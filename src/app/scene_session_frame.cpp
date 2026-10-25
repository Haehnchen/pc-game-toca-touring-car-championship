#include "app/scene_session.hpp"
#include "frontend/network_status.hpp"
#include "scene/split_screen.hpp"

namespace toca {
SceneSession::PreparedFrame SceneSession::prepare_frame(
    gpu::Renderer& renderer, std::uint32_t width, std::uint32_t height, bool vertical, std::uint64_t now, bool exact_tick, std::uint32_t language) {
    auto& session = *this;
    std::size_t scene_vertices = 0;
    session.set_viewport(width, height, vertical);
    const auto& presentation = session.presentation_frame(now, exact_tick);
    if (session.second) {
        const auto panes = scene::split_viewports(width, height, vertical);
        auto& combined = session.combined_frame;
        combined.clear();
        for (std::uint32_t player = 0; player < 2; ++player) {
            const auto& pane = panes[player];
            const auto& race = player == 0 ? session.race : session.race_progress->participant(1);
            const auto& pit = player == 0 ? session.pit : session.second->pit;
            auto& scene_view = player == 0 ? session.view : session.second->view;
            const auto& camera = presentation.cameras[player];
            const std::array other{scene::RivalPose{1 - player, presentation.cars[1 - player]}};
            auto& frame = session.presentation_frames[player];
            scene_view->frame_into(frame, camera, scene::scene_lighting(session.resources.setup), presentation.cars[player], session.flags.variants(), presentation.horizon,
                presentation.props, presentation.fragments, &presentation.weather[player], presentation.parts, presentation.sparks, &presentation.surface_effects,
                &presentation.skid_marks, nullptr, false, &presentation.engine_effects, other, session.presentation_alpha);
            if (frontend::cheats::active(session.cheats, frontend::cheats::Code::upside)) {
                frame.reflect_world(float(pane.width), float(pane.height), false, true);
            }
            const auto overlay_start = frame.draws.size();
            if (!session.finishing()) {
                (player == 0 ? session.hud : session.second->hud)->append(frame, pane.width, pane.height);
            }
            if (pit.active() && !session.finishing() && !session.pause.active()) {
                const auto notice_ns = player == 0 ? session.pit_notice_ns : session.second->pit_notice_ns;
                session.qualifying_view->append_pit(frame, pit.exiting_hint(), (session.simulation_ns - notice_ns) / 1'000'000, pane.width, pane.height);
            }
            if (race.completed_laps() >= session.lap_limit && race.state() != 13 && !session.pause.active()) {
                session.qualifying_view->append_race_finish(frame, session.race_progress->position(player), pane.width, pane.height);
            }
            scene_vertices += frame.world_vertex_count();
            frame.mark_overlay(overlay_start);
            if (player == 0) {
                combined.color_load = frame.color_load;
                combined.clear_color = frame.clear_color;
            }
            combined.append_view(frame, pane);
        }
        if (session.race.state() == 13 && !session.pause.active()) {
            auto& overlay = session.overlay_frame;
            overlay.clear();
            session.qualifying_view->append_race(overlay, session.race_results(), width, height);
            overlay.mark_overlay(0);
            combined.append_view(overlay, {0, 0, width, height});
        }
        auto& pause_overlay = session.overlay_frame;
        pause_overlay.clear();
        session.pause_view->append(pause_overlay, session.pause, width, height, session.ghost_error);
        if (!pause_overlay.vertices.empty()) {
            pause_overlay.mark_overlay(0);
            combined.append_view(pause_overlay, {0, 0, width, height});
        }
        return {combined, scene_vertices};
    } else {
        const auto& camera = presentation.cameras[0];
        const auto& ghost = presentation.ghost;
        if (session.time_trial && session.rendered_ghost_name != session.time_trial->best_driver()) {
            session.rendered_ghost_name = session.time_trial->best_driver();
            const auto name = session.name_font.render(session.rendered_ghost_name, session.name_glyphs);
            session.view->set_ghost_name(renderer, name.get());
        }
        auto& frame = session.presentation_frames[0];
        session.view->frame_into(frame, camera, scene::scene_lighting(session.resources.setup), presentation.cars[0], session.flags.variants(), presentation.horizon,
            presentation.props, presentation.fragments, &presentation.weather[0], presentation.parts, presentation.sparks, &presentation.surface_effects, &presentation.skid_marks,
            ghost ? &*ghost : nullptr, session.time_trial && session.time_trial->best_car_id() == session.resources.setup.selection.car_id, &presentation.engine_effects,
            std::span(presentation.rivals).first(presentation.rival_count), session.presentation_alpha);
        // 48f5f9 negates projection Y for CMUPSIDE. Applying it to the
        // projected 3D pass retains original culling and upright HUD.
        if (frontend::cheats::active(session.cheats, frontend::cheats::Code::upside)) {
            frame.reflect_world(float(width), float(height), false, true);
        }
        if (session.championship && session.championship->showdown) {
            // 44f0a3 sets config+24;48f5f9 negates projection X.
            // Reflect only the projected world; menus and HUD stay upright.
            frame.reflect_world(float(width), float(height), true, false);
        }
        scene_vertices = frame.world_vertex_count();
        const auto overlay_start = frame.draws.size();
        if (session.race.state() == 8) {
            if (!session.pause.active()) {
                session.qualifying_view->append(frame, *session.qualifying_results, width, height);
            }
        } else if (session.race.state() == 13) {
            if (!session.pause.active()) {
                session.qualifying_view->append_race(frame, session.race_results(), width, height);
            }
        } else {
            if (!session.race.finish_camera_active() && session.race.state() != 10) {
                session.hud->append(frame, width, height);
            }
            if (session.pit.active() && !session.finishing() && !session.pause.active()) {
                session.qualifying_view->append_pit(frame, session.pit.exiting_hint(), (session.simulation_ns - session.pit_notice_ns) / 1'000'000, width, height);
            } else if (session.race.qualifying_rollout() && !session.pause.active()) {
                session.qualifying_view->append_rollout(
                    frame, session.mode, session.race.next_point(), session.race.point_count(), (session.simulation_ns - session.session_epoch_ns) / 1'000'000, width, height);
            }
            if (session.race.state() == 10 && !session.pause.active()) {
                session.qualifying_view->append_race_finish(frame, session.race_progress->position(session.qualifying_data.local_actor), width, height);
            }
            if (session.race.state() == 7 && !session.pause.active()) {
                session.qualifying_view->append_finish(frame, width, height);
            }
        }
        const auto network_notice =
            session.linkup ? frontend::network_pause_notice(language, session.network_runtime->session().view(), session.network_runtime->session().race_view()) : std::string{};
        session.pause_view->append(frame, session.pause, width, height, session.ghost_error, network_notice);
        frame.mark_overlay(overlay_start);
        return {frame, scene_vertices};
    }
}
} // namespace toca
