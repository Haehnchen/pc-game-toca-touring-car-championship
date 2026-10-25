#include "frontend/canvas.hpp"

#include <algorithm>

namespace toca::frontend {
Canvas::Canvas(Resources resources) : resources_(resources) {}

std::uint8_t& Canvas::destination(int x, int y) {
    if (resources_.recording_content) {
        resources_.content_mask[std::size_t(y * 640 + x)] = 255;
    }
    return static_cast<std::uint8_t*>(resources_.canvas->pixels)[y * resources_.canvas->pitch + x];
}

std::uint8_t Canvas::pixel(const SDL_Surface* surface, int x, int y) {
    return static_cast<const std::uint8_t*>(surface->pixels)[y * surface->pitch + x];
}

Canvas::CanvasReplay Canvas::canvas_replay(int x, int y, int width, int height) {
    CanvasReplay result;
    result.x = std::max(0, x);
    result.y = std::max(0, y);
    const int right = std::min(640, x + width), bottom = std::min(480, y + height);
    if (!resources_.font_overlay.empty() && right > result.x && bottom > result.y) {
        result.pixels.reset(SDL_CreateSurface(right - result.x, bottom - result.y, SDL_PIXELFORMAT_RGBA32));
        gpu::check(bool(result.pixels) && SDL_FillSurfaceRect(result.pixels.get(), nullptr, 0), "Create ordered frontend canvas overlay");
    }
    return result;
}

void Canvas::replay_pixel(CanvasReplay& replay, int x, int y, std::uint8_t index) {
    if (replay.pixels) {
        const auto color = SDL_GetSurfacePalette(resources_.canvas.get())->colors[index];
        auto* rgba = static_cast<std::uint8_t*>(replay.pixels->pixels) + (y - replay.y) * replay.pixels->pitch + (x - replay.x) * 4;
        rgba[0] = color.r;
        rgba[1] = color.g;
        rgba[2] = color.b;
        rgba[3] = 255;
        replay.written = true;
    }
}

void Canvas::finish_replay(const CanvasReplay& replay) {
    if (replay.written) {
        resources_.font_overlay.emplace_back(CanvasSprite{resources_.renderer.upload(replay.pixels.get()), replay.x, replay.y, replay.pixels->w, replay.pixels->h});
    }
}

void Canvas::blit(const SDL_Surface* source, int sx, int sy, int width, int height, int x, int y, bool transparent, bool alias_edges) {
    if (!source || source->format != SDL_PIXELFORMAT_INDEX8 || sx < 0 || sy < 0 || width < 0 || height < 0 || sx + width > source->w || sy + height > source->h) {
        throw std::runtime_error("Frontend indexed image rectangle is outside its resource");
    }
    // Replay later canvas writes over earlier artwork or font quads.
    // Transparent source holes leave those earlier quads visible.
    auto overlay = canvas_replay(x, y, width, height);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            if (x + column < 0 || x + column >= 640 || y + row < 0 || y + row >= 480) {
                continue;
            }
            const auto value = pixel(source, sx + column, sy + row);
            if (!transparent || value != 0) {
                auto& target = destination(x + column, y + row);
                // FUN_0046f526 aliases only exposed run endpoints; interiors retain source indices.
                const bool endpoint = column == 0 || column == width - 1 || pixel(source, sx + column - 1, sy + row) == 0 || pixel(source, sx + column + 1, sy + row) == 0;
                const bool exposed = row == height - 1 || sy + row == 0 || sy + row + 1 == source->h || pixel(source, sx + column, sy + row - 1) == 0
                                     || pixel(source, sx + column, sy + row + 1) == 0;
                target = alias_edges && endpoint && exposed ? resources_.alias[std::size_t(value) * 256 + target] : value;
                replay_pixel(overlay, x + column, y + row, target);
            }
        }
    }
    finish_replay(overlay);
}

void Canvas::text(const Font& font, std::string_view value, int x, int y, int color, int adjustment, bool shadow, int shadow_offset) {
    if (shadow) {
        text(font, value, x + shadow_offset, y + shadow_offset, 1, adjustment, false);
    }
    CanvasReplay overlay;
    if (!resources_.mega_fonts && !resources_.font_overlay.empty()) {
        int cursor = x, left = x, right = x, height = 0;
        for (const unsigned char character : value) {
            if (character == ' ' || character == '_' || character == '^') {
                cursor += font.space_width + adjustment;
            } else if (const auto* glyph = font.find(character)) {
                left = std::min(left, cursor);
                right = std::max(right, cursor + glyph->width);
                height = std::max(height, int(glyph->height));
                cursor += glyph->width + font.advance + adjustment;
            }
        }
        overlay = canvas_replay(left, y, right - left, height);
    }
    for (const unsigned char character : value) {
        if (character == ' ' || character == '_' || character == '^') {
            x += font.space_width + adjustment;
            continue;
        }
        const auto* glyph = font.find(character);
        if (!glyph) {
            continue;
        }
        if (resources_.mega_fonts) {
            const auto index = std::size_t(&font - resources_.fonts.data());
            auto face = SDL_GetSurfacePalette(resources_.canvas.get())->colors[color < 0 ? 1 : color];
            auto edge = SDL_GetSurfacePalette(resources_.canvas.get())->colors[1];
            if (index == 3 && color < 0) {
                face = {255, 255, 255, 255}; // TITLE3's original metallic colors.
            }
            if (resources_.white_font_overlay) {
                // Honors applies upload_canvas(true, true): palette1 turns
                // black; all other nonzero indices turn white.
                face = color == 1 || color < 0 ? SDL_Color{0, 0, 0, 255} : SDL_Color{255, 255, 255, 255};
                edge = {0, 0, 0, 255};
            }
            const auto key = index < 4 ? 0
                                       : (std::uint64_t(face.r) << 40) | (std::uint64_t(face.g) << 32) | (std::uint64_t(face.b) << 24) | (std::uint64_t(edge.r) << 16)
                                             | (std::uint64_t(edge.g) << 8) | edge.b;
            auto& cached = resources_.font_colors[index][key];
            cached.used = true;
            if (!cached.texture) {
                if (index < 4) {
                    cached.texture = resources_.renderer.upload(load_menu_font_artwork(index, font).get());
                } else {
                    const auto load = index == 5 ? load_big_font_artwork : load_small_font_artwork;
                    cached.texture = resources_.renderer.upload(load({}, face, edge).get());
                }
            }
            const auto tint = index < 4 ? 0xff000000u | (std::uint32_t(face.r) << 16) | (std::uint32_t(face.g) << 8) | face.b : 0xffffffffu;
            resources_.font_overlay.emplace_back(FontGlyph{cached.texture.get(), &font, glyph, x, y, tint});
            x += glyph->width + font.advance + adjustment;
            continue;
        }
        for (int row = 0; row < glyph->height; ++row) {
            for (int column = 0; column < glyph->width; ++column) {
                if (x + column < 0 || x + column >= 640 || y + row < 0 || y + row >= 480) {
                    continue;
                }
                const auto source = pixel(font.atlas.get(), glyph->x + column, glyph->y + row);
                if (source == 0) {
                    continue;
                }
                auto& target = destination(x + column, y + row);
                if (&font == &resources_.fonts[4] || &font == &resources_.fonts[5]) {
                    // LIL640 uses1 for the face and2 for its black outline;
                    // its game palette indices are not frontend alias ramps.
                    target = source == 2 || source == 3 ? 1 : color < 0 ? source : static_cast<std::uint8_t>(color);
                } else {
                    target = color < 0 ? source : source == 1 ? static_cast<std::uint8_t>(color) : resources_.alias[std::size_t(color) * 256 + target];
                }
                replay_pixel(overlay, x + column, y + row, target);
            }
        }
        x += glyph->width + font.advance + adjustment;
    }
    finish_replay(overlay);
}

int Canvas::fit(std::string_view value, std::size_t normal, std::size_t smaller, int left, int right, int y, int color, bool center, bool shadow) {
    const auto* font = &resources_.fonts[normal];
    const bool missing_glyph = std::ranges::any_of(value, [&](unsigned char character) {
        return character != ' ' && character != '_' && character != '^' && !font->find(character);
    });
    if (missing_glyph || font->text_width(value) >= right - left) {
        font = &resources_.fonts[smaller];
    }
    int adjustment = 0;
    while (font->text_width(value, adjustment, adjustment) >= right - left && adjustment > -32) {
        --adjustment;
    }
    const int x = center ? (left + right) / 2 - font->text_width(value, adjustment, adjustment) / 2 : left;
    text(*font, value, x, y, color, adjustment, shadow);
    return x + font->text_width(value, adjustment, adjustment);
}

void Canvas::upload(SDL_GPUTexture* menu_texture, SDL_GPUTexture* content_texture, bool transparent, bool white_text) {
    resources_.recording_content = false;
    gpu::check(SDL_BlitSurface(resources_.canvas.get(), nullptr, resources_.rgba_canvas.get(), nullptr), "Convert frontend indexed canvas");
    transparent = transparent || (resources_.mega_fonts && !resources_.white_font_overlay);
    if (transparent) {
        auto* pixels = static_cast<std::uint8_t*>(resources_.rgba_canvas->pixels);
        for (int y = 0; y < 480; ++y) {
            const auto* indices = static_cast<const std::uint8_t*>(resources_.canvas->pixels) + y * resources_.canvas->pitch;
            for (int x = 0; x < 640; ++x) {
                auto* pixel = pixels + y * resources_.rgba_canvas->pitch + x * 4;
                pixel[3] = indices[x] == 0 ? 0 : 255;
                if (white_text) {
                    pixel[0] = pixel[1] = pixel[2] = indices[x] == 1 ? 0 : 255;
                }
            }
        }
    }
    resources_.renderer.update(menu_texture, resources_.rgba_canvas->pixels, 640, 480);
    // Keep the unchanged stable composition for normal frames, and a
    // separate keyed snapshot for the three independently moving regions.
    if (!resources_.mega_fonts && !resources_.white_font_overlay) {
        auto* pixels = static_cast<std::uint8_t*>(resources_.rgba_canvas->pixels);
        for (int y = 0; y < 480; ++y) {
            for (int x = 0; x < 640; ++x) {
                pixels[y * resources_.rgba_canvas->pitch + x * 4 + 3] = resources_.content_mask[std::size_t(y * 640 + x)];
            }
        }
    }
    resources_.renderer.update(content_texture, resources_.rgba_canvas->pixels, 640, 480);
}

void Canvas::append_overlays(gpu::Frame& frame) const {
    for (const auto& overlay : resources_.font_overlay) {
        if (const auto* sprite = std::get_if<CanvasSprite>(&overlay)) {
            frame.quad({.texture = sprite->texture.get(), .blend = true, .clamp = true}, float(sprite->x), float(sprite->y), float(sprite->width), float(sprite->height));
            continue;
        }
        if (const auto* sprite = std::get_if<ArtworkSprite>(&overlay)) {
            frame.quad({.texture = sprite->texture, .blend = true, .linear = true, .clamp = true}, float(sprite->x), float(sprite->y), float(sprite->width), float(sprite->height));
            continue;
        }
        const auto& item = std::get<FontGlyph>(overlay);
        const auto& glyph = *item.glyph;
        const auto first = frame.vertices.size();
        frame.quad({.texture = item.texture, .blend = true, .linear = true, .clamp = true}, float(item.x), float(item.y), float(glyph.width), float(glyph.height), item.color);
        for (std::size_t corner = 0; corner < 4; ++corner) {
            auto& vertex = frame.vertices[first + corner];
            vertex.u = (float(glyph.x) + vertex.u * glyph.width) / float(item.font->atlas->w);
            vertex.v = (float(glyph.y) + vertex.v * glyph.height) / float(item.font->atlas->h);
        }
    }
}
} // namespace toca::frontend
