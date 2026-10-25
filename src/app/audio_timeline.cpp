#include "app/audio_timeline.hpp"

#include "audio/service.hpp"

#include <algorithm>

namespace toca {
AudioTimeline::AudioTimeline(audio::Service& audio) : audio_(audio) {}

void AudioTimeline::reset(std::uint64_t now) {
    previous_ = now;
    remainder_ = 0;
}

void AudioTimeline::advance(std::uint64_t now) {
    if (now <= previous_) {
        return;
    }
    const auto elapsed = now - previous_;
    previous_ = now;
    if (!audio_.offline()) {
        return;
    }
    remainder_ += (elapsed % 1'000'000'000) * 44100;
    auto samples = (elapsed / 1'000'000'000) * 44100 + remainder_ / 1'000'000'000;
    remainder_ %= 1'000'000'000;
    while (samples > 0) {
        const auto chunk = std::min<std::uint64_t>(samples, 4096);
        (void)audio_.offline_mix(static_cast<std::size_t>(chunk));
        samples -= chunk;
    }
}
} // namespace toca
