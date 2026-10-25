#include "assets/store.hpp"
#include "scene/hud.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void synthetic() {
    using namespace toca::scene;
    expect(!qualifying_rollout_notice_visible(4, 50, 0) && qualifying_rollout_notice_visible(5, 50, 900) && qualifying_rollout_notice_visible(46, 50, 900),
        "Preparation caption must be solid between target5 and count-4");
    expect(qualifying_rollout_notice_visible(47, 50, 659) && !qualifying_rollout_notice_visible(47, 50, 660) && !qualifying_rollout_notice_visible(49, 50, 999)
               && qualifying_rollout_notice_visible(49, 50, 1000),
        "Final three rollout targets must flash with the original strict660ms/1000ms boundary");
    HudValues values;
    values.update(100, 8500, 1);
    expect(values.rpm() == 2500 && values.rpm_segments() == 0, "HUD RPM must slew500 per simulation update");
    values.update(100, 8500, 7);
    expect(
        values.rpm() == 3000 && values.rpm_segments() == 1 && values.speed(true) == 100 && values.speed(false) == 62, "Source RPM thresholds, gear or KM/MILES conversion changed");
    for (int index = 0; index < 20; ++index) {
        values.update(260, 8500, 6);
    }
    expect(values.rpm_segments() == 12, "Original tachometer must saturate at12 sprites");
    values.update(2, 2000, 0);
    expect(values.rpm() == 8000 && values.speed(true) == 0 && values.speed(false) == 0, "RPM fall or low-speed suppression changed");
    HudData data;
    for (std::size_t index = 0; index < data.gear_glyphs.size(); ++index) {
        data.gear_glyphs[index] = {int(index), 0, 10, 20};
    }
    for (auto& glyph : data.speed_glyphs) {
        glyph = {0, 0, 19, 12};
    }
    values.update(100, 8000, 7);
    const auto blits = hud_blits(data, values);
    expect(blits[2 + values.rpm_segments()].source.x == 10 && blits[2 + values.rpm_segments()].atlas == 1, "Reverse must select original large R glyph in secondary atlas");
    expect(blits[3 + values.rpm_segments()].x == 560 && blits[4 + values.rpm_segments()].x == 581 && blits[4 + values.rpm_segments()].y == 459,
        "Original speed digits require fixed2-pixel advance");
    values.update(0, 8000, 0);
    expect(hud_blits(data, values)[2 + values.rpm_segments()].source.x == 11, "Neutral must select original N glyph");
    const auto gauges = hud_blits(data, values).size();
    data.start_lamps = {{{0, 0, 64, 64}, {0, 0, 64, 64}}};
    values.update(0, 8000, 0, 1);
    expect(hud_blits(data, values).size() == gauges + 2, "Initial start phase must show only the original housing");
    values.update(0, 8000, 0, 2);
    const auto red = hud_blits(data, values);
    expect(red.size() == gauges + 5 && red.back().atlas == 3 && red.back().x == 338 && red.back().y == 52, "Red start phase must show three original lamps with49-pixel spacing");
    values.update(0, 8000, 0, 3);
    const auto green = hud_blits(data, values);
    expect(green.back().atlas == 4 && green.back().y == 113, "Release phase must use the separate lower green lamps");
    values.update(0, 8000, 0, 4);
    expect(hud_blits(data, values).size() == gauges, "Completed start phase must remove the whole start housing");
    expect(hud_time_text(0) == "00:00.00" && hud_time_text(61'239) == "01:01.23" && hud_time_text(3'600'000) == "59:59.99",
        "Original time syntax must truncate centiseconds, carry minutes and saturate at59:59.99");
    for (std::size_t index = 0; index < data.lap_glyphs.size(); ++index) {
        data.lap_glyphs[index] = {int(index), 0, 20, 35};
    }
    for (std::size_t index = 0; index < data.time_glyphs.size(); ++index) {
        data.time_glyphs[index] = {int(index), 106, 19, 12};
    }
    data.unknown_time_y = 118;
    data.position_background = {7, 109, 84, 9};
    data.position_glyphs = data.lap_glyphs;
    values.set_session({false, 1, 3, 0, 0, toca::GameMode::single_race, 16});
    const auto race_position = hud_blits(data, values);
    const auto position_plaque = std::find_if(race_position.begin(), race_position.end(), [](const auto& blit) {
        return blit.anchor == HudAnchor::top_right && blit.atlas == 0;
    });
    expect(position_plaque != race_position.end() && position_plaque->x == 437 && position_plaque->y == 9,
        "Source4c33ea must place the POS plaque at y9 independently of the large rank at y5");
    const auto position_digit = std::find_if(race_position.begin(), race_position.end(), [](const auto& blit) {
        return blit.anchor == HudAnchor::top_right && blit.x == 580;
    });
    expect(position_digit != race_position.end() && position_digit->atlas == 1 && position_digit->source.x == 1 && position_digit->y == 5,
        "Actual16-actor position must use original secondary-atlas rank digits right-aligned at620");
    values.set_session({false, 2, 6, 61'239, 59'990, toca::GameMode::championship, 16});
    const auto championship = hud_blits(data, values);
    expect(values.session().lap == 2 && values.session().lap_limit == 6
               && std::any_of(championship.begin(), championship.end(),
                   [](const auto& blit) {
                       return blit.anchor == HudAnchor::top_right && blit.x == 580 && blit.atlas == 1 && blit.source.x == 1;
                   })
               && std::any_of(championship.begin(), championship.end(),
                   [](const auto& blit) {
                       return blit.anchor == HudAnchor::top_left && blit.x == 146 && blit.y == 27 && blit.source.x == 6;
                   }),
        "Championship HUD must show the actual finite six-lap limit and16-car rank alongside measured timing");
    values.set_session({true, 1, 3, 0, 0, toca::GameMode::single_race, 16});
    const auto qualifying_position = hud_blits(data, values);
    expect(std::none_of(qualifying_position.begin(), qualifying_position.end(),
               [](const auto& blit) {
                   return blit.anchor == HudAnchor::top_right;
               }),
        "Qualifying must omit race position artwork");
    const auto rpm_before = values.rpm();
    values.set_session({false, 12, 15, 61'239, 0});
    const auto timing = hud_blits(data, values);
    const auto at = [&](int x, int y) -> const HudBlit& {
        const auto found = std::find_if(timing.begin(), timing.end(), [&](const HudBlit& blit) {
            return blit.x == x && blit.y == y;
        });
        expect(found != timing.end(), "Missing original lap/time glyph position");
        return *found;
    };
    expect(values.rpm() == rpm_before && values.session().lap == 12 && values.session().lap_limit == 15, "Session caching must not perform another RPM smoothing update");
    expect(at(92, 5).source.x == 1 && at(112, 5).source.x == 2 && at(146, 27).source.x == 1, "Lap number must right-align at132 and total start at146");
    expect(at(286 + 2 * 21, 11).source.x == 10 && at(286 + 5 * 21, 11).source.x == 13 && at(286 + 7 * 21, 11).source.x == 3,
        "Original time must use colon/dot atlas slots and truncated final centisecond");
    expect(at(286, 26).source.x == 11 && at(286, 26).source.y == 118 && at(286 + 2 * 21, 26).source.y == 118,
        "Unknown best time must use the separate placeholder row for every glyph");
    data.split_label = {178, 160, 72, 12};
    for (const auto ticks : {0, 9, 10, 15, 16, 47, 48, 200}) {
        values.set_session({false, 2, 3, 61'239, 60'000, toca::GameMode::single_race, 1, true, std::int16_t(ticks), "+00:01.23"});
        const auto split = hud_blits(data, values);
        const auto visible = ticks > 47 || (ticks & 15) > 9;
        expect(std::count_if(split.begin(), split.end(),
                   [](const auto& blit) {
                       return blit.y == 60;
                   })
                   == (visible ? 10 : 1),
            "Split label must remain visible while its numeric text uses original48-update/low-nibble flash timing");
        if (visible) {
            const auto sign = std::find_if(split.begin(), split.end(), [](const auto& blit) {
                return blit.x == 286 && blit.y == 60;
            });
            expect(sign != split.end() && sign->source.x == 12 && sign->source.y == 106 && sign->anchor == HudAnchor::top_center,
                "Positive split must use the original plus glyph and centered time row");
        }
    }
    values.set_session({false, 2, 3, 61'239, 60'000, toca::GameMode::single_race, 1, true, 50, "-00:01.23"});
    const auto faster = hud_blits(data, values);
    expect(std::count_if(faster.begin(), faster.end(),
               [](const auto& blit) {
                   return blit.y == 60 && blit.source.y == 118;
               })
               == 9,
        "A negative split must select the original alternate row for its complete signed time");
    values.set_session({false, 2, 3, 61'239, 60'000, toca::GameMode::single_race, 1, false, 201, "+00:01.23"});
    const auto expired = hud_blits(data, values);
    expect(std::none_of(expired.begin(), expired.end(),
               [](const auto& blit) {
                   return blit.y == 60;
               }),
        "Expired split must remove both the label and its numeric text");
    values.set_session({true, 4, 15, 0, 2600});
    expect(values.session().lap == 3 && values.session().lap_limit == 3, "Qualifying must display original three-lap limit independently of menu laps");
    const auto known = hud_blits(data, values);
    const auto best = std::find_if(known.begin(), known.end(), [](const HudBlit& blit) {
        return blit.x == 286 && blit.y == 26;
    });
    expect(best != known.end() && best->source.x == 0 && best->source.y == 106, "Known best must use numeric time row rather than placeholder row");
    values.set_session({false, 12, 0, 61'239, 60'010, toca::GameMode::time_trial});
    const auto trial = hud_blits(data, values);
    expect(std::ranges::none_of(trial,
               [](const HudBlit& blit) {
                   return blit.y == 27;
               }),
        "Time Trial must omit the finite lap limit");
}

void snapshot() {
    using namespace toca::scene;
    HudValues values;
    values.update(10, 8500, 1);
    HudSnapshot cached{.speed_integer = 161,
        .rpm = 8465,
        .gear = 7,
        .start_phase = 3,
        .session = {false, 12, 15, 61'239, 60'010, toca::GameMode::single_race, 16, true, 48, "+00:01.23"},
        .kilometres = true};
    values.import_snapshot(cached);
    expect(values.rpm() == 8465 && values.rpm_segments() == 12 && values.gear() == 7 && values.speed(true) == 161 && values.speed(false) == 100 && values.start_phase() == 3,
        "Cached source gauges must import exact RPM without another500-RPM slew or another speed conversion");
    expect(values.session().lap == 12 && values.session().lap_limit == 15 && values.session().lap_time_ms == 61'239 && values.session().best_lap_ms == 60'010
               && values.session().position == 16 && values.session().split_active && values.session().split_ticks == 48 && values.session().split_text == "+00:01.23",
        "Cached source lap, position, timer bank and split must reach the existing HUD session unchanged");
    HudData data;
    data.kilometres = cached.kilometres;
    data.gear_glyphs.fill({0, 106, 20, 35});
    data.speed_glyphs.fill({0, 106, 19, 12});
    data.lap_glyphs.fill({0, 0, 20, 35});
    data.position_glyphs = data.lap_glyphs;
    data.time_glyphs.fill({0, 106, 19, 12});
    data.start_lamps.fill({0, 0, 64, 64});
    const auto first = hud_blits(data, values);
    expect(std::ranges::count_if(first,
               [](const HudBlit& blit) {
                   return blit.y == 60;
               }) == 10
               && first.back().atlas == 4,
        "Imported split and start phase must reach the ordinary production blit path");
    values.import_snapshot(cached);
    expect(values.rpm() == 8465 && hud_blits(data, values).size() == first.size(), "Repeated rendering of one cached HUD snapshot must not advance its smoothed gauges");
    cached.session.lap_limit = 0;
    cached.rpm = 2000;
    bool rejected = false;
    try {
        values.import_snapshot(cached);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected && values.rpm() == 8465 && values.session().lap_limit == 15, "Invalid snapshot session must not partially overwrite valid cached gauges");
    values.update(100, 2000, 1);
    expect(values.rpm() == 7965 && values.speed(true) == 100, "Live HUD updates after snapshot import must resume the existing500-RPM slew from the imported value");
}

void layout() {
    using namespace toca::scene;
    HudData data;
    data.speed_background = {0, 0, 153, 32};
    data.rpm_background = {123, 144, 116, 41};
    data.lap_background = {0, 212, 186, 44};
    data.timing_labels = {178, 186, 72, 25};
    data.gear_glyphs.fill({0, 106, 20, 35});
    data.speed_glyphs.fill({0, 106, 19, 12});
    data.lap_glyphs.fill({0, 0, 20, 35});
    data.time_glyphs.fill({0, 106, 19, 12});
    data.start_lamps.fill({0, 0, 64, 64});
    HudValues values;
    const auto close = [](float actual, float expected) {
        return std::abs(actual - expected) < .001f;
    };
    for (std::uint32_t phase = 0; phase <= 4; ++phase) {
        values.update(100, 2000, 1, phase);
        const auto authored = hud_blits(data, values);
        const auto placed = hud_layout_blits(data, values, 640, 480);
        expect(placed.size() == authored.size(), "640x480 HUD must retain exactly the authored blit count");
        for (std::size_t index = 0; index < authored.size(); ++index) {
            const auto& original = authored[index];
            const auto& actual = placed[index];
            expect(actual.atlas == original.atlas && actual.source.x == original.source.x && actual.source.y == original.source.y && actual.source.width == original.source.width
                       && actual.source.height == original.source.height && actual.destination.x == original.x && actual.destination.y == original.y
                       && actual.destination.width == (original.width == 0 ? original.source.width : original.width)
                       && actual.destination.height == (original.height == 0 ? original.source.height : original.height),
                "640x480 source and destination rectangles must remain exact through every start phase");
        }
    }
    values.update(100, 2000, 1, 2);
    const auto authored = hud_blits(data, values);
    const auto wide = hud_layout_blits(data, values, 1280, 720);
    const auto placed = [&](const HudBlit& original) -> const HudLayoutBlit& {
        const auto index = std::size_t(&original - authored.data());
        // The extra cropped support tile precedes the three lamps.
        return wide[index + (original.atlas == 3 ? 1 : 0)];
    };
    for (const auto& original : authored) {
        const auto& actual = placed(original);
        if (original.anchor == HudAnchor::top_center_support) {
            continue;
        }
        const float offset_x = original.anchor == HudAnchor::bottom_right ? 320.f : original.anchor == HudAnchor::top_center ? 160.f : 0.f;
        expect(close(actual.destination.x, float(original.x) * 1.5f + offset_x) && close(actual.destination.y, float(original.y) * 1.5f),
            "16:9 HUD must place lap at left, timing/start at center and gauges at right");
        expect(close(actual.destination.width, float(original.width == 0 ? original.source.width : original.width) * 1.5f)
                   && close(actual.destination.height, float(original.height == 0 ? original.source.height : original.height) * 1.5f),
            "All HUD artwork and lamps must keep uniform proportions");
    }
    const auto support = std::find_if(wide.begin(), wide.end(), [](const HudLayoutBlit& blit) {
        return blit.atlas == 2 && blit.source.y == 64;
    });
    expect(support != wide.end() && support != wide.begin() && support + 1 != wide.end(), "Widescreen start housing requires repeated support tiles");
    const auto& housing = *(support - 1);
    const auto& tail = *(support + 1);
    expect(close(housing.destination.x + housing.destination.width, support->destination.x) && close(support->destination.x, 887.5f) && close(support->destination.width, 232.5f)
               && close(tail.destination.x, 1120) && close(tail.destination.width, 160) && close(tail.destination.x + tail.destination.width, 1280),
        "Start support must connect to the centered housing and terminate at the physical right edge");
    expect(support->source.width == 77 && close(tail.source.width / tail.destination.width, support->source.width / support->destination.width)
               && support->source.height == tail.source.height && support->destination.height == tail.destination.height,
        "Final support tile must crop UVs rather than stretch the lattice");
    const auto tall = hud_layout_blits(data, values, 640, 800);
    const auto lap_panel = std::find_if(authored.begin(), authored.end(), [](const HudBlit& blit) {
        return blit.anchor == HudAnchor::top_left;
    });
    const auto& tall_lap = tall[std::size_t(lap_panel - authored.begin())];
    expect(tall[0].destination.x == 470 && tall[0].destination.y == 768 && tall_lap.destination.x == 11 && tall_lap.destination.y == 9,
        "Tall drawables must anchor gauges at bottom and lap panel at top");
    for (const auto& [width, height] : {std::pair{1024u, 768u}, {1280u, 800u}, {1920u, 1080u}, {1920u, 1200u}, {853u, 479u}, {1u, 1u}}) {
        const auto geometry = hud_layout_blits(data, values, width, height);
        for (const auto& blit : geometry) {
            const auto& destination = blit.destination;
            expect(std::isfinite(destination.x) && std::isfinite(destination.y) && destination.width > 0 && destination.height > 0 && destination.x >= 0 && destination.y >= 0
                       && destination.x + destination.width <= float(width) + .001f && destination.y + destination.height <= float(height) + .001f,
                "HUD geometry must remain finite and within odd, small and supported drawable sizes");
        }
    }
    expect(hud_layout_blits(data, values, 0, 480).empty() && hud_layout_blits(data, values, 640, 0).empty(), "Zero-sized drawables must produce no HUD geometry");
    expect(hud_layout_blits(data, values, 1280, 720, wide.size()).size() == wide.size(), "HUD capacity must accept an exact fit including repeated support tiles");
    for (const auto& [width, height, capacity] : {std::array<std::size_t, 3>{1280, 720, wide.size() - 1}, {std::numeric_limits<std::uint32_t>::max(), 1, 16384}}) {
        bool rejected = false;
        try {
            hud_layout_blits(data, values, std::uint32_t(width), std::uint32_t(height), capacity);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        expect(rejected, "HUD must reject insufficient index capacity before unbounded support tiling");
    }
}

void reference(const char* root) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    const auto miles = load_hud(store, 0), km = load_hud(store, 2);
    expect(!miles.kilometres && km.kilometres, "Original language0 must use MILES and German KM");
    expect(miles.atlases[0]->w == 256 && miles.atlases[0]->h == 256 && km.atlases[1]->w == 256, "Original HUD atlas dimensions changed");
    expect(km.rpm_background.x == 123 && km.rpm_background.y == 144 && km.rpm_background.width == 116 && km.rpm_background.height == 41,
        "Original compiled640 tachometer rectangle changed");
    HudValues values;
    values.update(100, 2000, 1);
    const auto blits = hud_blits(km, values);
    expect(blits[0].x == 470 && blits[0].y == 448 && blits[1].x == 452 && blits[1].y == 427 && blits[2].x == 566 && blits[2].y == 424,
        "Original single-player640 HUD placement changed");
    expect(blits.size() == 26 && blits[5].x == 602 && blits[5].source.width == 19, "Canonical speed100 must use three original glyphs alongside the regular lap/time panel");
    expect(km.timing_labels.x == 178 && km.timing_labels.y == 186 && km.timing_labels.width == 72 && km.timing_labels.height == 25,
        "Original combined TIME/BEST label rectangle changed");
    const auto labels = std::find_if(blits.begin(), blits.end(), [](const HudBlit& blit) {
        return blit.x == 208 && blit.y == 12;
    });
    expect(labels != blits.end() && labels->atlas == 0 && labels->source.x == 178 && labels->anchor == HudAnchor::top_center,
        "Original timing label artwork must sit immediately left of both time rows");
    expect(km.atlases[2]->w == 128 && km.atlases[2]->h == 128 && km.start_lamps[0].width == 64 && km.start_lamps[1].height == 64,
        "Original start housing/red/green archive members changed");
    values.update(100, 2000, 1, 1);
    const auto start = hud_blits(km, values);
    expect(start.back().source.y == 64 && start.back().source.width == 77 && start.back().x + start.back().width == 640 && start.back().height == 128,
        "Original split start housing must stretch its second strip to the640-wide right edge");
    expect(km.gear_glyphs[1].y == 106 && km.lap_glyphs[1].y == 0 && km.lap_background.x == 0 && km.lap_background.y == 212 && km.lap_background.width == 186,
        "Original gear secondary row and lap primary row/background changed");
    expect(km.time_glyphs[10].x == 192 && km.time_glyphs[10].width == 7 && km.time_glyphs[11].x == 200 && km.time_glyphs[13].x == 235 && km.unknown_time_y == 118,
        "Original time punctuation or unknown-time atlas row changed");
    values.set_session({false, 1, 5, 0, 0});
    const auto session = hud_blits(km, values);
    const auto first = std::find_if(session.begin(), session.end(), [](const HudBlit& blit) {
        return blit.x == 286 && blit.y == 11;
    });
    expect(first != session.end() && first->source.x == 0 && first->source.y == 106, "Initial current lap time must render numeric zero before release");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            snapshot();
            layout();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-hud-tests [canonical-root]");
        }
        std::cout << "PASS: original HUD values, sprites and layout\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
