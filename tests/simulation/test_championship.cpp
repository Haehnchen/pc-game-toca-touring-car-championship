#include "race/championship.hpp"
#include "settings/championship.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

toca::scene::QualifyingResults qualifying(const toca::race::ChampionshipState& state, std::uint32_t local_rank = 0) {
    toca::scene::QualifyingResults result;
    result.local_actor = state.local_actor;
    result.local_rank = local_rank;
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        result.rows[actor] = {actor, actor / 2, 90000 + actor * 100, "DRIVER" + std::to_string(actor)};
    }
    std::swap(result.rows[0], result.rows[state.local_actor]);
    std::swap(result.rows[0], result.rows[local_rank]);
    return result;
}

toca::race::RaceResults completed_race(const toca::race::ChampionshipState& state, std::uint32_t local_rank = 0) {
    const auto grid = qualifying(state, local_rank);
    toca::race::RaceResults result;
    result.local_actor = state.local_actor;
    result.local_rank = local_rank;
    for (std::uint32_t index = 0; index < 16; ++index) {
        const auto& row = grid.rows[index];
        result.rows[index] = {row.actor_id, row.car_id, 600000 + index * 100, 6, row.driver, true, false};
    }
    return result;
}

void run() {
    using namespace toca;
    using namespace race;
    auto state = new_championship(false, 3, "PLAYER");
    const std::array<std::uint32_t, 12> tracks{0, 1, 2, 3, 4, 5, 6, 7, 8, 2, 3, 1};
    const std::array<std::uint32_t, 12> short_laps{6, 6, 6, 12, 9, 8, 8, 11, 7, 6, 12, 6};
    frontend::cheats::Progress earned;
    for (std::uint32_t round = 0; round < 24; ++round) {
        expect(state.meeting == round / 2 && state.race == round % 2 && state.phase == ChampionshipPhase::qualifying,
            "Each meeting must retain both independent qualifying/race sessions");
        expect(championship_track(state) == tracks[round / 2] && championship_laps(state) == short_laps[round / 2], "Original schedule or short integer lap count differs");
        championship_apply_qualifying(state, qualifying(state));
        expect(state.phase == ChampionshipPhase::race && state.points[state.local_actor] == round * 16 + 1, "Each pole must add exactly one point");
        championship_apply_race(state, completed_race(state));
        expect(state.phase == ChampionshipPhase::results && state.points[state.local_actor] == (round + 1) * 16, "Winner must earn fifteen additional points");
        championship_advance(state);
        championship_unlocks(state, earned);
    }
    expect(state.phase == ChampionshipPhase::complete && state.points[state.local_actor] == 384 && earned.special_championship && earned.track_threshold == 9,
        "A full24-race championship must finish and retain earned progression");
    expect(std::ranges::all_of(state.awards,
               [](auto award) {
                   return award == 3;
               }),
        "Four six-race blocks must award four gold medals");
    expect(championship_standings(state)[0] == state.local_actor, "Standings must rank the actual champion first");

    auto first = new_championship(true, 0, "PLAYER");
    expect(championship_laps(first) == 18 && championship_target(first) == 20, "Full race length and meeting target must follow source tables");
    championship_apply_qualifying(first, qualifying(first, 15));
    championship_apply_race(first, completed_race(first, 8)); // Two points cannot reach20 even with another16.
    championship_advance(first);
    expect(first.phase == ChampionshipPhase::eliminated, "First-race impossibility must end the season");
    auto edge = new_championship(false, 0, "PLAYER");
    championship_apply_qualifying(edge, qualifying(edge, 15));
    championship_apply_race(edge, completed_race(edge, 6)); // Four points leave exactly sixteen.
    championship_advance(edge);
    expect(edge.phase == ChampionshipPhase::qualifying && edge.race == 1, "Exactly target minus sixteen must reach second qualifying");
    championship_apply_qualifying(edge, qualifying(edge));
    championship_apply_race(edge, completed_race(edge));
    championship_advance(edge);
    expect(edge.meeting == 1 && edge.points[0] == 20, "Exactly twenty points must qualify for the next meeting");

    auto penalty = new_championship(false, 0, "PLAYER");
    penalty.points[0] = 10;
    championship_apply_qualifying(penalty, qualifying(penalty));
    championship_apply_race(penalty, completed_race(penalty), 100);
    expect(penalty.points[0] == 11, "A penalty must not consume points accumulated before the race");
    penalty.points.fill(20);
    const auto tied = championship_standings(penalty);
    expect(std::ranges::is_sorted(tied), "Equal championship scores must retain actor ID order");

    auto final = new_championship(false, 0, "PLAYER");
    final.meeting = 11;
    championship_apply_qualifying(final, qualifying(final, 15));
    championship_apply_race(final, completed_race(final, 15));
    championship_advance(final);
    expect(final.phase == ChampionshipPhase::qualifying && final.race == 1, "Final first race must bypass the elimination threshold");
    championship_apply_qualifying(final, qualifying(final, 15));
    championship_apply_race(final, completed_race(final, 15));
    championship_advance(final);
    expect(final.phase == ChampionshipPhase::complete, "Final classification must finish without an invented final points gate");

    scene::QualifyingData normal_data;
    normal_data.minimum_centiseconds = normal_data.maximum_centiseconds = 10000;
    normal_data.variation_q16[1] = 7; // Exactly one-centisecond random bound.
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        normal_data.drivers[actor].name = "DRIVER";
        normal_data.drivers[actor].car_id = actor / 2;
    }
    auto showdown_data = normal_data;
    showdown_data.showdown = true;
    SourceRandom normal_random(1), showdown_random(1);
    const auto normal_grid = scene::classify_qualifying(normal_data, 123456, normal_random);
    const auto showdown_grid = scene::classify_qualifying(showdown_data, 123456, showdown_random);
    for (const auto& row : showdown_grid.rows) {
        const auto original = std::ranges::find(normal_grid.rows, row.actor_id, &scene::QualifyingEntry::actor_id);
        expect(original != normal_grid.rows.end(), "Showdown must retain the same sixteen classified actors");
        expect(row.time_ms == (row.actor_id == 0 ? 123450u : 96000u) && original->time_ms == (row.actor_id == 0 ? 123450u : 100010u),
            "Showdown must apply source960/100 to rivals while preserving actual player time");
    }
    expect(normal_random.next() == showdown_random.next(), "Showdown must retain the original number of qualifying random draws");

    const std::array<std::array<std::uint32_t, 3>, 4> medal_counts{{{0, 0, 3}, {1, 1, 2}, {1, 3, 0}, {4, 0, 0}}};
    const std::array<std::array<std::string_view, 3>, 4> rewards{
        {{"CMSTARS", "CMNOHITS", "CMMICRO"}, {"CMCOPTER", "CMHANDY", "CMMAYHEM"}, {"CMRAINUP", "CMTOON", "CMFOLLOW"}, {"CMCHUN", "CMCATDOG", "LAVALAND"}}};
    const std::array<std::string_view, 4> showdown_rewards{"CMUPSIDE", "FLEXMOBILE", "CMIMPOSS", "TANK"};
    for (std::uint32_t block = 0; block < 4; ++block) {
        for (std::uint32_t tier = 0; tier < 4; ++tier) {
            auto award_state = new_championship(false, 0, "PLAYER");
            award_state.meeting = block * 3 + 2;
            award_state.race = 1;
            award_state.phase = ChampionshipPhase::results;
            award_state.points[0] = championship_target(award_state);
            award_state.podiums = medal_counts[tier];
            const auto before = award_state;
            const auto honors = championship_honors(award_state);
            expect(honors && honors->block == block && honors->tier == tier && honors->podiums == medal_counts[tier] && award_state == before,
                "Award snapshot must capture source six-race counts without advancing the season");
            expect(honors->rewards.size() == tier, "Ordinary gold/silver awards must retain all lower-tier codes");
            for (std::uint32_t index = 0; index < tier; ++index) {
                expect(honors->rewards[index].name == rewards[block][index] && honors->rewards[index].minimum_tier == index + 1, "Decoded source award code order differs");
                expect(honors->rewards[index].kind == (block == 3 && index == 2 ? ChampionshipRewardKind::track : ChampionshipRewardKind::code),
                    "Lavaland must be a track unlock rather than an invented name code");
            }
            award_state.showdown = true;
            const auto special = championship_honors(award_state);
            expect(special && special->rewards.size() == (tier == 3 ? 1u : 0u), "Showdown must require four wins for every secret reward");
            if (tier == 3) {
                expect(special->rewards[0].name == showdown_rewards[block]
                           && special->rewards[0].kind == (block == 1 || block == 3 ? ChampionshipRewardKind::car : ChampionshipRewardKind::code),
                    "Showdown must retain its exact code and vehicle reward sequence");
            }
            if (block < 3) {
                award_state.points[0] = championship_target(award_state) - 1;
                expect(!championship_honors(award_state), "A failed points gate must not grant an award screen");
            }
        }
    }

    const auto path = std::filesystem::current_path() / "championship-test-state" / "season.dat";
    std::filesystem::remove_all(path.parent_path());
    expect(!load_championship(path), "Missing slot must be empty");
    auto checkpoint = new_championship(true, 2, "PLAYER");
    checkpoint.transmission_manual = true;
    championship_apply_qualifying(checkpoint, qualifying(checkpoint, 3));
    save_championship(path, checkpoint);
    auto loaded = load_championship(path);
    expect(loaded && *loaded == checkpoint, "Resume must retain names, classified grid, points, phase and manual transmission");
    championship_apply_race(*loaded, completed_race(checkpoint));
    expect(loaded->points[checkpoint.local_actor] == checkpoint.points[checkpoint.local_actor] + 15, "Resuming a race must not duplicate its pole point");
    // Independent final-state replacement uses the same atomic writer.
    save_championship(path, state);
    expect(load_championship(path) == std::optional(state), "Final classification and all awards must survive replacement");
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(40);
        file.put('\1');
    }
    bool corrupt_rejected = false;
    try {
        (void)load_championship(path);
    } catch (const std::runtime_error&) {
        corrupt_rejected = true;
    }
    expect(corrupt_rejected, "CRC32 must reject a damaged checkpoint");
    std::filesystem::remove_all(path.parent_path());
}
} // namespace

int main() {
    try {
        run();
        std::cout << "Original championship schedule, scoring, progression and checkpoint checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
