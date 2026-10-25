#pragma once

#include <cstdint>
#include <iosfwd>

namespace toca {
// Diagnostic driving actions per local player: steering, pedals and manual gears.
inline constexpr std::uint32_t step_input_actions = 0x30f;

struct StepInputCommand {
    bool live = false;
    std::uint32_t held = 0;
    std::uint32_t held_player2 = 0;
};

StepInputCommand read_step_input(std::istream& input);
void write_step_ready(std::ostream& output, std::uint64_t step);

// The normal host clock until a diagnostic command selects the next deadline.
// Returning to live time preserves the logical epoch instead of adding the
// time spent waiting for the test driver or moving the application backwards.
class StepInputClock {
    std::uint64_t wall_anchor_ = 0, logical_anchor_ = 0, previous_ = 0;
    bool stepping_ = false;

public:
    std::uint64_t now(std::uint64_t wall_ns);
    std::uint64_t event_time(std::uint64_t wall_timestamp_ns) const;
    void advance_to(std::uint64_t deadline_ns);
    void resume(std::uint64_t wall_ns);
    bool stepping() const;
};
} // namespace toca
