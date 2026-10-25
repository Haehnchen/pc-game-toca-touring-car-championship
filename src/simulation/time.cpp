#include "simulation/time.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace toca::simulation {
std::uint32_t TimeStep::update_count() const {
    return extra_step ? 2 : 1;
}

TimeStep boosted_step(TimeStep step, bool boost, bool special_mode) {
    if (boost && !special_mode) {
        step.delta_q16 = std::bit_cast<std::int32_t>(std::uint32_t(step.delta_q16) * 2);
        if (step.delta_q16 > 0x30000) {
            step.delta_q16 = 0x30000;
        }
        step.reciprocal_q16 /= 2;
    }
    return step;
}

SourceClock::SourceClock(std::uint32_t frequency) {
    if (frequency < 25 || frequency > std::uint32_t(std::numeric_limits<std::int32_t>::max())) {
        throw std::runtime_error("Source clock needs a positive signed low-32 frequency divided by 25");
    }
    divisor_ = std::int32_t(frequency) / 25;
}

SourceClock::SourceClock(std::int32_t divisor, bool retained_extra) : divisor_(divisor), extra_step_(retained_extra) {
    if (divisor <= 0) {
        throw std::runtime_error("Source clock needs a positive stored divisor");
    }
}

SourceClock SourceClock::from_stored_divisor(std::int32_t divisor, bool retained_extra) {
    return SourceClock(divisor, retained_extra);
}

void SourceClock::reset() {
    // 412a80 resets only its first-sample flag, not the previous extra-step flag.
    first_ = true;
}

void SourceClock::reset(std::int32_t stored_divisor) {
    if (stored_divisor <= 0) {
        throw std::runtime_error("Source clock needs a positive stored divisor");
    }
    divisor_ = stored_divisor;
    reset();
}

TimeStep SourceClock::sample(std::uint32_t counter_low, bool boost, bool special_mode) {
    TimeStep step;
    if (first_) {
        first_ = false;
    } else {
        const auto difference = counter_low - previous_ - std::uint32_t(previous_ >= counter_low);
        const auto ticks = std::bit_cast<std::int32_t>(difference);
        step.delta_q16 = std::bit_cast<std::int32_t>(std::uint32_t(std::int64_t(ticks) * 65536 / divisor_));
        if (step.delta_q16 <= 0x18000) {
            extra_step_ = false;
        } else {
            extra_step_ = true;
            step.delta_q16 = step.delta_q16 <= 0x30000 ? step.delta_q16 / 2 : 0x18000;
        }
        if (step.delta_q16 == 0) {
            throw std::runtime_error("Source clock delta would divide by zero");
        }
        step.reciprocal_q16 = std::bit_cast<std::int32_t>(std::uint32_t(INT64_C(0x100000000) / step.delta_q16));
    }
    previous_ = counter_low;
    step.extra_step = extra_step_;
    return boosted_step(step, boost, special_mode);
}

void LiveSchedule::reset(std::uint64_t now_ns) {
    if (now_ns > std::numeric_limits<std::uint64_t>::max() - interval_ns) {
        throw std::runtime_error("Simulation deadline overflows the monotonic clock");
    }
    next_ = now_ns + interval_ns;
    initialized_ = true;
}

std::vector<std::uint64_t> LiveSchedule::take_due(std::uint64_t now_ns) {
    if (!initialized_) {
        throw std::runtime_error("Simulation schedule must start at an explicit clock boundary");
    }
    std::vector<std::uint64_t> deadlines;
    while (next_ <= now_ns && deadlines.size() < 8) {
        if (next_ > std::numeric_limits<std::uint64_t>::max() - interval_ns) {
            throw std::runtime_error("Simulation deadline overflows the monotonic clock");
        }
        deadlines.push_back(next_);
        next_ += interval_ns;
    }
    return deadlines;
}

std::uint64_t LiveSchedule::next_deadline_ns() const {
    if (!initialized_) {
        throw std::runtime_error("Simulation schedule must start at an explicit clock boundary");
    }
    return next_;
}
} // namespace toca::simulation
