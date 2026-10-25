#pragma once

#include "audio/engine.hpp"

namespace toca::audio {
// 4234fc/425880/425930: local player0's held CARHORN loop, stopped on release.
// The host supplies only eligible driving input and stops on focus/scene exit.
// Service must outlive Horn. Native car8 uses the separate original special action.
class Horn {
    Service& service_;
    EngineConfig config_;
    std::uint32_t sample_;
    Voice voice_ = 0;
    std::uint64_t serial_ = 0, played_ = 0;
    bool held_ = false;

public:
    Horn(Service& service, const AssetStore& store, const EngineConfig& config);
    Horn(Service& service, const EngineConfig& config, std::uint32_t sample);
    ~Horn();
    Horn(const Horn&) = delete;
    Horn& operator=(const Horn&) = delete;
    void update(bool held);
    // Original gain is sampled at loop start; changing view affects the next press.
    void set_camera(std::uint32_t view);
    // Stop without creating a new press if an unchanged held input is delivered.
    void stop();
    std::uint64_t played_count() const;
    bool playing() const;
};
} // namespace toca::audio
