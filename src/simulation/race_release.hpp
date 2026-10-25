#pragma once

#include <cstdint>
#include <stdexcept>

namespace toca::simulation {
//4d37b0 begins at zero in460170/443f60.448130 installs the launch
//interval; only the active4482d0 wrapper decrements it before421aa0.
class RaceReleaseCounter {
    std::uint32_t counter_ = 0;

public:
    //A bounded observed interval can begin after the countdown wrapper has
    //already decremented. Import that first PRE value once, then retain it.
    explicit RaceReleaseCounter(std::uint32_t initialized = 0) : counter_(initialized) {
        if (initialized > 360) {
            throw std::runtime_error("Source release initialization exceeds its360-tick countdown");
        }
    }

    void enter(std::int32_t stage, std::uint32_t source_mode, std::uint32_t local_players = 1) {
        counter_ = local_players > 0 && (stage == 2 || stage == 4) && source_mode != 2 ? 360u : 0u;
    }

    void before_update(std::uint32_t session_state) {
        if (session_state == 6 && counter_ > 0) {
            --counter_;
        }
    }

    std::uint32_t value() const {
        return counter_;
    }
};
} // namespace toca::simulation
