#include "audio/pit.hpp"

#include "assets/store.hpp"

namespace toca::audio {
Pit::Pit(Service& service, const AssetStore& store, const EngineConfig& config) : Pit(service, 0x400 + 75) {
    (void)engine_sample_indices(config);
    const auto archive = store.open_archive(config.weather == 3 || config.weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    service_.load_sample(sample_, archive.member(75).bytes());
}

Pit::Pit(Service& service, std::uint32_t sample) : service_(service), sample_(sample) {}

Pit::~Pit() {
    stop();
}

void Pit::enter() {
    stop();
    voice_ = service_.play_original_volume(sample_, 1, 60);
    if (voice_ != 0) {
        serial_ = service_.playback_serial(voice_);
        ++played_;
    }
}

void Pit::stop() {
    if (voice_ != 0 && serial_ == service_.playback_serial(voice_) && service_.playing(voice_)) {
        service_.stop(voice_);
    }
    voice_ = 0;
    serial_ = 0;
}

std::uint64_t Pit::played_count() const {
    return played_;
}
} // namespace toca::audio
