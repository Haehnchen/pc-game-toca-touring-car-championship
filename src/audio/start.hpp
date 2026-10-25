#pragma once

#include "audio/engine.hpp"

namespace toca::audio {
enum class StartCue {
    red,
    go
};

struct StartAudioInputs {
    EngineInputs engine;
    std::int32_t speed_integer = 0;
};

// 44a6f0's audio-only state9 overrides; the HUD keeps raw actor inputs.
StartAudioInputs session_audio_inputs(EngineInputs raw, std::int32_t speed_integer, std::uint32_t session_state);

struct StartData {
    std::array<std::uint32_t, 2> samples{};
};

StartData load_start_audio(Service& service, const AssetStore& store, const EngineConfig& config);

// 425850: one finite sample handle, member69/red and70/go, volume60/pan64.
class Start {
    Service& service_;
    StartData data_;
    Voice voice_ = 0;
    std::uint64_t serial_ = 0, played_ = 0;
    std::uint32_t last_sample_ = 0;
    bool active_ = false;
    void stop_voice();

public:
    Start(Service& service, const AssetStore& store, const EngineConfig& config);
    Start(Service& service, StartData data);
    ~Start();
    Start(const Start&) = delete;
    Start& operator=(const Start&) = delete;
    void start();
    void update(StartCue cue);
    void stop();
    std::uint64_t played_count() const;
    std::uint32_t last_sample() const;
};
} // namespace toca::audio
