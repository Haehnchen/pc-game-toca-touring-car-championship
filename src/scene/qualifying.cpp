#include "scene/qualifying.hpp"

#include "assets/store.hpp"
#include "scene/qualifying_tables.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace toca::scene {
namespace {
std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("Qualifying: truncated CONFIG field");
    }
    return bytes[offset] | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        throw std::runtime_error("Qualifying: truncated CONFIG short");
    }
    return std::uint16_t(bytes[offset]) | (std::uint16_t(bytes[offset + 1]) << 8);
}

std::string name(std::span<const std::uint8_t> bytes) {
    const auto end = std::find(bytes.begin(), bytes.end(), 0);
    if (end == bytes.end()) {
        throw std::runtime_error("Qualifying: unterminated CONFIG name");
    }
    return {bytes.begin(), end};
}
} // namespace

QualifyingData load_qualifying_data(const AssetStore& store, const SceneSetup& setup, std::uint32_t difficulty, std::string_view player_name, bool mayhem, bool impossible,
    std::optional<std::uint32_t> championship_round, bool showdown) {
    const auto checked = resolve_setup(setup.selection, setup.horizon_index);
    if (setup.selection.car_id >= 10 || checked.weather != setup.weather || checked.horizon_index != setup.horizon_index || difficulty > 2 || player_name.empty()
        || player_name.size() > 9 || (championship_round && *championship_round >= 24)) {
        throw std::runtime_error("Qualifying requires a valid race setup and player name");
    }
    const auto archive = store.open_archive("data/config.bfp");
    const auto ai_member = archive.member(0), times_member = archive.member(1);
    const auto ai = ai_member.bytes(), times = times_member.bytes();
    if (ai.size() < 17416 || u32(ai, 0) != 3) {
        throw std::runtime_error("Qualifying requires original AIBIN version3 driver banks");
    }
    QualifyingData result;
    result.showdown = showdown;
    result.point_speed_boost = showdown || impossible;
    // 44df6e replaces Ford's pair8/9 with either selected bonus car.
    result.local_actor = setup.selection.car_id >= 8 ? 8 : setup.selection.car_id * 2;
    for (std::size_t car = 0; car < result.car_names.size(); ++car) {
        result.car_names[car] = name(ai.subspan(4 + car * 10, 10));
    }
    for (std::size_t actor = 0; actor < result.drivers.size(); ++actor) {
        // 420460 uses the one-based championship race directly as AIBIN bank.
        const auto bank = championship_round ? *championship_round + 1 : 24 + difficulty;
        const auto offset = 124 + bank * 640 + actor * 40;
        auto& driver = result.drivers[actor];
        std::copy_n(ai.begin() + std::ptrdiff_t(offset), driver.raw.size(), driver.raw.begin());
        driver.name = actor == result.local_actor ? std::string(player_name) : name(std::span(driver.raw).first(16));
        if (driver.raw[16] < 1 || driver.raw[16] > 8 || driver.raw[18] > 10) {
            throw std::runtime_error("Qualifying: driver car/ability outside original table range");
        }
        driver.car_id = driver.raw[16] - 1u;
        if (setup.selection.car_id >= 8 && (actor == 8 || actor == 9)) {
            driver.car_id = setup.selection.car_id;
        }
        driver.ability = std::max(1u, std::uint32_t(driver.raw[18]));
    }
    std::size_t cursor = 17416;
    for (std::size_t index = 0; index < result.distributions.size(); ++index) {
        const auto count = u32(ai, 17404 + index * 4);
        if (count == 0 || count > (ai.size() - cursor) / 2) {
            throw std::runtime_error("Qualifying: invalid AIBIN distribution count");
        }
        for (std::size_t entry = 0; entry < count; ++entry) {
            result.distributions[index].push_back(std::bit_cast<std::int16_t>(u16(ai, cursor)));
            cursor += 2;
        }
    }
    if (cursor != ai.size()) {
        throw std::runtime_error("Qualifying: unaccounted AIBIN distribution bytes");
    }
    result.weather_speed_penalty_q16 = setup.weather == 5 ? std::bit_cast<std::int32_t>(u32(ai, 108)) : setup.weather >= 3 ? std::bit_cast<std::int32_t>(u32(ai, 104)) : 0;
    result.tire_offset_q16 = std::bit_cast<std::int32_t>(u32(ai, setup.weather == 5 ? 116 : setup.weather >= 3 ? 112 : 120));
    // 4019f0: Showdown sets228f before the Mayhem/Impossible overrides.
    if (showdown) {
        result.tire_offset_q16 = 0x228f;
    }
    // 4019f0 runs after420460's weather defaults; Mayhem wins when both set.
    if (mayhem) {
        result.tire_offset_q16 = 0;
    } else if (impossible) {
        result.tire_offset_q16 = 0x3333;
    }
    // 444670 selects group1 for storm and group2 for rain/snow; 444730 adds 2*group shorts.
    const auto weather_group = setup.weather == 5 ? 2u : setup.weather >= 3 ? 4u : 0u;
    // 444670 subtracts one;444730 selects the championship pair before
    // the separate Single Race track/difficulty/weather tables.
    const auto time_offset = std::size_t(championship_round ? *championship_round * 2 : 48 + difficulty * 60 + setup.selection.track_id * 6 + weather_group) * 2;
    result.minimum_centiseconds = std::int32_t(u16(times, time_offset)) * 100;
    result.maximum_centiseconds = std::int32_t(u16(times, time_offset + 2)) * 100;
    if (result.minimum_centiseconds <= 0 || result.maximum_centiseconds > 32767 || result.minimum_centiseconds > result.maximum_centiseconds) {
        throw std::runtime_error("Qualifying: invalid original lap-time bounds");
    }
    result.ability_fraction_q16 = qualifying_tables::ability_lap_fraction_q16;
    result.variation_q16 = qualifying_tables::lap_variation_fraction_q16;
    return result;
}

std::uint32_t qualifying_random(SourceRandom& random, std::uint32_t bound) {
    if (bound == 0) {
        throw std::runtime_error("Qualifying RNG requires a positive source bound");
    }
    return random.next() % bound + 1;
}

QualifyingResults classify_qualifying(const QualifyingData& data, std::uint32_t local_best_ms, SourceRandom& random, const std::array<std::uint8_t, 16>* driver_bindings) {
    if (data.local_actor >= data.drivers.size() || data.minimum_centiseconds <= 0 || data.maximum_centiseconds < data.minimum_centiseconds || data.maximum_centiseconds > 32767
        || local_best_ms == 0 || local_best_ms > 3'600'000) {
        throw std::runtime_error("Qualifying classification requires a local best or original skipped-session record");
    }
    QualifyingResults result;
    result.local_actor = data.local_actor;
    for (std::uint32_t actor = 0; actor < result.rows.size(); ++actor) {
        const auto& driver = data.drivers[actor];
        auto time = local_best_ms / 10 * 10; // Only the source centisecond record survives.
        if (actor != data.local_actor) {
            const auto binding = driver_bindings ? (*driver_bindings)[actor] : actor;
            if (binding >= data.drivers.size()) {
                throw std::runtime_error("Qualifying classification has an invalid retained driver binding");
            }
            const auto raw_ability = data.drivers[binding].ability;
            const auto ability = raw_ability == 0 ? 1u : raw_ability;
            if (ability >= data.ability_fraction_q16.size() || driver.car_id >= 10) {
                throw std::runtime_error("Qualifying classification has an invalid driver ability/car");
            }
            const auto spread = data.maximum_centiseconds - data.minimum_centiseconds;
            const auto offset = (std::int64_t(data.ability_fraction_q16[ability]) * spread) >> 16;
            const auto variation = (std::int64_t(data.variation_q16[ability]) * data.minimum_centiseconds) >> 16;
            if (offset < 0 || variation <= 0 || variation > UINT32_MAX) {
                throw std::runtime_error("Qualifying classification has an invalid source variation bound");
            }
            const auto centiseconds = data.minimum_centiseconds + offset + qualifying_random(random, std::uint32_t(variation));
            if (centiseconds <= 0 || centiseconds >= 360000) {
                throw std::runtime_error("Qualifying generated time is outside the original minute record");
            }
            // 444270 applies960/100 instead of10 to Showdown rivals only.
            // Its split record then discards sub-centiseconds.
            time = data.showdown ? std::uint32_t(centiseconds * 960 / 100 / 10 * 10) : std::uint32_t(centiseconds) * 10;
        }
        result.rows[actor] = {actor, driver.car_id, time, driver.name};
    }
    std::stable_sort(result.rows.begin(), result.rows.end(), [](const auto& a, const auto& b) {
        return a.time_ms < b.time_ms;
    });
    for (std::uint32_t rank = 0; rank < result.rows.size(); ++rank) {
        if (result.rows[rank].actor_id == data.local_actor) {
            result.local_rank = rank;
        }
    }
    return result;
}
} // namespace toca::scene
