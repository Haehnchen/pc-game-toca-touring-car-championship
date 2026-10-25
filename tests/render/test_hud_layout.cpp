#include "assets/store.hpp"
#include "scene/hud.hpp"

#include <array>
#include <cmath>
#include <iostream>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Image {
    int width, height;
    std::vector<Uint32> pixels;

    Uint32 at(int x, int y) const {
        return pixels.at(std::size_t(y * width + x));
    }
};

struct LampCenter {
    double x, y;
};

LampCenter housing_ring_center(const Image& housing, int lamp, std::uint32_t phase) {
    const auto scale = std::min(double(housing.width) / 640, double(housing.height) / 480);
    const auto left = (housing.width - 640 * scale) / 2;
    const LampCenter approximate{left + (270.5 + lamp * 49) * scale, (phase == 2 ? 86.7 : 143.5) * scale};
    const auto axis_center = [&](bool horizontal) {
        const auto along = horizontal ? approximate.x : approximate.y;
        const auto across = horizontal ? approximate.y : approximate.x;
        std::array<double, 5> centers;
        for (int scan = 0; scan < 5; ++scan) {
            const int fixed = int(std::floor(across + (scan - 2) * scale));
            double edges = 0;
            for (const int side : {-1, 1}) {
                int brightest = 0, position = 0;
                const double begin = along + (side < 0 ? -29 : 17) * scale;
                const double end = along + (side < 0 ? -17 : 29) * scale;
                for (int sample = int(std::ceil(begin)); sample < int(std::floor(end)); ++sample) {
                    const auto pixel = horizontal ? housing.at(sample, fixed) : housing.at(fixed, sample);
                    const int red = (pixel >> 16) & 255, green = (pixel >> 8) & 255, blue = pixel & 255;
                    const int neutral = std::min({red, green, blue});
                    if (std::max({red, green, blue}) - neutral < 30 && neutral > brightest) {
                        brightest = neutral;
                        position = sample;
                    }
                }
                expect(brightest > 45, "Housing-only scan must find both neutral metal rim edges");
                edges += position + .5;
            }
            centers[scan] = edges / 2;
        }
        // Nearby scans suppress isolated highlights in the generated metal.
        std::ranges::sort(centers);
        return centers[2];
    };
    return {axis_center(true), axis_center(false)};
}

void aligned_start_lamps(const Image& housing, const Image& illuminated, std::uint32_t phase) {
    expect(housing.width == illuminated.width && housing.height == illuminated.height, "Lamp comparison requires equal render sizes");
    const auto scale = std::min(double(housing.width) / 640, double(housing.height) / 480);
    const auto left = (housing.width - 640 * scale) / 2;
    for (int lamp = 0; lamp < 3; ++lamp) {
        std::size_t colored = 0;
        double weight = 0, center_x = 0, center_y = 0;
        for (int y = int(std::ceil((phase == 2 ? 52 : 113) * scale)); y < int(std::floor((phase == 2 ? 116 : 177) * scale)); ++y) {
            // Socket midpoints partition the lamps without cropping a lens.
            for (int x = int(std::ceil(left + (246 + lamp * 49) * scale)); x < int(std::floor(left + (295 + lamp * 49) * scale)); ++x) {
                const auto pixel = illuminated.at(x, y);
                const int red = (pixel >> 16) & 255, green = (pixel >> 8) & 255, blue = pixel & 255;
                const bool lit = phase == 2 ? red > 100 && red > green + 40 && red > blue + 40 : green > 100 && green > red + 40 && green > blue + 40;
                if (lit) {
                    ++colored;
                    const auto intensity = phase == 2 ? red - std::max(green, blue) : green - std::max(red, blue);
                    weight += intensity;
                    center_x += (x + .5) * intensity;
                    center_y += (y + .5) * intensity;
                }
            }
        }
        expect(colored > 20 * scale * scale, "Replacement start lamps must retain their red hold and green release colors");
        const LampCenter lens{center_x / weight, center_y / weight};
        // Preserve the original sprite-pitch assertion independently of the
        // physical socket measurement from the housing-only render.
        expect(std::abs(lens.x - left - (270.5 + lamp * 49) * scale) < scale && std::abs(lens.y - (phase == 2 ? 86.7 : 143.5) * scale) < scale,
            "Illuminated start-light lenses must retain the original sprite centers");
        const auto ring = housing_ring_center(housing, lamp, phase);
        const auto dx = (lens.x - ring.x) / scale, dy = (lens.y - ring.y) / scale;
        std::cout << "Start lamp " << lamp + 1 << " phase" << phase << " at " << housing.width << 'x' << housing.height << ": lens/rim offset " << dx << ',' << dy << '\n';
        // Horizontal drift is the reported defect; the generated upper/lower
        // metal highlights have a small independent vertical asymmetry.
        expect(std::abs(dx) < .8 && std::abs(dy) < 1.5, "Illuminated start-light lenses must center inside the independently measured housing rims");
    }
}

void start_closeup(const Image& image, const char* path) {
    const auto scale = std::min(double(image.width) / 640, double(image.height) / 480);
    const int x = int(std::floor((image.width - 640 * scale) / 2 + 229 * scale));
    const int y = int(std::floor(50 * scale));
    const int width = int(std::ceil(256 * scale)), height = int(std::ceil(128 * scale));
    std::vector<Uint32> pixels(std::size_t(width) * height);
    for (int row = 0; row < height; ++row) {
        std::copy_n(image.pixels.begin() + std::size_t(y + row) * image.width + x, width, pixels.begin() + std::size_t(row) * width);
    }
    toca::gpu::Surface surface(SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_XRGB8888, pixels.data(), width * 4), SDL_DestroySurface);
    toca::gpu::check(bool(surface), "Create game-window start-light closeup");
    toca::gpu::check(SDL_SavePNG(surface.get(), path), "Save game-window start-light closeup");
}

void gauge_closeup(const Image& image, const char* path) {
    const auto scale = std::min(double(image.width) / 640, double(image.height) / 480);
    const int x = int(std::floor(image.width - 195 * scale));
    const int y = int(std::floor(image.height - 60 * scale));
    const int width = image.width - x, height = image.height - y;
    std::vector<Uint32> pixels(std::size_t(width) * height);
    for (int row = 0; row < height; ++row) {
        std::copy_n(image.pixels.begin() + std::size_t(y + row) * image.width + x, width, pixels.begin() + std::size_t(row) * width);
    }
    toca::gpu::Surface surface(SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_XRGB8888, pixels.data(), width * 4), SDL_DestroySurface);
    toca::gpu::check(bool(surface), "Create game-window gauge closeup");
    toca::gpu::check(SDL_SavePNG(surface.get(), path), "Save game-window gauge closeup");
}

void clean_lap_one_top(const Image& image) {
    const auto scale = std::min(double(image.width) / 640, double(image.height) / 480);
    // The single1 starts at96,5. Its top-left interior is transparent; an
    // atlas wrap can instead pull the bottom POS plaque across this first row.
    const int y = int(std::floor(5 * scale));
    const auto empty = image.at(int(90 * scale), y);
    for (int x = int(std::ceil(98 * scale)); x < int(104 * scale); ++x) {
        expect(image.at(x, y) == empty, "Lap1 must not sample a stray horizontal line from a neighboring atlas edge");
    }
}

void equal_region(const Image& original, const Image& wide, int x, int y, int width, int height, int shift, const char* message) {
    std::size_t visible = 0;
    for (int row = y; row < y + height; ++row) {
        for (int column = x; column < x + width; ++column) {
            expect(original.at(column, row) == wide.at(column + shift, row), message);
            visible += original.at(column, row) != toca::gpu::background;
        }
    }
    expect(visible > 50, "Compared HUD region contains no original artwork");
}

void anchored_pixels(const Image& original, const Image& wide, bool filtered = false) {
    const auto extra = wide.width - original.width;
    equal_region(original, wide, 0, 0, 200, 45, 0, "Lap panel or lap limit moved away from the left edge");
    equal_region(original, wide, 208, 5, 252, 40, extra / 2, "Lap times and TIME/BEST labels lost their top-center artwork or placement");
    equal_region(original, wide, 445, 420, 195, 60, extra, "Speed, gear or RPM artwork lost its bottom-right placement");
    equal_region(original, wide, 229, 50, 256, 128, extra / 2, "Start housing or lamps lost their top-center artwork or placement");
    std::size_t visible = 0;
    for (int y = 50; y < 178; ++y) {
        for (int x = 485 + extra / 2; x < wide.width; ++x) {
            const auto source = original.at(485 + (x - 485 - extra / 2) % 155, y);
            const auto actual = wide.at(x, y);
            if (filtered) {
                // The77/155 texture gradient is not binary-exact. Translating
                // or clipping a linearly filtered quad can put the same sample
                // on opposite sides of an8-bit rounding boundary. Allow only
                // that single quantization step, independently per RGB channel.
                for (const auto shift : {0, 8, 16}) {
                    expect(std::abs(int((actual >> shift) & 255) - int((source >> shift) & 255)) <= 1,
                        "Filtered start support must retain its lattice within one8-bit quantization step");
                }
            } else {
                expect(actual == source, "Start support must repeat and clip the original lattice through the right edge");
            }
            visible += source != toca::gpu::background;
        }
    }
    expect(visible > 100, "Start support comparison contains no original lattice");
}

void valid_geometry(const toca::gpu::Frame& frame, int width, int height, bool original = true) {
    expect(!frame.vertices.empty(), "HUD produced no drawable geometry");
    for (const auto& vertex : frame.vertices) {
        expect(vertex.diffuse == 0xffc8c8c8, "Original490c00 HUD sprites require RGB200 modulation with opaque alpha");
        expect(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.u) && std::isfinite(vertex.v), "HUD produced nonfinite geometry");
        const float edge = original ? 0 : -.5f;
        expect(vertex.x >= edge && vertex.y >= edge && vertex.x <= float(width) + edge && vertex.y <= float(height) + edge, "HUD artwork extends beyond the drawable");
        expect(vertex.u >= 0 && vertex.v >= 0, "HUD atlas coordinates must retain their nonnegative source origin");
    }
    expect(std::ranges::all_of(frame.draws,
               [](const auto& draw) {
                   return draw.state.clamp;
               }),
        "Source +0.1 texel corners and clipped support tails require clamped atlas sampling");
    for (std::size_t first = 0; first < frame.vertices.size(); first += 4) {
        expect(frame.vertices[first + 1].x > frame.vertices[first].x && frame.vertices[first + 2].y > frame.vertices[first].y, "HUD emitted an empty or inverted sprite");
    }
}

void reference(const char* root, const char* artwork_directory) {
    using namespace toca;
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private HUD layout view");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA HUD layout", 640, 480, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private HUD layout window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(root);
    scene::Hud hud(store, renderer, 2);
    for (int tick = 0; tick < 16; ++tick) {
        hud.update(172, 7000, 4);
    }

    scene::Hud mega(store, renderer, 2, true, artwork_directory);
    for (int tick = 0; tick < 16; ++tick) {
        mega.update(172, 7000, 4);
    }
    auto* active = &hud;
    auto* reference_hud = &hud;
    std::string prefix;
    const auto render = [&](int width, int height, std::uint32_t phase, std::uint32_t position = 0, std::uint32_t lap = 12, std::int32_t rpm = 7000,
                            std::uint32_t lap_time = 61'239, const std::string& split = {}) {
        gpu::check(SDL_SetWindowSize(window.get(), width, height), "Resize HUD target");
        gpu::check(SDL_SyncWindow(window.get()), "Synchronize HUD target size");
        SDL_PumpEvents();
        // Settle the original500RPM/update cache before comparing each state.
        for (int tick = 0; tick < 16; ++tick) {
            reference_hud->update(172, rpm, 4, phase);
            if (active != reference_hud) {
                active->update(172, rpm, 4, phase);
            }
        }
        reference_hud->set_session({false, lap, 15, lap_time, phase == 2 ? 60'110u : 0u, GameMode::single_race, position, !split.empty(), 50, split});
        if (active != reference_hud) {
            active->set_session(reference_hud->values().session());
        }
        gpu::Frame frame;
        active->append(frame, std::uint32_t(width), std::uint32_t(height));
        if (active != reference_hud) {
            gpu::Frame canonical;
            reference_hud->append(canonical, std::uint32_t(width), std::uint32_t(height));
            expect(frame.vertices.size() == canonical.vertices.size() && frame.indices == canonical.indices && frame.draws.size() == canonical.draws.size(),
                "Mega replacement artwork must retain every original sprite, index and draw");
            for (std::size_t index = 0; index < frame.vertices.size(); ++index) {
                const auto& actual = frame.vertices[index];
                const auto& original = canonical.vertices[index];
                expect(actual.x + .5f == original.x && actual.y + .5f == original.y && actual.z == original.z && actual.rhw == original.rhw && actual.diffuse == original.diffuse
                           && actual.specular == original.specular,
                    "Mega pixel edges must retain the source TL sprite bounds and original colors");
            }
        }
        valid_geometry(frame, width, height, active == reference_hud);
        if (active == reference_hud && width == 640 && height == 480 && phase >= 1 && phase <= 3) {
            const auto support = std::ranges::find_if(frame.vertices, [](const auto& vertex) {
                return vertex.x == 485 && vertex.y == 50 && vertex.u < .01f;
            });
            expect(support != frame.vertices.end() && support->u == float(.1 / 128) && support->v == float(64.1 / 128) && (support + 1)->x == 640
                       && (support + 1)->u == float(77.1 / 128),
                "490cb0 start support must retain its integer TL corners and +0.1 texel source UVs");
        }
        expect(renderer.draw(frame, false), "Private HUD render target was unavailable");
        const auto name = prefix + "hud-" + std::to_string(width) + "x" + std::to_string(height) + "-phase" + std::to_string(phase)
                          + (position == 0 ? "" : "-position" + std::to_string(position)) + (lap == 12 ? "" : "-lap" + std::to_string(lap))
                          + (rpm == 7000 ? "" : "-rpm" + std::to_string(rpm)) + (lap_time == 61'239 ? "" : "-time" + std::to_string(lap_time))
                          + (split.empty()           ? ""
                              : split.front() == '-' ? "-split-faster"
                                                     : "-split-slower")
                          + ".png";
        renderer.save_screenshot(name);
        Image image{width, height, renderer.readback()};
        expect(image.pixels.size() == std::size_t(width * height), "HUD readback has the wrong drawable dimensions");
        return image;
    };

    // Equal scale and integral offsets make every original texel comparable,
    // including transparency, glyph spacing and the red/green lamp rows.
    for (const auto phase : {2u, 3u}) {
        const auto original = render(640, 480, phase);
        const auto wide = render(854, 480, phase);
        anchored_pixels(original, wide);
    }
    render(960, 540, 2);
    render(960, 600, 3);
    const auto position_original = render(640, 480, 4, 16), position_wide = render(854, 480, 4, 16);
    equal_region(position_original, position_wide, 470, 0, 170, 45, 214, "Original POS artwork and real rank must retain their top-right placement");

    const auto split_original = render(640, 480, 4, 16, 2, 7000, 61'239, "+00:01.23");
    const auto split_wide = render(854, 480, 4, 16, 2, 7000, 61'239, "+00:01.23");
    equal_region(split_original, split_wide, 208, 60, 252, 16, 107, "Checkpoint label and signed split must retain exact pixels at the top-center anchor");
    gpu::Frame empty;
    hud.append(empty, 0, 480);
    hud.append(empty, 640, 0);
    expect(empty.vertices.empty() && empty.indices.empty() && empty.draws.empty(), "Zero-sized HUD target must not append geometry");
    gpu::Frame odd;
    hud.append(odd, 853, 479);
    valid_geometry(odd, 853, 479);
    start_closeup(render(1920, 1080, 2, 16, 1), "original-start-red-lap1-closeup.png");
    start_closeup(render(1920, 1080, 3, 16, 1), "original-start-green-lap1-closeup.png");
    active = &mega;
    prefix = "mega-";
    const auto housing = render(640, 480, 1);
    anchored_pixels(housing, render(854, 480, 1), true);
    for (const auto phase : {2u, 3u, 4u}) {
        const auto original = render(640, 480, phase);
        const auto wide = render(854, 480, phase);
        // Compare complete original anchor groups, including the unchanged
        // housing/lamp shape and repeated support, with replacement artwork.
        if (phase < 4) {
            anchored_pixels(original, wide, true);
        } else {
            equal_region(original, wide, 445, 420, 195, 60, 214, "Replacement gauges must retain their original bottom-right anchor after start");
        }
        if (phase == 2 || phase == 3) {
            aligned_start_lamps(housing, original, phase);
        }
    }
    render(960, 540, 2, 16);
    render(960, 600, 3, 16);
    render(1920, 1080, 4, 16);
    render(960, 540, 4, 16, 2, 7000, 61'239, "-00:01.23");
    const auto housing_1080p = render(1920, 1080, 1, 16, 1);
    const auto red_lap1 = render(1920, 1080, 2, 16, 1);
    aligned_start_lamps(housing_1080p, red_lap1, 2);
    clean_lap_one_top(red_lap1);
    start_closeup(red_lap1, "mega-start-red-lap1-closeup.png");
    const auto green_lap1 = render(1920, 1080, 3, 16, 1);
    aligned_start_lamps(housing_1080p, green_lap1, 3);
    clean_lap_one_top(green_lap1);
    start_closeup(green_lap1, "mega-start-green-lap1-closeup.png");
    // English's MpH descender and the single1's neighboring atlas boundary
    // need a review capture in addition to the ordinary12/KpH fixture.
    scene::Hud english(store, renderer, 0, true, artwork_directory);
    scene::Hud english_original(store, renderer, 0);
    for (int tick = 0; tick < 16; ++tick) {
        english.update(172, 7000, 4);
        english_original.update(172, 7000, 4);
    }
    active = &english;
    reference_hud = &english_original;
    prefix = "mega-english-";
    render(960, 540, 4, 16, 1);
    for (const auto rpm : {2000, 6000, 8500}) {
        const auto image = render(1920, 1080, 4, 16, 1, rpm);
        const auto name = "mega-gauge-rpm" + std::to_string(rpm) + "-closeup.png";
        gauge_closeup(image, name.c_str());
        expect(active->values().rpm_segments() == (rpm == 2000 ? 0u : rpm == 6000 ? 7u : 12u), "Gauge review must exercise idle, partial and full original RPM states");
    }
    // Reproduce the reported 01:40.00 time alongside unknown-best punctuation.
    render(1920, 1080, 4, 16, 1, 6000, 100'000);
    active = &hud;
    reference_hud = &hud;
    prefix.clear();
    render(960, 540, 2, 16);
    render(1920, 1080, 4, 16);
    render(960, 540, 4, 16, 2, 7000, 61'239, "-00:01.23");
    const auto rpm = mega.values().rpm();
    const auto session = mega.values().session();
    const auto start_phase = mega.values().start_phase();
    gpu::Frame repeated;
    mega.append(repeated, 1920, 1080);
    expect(mega.values().rpm() == rpm && mega.values().session().lap_time_ms == session.lap_time_ms && mega.values().start_phase() == start_phase,
        "Replacement artwork must not advance HUD smoothing, timing or start state");
    expect(std::ranges::all_of(repeated.draws,
               [](const auto& draw) {
                   return draw.state.blend && draw.state.linear && draw.state.clamp && !draw.state.key;
               }),
        "Replacement HUD textures must filter authored artwork with transparent edges");
    std::cout << "PASS: canonical HUD pixels retain left/center/right anchors, red/green lamps and repeated support at 4:3/16:9/16:10\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("Usage: toca-hud-layout-tests CANONICAL_ROOT ARTWORK_DIRECTORY");
        }
        reference(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
