#pragma once

#include "audio/service.hpp"

#include <array>

namespace toca {
class AssetStore;
}

namespace toca::audio {
// 426320: race menu navigation/confirmation/option samples78/79/80.
// Service outlives this owner; stop before clearing scene samples.
class Pause {
    Service& service_;
    std::array<std::uint32_t, 3> samples_;
    std::array<std::uint64_t, max_effect_voices> serials_{};
    std::uint64_t played_ = 0;

public:
    Pause(Service& service, const AssetStore& store, std::uint32_t weather);
    Pause(Service& service, std::array<std::uint32_t, 3> samples);
    ~Pause();
    Pause(const Pause&) = delete;
    Pause& operator=(const Pause&) = delete;
    void play(std::uint32_t original_sample);
    void stop();
    std::uint64_t played_count() const;
};
} // namespace toca::audio
