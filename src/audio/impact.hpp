#pragma once

#include "audio/engine.hpp"
#include "audio/spatial.hpp"

#include <span>

namespace toca {
class SourceRandom;
}

namespace toca::audio {
struct SourceContactListener;

struct ImpactEvent {
    std::array<std::int32_t, 3> position_q16{};
    std::uint32_t kind = 1;
    std::int32_t strength_q16 = 0;
    bool local_player = true;
};

struct ImpactData {
    std::array<std::uint32_t, 24> samples{}; // Collision/prop, damage, landing51..55 and exhaust64 sounds.
    std::int32_t local_gain_q16 = 0xa666, remote_gain_q16 = 0x7333;
};

struct ImpactControl {
    std::uint32_t sample = 0, volume = 0, pan = 64;
};

ImpactData load_impact_audio(Service& service, const AssetStore& store, const EngineConfig& config);
// 00424860: collisions1, damage2..4, landing5, cone6, exhaust17, post/sign18..20, sign break21.
ImpactControl impact_control(const ImpactEvent& event, const SpatialListener& listener, const ImpactData& data, SourceRandom* random = nullptr);
//424b50 selects one voice across active views; camera gains follow424070.
ImpactControl impact_control_views(const ImpactEvent& event, std::span<const SourceContactListener> listeners, const ImpactData& data, SourceRandom* random = nullptr);

class Impact {
    Service& service_;
    ImpactData data_;
    std::array<std::uint64_t, max_effect_voices> serials_{};
    bool started_ = false;
    std::uint64_t played_count_ = 0;
    ImpactControl last_;
    void play(const ImpactControl& control);

public:
    Impact(Service& service, const AssetStore& store, const EngineConfig& config);
    Impact(Service& service, ImpactData data);
    ~Impact();
    Impact(const Impact&) = delete;
    Impact& operator=(const Impact&) = delete;
    void start();
    void set_camera(std::uint32_t view);
    void update(const ImpactEvent& event, const SpatialListener& listener, SourceRandom* random = nullptr);
    void update_views(const ImpactEvent& event, std::span<const SourceContactListener> listeners, SourceRandom* random = nullptr);
    void stop();
    std::uint64_t played_count() const;
    std::uint32_t last_sample() const;
    std::uint32_t last_volume() const;
    std::uint32_t last_pan() const;
};
} // namespace toca::audio
