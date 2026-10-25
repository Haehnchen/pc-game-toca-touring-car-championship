#pragma once

#include "core/game_mode.hpp"
#include "frontend/font.hpp"
#include "image/decode.hpp"
#include "race/race_progress.hpp"
#include "render/gpu_renderer.hpp"
#include "scene/qualifying.hpp"

#include <filesystem>

namespace toca::scene {
class QualifyingView {
    frontend::Font title_font_, row_font_;
    gpu::Texture title_texture_, row_texture_;
    bool mega_ = false;
    Surface icon_atlas_{nullptr, SDL_DestroySurface};
    gpu::Texture icon_texture_;
    std::array<std::array<int, 4>, 10> icon_rects_{};
    std::uint32_t panel_color_ = 0x80000000;
    std::string title_, finish_title_, rollout_title_, pit_title_, pit_exit_title_;
    std::string race_title_;
    std::array<std::string, 4> race_finish_titles_;
    void append_caption(gpu::Frame&, std::string_view, bool wrap, std::uint32_t width, std::uint32_t height) const;
    void append_table(gpu::Frame&, const QualifyingResults&, bool race, std::uint32_t width, std::uint32_t height, std::uint32_t count = 16) const;

public:
    QualifyingView(const AssetStore&, gpu::Renderer&, std::uint32_t language, bool mega = false, const std::filesystem::path& artwork_directory = {});
    void append_rollout(gpu::Frame&, GameMode mode, std::uint32_t next_point, std::uint32_t point_count, std::uint64_t active_ms, std::uint32_t width, std::uint32_t height) const;
    // Draw an already selected source caption without advancing its timing gate.
    void append_rollout_caption(gpu::Frame&, GameMode mode, std::uint32_t width, std::uint32_t height) const;
    void append_finish(gpu::Frame&, std::uint32_t width, std::uint32_t height) const;
    void append_race_finish(gpu::Frame&, std::uint32_t position, std::uint32_t width, std::uint32_t height) const;
    void append_race(gpu::Frame&, const race::RaceResults&, std::uint32_t width, std::uint32_t height) const;
    void append_pit(gpu::Frame&, bool exiting, std::uint64_t active_ms, std::uint32_t width, std::uint32_t height) const;
    void append(gpu::Frame&, const QualifyingResults&, std::uint32_t width, std::uint32_t height) const;
};
} // namespace toca::scene
