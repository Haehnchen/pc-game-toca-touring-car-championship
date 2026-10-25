#pragma once

#include "frontend/cheats.hpp"
#include "race/race_progress.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toca::race {
enum class ChampionshipPhase : std::uint32_t {
    qualifying,
    race,
    results,
    eliminated,
    complete
};

struct ChampionshipState {
    bool full_length = false;
    bool showdown = false;
    bool transmission_manual = false;
    std::uint32_t player_car = 0, local_actor = 0, meeting = 0, race = 0;
    std::string driver;
    ChampionshipPhase phase = ChampionshipPhase::qualifying;
    std::array<std::uint32_t, 16> points{};
    std::array<std::uint32_t, 3> podiums{};
    std::array<std::uint32_t, 4> awards{};
    std::array<scene::QualifyingEntry, 16> drivers{}; // Indexed by actor ID.
    std::array<std::uint32_t, 16> grid{};
    bool has_won = false;
    bool operator==(const ChampionshipState&) const = default;
};

enum class ChampionshipRewardKind {
    code,
    track,
    car
};

struct ChampionshipReward {
    ChampionshipRewardKind kind = ChampionshipRewardKind::code;
    std::string name;
    std::uint32_t minimum_tier = 0;
    bool operator==(const ChampionshipReward&) const = default;
};

struct ChampionshipHonors {
    std::uint32_t block = 0, tier = 0; // Block0..3; tier0/bronze1/silver2/gold3.
    bool showdown = false;
    std::array<std::uint32_t, 3> podiums{};
    std::vector<ChampionshipReward> rewards;
    bool operator==(const ChampionshipHonors&) const = default;
};

// Capture after Race6/12/18/24, before advance resets the next block counters.
// A failed points gate has no award page; tier0 still has the original page.
std::optional<ChampionshipHonors> championship_honors(const ChampionshipState& state);

ChampionshipState new_championship(bool full_length, std::uint32_t player_car, std::string driver, bool showdown = false);
void validate_championship(const ChampionshipState& state);
std::uint32_t championship_track(const ChampionshipState& state);
std::uint32_t championship_laps(const ChampionshipState& state);
std::uint32_t championship_target(const ChampionshipState& state);
std::array<std::uint32_t, 16> championship_standings(const ChampionshipState& state);
// Pole earns one point. Calling twice without a new qualifying stage is an error.
void championship_apply_qualifying(ChampionshipState& state, const scene::QualifyingResults& results);
// Consumes the original classified order, including classified retirements.
void championship_apply_race(ChampionshipState& state, const RaceResults& results, std::uint32_t local_penalty = 0);
// Results -> next qualifying, elimination, or completion. No invented retry.
void championship_advance(ChampionshipState& state);
// Merge earned progress; explicit Extras overrides remain independent.
bool championship_unlocks(const ChampionshipState& state, frontend::cheats::Progress& progress);
} // namespace toca::race
