#pragma once

#include "image/decode.hpp"

#include <filesystem>

namespace toca::scene {
inline constexpr int hud_artwork_scale = 4;
using HudArtwork = std::array<Surface, 5>;

// Authored replacement atlases retain the canonical normalized sprite regions.
// All files carry their own alpha; never apply the original RGB-black key again.
HudArtwork load_hud_artwork(const std::filesystem::path& directory, bool kilometres);
} // namespace toca::scene
