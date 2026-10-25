#pragma once

#include "audio/engine.hpp"

namespace toca {
class SourceRandom;
}

namespace toca::audio {
// 446a80 ->424860(event25): delayed, centered, finite thunder76/77.
class Weather {
    Service& service_;
    std::array<std::uint32_t, 2> samples_;
    std::array<std::uint64_t, max_effect_voices> serials_{};
    std::uint64_t played_ = 0;
    std::uint32_t last_sample_ = 0;

public:
    Weather(Service& service, const AssetStore& store, const EngineConfig& config);
    Weather(Service& service, std::array<std::uint32_t, 2> samples);
    ~Weather();
    Weather(const Weather&) = delete;
    Weather& operator=(const Weather&) = delete;
    void thunder(std::int32_t strength_q16, SourceRandom& random);
    void stop();
    std::uint64_t played_count() const;
    std::uint32_t last_sample() const;
};
} // namespace toca::audio
