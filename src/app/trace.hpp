#pragma once

#include "audio/service.hpp"
#include "core/contracts.hpp"
#include "platform/display.hpp"
#include "render/gpu_renderer.hpp"

#include <fstream>

namespace toca {
struct SceneSession;
struct FinaleSession;

namespace frontend {
class Controller;
}

namespace audio {
class Championship;
}

namespace race {
struct ChampionshipState;
}

namespace simulation {
class SurfaceRoughness;
}

struct ExtrasChanges {
    std::uint64_t saved = 0, announcements = 0;
    std::uint32_t sample = 0;
    audio::Voice announcement_voice = 0;
};

struct SettingsChanges {
    std::uint64_t requests = 0, saved = 0;
    bool error = false;
};

class Trace {
    const simulation::SurfaceRoughness& roughness_;
    std::ofstream output_;
    std::ofstream simulation_;

    void session_values(std::ostream& stream, const SceneSession* scene, const race::ChampionshipState* championship = nullptr);

public:
    Trace(const std::filesystem::path& path, const simulation::SurfaceRoughness& roughness);

    void record(std::uint64_t poll, std::uint64_t frame, bool presented, const ClockStep& clock, platform::Extent drawable, const InputSnapshot& input, bool minimized,
        const Settings& settings, const audio::Service& audio, const frontend::Controller& frontend, std::uint64_t frontend_cues, std::uint32_t loading_step,
        std::size_t scene_vertices, const SceneSession* scene, const SettingsChanges& changes, const ExtrasChanges& extras, const audio::Championship* championship_commentary,
        const FinaleSession* finale, const gpu::FrameTiming& frame_timing = {}, std::uint64_t frame_prepare_ns = 0);

    void record_simulation(std::uint64_t timestamp, const SceneSession& session, const InputSnapshot& input);
};
} // namespace toca
