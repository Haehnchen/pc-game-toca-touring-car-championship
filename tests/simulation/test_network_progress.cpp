#include "race/race_progress.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::array<std::int32_t, 3> position(double index, double radius = 100) {
    const auto angle = index * 6.283185307179586 / 24;
    return {std::int32_t(std::cos(angle) * radius * 65536), 0, std::int32_t(std::sin(angle) * radius * 65536)};
}

std::vector<std::uint8_t> route() {
    std::vector<std::uint8_t> bytes(4 + 24 * 64);
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        for (std::size_t index = 0; index < 4; ++index) {
            bytes.at(offset + index) = std::uint8_t(value >> (index * 8));
        }
    };
    put(0, 24);
    for (std::uint32_t point = 0; point < 24; ++point) {
        for (const auto& [offset, radius] : {std::pair{0u, 90.0}, {12u, 100.0}, {36u, 110.0}}) {
            const auto sample = position(point, radius);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                put(4 + point * 64 + offset + axis * 4, std::uint32_t(sample[axis]));
            }
        }
    }
    return bytes;
}

void check_invalid_solo_routes() {
    using namespace toca;
    const auto rejects = [](std::span<const std::uint8_t> bytes) {
        bool rejected = false;
        try {
            race::RaceProgress progress(bytes, 1, 0, {}, 1, GameMode::linkup);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        expect(rejected, "One-actor Linkup must reject malformed POINTS without relying on a rival Session");
    };
    rejects({});
    rejects(std::array<std::uint8_t, 3>{});
    rejects(std::array<std::uint8_t, 4>{});
    rejects(std::array<std::uint8_t, 4>{255, 255, 255, 255});
    auto oversized = route();
    oversized.resize(4 + 257 * 64);
    oversized[0] = 1;
    oversized[1] = 1; //257 complete records still exceed the original table.
    rejects(oversized);
    auto truncated = route();
    truncated.pop_back();
    rejects(truncated);
    auto undersized = route();
    undersized[0] = 17;
    rejects(undersized);
}

void check() {
    using namespace toca;
    const auto bytes = route();
    std::array<std::uint32_t, 16> grid{};
    std::iota(grid.begin(), grid.end(), 0);
    std::reverse(grid.begin(), grid.begin() + 4);
    simulation::Session local(bytes, false, GameMode::linkup, 1);
    local.set_finish_group_complete(false);
    race::RaceProgress progress(bytes, 1, 1, grid, 4, GameMode::linkup);
    race::RacePositions positions{};
    positions.fill(position(-.1));
    std::uint64_t now = 0;
    const auto update = [&](std::uint64_t time) {
        now = time;
        local.update(now, positions[1]);
        progress.update(now, local, positions);
    };
    for (const auto time : {3063u, 6126u, 9189u}) {
        update(time);
    }
    positions.fill(position(.1));
    update(now + 100);
    for (unsigned point = 1; point <= 24; ++point) {
        positions[0] = position(double(point % 24) + .1);
        update(now + 100);
    }
    expect(progress.participant(0).completed_laps() == 1 && progress.participant(0).state() == 6 && !progress.finished(0),
        "A remote predicted crossing must not invent an owner finish or start presentation");
    progress.apply_network_finish(0, 456, 1, local);
    progress.apply_network_finish(0, 456, 1, local);
    expect(progress.finished(0) && progress.participant(0).race_time_ms() == 4560 && !progress.classified(),
        "The received owner time must replace local prediction once, without ending another player's race");
    for (unsigned point = 1; point <= 24; ++point) {
        positions[1] = position(double(point % 24) + .1);
        update(now + 100);
    }
    expect(local.state() == 10 && !progress.finished(1) && !progress.classified(), "The local owner crossing waits for its ordered reliable finish acknowledgment");
    progress.apply_network_finish(1, 321, 2, local);
    update(now + 6000);
    expect(local.state() == 10 && !progress.classified(), "Finishing alone must not bypass the shared results barrier");
    progress.finish_network_classification(local);
    scene::QualifyingData drivers;
    drivers.local_actor = 1;
    drivers.drivers[0].name = "HOST";
    drivers.drivers[1].name = "CLIENT";
    const auto results = progress.results(drivers);
    expect(results.count == 4 && results.local_rank == 1 && results.rows[0].actor_id == 0 && results.rows[0].time_ms == 4560 && results.rows[1].actor_id == 1
               && results.rows[1].time_ms == 3210 && !results.rows[2].finished && results.rows[2].actor_id == 2 && results.rows[3].actor_id == 3,
        "Retain owner times and delivery order, then append inactive identities in ascending actor order, independent of running rank");
    local.set_finish_group_complete(true);
    update(now + 5000);
    expect(local.state() == 13 && local.race_time_ms() == 3210, "Shared presentation must retain the acknowledged owner time");
    progress.finish_network_classification(local);

    std::iota(grid.begin(), grid.end(), 0);
    for (const auto count : {1u, 8u}) {
        race::RaceProgress supported(bytes, 1, 0, grid, count, GameMode::linkup);
        expect(supported.position(count - 1) == count, "Linkup must support its one-to-eight actor count independently of split screen");
    }
}
} // namespace

int main() {
    try {
        check_invalid_solo_routes();
        check();
        std::cout << "PASS: Linkup owner finish, reliable ordering and shared results barrier\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
