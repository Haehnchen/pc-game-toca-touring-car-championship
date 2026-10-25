#pragma once

#include "frontend/font.hpp"
#include "render/gpu_renderer.hpp"

#include <array>
#include <map>
#include <variant>

namespace toca::frontend {
// All resources remain owned by View. Keeping this painter non-owning preserves
// the lifetime of cached textures and overlays retained by outgoing page turns.
class Canvas {
public:
    struct FontTexture {
        gpu::Texture texture;
        bool used = false;
    };

    struct FontGlyph {
        SDL_GPUTexture* texture;
        const Font* font;
        const Glyph* glyph;
        int x, y;
        std::uint32_t color;
    };

    struct CanvasSprite {
        gpu::Texture texture;
        int x, y, width, height;
    };

    struct CanvasReplay {
        Surface pixels{nullptr, SDL_DestroySurface};
        int x = 0, y = 0;
        bool written = false;
    };

    struct ArtworkSprite {
        SDL_GPUTexture* texture;
        int x, y, width, height;
    };

    using FontColors = std::array<std::map<std::uint64_t, FontTexture>, 6>;
    using Overlay = std::vector<std::variant<FontGlyph, CanvasSprite, ArtworkSprite>>;

    struct Resources {
        gpu::Renderer& renderer;
        std::array<Font, 6>& fonts;
        FontColors& font_colors;
        Overlay& font_overlay;
        Surface& canvas;
        Surface& rgba_canvas;
        const std::vector<std::uint8_t>& alias;
        std::vector<std::uint8_t>& content_mask;
        bool& recording_content;
        const bool& mega_fonts;
        const bool& white_font_overlay;
    };

    explicit Canvas(Resources resources);
    std::uint8_t& destination(int x, int y);
    static std::uint8_t pixel(const SDL_Surface* surface, int x, int y);
    void blit(const SDL_Surface* source, int sx, int sy, int width, int height, int x, int y, bool transparent, bool alias_edges = false);
    void text(const Font& font, std::string_view value, int x, int y, int color, int adjustment = 0, bool shadow = false, int shadow_offset = 2);
    int fit(std::string_view value, std::size_t normal, std::size_t smaller, int left, int right, int y, int color, bool center, bool shadow);
    void upload(SDL_GPUTexture* menu_texture, SDL_GPUTexture* content_texture, bool transparent = false, bool white_text = false);
    void append_overlays(gpu::Frame& frame) const;

private:
    Resources resources_;
    CanvasReplay canvas_replay(int x, int y, int width, int height);
    void replay_pixel(CanvasReplay& replay, int x, int y, std::uint8_t index);
    void finish_replay(const CanvasReplay& replay);
};
} // namespace toca::frontend
