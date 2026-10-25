#include "retained_state.hpp"

#include "binary_io.hpp"

#include <bit>

namespace toca::test::fleet {
using namespace toca::simulation;

void RetainedRaceTiming::primary(std::uint32_t cached_clock) {
    if (cached_clock != timers.clock && !(warm_restart && state == 13)) {
        throw std::runtime_error("Neutral primary event changed the last cached caller clock");
    }
    timers.clock = cached_clock; //Paused44a1d0 callers still sample the external wall clock.
    primary_update = true;
    const auto pending = finish.pending_state & 0xffff;
    if (pending != std::uint32_t(state)) {
        if ((state != 6 || pending != 10) && (state != 10 || pending != 13) && !((warm_restart || cold_start) && state == 9 && pending == 6)) {
            throw UnsupportedSourceState("Unsupported generated local session primary transition");
        }
        state = std::int32_t(pending);
        timers.reset(0); //44a392/399, before44a1d0 samples its next clock.
    }
}

void RetainedRaceTiming::before_update(std::uint32_t clock) {
    handoff.reset();
    timers.clock = clock;
    if (cold_first && created_origins) {
        timers.reset(3); //447980 precedes the repeated460170.
        timers.reset(2); //Its44ffe1→421290 reset uses the same caller sample.
    }
    if (state == 6 && ((cold_start && cold_active_entry) || (warm_restart && release_flags[3] != 0 && warm_generations != 0))) {
        timers.reset(0); //4482e3's first active wrapper, after the pending state commit.
        release_flags[3] = 0;
        cold_active_entry = false;
    }
    if (state == 10 && release_flags[1] != 0) {
        timers.reset(0);
        timers.reset(3);
        release_flags[1] = 0; //af050 is the same flag exposed by the lifecycle and typed release aliases.
        if (finish.order[0] == local_actor) {
            source_store(configuration_tail, 0x28, 1); //448b.. local winner's CONFIG+624 latch.
        }
    }
    if (state == 10) {
        //448a20 writes both latches before every421aa0 call, including
        //extra updates after its one-time af050 entry branch.
        release_flags[2] = release_flags[3] = 1;
        SourceSessionStart start{timers, start_phase};
        start.finish_wrapper();
        start_phase = start.phase;
    }
    release_due = false;
    if ((warm_restart || cold_start) && state == 9 && !cold_first) {
        SourceSessionStart start{timers, start_phase};
        release_due = start.advance_countdown() == SourceCountdownEvent::green;
        timers = start.timers;
        start_phase = start.phase;
        if (release_due) {
            set_pending_session(finish, 6);
            release_clock = clock;
            running = release_marker = release_flags[0] = 1;
        }
    }
}

void RetainedRaceTiming::after_update() {
    if (state == 10 && std::bit_cast<std::int32_t>(timers.read(0)) >= 0x50000) {
        set_pending_session(finish, 13);
    }
    //44a336 executes41ab70 once after every ordinary state wrapper.
    for (std::size_t player = 0; player < displays.players.size(); ++player) {
        advance_split_display(displays.players[player], false, finish.local_finished[player] != 0);
    }
    if ((warm_restart || cold_start) && primary_update) {
        SourceSessionStart start{timers, start_phase};
        start.expire_green();
        start_phase = start.phase;
    }
    primary_update = false;
    warm_grid_first = false;
    cold_first = false;
}

void RetainedRaceTiming::consume_menu_input() {
    //4235a0 follows403dd0 return and consumes only a positive signed
    //dcdb2 byte; the AI_RETURN timing sidecar still observes its set value.
    const auto countdown = std::uint8_t(flags >> 16);
    if (std::bit_cast<std::int8_t>(countdown) > 0) {
        flags = (flags & ~0x00ff0000u) | (std::uint32_t(countdown - 1) << 16);
    }
}

} // namespace toca::test::fleet
