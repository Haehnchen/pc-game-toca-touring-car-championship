#include "scene/qualifying_view.hpp"

#include "assets/store.hpp"
#include "image/decode.hpp"
#include "scene/hud.hpp"
#include "scene/presentation_tables.hpp"

#include <cstdio>

namespace toca::scene {
namespace {
// 4652ae modulates ordinary (-1) BIG/LIL glyphs by RGB200, alpha255.
constexpr std::uint32_t glyph_color = 0xffc8c8c8;

std::uint8_t display_character(char character) {
    return std::uint8_t(character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character);
}

int text_width(const frontend::Font& font, std::string_view text, int tracking = 0) {
    int width = 0;
    std::uint8_t previous = 0;
    for (const auto character : text) {
        const auto current = display_character(character);
        if (character == ' ' || character == '_' || character == '^') {
            width += font.space_width + tracking;
        } else if (const auto* glyph = font.find(current)) {
            width += glyph->width + font.advance + tracking - font.pair_spacing(current, previous);
        }
        previous = current;
    }
    return width;
}

int title_tracking(const frontend::Font& font, std::string_view text) {
    // Native one-line fit, as in464e4d: preserve all16 result rows rather than
    // let464935's title word-wrap push the final rows beyond the640 canvas.
    int tracking = 0;
    while (text_width(font, text, tracking) >= 640 && tracking > -32) {
        --tracking;
    }
    if (text_width(font, text, tracking) >= 640) {
        throw std::runtime_error("Qualifying title cannot fit the authored canvas");
    }
    return tracking;
}
} // namespace

QualifyingView::QualifyingView(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t language, bool mega, const std::filesystem::path& artwork_directory) : mega_(mega) {
    if (language > 4) {
        throw std::runtime_error("Qualifying view requires original language0..4");
    }
    const auto images = store.open_archive("data/image640.bfp");
    title_font_ = frontend::decode_font(images.member(23).bytes());
    row_font_ = frontend::decode_font(images.member(24).bytes());
    if (mega_) {
        const auto artwork = frontend::load_big_font_artwork(artwork_directory);
        // Keep the original font's glyph rectangles, pair spacing and wrapping.
        // The authored atlas has the same normalized cells and its own alpha.
        title_texture_ = renderer.upload(artwork.get());
        const auto rows = frontend::load_small_font_artwork(artwork_directory);
        row_texture_ = renderer.upload(rows.get());
    } else {
        title_texture_ = renderer.upload(title_font_.atlas.get(), true);
        row_texture_ = renderer.upload(row_font_.atlas.get(), true);
    }
    icon_atlas_ = decode_bmp(images.member(language == 0 ? 33 : 32).bytes());
    icon_texture_ = renderer.upload(icon_atlas_.get(), true);
    for (std::size_t car = 0; car < presentation_tables::qualifying_car_icons.size(); ++car) {
        auto& rect = icon_rects_[car];
        const auto& source = presentation_tables::qualifying_car_icons[car];
        rect = {source.x, source.y, source.width, source.height};
        if (rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0 || rect[0] > icon_atlas_->w - rect[2] || rect[1] > icon_atlas_->h - rect[3]) {
            throw std::runtime_error("Qualifying car icon exceeds its original atlas");
        }
    }
    panel_color_ = presentation_tables::result_panel_color.argb();
    // 42bee0 splits CONFIG GSTRINGS at runs of CR/LF; 4d520c is line53.
    const auto config = store.open_archive("data/config.bfp");
    const auto strings_member = config.member(2 + language);
    const auto strings = strings_member.bytes();
    std::size_t begin = 0;
    for (std::uint32_t line = 0; line <= 56; ++line) {
        auto end = begin;
        while (end < strings.size() && strings[end] != '\r' && strings[end] != '\n') {
            ++end;
        }
        if (end == begin || end == strings.size()) {
            throw std::runtime_error("Qualifying title is missing from original GSTRINGS");
        }
        if (line >= 14 && line <= 17) {
            race_finish_titles_[line - 14].assign(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
        } else if (line == 3) {
            pit_title_.assign(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
        } else if (line == 51) {
            pit_exit_title_.assign(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
        } else if (line == 50) {
            rollout_title_.assign(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
        } else if (line == 52) {
            finish_title_.assign(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
        } else if (line == 53) {
            title_.assign(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
        } else if (line == 56) {
            race_title_.assign(strings.begin() + std::ptrdiff_t(begin), strings.begin() + std::ptrdiff_t(end));
        }
        begin = end;
        while (begin < strings.size() && (strings[begin] == '\r' || strings[begin] == '\n')) {
            ++begin;
        }
    }
}

void QualifyingView::append_finish(gpu::Frame& frame, std::uint32_t width, std::uint32_t height) const {
    append_caption(frame, finish_title_, false, width, height);
}

void QualifyingView::append_race_finish(gpu::Frame& frame, std::uint32_t position, std::uint32_t width, std::uint32_t height) const {
    if (position < 1 || position > 16) {
        throw std::runtime_error("Race finish caption requires actual position1..16");
    }
    const auto& format = race_finish_titles_[std::min(position - 1, 3u)];
    if (position <= 3) {
        append_caption(frame, format, false, width, height);
    } else {
        //41eea0: original fourth caption supplies the integer finish rank.
        const auto token = format.find("%d");
        if (token == std::string::npos) {
            throw std::runtime_error("Original race finish caption lacks its rank placeholder");
        }
        const auto text = format.substr(0, token) + std::to_string(position) + format.substr(token + 2);
        append_caption(frame, text, false, width, height);
    }
}

void QualifyingView::append_race(gpu::Frame& frame, const race::RaceResults& results, std::uint32_t width, std::uint32_t height) const {
    QualifyingResults rows;
    rows.local_actor = results.local_actor;
    rows.local_rank = results.local_rank;
    for (std::size_t rank = 0; rank < results.rows.size(); ++rank) {
        const auto& row = results.rows[rank];
        rows.rows[rank] = {row.actor_id, row.car_id, row.time_ms, row.driver};
    }
    append_table(frame, rows, true, width, height, results.count);
}

void QualifyingView::append_pit(gpu::Frame& frame, bool exiting, std::uint64_t active_ms, std::uint32_t width, std::uint32_t height) const {
    // 41ed00: original GSTRINGS3, then51 strictly after PITS count-8.
    // 41eca0/41eec0 flash the entry caption for660ms each second. Exit sets
    // c3b68 and keeps its control-return hint visible throughout that stage.
    if (!exiting && active_ms % 1000 >= 660) {
        return;
    }
    append_caption(frame, exiting ? pit_exit_title_ : pit_title_, true, width, height);
}

void QualifyingView::append_rollout(
    gpu::Frame& frame, GameMode mode, std::uint32_t next_point, std::uint32_t point_count, std::uint64_t active_ms, std::uint32_t width, std::uint32_t height) const {
    if (qualifying_rollout_notice_visible(next_point, point_count, active_ms)) {
        append_rollout_caption(frame, mode, width, height);
    }
}

void QualifyingView::append_rollout_caption(gpu::Frame& frame, GameMode mode, std::uint32_t width, std::uint32_t height) const {
    // 45eb60: Time Trial selects GSTRINGS51 (GET READY), also used by
    // the pit exit, instead of qualifying's GSTRINGS50.
    append_caption(frame, mode == GameMode::time_trial ? pit_exit_title_ : rollout_title_, true, width, height);
}

void QualifyingView::append_caption(gpu::Frame& frame, std::string_view caption, bool wrap, std::uint32_t width, std::uint32_t height) const {
    if (width == 0 || height == 0) {
        return;
    }
    const float scale = std::min(float(width) / 640, float(height) / 480);
    const float left = (float(width) - 640 * scale) / 2, top = (float(height) - 480 * scale) / 2;
    // 464935 wraps at word boundaries against the authored screen width;
    // German's original preparation caption occupies two centered lines.
    std::vector<std::string_view> lines;
    while (wrap && text_width(title_font_, caption) >= 640) {
        std::size_t split = 0;
        for (auto space = caption.find(' '); space != std::string_view::npos; space = caption.find(' ', space + 1)) {
            if (text_width(title_font_, caption.substr(0, space)) >= 640) {
                break;
            }
            split = space;
        }
        if (split == 0) {
            throw std::runtime_error("Original qualifying caption word exceeds its authored canvas");
        }
        lines.push_back(caption.substr(0, split));
        caption.remove_prefix(split + 1);
    }
    lines.push_back(caption);
    int y = (0x370a * 480) >> 16;
    for (const auto line : lines) {
        const auto tracking = wrap ? 0 : title_tracking(title_font_, line);
        auto x = 320 - text_width(title_font_, line, tracking) / 2;
        std::uint8_t previous = 0;
        for (const auto character : line) {
            const auto current = display_character(character);
            if (character == ' ' || character == '_' || character == '^') {
                x += title_font_.space_width + tracking;
            } else if (const auto* glyph = title_font_.find(current)) {
                x -= title_font_.pair_spacing(current, previous);
                const auto first = frame.vertices.size();
                frame.quad({.texture = title_texture_.get(),
                               .blend = mega_,
                               .key = !mega_,
                               .linear = mega_,
                               .clamp = true,
                               .palette = !mega_ && SDL_ISPIXELFORMAT_INDEXED(title_font_.atlas->format)},
                    left + float(x) * scale, top + float(y) * scale, float(glyph->width) * scale, float(glyph->height) * scale, glyph_color);
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    auto& vertex = frame.vertices[first + corner];
                    vertex.u = (float(glyph->x) + vertex.u * glyph->width) / float(title_font_.atlas->w);
                    vertex.v = (float(glyph->y) + vertex.v * glyph->height) / float(title_font_.atlas->h);
                }
                x += glyph->width + title_font_.advance + tracking;
            }
            previous = current;
        }
        y += 3 + title_font_.line_height;
    }
}

void QualifyingView::append(gpu::Frame& frame, const QualifyingResults& results, std::uint32_t width, std::uint32_t height) const {
    append_table(frame, results, false, width, height);
}

void QualifyingView::append_table(gpu::Frame& frame, const QualifyingResults& results, bool race, std::uint32_t width, std::uint32_t height, std::uint32_t count) const {
    if (count == 0 || count > results.rows.size()) {
        throw std::runtime_error("Result table requires 1..16 drivers");
    }
    if (width == 0 || height == 0) {
        return;
    }
    // 45ed30 case8's normal 640 layout (graphics option0: all16 rows).
    const float scale = std::min(float(width) / 640, float(height) / 480);
    const float left = (float(width) - 640 * scale) / 2, top = (float(height) - 480 * scale) / 2;
    const auto pixel = [](int fixed, int extent) {
        return (std::int64_t(fixed) * extent) >> 16;
    };
    const auto title_y = int(pixel(0xf5c, 480));
    auto row_y_q16 = ((title_y + 3 + title_font_.line_height) << 16) / 480 + 0xa3d;
    const auto initial_y_q16 = row_y_q16;
    std::array<int, 16> row_positions{};
    for (auto& y : std::span(row_positions).first(count)) {
        y = int(pixel(row_y_q16, 480));
        row_y_q16 = ((y + 3 + row_font_.line_height) << 16) / 480 - 0x3d7;
    }
    // Original panel is x=.1,width=.8, beginning .02 above the first row.
    const auto panel_y = int(pixel(initial_y_q16 - 0x51e, 480));
    const auto panel_height = int(pixel(row_y_q16 - initial_y_q16 + 0x51e, 480));
    frame.quad(
        {.blend = true}, left + float(pixel(0x1999, 640)) * scale, top + float(panel_y) * scale, float(pixel(0xcccc, 640)) * scale, float(panel_height) * scale, panel_color_);
    const auto quad = [&](SDL_GPUTexture* texture, const SDL_Surface* atlas, int sx, int sy, int w, int h, int x, int y, std::uint32_t color = 0xffffffff) {
        const auto first = frame.vertices.size();
        const bool authored = mega_ && (texture == title_texture_.get() || texture == row_texture_.get());
        frame.quad({.texture = texture, .blend = authored, .key = !authored, .linear = authored, .clamp = true, .palette = !authored && SDL_ISPIXELFORMAT_INDEXED(atlas->format)},
            left + float(x) * scale, top + float(y) * scale, float(w) * scale, float(h) * scale, color);
        for (std::size_t corner = 0; corner < 4; ++corner) {
            auto& vertex = frame.vertices[first + corner];
            vertex.u = (float(sx) + vertex.u * float(w)) / float(atlas->w);
            vertex.v = (float(sy) + vertex.v * float(h)) / float(atlas->h);
        }
    };
    const auto text = [&](const frontend::Font& font, SDL_GPUTexture* texture, std::string_view value, int x, int y, int tracking = 0) {
        std::uint8_t previous = 0;
        for (const auto character : value) {
            // 45ed30 calls426570 (ASCII uppercase) for each driver name.
            const auto current = display_character(character);
            if (character == ' ' || character == '_' || character == '^') {
                x += font.space_width + tracking;
            } else if (const auto* glyph = font.find(current)) {
                x -= font.pair_spacing(current, previous);
                quad(texture, font.atlas.get(), glyph->x, glyph->y, glyph->width, glyph->height, x, y, glyph_color);
                x += glyph->width + font.advance + tracking;
            }
            previous = current;
        }
    };
    const auto& title = race ? race_title_ : title_;
    const auto tracking = title_tracking(title_font_, title);
    text(title_font_, title_texture_.get(), title, 320 - text_width(title_font_, title, tracking) / 2, title_y, tracking);
    for (std::size_t rank = 0; rank < count; ++rank) {
        const auto& row = results.rows[rank];
        if (row.car_id >= icon_rects_.size() || (!race && row.time_ms > 3'600'000) || row.driver.size() > 15) {
            throw std::runtime_error("Qualifying result row exceeds its original record bounds");
        }
        const auto y = row_positions[rank];
        text(row_font_, row_texture_.get(), std::to_string(rank + 1), int(pixel(0x23d7, 640)), y);
        const auto& icon = icon_rects_[row.car_id];
        const auto center_x = int(pixel(0x428f, 640)), center_y = y + int(pixel(0x7ae, 480));
        quad(icon_texture_.get(), icon_atlas_.get(), icon[0], icon[1], icon[2], icon[3], center_x - icon[2] / 2, center_y - icon[3] / 2);
        text(row_font_, row_texture_.get(), row.driver, int(pixel(0x51eb, 640)), y);
        std::array<char, 16> time{};
        const auto milliseconds = race && rank != 0 ? row.time_ms - std::min(row.time_ms, results.rows[0].time_ms) : row.time_ms;
        std::snprintf(time.data(), time.size(), race && rank != 0 ? "+%u:%02u.%02u" : "%u:%02u.%02u", milliseconds / 60000, (milliseconds / 1000) % 60, (milliseconds % 1000) / 10);
        text(row_font_, row_texture_.get(), time.data(), int(pixel(race && rank == 0 ? 0xb332 : 0xae14, 640)), y);
    }
}
} // namespace toca::scene
