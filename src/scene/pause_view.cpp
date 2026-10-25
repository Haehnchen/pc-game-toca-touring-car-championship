#include "scene/pause_view.hpp"

#include "assets/store.hpp"
#include "scene/presentation_tables.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::scene {
namespace {
int text_width(const frontend::Font& font, std::string_view text) {
    int width = 0;
    std::uint8_t previous = 0;
    for (const auto character : text) {
        const auto current = std::uint8_t(character);
        if (character == ' ' || character == '_' || character == '^') {
            width += font.space_width;
        } else if (const auto* glyph = font.find(current)) {
            width += glyph->width + font.advance - font.pair_spacing(current, previous);
        }
        previous = current;
    }
    return width;
}

int pixel(int fixed, int extent) {
    return int((std::int64_t(fixed) * extent) >> 16);
}

std::vector<std::string_view> wrap(const frontend::Font& font, std::string_view text) {
    std::vector<std::string_view> result;
    while (text_width(font, text) >= 640) {
        std::size_t split = 0;
        for (auto space = text.find(' '); space != std::string_view::npos; space = text.find(' ', space + 1)) {
            if (text_width(font, text.substr(0, space)) >= 640) {
                break;
            }
            split = space;
        }
        if (split == 0) {
            throw std::runtime_error("Original pause word exceeds the authored canvas");
        }
        result.push_back(text.substr(0, split));
        text.remove_prefix(split + 1);
    }
    result.push_back(text);
    return result;
}
} // namespace

PauseView::PauseView(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t language, bool mega, const std::filesystem::path& artwork_directory)
    : labels_(load_pause_labels(store, language)), mega_(mega) {
    const auto images = store.open_archive("data/image640.bfp");
    title_font_ = frontend::decode_font(images.member(23).bytes());
    row_font_ = frontend::decode_font(images.member(24).bytes());
    if (mega_) {
        const auto artwork = frontend::load_big_font_artwork(artwork_directory);
        title_texture_ = renderer.upload(artwork.get());
        const auto rows = frontend::load_small_font_artwork(artwork_directory);
        row_texture_ = renderer.upload(rows.get());
    } else {
        title_texture_ = renderer.upload(title_font_.atlas.get(), true);
        row_texture_ = renderer.upload(row_font_.atlas.get(), true);
    }
    panel_color_ = presentation_tables::result_panel_color.argb();
}

void PauseView::append_network_notice(gpu::Frame& frame, std::uint32_t width, std::uint32_t height, std::string_view notice) const {
    if (notice.empty()) {
        return;
    }
    const float scale = std::min(float(width) / 640, float(height) / 480);
    const float left = (float(width) - 640 * scale) / 2, top = (float(height) - 480 * scale) / 2;
    std::string display(notice);
    for (auto& character : display) {
        if (character >= 'a' && character <= 'z') {
            character = char(character - 'a' + 'A');
        }
    }
    const float text_scale = std::min(1.0f, 14.0f / float(std::max(row_font_.line_height, 1)));
    std::vector<std::string_view> lines;
    notice = display;
    while (!notice.empty() && lines.size() < 3) {
        auto end = notice.size();
        while (end > 1 && text_width(row_font_, notice.substr(0, end)) * text_scale > 592) {
            --end;
        }
        if (end < notice.size()) {
            const auto space = notice.rfind(' ', end);
            if (space != std::string_view::npos && space > 0) {
                end = space;
            }
        }
        lines.push_back(notice.substr(0, end));
        notice.remove_prefix(end);
        while (!notice.empty() && notice.front() == ' ') {
            notice.remove_prefix(1);
        }
    }
    const auto step = int(row_font_.line_height * text_scale) + 3;
    frame.quad({.blend = true}, left + 16 * scale, top + 58 * scale, 608 * scale, float(int(lines.size()) * step + 12) * scale, panel_color_);
    for (std::size_t row = 0; row < lines.size(); ++row) {
        float x = 320 - text_width(row_font_, lines[row]) * text_scale / 2;
        std::uint8_t previous = 0;
        for (const unsigned char current : lines[row]) {
            if (current == ' ' || current == '_' || current == '^') {
                x += row_font_.space_width * text_scale;
            } else if (const auto* glyph = row_font_.find(current)) {
                x -= row_font_.pair_spacing(current, previous) * text_scale;
                const auto first = frame.vertices.size();
                frame.quad({.texture = row_texture_.get(), .blend = mega_, .key = !mega_, .linear = mega_, .clamp = mega_}, left + x * scale,
                    top + float(64 + int(row) * step) * scale, glyph->width * scale * text_scale, glyph->height * scale * text_scale, 0xffc8c8c8);
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    auto& vertex = frame.vertices[first + corner];
                    vertex.u = (float(glyph->x) + vertex.u * glyph->width) / float(row_font_.atlas->w);
                    vertex.v = (float(glyph->y) + vertex.v * glyph->height) / float(row_font_.atlas->h);
                }
                x += (glyph->width + row_font_.advance) * text_scale;
            }
            previous = current;
        }
    }
}

void PauseView::append(
    gpu::Frame& frame, const PauseController& controller, std::uint32_t width, std::uint32_t height, std::string_view notice, std::string_view network_notice) const {
    if (width == 0 || height == 0) {
        return;
    }
    if (!controller.active()) {
        append_network_notice(frame, width, height, network_notice);
        return;
    }
    const auto menu = pause_menu(labels_, controller);
    const float scale = std::min(float(width) / 640, float(height) / 480);
    const float left = (float(width) - 640 * scale) / 2, top = (float(height) - 480 * scale) / 2;
    const auto title_lines = wrap(title_font_, menu.title);
    // 440510 single-player normal640 layout: title at.3, rows separated by.06.
    const auto title_y = pixel(0x4ccc, 480);
    const auto title_end = ((title_y + int(title_lines.size()) * (title_font_.line_height + 3)) << 16) / 480;
    const auto row_origin = title_end - 0xa3d;
    std::vector<std::vector<std::string_view>> row_lines;
    int widest = 0;
    for (const auto line : title_lines) {
        widest = std::max(widest, text_width(title_font_, line));
    }
    for (const auto& row : menu.rows) {
        row_lines.push_back(wrap(row_font_, row));
        for (const auto line : row_lines.back()) {
            widest = std::max(widest, text_width(row_font_, line));
        }
    }
    const auto last_row_y = row_origin + int(menu.rows.size()) * 0xf5c;
    const auto panel_top = title_end - 0xccc + 0xf5c;
    const auto panel_height = last_row_y - panel_top + 0x147a;
    // 440510 converts its even-width pixel rectangle to Q16, caps it at .9,
    // then46557f converts back. 49d0c0's4x4 panel tiles truncate each quarter
    // independently, so the final panel can lose up to three more pixels.
    const auto margin = (640 - 40 - widest) / 2;
    auto panel_left_q16 = margin * 65536 / 640;
    auto panel_width_q16 = (640 - margin * 2) * 65536 / 640;
    if (panel_width_q16 > 0xe666) {
        panel_left_q16 += (panel_width_q16 - 0xe666) / 2;
        panel_width_q16 = 0xe666;
    }
    const auto panel_left = pixel(panel_left_q16, 640);
    const auto panel_width = (pixel(panel_width_q16, 640) / 4) * 4;
    const auto panel_height_pixels = (pixel(panel_height, 480) / 4) * 4;
    const auto quad = [&](gpu::State state, float x, float y, float w, float h, std::uint32_t color) {
        frame.quad(state, left + x * scale, top + y * scale, w * scale, h * scale, color);
    };
    quad({.blend = true}, float(panel_left), float(pixel(panel_top, 480)), float(panel_width), float(panel_height_pixels), panel_color_);
    const auto text = [&](const frontend::Font& font, SDL_GPUTexture* texture, std::string_view value, int y, bool selected) {
        int x = 320 - text_width(font, value) / 2;
        std::uint8_t previous = 0;
        for (const auto character : value) {
            const auto current = std::uint8_t(character);
            if (character == ' ' || character == '_' || character == '^') {
                x += font.space_width;
            } else if (const auto* glyph = font.find(current)) {
                x -= font.pair_spacing(current, previous);
                const auto first = frame.vertices.size();
                // 4652ae selects RGB200 for active/title text, RGB50 otherwise.
                const bool smooth = mega_;
                quad({.texture = texture, .blend = smooth, .key = !smooth, .linear = smooth, .clamp = smooth}, float(x), float(y), float(glyph->width), float(glyph->height),
                    selected ? 0xffc8c8c8 : 0xff323232);
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    auto& vertex = frame.vertices[first + corner];
                    vertex.u = (float(glyph->x) + vertex.u * glyph->width) / float(font.atlas->w);
                    vertex.v = (float(glyph->y) + vertex.v * glyph->height) / float(font.atlas->h);
                }
                x += glyph->width + font.advance;
            }
            previous = current;
        }
    };
    for (std::size_t index = 0; index < title_lines.size(); ++index) {
        text(title_font_, title_texture_.get(), title_lines[index], title_y + int(index) * (title_font_.line_height + 3), true);
    }
    for (std::size_t row = 0; row < row_lines.size(); ++row) {
        const auto y = pixel(row_origin + int(row + 1) * 0xf5c, 480);
        for (std::size_t line = 0; line < row_lines[row].size(); ++line) {
            text(row_font_, row_texture_.get(), row_lines[row][line], y + int(line) * (row_font_.line_height + 3), row == controller.selection());
        }
    }
    if (!notice.empty()) {
        // Native persistence errors remain visible while the actual run is
        // retained. Bound path/error text independently of original menu words.
        std::string notice_text(notice);
        for (auto& character : notice_text) {
            if (character >= 'a' && character <= 'z') {
                character = char(character - 'a' + 'A');
            }
        }
        notice = notice_text;
        std::vector<std::string_view> lines;
        while (!notice.empty() && lines.size() < 3) {
            auto end = notice.size();
            while (end > 1 && text_width(row_font_, notice.substr(0, end)) > 592) {
                --end;
            }
            if (end < notice.size()) {
                const auto space = notice.rfind(' ', end);
                if (space != std::string_view::npos && space > 0) {
                    end = space;
                }
            }
            lines.push_back(notice.substr(0, end));
            notice.remove_prefix(end);
            while (!notice.empty() && notice.front() == ' ') {
                notice.remove_prefix(1);
            }
        }
        const auto step = row_font_.line_height + 3;
        const auto y = 472 - int(lines.size()) * step;
        quad({.blend = true}, 16, float(y - 6), 608, float(int(lines.size()) * step + 12), panel_color_);
        for (std::size_t index = 0; index < lines.size(); ++index) {
            text(row_font_, row_texture_.get(), lines[index], y + int(index) * step, true);
        }
    }
    append_network_notice(frame, width, height, network_notice);
}
} // namespace toca::scene
