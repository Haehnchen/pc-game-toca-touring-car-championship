#pragma once

#include "audio/driving.hpp"
#include "audio/engine.hpp"
#include "audio/rivals.hpp"
#include "frontend/controller.hpp"
#include "scene/championship_finale.hpp"
#include "scene/championship_finale_view.hpp"
#include "scene/flag_animation.hpp"
#include "scene/horizon_actors.hpp"

namespace toca {
class AudioTimeline;

struct FinaleSession {
    SourceRandom& random;
    scene::SceneResources resources;
    scene::ChampionshipFinaleResources finale_resources;
    scene::QualifyingData drivers;
    scene::FlagAnimation flags;
    scene::HorizonActors horizon;
    scene::ChampionshipFinale flow;
    std::unique_ptr<scene::ChampionshipFinaleView> view;
    gpu::Frame rendered_frame;
    std::unique_ptr<audio::Engine> engine;
    std::array<std::unique_ptr<audio::Driving>, 2> driving;
    std::array<audio::SourceWheelQueueActor, 2> wheel_queues;
    std::unique_ptr<audio::Rivals> rival_audio;
    std::uint64_t previous_ns = 0, presentation_due_ns = 0, pending_ns = 0;
    scene::ChampionshipFinaleFrame previous_presentation{}, current_presentation{};
    std::array<float, 3> presentation_pace_position{};
    float presentation_alpha = 1;
    bool presentation_valid = false;
    std::uint32_t retained_audio_view = 2, local_actor = 0;
    bool started = false, active = false, mirror = false, upside = false;

    static std::array<std::uint32_t, 3> teams(const race::ChampionshipState& championship);

    FinaleSession(const AssetStore& assets, const frontend::Controller& menu, SourceRandom& random, simulation::SurfaceRoughness& roughness, std::uint32_t detail,
        std::int16_t camera_heading, std::uint32_t audio_view);

    void update_audio(bool advance_queues = true);

    void advance(std::uint64_t now, AudioTimeline& audio);

    scene::ChampionshipFinaleFrame presentation_frame();

    void activate(std::uint64_t now, bool focused, AudioTimeline& audio);
};
} // namespace toca
