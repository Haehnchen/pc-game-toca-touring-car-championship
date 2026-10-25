#include "audio/horn.hpp"

#include "assets/store.hpp"

namespace toca::audio {
namespace {
std::uint32_t volume(std::uint32_t camera) {
    // 4257d0 ->424410 index13, initialized by4244f0. Preserve integer truncation.
    const auto gain = camera == 2 ? 0x6666u : camera == 0 || camera == 3 ? 0x4cccu : 0x9999u;
    return gain * 100 >> 16;
}
} // namespace

Horn::Horn(Service& service, const AssetStore& store, const EngineConfig& config) : Horn(service, config, 0x400 + 71) {
    const auto archive = store.open_archive(config.weather == 3 || config.weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    service_.load_sample(sample_, archive.member(71).bytes());
}

Horn::Horn(Service& service, const EngineConfig& config, std::uint32_t sample) : service_(service), config_(config), sample_(sample) {
    (void)engine_sample_indices(config_);
}

Horn::~Horn() {
    stop();
}

void Horn::update(bool held) {
    if (!held) {
        stop();
        held_ = false;
        return;
    }
    if (held_) {
        return;
    }
    held_ = true;
    // 4234fc dispatches406a40 for native bonus-car8 rather than playing a horn.
    if (config_.car_id == 8) {
        return;
    }
    voice_ = service_.play_original_volume(sample_, 0, volume(config_.camera_view), 64);
    if (voice_ != 0) {
        serial_ = service_.playback_serial(voice_);
        ++played_;
    }
}

void Horn::set_camera(std::uint32_t view) {
    auto next = config_;
    next.camera_view = view;
    (void)engine_sample_indices(next);
    config_ = next;
}

void Horn::stop() {
    if (playing()) {
        service_.stop(voice_);
    }
    voice_ = 0;
    serial_ = 0;
}

std::uint64_t Horn::played_count() const {
    return played_;
}

bool Horn::playing() const {
    return voice_ != 0 && serial_ == service_.playback_serial(voice_) && service_.playing(voice_);
}
} // namespace toca::audio
