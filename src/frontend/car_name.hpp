#pragma once

#include "frontend/font.hpp"

#include <string_view>

namespace toca {
class AssetStore;
}

namespace toca::frontend {
// 44df6e/4437f0 use the first ALLCARS member. Mega artwork retains its metrics.
class CarNameFont {
    Font font_;
    Surface mega_atlas_{nullptr, SDL_DestroySurface};

public:
    explicit CarNameFont(const AssetStore& store, bool mega = false, const std::filesystem::path& artwork_directory = {});

    // Nine-character profiles and stored names up to 255 bytes, white on
    // transparent, 128x16 or exactly 256x32 for Mega. Text beyond the bitmap
    // retains original clipping and Mega doubles the original layout coordinates.
    // Callers pass false for the original 320-pixel-wide rendering gate.
    Surface render(std::string_view name, bool draw_glyphs = true) const;
};
} // namespace toca::frontend
