#pragma once

#include "image/decode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace toca::frontend {
inline constexpr int extras_icon_width = 88, extras_icon_height = 56;

enum class BadgeSymbol {
    star,
    fullscreen,
    vsync,
    fps,
    camera,
    unlocks
};

// Native symbols share the frontend's elliptical metal badge. Both variants
// use the active original palette and its transparent index zero.
inline Surface make_badge_icon(SDL_Palette* palette, bool selected, BadgeSymbol symbol) {
    if (!palette || palette->ncolors < 2) {
        throw std::runtime_error("Extras icon requires the frontend palette");
    }
    Surface icon(SDL_CreateSurface(extras_icon_width, extras_icon_height, SDL_PIXELFORMAT_INDEX8), SDL_DestroySurface);
    if (!icon || !SDL_SetSurfacePalette(icon.get(), palette) || !SDL_FillSurfaceRect(icon.get(), nullptr, 0)) {
        throw std::runtime_error("Could not create the Extras icon");
    }
    const auto nearest = [&](int r, int g, int b) {
        int distance = std::numeric_limits<int>::max();
        std::uint8_t index = 1;
        for (int i = 1; i < palette->ncolors; ++i) {
            const auto& color = palette->colors[i];
            const int dr = color.r - r, dg = color.g - g, db = color.b - b;
            const int candidate = dr * dr + dg * dg + db * db;
            if (candidate < distance) {
                distance = candidate;
                index = static_cast<std::uint8_t>(i);
            }
        }
        return index;
    };
    const auto put = [&](int x, int y, std::uint8_t color) {
        static_cast<std::uint8_t*>(icon->pixels)[y * icon->pitch + x] = color;
    };
    std::array<std::uint8_t, 256> metal{}, chrome{}, face{};
    for (int light = 0; light < 256; ++light) {
        metal[light] = nearest(light * 3 / 4, light * 7 / 8, light);
        chrome[light] = nearest(light, light, light);
        face[light] = selected ? nearest(light, light * 4 / 5, light / 4) : nearest(light * 7 / 8, light * 15 / 16, light);
    }
    for (int y = 1; y < 55; ++y) {
        for (int x = 1; x < 87; ++x) {
            const float dx = float(x - 44) / 42, dy = float(y - 28) / 25;
            const auto radius = dx * dx + dy * dy;
            if (radius > 1) {
                continue;
            }
            const int grain = ((x * 17 + y * 23) % 13) - 6;
            if (radius > .94f) {
                put(x, y, chrome[0]);
            } else if (radius > .80f) {
                // Broad polished outer bevel, with the original top-right
                // highlight and deep lower-left shadow.
                const float highlight = std::clamp((dx - dy) * .7f + .35f, 0.f, 1.f);
                const int light = 28 + int(highlight * 215) + grain;
                put(x, y, chrome[std::size_t(std::clamp(light, 0, 255))]);
            } else if (radius > .72f) {
                put(x, y, chrome[y < 28 ? 8 : 92]);
            } else {
                const int light = 58 + int(dx * 30 - dy * 16) + grain;
                put(x, y, metal[std::size_t(std::clamp(light, 1, 255))]);
            }
        }
    }

    struct Point {
        float x, y;
    };

    std::array<Point, 10> points{};
    constexpr float pi = 3.14159265358979323846f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const float angle = -pi / 2 + float(i) * pi / 5;
        const float radius = i % 2 == 0 ? 23.f : 10.5f;
        points[i] = {44 + std::cos(angle) * radius, 29 + std::sin(angle) * radius};
    }
    const auto inside = [&](float x, float y) {
        if (symbol == BadgeSymbol::fullscreen) {
            const bool horizontal = (y >= 13 && y <= 17) || (y >= 39 && y <= 43);
            const bool vertical = (x >= 23 && x <= 27) || (x >= 61 && x <= 65);
            return (horizontal && ((x >= 23 && x <= 36) || (x >= 52 && x <= 65))) || (vertical && ((y >= 13 && y <= 25) || (y >= 31 && y <= 43)));
        }
        if (symbol == BadgeSymbol::vsync) {
            // Two opposing arrows form a synchronization loop.
            const bool upper = (x >= 25 && x <= 59 && y >= 16 && y <= 20) || (x >= 25 && x <= 29 && y >= 16 && y <= 29);
            const bool lower = (x >= 29 && x <= 63 && y >= 36 && y <= 40) || (x >= 59 && x <= 63 && y >= 27 && y <= 40);
            const bool right_arrow = x >= 55 && x <= 66 && std::abs(y - 18) <= (66 - x) * .8f;
            const bool left_arrow = x >= 22 && x <= 33 && std::abs(y - 38) <= (x - 22) * .8f;
            return upper || lower || right_arrow || left_arrow;
        }
        if (symbol == BadgeSymbol::fps) {
            const auto dx = x - 44, dy = y - 36;
            const auto radius = dx * dx + dy * dy;
            const bool dial = y <= 36 && radius >= 17 * 17 && radius <= 21 * 21;
            const bool base = x >= 23 && x <= 65 && y >= 35 && y <= 39;
            const bool needle = x >= 42 && x <= 57 && std::abs(y - (36 - (x - 44))) <= 2;
            return dial || base || needle;
        }
        if (symbol == BadgeSymbol::camera) {
            const auto dx = x - 45, dy = y - 31;
            const auto lens = dx * dx + dy * dy;
            const bool body = x >= 23 && x <= 65 && y >= 20 && y <= 42;
            const bool top = x >= 29 && x <= 40 && y >= 14 && y <= 20;
            const bool viewfinder = x >= 56 && x <= 61 && y >= 24 && y <= 27;
            return top || (body && !viewfinder && (lens >= 9 * 9 || lens <= 4 * 4));
        }
        if (symbol == BadgeSymbol::unlocks) {
            const auto dx = x - 44, dy = y - 19;
            const auto shackle_radius = dx * dx + dy * dy;
            const bool arch = y <= 19 && shackle_radius >= 8 * 8 && shackle_radius <= 12 * 12;
            // Only the right shackle leg reaches the body; the left stays open.
            const bool shackle = arch || (x >= 52 && x <= 56 && y >= 19 && y <= 29) || (x >= 32 && x <= 36 && y >= 19 && y <= 22);
            const auto key_y = y - 34;
            const bool keyhole = dx * dx + key_y * key_y <= 3 * 3 || (x >= 42 && x <= 46 && y >= 34 && y <= 40);
            const bool body = x >= 31 && x <= 57 && y >= 27 && y <= 45;
            return shackle || (body && !keyhole);
        }
        bool result = false;
        for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            const auto a = points[i], b = points[j];
            if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) {
                result = !result;
            }
        }
        return result;
    };
    for (int y = 5; y < 54; ++y) {
        for (int x = 19; x < 71; ++x) {
            if (inside(float(x - 2), float(y - 2))) {
                put(x, y, metal[12]);
            }
        }
    }
    for (int y = 5; y < 52; ++y) {
        for (int x = 19; x < 69; ++x) {
            if (!inside(float(x), float(y))) {
                continue;
            }
            const int light = !inside(float(x - 1), float(y - 1)) ? 245 : !inside(float(x + 1), float(y + 1)) ? 78 : 218 - y + (x < 44 ? 12 : -20);
            put(x, y, face[std::size_t(light)]);
        }
    }
    return icon;
}

inline Surface make_extras_icon(SDL_Palette* palette, bool selected) {
    return make_badge_icon(palette, selected, BadgeSymbol::star);
}
} // namespace toca::frontend
