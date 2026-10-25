#include "app/step_input.hpp"

#include <charconv>
#include <istream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>

namespace toca {
namespace {
std::uint64_t add_time(std::uint64_t anchor, std::uint64_t elapsed) {
    if (elapsed > std::numeric_limits<std::uint64_t>::max() - anchor) {
        throw std::runtime_error("Step input clock overflow");
    }
    return anchor + elapsed;
}
} // namespace

StepInputCommand read_step_input(std::istream& input) {
    std::string line;
    char character;
    for (;;) {
        if (!input.get(character)) {
            throw std::runtime_error("Step input reached EOF before a complete command");
        }
        if (character == '\n') {
            break;
        }
        if (line.size() == 64) {
            throw std::runtime_error("Step input command exceeds64 bytes");
        }
        line.push_back(character);
    }
    if (line == "live") {
        return {.live = true};
    }
    if (!line.starts_with("step ")) {
        throw std::runtime_error("Step input expects 'step HELD_MASK [PLAYER2_MASK]' or 'live'");
    }
    StepInputCommand command;
    const auto first = line.data() + 5, last = line.data() + line.size();
    const auto parsed = std::from_chars(first, last, command.held);
    if (parsed.ec != std::errc{} || (command.held & ~step_input_actions) != 0) {
        throw std::runtime_error("Step input requires an unsigned decimal driving-key mask");
    }
    if (parsed.ptr != last) {
        if (*parsed.ptr != ' ') {
            throw std::runtime_error("Step input requires space-separated driving-key masks");
        }
        const auto second = std::from_chars(parsed.ptr + 1, last, command.held_player2);
        if (second.ec != std::errc{} || second.ptr != last || (command.held_player2 & ~step_input_actions) != 0) {
            throw std::runtime_error("Step input requires an unsigned decimal player2 driving-key mask");
        }
    }
    return command;
}

void write_step_ready(std::ostream& output, std::uint64_t step) {
    output << "TOCA_STEP " << step << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error("Writing step input readiness failed");
    }
}

std::uint64_t StepInputClock::now(std::uint64_t wall_ns) {
    if (!stepping_ && wall_ns < wall_anchor_) {
        throw std::runtime_error("Step input wall clock moved backwards");
    }
    const auto current = stepping_ ? logical_anchor_ : add_time(logical_anchor_, wall_ns - wall_anchor_);
    if (current < previous_) {
        throw std::runtime_error("Step input application clock moved backwards");
    }
    previous_ = current;
    return current;
}

std::uint64_t StepInputClock::event_time(std::uint64_t wall_timestamp_ns) const {
    if (stepping_ || wall_timestamp_ns <= wall_anchor_) {
        return logical_anchor_;
    }
    return add_time(logical_anchor_, wall_timestamp_ns - wall_anchor_);
}

void StepInputClock::advance_to(std::uint64_t deadline_ns) {
    if (deadline_ns <= previous_) {
        throw std::runtime_error("Step input must advance to the next simulation deadline");
    }
    logical_anchor_ = deadline_ns;
    previous_ = deadline_ns;
    stepping_ = true;
}

void StepInputClock::resume(std::uint64_t wall_ns) {
    logical_anchor_ = previous_;
    wall_anchor_ = wall_ns;
    stepping_ = false;
}

bool StepInputClock::stepping() const {
    return stepping_;
}
} // namespace toca
