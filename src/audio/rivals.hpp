#pragma once

#include "audio/engine.hpp"
#include "audio/spatial.hpp"

namespace toca::audio {
struct RivalEngineInput {
    std::uint32_t actor = 0, car = 0;
    SpatialVector position{};
    std::int32_t rpm = 2000;
};

// 4259f0/425c10/425d20: the two loudest nonlocal motors, with stable actor
// order for ties. Original voices persist while their actor remains selected.
class Rivals {
    Service& service_;
    std::array<EngineLayer, 8> layers_;
    std::array<Voice, 16> voices_{};
    std::array<std::uint64_t, 16> serials_{};
    std::uint32_t playing_ = 0, volume_ = 0;

public:
    Rivals(Service&, const AssetStore&, std::uint32_t weather);
    Rivals(Service&, std::array<EngineLayer, 8> layers);
    ~Rivals();
    void update(std::span<const RivalEngineInput>, const SpatialListener&, std::uint16_t camera_view = 2);
    void stop();
    std::uint32_t playing_count() const;
    std::uint32_t maximum_volume() const;
};
} // namespace toca::audio
