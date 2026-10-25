#pragma once

#include "audio/engine.hpp"
#include "audio/spatial.hpp"

namespace toca {
class SourceRandom;
}

namespace toca::audio {
struct ScrapeData {
    std::uint32_t sample = 0;
    std::int32_t gain_q16 = 0xa666;
};

// 409c30 stops the local wall handle before409fa0 starts the current contact.
// 4250a0 uses member50, a discarded random draw and half the local camera gain.
class Scrapes {
    Service& service_;
    ScrapeData data_;

    struct Handle {
        Voice voice = 0;
        std::uint64_t serial = 0;
    };

    std::array<Handle, 4> handles_{};
    std::array<std::int8_t, 256> pairs_ = [] {
        std::array<std::int8_t, 256> value;
        value.fill(-1);
        return value;
    }();
    std::int8_t wall_ = -1;
    std::uint64_t starts_ = 0;
    bool playing(std::int8_t handle) const;
    std::int8_t allocate() const;
    void event(std::int8_t handle, std::uint32_t kind, const std::array<std::int32_t, 3>& position, std::int32_t strength, const SpatialListener&, SourceRandom&);
    std::uint32_t volume_ = 0, pan_ = 64;
    bool started_ = false;
    void release();

public:
    Scrapes(Service& service, const AssetStore& store, const EngineConfig& config);
    Scrapes(Service& service, ScrapeData data);
    ~Scrapes();
    void start();
    void set_camera(std::uint32_t view);
    void update(const std::array<std::int32_t, 3>& position, std::int32_t strength_q16, const SpatialListener& listener, SourceRandom& random);
    bool pair_handle(std::uint32_t first, std::uint32_t second) const;
    void reset_pair(std::uint32_t first, std::uint32_t second);
    void pair_event(
        std::uint32_t first, std::uint32_t second, std::uint32_t kind, const std::array<std::int32_t, 3>& position, std::int32_t strength, const SpatialListener&, SourceRandom&);
    void reset_pairs();
    void stop();
    bool playing() const;
    std::uint64_t played_count() const;
    std::uint32_t volume() const;
    std::uint32_t pan() const;
};
} // namespace toca::audio
