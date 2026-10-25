#pragma once

#include "frontend/font.hpp"
#include "render/gpu_renderer.hpp"
#include "scene/pause.hpp"

namespace toca::scene {
class PauseView {
    frontend::Font title_font_, row_font_;
    gpu::Texture title_texture_, row_texture_;
    PauseLabels labels_;
    std::uint32_t panel_color_ = 0x900a0a0a;
    bool mega_ = false;
    void append_network_notice(gpu::Frame&, std::uint32_t width, std::uint32_t height, std::string_view) const;

public:
    PauseView(const AssetStore&, gpu::Renderer&, std::uint32_t language, bool mega = false, const std::filesystem::path& artwork_directory = {});
    void append(gpu::Frame&, const PauseController&, std::uint32_t width, std::uint32_t height, std::string_view notice = {}, std::string_view network_notice = {}) const;
};
} // namespace toca::scene
