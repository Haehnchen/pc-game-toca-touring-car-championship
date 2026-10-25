#include "scene/loading.hpp"

#include "frontend/builtin_data.hpp"
#include "image/artwork.hpp"
#include "image/decode.hpp"
#include "scene/catalog.hpp"

#include <cstdio>

namespace toca::scene {
namespace {
gpu::Surface loading_backdrop(SDL_Surface* artwork) {
    gpu::Surface source(SDL_ConvertSurface(artwork, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    const int scale = artwork->w / 640;
    gpu::Surface backdrop(SDL_CreateSurface(640 * scale, 480 * scale, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    gpu::check(source && backdrop, "Create loading side backdrop");
    gpu::check(SDL_FillSurfaceRect(backdrop.get(), nullptr, SDL_MapSurfaceRGB(backdrop.get(), 0, 0, 0)), "Clear loading side backdrop");
    // Only the shared flag and fire separator are decoration. Titles, the TOCA
    // logo, track names, progress and miniatures must never enter the margins.
    for (const SDL_Rect band : {SDL_Rect{0, 0, 640 * scale, 72 * scale}, SDL_Rect{0, 222 * scale, 640 * scale, 11 * scale}}) {
        SDL_Rect destination = band;
        gpu::check(SDL_BlitSurface(source.get(), &band, backdrop.get(), &destination), "Copy loading decoration");
    }
    // The left logo starts at x25. A clean flag repeat at x242 has matching
    // checker geometry; retain the edge and blend over sixteen clean columns.
    // Both supported widescreen aspects sample less than 107 columns per side.
    for (int y = 0; y < 72 * scale; ++y) {
        const auto* original = static_cast<const Uint8*>(source->pixels) + y * source->pitch;
        auto* destination = static_cast<Uint8*>(backdrop->pixels) + y * backdrop->pitch;
        for (int x = 8 * scale; x < 160 * scale; ++x) {
            const auto weight = std::min(x - 8 * scale, 16 * scale);
            for (int channel = 0; channel < 3; ++channel) {
                destination[x * 4 + channel] =
                    Uint8((original[x * 4 + channel] * (16 * scale - weight) + original[(x + 242 * scale) * 4 + channel] * weight + 8 * scale) / (16 * scale));
            }
        }
    }
    return backdrop;
}

std::array<std::string, 8> statistics_text(const AssetStore& store, std::uint32_t track_id, const LoadingStatistics& statistics) {
    if (statistics.language > 4) {
        throw std::runtime_error("Loading statistics require original language0..4");
    }
    // 42bee0's GSTRINGS table:4d526c..4d527c are lines77..81.
    const auto config = store.open_archive("data/config.bfp");
    const auto strings = config.member(2 + statistics.language).bytes();
    std::array<std::string, 5> labels;
    std::size_t begin = 0;
    for (std::size_t line = 0; line <= 81; ++line) {
        auto end = begin;
        while (end < strings.size() && strings[end] != '\r' && strings[end] != '\n') {
            ++end;
        }
        if (end == begin || end == strings.size()) {
            throw std::runtime_error("Loading statistics are missing from original GSTRINGS");
        }
        if (line >= 77) {
            labels[line - 77].assign(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
        }
        begin = end;
        while (begin < strings.size() && (strings[begin] == '\r' || strings[begin] == '\n')) {
            ++begin;
        }
    }
    // 43a290 uses the original1600metres/mile conversion, then drops
    // sub-hundredths before multiplying by laps for the race distance.
    auto metres = frontend::builtin::tracks().at(track_id).metres;
    if (statistics.language == 0) {
        metres = metres * 1000 / 1600;
    }
    const auto hundredths = metres / 10;
    const auto distance = [&](std::uint32_t value) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), statistics.language == 0 ? "%u.%02u MILES" : "%u.%02uKM", value / 100, value % 100);
        return std::string(buffer);
    };
    const auto& record = statistics.time_trial && statistics.ghost ? *statistics.ghost : statistics.record;
    // 439820 uses seconds.hundredths below a minute and m:ss.hh otherwise.
    const auto milliseconds = std::min(record.lap_ms, 3'599'999u);
    char time[32];
    if (milliseconds < 60'000) {
        std::snprintf(time, sizeof(time), "%u.%02u", milliseconds / 1000, milliseconds % 1000 / 10);
    } else {
        std::snprintf(time, sizeof(time), "%u:%02u.%02u", milliseconds / 60'000, milliseconds / 1000 % 60, milliseconds % 1000 / 10);
    }
    const auto laps = statistics.time_trial ? 0 : statistics.laps;
    return {labels[0], distance(hundredths), labels[2], laps == 0 ? labels[1] : std::to_string(laps), labels[3], laps == 0 ? labels[1] : distance(hundredths * laps),
        (statistics.time_trial ? "T.T " : "") + labels[4], record.driver + " " + time};
}

void text(gpu::Frame& frame, const frontend::Font& font, SDL_GPUTexture* texture, bool mega, std::string_view line, float x, float y, float scale, std::uint32_t color, bool kern) {
    std::uint8_t previous = 0;
    for (const unsigned char current : line) {
        if (current == ' ' || current == '_' || current == '^') {
            x += font.space_width * scale;
        } else if (const auto* glyph = font.find(current)) {
            if (kern) {
                x -= font.pair_spacing(current, previous) * scale;
            }
            const auto first = frame.vertices.size();
            frame.quad({.texture = texture, .blend = mega, .key = !mega, .linear = mega, .clamp = mega}, x, y, glyph->width * scale, glyph->height * scale, color);
            for (std::size_t corner = 0; corner < 4; ++corner) {
                auto& vertex = frame.vertices[first + corner];
                vertex.u = (float(glyph->x) + vertex.u * glyph->width) / float(font.atlas->w);
                vertex.v = (float(glyph->y) + vertex.v * glyph->height) / float(font.atlas->h);
            }
            x += (glyph->width + font.advance) * scale;
        }
        previous = current;
    }
}
} // namespace

LoadingView::LoadingView(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t track_id, bool mega, const std::filesystem::path& artwork_directory,
    const std::optional<LoadingStatistics>& statistics) {
    const auto& content = track_content(track_id);
    const auto archive = store.open_archive("data/loadhigh.bfp");
    const auto root = artwork_directory.empty() && mega ? artwork_root() : artwork_directory;
    constexpr std::array names{"donington", "silverstone", "thruxton", "brands-hatch", "oulton-park", "donington-short", "croft", "knockhill", "snetterton", "lavaland"};
    const auto name = "loading/" + std::string(names.at(track_id)) + ".webp";
    const auto replacement = root.empty() ? std::nullopt : find_artwork(root, name, mega);
    auto bitmap = replacement ? load_artwork_file(*replacement) : decode_bmp(archive.member(content.loading_image).bytes());
    mega_image_ = replacement.has_value() && bitmap->w == 2560 && bitmap->h == 1920;
    if ((!replacement && (bitmap->w != 640 || bitmap->h != 480)) || (replacement && !mega_image_)) {
        throw std::runtime_error("Loading artwork must retain its640x480 canvas at fourfold resolution");
    }
    image_ = renderer.upload(bitmap.get());
    const auto sides = mega_image_ ? find_artwork(root, "loading/sides.webp", mega) : std::nullopt;
    auto backdrop = sides ? load_artwork_file(*sides) : loading_backdrop(bitmap.get());
    if (backdrop->w != bitmap->w || backdrop->h != bitmap->h) {
        throw std::runtime_error("Loading sides must match their artwork dimensions");
    }
    side_backdrop_ = renderer.upload(backdrop.get());
    const auto fonts = store.open_archive("data/image640.bfp");
    status_font_ = frontend::decode_font(fonts.member(24).bytes());
    const auto status = mega && !root.empty() ? find_artwork(root, "hud/caption-small.webp", true) : std::nullopt;
    mega_status_ = status.has_value();
    if (status) {
        const auto atlas = load_artwork_file(*status);
        if (atlas->w != status_font_.atlas->w * 4 || atlas->h != status_font_.atlas->h * 4) {
            throw std::runtime_error("Loading network font must retain its original cells at fourfold resolution");
        }
        status_texture_ = renderer.upload(atlas.get());
    } else {
        status_texture_ = renderer.upload(status_font_.atlas.get(), true);
    }
    if (statistics) {
        show_statistics_ = true;
        statistics_ = statistics_text(store, track_id, *statistics);
        statistics_font_ = frontend::decode_font(archive.member(0).bytes());
        const auto font = mega && !root.empty() ? find_artwork(root, "loading/font.webp", true) : std::nullopt;
        mega_statistics_ = font.has_value();
        if (font) {
            const auto atlas = load_artwork_file(*font);
            if (atlas->w != statistics_font_.atlas->w * 4 || atlas->h != statistics_font_.atlas->h * 4) {
                throw std::runtime_error("Loading statistics font must retain its original cells at fourfold resolution");
            }
            statistics_texture_ = renderer.upload(atlas.get());
        } else {
            statistics_texture_ = renderer.upload(statistics_font_.atlas.get(), true);
        }
    }
}

gpu::Frame LoadingView::frame(std::uint32_t completed, std::uint32_t total, std::span<const std::string> status) const {
    if (total == 0 || completed > total) {
        throw std::logic_error("Loading progress requires completed resource stages");
    }
    gpu::Frame result;
    result.side_backdrop = side_backdrop_.get();
    result.quad({.texture = image_.get(), .linear = mega_image_, .clamp = mega_image_}, 0, 0, 640, 480);
    if (show_statistics_) {
        constexpr std::array positions{0xa3d7, 0xab85, 0xb851, 0xc000, 0xcccc, 0xd47a, 0xe147, 0xe8f5};
        for (std::size_t row = 0; row < statistics_.size(); ++row) {
            const auto& line = statistics_[row];
            const auto width = statistics_font_.text_width(line);
            //43a300 right-aligns using normalized fixed-point subtraction;
            //64f58 floors the resulting position separately from the width.
            const auto normalized_x = 0xf0a3 - width * 65536 / 640;
            const auto x = float(normalized_x * 640 >> 16);
            const auto y = float(positions[row] * 480 >> 16);
            text(result, statistics_font_, statistics_texture_.get(), mega_statistics_, line, x, y, 1, 0xffc8c8c8, false);
        }
    }
    // 0043a050's original bar; actual native stages, no invented loading timer.
    if (completed != 0) {
        result.quad({}, 32, 289, float(257u * completed / total), 15, 0xffffff00);
    }
    if (!status.empty()) {
        // Native Linkup diagnostics supplement 439820 artwork; the original
        // all-active-human barrier is 43a820. Offline frames remain identical.
        const auto rows = std::min<std::size_t>(status.size(), 8);
        const float scale = std::min(1.0f, 14.0f / float(std::max(status_font_.line_height, 1)));
        const float panel_width = show_statistics_ ? 288 : 592;
        result.quad({.blend = true}, 24, 318, panel_width, float(rows * 18 + 12), 0xd0000000);
        for (std::size_t row = 0; row < rows; ++row) {
            const auto& line = status[row];
            const auto natural_width = float(status_font_.text_width(line));
            const auto fit = std::min(scale, (panel_width - 24) / std::max(natural_width, 1.0f));
            const auto x = 24 + (panel_width - natural_width * fit) / 2;
            text(result, status_font_, status_texture_.get(), mega_status_, line, x, 324 + float(row * 18), fit, 0xffeeeeee, true);
        }
    }
    return result;
}
} // namespace toca::scene
