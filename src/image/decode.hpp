#pragma once

#include "formats/model.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <span>

namespace toca {
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

// A supplied complete bank restores HOR's omitted final indexed entry only
// when its stored prefix exactly matches. Palette bytes remain caller-owned.
Surface decode_bmp(std::span<const std::uint8_t> bytes, std::span<const std::uint8_t> complete_palette = {});
// Explicit alpha is retained; black pixels remain opaque unless authored otherwise.
Surface decode_webp(std::span<const std::uint8_t> bytes);
Surface decode_model_texture(const ModelTexture& texture, std::span<const std::uint8_t> complete_palette = {});
} // namespace toca
