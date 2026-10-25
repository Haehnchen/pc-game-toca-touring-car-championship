#pragma once

#include "scene/car_visual.hpp"
#include "scene/placement.hpp"
#include "scene/qualifying.hpp"

#include <memory>

namespace toca::simulation {
class SurfaceRoughness;
struct MotionState;
struct VehicleState;
} // namespace toca::simulation

namespace toca::scene {
struct ChampionshipFinaleCarPose {
    std::array<float, 16> object_to_world{};
    std::int16_t steering_angle = 0;
    std::array<std::int16_t, 4> wheel_angles{};
    CarVisualAngles visual_angles{};
};

struct ChampionshipFinaleCredit {
    std::string text;
    bool heading = false;
    bool operator==(const ChampionshipFinaleCredit&) const = default;
};

struct ChampionshipFinaleFrame {
    ChampionshipFinaleCarPose pace, transporter;
    // 449b6d: original standings actor/2, used directly as CHAMPFIN team index.
    std::array<std::uint32_t, 3> teams{};
    std::array<float, 16> camera_world_to_view{};
    std::array<std::int32_t, 3> camera_position_q16{};
    std::vector<ChampionshipFinaleCredit> credits;
    std::uint32_t camera_phase = 0;
    // Phase and authored-camera selection jointly identify a presentation cut.
    std::uint64_t camera_cut = 0;
    // Counts4198f0 calls, including the leading PAGE and END empty pages.
    std::uint64_t credit_page = 0;
};

// State18's quantized timer and literal CRLF/PAGE/END parser, independently
// usable without graphics/physics. No localization: CHAMPFIN has one stream.
class ChampionshipFinaleSequence {
    std::string source_;
    std::size_t cursor_ = 0;
    std::uint64_t active_ns_ = 0, ticks_ = 0, cycle_start_ms_ = 0, page_ = 0;
    std::uint32_t phase_ = 0;
    std::vector<ChampionshipFinaleCredit> lines_;
    std::string read_line();
    void next_page();

public:
    explicit ChampionshipFinaleSequence(std::span<const std::uint8_t> credits);
    void advance(std::uint64_t active_delta_ns);
    bool can_exit() const;
    std::uint64_t active_time_ms() const;
    std::uint64_t ticks() const;
    std::uint32_t camera_phase() const;
    std::uint64_t credit_page() const;
    const std::vector<ChampionshipFinaleCredit>& credits() const;
};

// 449828..449899: replace both AI targets by the edge midpoint, retaining
// every other original POINTS field. The caller's resource bytes stay immutable.
std::vector<std::uint8_t> championship_finale_route(std::span<const std::uint8_t> route);
// 4498cf/4499fe use the first GRID endpoint, without race-grid side offsets.
std::array<HorizontalPlacement, 2> championship_finale_starts(const SceneResources& resources);

// Native owner for State18's two physical actors, source camera cycle and
// 419870/4198f0 credits. Borrowed resources/RNG/roughness must outlive this object.
// Advance only ACTIVE time: focus/minimize pauses and host input stay outside.
class ChampionshipFinale {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    ChampionshipFinale(const SceneResources&, const QualifyingData&, std::span<const std::uint8_t> credits, std::array<std::uint32_t, 3> podium_teams, SourceRandom&,
        simulation::SurfaceRoughness&, std::int16_t initial_camera_heading = 0);
    ~ChampionshipFinale();
    ChampionshipFinale(const ChampionshipFinale&) = delete;
    ChampionshipFinale& operator=(const ChampionshipFinale&) = delete;
    // Accumulates exact25Hz deadlines; zero delta never advances the scene.
    void advance(std::uint64_t active_delta_ns);
    ChampionshipFinaleFrame frame() const;
    // Source timer0 resets every12s: the first4s of EACH cycle reject exit.
    bool can_exit() const;
    std::uint64_t active_time_ms() const;
    const simulation::MotionState& actor(std::uint32_t source_actor) const;
    const simulation::VehicleState& controls(std::uint32_t source_actor) const;
};
} // namespace toca::scene
