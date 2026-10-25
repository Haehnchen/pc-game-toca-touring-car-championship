#pragma once

#include "simulation/source_session_start.hpp"
#include "simulation/time.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace toca::test::fleet {
using namespace toca::simulation;

class ClockTransactions {
    struct Operation {
        std::uint32_t id, order, kind, value;
    };

    std::optional<SourceClock> clock_;
    TimeStep retained_;
    TimeStep initial_;
    std::int32_t divisor_ = 0;
    std::uint32_t updates_ = 0;
    bool scene_owned_ = false, terminal_sampled_ = false;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> samples_;
    std::vector<Operation> operations_;
    std::size_t next_sample_ = 0;
    std::size_t next_operation_ = 0, observed_operation_ = 0;
    std::uint32_t next_update_ = 0;
    std::uint32_t frame_physical_ = 0;
    bool sampled_frame_ = false;
    bool extra_pending_ = false;
    SourceSessionStart start_;
    std::vector<std::uint32_t> caller_clocks_;
    std::uint32_t session_state_ = 9, release_epoch_ = 0;
    bool primary_ = true, pending_active_ = false, active_entry_ = true, release_due_ = false;
    bool created_origins_ = false;
    const SourceTimerBank* shared_timers_ = nullptr;
    const std::uint16_t* shared_phase_ = nullptr;
    const std::int32_t* shared_state_ = nullptr;
    const std::uint32_t* shared_release_ = nullptr;
    const bool* shared_release_due_ = nullptr;

public:
    ClockTransactions(const char* path, std::uint32_t updates, bool scene_owned = false);
    TimeStep step(std::uint32_t id);
    std::uint32_t caller_clock() const;
    std::uint32_t session_state() const;
    std::uint32_t update_caller() const;
    bool release_due() const;
    void share_lifecycle(SourceTimerBank& timers, const std::uint16_t& phase, const std::int32_t& state, const std::uint32_t& release, const bool& release_due);
    bool primary() const;
    bool created_origins() const;
    const TimeStep& initial_step() const;
    std::int32_t divisor() const;
    void observe_operation(std::uint32_t id, std::uint32_t kind, std::optional<std::uint32_t> value = std::nullopt);
    std::array<std::uint32_t, 9> lifecycle_values() const;
    void finish_update();
    void finish() const;
};

} // namespace toca::test::fleet
