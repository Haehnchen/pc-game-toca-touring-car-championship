#include "audio/finish.hpp"

#include "assets/store.hpp"
#include "audio/audio_tables.hpp"

#include <algorithm>

namespace toca::audio {
FinishData load_finish_audio(Service& service, const AssetStore& store, const EngineConfig& config) {
    (void)engine_sample_indices(config);
    const auto archive = store.open_archive(config.weather == 3 || config.weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    FinishData result{0x400 + 83, builtin::finish_loop_volume_miles};
    service.load_sample(result.sample, archive.member(83).bytes());
    return result;
}

Finish::Finish(Service& service, const AssetStore& store, const EngineConfig& config) : Finish(service, load_finish_audio(service, store, config)) {}

Finish::Finish(Service& service, FinishData data) : service_(service), data_(data) {
    if (data_.volume > 127) {
        throw std::runtime_error("Finish-loop volume is outside Miles range");
    }
}

Finish::~Finish() {
    stop();
}

bool Finish::playing() const {
    return voice_ != 0 && serial_ == service_.playback_serial(voice_) && service_.playing(voice_);
}

void Finish::play() {
    if (!active_ || !requested_ || playing()) {
        return;
    }
    // Service defaults preserve original centered pan64 and source rate.
    voice_ = service_.play_original_volume(data_.sample, 0, volume_);
    serial_ = voice_ == 0 ? 0 : service_.playback_serial(voice_);
    if (voice_ != 0) {
        ++played_;
    }
}

void Finish::start() {
    if (!requested_) {
        volume_ = data_.volume;
        requested_ = true;
        fading_ = false;
    }
    play();
}

void Finish::fade() {
    fading_ = true;
}

void Finish::update() {
    if (!active_ || !requested_ || !fading_) {
        return;
    }
    // 405840(2)/405860: decrement four per source dispatch, then cap124.
    volume_ = std::min(124u, volume_ > 4 ? volume_ - 4 : 0);
    if (volume_ == 0) {
        stop();
    } else if (playing()) {
        service_.controls_original_volume(voice_, volume_, 1);
    }
}

void Finish::set_active(bool active) {
    active_ = active;
    if (active_) {
        play();
    } else {
        stop_voice();
    }
}

void Finish::stop_voice() {
    if (playing()) {
        service_.stop(voice_);
    }
    voice_ = 0;
    serial_ = 0;
}

void Finish::stop() {
    stop_voice();
    requested_ = fading_ = false;
    volume_ = 0;
}

std::uint32_t Finish::volume() const {
    return volume_;
}

std::uint64_t Finish::played_count() const {
    return played_;
}
} // namespace toca::audio
