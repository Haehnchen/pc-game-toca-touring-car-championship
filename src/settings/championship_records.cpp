#include "settings/championship_records.hpp"

#include "race/championship.hpp"
#include "settings/championship_defaults.hpp"
#include "settings/storage.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca {
namespace {
constexpr std::uint32_t magic = 0x52434354; //TCCR: native championship records.
constexpr std::size_t row_size = 24, file_size = 8 + 9 * row_size + 4;

constexpr state_file::Format format{file_size, magic, "Championship records"};

void validate(const ChampionshipRecord& record) {
    if (record.driver.empty() || record.driver.size() >= 12 || record.driver.find('\0') != std::string::npos || record.points > 384 || record.round > 24
        || std::ranges::any_of(record.awards, [](auto award) {
               return award > 3;
           })) {
        throw std::runtime_error("Championship records: invalid driver, points, round or awards");
    }
}

void validate(const ChampionshipRecords& records) {
    for (std::size_t rank = 0; rank < records.size(); ++rank) {
        validate(records[rank]);
        if (rank != 0 && records[rank - 1].points < records[rank].points) {
            throw std::runtime_error("Championship records: points are not in descending order");
        }
    }
}

std::array<std::uint32_t, 4> unpack_awards(std::uint32_t packed) {
    if (packed > 255) {
        throw std::runtime_error("Championship records: invalid packed awards");
    }
    return {packed & 3, (packed >> 2) & 3, (packed >> 4) & 3, (packed >> 6) & 3};
}
} // namespace

ChampionshipRecords load_default_championship_records() {
    ChampionshipRecords records;
    // Preserve the established tie order of the native default records.
    for (std::uint32_t rank = 0; rank < records.size(); ++rank) {
        const auto& source = championship_defaults::records[rank];
        records[rank] = {std::string(source.driver), source.points, source.round, source.awards};
    }
    validate(records);
    return records;
}

ChampionshipRecords load_championship_records(const std::filesystem::path& path, const ChampionshipRecords& defaults) {
    if (!std::filesystem::exists(path)) {
        validate(defaults);
        return defaults;
    }
    const auto bytes = state_file::read(path, format);
    state_file::Reader reader{std::span(bytes).subspan(8, file_size - 12), format.context};
    ChampionshipRecords records;
    for (std::size_t rank = 0; rank < records.size(); ++rank) {
        records[rank] = {reader.text(12, "driver"), reader.number(), reader.number(), unpack_awards(reader.number())};
    }
    validate(records);
    return records;
}

void save_championship_records(const std::filesystem::path& path, const ChampionshipRecords& records) {
    validate(records);
    std::array<std::uint8_t, file_size> bytes{};
    state_file::Writer writer{std::span(bytes).subspan(8, file_size - 12), format.context};
    for (std::size_t rank = 0; rank < records.size(); ++rank) {
        const auto& record = records[rank];
        writer.text(record.driver, 12);
        writer.number(record.points);
        writer.number(record.round);
        writer.number(record.awards[0] | (record.awards[1] << 2) | (record.awards[2] << 4) | (record.awards[3] << 6));
    }
    writer.finish();
    state_file::write(path, bytes, format);
}

std::optional<std::uint32_t> insert_championship_record(ChampionshipRecords& records, const ChampionshipRecord& candidate, bool boost) {
    validate(records);
    validate(candidate);
    if (boost) {
        return std::nullopt;
    }
    //4462d0 scans backwards through >= points, placing a new tied result first.
    const auto at = std::ranges::find_if(records, [&](const auto& row) {
        return row.points <= candidate.points;
    });
    if (at == records.end()) {
        return std::nullopt;
    }
    const auto rank = std::uint32_t(at - records.begin());
    const auto value = candidate;
    std::move_backward(at, records.end() - 1, records.end());
    records[rank] = value;
    return rank;
}

std::optional<ChampionshipRecord> championship_record_candidate(const race::ChampionshipState& state, bool boost) {
    race::validate_championship(state);
    if (boost || (state.phase != race::ChampionshipPhase::complete && state.phase != race::ChampionshipPhase::eliminated)) {
        return std::nullopt;
    }
    //44a0c0 advances config+c before Race2's4491b0 gate, but not before
    //Race1's early gate.449790 sees25 after the final meeting;426f20 stores-1.
    const auto round = state.phase == race::ChampionshipPhase::complete ? 24u : state.meeting * 2 + (state.race == 1 ? 2u : 0u);
    ChampionshipRecord result{state.driver, state.points[state.local_actor], round, state.awards};
    validate(result);
    return result;
}
} // namespace toca
