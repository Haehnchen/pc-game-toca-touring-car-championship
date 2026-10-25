#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace toca::simulation {
//44a170/44a190 use the cached44a1d0 clock, not a fresh clock sample per slot.
struct SourceTimerBank {
    std::array<std::uint32_t, 5> origins{};
    std::uint32_t clock = 0;

    //447a70 samples4a0580 once, then44a170 stores that cached time in all slots.
    void create(std::uint32_t creation_clock) {
        clock = creation_clock;
        origins.fill(creation_clock);
    }

    void reset(std::size_t slot) {
        origins.at(slot) = clock;
    }

    std::uint32_t read(std::size_t slot) const {
        const auto origin = origins.at(slot);
        return origin < clock ? (std::uint32_t((clock - origin) * 16u) / 1000u) << 12 : 0;
    }
};

//The qualifying approach has no starting-light countdown.421390/448130
//queues State6 during physics;44a370 commits it only at the next primary,
//resetting timer0 BEFORE44a1d0 replaces the cached caller clock.
//The ordered route crossing and controller/lap writers remain with their
//existing owners; this object owns only the startup dispatch boundary.
class SourceQualifyingApproach {
    std::uint16_t state_ = 4, pending_ = 4;
    bool first_entry_ = true;
    std::uint32_t release_clock_ = 0;

public:
    //True only for447f60's first443f60 initialization. Extra physical passes
    //must not call this primary boundary or commit the queued state.
    bool begin_primary(SourceTimerBank& timers) {
        if (state_ != pending_) {
            state_ = pending_;
            timers.reset(0);
        }
        const bool initialize = first_entry_;
        first_entry_ = false;
        return initialize;
    }

    //Called from the generated local421390 start-line event, with the cached
    //44a1d0 clock used by448130. It does not sample QPC or run a countdown.
    void queue_release(std::uint32_t clock) {
        if (first_entry_ || state_ != 4 || pending_ != 4) {
            throw std::runtime_error("Qualifying release requires one initialized State4 approach");
        }
        release_clock_ = clock;
        pending_ = 6;
    }

    std::uint16_t state() const { return state_; }
    std::uint16_t pending_state() const { return pending_; }
    std::uint32_t release_clock() const { return release_clock_; }
};

enum class SourceCountdownEvent {
    none,
    housing,
    red,
    green
};

//Ordinary421f40 countdown; state dispatch and render placement belong to caller.
struct SourceSessionStart {
    SourceTimerBank timers;
    std::uint16_t phase = 0;

    //448c33 clears the raw countdown WORD before every state10 physical pass.
    void finish_wrapper() {
        phase = 0;
    }

    SourceCountdownEvent advance_countdown() {
        if (std::bit_cast<std::int32_t>(timers.read(2)) <= 0x30000) {
            return SourceCountdownEvent::none;
        }
        ++phase;
        timers.reset(2);
        if (phase == 3) {
            timers.reset(1);
            return SourceCountdownEvent::green;
        }
        return phase == 2 ? SourceCountdownEvent::red : phase == 1 ? SourceCountdownEvent::housing : SourceCountdownEvent::none;
    }

    //41f520 render checks the cached primary clock before the optional extra.
    void expire_green() {
        if (phase == 3 && std::bit_cast<std::int32_t>(timers.read(1)) > 0x18000) {
            phase = 4;
        }
    }
};
} // namespace toca::simulation
