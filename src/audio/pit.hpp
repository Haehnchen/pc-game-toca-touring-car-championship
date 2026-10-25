#pragma once

#include "audio/engine.hpp"

namespace toca::audio {
// 425970: the local pit-entry announcement, sample75 once, volume60/pan64.
// Service outlives this owner; focus/scene exit stop without replaying the cue.
class Pit {
    Service& service_;
    std::uint32_t sample_;
    Voice voice_ = 0;
    std::uint64_t serial_ = 0, played_ = 0;

public:
    Pit(Service& service, const AssetStore& store, const EngineConfig& config);
    Pit(Service& service, std::uint32_t sample);
    ~Pit();
    Pit(const Pit&) = delete;
    Pit& operator=(const Pit&) = delete;
    void enter();
    void stop();
    std::uint64_t played_count() const;
};
} // namespace toca::audio
