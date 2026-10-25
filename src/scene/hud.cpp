#include "scene/hud.hpp"

#include "assets/store.hpp"
#include "image/decode.hpp"
#include "scene/hud_artwork.hpp"
#include "scene/hud_sprites.hpp"

#include <cmath>
#include <cstdio>

namespace toca::scene {
namespace {
void validate(const HudRect& source, const SDL_Surface* atlas) {
    if (source.x < 0 || source.y < 0 || source.width <= 0 || source.height <= 0 || source.x > atlas->w - source.width || source.y > atlas->h - source.height) {
        throw std::runtime_error("Original HUD sprite lies outside its atlas");
    }
}

void validate_session(const HudSession& session) {
    if (session.lap > 99 || (session.mode != GameMode::time_trial && session.lap_limit == 0) || session.lap_limit > 99 || session.position > 16) {
        throw std::runtime_error("HUD requires lap0..99 and lap limit1..99");
    }
    if (session.split_text.size() > 15 || session.split_text.find_first_not_of("0123456789:+-.") != std::string::npos) {
        throw std::runtime_error("HUD split text requires the bounded original time glyphs");
    }
}

void validate_gauges(std::int32_t speed, std::int32_t rpm, std::int32_t gear, std::uint32_t start_phase) {
    if (speed < 0 || gear < 0 || gear > 7 || rpm < 2000 || rpm > 8500 || start_phase > 4) {
        throw std::runtime_error("HUD requires original nonnegative speed, RPM2000..8500, gear0..7 and start phase0..4");
    }
}
} // namespace

HudData load_hud(const AssetStore& store, std::uint32_t language) {
    if (language > 4) {
        throw std::runtime_error("HUD language must be original ID0..4");
    }
    const auto archive = store.open_archive("data/image640.bfp");
    HudData result;
    // 004463e0 consumes23 members, then44f0a3 consumes2 fonts and3 images.
    // 00465631 returns MILES for language0 and KM for the other languages.
    result.kilometres = language != 0;
    result.atlases[0] = decode_bmp(archive.member(result.kilometres ? 32 : 33).bytes());
    result.atlases[1] = decode_bmp(archive.member(36).bytes());
    // 44f0a3: after the two fonts, load the housing, red and green lamps.
    for (std::size_t index = 0; index < 3; ++index) {
        result.atlases[2 + index] = decode_bmp(archive.member(25 + index).bytes());
    }
    validate({0, 0, 128, 64}, result.atlases[2].get());
    validate({0, 64, 77, 64}, result.atlases[2].get());
    for (std::size_t index = 0; index < result.start_lamps.size(); ++index) {
        const auto* atlas = result.atlases[3 + index].get();
        result.start_lamps[index] = {0, 0, atlas->w, atlas->h};
    }
    result.speed_background = hud_sprites::speed_background;
    result.rpm_background = hud_sprites::rpm_background;
    result.lap_background = hud_sprites::lap_background;
    result.timing_labels = hud_sprites::timing_labels;
    result.position_background = hud_sprites::position_background;
    result.split_label = hud_sprites::split_label;
    result.rpm_segments = hud_sprites::rpm_segments;
    for (std::uint32_t index = 0; index < 12; ++index) {
        const auto position = hud_sprites::rpm_positions[index];
        result.rpm_positions[index] = {position.x, position.y, 0, 0};
        const auto large = hud_sprites::large_glyphs[index];
        result.gear_glyphs[index] = large;
        result.gear_glyphs[index].y += hud_sprites::gear_atlas_row_offset;
        if (index < result.lap_glyphs.size()) {
            result.lap_glyphs[index] = large; // Flags3: primary atlas, unchanged row.
            result.position_glyphs[index] = large; // Flags5: secondary atlas, unchanged row.
            validate(large, result.atlases[0].get());
            validate(large, result.atlases[1].get());
        }
        validate(result.rpm_segments[index], result.atlases[0].get());
        validate(result.gear_glyphs[index], result.atlases[1].get());
    }
    for (std::uint32_t index = 0; index < 10; ++index) {
        result.speed_glyphs[index] = hud_sprites::small_glyphs[index];
        validate(result.speed_glyphs[index], result.atlases[0].get());
    }
    result.unknown_time_y = hud_sprites::unknown_time_y;
    for (std::uint32_t index = 0; index < result.time_glyphs.size(); ++index) {
        auto glyph = hud_sprites::small_glyphs[index];
        result.time_glyphs[index] = glyph;
        validate(glyph, result.atlases[0].get());
        glyph.y = result.unknown_time_y;
        validate(glyph, result.atlases[0].get());
    }
    validate(result.speed_background, result.atlases[0].get());
    validate(result.rpm_background, result.atlases[0].get());
    validate(result.lap_background, result.atlases[0].get());
    validate(result.timing_labels, result.atlases[0].get());
    validate(result.position_background, result.atlases[0].get());
    validate(result.split_label, result.atlases[0].get());
    return result;
}

std::string hud_time_text(std::uint32_t milliseconds) {
    const auto time = std::min(milliseconds, 3'599'999u);
    std::array<char, 9> text{};
    std::snprintf(text.data(), text.size(), "%02u:%02u.%02u", time / 60000, (time / 1000) % 60, (time % 1000) / 10);
    return text.data();
}

bool qualifying_rollout_notice_visible(std::uint32_t next_point, std::uint32_t point_count, std::uint64_t active_ms) {
    if (point_count < 18 || next_point >= point_count) {
        throw std::runtime_error("Qualifying caption requires an original POINTS target");
    }
    return next_point >= point_count - 3 ? active_ms % 1000 < 660 : next_point >= 5;
}

void HudValues::set_session(HudSession session) {
    validate_session(session);
    // 44a6f0 clamps the displayed lap, without mutating the actor lap record.
    if (session.mode != GameMode::time_trial) {
        session.lap_limit = session.qualifying ? 3u : session.lap_limit;
        session.lap = std::min(session.lap, session.lap_limit);
    }
    session_ = session;
}

const HudSession& HudValues::session() const {
    return session_;
}

void HudValues::update(std::int32_t speed_integer, std::int32_t rpm, std::int32_t gear, std::uint32_t start_phase) {
    validate_gauges(speed_integer, rpm, gear, start_phase);
    speed_ = std::min(speed_integer, 260);
    gear_ = gear;
    rpm_ += std::clamp(rpm - rpm_, -500, 500); // 0044a6f0, per simulation update.
    start_phase_ = start_phase;
}

void HudValues::import_snapshot(HudSnapshot snapshot) {
    validate_gauges(snapshot.speed_integer, snapshot.rpm, snapshot.gear, snapshot.start_phase);
    validate_session(snapshot.session);
    if (snapshot.speed_integer > 260) {
        throw std::runtime_error("Cached HUD speed must already be clamped to260");
    }
    speed_ = snapshot.speed_integer;
    rpm_ = snapshot.rpm;
    gear_ = snapshot.gear;
    start_phase_ = snapshot.start_phase;
    session_ = std::move(snapshot.session);
}

std::int32_t HudValues::rpm() const {
    return rpm_;
}

std::int32_t HudValues::gear() const {
    return gear_;
}

std::uint32_t HudValues::start_phase() const {
    return start_phase_;
}

std::uint32_t HudValues::speed(bool kilometres) const {
    auto result = std::uint32_t(speed_);
    if (!kilometres) {
        result = result * 100 / 161;
    }
    return result < (kilometres ? 3u : 2u) ? 0u : result;
}

std::uint32_t HudValues::rpm_segments() const {
    if (rpm_ < 2800) {
        return 0;
    }
    return std::min(12u, 1u + std::uint32_t(rpm_ - 2800) / 500u);
}

std::vector<HudBlit> hud_blits(const HudData& data, const HudValues& values) {
    // 0041de90/41e480/41e5f0: only values backed by the current local actor.
    std::vector<HudBlit> result{{0, data.speed_background, 470, 448}, {0, data.rpm_background, 452, 427}};
    for (std::uint32_t index = 0; index < values.rpm_segments(); ++index) {
        result.push_back({0, data.rpm_segments[index], data.rpm_positions[index].x, data.rpm_positions[index].y});
    }
    const auto gear_index = values.gear() == 7 ? 10u : values.gear() == 0 ? 11u : std::uint32_t(values.gear());
    result.push_back({1, data.gear_glyphs[gear_index], 566, 424});
    int x = 560;
    for (const auto digit : std::to_string(values.speed(data.kilometres))) {
        const auto& glyph = data.speed_glyphs[std::size_t(digit - '0')];
        result.push_back({0, glyph, x, 459});
        x += glyph.width + 2;
    }
    {
        const auto& session = values.session();
        if (session.mode != GameMode::time_trial && !session.qualifying && session.position != 0) {
            //41de90:4c33ea places the POS plaque at437,9;4c3022 keeps
            //the large secondary-atlas rank right-aligned at620,5.
            result.push_back({0, data.position_background, 437, 9, 0, 0, HudAnchor::top_right});
            const auto text = std::to_string(session.position);
            int position_x = 620;
            for (const auto digit : text) {
                position_x -= data.position_glyphs[std::size_t(digit - '0')].width;
            }
            for (const auto digit : text) {
                const auto& glyph = data.position_glyphs[std::size_t(digit - '0')];
                result.push_back({1, glyph, position_x, 5, 0, 0, HudAnchor::top_right});
                position_x += glyph.width;
            }
        }
        // 41de90's first single-player blit: combined TIME/BEST artwork.
        result.push_back({0, data.timing_labels, 208, 12, 0, 0, HudAnchor::top_center});
        result.push_back({0, data.lap_background, 11, 9, 0, 0, HudAnchor::top_left});
        const auto lap = std::to_string(session.lap);
        int lap_x = 132;
        for (const auto digit : lap) {
            lap_x -= data.lap_glyphs[std::size_t(digit - '0')].width;
        }
        for (const auto digit : lap) {
            const auto& glyph = data.lap_glyphs[std::size_t(digit - '0')];
            result.push_back({0, glyph, lap_x, 5, 0, 0, HudAnchor::top_left});
            lap_x += glyph.width;
        }
        const auto small_text = [&](std::string_view text, int left, int top, HudAnchor anchor) {
            const bool unknown = text.front() == '-';
            for (const auto character : text) {
                const auto index = character == ':' ? 10u : character == '-' ? 11u : character == '+' ? 12u : character == '.' ? 13u : std::uint32_t(character - '0');
                auto glyph = data.time_glyphs[index];
                if (unknown) {
                    glyph.y = data.unknown_time_y;
                }
                result.push_back({0, glyph, left, top, 0, 0, anchor});
                left += glyph.width + 2;
            }
        };
        // 41de90 suppresses the finite lap limit for mode2.
        if (session.mode != GameMode::time_trial) {
            small_text(std::to_string(session.lap_limit), 146, 27, HudAnchor::top_left);
        }
        small_text(hud_time_text(session.lap_time_ms), 286, 11, HudAnchor::top_center);
        small_text(session.best_lap_ms == 0 ? "--:--.--" : hud_time_text(session.best_lap_ms), 286, 26, HudAnchor::top_center);
        if (session.split_active && !session.split_text.empty()) {
            //41e3d0: label remains visible while the numeric split flashes
            //for its first48 updates. 41ab70 owns expiry in simulation time.
            result.push_back({0, data.split_label, 208, 60, 0, 0, HudAnchor::top_center});
            if (session.split_ticks > 47 || (session.split_ticks & 15) > 9) {
                small_text(session.split_text, 286, 60, HudAnchor::top_center);
            }
        }
    }
    // 41f7ab / 450067: single-player 640-wide start housing, then three lamps.
    const auto phase = values.start_phase();
    if (phase >= 1 && phase <= 3) {
        result.push_back({2, {0, 0, 128, 64}, 229, 50, 256, 128, HudAnchor::top_center});
        result.push_back({2, {0, 64, 77, 64}, 485, 50, 155, 128, HudAnchor::top_center_support});
        if (phase >= 2) {
            for (int lamp = 0; lamp < 3; ++lamp) {
                result.push_back({phase + 1, data.start_lamps[phase - 2], 240 + lamp * 49, phase == 2 ? 52 : 113, 0, 0, HudAnchor::top_center});
            }
        }
    }
    return result;
}

std::vector<HudLayoutBlit> hud_layout_blits(const HudData& data, const HudValues& values, std::uint32_t width, std::uint32_t height, std::size_t max_quads) {
    std::vector<HudLayoutBlit> result;
    if (width == 0 || height == 0) {
        return result;
    }
    const bool width_limited = std::uint64_t(width) * 480 <= std::uint64_t(height) * 640;
    const double scale = width_limited ? double(width) / 640 : double(height) / 480;
    const double center_left = width_limited ? 0 : (double(width) - 640 * scale) / 2;
    const double bottom_top = width_limited ? double(height) - 480 * scale : 0;
    const auto add = [&](const HudBlit& blit, double x, double y, double w, double h, double source_fraction = 1) {
        if (result.size() >= max_quads) {
            throw std::runtime_error("HUD exceeds frame index capacity");
        }
        result.push_back({blit.atlas, {float(blit.source.x), float(blit.source.y), float(double(blit.source.width) * source_fraction), float(blit.source.height)},
            {float(x), float(y), float(w), float(h)}});
    };
    for (const auto& blit : hud_blits(data, values)) {
        double left = 0, top = 0;
        switch (blit.anchor) {
            case HudAnchor::top_left:
                break;
            case HudAnchor::top_center:
            case HudAnchor::top_center_support:
                left = center_left;
                break;
            case HudAnchor::top_right:
                left = 2 * center_left;
                break;
            case HudAnchor::bottom_right:
                left = 2 * center_left;
                top = bottom_top;
                break;
        }
        const double x = left + double(blit.x) * scale, y = top + double(blit.y) * scale;
        const double w = double(blit.width == 0 ? blit.source.width : blit.width) * scale;
        const double h = double(blit.height == 0 ? blit.source.height : blit.height) * scale;
        if (blit.anchor != HudAnchor::top_center_support) {
            add(blit, x, y, w, h);
            continue;
        }
        // 41f7ab's authored155x128 strip keeps its lattice scale. Continue
        // its pattern from the centered housing, cropping only the final tile.
        const double extent = w + center_left;
        const double tiles = std::ceil(extent / w);
        if (tiles > double(max_quads - result.size())) {
            throw std::runtime_error("HUD exceeds frame index capacity");
        }
        for (std::size_t tile = 0; tile < std::size_t(tiles); ++tile) {
            const double tile_width = std::min(w, extent - double(tile) * w);
            add(blit, x + double(tile) * w, y, tile_width, h, tile_width / w);
        }
    }
    return result;
}

Hud::Hud(const AssetStore& store, gpu::Renderer& renderer, std::uint32_t language, bool mega, const std::filesystem::path& artwork_directory)
    : data_(load_hud(store, language)), mega_(mega) {
    if (mega_) {
        const auto artwork = load_hud_artwork(artwork_directory, data_.kilometres);
        for (std::size_t index = 0; index < textures_.size(); ++index) {
            textures_[index] = renderer.upload(artwork[index].get());
        }
        return;
    }
    for (std::size_t index = 0; index < textures_.size(); ++index) {
        textures_[index] = renderer.upload(data_.atlases[index].get(), true);
    }
}

void Hud::update(std::int32_t speed_integer, std::int32_t rpm, std::int32_t gear, std::uint32_t start_phase) {
    values_.update(speed_integer, rpm, gear, start_phase);
}

void Hud::set_session(HudSession session) {
    values_.set_session(session);
}

void Hud::import_snapshot(HudSnapshot snapshot) {
    if (snapshot.kilometres != data_.kilometres) {
        throw std::runtime_error("HUD snapshot units must match the constructor's MILES/KM atlas");
    }
    const auto visible = snapshot.visible;
    values_.import_snapshot(std::move(snapshot));
    visible_ = visible;
}

const HudValues& Hud::values() const {
    return values_;
}

void Hud::append(gpu::Frame& frame, std::uint32_t width, std::uint32_t height) const {
    if (!visible_ || width == 0 || height == 0) {
        return;
    }
    const auto blits = hud_layout_blits(data_, values_, width, height);
    for (const auto& blit : blits) {
        const auto first = frame.vertices.size();
        const auto& source = blit.source;
        const auto& destination = blit.destination;
        const auto* atlas = data_.atlases[blit.atlas].get();
        frame.quad(
            {.texture = textures_[blit.atlas].get(), .blend = mega_, .key = !mega_, .linear = mega_, .clamp = true, .palette = !mega_ && SDL_ISPIXELFORMAT_INDEXED(atlas->format)},
            destination.x, destination.y, destination.width, destination.height, 0xffc8c8c8); // 490c00: opaque RGB200 for gauges, housing and lamps.
        for (std::size_t corner = 0; corner < 4; ++corner) {
            auto& vertex = frame.vertices[first + corner];
            if (!mega_) {
                // 490cb0 emits integer TL corners; quad() accepts pixel edges.
                vertex.x += .5f;
                vertex.y += .5f;
            }
            // 490cb0 stores (source coordinate +0.1)/atlas extent in each TL
            // corner. The bias matters on the stretched77/155 start support.
            const double bias = mega_ ? 0 : .1;
            vertex.u = float((double(source.x) + double(vertex.u) * double(source.width) + bias) / double(atlas->w));
            vertex.v = float((double(source.y) + double(vertex.v) * double(source.height) + bias) / double(atlas->h));
        }
    }
}
} // namespace toca::scene
