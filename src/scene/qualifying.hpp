#pragma once

#include "core/random.hpp"
#include "scene/resources.hpp"

#include <optional>
#include <string>

namespace toca::scene {
struct QualifyingDriver {
    std::array<std::uint8_t, 40> raw{};
    std::string name;
    std::uint32_t car_id = 0, ability = 1;
};

struct QualifyingData {
    std::array<QualifyingDriver, 16> drivers;
    std::array<std::string, 10> car_names;
    // AIBIN's three original signed-short AI distributions, in source order.
    std::array<std::vector<std::int16_t>, 3> distributions;
    std::array<std::int32_t, 11> ability_fraction_q16{}, variation_q16{};
    std::int32_t minimum_centiseconds = 0, maximum_centiseconds = 0;
    std::int32_t weather_speed_penalty_q16 = 0, tire_offset_q16 = 0;
    std::uint32_t local_actor = 0;
    bool showdown = false, point_speed_boost = false;
};

struct QualifyingEntry {
    std::uint32_t actor_id = 0, car_id = 0, time_ms = 0;
    std::string driver;
    bool operator==(const QualifyingEntry&) const = default;
};

struct QualifyingResults {
    // Sorted ascending, with original actor order retained for equal times.
    std::array<QualifyingEntry, 16> rows;
    std::uint32_t local_actor = 0, local_rank = 0;
};

QualifyingData load_qualifying_data(const AssetStore&, const SceneSetup&, std::uint32_t difficulty, std::string_view player_name, bool mayhem = false, bool impossible = false,
    std::optional<std::uint32_t> championship_round = {}, bool showdown = false);
// 49ff60 / 44cb80: explicit MSVCRT stream state; no hidden global generator.
std::uint32_t qualifying_random(SourceRandom& random, std::uint32_t bound);
//444270 reads the retained gameplay+8d binding before401370 installs actor
//indices. A fresh cold call has zero bindings; completed qualifying uses each
//actor's own driver. The caller owns this lifecycle distinction.
QualifyingResults classify_qualifying(const QualifyingData&, std::uint32_t local_best_ms, SourceRandom& random, const std::array<std::uint8_t, 16>* driver_bindings = nullptr);
} // namespace toca::scene
