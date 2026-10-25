#pragma once

#include "scene/qualifying.hpp"
#include "simulation/race_timing.hpp"
#include "simulation/session.hpp"

#include <optional>

namespace toca::simulation {
struct QualifyingStartInitResult;
}

namespace toca::race {
using RacePositions = std::array<std::array<std::int32_t, 3>, 16>;

struct RaceEntry {
    std::uint32_t actor_id = 0, car_id = 0, time_ms = 0, completed_laps = 0;
    std::string driver;
    bool finished = false, retired = false;
};

struct RaceResults {
    std::array<RaceEntry, 16> rows;
    std::uint32_t local_actor = 0, local_rank = 0;
    std::uint32_t count = 16;
};

// Real POINTS/checkpoint records for the15 rivals. The application retains its
// existing local Session. Poses are consumed before physics, as in403dd0.
class RaceProgress {
    std::array<std::optional<simulation::Session>, 16> rivals_;
    std::array<std::uint32_t, 16> order_{}, finish_order_{}, completed_{}, points_{};
    std::array<std::uint32_t, 16> ranks_{}, spatial_ranks_{}, ahead_{}, behind_{};
    std::array<std::uint32_t, 16> times_{};
    std::array<bool, 16> finished_{};
    std::array<bool, 16> crossed_start_{}, removed_{}, retired_{};
    std::vector<std::array<std::int32_t, 3>> targets_;
    std::vector<std::int32_t> distances_;
    std::array<std::int32_t, 16> distance_{};
    std::array<std::int32_t, 16> distance_delta_{};
    std::uint32_t local_actor_, finish_count_ = 0;
    std::uint32_t actor_count_ = 16;
    GameMode mode_ = GameMode::single_race;
    bool qualifying_ = false;
    bool classified_ = false, stop_horns_ = false;
    simulation::CheckpointTimes checkpoint_times_;
    simulation::SplitDisplays split_displays_;
    std::int32_t route_distance(std::uint32_t point, const std::array<std::int32_t, 3>& position) const;
    void update_positions();
    RaceProgress(std::span<const std::uint8_t> route, std::uint32_t lap_limit, std::uint32_t local_actor, const std::array<std::uint32_t, 16>& grid_order,
        std::uint32_t actor_count, GameMode mode, bool qualifying);

public:
    RaceProgress(std::span<const std::uint8_t> route, std::uint32_t lap_limit, std::uint32_t local_actor, const std::array<std::uint32_t, 16>& grid_order,
        std::uint32_t actor_count = 16, GameMode mode = GameMode::single_race);
    RaceProgress(std::span<const std::uint8_t> route, std::uint32_t lap_limit, const simulation::QualifyingStartInitResult&);
    simulation::Session& participant(std::uint32_t actor);
    const simulation::Session& participant(std::uint32_t actor) const;
    void update(std::uint64_t active_ms, const simulation::Session& local, const RacePositions& positions);
    void apply_network_finish(std::uint32_t actor, std::uint32_t centiseconds, std::uint32_t ordinal, simulation::Session& local);
    void finish_network_classification(const simulation::Session& local);
    std::array<std::uint32_t, 16> next_points(const simulation::Session& local) const;
    //414ef0 follows actor+96/+97 spatial neighbors, independently of race
    //positions/finish order. Ahead is previous, behind next, with wrap.
    std::array<std::uint32_t, 16> spatial_order() const;
    const std::array<std::int32_t, 16>& route_distances() const;
    std::int32_t total_route_distance() const;
    const simulation::CheckpointTimes& checkpoint_times() const;
    const simulation::SplitDisplayState& checkpoint_display(std::uint32_t player = 0) const;
    bool stop_horns_requested() const;
    std::uint32_t position(std::uint32_t actor) const; //1..16
    bool finished(std::uint32_t actor) const;
    //401650 unlinks an inactive actor; only unfinished actors retire.
    void remove(std::uint32_t actor);
    bool active(std::uint32_t actor) const;
    bool retired(std::uint32_t actor) const;
    std::uint32_t completed_laps(std::uint32_t actor) const;
    bool classified() const;
    RaceResults results(const scene::QualifyingData& drivers) const;
};
} // namespace toca::race
