#include "race/championship.hpp"

#include "simulation/finish_classification.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace toca::race {
namespace {
constexpr std::array<std::uint32_t, 12> tracks{0, 1, 2, 3, 4, 5, 6, 7, 8, 2, 3, 1};
constexpr std::array<std::uint32_t, 9> full_laps{18, 20, 20, 38, 27, 25, 25, 35, 23};

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Rows> void validate_order(const Rows& rows, std::uint32_t local_actor, std::uint32_t local_rank) {
    std::array<bool, 16> seen{};
    for (const auto& row : rows) {
        require(row.actor_id < 16 && !seen[row.actor_id] && row.car_id < 10 && row.driver.size() < 40 && row.driver.find('\0') == std::string::npos,
            "Championship: invalid classified driver order");
        seen[row.actor_id] = true;
    }
    require(local_actor < 16 && local_rank < 16 && rows[local_rank].actor_id == local_actor, "Championship: inconsistent local classification");
}
} // namespace

ChampionshipState new_championship(bool full_length, std::uint32_t player_car, std::string driver, bool showdown) {
    ChampionshipState state;
    state.full_length = full_length;
    state.showdown = showdown;
    state.player_car = player_car;
    state.driver = std::move(driver);
    state.local_actor = player_car >= 8 ? 8 : player_car * 2;
    std::iota(state.grid.begin(), state.grid.end(), 0);
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        state.drivers[actor].actor_id = actor;
        state.drivers[actor].car_id = actor / 2;
    }
    validate_championship(state);
    return state;
}

void validate_championship(const ChampionshipState& state) {
    require(state.player_car < 10 && state.local_actor < 16 && state.local_actor == (state.player_car >= 8 ? 8 : state.player_car * 2) && state.meeting < tracks.size()
                && state.race < 2 && std::uint32_t(state.phase) <= std::uint32_t(ChampionshipPhase::complete) && !state.driver.empty() && state.driver.size() < 10
                && state.driver.find('\0') == std::string::npos,
        "Championship: invalid season metadata");
    std::array<bool, 16> seen{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto& row = state.drivers[index];
        require(state.grid[index] < 16 && !seen[state.grid[index]], "Championship: invalid starting grid");
        seen[state.grid[index]] = true;
        require(row.actor_id == index && row.car_id < 10 && row.driver.size() < 40 && row.driver.find('\0') == std::string::npos && state.points[index] <= 384,
            "Championship: invalid driver or points");
    }
    require(std::accumulate(state.podiums.begin(), state.podiums.end(), 0u) <= 6
                && std::ranges::all_of(state.awards,
                    [](auto award) {
                        return award <= 3;
                    }),
        "Championship: invalid podium counts or awards");
    require(state.phase != ChampionshipPhase::complete || (state.meeting == 11 && state.race == 1), "Championship: premature final result");
}

std::uint32_t championship_track(const ChampionshipState& state) {
    validate_championship(state);
    return tracks[state.meeting];
}

std::uint32_t championship_laps(const ChampionshipState& state) {
    const auto laps = full_laps[championship_track(state)];
    return state.full_length ? laps : laps / 3;
}

std::uint32_t championship_target(const ChampionshipState& state) {
    validate_championship(state);
    return (state.meeting + 1) * 20;
}

std::array<std::uint32_t, 16> championship_standings(const ChampionshipState& state) {
    validate_championship(state);
    const auto source_order = simulation::classification_standings(state.points);
    std::array<std::uint32_t, 16> order;
    std::copy(source_order.begin(), source_order.end(), order.begin());
    return order;
}

void championship_apply_qualifying(ChampionshipState& state, const scene::QualifyingResults& results) {
    validate_championship(state);
    require(state.phase == ChampionshipPhase::qualifying, "Championship: qualifying already consumed");
    validate_order(results.rows, results.local_actor, results.local_rank);
    require(results.local_actor == state.local_actor, "Championship: qualifying changed player actor");
    for (std::size_t rank = 0; rank < 16; ++rank) {
        const auto& row = results.rows[rank];
        state.drivers[row.actor_id] = row;
        state.grid[rank] = row.actor_id;
    }
    ++state.points[state.grid[0]]; // 444270: pole-position point.
    state.phase = ChampionshipPhase::race;
}

void championship_apply_race(ChampionshipState& state, const RaceResults& results, std::uint32_t local_penalty) {
    validate_championship(state);
    require(state.phase == ChampionshipPhase::race, "Championship: race already consumed or qualifying missing");
    validate_order(results.rows, results.local_actor, results.local_rank);
    require(results.local_actor == state.local_actor, "Championship: race changed player actor");
    std::array<std::uint32_t, 16> order;
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        order[rank] = results.rows[rank].actor_id;
    }
    simulation::award_race_classification(state.points, state.podiums, order, state.local_actor, local_penalty);
    state.has_won = state.has_won || results.local_rank == 0;
    state.phase = ChampionshipPhase::results;
}

std::optional<ChampionshipHonors> championship_honors(const ChampionshipState& state) {
    validate_championship(state);
    if ((state.phase != ChampionshipPhase::results && state.phase != ChampionshipPhase::complete) || state.race != 1 || state.meeting % 3 != 2
        || (state.meeting < 11 && state.points[state.local_actor] < championship_target(state))) {
        return std::nullopt;
    }
    ChampionshipHonors honors;
    honors.block = state.meeting / 3;
    honors.showdown = state.showdown;
    honors.podiums = state.podiums;
    const auto first_two = state.podiums[0] + state.podiums[1];
    const auto all = first_two + state.podiums[2];
    honors.tier = state.podiums[0] >= 4 ? 3 : first_two >= 4 ? 2 : all >= 4 ? 1 : 0;
    // 427cb0 decodes these strings via428710's COPYRIGHT subtraction.
    if (state.showdown) {
        if (honors.tier == 3) {
            constexpr std::array<std::string_view, 4> names{"CMUPSIDE", "FLEXMOBILE", "CMIMPOSS", "TANK"};
            const auto kind = honors.block == 1 || honors.block == 3 ? ChampionshipRewardKind::car : ChampionshipRewardKind::code;
            honors.rewards.push_back({kind, std::string(names[honors.block]), 3});
        }
    } else {
        constexpr std::array<std::array<std::string_view, 3>, 4> codes{
            {{"CMSTARS", "CMNOHITS", "CMMICRO"}, {"CMCOPTER", "CMHANDY", "CMMAYHEM"}, {"CMRAINUP", "CMTOON", "CMFOLLOW"}, {"CMCHUN", "CMCATDOG", "LAVALAND"}}};
        for (std::uint32_t tier = 1; tier <= honors.tier; ++tier) {
            const auto kind = honors.block == 3 && tier == 3 ? ChampionshipRewardKind::track : ChampionshipRewardKind::code;
            honors.rewards.push_back({kind, std::string(codes[honors.block][tier - 1]), tier});
        }
    }
    return honors;
}

void championship_advance(ChampionshipState& state) {
    validate_championship(state);
    require(state.phase == ChampionshipPhase::results, "Championship: no race result to advance");
    const auto target = championship_target(state);
    // 44a0c0 / 491b0 / 4283b0: first race can leave at most16 points
    // to earn at the second race. Both checks exempt the final meeting.
    const auto allowance = state.race == 0 ? 16u : 0u;
    if (state.meeting < 11 && state.points[state.local_actor] + allowance < target) {
        state.phase = ChampionshipPhase::eliminated;
        return;
    }
    if (state.race == 0) {
        state.race = 1;
    } else {
        // 4492e0 is called after six races (three meetings).
        if (state.meeting % 3 == 2) {
            const auto first_two = state.podiums[0] + state.podiums[1];
            const auto all = first_two + state.podiums[2];
            state.awards[state.meeting / 3] = state.podiums[0] >= 4 ? 3 : first_two >= 4 ? 2 : all >= 4 ? 1 : 0;
            if (state.meeting == 11) {
                state.phase = ChampionshipPhase::complete;
                return;
            }
            state.podiums.fill(0);
        }
        ++state.meeting;
        state.race = 0;
    }
    state.phase = ChampionshipPhase::qualifying;
}

bool championship_unlocks(const ChampionshipState& state, frontend::cheats::Progress& progress) {
    validate_championship(state);
    const auto before = progress;
    const auto track = championship_track(state);
    // 44f0a3: entering the next meeting exposes the previous track; after
    // race18 all tracks are available. The native progress merges monotonically.
    if (state.meeting * 2 + state.race + 1 > 18) {
        progress.track_threshold = 9;
    } else if (track > progress.track_threshold) {
        progress.track_threshold = std::max(progress.track_threshold, track - 1);
    }
    if (state.phase == ChampionshipPhase::complete) {
        progress.special_championship = true; // 449790: config+38.
    }
    // 427cb0: the Showdown's second/final six-race gold awards unlock the
    // two bonus cars. The ordinary championship instead displays code awards.
    if (state.showdown && state.awards[1] == 3) {
        progress.bonus_car_9 = true; // +40 FLEXMOBILE, car9 (Cadillac).
    }
    if (state.showdown && state.awards[3] == 3) {
        progress.bonus_car_8 = true; // +3c, car8 (tank).
    }
    if (!state.showdown && state.awards[3] == 3) {
        progress.track_threshold = 9; // 427cb0: LAVALAND.
    }
    return progress != before;
}
} // namespace toca::race
