#include "frontend/font.hpp"
#include "scene/hud_artwork.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void transparency(const SDL_Surface* atlas) {
    std::size_t transparent = 0, visible = 0, antialiased = 0;
    for (int y = 0; y < atlas->h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(atlas->pixels) + y * atlas->pitch;
        for (int x = 0; x < atlas->w; ++x) {
            const auto alpha = row[x * 4 + 3];
            transparent += alpha == 0;
            visible += alpha != 0;
            antialiased += alpha != 0 && alpha != 255;
        }
    }
    expect(visible > 100 && transparent > 100, "Replacement HUD atlas must retain artwork and transparent sprite backgrounds");
    expect(antialiased > 20, "Replacement HUD artwork must carry authored smooth alpha edges");
}

void language_selection(const toca::scene::HudArtwork& km, const toca::scene::HudArtwork& miles) {
    // Original language0 selects only the primary unit artwork; the position,
    // gear and starting-light atlases must remain byte-identical after decode.
    for (std::size_t index = 1; index < km.size(); ++index) {
        const auto* first = km[index].get();
        const auto* second = miles[index].get();
        expect(first->w == second->w && first->h == second->h, "HUD language selection must retain the shared atlas dimensions");
        for (int y = 0; y < first->h; ++y) {
            const auto* first_row = static_cast<const std::uint8_t*>(first->pixels) + y * first->pitch;
            const auto* second_row = static_cast<const std::uint8_t*>(second->pixels) + y * second->pitch;
            expect(std::memcmp(first_row, second_row, std::size_t(first->w) * 4) == 0, "HUD language selection must not replace shared gear, rank or start artwork");
        }
    }
    // Original speed-background rectangle includes KpH/MILES. Check this exact
    // active region, so different unrelated atlas pixels cannot satisfy it.
    constexpr int scale = toca::scene::hud_artwork_scale;
    std::size_t different = 0;
    for (int y = 190 * scale; y < 210 * scale; ++y) {
        const auto* first_row = static_cast<const std::uint8_t*>(km[0]->pixels) + y * km[0]->pitch;
        const auto* second_row = static_cast<const std::uint8_t*>(miles[0]->pixels) + y * miles[0]->pitch;
        for (int x = 15 * scale; x < 165 * scale; ++x) {
            different += std::memcmp(first_row + x * 4, second_row + x * 4, 4) != 0;
        }
    }
    expect(different > 100, "KM and MILES must select distinct replacement units in the original speed-label region");
}

void legible_details(const SDL_Surface* atlas) {
    constexpr int scale = toca::scene::hud_artwork_scale;
    std::size_t descender = 0, separator = 0, split_label = 0;
    // 4ada60 is drawn by41e3d0 whenever a checkpoint split is active.
    // Geometry parity alone cannot detect an empty replacement sprite.
    for (int y = 132 * scale; y < 143 * scale; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(atlas->pixels) + y * atlas->pitch;
        for (int x = scale; x < 76 * scale; ++x) {
            const auto* pixel = row + x * 4;
            split_label += pixel[0] > 100 && pixel[1] > 100 && pixel[2] > 100 && pixel[3] > 200;
        }
    }
    expect(split_label > 1000, "Checkpoint SPLIT must have visible letter artwork in both unit atlases");
    // In both original units the lowercase p continues below the M/K/H
    // baseline. A seven-unit capital box silently cropped this entire stroke.
    for (int y = 209 * scale; y < 210 * scale; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(atlas->pixels) + y * atlas->pitch;
        for (int x = 66 * scale; x < 70 * scale; ++x) {
            const auto* pixel = row + x * 4;
            descender += pixel[0] > 50 && pixel[1] > 50 && pixel[2] > 50 && pixel[3] > 200;
        }
    }
    for (int y = 228 * scale; y < 239 * scale; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(atlas->pixels) + y * atlas->pitch;
        for (int x = 128 * scale; x < 137 * scale; ++x) {
            const auto* pixel = row + x * 4;
            separator += pixel[0] > 80 && pixel[1] > 80 && pixel[2] > 80 && pixel[3] > 200;
        }
    }
    expect(descender > 8, "The original lowercase p in MpH/KpH must retain its visible descender");
    expect(separator > 50, "The lap-limit slash must have a visible light stroke, not only a black outline");
}

template <typename Face> void sprite_contrast(const SDL_Surface* atlas, const std::array<int, 4>& cell, Face is_face) {
    constexpr int scale = toca::scene::hud_artwork_scale;
    std::size_t face = 0, edge = 0;
    for (int y = cell[1] * scale; y < (cell[1] + cell[3]) * scale; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(atlas->pixels) + y * atlas->pitch;
        for (int x = cell[0] * scale; x < (cell[0] + cell[2]) * scale; ++x) {
            const auto* pixel = row + x * 4;
            face += pixel[3] > 200 && is_face(pixel);
            edge += pixel[3] > 200 && std::max({pixel[0], pixel[1], pixel[2]}) < 30;
        }
    }
    expect(face > 20 && edge > 20, "Each HUD glyph must retain its colored face and opaque near-black edge inside its source rectangle");
}

void outlined_digits(const toca::scene::HudArtwork& artwork) {
    const auto red = [](const std::uint8_t* pixel) {
        return pixel[0] > 150 && pixel[1] < 80 && pixel[2] < 80;
    };
    const auto yellow = [](const std::uint8_t* pixel) {
        return pixel[0] > 150 && pixel[1] > 150 && pixel[2] < 80;
    };
    constexpr std::array<int, 14> small_x{0, 21, 40, 59, 78, 97, 116, 135, 154, 173, 192, 200, 219, 235};
    constexpr std::array<int, 14> small_width{19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 7, 19, 11, 7};
    // TIME/BEST, unknown times and splits share these digit/punctuation cells.
    // Check each cell: neighboring dark artwork cannot hide a missing outline.
    for (std::size_t index = 0; index < small_x.size(); ++index) {
        sprite_contrast(artwork[0].get(), {small_x[index], 106, small_width[index], 12}, red);
        sprite_contrast(artwork[0].get(), {small_x[index], 118, small_width[index], 12}, yellow);
    }
    constexpr std::array<std::array<int, 4>, 11> large{{{60, 70, 62, 35}, {0, 0, 36, 35}, {36, 0, 63, 35}, {99, 0, 62, 35}, {161, 0, 60, 35}, {0, 35, 61, 35}, {61, 35, 61, 35},
        {123, 35, 57, 35}, {180, 35, 62, 35}, {0, 70, 60, 35}, {122, 70, 65, 35}}};
    for (const auto& cell : large) {
        sprite_contrast(artwork[1].get(), cell, red);
    }
    // MpH/KpH keep their pale face against the original near-black backing.
    sprite_contrast(artwork[0].get(), {48, 201, 48, 9}, [](const std::uint8_t* pixel) {
        return std::min({pixel[0], pixel[1], pixel[2]}) > 150;
    });
}

void caption_glyphs(const std::filesystem::path& directory) {
    const auto red = toca::frontend::load_big_font_artwork(directory);
    const auto white = toca::frontend::load_big_font_artwork(directory, {255, 255, 255, 255});
    const auto finale = toca::frontend::load_big_font_artwork(directory, {255, 255, 255, 255}, {0, 0, 0, 255});
    const auto honors = toca::frontend::load_big_font_artwork(directory, {247, 247, 0, 255}, {16, 16, 16, 255});
    transparency(red.get());
    // Exact BIG640 source cells, including glyphs not used by English rollout.
    constexpr std::array<std::array<int, 3>, 46> cells{{{0, 0, 37}, {38, 0, 38}, {77, 0, 37}, {115, 0, 38}, {154, 0, 39}, {194, 0, 37}, {0, 36, 37}, {38, 36, 43}, {82, 36, 24},
        {107, 36, 36}, {144, 36, 45}, {190, 36, 31}, {0, 72, 47}, {48, 72, 43}, {92, 72, 38}, {131, 72, 37}, {169, 72, 38}, {208, 72, 39}, {0, 108, 35}, {36, 108, 35},
        {72, 108, 40}, {113, 108, 37}, {151, 108, 49}, {201, 108, 45}, {0, 144, 37}, {38, 144, 42}, {81, 144, 32}, {114, 144, 27}, {142, 144, 37}, {180, 144, 33}, {214, 144, 35},
        {0, 180, 35}, {36, 180, 33}, {70, 180, 33}, {104, 180, 33}, {138, 180, 33}, {172, 180, 23}, {196, 180, 31}, {228, 180, 16}, {0, 216, 17}, {18, 216, 20}, {39, 216, 18},
        {58, 216, 24}, {83, 216, 31}, {115, 216, 21}, {137, 216, 26}}};
    constexpr int scale = toca::scene::hud_artwork_scale;
    for (const auto& cell : cells) {
        std::size_t face = 0, outline = 0;
        for (int y = cell[1] * scale; y < (cell[1] + 36) * scale; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(red->pixels) + y * red->pitch;
            for (int x = cell[0] * scale; x < (cell[0] + cell[2]) * scale; ++x) {
                const auto* pixel = row + x * 4;
                if (x == cell[0] * scale || x == (cell[0] + cell[2]) * scale - 1 || y == cell[1] * scale || y == (cell[1] + 36) * scale - 1) {
                    expect(pixel[3] == 0, "Font cells must have transparent borders so linear sampling cannot pull a dark line from an adjacent glyph");
                }
                face += pixel[3] > 200 && pixel[0] > 150 && pixel[1] < 80 && pixel[2] < 80;
                outline += pixel[3] > 200 && std::max({pixel[0], pixel[1], pixel[2]}) < 60;
            }
        }
        expect(face > 20 && outline > 20, "Every original BIG640 glyph must retain a red face and dark outline inside its own source cell");
    }
    std::size_t neutral_edges = 0, white_faces = 0;
    for (int y = 0; y < red->h; ++y) {
        const auto* colored = static_cast<const std::uint8_t*>(red->pixels) + y * red->pitch;
        const auto* neutral = static_cast<const std::uint8_t*>(white->pixels) + y * white->pitch;
        const auto* finale_row = static_cast<const std::uint8_t*>(finale->pixels) + y * finale->pitch;
        const auto* honors_row = static_cast<const std::uint8_t*>(honors->pixels) + y * honors->pitch;
        for (int x = 0; x < red->w; ++x) {
            const auto* a = colored + x * 4;
            const auto* b = neutral + x * 4;
            const auto* c = finale_row + x * 4;
            const auto* d = honors_row + x * 4;
            expect(a[3] == b[3] && a[3] == c[3] && a[3] == d[3], "Scene, honors and finale font palettes must preserve exact glyph coverage");
            if (b[3] > 200 && b[0] == 8 && b[1] == 8 && b[2] == 8) {
                expect(a[0] == 8 && a[1] == 8 && a[2] == 8 && c[0] == 0 && c[1] == 0 && c[2] == 0 && d[0] == 16 && d[1] == 16 && d[2] == 16,
                    "BIG640 must preserve the distinct original scene8, finale0 and honors16 edge palettes");
            }
            neutral_edges += a[3] > 200 && a[0] <= 8 && a[1] <= 8 && a[2] <= 8 && std::memcmp(a, b, 4) == 0;
            white_faces += b[3] > 200 && b[0] > 200 && b[1] > 200 && b[2] > 200 && a[0] > 200 && a[1] < 80 && a[2] < 80;
        }
    }
    expect(neutral_edges > 100 && white_faces > 1000, "White finale headings and red captions must share artwork while retaining neutral black edges");
}

void other_fonts(const std::filesystem::path& directory) {
    const auto small = toca::frontend::load_small_font_artwork(directory);
    const auto small_finale = toca::frontend::load_small_font_artwork(directory, {255, 255, 255, 255}, {0, 0, 0, 255});
    transparency(small.get());
    std::size_t white_faces = 0, dark_edges = 0;
    for (int y = 0; y < small->h; ++y) {
        const auto* a = static_cast<const std::uint8_t*>(small->pixels) + y * small->pitch;
        const auto* b = static_cast<const std::uint8_t*>(small_finale->pixels) + y * small_finale->pitch;
        for (int x = 0; x < small->w; ++x) {
            expect(a[x * 4 + 3] == b[x * 4 + 3], "Pause and credit rows must share the exact LIL640 silhouette");
            white_faces += a[x * 4 + 3] == 255 && a[x * 4] > 240 && a[x * 4 + 1] > 240 && a[x * 4 + 2] > 240;
            if (a[x * 4 + 3] == 255 && a[x * 4] == 8 && a[x * 4 + 1] == 8 && a[x * 4 + 2] == 8) {
                expect(b[x * 4] == 0 && b[x * 4 + 1] == 0 && b[x * 4 + 2] == 0, "Finale must retain its original black LIL640 edges");
                ++dark_edges;
            }
        }
    }
    expect(white_faces > 1000 && dark_edges > 100, "LIL640 must retain substantial white faces and dark shadows");
    constexpr std::array<std::array<int, 2>, 4> sizes{{{140, 114}, {116, 114}, {88, 64}, {232, 148}}};
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        toca::frontend::Font metadata;
        metadata.atlas.reset(SDL_CreateSurface(sizes[index][0], sizes[index][1], SDL_PIXELFORMAT_INDEX8));
        expect(bool(metadata.atlas), "Create original menu font dimensions");
        const auto artwork = toca::frontend::load_menu_font_artwork(index, metadata, directory);
        transparency(artwork.get());
        expect(artwork->w == sizes[index][0] * 4 && artwork->h == sizes[index][1] * 4,
            "Every frontend font must retain its own atlas proportions and the native appended parentheses row");
        if (index == 3) {
            std::size_t shadow = 0, highlight = 0;
            for (int y = 0; y < artwork->h; ++y) {
                const auto* row = static_cast<const std::uint8_t*>(artwork->pixels) + y * artwork->pitch;
                for (int x = 0; x < artwork->w; ++x) {
                    const auto* pixel = row + x * 4;
                    shadow += pixel[3] > 200 && std::max({pixel[0], pixel[1], pixel[2]}) < 40;
                    highlight += pixel[3] > 200 && std::min({pixel[0], pixel[1], pixel[2]}) > 100;
                }
            }
            expect(shadow > 1000 && highlight > 100, "TITLE3 must preserve its dark metal face and bright bevel rather than becoming a flat white font");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Usage: toca-hud-artwork-tests ARTWORK_DIRECTORY");
        }
        const auto km = toca::scene::load_hud_artwork(argv[1], true);
        const auto miles = toca::scene::load_hud_artwork(argv[1], false);
        for (const auto& atlas : km) {
            transparency(atlas.get());
        }
        transparency(miles[0].get());
        language_selection(km, miles);
        legible_details(km[0].get());
        legible_details(miles[0].get());
        outlined_digits(km);
        outlined_digits(miles);
        caption_glyphs(argv[1]);
        other_fonts(argv[1]);
        std::cout << "PASS: authored replacement HUD atlases decode at compatible dimensions with smooth transparent artwork\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
