#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace toca {
namespace race {
struct ChampionshipState;
}

struct ChampionshipRecord {
    std::string driver;
    std::uint32_t points = 0, round = 0;
    //46c45f reads four2-bit tiers: none0, bronze1, silver2, gold3.
    std::array<std::uint32_t, 4> awards{};
    bool operator==(const ChampionshipRecord&) const = default;
};

//426f20/4462d0: one shared table for Short/Full and normal/Showdown.
using ChampionshipRecords = std::array<ChampionshipRecord, 9>;

// Native default drivers, points, rounds and medals, in the original tie order.
ChampionshipRecords load_default_championship_records();
//Missing file returns the supplied defaults; corrupt/unreadable files throw.
ChampionshipRecords load_championship_records(const std::filesystem::path&, const ChampionshipRecords& defaults);
void save_championship_records(const std::filesystem::path&, const ChampionshipRecords&);

//4462d0: descending points, NEW equal-point results precede existing ties.
//Returns zero-based inserted rank, or nullopt for a score below ninth/XBOOSTME.
//For retry-safe persistence, mutate a copy and adopt it only after save succeeds.
std::optional<std::uint32_t> insert_championship_record(ChampionshipRecords&, const ChampionshipRecord&, bool boost = false);

//Capture only at terminal entry: eliminated overview (449de0), or AFTER final
//honors immediately BEFORE finale (449790). Abandon/quit and interim rounds
//do not submit. Boost is checked at this entry; no other cheat is excluded.
//Original config+c-1 displays24 at completion,2*meeting+2 after Race2
//elimination, and2*meeting after Race1 elimination (not completed-race count).
std::optional<ChampionshipRecord> championship_record_candidate(const race::ChampionshipState&, bool boost = false);
} // namespace toca
