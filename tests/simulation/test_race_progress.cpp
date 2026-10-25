#include "race/race_progress.hpp"
#include "simulation/cold_grid.hpp"
#include "simulation/finish_classification.hpp"
#include "simulation/source_race_progress.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void source_neighbor_order() {
    using namespace toca::simulation;
    std::array<SourceRaceOrderActor, 3> actors;
    actors[0] = {true, false, false, 1, 1, 0, 2, 1, 100, 0};
    actors[1] = {true, false, false, 1, 2, 1, 0, 2, 200, 9};
    actors[2] = {true, false, false, 1, 3, 2, 1, 0, 50, 0};
    const auto initial = actors;
    const auto old_ahead = advance_source_race_order(actors, 1, 10);
    expect(old_ahead == 0 && actors[1].ahead == 2 && actors[1].behind == 0 && actors[0].ahead == 1 && actors[2].behind == 1 && actors[1].rank == 1 && actors[0].rank == 2
               && actors[1].spatial_rank == 0 && actors[0].spatial_rank == 1,
        "402e10 must splice the adjacent ring while403dd0 retains the old ahead pointer for the gap after a safe rank exchange");
    actors = initial;
    actors[1].distance_delta_q16 = 10;
    advance_source_race_order(actors, 1, 10);
    expect(actors[1].rank == 2 && actors[0].rank == 1, "Equality at the half-route displacement threshold must prevent a source rank swap");
    expect(source_route_distance(10, 30, 100) == 80 && source_route_distance(110, 0, 100) == 100,
        "Source cumulative projection must wrap once below zero and cap only above track length");
    expect(source_forward_gap(90, 10, 100, {}, {}, false) == 20 && source_forward_gap(90, 10, 100, {}, {}, true) == 0,
        "Source forward gaps wrap in route space but use horizontal body distance for a nearby permanent player");
}

void expect_actor_order(const std::array<std::uint32_t, 16>& order) {
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        expect(sorted[actor] == actor, "Spatial ring must retain all sixteen original actors exactly once");
    }
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes.at(offset + index) = std::uint8_t(value >> (index * 8));
    }
}

std::array<std::int32_t, 3> circle(double index, double radius = 100, std::uint32_t count = 24) {
    constexpr double tau = 6.283185307179586;
    return {std::int32_t(std::cos(index * tau / count) * radius * 65536), 0, std::int32_t(std::sin(index * tau / count) * radius * 65536)};
}

std::vector<std::uint8_t> route(std::uint32_t count = 24) {
    std::vector<std::uint8_t> bytes(4 + count * 64);
    put32(bytes, 0, count);
    for (std::uint32_t index = 0; index < count; ++index) {
        for (const auto& [offset, radius] : {std::pair{0u, 90.0}, {12u, 100.0}, {36u, 110.0}}) {
            const auto position = circle(index, radius, count);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                put32(bytes, 4 + index * 64 + offset + axis * 4, std::uint32_t(position[axis]));
            }
        }
    }
    return bytes;
}

void qualifying_progress() {
    using namespace toca;
    constexpr std::uint32_t count = 130, local_id = 14, npc = 2;
    const auto bytes = route(count);
    simulation::QualifyingStartInitResult startup;
    startup.repeated.data.local_actor = local_id;
    std::iota(startup.repeated.grid.rank_to_actor.begin(), startup.repeated.grid.rank_to_actor.end(), 0);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        startup.repeated.actors[actor].mode = actor % 4 == local_id % 4 ? simulation::BodyMode::full : simulation::BodyMode::removed;
    }
    race::RaceProgress progress(bytes, 3, startup);
    simulation::Session local(bytes, true, GameMode::single_race, 3);
    race::RacePositions positions{};
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        const auto previous = count - 18 - 7 * ((actor + 16 - local_id) % 16);
        positions[actor] = circle(previous + .1, 100, count);
        expect(progress.active(actor) == (actor % 4 == local_id % 4), "Qualifying progression must keep the authored actor quarter");
        if (actor != local_id) {
            expect(progress.participant(actor).next_point() == previous + 1 && !progress.participant(actor).released(),
                "Every qualifying NPC must start at its own rotated POINTS cursor without a countdown or player handback");
        }
    }
    std::uint64_t clock = 0;
    for (std::uint32_t call = 0; call < 4 * count; ++call) {
        positions[npc] = circle(progress.participant(npc).next_point() + .1, 100, count);
        progress.update(clock += 40, local, positions);
    }
    expect(progress.completed_laps(npc) >= 4 && !progress.participant(npc).released() && !progress.finished(npc) && !progress.classified() && local.state() == 4,
        "NPC ordered laps must neither release the local approach nor finish/classify qualifying at three laps");
    const auto laps = progress.completed_laps(npc), point = progress.participant(npc).next_point();
    expect(local.end_qualifying(), "The local player alone can enter the qualifying finish");
    for (std::uint32_t call = 0; call < count + 1; ++call) {
        positions[npc] = circle(progress.participant(npc).next_point() + .1, 100, count);
        progress.update(clock += 40, local, positions);
    }
    expect(progress.completed_laps(npc) == laps && progress.participant(npc).next_point() != point && !progress.finished(npc),
        "Global qualifying finish must stop NPC lap production while route cursors and bodies continue");
    race::RaceProgress race_grid(bytes, 3, local_id, startup.repeated.grid.rank_to_actor);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        expect(race_grid.active(actor) && (actor == local_id || race_grid.participant(actor).state() == 9),
            "Earned race grid must create sixteen active race actors without retained qualifying roles");
    }
}

void source_classification() {
    using namespace toca::simulation;
    std::array<FinishClassificationActor, 16> actors;
    std::array<std::uint8_t, 16> order;
    std::iota(order.begin(), order.end(), 0);
    for (std::size_t actor = 0; actor < 16; ++actor) {
        actors[actor] = {10000 + std::int32_t(actor) * 100, 0, 60000, 2, std::int8_t(actor + 1)};
    }
    //A missing actor rank leaves a duplicate for422ac0, which replaces the
    //last copy. Zero winner and unsigned best-time units are source behavior.
    order[15] = 0;
    actors[0].total = 0;
    actors[15].total = 0;
    actors[15].lap = 1;
    actors[15].distance_q16 = 5 * 65536;
    classify_finish_totals(actors, order, 16, 0, 16, 10 * 65536);
    expect(order[0] == 0 && order[15] == 15 && actors[0].best_ms == 60000, "422ac0 must retain the first duplicate and repair the last with the missing actor");
    expect(actors[15].total == 69000, "4220c0 extrapolates a missing lap plus half a lap in source centiseconds");
    for (std::size_t rank = 1; rank < 16; ++rank) {
        expect(actors[order[rank - 1]].total <= actors[order[rank]].total, "Final classification swaps total values along the retained order");
    }
    std::array<std::int16_t, 16> scores{};
    std::array<std::uint8_t, 3> podiums{255, 8, 9};
    scores[0] = 30;
    scores[1] = 33;
    award_race_classification(scores, podiums, order, 0, 255);
    expect(scores[0] == 30 && scores[1] == 45 && podiums == std::array<std::uint8_t, 3>{0, 8, 9}, "422720 penalty floors prior signed points and podium counters wrap as bytes");
    scores.fill(100);
    const auto standing = classification_standings(scores);
    expect(standing == order, "422880 must keep actor order for strict equal-score ties");
    scores[3] = 32767;
    award_race_classification(scores, podiums, order, 15, 0);
    expect(scores[3] == -32761 && classification_standings(scores).back() == 3, "Source signed-short score overflow must participate in the signed standings comparison");
}

void check_split() {
    using namespace toca;
    const auto bytes = route();
    std::array<std::uint32_t, 16> grid{};
    grid[1] = 1;
    simulation::Session first(bytes, false, GameMode::single_race, 1);
    first.configure_local_players(2);
    race::RaceProgress progress(bytes, 1, 0, grid, 2);
    auto& second = progress.participant(1);
    first.set_finish_group_complete(false);
    second.set_finish_group_complete(false);
    race::RacePositions positions{};
    positions[0] = circle(-.1);
    positions[1] = circle(-.2);
    std::uint64_t now = 0;
    std::vector<simulation::SessionEvent> events;
    const auto tick = [&](std::uint64_t time) {
        now = time;
        events = first.update(now, positions[0]);
        progress.update(now, first, positions);
    };
    for (auto time : {3063u, 6126u, 9189u}) {
        tick(time);
    }
    expect(first.released() && second.released(), "Both local cars must share the start release");
    positions[0] = positions[1] = circle(.1);
    tick(now + 100);
    for (unsigned point = 1; point <= 24; ++point) {
        positions[1] = circle(double(point % 24) + .1);
        tick(now + 100);
        if (point == 8) {
            const auto& display = progress.checkpoint_display(1);
            expect(
                display.active == 1 && display.ticks == 1 && display.reference[1] > 0 && display.mode == 1 && display.text[0] != '-' && progress.checkpoint_display(0).active == 0,
                "The normal split race must consume player2's sector into its own timed display without inventing a player1 event");
        }
    }
    expect(progress.position(1) == 1 && progress.finished(1) && !progress.classified(), "Player two may win while player one keeps racing");
    const auto winner_time = second.race_time_ms();
    expect(progress.stop_horns_requested(), "A real finish must publish421390's shared horn-stop side effect");
    tick(now + 60000);
    expect(!progress.stop_horns_requested(), "A retained finish must not repeatedly emit a new horn-stop event");
    expect(second.state() == 10 && !second.finish_camera_active() && first.state() == 6 && !progress.classified(),
        "A finished local player must wait beyond the ordinary results timeout without stopping the other driver");
    for (unsigned point = 1; point <= 24; ++point) {
        positions[0] = circle(double(point % 24) + .1);
        tick(now + 100);
    }
    expect(progress.classified() && progress.finished(0) && progress.completed_laps(1) == 1, "Classify only after both actual finish crossings");
    scene::QualifyingData drivers;
    drivers.local_actor = 0;
    drivers.drivers[0].name = "FIRST";
    drivers.drivers[1].name = "SECOND";
    const auto results = progress.results(drivers);
    expect(results.count == 2 && results.rows[0].actor_id == 1 && results.rows[0].time_ms == winner_time && results.rows[1].actor_id == 0,
        "Two-player results must retain measured times and identities without phantom AI rows");
    first.set_finish_group_complete(true);
    second.set_finish_group_complete(true);
    const auto group_finish = now;
    tick(group_finish + 5000);
    expect(events.empty() && first.state() == 10 && second.state() == 10, "The shared finish timer must start at the second actual crossing and remain strict at five seconds");
    tick(group_finish + 5062);
    expect(events.empty() && first.state() == 10 && second.state() == 10,
        "Neither local view may enter the one-player table at the last millisecond before the next source timer quantum");
    tick(group_finish + 5063);
    expect(events == std::vector{simulation::SessionEvent::race_return_requested} && first.state() == 10 && second.state() == 10,
        "Both completed local views must retain state10 while automatically requesting Race Again after the strict timer gate");
    tick(group_finish + 40000);
    expect(events.empty() && first.state() == 10 && second.state() == 10 && second.race_time_ms() == winner_time && progress.results(drivers).rows[0].time_ms == winner_time,
        "The shared finish must retain measured classification and emit no duplicate overlay request or delayed result table");
}

void check() {
    using namespace toca;
    using simulation::SessionEvent;
    const auto bytes = route();
    std::array<std::uint32_t, 16> grid;
    std::iota(grid.begin(), grid.end(), 0);
    simulation::Session local(bytes, false, GameMode::single_race, 1);
    race::RaceProgress progress(bytes, 1, 15, grid);
    race::RacePositions positions;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        positions[actor] = circle(-.1 - double(actor) * .01);
    }
    std::uint64_t time = 0;
    std::vector<SessionEvent> events;
    const auto update = [&](std::uint64_t tick) {
        time = tick;
        events = local.update(time, positions[15]);
        progress.update(time, local, positions);
        expect_actor_order(progress.spatial_order());
    };
    for (const auto tick : {3063u, 6126u, 9189u}) {
        update(tick);
    }
    expect(local.released() && progress.position(15) == 16 && !progress.classified(), "All actual grid actors must share the start clock and preserve grid ties");
    positions.fill(circle(.1));
    update(time + 100);
    expect(progress.completed_laps(15) == 0 && progress.position(15) == 16 && progress.position(0) == 1,
        "An equal-position start crossing must preserve source neighbor chronology without awarding a lap or globally sorting the field");
    for (std::uint32_t point = 1; point <= 24; ++point) {
        positions[2] = circle(double(point % 24) + .1);
        if (point <= 10) {
            positions[3] = positions[2];
        }
        update(time + 100);
    }
    expect(progress.finished(2) && progress.completed_laps(2) == 1 && progress.position(2) == 1 && local.state() == 6,
        "A real rival's ordered final crossing must preserve its finish before the local actor");
    expect(std::any_of(progress.checkpoint_times().begin() + 16, progress.checkpoint_times().end(),
               [](const auto& split) {
                   return split.actor == 2 && split.lap == 1 && split.clock != 0;
               }),
        "The normal race path must consume a rival's actual Session checkpoint event into the shared source timing table");
    {
        auto mixed = progress;
        auto driving_local = local;
        auto moving = positions;
        const auto spatial_before = mixed.spatial_order();
        for (std::uint32_t point = 1; point <= 15; ++point) {
            moving[2] = circle(double(point) + .1);
            const auto tick = time + point * 40;
            driving_local.update(tick, moving[15]);
            mixed.update(tick, driving_local, moving);
            expect_actor_order(mixed.spatial_order());
        }
        expect(mixed.spatial_order() != spatial_before && mixed.position(2) == 1 && mixed.finished(2) && !mixed.finished(3) && !mixed.classified(),
            "A finished rival coasting past an unfinished actor must update proximity neighbors while keeping its earned race position");
    }
    {
        auto removal = progress;
        auto driving_local = local;
        auto moving = positions;
        removal.remove(3);
        removal.remove(2); //Already finished: hiding must preserve the finish.
        removal.remove(3); //Repeated host notification must be harmless.
        expect(!removal.active(3) && removal.retired(3) && !removal.finished(3) && removal.position(3) == 16,
            "An unfinished removed actor must leave the active ring and move to the rear of classification");
        expect(!removal.active(2) && removal.finished(2) && !removal.retired(2) && removal.position(2) == 1,
            "A finished actor disappearing must retain its measured finish and earned rank");
        const auto ring = removal.spatial_order();
        expect_actor_order(ring);
        expect(std::find(ring.begin(), ring.begin() + 14, 2) == ring.begin() + 14 && std::find(ring.begin(), ring.begin() + 14, 3) == ring.begin() + 14,
            "Removed identities must not appear in the active proximity ring");
        const auto removed_point = removal.next_points(driving_local)[3];
        for (std::uint32_t point = 1; point <= 24; ++point) {
            moving[15] = circle(double(point % 24) + .1);
            moving[3] = moving[15];
            const auto tick = time + point * 100;
            driving_local.update(tick, moving[15]);
            removal.update(tick, driving_local, moving);
            expect_actor_order(removal.spatial_order());
        }
        expect(removal.classified() && removal.position(15) == 2 && removal.position(3) == 16 && removal.next_points(driving_local)[3] == removed_point
                   && removal.completed_laps(3) == 0,
            "Removed cars must stop consuming checkpoints while the remaining field finishes and classifies");
        scene::QualifyingData identities;
        identities.local_actor = 15;
        const auto classified = removal.results(identities);
        expect(classified.rows.back().actor_id == 3 && classified.rows.back().retired && !classified.rows.front().retired,
            "The result must distinguish an actual retirement from a hidden finisher");
    }
    for (std::uint32_t point = 1; point <= 24; ++point) {
        positions[15] = circle(double(point % 24) + .1);
        update(time + 100);
    }
    expect(events == std::vector{SessionEvent::lap_complete, SessionEvent::race_finished} && local.state() == 10 && local.lap() == 1 && local.completed_laps() == 1,
        "Selected finite lap count must emit the actual local finish once and retain the final displayed lap");
    expect(progress.classified() && progress.position(15) == 2 && progress.position(3) == 3 && !progress.finished(3) && progress.completed_laps(3) == 0,
        "Actual finish order and unfinished rivals' true progress must determine all16 classification positions");
    scene::QualifyingData drivers;
    drivers.local_actor = 15;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        drivers.drivers[actor].name = "DRIVER" + std::to_string(actor);
        drivers.drivers[actor].car_id = actor / 2;
    }
    const auto results = progress.results(drivers);
    expect(results.local_rank == 1 && results.rows[0].actor_id == 2 && results.rows[1].actor_id == 15 && results.rows[2].actor_id == 3
               && results.rows[0].time_ms < results.rows[1].time_ms && results.rows[2].time_ms >= results.rows[1].time_ms && !results.rows[2].finished,
        "Race table must retain measured finish times and real progress, independent of generated qualifying records");
    {
        auto classified = progress;
        auto finished_local = local;
        auto moving = positions;
        const auto spatial_before = classified.spatial_order();
        bool crossed_during_results = false;
        for (std::uint32_t point = 1; point <= 158; ++point) {
            moving[2] = circle(double(point) + .1);
            moving[3] = circle(double((10 + point) % 24) + .1);
            const auto tick = time + point * 40;
            finished_local.update(tick, moving[15]);
            classified.update(tick, finished_local, moving);
            expect_actor_order(classified.spatial_order());
            crossed_during_results = crossed_during_results || (finished_local.state() == 13 && classified.next_points(finished_local)[3] == 1);
        }
        expect(classified.spatial_order() != spatial_before && classified.position(2) == 1 && classified.position(15) == 2,
            "Global finish states must continue spatial neighbor updates independently of immutable result positions");
        expect(crossed_during_results && classified.next_points(finished_local)[3] == 1 && !classified.finished(3) && classified.completed_laps(3) == 0,
            "Ordered start-line passages in global states10 and13 must advance an unfinished actor's route cursor without setting the live finish flag or counting another lap");
        const auto after = classified.results(drivers);
        expect(after.local_actor == results.local_actor && after.local_rank == results.local_rank, "Coasting must preserve the classified local actor and finish rank");
        for (std::size_t rank = 0; rank < results.rows.size(); ++rank) {
            const auto& before = results.rows[rank];
            const auto& row = after.rows[rank];
            expect(row.actor_id == before.actor_id && row.car_id == before.car_id && row.driver == before.driver && row.time_ms == before.time_ms
                       && row.completed_laps == before.completed_laps && row.finished == before.finished,
                "Coasting after local finish must preserve every actual finish flag, completed lap, classified total and row");
        }
    }
    const auto finish = time;
    positions[15] = circle(1.1);
    update(finish + 1000);
    expect(local.state() == 10 && !local.finish_camera_active(), "Race finish must retain follow camera before two seconds");
    update(finish + 2000);
    expect(local.finish_camera_active(), "Race finish camera must activate at the original two-second threshold");
    events = local.update(finish + 4999, positions[15], true);
    expect(events.empty() && local.state() == 10, "Single Race state10 must ignore confirm and retain its five-second transition gate");
    events = local.update(finish + 5000, positions[15]);
    expect(events == std::vector{SessionEvent::results_entered} && local.state() == 13, "Race results must begin at five seconds");
    expect(local.update(finish + 40000, positions[15]).empty() && local.state() == 13, "The result table must not inherit the earlier finish origin for its forty-second timeout");
    expect(local.update(finish + 44998, positions[15]).empty(), "The result table must retain its final timer quantum before forty seconds");
    events = local.update(finish + 44999, positions[15]);
    expect(events == std::vector{SessionEvent::race_return_requested} && local.state() == 13,
        "Race results must request Race Again forty seconds from the cached result-entry clock, retaining source state13 beneath it");
    expect(local.update(finish + 44999, positions[15], true).empty(), "Race Again continuation must not repeat while its confirmation overlay is open");
    progress.update(finish + 44999, local, positions);
    expect(progress.results(drivers).rows[2].completed_laps == 0 && local.completed_laps() == 1 && local.next_point() != 1,
        "Autonomous route cursors must continue while classification and actual lap records stay fixed");
    for (const auto limit : {3u, 5u, 15u}) {
        simulation::Session selected(bytes, false, GameMode::single_race, limit);
        for (const auto tick : {3063u, 6126u, 9189u}) {
            selected.update(tick, circle(-.1));
        }
        std::uint64_t tick = 9289;
        selected.update(tick, circle(.1));
        for (std::uint32_t lap = 0; lap < limit; ++lap) {
            for (std::uint32_t point = 1; point <= 24; ++point) {
                selected.update(tick += 100, circle(double(point % 24) + .1));
            }
            expect(selected.state() == (lap + 1 == limit ? 10u : 6u), "Selected race length must finish only after the configured number of actual complete circuits");
        }
    }
}
} // namespace

int main() {
    try {
        source_neighbor_order();
        source_classification();
        qualifying_progress();
        check();
        check_split();
        std::cout << "PASS: actual16-actor route progression, finite race finish and classification\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
