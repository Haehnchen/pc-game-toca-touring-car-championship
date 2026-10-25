#pragma once

#include "simulation/fixed.hpp"

#include <algorithm>
#include <cstdio>

namespace toca::simulation {
struct LapTimingState {
    std::int16_t lap = 1;
    std::uint32_t record_flag = 0, best_ms = 0, current_ms = 0, lap_start_ms = 0;
    std::array<std::int16_t, 3> best_display{}, current_display{};
    std::uint8_t best_notice = 0;
};

//403dd0's clock pass precedes all actor checkpoint/lap calls. Components are
//updated only for permanent players; AI actors retain their source fields.
inline LapTimingState observe_lap_clock(LapTimingState state, std::uint32_t clock, bool running, bool player, std::int32_t session_state) {
    if (player && state.best_notice != 0) {
        --state.best_notice;
    }
    if (state.record_flag != 0) {
        state.current_ms = 3'599'000;
        state.current_display = {59, 59, 99};
    } else if (!running) {
        state.current_ms = 0;
        state.current_display = {};
    } else {
        state.current_ms = clock - state.lap_start_ms;
        if (player && session_state != 10 && session_state != 7) {
            const auto elapsed = fixed::bits(state.current_ms);
            const auto whole_seconds = std::bit_cast<std::int16_t>(std::uint16_t(elapsed / 1000));
            const auto minutes = std::int16_t(whole_seconds / 60);
            const auto remainder = fixed::sub(elapsed, std::int32_t(whole_seconds) * 1000);
            const auto hundredths = fixed::bits(std::uint32_t(remainder) * 100) / 1000;
            state.current_display = {minutes, std::int16_t(whole_seconds - minutes * 60), std::bit_cast<std::int16_t>(std::uint16_t(hundredths))};
            if (minutes > 59) {
                state.record_flag = 1;
                state.current_display = {59, 59, 99};
            }
        }
    }
    return state;
}

//421390 lap arithmetic before mode/finish/handoff side effects. A final lap
//retains its completed time; best-time comparison is deliberately unsigned.
inline LapTimingState complete_lap_timing(LapTimingState state, std::uint32_t clock, bool retain_completed_time = false) {
    if (state.lap < 0 || state.lap > 99) {
        throw std::runtime_error("Lap timing requires the original0..99 displayed lap");
    }
    state.lap = state.lap == 99 ? 0 : std::int16_t(state.lap + 1);
    state.record_flag = 0;
    state.lap_start_ms = clock;
    if (state.current_ms < state.best_ms || state.best_ms == 0) {
        state.best_ms = state.current_ms;
        state.best_display = state.current_display;
        state.best_notice = 0x50;
    }
    if (!retain_completed_time) {
        state.current_ms = 0;
    }
    return state;
}

enum class LapTimingAction {
    none,
    race_finish,
    qualifying_restart,
    qualifying_finish
};

struct LapTimingContext {
    std::uint32_t clock = 0, lap_limit = 0;
    std::int32_t stage = 2, session_state = 6;
    bool player = false, local_finished = false, human = false, pit = false;
};

struct LapTimingResult {
    LapTimingState state;
    LapTimingAction action = LapTimingAction::none;
    bool stop_horns = false;
};

//421390's numerical state and branch selection. The caller performs controller
//handoffs and global finish dispatch; no saved controller outputs enter here.
inline LapTimingResult advance_lap_timing(LapTimingState state, const LapTimingContext& context) {
    LapTimingResult result{state};
    if (context.session_state == 7 || context.session_state == 8 || context.session_state == 10 || context.session_state == 13) {
        return result;
    }
    if (context.lap_limit > 99) {
        throw std::runtime_error("Lap timing limit exceeds original displayed bounds");
    }
    const auto next_lap = state.lap == 99 ? 0 : state.lap + 1;
    result.stop_horns = next_lap > std::int32_t(context.lap_limit);
    result.state = complete_lap_timing(state, context.clock, result.stop_horns);
    if ((context.stage == 1 || context.stage == 3) && context.player) {
        if (next_lap >= 4) {
            result.action = LapTimingAction::qualifying_finish;
        } else if (!context.human && !context.pit && !context.local_finished) {
            result.action = LapTimingAction::qualifying_restart;
            result.state.lap = 1;
            result.state.best_ms = 0;
            result.state.best_display = {};
        }
    } else if ((context.stage == 2 || context.stage == 4) && result.stop_horns) {
        result.action = LapTimingAction::race_finish;
    }
    return result;
}

struct RaceFinishState {
    std::uint32_t count = 0, local_count = 0, timer2 = 0, timer3 = 0, pending_state = 0, stopped = 0;
    std::array<std::uint8_t, 4> local_finished{};
    std::array<std::uint8_t, 16> order{};
};

struct ActorFinishState {
    std::uint32_t finished = 0;
    std::int32_t total_hundredths = 0;
    std::uint8_t notice = 0, best_notice = 0;
};

inline std::int32_t finish_time_hundredths(std::uint32_t clock, std::uint32_t release_clock) {
    return fixed::bits(std::uint32_t(clock - release_clock) * 100) / 1000;
}

//The non-player/already-finished-player branch of421390. Local controller
//handoff and network/Time Trial callbacks are separate caller boundaries.
inline ActorFinishState record_ai_finish(RaceFinishState& race, ActorFinishState state, std::uint32_t actor, std::uint32_t clock, std::uint32_t release_clock) {
    if (actor >= 16 || (state.finished == 0 && (race.count & 0xff) >= 16)) {
        throw std::runtime_error("AI finish requires original actor/order bounds");
    }
    if (state.finished == 0) {
        state.finished = 1;
        state.best_notice = 0;
        state.total_hundredths = finish_time_hundredths(clock, release_clock);
        race.order[race.count & 0xff] = std::uint8_t(actor);
        if ((race.count & 0xff) == 0) {
            race.timer3 = 0;
        } else if ((race.count & 0xff) == 1) {
            race.timer2 = race.timer3;
        }
        race.count = (race.count & 0xffffff00u) | ((race.count + 1) & 0xff);
    }
    state.best_notice = state.notice = 0;
    return state;
}

//421390 writes the pending state as a WORD and its counters as bytes.
inline void set_pending_session(RaceFinishState& race, std::uint16_t state) {
    race.pending_state = (race.pending_state & 0xffff0000u) | state;
}

inline bool record_qualifying_finish(RaceFinishState& race, std::uint32_t local_players) {
    if (local_players < 1 || local_players > 4) {
        throw std::runtime_error("Qualifying finish requires original local-player bounds");
    }
    race.local_count = (race.local_count & 0xffffff00u) | ((race.local_count + 1) & 0xff);
    race.local_finished[0] = 1; //4217bd addresses byte0 even for split-screen.
    if ((race.local_count & 0xff) == local_players) {
        set_pending_session(race, 7);
        race.stopped = 1;
        return true;
    }
    return false;
}

inline ActorFinishState record_local_finish(
    RaceFinishState& race, ActorFinishState state, std::uint32_t actor, std::uint32_t player, std::uint32_t local_players, std::uint32_t clock, std::uint32_t release_clock) {
    const auto count = race.count & 0xff;
    if (actor >= 16 || player >= 4 || local_players < 1 || local_players > 4 || count >= 16) {
        throw std::runtime_error("Local finish requires original actor/player/order bounds");
    }
    state.finished = 1;
    state.total_hundredths = finish_time_hundredths(clock, release_clock);
    if (count == 1) {
        race.timer2 = race.timer3;
    } else if (count == 0) {
        race.timer2 = 9999; //Unlike the first AI finisher, retains timer3.
    }
    race.local_finished[player] = std::uint8_t(count + 1);
    race.order[count] = std::uint8_t(actor);
    race.count = (race.count & 0xffffff00u) | ((count + 1) & 0xff);
    race.local_count = (race.local_count & 0xffffff00u) | ((race.local_count + 1) & 0xff);
    if ((race.local_count & 0xff) == local_players) {
        set_pending_session(race, 10);
        race.stopped = 1;
    }
    state.best_notice = state.notice = 0;
    return state;
}

inline void reset_qualifying_steering(std::int32_t& left, std::int32_t& right, std::int16_t& angle) {
    //421896..4218a2: two DWORD input axes and the WORD steering angle.
    //The filtered steering at dynamics+150 is deliberately retained.
    left = right = 0;
    angle = 0;
}

struct QualifyingReleaseState {
    std::uint32_t release_counter = 0, release_clock = 0, running = 0, marker = 0;
    std::array<std::uint32_t, 4> flags{};
};

//421390 ->448130 for an ordinary qualifying out-lap. Stage1/3 does not
//recalculate the launch tire scalars; only the local lap clocks are reset.
inline void release_qualifying_session(QualifyingReleaseState& release, RaceFinishState& race, std::uint32_t clock) {
    release.release_counter = 0;
    release.release_clock = clock;
    release.running = release.marker = 1;
    release.flags[0] = release.flags[3] = 1;
    set_pending_session(race, 6);
    race.stopped = 0;
}

struct CheckpointTime {
    std::uint32_t clock = 0;
    std::int32_t lap = 0, actor = 0;
};

using CheckpointTimes = std::array<CheckpointTime, 3 * 16>;

struct CheckpointTimeResult {
    bool written = false;
    std::int32_t delta_ms = 0, leader = -1;
};

//4212d0 writes the current rank slot, then compares the preceding slot only
//when its lap agrees. Sector0 is called after421390 increments the lap.
inline CheckpointTimeResult record_checkpoint_time(CheckpointTimes& times, std::uint32_t actor, std::uint32_t sector, std::uint32_t rank, std::int16_t lap, std::uint32_t clock) {
    if (actor >= 16 || sector >= 3 || rank < 1 || rank > 16) {
        throw std::runtime_error("Checkpoint timing requires original actor/sector/rank bounds");
    }
    if (lap <= 1 && sector == 0) {
        return {};
    }
    const auto index = sector * 16 + rank - 1;
    times[index] = {clock, lap, std::int32_t(actor)};
    CheckpointTimeResult result;
    result.written = true;
    if (rank > 1 && times[index - 1].lap == lap) {
        result.delta_ms = fixed::bits(clock - times[index - 1].clock);
        result.leader = times[index - 1].actor;
    }
    return result;
}

struct SplitDisplayState {
    std::int32_t mode = 0, leader = -1, active = 0;
    std::int16_t ticks = 0;
    std::array<std::int16_t, 3> components{};
    std::array<std::int32_t, 3> reference{}, current{};
    std::array<char, 16> text{'-', '-', '-', '-', '-', '-'}, leader_text{'-', '-', '-', '-', '-', '-'};
};

struct SplitDisplays {
    std::array<SplitDisplayState, 4> players;
    std::array<std::int32_t, 3> persisted{};
};

inline void format_split_time(std::array<char, 16>& target, const char* format, std::int32_t minutes, std::int32_t seconds, std::int32_t hundredths) {
    std::array<char, 64> formatted{};
    const auto size = std::snprintf(formatted.data(), formatted.size(), format, minutes, seconds, hundredths);
    if (size < 0 || std::size_t(size) >= target.size()) {
        throw std::runtime_error("Source split text exceeds its16-byte display slot");
    }
    //sprintf overwrites through NUL only; unrelated trailing bytes retain.
    std::copy_n(formatted.begin(), size + 1, target.begin());
}

//4212d0 ->41a840: preceding-rank timing and personal sector references are
//independent displays. record/playback gates only the personal baseline path.
inline void consume_checkpoint_display(SplitDisplays& displays, std::uint32_t player, std::uint32_t sector, const CheckpointTimeResult& split,
    const std::array<std::int16_t, 3>& lap_display, bool time_trial = false, bool record_playback = false) {
    if (player >= displays.players.size() || sector >= 3) {
        throw std::runtime_error("Split display requires original player/sector bounds");
    }
    if (!split.written) {
        return;
    }
    auto& state = displays.players[player];
    state.leader = split.leader;
    if (split.leader != -1) {
        const auto seconds = (split.delta_ms % 60000) / 1000;
        const auto minutes = split.delta_ms / 60000;
        const auto hundredths = fixed::bits(std::uint32_t(fixed::sub(split.delta_ms, (seconds + minutes * 60) * 1000)) * 100) / 1000;
        format_split_time(state.leader_text, "+%02d:%02d.%02d", minutes, seconds, hundredths);
    }
    state.active = 1;
    state.ticks = 0;
    const auto cumulative = (std::int32_t(lap_display[1]) + std::int32_t(lap_display[0]) * 60) * 100 + lap_display[2];
    if (state.mode == 1 && (!record_playback || !time_trial)) {
        state.components = lap_display;
        state.reference[sector] = cumulative;
        if (sector == 0) {
            displays.persisted = displays.players[0].reference;
        }
        format_split_time(state.text, "%02d:%02d.%02d", lap_display[0], lap_display[1], lap_display[2]);
    } else {
        state.current[sector] = cumulative;
        const auto difference = fixed::sub(cumulative, state.reference[sector]);
        const auto absolute = fixed::abs(difference);
        format_split_time(state.text, difference < 0 ? "-%02d:%02d.%02d" : "+%02d:%02d.%02d", absolute / 6000, (absolute % 6000) / 100, absolute % 100);
        if (difference < 0 && sector == 0) {
            state.reference = state.current;
            displays.persisted = displays.players[0].reference;
        }
    }
}

inline void advance_split_display(SplitDisplayState& state, bool paused, bool finished) {
    if (!paused && state.active != 0 && !finished) {
        state.ticks = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(state.ticks) + 1));
        if (state.ticks > 200) {
            state.active = 0;
        }
    }
}

inline bool split_display_visible(const SplitDisplayState& state, bool finished) {
    return state.active != 0 && !finished && (state.ticks > 47 || (std::uint16_t(state.ticks) & 15) > 9);
}
} // namespace toca::simulation
