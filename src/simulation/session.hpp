#pragma once

#include "core/game_mode.hpp"
#include "simulation/race_release.hpp"
#include "simulation/route_progress.hpp"
#include "simulation/source_session_start.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace toca::simulation {
enum class SessionEvent {
    red,
    go,
    released,
    lap_complete,
    attempt_restarted,
    qualifying_finished,
    results_entered,
    race_requested,
    race_finished,
    race_return_requested
};

// One actual Single Race/qualifying/Time Trial actor's route and lap records.
// elapsed_ms is cumulative ACTIVE scene time, never presentation time or delta.
class Session {
    struct Point {
        std::array<std::int32_t, 3> first{}, second{};
    };

    std::vector<Point> points_;
    NearestRouteMap nearest_route_;
    RaceReleaseCounter release_counter_;
    std::optional<std::uint8_t> route_refresh_period_;
    std::uint8_t route_refresh_ticks_ = 0;
    bool human_control_ = true;
    std::array<std::uint32_t, 6> checkpoints_{};
    std::uint32_t next_ = 0, previous_ = 0, checkpoint_ = 0, checkpoint_marker_ = 2;
    std::optional<std::uint32_t> split_sector_;
    std::array<std::int16_t, 3> split_components_{};
    SourceSessionStart start_;
    std::uint32_t lap_ = 1, completed_ = 0, lap_limit_ = 0;
    std::uint32_t best_ms_ = 0, last_ms_ = 0, lap_ms_ = 0;
    std::uint64_t now_ms_ = 0, lap_start_ms_ = 0;
    std::uint64_t finish_ms_ = 0, results_ms_ = 0, race_start_ms_ = 0;
    std::uint64_t finish_presentation_ms_ = 0;
    std::uint32_t finish_state_ = 0;
    std::uint32_t local_players_ = 1;
    GameMode mode_;
    bool qualifying_, qualifying_rival_ = false, qualifying_laps_frozen_ = false;
    bool released_ = false, backwards_ = false, initial_start_pending_ = false;
    bool start_commit_pending_ = false;
    bool race_return_requested_ = false;
    bool finish_speech_started_ = false, finish_speech_playing_ = false;
    bool finish_group_complete_ = true;
    bool network_finish_authority_ = true;
    std::optional<std::uint32_t> network_finish_ms_;
    bool passed(std::uint32_t point, const std::array<std::int32_t, 3>& position) const;
    void cross_start(std::vector<SessionEvent>& events);

public:
    Session(std::span<const std::uint8_t> route_bytes, bool qualifying, GameMode mode = GameMode::single_race, std::uint32_t lap_limit = 0);
    //443d30 starts each permanent actor seven POINTS records apart. NPCs
    //retain AI ownership and never trigger the local qualifying handback/finish.
    void configure_qualifying_actor(std::uint32_t local_actor, std::uint32_t actor);
    void freeze_qualifying_laps();
    //Configure once from the resolved driver's byte19, not on controller handback.
    void configure_route_recovery(std::uint8_t refresh_period);
    void set_human_control(bool human);
    // 403dd0 resets route cursors at PITS entry, preserving lap/checkpoint state.
    void enter_pit();
    // 441720 action3 enters the ordinary finish flow, preserving completed laps.
    bool end_qualifying();
    std::vector<SessionEvent> update(std::uint64_t elapsed_ms, const std::array<std::int32_t, 3>& position_q16, bool confirm = false);
    void set_finish_speech(bool started, bool playing);
    // 421390 waits for every local human before entering the global finish.
    // Keep each driver's measured finish time while the other still races.
    void set_finish_group_complete(bool complete);
    //448a20 returns local multiplayer directly to overlay3, retaining state10.
    void configure_local_players(std::uint32_t count);
    void set_network_finish_authority(bool enabled);
    void apply_network_finish(std::uint32_t centiseconds);
    std::uint32_t state() const;
    std::uint32_t start_phase() const;
    bool released() const;
    //Green installs the launch adjustment; the following state6 wrapper engages the clutch.
    bool grid_held() const;
    std::uint32_t release_counter() const;
    // 443f60/402be0 owns the qualifying approach until421390/402c80
    // restores player controls at the first ordered start-line crossing.
    bool qualifying_rollout() const;
    std::uint32_t lap() const;
    std::uint32_t completed_laps() const;
    std::uint32_t lap_time_ms() const;
    // 403dd0 publishes components before the route/lap transition. HUD reads
    // this cache even when the new lap's elapsed clock has already reset.
    std::uint32_t lap_display_ms() const;
    std::uint32_t best_lap_ms() const;
    std::uint32_t last_lap_ms() const;
    std::uint32_t race_time_ms() const;
    std::uint64_t finish_time_ms() const;
    // Global states10/13 stop421390 for all actors while route cursors continue.
    void freeze_race_laps();
    std::uint32_t next_point() const;
    std::uint32_t point_count() const;
    //4212d0 sector emitted by the latest source checkpoint update.
    std::optional<std::uint32_t> split_sector() const;
    const std::array<std::int16_t, 3>& split_components() const;
    //403dd0's +0c start/backwards flag, used by402e90 position exchanges.
    bool start_line_pending() const;
    bool finish_camera_active() const;
};

// 44a190: original timer slots quantize millisecond differences to1/16 second.
std::uint32_t session_timer_q16(std::uint64_t elapsed_ms);
} // namespace toca::simulation
