#include "assets/store.hpp"
#include "core/crc32.hpp"
#include "race/championship.hpp"
#include "settings/championship_records.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
using namespace toca;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

template <class Function> void fails(Function function, const char* message) {
    bool failed = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    expect(failed, message);
}

ChampionshipRecords defaults() {
    return {{{"PATRICK", 225, 24, {3, 3, 2, 1}}, {"BRYAN", 225, 23, {3, 2, 1, 0}}, {"LES", 171, 18, {2, 2, 0, 0}}, {"IAIN", 144, 15, {2, 1, 0, 0}}, {"ANDY", 114, 12, {2, 0, 0, 0}},
        {"PHIL", 83, 9, {2, 0, 0, 0}}, {"SCOTT", 62, 7, {1, 0, 0, 0}}, {"JON", 30, 4, {}}, {"RICH", 6, 2, {}}}};
}

void insertion() {
    const auto initial = defaults();
    auto records = initial;
    const ChampionshipRecord winner{"PLAYER", 312, 24, {3, 2, 1, 3}};
    expect(!insert_championship_record(records, winner, true) && records == initial, "Only the original XBOOSTME gate must suppress a championship insertion");
    expect(!insert_championship_record(records, {"LOW", 5, 22, {}}) && records == initial, "A score below ninth place must leave the complete table intact");
    expect(insert_championship_record(records, winner) == 0 && records[0] == winner && records[1] == initial[0] && records[8] == initial[7],
        "A new leader must shift all retained rows and evict only the old ninth");
    const ChampionshipRecord tied{"TIED", 225, 0, {}};
    expect(insert_championship_record(records, tied) == 1 && records[1] == tied && records[2] == initial[0] && records[3] == initial[1],
        "Equal points must insert BEFORE older ties without using round or medals as a tiebreak");
    records = initial;
    const ChampionshipRecord ninth{"NINTH", 6, 0, {}};
    expect(insert_championship_record(records, ninth) == 8 && records[8] == ninth, "A ninth-place point tie must replace the older ninth-place entry");
    records = initial;
    expect(insert_championship_record(records, initial[0]) == 0 && records[0] == records[1],
        "Separate identical real attempts remain eligible; table content is not a submission token");
}

void candidates() {
    auto state = race::new_championship(false, 4, "PLAYER");
    state.points[state.local_actor] = 300;
    expect(!championship_record_candidate(state), "Abandoning qualifying must not manufacture a championship record");
    for (const auto phase : {race::ChampionshipPhase::race, race::ChampionshipPhase::results}) {
        state.phase = phase;
        expect(!championship_record_candidate(state), "Interim races and results must not submit cumulative points");
    }
    state.meeting = 11;
    state.race = 1;
    state.phase = race::ChampionshipPhase::complete;
    state.points[state.local_actor] = 312;
    state.awards = {3, 2, 1, 3};
    const ChampionshipRecord expected{"PLAYER", 312, 24, {3, 2, 1, 3}};
    expect(championship_record_candidate(state) == expected && !championship_record_candidate(state, true),
        "Finale entry must capture final points and four medals, with the original boost exclusion");
    for (const auto full : {false, true}) {
        for (const auto showdown : {false, true}) {
            state.full_length = full;
            state.showdown = showdown;
            expect(championship_record_candidate(state) == expected, "Short/Full and normal/Showdown must all feed the same original table");
        }
    }
    state.phase = race::ChampionshipPhase::eliminated;
    state.meeting = 10;
    state.race = 1;
    state.points[state.local_actor] = 199;
    expect(championship_record_candidate(state)->round == 22 && championship_record_candidate(state)->points == 199,
        "Race2 elimination must record after44a0c0 advances the source round counter");
    state.race = 0;
    expect(championship_record_candidate(state)->round == 20, "Race1 elimination must retain config+c-1 without an invented completed-race increment");
    state.meeting = 0;
    state.points[state.local_actor] = 3;
    expect(championship_record_candidate(state)->round == 0, "The first-race elimination source field must remain round0");
}

struct TemporaryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("toca-championship-records-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    TemporaryDirectory() {
        std::filesystem::create_directory(path);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, {}};
}

std::uint32_t get(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return bytes[offset] | std::uint32_t(bytes[offset + 1]) << 8 | std::uint32_t(bytes[offset + 2]) << 16 | std::uint32_t(bytes[offset + 3]) << 24;
}

void storage() {
    TemporaryDirectory temporary;
    const auto path = temporary.path / "championship" / "records.dat";
    const auto initial = defaults();
    expect(load_championship_records(path, initial) == initial, "Missing native storage must use original compiled defaults");
    save_championship_records(path, initial);
    const auto persisted = read(path);
    expect(persisted.size() == 228 && get(persisted, 0) == 0x52434354 && get(persisted, 4) == 1 && get(persisted, 224) == crc32(std::span(persisted).first(224)),
        "Championship records must use the native TCCR header and IEEE/zlib CRC32 over the entire payload");
    expect(crc32(std::span(persisted).first(224)) == 0x218dfd6d, "Championship-record version1 bytes must remain compatible with existing saves");
    expect(get(persisted, 8 + 20) == 0x6f && load_championship_records(path, initial) == initial,
        "Native persistence must round-trip the original four2-bit medal fields and every row");
    auto prepared = initial;
    const auto rank = insert_championship_record(prepared, {"NEW", 312, 24, {3, 3, 3, 3}});
    const auto blocked = temporary.path / "blocked.dat";
    std::filesystem::create_directory(blocked);
    fails(
        [&] {
            save_championship_records(blocked, prepared);
        },
        "Failed atomic replacements must report an error");
    expect(rank == 0 && load_championship_records(path, initial) == initial, "A failed candidate save must preserve existing durable records");
    save_championship_records(path, prepared);
    expect(load_championship_records(path, initial) == prepared, "Retry must persist the retained prepared table without inserting it again");
    auto corrupt = read(path);
    corrupt[12] ^= 1;
    const auto bad = temporary.path / "corrupt.dat";
    {
        std::ofstream output(bad, std::ios::binary);
        output.write(reinterpret_cast<const char*>(corrupt.data()), std::streamsize(corrupt.size()));
    }
    fails(
        [&] {
            (void)load_championship_records(bad, initial);
        },
        "Corrupt records must report their CRC32 failure instead of returning defaults");
    auto invalid = prepared;
    invalid[0].awards[3] = 4;
    fails(
        [&] {
            save_championship_records(path, invalid);
        },
        "Out-of-range medal values must not corrupt native storage");
    expect(load_championship_records(path, initial) == prepared, "Invalid metadata must leave the previous complete file intact");
}

void reference(const char* root) {
    const AssetStore store(root);
    expect(load_default_championship_records() == defaults(), "The canonical nine defaults, tie order, rounds and all original medals must match executable4b7da8");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            insertion();
            candidates();
            storage();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("usage: toca-championship-records-tests [canonical-root]");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
