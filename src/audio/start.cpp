#include "audio/start.hpp"

#include "assets/store.hpp"

#include <algorithm>

namespace toca::audio {
StartAudioInputs session_audio_inputs(EngineInputs raw, std::int32_t speed_integer, std::uint32_t session_state) {
    const auto values = source_session_engine_values({raw.rpm, speed_integer, raw.gear, raw.throttle_percent}, std::int32_t(session_state));
    return {{values.rpm, values.gear, values.throttle, values.speed}, values.speed};
}

StartData load_start_audio(Service& service, const AssetStore& store, const EngineConfig& config) {
    (void)engine_sample_indices(config);
    const auto archive = store.open_archive(config.weather == 3 || config.weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    StartData result;
    for (std::uint32_t index = 0; index < 2; ++index) {
        result.samples[index] = 0x400 + 69 + index;
        service.load_sample(result.samples[index], archive.member(69 + index).bytes());
    }
    return result;
}

Start::Start(Service& service, const AssetStore& store, const EngineConfig& config) : Start(service, load_start_audio(service, store, config)) {}

Start::Start(Service& service, StartData data) : service_(service), data_(data) {}

Start::~Start() {
    stop();
}

void Start::start() {
    active_ = true;
}

void Start::stop_voice() {
    if (voice_ != 0 && serial_ == service_.playback_serial(voice_) && service_.playing(voice_)) {
        service_.stop(voice_);
    }
    voice_ = 0;
    serial_ = 0;
}

void Start::update(StartCue cue) {
    if (!active_) {
        return;
    }
    const auto index = cue == StartCue::red ? 0u : 1u;
    stop_voice();
    voice_ = service_.play_original_volume(data_.samples[index], 1, 60);
    if (voice_ != 0) {
        serial_ = service_.playback_serial(voice_);
        last_sample_ = 69 + index;
        ++played_;
    }
}

void Start::stop() {
    stop_voice();
    active_ = false;
}

std::uint64_t Start::played_count() const {
    return played_;
}

std::uint32_t Start::last_sample() const {
    return last_sample_;
}
} // namespace toca::audio
