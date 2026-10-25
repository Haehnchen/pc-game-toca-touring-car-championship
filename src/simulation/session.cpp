#include "simulation/session.hpp"

#include "simulation/race_timing.hpp"
#include "simulation/route_progress.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace toca::simulation {
namespace {
std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("Session: truncated POINTS field");
    }
    return bytes[offset] | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}
} // namespace

std::uint32_t session_timer_q16(std::uint64_t elapsed_ms) {
    // Keep the source unsigned32 multiply/wrap before division and final shift.
    return (std::uint32_t(elapsed_ms * 16) / 1000) << 12;
}

Session::Session(std::span<const std::uint8_t> route_bytes, bool qualifying, GameMode mode, std::uint32_t lap_limit)
    : lap_limit_(lap_limit), mode_(mode), qualifying_(qualifying || mode == GameMode::time_trial) {
    if (lap_limit > 99) {
        throw std::runtime_error("Session race lap limit must be0..99");
    }
    const auto count = u32(route_bytes, 0);
    if (count < 18 || count > 256 || count > (route_bytes.size() - 4) / 64) {
        throw std::runtime_error("Session requires18..256 original POINTS records");
    }
    points_.resize(count);
    std::vector<fixed::Vector> targets(count);
    for (std::size_t index = 0; index < count; ++index) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            points_[index].first[axis] = std::bit_cast<std::int32_t>(u32(route_bytes, 4 + index * 64 + axis * 4));
            points_[index].second[axis] = std::bit_cast<std::int32_t>(u32(route_bytes, 4 + index * 64 + 36 + axis * 4));
            targets[index][axis] = std::bit_cast<std::int32_t>(u32(route_bytes, 4 + index * 64 + 12 + axis * 4));
        }
    }
    nearest_route_ = NearestRouteMap(targets);
    // 4019f0 initializes six ordered checkpoint indices, not nearest-point laps.
    const auto step = (count * 0x2b85u) >> 16;
    for (std::uint32_t index = 0; index < checkpoints_.size(); ++index) {
        checkpoints_[index] = step * index;
    }
    if (qualifying_) {
        // 443d30/443f60's local actor-slot permutation starts at count-18.
        previous_ = count - 18;
        next_ = count - 17;
    } else {
        // 460170 explicitly clears cc5ba/bc and sets cc5be (checkpoint) to1.
        checkpoint_ = 1;
        initial_start_pending_ = true;
    }
}

void Session::configure_qualifying_actor(std::uint32_t local, std::uint32_t actor) {
    const auto offset = 18u + 7u * ((actor + 16u - local) % 16u);
    if (!qualifying_ || mode_ == GameMode::time_trial || local >= 16 || actor >= 16 || now_ms_ != 0 || completed_ != 0 || offset > points_.size()) {
        throw std::runtime_error("Qualifying actor setup requires a fresh Single Race POINTS approach");
    }
    previous_ = std::uint32_t(points_.size()) - offset;
    next_ = previous_ + 1;
    checkpoint_ = 0;
    qualifying_rival_ = actor != local;
    human_control_ = !qualifying_rival_;
}

void Session::freeze_qualifying_laps() {
    if (!qualifying_rival_) {
        throw std::runtime_error("Only qualifying NPC lap clocks follow the local finish");
    }
    qualifying_laps_frozen_ = true;
}

void Session::configure_route_recovery(std::uint8_t refresh_period) {
    if (refresh_period > 127) {
        throw std::runtime_error("Session route cadence exceeds signed source byte");
    }
    route_refresh_period_ = refresh_period;
}

void Session::set_human_control(bool human) {
    human_control_ = human;
}

void Session::enter_pit() {
    next_ = 0;
    previous_ = std::uint32_t(points_.size()) - 1;
}

bool Session::end_qualifying() {
    if (!qualifying_ || mode_ == GameMode::time_trial || finish_state_ != 0) {
        return false;
    }
    // The source checks the minute/second fields, then supplies60:00.00
    // when no full-second best exists. It never awards an unfinished lap.
    if (best_ms_ < 1000) {
        best_ms_ = 3'600'000;
    }
    finish_state_ = 7;
    finish_ms_ = now_ms_;
    lap_ms_ = last_ms_;
    return true;
}

bool Session::passed(std::uint32_t point, const std::array<std::int32_t, 3>& position) const {
    const auto& line = points_.at(point);
    return route_boundary_passed(position, line.first, line.second);
}

void Session::cross_start(std::vector<SessionEvent>& events) {
    // 403dd0 only calls421390 when all six checkpoints have been consumed.
    if (finish_state_ != 0 || qualifying_laps_frozen_) {
        return; //421390 excludes states7/8/10/13; AI route targets continue.
    }
    if (qualifying_ && !qualifying_rival_ && !released_) {
        // 421390 discards the out-lap and starts measured lap1 via448130.
        released_ = true;
        release_counter_.enter(1, std::uint32_t(mode_));
        human_control_ = true; //421390 calls402c80 before this tick's recovery.
        lap_ = 1;
        RaceFinishState race;
        QualifyingReleaseState release;
        release_qualifying_session(release, race, std::uint32_t(now_ms_));
        lap_start_ms_ = release.release_clock;
        events.push_back(SessionEvent::released);
        return;
    }
    if (!released_ && !qualifying_rival_) {
        return;
    }
    last_ms_ = std::uint32_t(now_ms_ - lap_start_ms_);
    LapTimingState timing;
    timing.lap = std::int16_t(lap_);
    timing.best_ms = best_ms_;
    timing.current_ms = last_ms_;
    const LapTimingContext context{std::uint32_t(now_ms_), lap_limit_ == 0 ? 99 : lap_limit_, qualifying_ ? 1 : 2, 6, !qualifying_rival_, false, !qualifying_rival_};
    const auto transition = advance_lap_timing(timing, context);
    timing = transition.state;
    best_ms_ = timing.best_ms;
    ++completed_;
    lap_ = std::uint32_t(timing.lap);
    lap_start_ms_ = timing.lap_start_ms;
    events.push_back(SessionEvent::lap_complete);
    if (qualifying_ && mode_ != GameMode::time_trial && transition.action == LapTimingAction::qualifying_finish) {
        human_control_ = false; //402be0 precedes the later nearest-route pass.
        RaceFinishState race;
        record_qualifying_finish(race, 1);
        finish_state_ = race.pending_state;
        finish_ms_ = now_ms_;
        events.push_back(SessionEvent::qualifying_finished);
    } else if (!qualifying_ && mode_ != GameMode::time_trial && lap_limit_ != 0 && transition.action == LapTimingAction::race_finish && network_finish_authority_) {
        human_control_ = false;
        lap_ = lap_limit_; //421390 restores the local displayed lap at finish.
        RaceFinishState race;
        record_local_finish(race, {}, 0, 0, 1, std::uint32_t(now_ms_), std::uint32_t(race_start_ms_));
        finish_state_ = race.pending_state;
        finish_ms_ = now_ms_;
        finish_presentation_ms_ = now_ms_;
        events.push_back(SessionEvent::race_finished);
    }
}

void Session::set_finish_speech(bool started, bool playing) {
    finish_speech_started_ = started;
    finish_speech_playing_ = playing;
}

void Session::set_finish_group_complete(bool complete) {
    if (complete && !finish_group_complete_) {
        finish_presentation_ms_ = now_ms_;
    }
    finish_group_complete_ = complete;
}

void Session::configure_local_players(std::uint32_t count) {
    if (count < 1 || count > 2) {
        throw std::invalid_argument("Session requires one or two local players");
    }
    local_players_ = count;
}

void Session::set_network_finish_authority(bool enabled) {
    if (mode_ != GameMode::linkup) {
        throw std::logic_error("Network finish ownership requires Linkup");
    }
    network_finish_authority_ = enabled;
}

void Session::apply_network_finish(std::uint32_t centiseconds) {
    if (mode_ != GameMode::linkup || centiseconds > std::numeric_limits<std::uint32_t>::max() / 10) {
        throw std::invalid_argument("Invalid owner-authored Linkup finish time");
    }
    if (network_finish_ms_) {
        return;
    }
    network_finish_ms_ = centiseconds * 10;
    completed_ = lap_limit_;
    lap_ = lap_limit_;
    if (finish_state_ == 0) {
        finish_state_ = 10;
        finish_ms_ = now_ms_;
        finish_presentation_ms_ = now_ms_;
    }
}

std::vector<SessionEvent> Session::update(std::uint64_t elapsed_ms, const std::array<std::int32_t, 3>& position_q16, bool confirm) {
    if (elapsed_ms < now_ms_ || elapsed_ms > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("Session time must be monotonic active milliseconds within the source32-bit range");
    }
    const auto cached_clock = now_ms_;
    now_ms_ = elapsed_ms;
    start_.timers.clock = std::uint32_t(now_ms_);
    if (finish_state_ == 10) {
        start_.finish_wrapper();
    }
    if (start_commit_pending_) {
        start_commit_pending_ = false;
        start_.timers.reset(0);
    }
    //448416..44842f precedes421aa0 only when the update entered state6.
    //A green-light transition later in this update therefore retains360.
    release_counter_.before_update(state());
    const bool route_refresh = route_refresh_period_ && route_refresh_ticks_ >= *route_refresh_period_;
    if (route_refresh_period_) {
        route_refresh_ticks_ = route_refresh ? 0 : std::uint8_t(route_refresh_ticks_ + 1);
    }
    split_sector_.reset();
    std::vector<SessionEvent> events;
    const auto finish_timer = session_timer_q16(now_ms_ - finish_presentation_ms_);
    const bool finish_ready = mode_ == GameMode::championship && finish_speech_started_ ? !finish_speech_playing_ || finish_timer >= 0xf0000 : finish_timer >= 0x50000;
    if (finish_state_ == 10 && finish_group_complete_ && mode_ == GameMode::single_race && local_players_ > 1) {
        if (!race_return_requested_ && std::bit_cast<std::int32_t>(finish_timer) > 0x50000) {
            //448d27 uses strict >5s and queues441650(3,0,0), without state13.
            race_return_requested_ = true;
            events.push_back(SessionEvent::race_return_requested);
        }
    } else if (finish_state_ == 10 && finish_group_complete_ && finish_ready) {
        //448a20: Championship waits for its finish speech, capped at15s.
        finish_state_ = 13;
        //44a370 commits the result state and resets timer0 before44a1d0
        //samples the next clock.44a020 then uses this new result origin.
        results_ms_ = cached_clock;
        events.push_back(SessionEvent::results_entered);
    } else if (finish_state_ == 13 && !race_return_requested_ && (confirm || session_timer_q16(now_ms_ - results_ms_) >= 0x280000)) {
        //44a020/44a090/44a0c0 use the result-entry timer, then open overlay3.
        //The original global session state remains13 beneath that overlay.
        race_return_requested_ = true;
        events.push_back(SessionEvent::race_return_requested);
    } else if (finish_state_ == 7) {
        const auto timer = session_timer_q16(now_ms_ - finish_ms_);
        if ((confirm && timer > 0x10000) || timer > 0x60000) {
            finish_state_ = 8;
            results_ms_ = now_ms_;
            events.push_back(SessionEvent::results_entered);
        }
    } else if (finish_state_ == 8 && (confirm || session_timer_q16(now_ms_ - results_ms_) >= 0x280000)) {
        finish_state_ = 9;
        events.push_back(SessionEvent::race_requested);
    }
    if (!qualifying_ && !released_) {
        const auto countdown = start_.advance_countdown();
        if (countdown == SourceCountdownEvent::red) {
            events.push_back(SessionEvent::red);
        } else if (countdown == SourceCountdownEvent::green) {
            events.push_back(SessionEvent::go);
            events.push_back(SessionEvent::released);
            released_ = true;
            start_commit_pending_ = true;
            release_counter_.enter(2, std::uint32_t(mode_));
            lap_start_ms_ = now_ms_;
            race_start_ms_ = now_ms_;
        }
    }
    //Native presentation stays independent: evaluate the cached-clock render gate once per deadline.
    start_.expire_green();

    // Source403dd0 consumes at most one route boundary per update. It uses
    // current and previous targets to unwind reverse travel without a lap.
    //403dd0 formats the current lap before421390 can restart its clock.
    LapTimingState display;
    display.lap_start_ms = std::uint32_t(lap_start_ms_);
    if (finish_state_ != 0) {
        display.lap_start_ms = std::uint32_t(now_ms_ - last_ms_);
    }
    split_components_ = observe_lap_clock(display, std::uint32_t(now_ms_), released_ || qualifying_rival_, !qualifying_rival_, 6).current_display;
    bool restarted_incomplete_lap = false;
    const auto route_step = advance_route_cursor({next_, previous_}, std::uint32_t(points_.size()), passed(next_, position_q16), passed(previous_, position_q16));
    if (route_step.crossing == RouteCrossing::forward) {
        const auto crossed = next_;
        if (crossed == 0) {
            initial_start_pending_ = false;
        }
        if (backwards_ && crossed == 0) {
            backwards_ = false;
        } else if (crossed == checkpoints_[checkpoint_]) {
            if (crossed == 0) {
                cross_start(events);
            }
            if (checkpoint_marker_ == checkpoint_) {
                split_sector_ = checkpoint_marker_ / 2;
                checkpoint_marker_ = (checkpoint_marker_ + 2) % 6;
            }
            checkpoint_ = (checkpoint_ + 1) % 6;
        } else if (crossed == 0 && !backwards_) {
            checkpoint_ = 1;
            checkpoint_marker_ = 2;
            // 403dd0 ->45dd60 restarts an incomplete Time Trial attempt at
            // the line without awarding a lap or replacing the best stream.
            if (mode_ == GameMode::time_trial && released_) {
                lap_start_ms_ = now_ms_;
                events.push_back(SessionEvent::attempt_restarted);
            } else if (qualifying_ && mode_ != GameMode::time_trial) {
                // 403dd0's unmatched-start branch resets gameplay+c8 while
                // DAT_004bd934 is set. It awards no lap. The current raw time
                // was already computed earlier in that source update.
                lap_ms_ = released_ || qualifying_rival_ ? std::uint32_t(now_ms_ - lap_start_ms_) : 0;
                lap_start_ms_ = now_ms_;
                restarted_incomplete_lap = true;
            }
        }
    } else if (route_step.crossing == RouteCrossing::backward) {
        if (previous_ == 0) {
            backwards_ = true;
        }
    }
    next_ = route_step.cursor.next;
    previous_ = route_step.cursor.previous;
    //4046a3 runs after the one-step gate/checkpoint pass; it changes neither
    //lap/checkpoint state nor the earlier lookahead cursor and consumes no RNG.
    if (route_refresh && human_control_) {
        const auto recovery = nearest_route_.recover({next_, previous_}, position_q16);
        next_ = recovery.cursor.next;
        previous_ = recovery.cursor.previous;
    }
    lap_ms_ = finish_state_ != 0 ? last_ms_ : restarted_incomplete_lap ? lap_ms_ : released_ || qualifying_rival_ ? std::uint32_t(now_ms_ - lap_start_ms_) : 0;
    return events;
}

std::uint32_t Session::state() const {
    if (finish_state_ != 0) {
        return finish_state_;
    }
    return released_ && !start_commit_pending_ ? 6u : qualifying_ ? 4u : 9u;
}

std::uint32_t Session::start_phase() const {
    return start_.phase;
}

bool Session::released() const {
    return released_;
}

bool Session::grid_held() const {
    return !qualifying_ && (!released_ || start_commit_pending_);
}

std::uint32_t Session::release_counter() const {
    return release_counter_.value();
}

bool Session::qualifying_rollout() const {
    return qualifying_ && !released_ && finish_state_ == 0;
}

std::uint32_t Session::lap() const {
    return lap_;
}

std::uint32_t Session::completed_laps() const {
    return completed_;
}

std::uint32_t Session::lap_time_ms() const {
    return lap_ms_;
}

std::uint32_t Session::lap_display_ms() const {
    return std::uint32_t(split_components_[0]) * 60000u + std::uint32_t(split_components_[1]) * 1000u + std::uint32_t(split_components_[2]) * 10u;
}

std::uint32_t Session::best_lap_ms() const {
    return best_ms_;
}

std::uint32_t Session::last_lap_ms() const {
    return last_ms_;
}

std::uint32_t Session::race_time_ms() const {
    if (network_finish_ms_) {
        return *network_finish_ms_;
    }
    return released_ ? std::uint32_t((finish_state_ != 0 ? finish_ms_ : now_ms_) - race_start_ms_) : 0;
}

std::uint64_t Session::finish_time_ms() const {
    return finish_ms_;
}

void Session::freeze_race_laps() {
    if (finish_state_ == 0) {
        finish_state_ = 10;
        finish_ms_ = now_ms_;
        finish_presentation_ms_ = now_ms_;
    }
}

std::uint32_t Session::next_point() const {
    return next_;
}

const std::array<std::int16_t, 3>& Session::split_components() const {
    return split_components_;
}

std::optional<std::uint32_t> Session::split_sector() const {
    return split_sector_;
}

std::uint32_t Session::point_count() const {
    return std::uint32_t(points_.size());
}

bool Session::start_line_pending() const {
    return initial_start_pending_ || backwards_;
}

bool Session::finish_camera_active() const {
    return finish_state_ == 8 || finish_state_ == 13 || (finish_state_ == 7 && session_timer_q16(now_ms_ - finish_ms_) >= 0x20000)
           || (finish_state_ == 10 && finish_group_complete_ && session_timer_q16(now_ms_ - finish_presentation_ms_) >= 0x20000);
}
} // namespace toca::simulation
