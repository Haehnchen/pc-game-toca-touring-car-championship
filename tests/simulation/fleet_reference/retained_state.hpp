#pragma once

#include "record.hpp"
#include "simulation/source_session_start.hpp"

namespace toca::test::fleet {
using namespace toca::simulation;

struct AiRouteStepResults {
    std::array<AiRouteState, 16> routes;
    std::array<AiRouteProgress, 16> progress;
    std::array<AiRouteCadence, 16> cadence;
};

using AiDrivers = std::array<std::optional<AiDriver>, 16>;

struct RetainedRaceTiming {
    bool initialized = false;
    bool local_session = false;
    bool warm_restart = false, restart_requested = false, warm_pending = false, warm_grid_first = false;
    bool cold_start = false, cold_first = false, cold_active_entry = false, created_origins = false;
    std::uint32_t warm_generations = 0;
    std::uint16_t start_phase = 0;
    bool release_due = false, primary_update = false;
    std::int32_t stage = 0, state = 0;
    std::uint32_t lap_limit = 0, flags = 0, running = 0, release_clock = 0;
    RaceFinishState finish;
    CheckpointTimes checkpoints;
    SplitDisplays displays;
    SourceTimerBank timers;
    std::uint32_t release_marker = 0, local_actor = 0;
    std::array<std::uint32_t, 4> release_flags{};
    std::array<std::uint8_t, 16> classification_order{}, split_enable{};
    std::array<std::uint8_t, 48> configuration_tail{};
    std::int32_t lap_distance = 0;
    std::optional<ControllerHandoffState> handoff;
    std::uint32_t handoff_rng = 0;
    SourceSnapshot controller_checkpoint;
    std::int32_t launch_at_ai_return = 0;

    void primary(std::uint32_t cached_clock);

    void before_update(std::uint32_t clock);

    void after_update();

    void consume_menu_input();
};

struct RetainedDrivers {
    AiDrivers drivers;
    std::array<SourceSnapshot, 16> actors;
    std::array<std::uint8_t, 24> status;
    std::array<std::uint8_t, 1212> braking;
    bool initialized = false;
    bool retain_published_brake = false;
    bool retain_drivetrain = false;
    bool retain_rpm = false;
    bool retain_pedals = false;
    bool retain_steering = false;
    bool retain_physical = false;
    bool retain_dispatch = false;
    bool reset_controls = false;
};

} // namespace toca::test::fleet
