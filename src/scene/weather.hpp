#pragma once

#include "scene/prop_fragments.hpp"
#include "scene/setup.hpp"

#include <optional>

namespace toca {
class AssetStore;
}

namespace toca::scene {
using WeatherVector = std::array<std::int32_t, 3>;

enum class WeatherTexture : std::uint8_t {
    snow,
    dogs,
    cats
};

struct WeatherResources {
    // 4463e0: IMAGE640 members 13,14,15, in original order.
    std::array<std::vector<std::uint8_t>, 3> textures;
};

WeatherResources load_weather_resources(const AssetStore& store);

struct WeatherCamera {
    WeatherVector position{}, forward{0, 0, 65536};
    // 497180 truncates sprite basis components, unlike the rounded position
    // and forward vector consumed by the precipitation update.
    WeatherVector right{65536, 0, 0}, up{0, 65536, 0};
};

// Recover Actor+cc/d0/d4 and +bc/c0/c4 from the native view's full affine
// inverse, then round to Q16 as462000/4628a0/4623b0/462d20 do.
WeatherCamera weather_camera(const std::array<float, 16>& world_to_view);

struct WeatherLine {
    WeatherVector from{}, to{};
    std::uint32_t color = 0;
};

struct WeatherSprite {
    // Source rectangle words: vertical, horizontal, unused Z.
    WeatherVector center{}, minimum{}, maximum{};
    WeatherTexture texture = WeatherTexture::snow;
    std::uint8_t brightness = 255, alpha = 254;
};

struct WeatherFrame {
    std::vector<WeatherVector> particles;
    std::vector<WeatherLine> lines;
    std::vector<WeatherSprite> sprites;
    WeatherVector wind{}, streak{};
    std::uint64_t updates = 0, wind_updates = 0;
    std::uint8_t lightning_alpha = 0;
};

// 4467a0 does not clear these process-global fields for a dry scene. The host
// carries them explicitly across scene ownership; wet setup replaces them.
struct WeatherWindState {
    WeatherVector velocity{};
    std::int32_t strength = 0, target_strength = 0;
    std::int16_t angle = 0, target_angle = 0;
    std::int32_t elapsed = 0, duration = 0, interval = 0, hold = 0;
    bool calm = false;
};

struct WeatherPersistentState {
    WeatherWindState wind;
    // 463100 restarts flash phase but leaves the delayed/ready thunder globals.
    std::int32_t thunder_remaining = 0, strength = 0;
    std::optional<std::int32_t> pending;
};

// Local single-view weather. The shared431820 table/cursor must outlive this
// consumer and is also used by sign fragments. No rendering call advances it.
class Weather {
    PropFragmentTables& tables_;
    WeatherFrame frame_, previous_frame_;
    std::vector<std::uint8_t> particle_cuts_;
    WeatherVector center_{}, fall_{};
    std::vector<std::uint8_t> alpha_, variants_;
    bool rain_ = false, snow_ = false, storm_ = false, sprites_ = false;
    bool calm_ = false, flashing_ = false;
    std::int32_t strength_ = 0, target_strength_ = 0;
    std::int16_t angle_ = 0, target_angle_ = 0;
    std::int32_t elapsed_ = 0, duration_ = 0, interval_ = 0, hold_ = 0;
    std::int32_t flash_remaining_ = 0, thunder_remaining_ = 0, thunder_strength_ = 0;
    std::uint32_t flash_variant_ = 0;
    std::optional<std::int32_t> pending_thunder_;
    std::int32_t sample();
    void next();
    void geometry();
    void lightning();

public:
    Weather(PropFragmentTables& tables, const SceneSetup& setup, SourceRandom& random, const WeatherCamera& initial_camera, bool low_gravity = false,
        std::optional<WeatherPersistentState> previous = std::nullopt);
    // 446a80/461bb0 wind step, after fragments and before HOR. Returns the previous
    // view update's thunder event once. Audio424860's sample choice belongs
    // to the audio caller's shared SourceRandom, never this frame accessor.
    std::optional<std::int32_t> advance();
    // 446d30's camera-relative particles and storm state. The native host
    // schedules one call per active25Hz tick, independently of render FPS.
    void update_view(const WeatherCamera& camera);
    const WeatherFrame& frame() const;
    // Render-only positions. Wrapped/recentered particles cut to current;
    // lightning, texture variants, wind and thunder retain their source ticks.
    WeatherFrame presentation_frame(float alpha) const;
    void presentation_frame_into(WeatherFrame& result, float alpha) const;
    WeatherWindState wind_state() const;
    WeatherPersistentState persistent_state() const;
};
} // namespace toca::scene
