#pragma once

#include "audio/engine.hpp"

namespace toca::audio {
struct FinishData {
    std::uint32_t sample = 0, volume = 127;
};

FinishData load_finish_audio(Service& service, const AssetStore& store, const EngineConfig& config);

// 4058e0/405960: owned sample83 loop shared by qualifying and race endings.
class Finish {
    Service& service_;
    FinishData data_;
    Voice voice_ = 0;
    std::uint64_t serial_ = 0, played_ = 0;
    std::uint32_t volume_ = 0;
    bool requested_ = false, active_ = true, fading_ = false;
    void play();
    void stop_voice();

public:
    Finish(Service& service, const AssetStore& store, const EngineConfig& config);
    Finish(Service& service, FinishData data);
    ~Finish();
    Finish(const Finish&) = delete;
    Finish& operator=(const Finish&) = delete;
    void start();
    void fade();
    void update();
    void set_active(bool active);
    void stop();
    bool playing() const;
    std::uint32_t volume() const;
    std::uint64_t played_count() const;
};
} // namespace toca::audio
