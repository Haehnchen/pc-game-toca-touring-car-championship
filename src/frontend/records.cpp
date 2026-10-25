#include "frontend/records.hpp"

#include "frontend/builtin_data.hpp"

#include <stdexcept>

namespace toca::frontend {
LapRecords load_default_lap_records() {
    return builtin::lap_records();
}

RecordResources load_record_resources() {
    RecordResources result;
    for (std::uint32_t language = 0; language < result.labels.size(); ++language) {
        result.labels[language] = {builtin::translated(builtin::TextId::time_trial, language), builtin::translated(builtin::TextId::track, language),
            builtin::translated(builtin::TextId::time, language), builtin::translated(builtin::TextId::player, language)};
        result.championship_labels[language] = {builtin::translated(builtin::TextId::championship_records_title, language), builtin::translated(builtin::TextId::player, language),
            builtin::translated(builtin::TextId::points, language), builtin::translated(builtin::TextId::round, language)};
    }
    const auto& tracks = builtin::record_track_names();
    for (std::size_t index = 0; index < result.tracks.size(); ++index) {
        result.tracks[index] = tracks[index];
    }
    result.badges = builtin::record_badges();
    return result;
}

std::string record_time(std::uint32_t milliseconds) {
    // 46b878 displays the last minute digit, two seconds and hundredths.
    const auto seconds = milliseconds / 1000 % 60, hundredths = milliseconds % 1000 / 10;
    return std::to_string(milliseconds / 60000 % 10) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds) + "." + (hundredths < 10 ? "0" : "") + std::to_string(hundredths);
}
} // namespace toca::frontend
