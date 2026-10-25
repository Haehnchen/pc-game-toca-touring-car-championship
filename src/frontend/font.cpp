#include "frontend/font.hpp"

#include "image/artwork.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace toca::frontend {
namespace {
std::uint16_t u16(std::span<const std::uint8_t> data, std::size_t offset) {
    return std::uint16_t(data[offset]) | (std::uint16_t(data[offset + 1]) << 8);
}

std::uint32_t u32(std::span<const std::uint8_t> data, std::size_t offset) {
    return std::uint32_t(u16(data, offset)) | (std::uint32_t(u16(data, offset + 2)) << 16);
}

bool blank(std::uint8_t character) {
    return character == ' ' || character == '_' || character == '^';
}

Surface load_font_artwork(const std::filesystem::path& directory, std::string_view filename, int width, int height) {
    auto atlas = directory.empty() ? load_artwork(artwork_root(), "hud/" + std::string(filename), true) : load_artwork_file(directory / filename);
    if (atlas->w != width * 4 || atlas->h != height * 4) {
        throw std::runtime_error("Mega font artwork must retain its original atlas cells at fourfold resolution");
    }
    return atlas;
}

void color_game_font(SDL_Surface* atlas, SDL_Color face, SDL_Color edge) {
    const std::array color{face.r, face.g, face.b};
    const std::array outline{edge.r, edge.g, edge.b};
    for (int y = 0; y < atlas->h; ++y) {
        auto* row = static_cast<std::uint8_t*>(atlas->pixels) + y * atlas->pitch;
        for (int x = 0; x < atlas->w; ++x) {
            // Original callers use different edge colors: scene gray8,
            // frontend palette1 and credit black. Alpha keeps the silhouette.
            for (std::size_t channel = 0; channel < color.size(); ++channel) {
                auto& value = row[x * 4 + channel];
                value = std::uint8_t(int(outline[channel]) + std::max(0, int(value) - 8) * (int(color[channel]) - int(outline[channel])) / 247);
            }
            row[x * 4 + 3] = std::uint8_t(unsigned(row[x * 4 + 3]) * face.a / 255);
        }
    }
}
} // namespace

Surface load_big_font_artwork(const std::filesystem::path& directory, SDL_Color face, SDL_Color edge) {
    auto atlas = load_font_artwork(directory, "caption-big.webp", 256, 256);
    color_game_font(atlas.get(), face, edge);
    return atlas;
}

Surface load_small_font_artwork(const std::filesystem::path& directory, SDL_Color face, SDL_Color edge) {
    auto atlas = load_font_artwork(directory, "caption-small.webp", 256, 256);
    color_game_font(atlas.get(), face, edge);
    return atlas;
}

Surface load_menu_font_artwork(std::size_t index, const Font& font, const std::filesystem::path& directory, SDL_Color face) {
    if (index >= 4) {
        throw std::runtime_error("Mega menu font requires original font index0..3");
    }
    auto atlas = load_font_artwork(directory, "menufont-" + std::to_string(index) + ".webp", font.atlas->w, font.atlas->h);
    const std::array color{face.r, face.g, face.b, face.a};
    for (int y = 0; y < atlas->h; ++y) {
        auto* row = static_cast<std::uint8_t*>(atlas->pixels) + y * atlas->pitch;
        for (int x = 0; x < atlas->w; ++x) {
            for (std::size_t channel = 0; channel < color.size(); ++channel) {
                auto& value = row[x * 4 + channel];
                value = std::uint8_t(unsigned(value) * color[channel] / 255);
            }
        }
    }
    return atlas;
}

Font decode_font(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 0x436 || bytes[0] != 'B' || bytes[1] != 'M' || u16(bytes, 28) != 8 || u32(bytes, 30) != 0 || u32(bytes, 10) != 0x436) {
        throw std::runtime_error("Frontend font must be an uncompressed eight-bit BMP with its original palette");
    }
    const auto width = u32(bytes, 18), height = u32(bytes, 22);
    if (width == 0 || width > 16384 || height == 0 || height > 16384) {
        throw std::runtime_error("Frontend font dimensions are invalid");
    }
    const auto appendix = 0x436 + ((std::uint64_t(width) + 3) & ~std::uint64_t(3)) * height;
    if (appendix > bytes.size() || bytes.size() - appendix < 24) {
        throw std::runtime_error("Truncated frontend font header appendix");
    }
    const auto header = bytes.subspan(static_cast<std::size_t>(appendix), 24);
    const auto count = header[1];
    if (count > 127 || bytes.size() - appendix - 24 < std::size_t(count) * 16) {
        throw std::runtime_error("Truncated frontend font glyph directory");
    }
    Font font;
    std::copy(header.begin(), header.end(), font.raw_header.begin());
    font.line_height = std::bit_cast<std::int32_t>(u32(header, 4));
    font.space_width = std::bit_cast<std::int32_t>(u32(header, 8));
    font.advance = std::bit_cast<std::int32_t>(u32(header, 12));
    if (font.line_height <= 0 || font.line_height > 16384 || font.space_width < 0 || font.space_width > 16384 || font.advance < -16384 || font.advance > 16384) {
        throw std::runtime_error("Frontend font metrics are invalid");
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto record = bytes.subspan(static_cast<std::size_t>(appendix) + 24 + index * 16, 16);
        Glyph glyph;
        glyph.character = record[0];
        glyph.x = u16(record, 2);
        glyph.y = u16(record, 4);
        glyph.width = u16(record, 6);
        glyph.height = u16(record, 8);
        if (std::uint32_t(glyph.x) + glyph.width > width || std::uint32_t(glyph.y) + glyph.height > height || font.find(glyph.character)) {
            throw std::runtime_error("Frontend font glyph bounds/identity are invalid");
        }
        for (std::size_t field = 0; field < glyph.spacing.size(); ++field) {
            glyph.spacing[field] = std::bit_cast<std::int8_t>(record[10 + field]);
        }
        font.glyphs.push_back(glyph);
    }
    font.atlas = decode_bmp(bytes);
    return font;
}

void add_menu_parentheses(Font& font, const Font& punctuation) {
    // The large original menu fonts omit parentheses. Enlarge only these two
    // glyphs from the original help font; keep all existing glyphs unchanged.
    const auto old_height = font.atlas->h;
    Surface atlas(SDL_CreateSurface(font.atlas->w, old_height + font.line_height, SDL_PIXELFORMAT_INDEX8), SDL_DestroySurface);
    if (!atlas || !SDL_SetSurfacePalette(atlas.get(), SDL_GetSurfacePalette(font.atlas.get())) || !SDL_FillSurfaceRect(atlas.get(), nullptr, 0)) {
        throw std::runtime_error("Cannot extend menu font atlas");
    }
    for (int y = 0; y < old_height; ++y) {
        const auto* source = static_cast<const std::uint8_t*>(font.atlas->pixels) + y * font.atlas->pitch;
        std::copy_n(source, font.atlas->w, static_cast<std::uint8_t*>(atlas->pixels) + y * atlas->pitch);
    }
    int x = 0;
    for (const auto character : {'(', ')'}) {
        if (font.find(character)) {
            continue;
        }
        const auto* source = punctuation.find(character);
        if (!source || source->height == 0) {
            throw std::runtime_error("Original help font is missing parentheses");
        }
        const int width = std::max(1, (source->width * font.line_height + source->height / 2) / source->height);
        if (x + width > atlas->w) {
            throw std::runtime_error("Menu parentheses exceed font atlas width");
        }
        for (int row = 0; row < font.line_height; ++row) {
            const auto* pixels = static_cast<const std::uint8_t*>(punctuation.atlas->pixels) + (source->y + row * source->height / font.line_height) * punctuation.atlas->pitch;
            auto* destination = static_cast<std::uint8_t*>(atlas->pixels) + (old_height + row) * atlas->pitch + x;
            for (int column = 0; column < width; ++column) {
                destination[column] = pixels[source->x + column * source->width / width];
            }
        }
        font.glyphs.push_back({static_cast<std::uint8_t>(character), static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(old_height), static_cast<std::uint16_t>(width),
            static_cast<std::uint16_t>(font.line_height)});
        x += width;
    }
    font.atlas = std::move(atlas);
}

const Glyph* Font::find(std::uint8_t character) const {
    const auto found = std::ranges::find(glyphs, character, &Glyph::character);
    return found == glyphs.end() ? nullptr : &*found;
}

int Font::pair_spacing(std::uint8_t current, std::uint8_t previous) const {
    if (previous == 0 || blank(current) || blank(previous)) {
        return 0;
    }
    const auto* left = find(current);
    const auto* right = find(previous);
    if (!left || !right) {
        return 0;
    }
    int result = std::min({int(left->spacing[0]) + right->spacing[3], int(left->spacing[1]) + right->spacing[4], int(left->spacing[2]) + right->spacing[5]});
    return result > 1 ? result - 2 : result;
}

int Font::text_width(std::string_view text, int advance_adjustment, int space_adjustment) const {
    int width = 0;
    for (const auto character : text) {
        const auto value = static_cast<std::uint8_t>(character);
        if (blank(value)) {
            width += space_width + space_adjustment;
        } else if (const auto* glyph = find(value)) {
            width += glyph->width + advance + advance_adjustment;
        }
    }
    // Startup explicitly disables kerning in FUN_004681b1; retain pair_spacing for callers that enable it later.
    return width;
}
} // namespace toca::frontend
