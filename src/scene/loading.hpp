#pragma once

#include "assets/store.hpp"
#include "frontend/font.hpp"
#include "frontend/records.hpp"
#include "render/gpu_renderer.hpp"

#include <optional>
#include <span>
#include <string>

namespace toca::scene {
struct LoadingStatistics {
    std::uint32_t language = 0, laps = 0;
    bool time_trial = false;
    frontend::LapRecord record;
    std::optional<frontend::LapRecord> ghost;
};

class LoadingView {
    gpu::Texture image_, side_backdrop_, status_texture_, statistics_texture_;
    frontend::Font status_font_, statistics_font_;
    std::array<std::string, 8> statistics_;
    bool mega_image_ = false, mega_status_ = false, mega_statistics_ = false, show_statistics_ = false;

public:
    LoadingView(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t track_id, bool mega = false, const std::filesystem::path& artwork_directory = {},
        const std::optional<LoadingStatistics>& statistics = {});
    gpu::Frame frame(std::uint32_t completed, std::uint32_t total, std::span<const std::string> status = {}) const;
};
} // namespace toca::scene
