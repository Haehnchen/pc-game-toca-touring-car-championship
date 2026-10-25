#include "audio/weather.hpp"

#include "assets/store.hpp"
#include "audio/contact_state.hpp"
#include "core/random.hpp"

#include <stdexcept>

namespace toca::audio {
Weather::Weather(Service& service, const AssetStore& store, const EngineConfig& config) : Weather(service, {0x600 + 76, 0x600 + 77}) {
    (void)engine_sample_indices(config);
    const auto archive = store.open_archive(config.weather == 3 || config.weather == 5 ? "data/soundwet.bfp" : "data/sounddry.bfp");
    for (std::size_t index = 0; index < samples_.size(); ++index) {
        service_.load_sample(samples_[index], archive.member(76 + index).bytes());
    }
}

Weather::Weather(Service& service, std::array<std::uint32_t, 2> samples) : service_(service), samples_(samples) {}

Weather::~Weather() {
    stop();
}

void Weather::thunder(std::int32_t strength_q16, SourceRandom& random) {
    if (strength_q16 < 0 || strength_q16 > 65536) {
        throw std::runtime_error("Thunder strength requires the original Q16 range");
    }
    SourceCameraMix camera;
    const auto decision = source_impact_event({{}, 25, strength_q16}, {}, camera, {}, &random);
    if (decision.sample < 0) {
        return;
    }
    const auto index = std::uint32_t(decision.sample - 76);
    // 4248d5..4248f9 multiplies the Q16 integer by100 without shifting;
    // 4798cb clamps every audible thunder value to127. Preserve that quirk.
    const auto voice = service_.play_original_volume(samples_[index], 1, std::uint32_t(std::clamp(decision.volume, 0, 127)));
    if (voice != 0) {
        serials_[voice - 1] = service_.playback_serial(voice);
        last_sample_ = 76 + index;
        ++played_;
    }
}

void Weather::stop() {
    for (std::size_t index = 0; index < serials_.size(); ++index) {
        const auto voice = Voice(index + 1);
        if (serials_[index] != 0 && service_.playback_serial(voice) == serials_[index] && service_.playing(voice)) {
            service_.stop(voice);
        }
        serials_[index] = 0;
    }
}

std::uint64_t Weather::played_count() const {
    return played_;
}

std::uint32_t Weather::last_sample() const {
    return last_sample_;
}
} // namespace toca::audio
