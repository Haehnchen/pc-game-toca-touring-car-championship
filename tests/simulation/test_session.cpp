#include "assets/store.hpp"
#include "scene/placement.hpp"
#include "simulation/race_timing.hpp"
#include "simulation/session.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes.at(offset + index) = std::uint8_t(value >> (index * 8));
    }
}

std::array<std::int32_t, 3> circle(double index, double radius = 100) {
    constexpr double tau = 6.283185307179586;
    return {std::int32_t(std::cos(index * tau / 24) * radius * 65536), 0, std::int32_t(std::sin(index * tau / 24) * radius * 65536)};
}

std::vector<std::uint8_t> route() {
    std::vector<std::uint8_t> bytes(4 + 24 * 64);
    put32(bytes, 0, 24);
    for (std::uint32_t index = 0; index < 24; ++index) {
        const auto first = circle(index, 90), second = circle(index, 110);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put32(bytes, 4 + index * 64 + axis * 4, std::uint32_t(first[axis]));
            put32(bytes, 4 + index * 64 + 36 + axis * 4, std::uint32_t(second[axis]));
        }
    }
    return bytes;
}

bool has(const std::vector<toca::simulation::SessionEvent>& events, toca::simulation::SessionEvent event) {
    return std::find(events.begin(), events.end(), event) != events.end();
}

void source_caller_lifecycle() {
    using namespace toca::simulation;
    RaceFinishState race;
    race.count = 0xaabbcc00;
    race.local_count = 0x11223300;
    race.pending_state = 0x98760006;
    race.timer3 = 2345;
    ActorFinishState actor{0, 0, 7, 9};
    actor = record_local_finish(race, actor, 14, 0, 1, 304160, 60000);
    expect(actor.finished == 1 && actor.total_hundredths == 24416 && actor.notice == 0 && actor.best_notice == 0,
        "Local421390 finish must retain signed source centiseconds and clear only its notices");
    expect(race.count == 0xaabbcc01 && race.local_count == 0x11223301 && race.timer2 == 9999 && race.timer3 == 2345 && race.pending_state == 0x9876000a && race.stopped == 1
               && race.local_finished[0] == 1 && race.order[0] == 14,
        "First local finish preserves timer3 and upper byte/word windows while scheduling state10");
    RaceFinishState qualifying;
    qualifying.local_count = 0x10203000;
    qualifying.pending_state = 0xaaaa0006;
    expect(record_qualifying_finish(qualifying, 1) && qualifying.local_count == 0x10203001 && qualifying.local_finished[0] == 1 && qualifying.pending_state == 0xaaaa0007
               && qualifying.stopped == 1 && qualifying.count == 0,
        "Qualifying final increments only local completion and writes the low pending WORD");
    QualifyingReleaseState release{360, 20, 0, 0, {8, 0x11223344, 0x55667788, 9}};
    release_qualifying_session(release, qualifying, 90210);
    expect(release.release_counter == 0 && release.release_clock == 90210 && release.running == 1 && release.marker == 1
               && release.flags == std::array<std::uint32_t, 4>{1, 0x11223344, 0x55667788, 1} && qualifying.pending_state == 0xaaaa0006 && qualifying.stopped == 0,
        "Qualifying448130 release retains adjacent globals, clears launch counter and schedules measured driving");
}

void source_release_counter() {
    using namespace toca::simulation;
    RaceReleaseCounter counter;
    expect(counter.value() == 0, "Source grid initialization clears the release counter");
    counter.enter(2, 0);
    counter.before_update(9);
    expect(counter.value() == 360, "The countdown update must retain the new launch interval");
    counter.before_update(6);
    expect(counter.value() == 359, "The first active wrapper decrements before the AI pass");
    for (const auto state : {4u, 7u, 8u, 9u, 10u, 13u}) {
        counter.before_update(state);
    }
    expect(counter.value() == 359, "Non-racing wrappers must preserve the release counter");
    for (unsigned tick = 0; tick < 400; ++tick) {
        counter.before_update(6);
    }
    expect(counter.value() == 0, "The launch interval expires without unsigned underflow");
    for (const auto stage : {1, 2, 3, 4}) {
        for (unsigned mode = 0; mode < 4; ++mode) {
            counter.enter(stage, mode);
            expect(counter.value() == ((stage == 2 || stage == 4) && mode != 2 ? 360u : 0u), "Release stage and Time Trial exclusion must match448130");
        }
    }
    counter.enter(2, 0, 0);
    expect(counter.value() == 0, "448130 installs the interval only inside its local-player loop");
}

void source_results_clock() {
    using namespace toca::simulation;
    LapTimingState state;
    state.lap = 1;
    state.best_ms = 368604;
    state.current_ms = 13406;
    state.lap_start_ms = 391294;
    state.best_display = {6, 8, 60};
    state.current_display = {0, 2, 72};
    state.best_notice = 3;
    //403dd0: state10 retains the completed components; state13 resumes
    //formatting. Both still advance raw elapsed time and the notice countdown.
    const auto finish = observe_lap_clock(state, 407359, true, true, 10);
    const auto results = observe_lap_clock(state, 407359, true, true, 13);
    expect(finish.current_ms == 16065 && finish.current_display == state.current_display && finish.best_notice == 2 && results.current_ms == 16065
               && results.current_display == std::array<std::int16_t, 3>{0, 16, 6} && results.best_notice == 2,
        "403dd0 results state13 must resume player clock components while finish state10 retains them");
    const auto ai_results = observe_lap_clock(state, 407359, true, false, 13);
    expect(ai_results.current_ms == 16065 && ai_results.current_display == state.current_display && ai_results.best_notice == 3,
        "Results-state AI clocks must advance without formatting player components or decrementing player notices");
    const auto lap = advance_lap_timing(results, {407359, 1, 2, 13, true, true, false});
    expect(lap.action == LapTimingAction::none && !lap.stop_horns && lap.state.lap == results.lap && lap.state.record_flag == results.record_flag
               && lap.state.best_ms == results.best_ms && lap.state.current_ms == results.current_ms && lap.state.lap_start_ms == results.lap_start_ms
               && lap.state.best_display == results.best_display && lap.state.current_display == results.current_display && lap.state.best_notice == results.best_notice,
        "421390 state13 must return before all lap/best/clock/handoff arithmetic even after403dd0 refreshes the clock");
}

void source_timer_creation() {
    using namespace toca::simulation;
    SourceSessionStart start;
    start.timers.create(10000); //447a70's single external creation sample.
    expect(start.timers.clock == 10000 && start.timers.origins == std::array<std::uint32_t, 5>{10000, 10000, 10000, 10000, 10000},
        "447a70 must initialize all timer slots from its one cached clock sample");
    start.timers.clock = 12500; //First44a1d0 caller after resource loading.
    start.timers.reset(3); //447980, then its460170→421290.
    start.timers.reset(2);
    expect(
        start.timers.origins == std::array<std::uint32_t, 5>{10000, 10000, 12500, 12500, 10000}, "The first NoQual grid must reset only timers3/2 using its later caller sample");
    start.timers.clock = 15562;
    expect(start.advance_countdown() == SourceCountdownEvent::none && start.phase == 0,
        "Countdown must use the grid reset rather than the earlier creation origin at its strict threshold");
    start.timers.clock = 15563;
    expect(
        start.advance_countdown() == SourceCountdownEvent::housing && start.phase == 1 && start.timers.origins == std::array<std::uint32_t, 5>{10000, 10000, 15563, 12500, 10000},
        "The first countdown consumer resets timer2 while preserving creation and grid timer ownership");
    start.timers.create(50000); //A later449f50→447a70 warm creation.
    expect(start.timers.clock == 50000 && start.timers.origins == std::array<std::uint32_t, 5>{50000, 50000, 50000, 50000, 50000} && start.phase == 1,
        "Warm timer creation must replace every origin without claiming41ec50's separate phase reset");
}

void source_nearest_recovery() {
    using namespace toca::simulation;
    constexpr std::int32_t one = 65536;
    std::vector<std::uint8_t> points(4 + 80 * 64);
    put32(points, 0, 80);
    for (std::size_t point = 0; point < 80; ++point) {
        const auto at = 4 + point * 64;
        put32(points, at, 200 * one);
        put32(points, at + 8, 10 * one);
        put32(points, at + 36, 200 * one);
        put32(points, at + 44, std::uint32_t(-10 * one));
        put32(points, at + 12, 4000 * one);
        put32(points, at + 20, 4000 * one);
    }
    put32(points, 4 + 12, 0);
    put32(points, 4 + 20, 0);
    put32(points, 4 + 23 * 64 + 12, 100 * one + 3 * one / 4);
    put32(points, 4 + 23 * 64 + 20, 0);
    put32(points, 4 + 79 * 64, std::uint32_t(-200 * one));
    put32(points, 4 + 79 * 64 + 36, std::uint32_t(-200 * one));
    const fixed::Vector position{100 * one + 3 * one / 4, 0, 0};
    Session session(points, false);
    session.configure_route_recovery(2);
    session.set_human_control(false);
    for (std::uint64_t tick = 0; tick < 3; ++tick) {
        expect(session.update(tick, position).empty() && session.next_point() == 0, "AI-owned Session must advance its source cadence without executing human nearest recovery");
    }
    session.set_human_control(true);
    session.update(3, position);
    session.configure_route_recovery(2); //Repeated scene setup must preserve ticks.
    session.update(4, position);
    expect(session.next_point() == 0, "Human handback must retain the active source cadence rather than recover immediately");
    const auto events = session.update(5, position);
    expect(session.next_point() == 23 && events.empty() && session.completed_laps() == 0 && session.lap() == 1 && !session.split_sector(),
        "The ordinary Session must share4046a3's actual recovery jump after gate movement without inventing lap/checkpoint side effects");
    Session tie(points, false);
    tie.configure_route_recovery(0);
    tie.update(0, {50 * one + 3 * one / 4, 0, 0});
    expect(tie.next_point() == 0, "Equal shifted integer distances must retain the Session cursor on a refresh");
}

void source_timing() {
    using namespace toca::simulation;
    LapTimingState state;
    state.lap_start_ms = 60000;
    state.current_ms = 999;
    state.best_notice = 5;
    state = observe_lap_clock(state, 123456, true, true, 6);
    expect(state.current_ms == 63456 && state.current_display == std::array<std::int16_t, 3>{1, 3, 45} && state.best_notice == 4,
        "403dd0 must derive current clock/components and decrement a player's old notice before checkpoint consumption");
    state = complete_lap_timing(state, 123456);
    expect(state.lap == 2 && state.best_ms == 63456 && state.best_display == std::array<std::int16_t, 3>{1, 3, 45} && state.best_notice == 0x50 && state.current_ms == 0
               && state.lap_start_ms == 123456,
        "421390 must retain completed components, copy an unsigned best and restart timing before the sector0 split");
    auto unsigned_best = state;
    unsigned_best.best_ms = 100;
    unsigned_best.current_ms = 0x80000000;
    unsigned_best = complete_lap_timing(unsigned_best, 123457);
    expect(unsigned_best.best_ms == 100, "A large unsigned lap duration must not beat a small positive best through a signed comparison");
    auto ai = state;
    ai.current_display = {9, 8, 7};
    ai = observe_lap_clock(ai, 124456, true, false, 6);
    expect(ai.current_ms == 1000 && ai.current_display == std::array<std::int16_t, 3>{9, 8, 7} && ai.best_notice == 0x50,
        "AI actor clocks must advance while retaining their unformatted component fields and notice");
    ai.lap_start_ms = 0xfffffff0;
    ai = observe_lap_clock(ai, 0x20, true, true, 6);
    expect(ai.current_ms == 48 && ai.current_display == std::array<std::int16_t, 3>{0, 0, 4}, "Source clock subtraction must preserve32-bit wrap before hundredth truncation");
    ai.lap_start_ms = 0;
    ai = observe_lap_clock(ai, 3600000, true, true, 6);
    expect(ai.record_flag == 1 && ai.current_display == std::array<std::int16_t, 3>{59, 59, 99} && ai.current_ms == 3600000,
        "The source minute cap must saturate displayed fields while retaining the actual elapsed clock");
    ai = observe_lap_clock(ai, 3600000, true, true, 6);
    expect(ai.current_ms == 3599000 && ai.current_display == std::array<std::int16_t, 3>{59, 59, 99},
        "A previously invalidated time record must retain the distinct source sentinel elapsed value");

    LapTimingState final_lap;
    final_lap.lap = 1;
    final_lap.current_ms = 65432;
    final_lap.current_display = {1, 5, 43};
    const auto finish = advance_lap_timing(final_lap, {100000, 1, 2, 6});
    expect(finish.action == LapTimingAction::race_finish && finish.stop_horns && finish.state.lap == 2 && finish.state.current_ms == 65432 && finish.state.best_ms == 65432
               && finish.state.best_display == final_lap.current_display && finish.state.lap_start_ms == 100000,
        "421390 final lap must retain the completed elapsed time while copying the best and restarting its absolute clock before finish dispatch");
    const auto stopped = advance_lap_timing(final_lap, {100000, 1, 2, 10});
    expect(stopped.action == LapTimingAction::none && !stopped.stop_horns && stopped.state.lap == 1 && stopped.state.best_ms == 0,
        "The421390 global finish-state guard must precede all lap arithmetic");
    auto qualifying = advance_lap_timing(final_lap, {100000, 99, 1, 6, true, false, true});
    expect(qualifying.action == LapTimingAction::none && qualifying.state.lap == 2 && qualifying.state.best_ms == 65432 && qualifying.state.current_ms == 0,
        "A measured qualifying lap must share ordinary best-time arithmetic without triggering an outlap restart");
    qualifying = advance_lap_timing(final_lap, {100000, 99, 1, 4, true});
    expect(qualifying.action == LapTimingAction::qualifying_restart && qualifying.state.lap == 1 && qualifying.state.best_ms == 0
               && qualifying.state.best_display == std::array<std::int16_t, 3>{} && qualifying.state.lap_start_ms == 100000,
        "The qualifying AI-owned outlap must restart displayed lap1 and clear its provisional best before the separate controller handoff");
    final_lap.lap = 3;
    qualifying = advance_lap_timing(final_lap, {100000, 99, 3, 6, true, false, true});
    expect(qualifying.action == LapTimingAction::qualifying_finish && qualifying.state.lap == 4 && qualifying.state.best_ms == 65432,
        "The third measured qualifying lap must select its final handoff while retaining numerical lap4");
    RaceFinishState race;
    race.count = 0x12340000;
    race.timer2 = 99;
    race.timer3 = 123;
    const auto first = record_ai_finish(race, {}, 7, 100000, 9000);
    expect(first.finished == 1 && first.total_hundredths == 9100 && race.count == 0x12340001 && race.order[0] == 7 && race.timer3 == 0 && race.timer2 == 99,
        "The first AI finisher must reset timer3, preserve the upper finish-count bytes and store its actual actor/time");
    race.timer3 = 456;
    auto second_finish = record_ai_finish(race, {}, 3, 100099, 9000);
    expect(second_finish.total_hundredths == 9109 && race.timer2 == 456 && race.timer3 == 456 && race.order[1] == 3 && race.count == 0x12340002,
        "The second AI finisher must snapshot the first gap timer and truncate total time to hundredths");
    second_finish.notice = second_finish.best_notice = 80;
    second_finish = record_ai_finish(race, second_finish, 3, 200000, 9000);
    expect(second_finish.total_hundredths == 9109 && second_finish.notice == 0 && second_finish.best_notice == 0 && race.count == 0x12340002,
        "An already-finished actor must clear notices without a duplicate finish-order/time write");
    expect(finish_time_hundredths(0x20, 0xfffffff0) == 4 && finish_time_hundredths(0, 1000) == -100,
        "Finish elapsed time must preserve source32-bit subtraction and signed hundredth truncation");

    SplitDisplays displays;
    auto& split_display = displays.players[0];
    split_display.mode = 1;
    consume_checkpoint_display(displays, 0, 1, {true, 80, 3}, {1, 3, 45});
    expect(split_display.active == 1 && split_display.ticks == 0 && split_display.leader == 3 && split_display.reference[1] == 6345
               && split_display.components == std::array<std::int16_t, 3>{1, 3, 45} && std::string(split_display.text.data()) == "01:03.45"
               && std::string(split_display.leader_text.data()) == "+00:00.08",
        "41a840 must initialize the personal sector reference independently of the preceding-rank gap text");
    split_display.mode = 2;
    consume_checkpoint_display(displays, 0, 1, {true, 0, -1}, {1, 2, 34});
    expect(split_display.current[1] == 6234 && split_display.reference[1] == 6345 && std::string(split_display.text.data()) == "-00:01.11"
               && std::string(split_display.leader_text.data()) == "+00:00.08" && split_display.leader == -1,
        "A faster sector must publish a negative personal difference, retaining reference and unused leader text until the lap completes");
    split_display.reference[0] = 10000;
    split_display.current[2] = 9000;
    consume_checkpoint_display(displays, 0, 0, {true, 0, -1}, {1, 39, 99});
    expect(split_display.reference == split_display.current && displays.persisted == split_display.reference && std::string(split_display.text.data()) == "-00:00.01",
        "Only a negative completed-lap comparison must promote all three measured sectors and persist player0's references");
    consume_checkpoint_display(displays, 0, 0, {true, 0, -1}, {1, 39, 99});
    expect(std::string(split_display.text.data()) == "+00:00.00", "Equal personal times must use the positive zero display and preserve their reference");
    split_display.mode = 1;
    consume_checkpoint_display(displays, 0, 2, {true, 0, -1}, {1, 31, 23}, true, true);
    expect(split_display.current[2] == 9123 && split_display.reference[2] == 9000 && std::string(split_display.text.data()) == "+00:01.23",
        "Time Trial record/playback must bypass first-lap reference initialization exactly at41a840's gate");
    split_display.ticks = 9;
    advance_split_display(split_display, true, false);
    expect(split_display.ticks == 9 && !split_display_visible(split_display, false), "Paused split timers retain their original early hidden flash phase");
    advance_split_display(split_display, false, false);
    expect(split_display.ticks == 10 && split_display_visible(split_display, false), "The original split text appears during low-nibble10..15 before tick48");
    split_display.ticks = 48;
    expect(split_display_visible(split_display, false) && !split_display_visible(split_display, true), "After tick47 the split text stays visible only for unfinished players");
    split_display.ticks = 200;
    advance_split_display(split_display, false, true);
    expect(split_display.active == 1 && split_display.ticks == 200, "A finished player's hidden split timer must remain retained");
    advance_split_display(split_display, false, false);
    expect(split_display.active == 0 && split_display.ticks == 201, "41ab70 must expire the split after200 actual unpaused active updates");

    CheckpointTimes table;
    auto split = record_checkpoint_time(table, 3, 0, 1, 1, 1000);
    expect(!split.written && table[0].clock == 0, "4212d0 sector0 must skip the initial displayed lap");
    split = record_checkpoint_time(table, 3, 1, 1, 1, 1000);
    expect(split.written && split.leader == -1 && table[16].actor == 3 && table[16].lap == 1,
        "The first rank in a sector must write its actual source actor/lap without a preceding rank");
    split = record_checkpoint_time(table, 7, 1, 2, 1, 1080);
    expect(split.written && split.leader == 3 && split.delta_ms == 80, "Later ascending actor consumption must observe a preceding rank written in the same update");
    split = record_checkpoint_time(table, 7, 1, 2, 2, 1100);
    expect(split.leader == -1 && split.delta_ms == 0, "A preceding rank from a different lap must not supply a split gap");
    split = record_checkpoint_time(table, 3, 0, 1, state.lap, state.lap_start_ms);
    expect(split.written && table[0].lap == 2 && table[0].clock == 123456, "Sector0 must consume the newly incremented lap and restarted absolute clock");

    Session session(route(), false);
    for (const auto time : {3063u, 6126u, 9189u}) {
        session.update(time, circle(-.1));
    }
    std::uint64_t time = 9289;
    session.update(time, circle(.1));
    std::vector<std::uint32_t> sectors, laps;
    for (std::uint32_t point = 1; point <= 24; ++point) {
        session.update(time += 100, circle(double(point % 24) + .1));
        if (const auto sector = session.split_sector()) {
            sectors.push_back(*sector);
            laps.push_back(session.lap());
        }
    }
    expect(sectors == std::vector<std::uint32_t>{1, 2, 0} && laps == std::vector<std::uint32_t>{1, 1, 2},
        "The normal Session must emit actual checkpoint markers and expose the incremented lap at its sector0 event");
    session.update(time + 40, circle(.1));
    expect(!session.split_sector(), "A retained route position must not replay the previous split event");
}

void synthetic() {
    using namespace toca::simulation;
    SourceSessionStart start;
    start.timers.origins = {101, 102, 1000, 104, 105};
    start.timers.clock = 999;
    expect(start.timers.read(2) == 0 && start.advance_countdown() == SourceCountdownEvent::none, "Source timer must suppress future origins before unsigned subtraction");
    start.timers.clock = 4062;
    expect(start.advance_countdown() == SourceCountdownEvent::none, "Source countdown must retain the exact quantized3s boundary");
    start.timers.clock = 4063;
    expect(start.advance_countdown() == SourceCountdownEvent::housing && start.phase == 1 && start.timers.origins[2] == 4063,
        "Countdown must advance only once and reset its actual origin");
    start.timers.clock = 7126;
    expect(start.advance_countdown() == SourceCountdownEvent::red, "Second source countdown phase must emit red");
    start.timers.clock = 10189;
    expect(start.advance_countdown() == SourceCountdownEvent::green && start.timers.origins == std::array<std::uint32_t, 5>{101, 10189, 10189, 104, 105},
        "Green must reset only timer1/2 and preserve the remaining observed origins");
    start.timers.clock = 11751;
    start.expire_green();
    expect(start.phase == 3, "Green expiry must retain the quantized1.5s boundary");
    start.timers.clock = 11752;
    start.expire_green();
    expect(start.phase == 4, "The separate cached-clock render gate must expire green");
    const auto finish_origins = start.timers.origins;
    start.finish_wrapper();
    expect(start.phase == 0 && start.timers.origins == finish_origins && start.timers.clock == 11752,
        "448c33 must clear only the raw countdown phase while retaining its timer bank and cached clock");
    start.timers.origins[2] = 0;
    start.timers.clock = 32'768'000;
    expect(start.advance_countdown() == SourceCountdownEvent::none, "Negative signed timer results must not pass the source phase gate");
    start.phase = 65535;
    start.timers.clock = 3063;
    expect(start.advance_countdown() == SourceCountdownEvent::none && start.phase == 0, "Countdown phase must wrap as the original WORD without inventing a cue");
    expect(session_timer_q16(3000) == 0x30000 && session_timer_q16(3062) == 0x30000 && session_timer_q16(3063) == 0x31000,
        "Original timer must quantize to1/16s before strict phase comparisons");
    const auto bytes = route();
    Session race(bytes, false);
    expect(race.state() == 9 && race.start_phase() == 0 && !race.released() && !race.qualifying_rollout() && race.release_counter() == 0,
        "No-qualifying start must begin in state9/phase0 with zero release counter");
    expect(race.update(3000, circle(-.1)).empty() && race.start_phase() == 0, "Source strict3s gate changed");
    expect(race.update(3063, circle(-.1)).empty() && race.start_phase() == 1, "First original phase must show housing without a cue");
    auto events = race.update(6126, circle(-.1));
    expect(events == std::vector{SessionEvent::red} && race.start_phase() == 2 && !race.released(), "Second phase must emit red only");
    events = race.update(9189, circle(-.1));
    expect(events == std::vector{SessionEvent::go, SessionEvent::released} && race.start_phase() == 3 && race.state() == 9 && race.grid_held() && race.release_counter() == 360,
        "Green must emit ordered go/release and start local timing while retaining the state9 grid clutch");
    expect(race.update(9189, circle(-.1)).empty(), "Repeated active timestamp must not replay start events");
    expect(
        race.state() == 6 && !race.grid_held() && race.release_counter() == 359, "The following wrapper must commit state6, open the grid clutch and decrement before its AI pass");
    race.update(10689, circle(-.1));
    expect(race.start_phase() == 3, "Green must remain at the exact1.5s threshold");
    race.update(10752, circle(-.1));
    expect(race.start_phase() == 4 && race.lap_time_ms() == 1563, "Green expiry must be a cached time transition independent of frames");
    race.update(11000, circle(.1));
    expect(race.completed_laps() == 0 && race.lap() == 1 && race.lap_time_ms() == 1811,
        "The first grid-to-start-line crossing must not complete a lap or restart the race release timer");
    expect(!race.start_line_pending(), "The initial grid passage must clear the original position consumer's start flag");
    auto reverse_race = race;
    reverse_race.update(11100, circle(-.1));
    expect(reverse_race.start_line_pending(), "Reversing over the start line must restore the source position flag");
    reverse_race.update(11200, circle(.1));
    expect(!reverse_race.start_line_pending() && reverse_race.completed_laps() == 0,
        "Returning over the reversed start line must clear its position flag without creating a completed lap");

    Session waiting(bytes, true);
    expect(waiting.update(60000, circle(6.1), true).empty() && waiting.qualifying_rollout() && waiting.state() == 4,
        "Qualifying rollout must not hand over on elapsed time or confirmation before the original start-line crossing");
    expect(waiting.end_qualifying() && waiting.state() == 7 && !waiting.qualifying_rollout() && waiting.best_lap_ms() == 3'600'000 && waiting.completed_laps() == 0,
        "Ending an unmeasured qualifying approach must use the original60-minute record without inventing a completed lap");
    expect(!waiting.end_qualifying() && !race.end_qualifying(), "Only an unfinished qualifying session may enter the skip transition");
    expect(waiting.update(61000, circle(6.1), true).empty() && waiting.state() == 7, "Skipped qualifying must retain the original strict one-second finish input gate");
    expect(waiting.update(61063, circle(6.1), true) == std::vector{SessionEvent::results_entered}, "Skipped qualifying must enter the ordinary result state");

    Session qualifying(bytes, true);
    std::uint64_t time = 0;
    expect(qualifying.next_point() == 7 && qualifying.state() == 4 && qualifying.qualifying_rollout(),
        "Qualifying must preserve source count-17 target and automatic rollout ownership");
    for (std::uint32_t point = 7; point <= 24; ++point) {
        events = qualifying.update(time += 100, circle(double(point % 24) + .1));
        expect(!has(events, SessionEvent::red) && !has(events, SessionEvent::go), "Qualifying must not inherit the race countdown");
        if (point < 24) {
            expect(qualifying.qualifying_rollout() && !has(events, SessionEvent::released), "Automatic qualifying ownership must remain until the actual ordered start line");
        }
    }
    expect(events == std::vector{SessionEvent::released} && !qualifying.qualifying_rollout() && qualifying.lap() == 1 && qualifying.completed_laps() == 0
               && qualifying.lap_time_ms() == 0,
        "First qualifying start line must restore player ownership, discard the out-lap and release measured lap1");
    // 403dd0 tests one current plane, even when a fast body has passed several.
    // Further calls may catch up at that same position; there is no swept loop.
    auto fast = qualifying;
    for (std::uint32_t expected = 2; expected <= 4; ++expected) {
        events = fast.update(time + (expected - 1) * 40, circle(3.1));
        expect(fast.next_point() == expected && events.empty() && fast.completed_laps() == 0, "Passing several POINTS planes must consume only one ordered boundary per update");
    }
    fast.update(time + 160, circle(3.1));
    expect(fast.next_point() == 4, "Route catch-up must stop at the first unpassed plane");
    fast.update(time + 200, circle(1.1));
    expect(fast.next_point() == 3, "Reverse travel must rewind only one route boundary per update");

    // PITS resets the route cursor without granting the missing checkpoints.
    // Qualifying restarts its time origin at that unmeasured start crossing.
    auto incomplete = qualifying;
    auto pit_time = time;
    for (std::uint32_t point = 1; point <= 5; ++point) {
        incomplete.update(pit_time += 100, circle(double(point) + .1));
    }
    incomplete.enter_pit();
    events = incomplete.update(pit_time += 100, circle(.1));
    expect(events.empty() && incomplete.next_point() == 1 && incomplete.completed_laps() == 0 && incomplete.lap_time_ms() == 600,
        "An early qualifying start crossing must not award a lap or discard this update's already computed time");
    incomplete.update(pit_time + 40, circle(.1));
    expect(incomplete.lap_time_ms() == 40 && incomplete.best_lap_ms() == 0 && incomplete.last_lap_ms() == 0,
        "An incomplete qualifying circuit must restart timing without inventing last or best records");
    for (std::uint32_t point = 1; point <= 24; ++point) {
        events = incomplete.update(pit_time += 100, circle(double(point % 24) + .1));
    }
    expect(events == std::vector{SessionEvent::lap_complete} && incomplete.completed_laps() == 1 && incomplete.last_lap_ms() == 2400,
        "Only the subsequent complete checkpoint circuit may finish a lap timed from the unmeasured start crossing");
    // Crossing backwards and forwards over the same line must not create laps.
    qualifying.update(time += 100, circle(-.1));
    qualifying.update(time += 100, circle(.1));
    expect(qualifying.completed_laps() == 0, "Reverse start-line bounce must not create a measured lap");
    for (std::uint32_t point = 1; point <= 24; ++point) {
        events = qualifying.update(time += 100, circle(double(point % 24) + .1));
    }
    expect(has(events, SessionEvent::lap_complete) && qualifying.completed_laps() == 1 && qualifying.lap() == 2 && qualifying.last_lap_ms() == 2600
               && qualifying.best_lap_ms() == 2600 && qualifying.lap_time_ms() == 0,
        "Sequential original checkpoints must complete and time the first measured lap");
    auto skipped = qualifying;
    expect(skipped.end_qualifying() && skipped.best_lap_ms() == 2600 && skipped.completed_laps() == 1 && skipped.lap_time_ms() == 2600,
        "Ending qualifying after a completed lap must keep its actual best for classification");
    for (int circuit = 0; circuit < 2; ++circuit) {
        for (std::uint32_t point = 1; point <= 24; ++point) {
            events = qualifying.update(time += 100, circle(double(point % 24) + .1));
        }
    }
    expect(events == std::vector{SessionEvent::lap_complete, SessionEvent::qualifying_finished} && qualifying.state() == 7 && !qualifying.qualifying_rollout()
               && qualifying.completed_laps() == 3 && qualifying.lap() == 4 && qualifying.best_lap_ms() == 2400 && qualifying.lap_time_ms() == 2400,
        "Third measured qualifying lap must hand off exactly once and retain the completed timing record");
    const auto finish_time = time;
    qualifying.update(finish_time + 40, circle(1.1));
    expect(qualifying.next_point() == 2 && qualifying.completed_laps() == 3 && qualifying.lap_time_ms() == 2400,
        "Finished actor must continue source route progress for AI without counting or timing another lap");
    qualifying.update(finish_time + 1000, circle(.1), true);
    expect(qualifying.state() == 7 && !qualifying.finish_camera_active(), "State7 must reject input at the exact one-second timer threshold");
    qualifying.update(finish_time + 2000, circle(.1));
    expect(qualifying.finish_camera_active() && qualifying.lap_time_ms() == 2400, "State7 must switch to the authored camera at two seconds without advancing frozen timing");
    events = qualifying.update(finish_time + 2063, circle(.1), true);
    expect(events == std::vector{SessionEvent::results_entered} && qualifying.state() == 8, "Accepted finish input must enter actual qualifying results");
    events = qualifying.update(finish_time + 2103, circle(.1), true);
    expect(events == std::vector{SessionEvent::race_requested} && qualifying.state() == 9, "Full normal-resolution results must request the classified race grid on confirmation");

    Session automatic(bytes, true);
    time = 0;
    for (std::uint32_t point = 7; point <= 24 + 3 * 24; ++point) {
        automatic.update(time += 100, circle(double(point % 24) + .1));
    }
    const auto completed_time = time;
    automatic.update(completed_time + 6000, circle(.1));
    expect(automatic.state() == 7, "Automatic qualifying continuation must remain strict at six seconds");
    events = automatic.update(completed_time + 6063, circle(.1));
    expect(events == std::vector{SessionEvent::results_entered}, "Automatic qualifying continuation must enter results after the source timer quantum");
    expect(automatic.update(completed_time + 6063, circle(.1)).empty(), "Repeated timestamp must not repeat finish events");
    events = automatic.update(completed_time + 6063 + 40000, circle(.1));
    expect(events == std::vector{SessionEvent::race_requested}, "Results timeout must request race at the original forty-second threshold");

    const auto finish_race = [&](toca::GameMode mode, std::uint32_t local_players = 1) {
        Session finite(bytes, false, mode, 2);
        finite.configure_local_players(local_players);
        finite.update(3063, circle(-.1));
        finite.update(6126, circle(-.1));
        finite.update(9189, circle(-.1));
        std::uint64_t tick = 9300;
        finite.update(tick, circle(.1));
        for (std::uint32_t lap = 0; lap < 2; ++lap) {
            for (std::uint32_t point = 1; point <= 24; ++point) {
                const auto result = finite.update(tick += 100, circle(double(point % 24) + .1));
                if (point == 24) {
                    expect(has(result, SessionEvent::lap_complete) && has(result, SessionEvent::race_finished) == (lap == 1),
                        "Finite races must emit their finish exactly on the final ordered checkpoint lap");
                }
            }
        }
        expect(finite.state() == 10 && finite.completed_laps() == 2 && finite.lap() == 2 && finite.race_time_ms() > 0 && finite.start_phase() == 4,
            "Championship and Single Race must finish their selected finite lap limit with actual accumulated race timing");
        return finite;
    };
    auto championship = finish_race(toca::GameMode::championship);
    const auto championship_finish = championship.finish_time_ms();
    const auto championship_total = championship.race_time_ms();
    championship.update(championship_finish + 4999, circle(1.1));
    expect(championship.state() == 10 && championship.race_time_ms() == championship_total && championship.start_phase() == 0,
        "The first finish wrapper must clear countdown phase while preserving frozen race totals until the five-second gate");
    expect(
        championship.update(championship_finish + 5000, circle(1.1)) == std::vector{SessionEvent::results_entered} && championship.state() == 13 && championship.start_phase() == 0,
        "Championship without speech must enter results at the original threshold and retain its cleared phase");
    expect(championship.update(championship_finish + 5040, circle(1.1), true) == std::vector{SessionEvent::race_return_requested}
               && championship.update(championship_finish + 5080, circle(1.1), true).empty(),
        "Championship result confirmation must request progression exactly once");
    auto speaking = finish_race(toca::GameMode::championship);
    const auto speech_finish = speaking.finish_time_ms();
    speaking.set_finish_speech(true, true);
    speaking.update(speech_finish + 5000, circle(1.1), true);
    expect(speaking.state() == 10, "Started championship finish speech must retain state10 beyond five seconds despite confirmation");
    auto timed_out_speech = speaking;
    speaking.set_finish_speech(true, false);
    expect(speaking.update(speech_finish + 5040, circle(1.1)) == std::vector{SessionEvent::results_entered}, "The actual end of started championship speech must release results");
    timed_out_speech.update(speech_finish + 14999, circle(1.1));
    expect(timed_out_speech.state() == 10, "Playing championship finish speech must retain its fifteen-second safety limit");
    expect(timed_out_speech.update(speech_finish + 15000, circle(1.1)) == std::vector{SessionEvent::results_entered},
        "A stuck championship speech voice must release results at fifteen seconds");
    auto ordinary_speech = finish_race(toca::GameMode::single_race);
    ordinary_speech.set_finish_speech(true, true);
    expect(ordinary_speech.update(ordinary_speech.finish_time_ms() + 40, circle(1.1)).empty() && ordinary_speech.state() == 10 && ordinary_speech.start_phase() == 0,
        "Single Race must clear the raw start phase on its first actual state10 wrapper");
    expect(ordinary_speech.update(ordinary_speech.finish_time_ms() + 5000, circle(1.1)) == std::vector{SessionEvent::results_entered} && ordinary_speech.state() == 13
               && ordinary_speech.start_phase() == 0,
        "Single Race must retain its finish timing and cleared raw phase into results even when a speech status is supplied");
    expect(ordinary_speech.update(ordinary_speech.finish_time_ms() + 5040, circle(1.1), true) == std::vector{SessionEvent::race_return_requested},
        "One-player Single Race must retain its results confirmation before opening Race Again");

    auto split_finish = finish_race(toca::GameMode::single_race, 2);
    const auto split_total = split_finish.race_time_ms();
    const auto last_finish = split_finish.finish_time_ms() + 60000;
    split_finish.set_finish_group_complete(false);
    expect(split_finish.update(last_finish, circle(1.1), true).empty() && split_finish.state() == 10 && !split_finish.finish_camera_active()
               && split_finish.race_time_ms() == split_total,
        "The first local finisher must wait for the other human beyond five seconds without confirmation advancing the session");
    split_finish.set_finish_group_complete(true);
    auto wrapped_finish_timer = split_finish;
    expect(wrapped_finish_timer.update(last_finish + 32768000, circle(1.1)).empty() && wrapped_finish_timer.state() == 10,
        "The local multiplayer timer gate must interpret a wrapped negative Q16 result as signed, as448d27's jle does");
    expect(split_finish.update(last_finish + 5000, circle(1.1), true).empty() && split_finish.state() == 10,
        "Two-player Race Again must use a strict five-second gate measured from the shared last finish");
    expect(split_finish.update(last_finish + 5062, circle(1.1), true).empty() && split_finish.state() == 10,
        "Confirmation must not bypass the final millisecond of the quantized two-player finish gate");
    expect(split_finish.update(last_finish + 5063, circle(1.1)) == std::vector{SessionEvent::race_return_requested} && split_finish.state() == 10
               && split_finish.race_time_ms() == split_total,
        "The first source timer quantum beyond five seconds must request Race Again automatically while retaining state10 and the measured total");
    expect(split_finish.update(last_finish + 5063, circle(1.1), true).empty() && split_finish.update(last_finish + 40000, circle(1.1), true).empty() && split_finish.state() == 10,
        "A retained two-player finish must emit its automatic overlay request only once and never enter the one-player result state");

    Session trial(bytes, false, toca::GameMode::time_trial);
    expect(trial.qualifying_rollout() && trial.state() == 4 && !trial.end_qualifying(), "Time Trial must use the automatic approach and exclude qualifying skip");
    time = 0;
    for (std::uint32_t point = 7; point <= 24; ++point) {
        events = trial.update(time += 100, circle(double(point % 24) + .1));
    }
    expect(events == std::vector{SessionEvent::released} && trial.completed_laps() == 0, "Time Trial must discard its out-lap at the ordered start line");
    for (int lap = 0; lap < 5; ++lap) {
        for (std::uint32_t point = 1; point <= 24; ++point) {
            events = trial.update(time += 100, circle(double(point % 24) + .1));
        }
        expect(events == std::vector{SessionEvent::lap_complete} && trial.state() == 6 && !trial.finish_camera_active(),
            "Time Trial must continue after each measured lap without qualifying classification or race finish");
        expect(trial.lap_time_ms() == 0 && trial.lap_display_ms() == 2400,
            "The first new-lap frame must retain403dd0's completed display components while its elapsed lap clock resets");
    }
    expect(trial.completed_laps() == 5 && trial.best_lap_ms() == 2400 && !trial.end_qualifying(), "Time Trial must keep measured records through unlimited attempts");
    trial.enter_pit();
    events = trial.update(time += 500, circle(.1));
    expect(events == std::vector{SessionEvent::attempt_restarted} && trial.completed_laps() == 5 && trial.lap_time_ms() == 0 && trial.best_lap_ms() == 2400,
        "Time Trial start crossing without ordered checkpoints must restart the attempt without promoting an incomplete lap");
}

void reference(const char* root) {
    using namespace toca::simulation;
    const toca::AssetStore store(root);
    const auto archive = store.open_archive("data/donlkeep.bfp");
    const auto member = archive.member(1);
    Session qualifying(member.bytes(), true), race(member.bytes(), false);
    const auto start = toca::scene::single_qualifying_start(member.bytes());
    expect(qualifying.next_point() == start.source_index + 1, "Canonical qualifying target differs from original placement");
    const auto grid = toca::scene::decode_start_grid(archive.member(3).bytes());
    const auto start_grid = toca::scene::single_race_start(grid);
    const std::array<std::int32_t, 3> position{start_grid.xz[0], 0, start_grid.xz[1]};
    std::vector<SessionEvent> observed;
    for (std::uint64_t milliseconds = 40; milliseconds <= 11200; milliseconds += 40) {
        const auto events = race.update(milliseconds, position);
        observed.insert(observed.end(), events.begin(), events.end());
    }
    expect(observed == std::vector{SessionEvent::red, SessionEvent::go, SessionEvent::released} && race.start_phase() == 4 && race.completed_laps() == 0,
        "Canonical stationary grid start must emit only actual race start events");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            source_caller_lifecycle();
            source_release_counter();
            source_results_clock();
            source_timer_creation();
            source_timing();
            source_nearest_recovery();
            synthetic();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-session-tests [canonical-root]");
        }
        std::cout << "PASS: original local start phases, route checkpoints and measured laps\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
