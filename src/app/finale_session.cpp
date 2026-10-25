#include "app/finale_session.hpp"

#include "app/audio_timeline.hpp"
#include "app/runtime_resources.hpp"
#include "scene/presentation.hpp"
#include "scene/weather.hpp"
#include "simulation/motion.hpp"
#include "simulation/time.hpp"

#include <algorithm>

namespace toca {
std::array<std::uint32_t, 3> FinaleSession::teams(const race::ChampionshipState& championship) {
    const auto order = race::championship_standings(championship);
    return {order[0] / 2, order[1] / 2, order[2] / 2};
}

FinaleSession::FinaleSession(const AssetStore& assets, const frontend::Controller& menu, SourceRandom& random, simulation::SurfaceRoughness& roughness, std::uint32_t detail,
    std::int16_t camera_heading, std::uint32_t audio_view)
    : random(random), resources(scene::load_scene_resources(assets,
                          scene::resolve_setup({1, menu.championship_state()->player_car, 0, detail, frontend::cheats::active(scene_cheats(menu), frontend::cheats::Code::stars),
                                                   frontend::cheats::active(scene_cheats(menu), frontend::cheats::Code::toon), false, false, 11},
                              random),
                          mega_model_directory(detail))),
      finale_resources(scene::load_championship_finale_resources(assets, mega_model_directory(detail))),
      drivers(
          scene::load_qualifying_data(assets, resources.setup, 1, menu.championship_state()->driver, frontend::cheats::active(scene_cheats(menu), frontend::cheats::Code::mayhem),
              frontend::cheats::active(scene_cheats(menu), frontend::cheats::Code::impossible), 23, menu.championship_state()->showdown)),
      flags(resources.flags, random), horizon(resources.moving_horizon),
      flow(resources, drivers, finale_resources.credits, teams(*menu.championship_state()), random, roughness, camera_heading), retained_audio_view(audio_view),
      local_actor(menu.championship_state()->local_actor), mirror(menu.championship_state()->showdown),
      upside(frontend::cheats::active(scene_cheats(menu), frontend::cheats::Code::upside)) {}

void FinaleSession::update_audio(bool advance_queues) {
    const auto frame = flow.frame();
    const auto camera_view = frame.camera_phase == 0 ? retained_audio_view : 3;
    const auto changed = engine->set_camera(camera_view);
    const auto camera = scene::weather_camera(frame.camera_world_to_view);
    const audio::SpatialListener listener{camera.position, audio::source_listener_heading(camera.forward)};
    std::array<audio::RivalEngineInput, 2> motors{};
    std::array<audio::DrivingInputs, 2> wheel_inputs;
    std::size_t motor_count = 0;
    for (std::uint32_t actor = 0; actor < 2; ++actor) {
        const auto& state = flow.actor(actor);
        auto& input = wheel_inputs[actor];
        input.listener = listener;
        input.random = &random;
        input.local_player = actor == local_actor;
        input.skid_queue = advance_queues ? wheel_queues[actor].queue : audio::SourceWheelQueue{};
        // State18's speed override silences WHIRR, but both live actors
        // still dispatch their actual wheel contacts through421980.
        for (std::size_t wheel = 0; wheel < input.wheels.size(); ++wheel) {
            input.wheels[wheel] = {state.body.contacts[wheel], state.rumble[wheel], state.rumble_strength_q16[wheel], !state.wheel_contact[wheel], state.surface_ids[wheel],
                state.longitudinal_slip_q16[wheel], state.lateral_slip_q16[wheel]};
        }
        driving[actor]->set_camera(camera_view, changed, frame.camera_phase != 0);
        if (input.local_player) {
            driving[actor]->update_contacts(input);
        }
        auto& next = wheel_queues[actor];
        next.identity = std::uint8_t(actor);
        next.human = input.local_player;
        next.visibility = 1;
        for (std::size_t wheel = 0; wheel < 4; ++wheel) {
            next.input.longitudinal_slip[wheel] = state.longitudinal_slip_q16[wheel];
            next.input.lateral_slip[wheel] = state.lateral_slip_q16[wheel];
            next.input.airborne[wheel] = std::uint8_t(!state.wheel_contact[wheel]);
        }
        if (actor != local_actor) {
            const auto& control = flow.controls(actor);
            const auto motor = audio::engine_inputs(control.engine_speed_q16, control.rpm_scale_q16, control.throttle_q16, control.gear);
            motors[motor_count++] = {actor, 0, state.body.position, motor.rpm};
        }
    }
    for (std::size_t actor = 0; actor < driving.size(); ++actor) {
        driving[actor]->update_queued_skids(wheel_inputs[actor]);
    }
    // 44a6f0 retains the local motor in State18, after queued tire audio.
    engine->update({2000, 0, 0, 0}, random);
    for (std::size_t actor = 0; actor < driving.size(); ++actor) {
        if (wheel_inputs[actor].local_player) {
            wheel_inputs[actor].shift_ticks = engine->shift_ticks();
            driving[actor]->update_wind(wheel_inputs[actor]);
        }
    }
    rival_audio->update(std::span(motors).first(motor_count), listener, frame.camera_phase == 0 ? retained_audio_view : 0);
    if (advance_queues) {
        (void)audio::prepare_source_wheel_queues(wheel_queues);
    }
}

void FinaleSession::advance(std::uint64_t now, AudioTimeline& audio) {
    if (!started || now <= previous_ns) {
        return;
    }
    const auto elapsed = now - previous_ns;
    previous_ns = now;
    if (active) {
        pending_ns += elapsed;
        while (pending_ns >= simulation::LiveSchedule::interval_ns) {
            pending_ns -= simulation::LiveSchedule::interval_ns;
            audio.advance(now - pending_ns);
            previous_presentation = current_presentation;
            flow.advance(simulation::LiveSchedule::interval_ns);
            current_presentation = flow.frame();
            presentation_valid = true;
            flags.update();
            horizon.update();
            update_audio();
        }
    }
}

scene::ChampionshipFinaleFrame FinaleSession::presentation_frame() {
    presentation_alpha = !active || !presentation_valid || previous_presentation.camera_cut != current_presentation.camera_cut
                             ? 1.f
                             : float(std::min(pending_ns, simulation::LiveSchedule::interval_ns)) / float(simulation::LiveSchedule::interval_ns);
    auto result = scene::interpolate_championship_finale(previous_presentation, current_presentation, presentation_alpha);
    std::copy_n(result.pace.object_to_world.begin() + 12, 3, presentation_pace_position.begin());
    return result;
}

void FinaleSession::activate(std::uint64_t now, bool focused, AudioTimeline& audio) {
    advance(now, audio);
    previous_ns = std::max(previous_ns, now);
    if (started && active == focused) {
        return;
    }
    started = true;
    active = focused;
    previous_presentation = current_presentation = flow.frame();
    presentation_valid = false;
    if (active) {
        engine->set_camera(flow.frame().camera_phase == 0 ? retained_audio_view : 3);
        engine->start();
        driving[0]->start();
        driving[1]->start(false); // Source has one local WHIRR channel.
        update_audio(false);
    } else {
        engine->stop();
        for (auto& wheel_audio : driving) {
            wheel_audio->stop();
        }
        rival_audio->stop();
        wheel_queues = {};
    }
}
} // namespace toca
