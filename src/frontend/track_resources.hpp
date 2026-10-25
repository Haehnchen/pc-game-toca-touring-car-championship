#pragma once

#include "frontend/resources.hpp"

#include <array>
#include <string>

namespace toca::frontend {
struct TrackInformation {
    std::string name, location;
    std::uint32_t metres = 0, weather = 0;
    std::string lap_time, lap_driver;
    std::uint32_t lap_car = 0;
};

struct TrackLabels {
    std::string title, location, length, unit, help, locked_help, fastest_lap, by, in, weather, overcast;
    std::array<std::string, 6> weather_values;
};

struct TrackResources {
    std::array<TrackInformation, 10> tracks;
    std::array<TrackLabels, 5> labels;
};

TrackResources load_track_resources();
Model load_track_preview(const AssetStore& store, std::uint32_t track);
std::string track_length(const TrackInformation& track, std::uint32_t language);
} // namespace toca::frontend
