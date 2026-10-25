#include "frontend/track_resources.hpp"

#include "assets/store.hpp"
#include "formats/cm3d.hpp"
#include "frontend/builtin_data.hpp"

#include <stdexcept>

namespace toca::frontend {
TrackResources load_track_resources() {
    TrackResources result;
    result.tracks = builtin::tracks();
    for (std::uint32_t language = 0; language < result.labels.size(); ++language) {
        auto& labels = result.labels[language];
        labels.title = builtin::translated(builtin::TextId::select_track, language);
        labels.location = builtin::translated(builtin::TextId::location, language);
        labels.length = builtin::translated(builtin::TextId::length, language);
        labels.help = builtin::translated(builtin::TextId::select_your_track, language);
        labels.locked_help = builtin::translated(builtin::TextId::you_cannot_select_this_track_yet, language);
        labels.fastest_lap = builtin::translated(builtin::TextId::fastest_lap, language);
        labels.by = builtin::translated(builtin::TextId::by, language);
        labels.in = builtin::translated(builtin::TextId::in, language);
        labels.weather = builtin::translated(builtin::TextId::weather, language);
        labels.overcast = builtin::translated(builtin::TextId::overcast, language);
        labels.unit = language == 0 ? builtin::miles_unit : builtin::kilometres_unit;
        for (std::uint32_t weather = 0; weather < labels.weather_values.size(); ++weather) {
            labels.weather_values[weather] = builtin::translated(builtin::weather_texts[weather], language);
        }
    }
    return result;
}

Model load_track_preview(const AssetStore& store, std::uint32_t track) {
    if (track >= 10) {
        throw std::runtime_error("Original track preview ID must be within 0..9");
    }
    const auto archive = store.open_archive("data/liltrax.bfp");
    auto cursor = archive.cursor();
    for (std::uint32_t index = 0; index < track; ++index) {
        if (!cursor.skip()) {
            throw std::runtime_error("Track archive ended before the selected preview");
        }
    }
    const auto member = cursor.read();
    if (!member) {
        throw std::runtime_error("Track archive does not contain the selected preview");
    }
    return parse_cm3d(member->bytes());
}

std::string track_length(const TrackInformation& track, std::uint32_t language) {
    if (language >= 5) {
        throw std::runtime_error("Original track language must be within 0..4");
    }
    // FUN_0046da56 uses 1600 metres per displayed English mile.
    const auto hundredths = language == 0 ? std::uint64_t(track.metres) * 100 / 1600 : track.metres / 10;
    const auto fraction = hundredths % 100;
    return std::to_string(hundredths / 100) + "." + (fraction < 10 ? "0" : "") + std::to_string(fraction);
}
} // namespace toca::frontend
