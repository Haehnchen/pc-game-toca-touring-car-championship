#pragma once

#include "settings/championship_records.hpp"

#include <array>
#include <compare>
#include <cstdint>
#include <string>

namespace toca::frontend {
struct LapRecord {
    std::uint32_t lap_ms = 0;
    std::string driver;
    std::uint32_t car_id = 0;
    std::string code;
    auto operator<=>(const LapRecord&) const = default;
};

using LapRecords = std::array<LapRecord, 10>;

struct RecordResources {
    struct Labels {
        std::string title, track, time, player;
    };

    std::array<Labels, 5> labels;

    struct ChampionshipLabels {
        std::string title, player, points, round;
    };

    std::array<ChampionshipLabels, 5> championship_labels;
    std::array<std::string, 10> tracks;

    struct Badge {
        int x, y, width, height;
    };

    std::array<Badge, 10> badges;
};

LapRecords load_default_lap_records();
RecordResources load_record_resources();
std::string record_time(std::uint32_t milliseconds);
} // namespace toca::frontend
