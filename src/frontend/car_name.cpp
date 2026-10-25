#include "frontend/car_name.hpp"

#include "assets/store.hpp"
#include "image/artwork.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::frontend {
namespace {
Surface load_name_artwork(const std::filesystem::path& directory) {
    auto atlas = directory.empty() ? load_artwork(artwork_root(), "hud/car-name.webp", true) : load_artwork_file(directory / "car-name.webp");
    if (atlas->w != 512 || atlas->h != 512) {
        throw std::runtime_error("Mega car name artwork must retain its original atlas cells at exactly twofold resolution");
    }
    return atlas;
}
} // namespace

CarNameFont::CarNameFont(const AssetStore& store, bool mega, const std::filesystem::path& artwork_directory)
    : font_(decode_font(store.open_archive("data/allcars.bfp").member(0).bytes())) {
    if (mega) {
        mega_atlas_ = load_name_artwork(artwork_directory);
    }
}

Surface CarNameFont::render(std::string_view name, bool draw_glyphs) const {
    // CONFIG/network profiles use nine characters; stored native ghost names
    // may use 255 bytes. strlen controls the original additional glyph spacing.
    name = name.substr(0, name.find('\0'));
    if (name.size() > 255) {
        throw std::runtime_error("Car name must contain at most 255 bytes");
    }
    const int scale = mega_atlas_ ? 2 : 1;
    Surface result(SDL_CreateSurface(128 * scale, 16 * scale, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    if (!result || !SDL_FillSurfaceRect(result.get(), nullptr, 0)) {
        throw std::runtime_error("Cannot allocate car name bitmap");
    }
    if (!draw_glyphs) {
        return result;
    }

    // 464cc8/46473a: kerning is disabled by 463fff; add spacing after
    // recognized glyphs only. Spaces, underscores and carets advance by eight.
    const int spacing = name.size() < 7 ? 6 : name.size() == 7 ? 5 : name.size() == 8 ? 4 : 3;
    const int width = font_.text_width(name, spacing);
    // 464dff rounds the half-width before subtracting it, retaining the source
    // one-pixel placement for odd widths. Overflowing names start at zero.
    // Calculate centering in the original raster before doubling coordinates;
    // centering the doubled width would shift odd original widths by one pixel.
    int x = width < 128 ? 64 - width / 2 : 0;
    for (const auto character : name) {
        const auto value = static_cast<std::uint8_t>(character);
        if (value == ' ' || value == '_' || value == '^') {
            x += font_.space_width;
        } else if (const auto* glyph = font_.find(value)) {
            const auto* atlas = mega_atlas_ ? mega_atlas_.get() : font_.atlas.get();
            for (int row = 0; row < std::min(int(glyph->height) * scale, result->h); ++row) {
                const auto* source = static_cast<const std::uint8_t*>(atlas->pixels) + (glyph->y * scale + row) * atlas->pitch + glyph->x * scale * (mega_atlas_ ? 4 : 1);
                auto* destination = static_cast<std::uint8_t*>(result->pixels) + row * result->pitch;
                for (int column = 0; column < glyph->width * scale; ++column) {
                    const int target = x * scale + column;
                    if (target >= 0 && target < result->w) {
                        if (mega_atlas_ && source[column * 4 + 3] != 0) {
                            std::copy_n(source + column * 4, 4, destination + target * 4);
                        } else if (!mega_atlas_ && source[column] != 0) {
                            std::fill_n(destination + target * 4, 4, std::uint8_t(255));
                        }
                    }
                }
            }
            x += glyph->width + font_.advance + spacing;
        }
    }
    return result;
}
} // namespace toca::frontend
