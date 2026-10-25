#pragma once

#include <cstdint>
#include <vector>

namespace toca::simulation {
struct TimeStep {
    std::int32_t delta_q16 = 65536;
    std::int32_t reciprocal_q16 = 65536;
    bool extra_step = false;
    std::uint32_t update_count() const;
};

// 412ab0's final XBOOSTME transform; pacing and deadlines are unchanged.
TimeStep boosted_step(TimeStep step, bool boost, bool special_mode = false);

// 412a80/412ab0, with explicit recorded low-32 performance-counter values.
class SourceClock {
    std::int32_t divisor_;
    std::uint32_t previous_ = 0;
    bool first_ = true, extra_step_ = false;
    SourceClock(std::int32_t divisor, bool retained_extra);

public:
    explicit SourceClock(std::uint32_t frequency);
    // Recorded412a80 output is an external clock input, not a recovered QPF.
    static SourceClock from_stored_divisor(std::int32_t divisor, bool retained_extra = false);
    void reset();
    //A later412a80 samples QPF again while preserving the old extra latch.
    void reset(std::int32_t stored_divisor);
    TimeStep sample(std::uint32_t counter_low, bool boost = false, bool special_mode = false);
};

// Native adaptation: fixed 25 Hz deadlines, independent of presentation.
// Up to eight due deadlines per call; outstanding debt is never discarded.
class LiveSchedule {
    std::uint64_t next_ = 0;
    bool initialized_ = false;

public:
    static constexpr std::uint64_t interval_ns = 40'000'000;
    void reset(std::uint64_t now_ns);
    std::uint64_t next_deadline_ns() const;
    std::vector<std::uint64_t> take_due(std::uint64_t now_ns);
};
} // namespace toca::simulation
