#pragma once

#include "image/decode.hpp"

#include <array>
#include <filesystem>
#include <string_view>
#include <vector>

namespace toca::frontend {
struct Glyph {
    std::uint8_t character = 0;
    std::uint16_t x = 0, y = 0, width = 0, height = 0;
    std::array<std::int8_t, 6> spacing{};
};

struct Font {
    Surface atlas{nullptr, SDL_DestroySurface};
    std::array<std::uint8_t, 24> raw_header{};
    std::vector<Glyph> glyphs;
    int line_height = 0, space_width = 0, advance = 0;

    const Glyph* find(std::uint8_t character) const;
    int pair_spacing(std::uint8_t current, std::uint8_t previous) const;
    int text_width(std::string_view text, int advance_adjustment = 0, int space_adjustment = 0) const;
};

Font decode_font(std::span<const std::uint8_t> bytes);
void add_menu_parentheses(Font& font, const Font& punctuation);
// Mega BIG640/LIL640 artwork uses the original normalized glyph cells and metrics.
// Neutral atlases permit the original red, honors and white credit colors.
Surface load_big_font_artwork(const std::filesystem::path& directory = {}, SDL_Color face = {255, 25, 25, 255}, SDL_Color edge = {8, 8, 8, 255});
Surface load_small_font_artwork(const std::filesystem::path& directory = {}, SDL_Color face = {255, 255, 255, 255}, SDL_Color edge = {8, 8, 8, 255});
// OPTION3/OPTION3S include the same added parentheses as add_menu_parentheses.
// TITLE3 retains its authored metallic face when the tint is white.
Surface load_menu_font_artwork(std::size_t index, const Font&, const std::filesystem::path& directory = {}, SDL_Color face = {255, 255, 255, 255});
} // namespace toca::frontend
