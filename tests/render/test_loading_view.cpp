#include "assets/store.hpp"
#include "image/artwork.hpp"
#include "image/decode.hpp"
#include "scene/loading.hpp"

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

Image source_image(SDL_Surface* bitmap) {
    toca::Surface source(SDL_ConvertSurface(bitmap, SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
    expect(bool(source) && source->w == 640 && source->h == 480, "Canonical loading image dimensions changed");
    Image image{640, 480, std::vector<Uint32>(640 * 480)};
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 640; ++x) {
            Uint32 color;
            std::memcpy(&color, static_cast<const Uint8*>(source->pixels) + y * source->pitch + x * 4, 4);
            image.pixels[std::size_t(y * 640 + x)] = color | 0xff000000u;
        }
    }
    return image;
}

void original_center(const Image& image, const Image& source, std::uint32_t completed) {
    const auto left = (image.width - 640) / 2;
    // 0043a050: x32/y289, width257*completed/total, height15.
    const auto progress_right = 32 + 257 * int(completed) / 2;
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 640; ++x) {
            const auto expected = x >= 32 && x < progress_right && y >= 289 && y < 304 ? 0xffffff00u : source.at(x, y);
            expect(image.at(left + x, y) == expected, "Centered loading image or original progress bar changed");
        }
    }
}

void clean_sides(const Image& image) {
    const auto scale = float(image.height) / 480;
    const auto left = int((float(image.width) - 640 * scale) / 2);
    const auto right = image.width - left;
    std::array<std::size_t, 2> flag{}, separator{};
    for (int y = 0; y < image.height; ++y) {
        const auto source_y = int((float(y) + .5f) / scale);
        for (int side = 0; side < 2; ++side) {
            for (int x = side == 0 ? 0 : right; x < (side == 0 ? left : image.width); ++x) {
                const auto color = image.at(x, y);
                if (source_y < 72) {
                    flag[side] += color != 0xff000000u;
                } else if (source_y >= 222 && source_y < 233) {
                    separator[side] += color != 0xff000000u;
                } else {
                    expect(color == 0xff000000u, "Loading margins repeated track text, progress, map or lower artwork");
                }
            }
        }
    }
    for (int side = 0; side < 2; ++side) {
        expect(flag[side] > 100 && separator[side] > 100, "Clean loading flag and separator did not extend into both margins");
    }
}

void reference_sides(const Image& image, const Image& artwork) {
    const auto margin = (image.width - 640) / 2;
    for (int y = 0; y < 480; ++y) {
        if (y >= 72 && (y < 222 || y >= 233)) {
            continue; // Full black bands are checked by clean_sides.
        }
        for (int distance = 0; distance < margin; ++distance) {
            expect(image.at(image.width - margin + distance, y) == artwork.at(639 - distance, y), "Right margin lost the original clean flag/separator edge");
            if (y < 72 && distance >= 8 && distance < 24) {
                continue; // Small seam blend between authored edge and clean patch.
            }
            // The original top-left logo occupies the repeated source strip.
            // Original x266..349 provides the corresponding clean flag patch.
            const auto source_x = y < 72 && distance >= 24 ? distance + 242 : distance;
            expect(image.at(margin - 1 - distance, y) == artwork.at(source_x, y), "Left margin repeated the logo instead of the original clean flag patch");
        }
    }
}

void statistics_layout(const toca::gpu::Frame& frame, const toca::frontend::Font& font, const std::array<std::string, 8>& lines) {
    constexpr std::array y{307, 321, 345, 360, 383, 398, 422, 436};
    std::size_t vertex = 4;
    for (std::size_t row = 0; row < lines.size(); ++row) {
        float x = float(601 - font.text_width(lines[row]));
        for (const unsigned char character : lines[row]) {
            if (character == ' ' || character == '_' || character == '^') {
                x += font.space_width;
            } else if (const auto* glyph = font.find(character)) {
                expect(vertex + 4 <= frame.vertices.size(), "Loading statistics lost an original character");
                const auto& top_left = frame.vertices[vertex];
                const auto& bottom_right = frame.vertices[vertex + 2];
                expect(
                    top_left.x == x - .5f && top_left.y == float(y[row]) - .5f && bottom_right.x == x + glyph->width - .5f && bottom_right.y == float(y[row] + glyph->height) - .5f,
                    "Loading statistics changed original right alignment, row placement or glyph dimensions");
                expect(top_left.u == float(glyph->x) / float(font.atlas->w) && top_left.v == float(glyph->y) / float(font.atlas->h) && top_left.diffuse == 0xffc8c8c8,
                    "Loading statistics lost original glyph cells or source modulation");
                vertex += 4;
                x += glyph->width + font.advance;
            }
        }
    }
    expect(vertex == frame.vertices.size(), "Loading statistics added unexpected text or decoration");
}

void statistics(const toca::AssetStore& store, toca::gpu::Renderer& renderer, SDL_Window* window) {
    using namespace toca;
    const auto font = frontend::decode_font(store.open_archive("data/loadhigh.bfp").member(0).bytes());
    // Exact CONFIG GSTRINGS77..81, including the Italian trailing space.
    const std::array<std::array<std::string, 5>, 5> labels{{
        {"TRACK LENGTH", "UNLIMITED", "LAPS", "RACE DISTANCE", "LAP RECORD"},
        {"LONGUEUR CIRCUIT", "ILLIMITES", "TOURS", "DISTANCE DE COURSE", "RECORD TOUR"},
        {"STRECKENLAENGE", "UNBEGRENZT", "RUNDEN", "RENNDISTANZ", "RUNDENREKORD"},
        {"LUNGHEZZA PISTA", "ILLIMITATO", "GIRI", "DISTANZA CORSA", "RECORD GIRO "},
        {"LONGITUD DE LA PISTA", "SIN LIMITE", "VUELTAS", "DISTANCIA DE CARRERA", "RECORD DE VUELTAS"},
    }};
    for (std::uint32_t language = 0; language < 5; ++language) {
        const auto& label = labels[language];
        scene::LoadingStatistics setup{.language = language, .laps = 2, .record = {.lap_ms = 599990, .driver = "ANDY", .code = {}}, .ghost = {}};
        const std::array<std::string, 8> race{
            label[0], language == 0 ? "2.51 MILES" : "4.02KM", label[2], "2", label[3], language == 0 ? "5.02 MILES" : "8.04KM", label[4], "ANDY 9:59.99"};
        scene::LoadingView original(store, renderer, 0, false, {}, setup);
        statistics_layout(original.frame(0, 2), font, race);
        setup.laps = 0;
        const std::array<std::string, 8> unlimited{race[0], race[1], race[2], label[1], race[4], label[1], race[6], race[7]};
        scene::LoadingView unlimited_view(store, renderer, 0, false, {}, setup);
        statistics_layout(unlimited_view.frame(0, 2), font, unlimited);
        setup.time_trial = true;
        setup.laps = 7; // Time Trial always displays unlimited attempts.
        auto trial = unlimited;
        trial[6] = "T.T " + label[4];
        scene::LoadingView no_ghost(store, renderer, 0, false, {}, setup);
        statistics_layout(no_ghost.frame(0, 2), font, trial);
        setup.ghost = frontend::LapRecord{.lap_ms = 59567, .driver = "GHOST", .code = {}};
        trial[7] = "GHOST 59.56";
        scene::LoadingView ghost(store, renderer, 0, false, {}, setup);
        statistics_layout(ghost.frame(0, 2), font, trial);
    }
    const auto root = artwork_root();
    // Enabled Mega with no matching files uses the canonical BFP, independently
    // for artwork and fonts. Point at game data as an existing empty UI root.
    scene::LoadingView fallback(store, renderer, 0, true, store.root());
    expect(!fallback.frame(0, 2).draws.front().state.linear, "Missing Mega loading image must use the unchanged BFP fallback");
    scene::LoadingStatistics setup{.language = 2, .laps = 3, .record = {.lap_ms = 599990, .driver = "ANDY", .code = {}}, .ghost = {}};
    constexpr std::array names{"donington", "silverstone", "thruxton", "brands-hatch", "oulton-park", "donington-short", "croft", "knockhill", "snetterton", "lavaland"};
    for (std::uint32_t track = 0; track < names.size(); ++track) {
        const auto path = find_artwork(root, "loading/" + std::string(names[track]) + ".webp", true);
        expect(path.has_value(), "The complete Mega loading artwork package must be installed");
        const auto art = load_artwork_file(*path);
        expect(art->w == 2560 && art->h == 1920, "Mega loading artwork must retain the4:3 source canvas");
        scene::LoadingView loading(store, renderer, track, true, root, setup);
        const auto frame = loading.frame(1, 2);
        expect(frame.draws.front().state.linear && frame.draws[1].state.blend && frame.draws[1].state.linear,
            "Mega loading picture and its separate dynamic bitmapfont must use authored artwork");
        gpu::check(SDL_SetWindowSize(window, 960, 720), "Resize Mega loading target");
        gpu::check(SDL_SyncWindow(window), "Synchronize Mega loading target");
        SDL_PumpEvents();
        expect(renderer.draw(frame), "Mega loading presentation was unavailable");
        renderer.save_screenshot("loading-mega-" + std::string(names[track]) + "-statistics-960x720.png");
        if (track == 0) {
            const std::vector<std::string> status{"NETZWERKRENNEN LADEN", "WARTE AUF :", "FAHRER 2 / FAHRER 3", "ESC : ABBRECHEN"};
            const auto network_frame = loading.frame(1, 2, status);
            for (std::size_t vertex = frame.vertices.size(); vertex < network_frame.vertices.size(); ++vertex) {
                expect(network_frame.vertices[vertex].x < 312, "Network status must leave the original loading statistics readable");
            }
            for (const auto width : {1280, 1152}) {
                gpu::check(SDL_SetWindowSize(window, width, 720), "Resize Mega widescreen loading target");
                gpu::check(SDL_SyncWindow(window), "Synchronize Mega widescreen loading target");
                SDL_PumpEvents();
                expect(renderer.draw(network_frame), "Mega network loading presentation was unavailable");
                renderer.save_screenshot("loading-mega-donington-network-" + std::to_string(width) + "x720.png");
            }
        }
    }
}

void reference(const char* root) {
    using namespace toca;
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private loading view");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA loading layout", 640, 480, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private loading window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(root);
    const auto archive = store.open_archive("data/loadhigh.bfp");

    struct Track {
        std::uint32_t id, member;
        const char* name;
    };

    // Independent selected-image mapping from 00439820; all ten are also
    // compared at640x480 by scene-reference-models.
    for (const auto track : {Track{0, 3, "donington"}, Track{9, 10, "lavaland"}}) {
        const auto bitmap = toca::decode_bmp(archive.member(track.member).bytes());
        const auto artwork = source_image(bitmap.get());
        scene::LoadingView loading(store, renderer, track.id);
        const auto render = [&](const gpu::Frame& frame, int width, int height) {
            gpu::check(SDL_SetWindowSize(window.get(), width, height), "Resize loading target");
            gpu::check(SDL_SyncWindow(window.get()), "Synchronize loading target size");
            SDL_PumpEvents();
            expect(renderer.draw(frame), "Private loading render target was unavailable");
            Image image{width, height, renderer.readback()};
            expect(image.pixels.size() == std::size_t(width * height), "Loading drawable dimensions changed");
            return image;
        };
        for (std::uint32_t progress = 0; progress <= 2; ++progress) {
            const auto frame = loading.frame(progress, 2);
            for (const auto width : {640, 854, 768}) {
                const auto image = render(frame, width, 480);
                original_center(image, artwork, progress);
                if (width > 640) {
                    clean_sides(image);
                    reference_sides(image, artwork);
                }
            }
        }
        const std::vector<std::string> status{"LOADING NETWORK RACE", "WAITING FOR :", "DRIVER 2 / DRIVER 3", "ESC : CANCEL"};
        const auto plain = render(loading.frame(2, 2), 640, 480);
        const auto network_frame = loading.frame(2, 2, status);
        const auto network = render(network_frame, 640, 480);
        std::size_t changed = 0;
        for (int y = 0; y < 480; ++y) {
            for (int x = 0; x < 640; ++x) {
                if (y < 318) {
                    expect(network.at(x, y) == plain.at(x, y), "Linkup status must preserve original loading artwork and actual progress above its panel");
                } else {
                    changed += network.at(x, y) != plain.at(x, y);
                }
            }
        }
        expect(changed > 100, "Linkup loading status must render readable waiting names and cancellation guidance");
        for (const auto& vertex : network_frame.vertices) {
            expect(vertex.x >= -.5f && vertex.x < 640 && vertex.y >= -.5f && vertex.y < 480, "Network loading status must stay inside the authored canvas");
        }
        clean_sides(render(network_frame, 854, 480));
        // A current frame remains usable after resizing; no reconstruction or
        // invented loading stage is needed when display settings change.
        const auto frame = loading.frame(1, 2);
        for (const auto height : {540, 600}) {
            clean_sides(render(frame, 960, height));
            renderer.save_screenshot(std::string("loading-") + track.name + "-960x" + std::to_string(height) + ".png");
        }
    }
    statistics(store, renderer, window.get());
    std::cout << "PASS: canonical loading artwork and staged progress retain their centered pixels; clean flag/black margins at4:3/16:9/16:10 and resize; source-localized "
                 "statistics, saved ghost and complete Mega package\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Usage: toca-loading-view-tests CANONICAL_ROOT");
        }
        reference(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
