#pragma once

#include "core/game_mode.hpp"
#include "image/decode.hpp"
#include "render/gpu_renderer.hpp"

#include <filesystem>

namespace toca {
class AssetStore;
}

namespace toca::scene {
struct HudRect {
    int x = 0, y = 0, width = 0, height = 0;
};

enum class HudAnchor {
    top_left,
    top_right,
    top_center,
    bottom_right,
    top_center_support
};

struct HudBlit {
    std::uint32_t atlas = 0;
    HudRect source;
    int x = 0, y = 0;
    int width = 0, height = 0; // Zero keeps the source size.
    HudAnchor anchor = HudAnchor::bottom_right;
};

struct HudLayoutRect {
    float x = 0, y = 0, width = 0, height = 0;
};

struct HudLayoutBlit {
    std::uint32_t atlas = 0;
    HudLayoutRect source, destination;
};

struct HudData {
    std::array<Surface, 5> atlases{
        {{nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}, {nullptr, SDL_DestroySurface}}};
    HudRect speed_background, rpm_background, lap_background, timing_labels, position_background, split_label;
    std::array<HudRect, 12> rpm_segments;
    std::array<HudRect, 12> rpm_positions;
    std::array<HudRect, 12> gear_glyphs;
    std::array<HudRect, 10> speed_glyphs;
    std::array<HudRect, 10> lap_glyphs;
    std::array<HudRect, 10> position_glyphs;
    std::array<HudRect, 14> time_glyphs;
    int unknown_time_y = 0;
    std::array<HudRect, 2> start_lamps;
    bool kilometres = false;
};

HudData load_hud(const AssetStore& store, std::uint32_t language);

struct HudSession {
    bool qualifying = false;
    std::uint32_t lap = 1, lap_limit = 3, lap_time_ms = 0, best_lap_ms = 0;
    GameMode mode = GameMode::single_race;
    std::uint32_t position = 0; //0 until the actual16-actor grid is present.
    bool split_active = false;
    std::int16_t split_ticks = 0;
    std::string split_text{};
};

// Cached41a760 values: speed remains km/h before the41de90 unit conversion;
// RPM has already passed the source500-per-update smoothing step.
struct HudSnapshot {
    std::int32_t speed_integer = 0, rpm = 2000, gear = 0;
    std::uint32_t start_phase = 0;
    HudSession session;
    bool kilometres = false, visible = true;
};

// 403dd0 / 41de90: centiseconds are truncated; zero best time is unknown.
std::string hud_time_text(std::uint32_t milliseconds);

// 45eb60 / 41eca0: target cc5ba, solid approach caption then 660ms/1s flash.
bool qualifying_rollout_notice_visible(std::uint32_t next_point, std::uint32_t point_count, std::uint64_t active_ms);

class HudValues {
    std::int32_t rpm_ = 2000, speed_ = 0, gear_ = 0;
    std::uint32_t start_phase_ = 0;
    HudSession session_;

public:
    void update(std::int32_t speed_integer, std::int32_t rpm, std::int32_t gear, std::uint32_t start_phase = 0);
    void set_session(HudSession session);
    void import_snapshot(HudSnapshot snapshot);
    const HudSession& session() const;
    std::int32_t rpm() const;
    std::int32_t gear() const;
    std::uint32_t speed(bool kilometres) const;
    std::uint32_t rpm_segments() const;
    std::uint32_t start_phase() const;
};

std::vector<HudBlit> hud_blits(const HudData& data, const HudValues& values);
// Authored 640x480 coordinates with uniform scale and explicit drawable anchors.
// The support strip repeats to the right edge; its last tile crops the source.
std::vector<HudLayoutBlit> hud_layout_blits(const HudData& data, const HudValues& values, std::uint32_t width, std::uint32_t height, std::size_t max_quads = 16384);

class Hud {
    HudData data_;
    HudValues values_;
    std::array<gpu::Texture, 5> textures_;
    bool mega_ = false, visible_ = true;

public:
    Hud(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t language, bool mega = false, const std::filesystem::path& artwork_directory = {});
    void update(std::int32_t speed_integer, std::int32_t rpm, std::int32_t gear, std::uint32_t start_phase = 0);
    void set_session(HudSession session);
    // Units must match the MILES/KM atlas chosen by the constructor language.
    void import_snapshot(HudSnapshot snapshot);
    void append(gpu::Frame& frame, std::uint32_t width, std::uint32_t height) const;
    const HudValues& values() const;
};
} // namespace toca::scene
