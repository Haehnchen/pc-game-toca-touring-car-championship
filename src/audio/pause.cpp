#include "audio/pause.hpp"

#include "assets/store.hpp"

#include <stdexcept>

namespace toca::audio {
Pause::Pause(Service& service, const AssetStore& store, std::uint32_t weather) : Pause(service, std::array<std::uint32_t, 3>{0x500 + 78, 0x500 + 79, 0x500 + 80}) {
    const auto archive = store.open_archive(weather == 3 || weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    for (std::size_t index = 0; index < samples_.size(); ++index) {
        service_.load_sample(samples_[index], archive.member(78 + index).bytes());
    }
}

Pause::Pause(Service& service, std::array<std::uint32_t, 3> samples) : service_(service), samples_(samples) {}

Pause::~Pause() {
    try {
        stop();
    } catch (...) {
    } // Destructors cannot report backend shutdown errors.
}

void Pause::play(std::uint32_t original_sample) {
    if (original_sample < 78 || original_sample > 80) {
        throw std::runtime_error("Pause cue must select original sample78,79 or80");
    }
    // 47992c allocates a free voice, allowing finite menu cues to overlap.
    const auto voice = service_.play_original_volume(samples_.at(original_sample - 78), 1, 100);
    if (voice != 0) {
        serials_.at(voice - 1) = service_.playback_serial(voice);
        ++played_;
    }
}

void Pause::stop() {
    for (std::size_t index = 0; index < serials_.size(); ++index) {
        const auto voice = Voice(index + 1);
        if (serials_[index] != 0 && serials_[index] == service_.playback_serial(voice) && service_.playing(voice)) {
            service_.stop(voice);
        }
        serials_[index] = 0;
    }
}

std::uint64_t Pause::played_count() const {
    return played_;
}
} // namespace toca::audio
