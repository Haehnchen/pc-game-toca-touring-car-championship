#include "fleet_reference/clock_transactions.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using toca::simulation::TimeStep;
using toca::test::fleet::ClockTransactions;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Action> void rejects(Action action, const char* message) {
    try {
        action();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

void word(std::vector<char>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(char(value >> shift));
    }
}

// The same independently authored counters used by the Python clock profile:
// retained initial update, first sample, split interval, capped interval, ordinary.
std::vector<char> stream(bool ordered) {
    std::vector<char> bytes{'T', 'O', 'C', 'A', 'C', 'L', 'O', 'K'};
    for (const auto value : {ordered ? 3u : 2u, 9u, 100u, 2621u, 1638425u, 1u, ordered ? 8u : 4u}) {
        word(bytes, value);
    }
    constexpr std::array<std::uint32_t, 4> counters{200, 400, 800, 900};
    for (std::size_t sample = 0; sample < counters.size(); ++sample) {
        const auto id = std::uint32_t(2 + sample * 2);
        if (ordered) {
            for (const auto value : {id, 1u, 0u, 0u, id, 2u, 1u, counters[sample]}) {
                word(bytes, value);
            }
        } else {
            word(bytes, id);
            word(bytes, counters[sample]);
        }
    }
    if (ordered) {
        word(bytes, 123); // One creation sample, never five imported timer origins.
    } else {
        for (unsigned slot = 0; slot < 5; ++slot) {
            word(bytes, 1000);
        }
    }
    for (std::uint32_t update = 0; update < 9; ++update) {
        word(bytes, 1000 + update * 4000);
    }
    return bytes;
}

class Fixture {
    std::filesystem::path directory_ = std::filesystem::temp_directory_path() / ("toca-fleet-clock-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path_ = directory_ / "clock.bin";

public:
    Fixture() {
        std::filesystem::create_directory(directory_);
    }

    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::string write(std::span<const char> bytes) const {
        std::ofstream output(path_, std::ios::binary);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.write(bytes.data(), std::streamsize(bytes.size()));
        return path_.string();
    }
};

void replay_profile(const Fixture& fixture, bool ordered) {
    ClockTransactions clock(fixture.write(stream(ordered)).c_str(), 9);
    expect(clock.created_origins() == ordered, "Clock version must distinguish creation from imported timer origins");
    constexpr std::array<TimeStep, 9> expected{{{2621, 1638425, true}, {2621, 1638425, true}, {65536, 65536, true}, {65536, 65536, true}, {65536, 65536, true},
        {65536, 65536, true}, {98304, 43690, true}, {98304, 43690, true}, {65536, 65536, false}}};
    constexpr std::array<std::uint32_t, 9> phases{0, 1, 2, 3, 3, 4, 4, 4, 4};
    constexpr std::array<std::uint32_t, 4> counters{200, 400, 800, 900};
    for (std::uint32_t update = 0; update < expected.size(); ++update) {
        const auto step = clock.step(update);
        expect(step.delta_q16 == expected[update].delta_q16 && step.reciprocal_q16 == expected[update].reciprocal_q16 && step.extra_step == expected[update].extra_step,
            "Decoded clock transactions changed independently expected source time steps");
        expect(clock.primary() == (update % 2 == 0), "An extra physical update must keep its primary's sampled step");
        expect(clock.caller_clock() == 1000 + update * 4000, "Caller clocks must remain separate from sampled physical counters");
        expect(clock.release_due() == (update == 3), "The third countdown transition must release once, including during an extra update");
        expect(clock.session_state() == (update < 4 ? 9u : 6u), "Green release must defer active state until the next primary");
        expect(clock.update_caller() == (update == 0 ? 0x4488a1u : update < 4 ? 0x4488d0u : 0x448434u), "Generated wrapper must follow deferred session state");
        if (ordered && update != 0 && update % 2 == 0) {
            clock.observe_operation(update, 0, 0);
            clock.observe_operation(update, 1, counters[update / 2 - 1]);
        }
        const auto origins = ordered ? 123u : 1000u;
        const std::array<std::uint32_t, 9> lifecycle{update >= 4 ? 17000u : origins, update >= 3 ? 13000u : origins, update < 4 ? 1000u + update * 4000 : 13000u, 1000, origins,
            phases[update], update < 4 ? 9u : 6u, update >= 3 ? 13000u : 0u, 1000u + update * 4000};
        expect(clock.lifecycle_values() == lifecycle, "Replay clock must preserve countdown/reset ownership and publish all five timer origins");
        clock.finish_update();
    }
    clock.finish();
}

void reject_malformed_streams(const Fixture& fixture) {
    auto bytes = stream(false);
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        rejects(
            [&] {
                ClockTransactions clock(fixture.write(std::span(bytes).first(length)).c_str(), 9);
            },
            "Every truncated packed clock stream must fail");
    }
    bytes.push_back(0);
    rejects(
        [&] {
            ClockTransactions clock(fixture.write(bytes).c_str(), 9);
        },
        "Clock stream must reject trailing bytes");
    bytes = stream(false);
    ClockTransactions skipped(fixture.write(bytes).c_str(), 9);
    rejects(
        [&] {
            (void)skipped.step(1);
        },
        "Skipped update IDs must fail before applying another transaction");
    ClockTransactions unfinished(fixture.write(bytes).c_str(), 9);
    (void)unfinished.step(0);
    rejects(
        [&] {
            unfinished.finish();
        },
        "An incomplete physical pair cannot pass interval acceptance");
    bytes[36] = 1; // A new sample in the initial extra update would change the simulation.
    ClockTransactions extra_sample(fixture.write(bytes).c_str(), 9);
    (void)extra_sample.step(0);
    rejects(
        [&] {
            (void)extra_sample.step(1);
        },
        "Extra physical updates cannot import a fresh clock sample");

    bytes = stream(true);
    bytes[52] = 1; // Second operation's update ID precedes the first operation.
    rejects(
        [&] {
            ClockTransactions clock(fixture.write(bytes).c_str(), 9);
        },
        "Ordered clock operations must reject reversed update chronology");
    bytes = stream(true);
    bytes[56] = 1; // Same update/order as its preceding primary operation.
    rejects(
        [&] {
            ClockTransactions clock(fixture.write(bytes).c_str(), 9);
        },
        "Duplicate operation ordinals must fail closed");
    for (const bool missing : {false, true}) {
        ClockTransactions journal(fixture.write(stream(true)).c_str(), 9);
        (void)journal.step(0);
        journal.finish_update();
        (void)journal.step(1);
        journal.finish_update();
        (void)journal.step(2);
        if (missing) {
            rejects(
                [&] {
                    journal.finish_update();
                },
                "A source journal cannot omit its primary and sample operations");
        } else {
            rejects(
                [&] {
                    journal.observe_operation(2, 1, 200);
                },
                "Journal sample cannot precede its actual primary operation");
        }
    }
}
} // namespace

int main() {
    try {
        const Fixture fixture;
        replay_profile(fixture, false);
        replay_profile(fixture, true);
        reject_malformed_streams(fixture);
        std::cout << "Fleet clock transactions preserve v2/v3 physical chronology, lifecycle and malformed-input rejection\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
