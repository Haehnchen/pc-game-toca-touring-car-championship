#include "core/random.hpp"
#include "scene/prop_fragments.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace toca::scene;
        toca::SourceRandom random(7), expected_random(7);
        auto generated = make_prop_fragment_tables(random);
        for (unsigned draw = 0; draw < 185; ++draw) {
            (void)expected_random.next();
        }
        expect(random.next() == expected_random.next() && generated.cursor == 0, "Fragment initialization must consume the original111+74 source draws");
        expect(std::ranges::all_of(generated.counts,
                   [](auto count) {
                       return count >= 1 && count <= 10;
                   }),
            "Original4153c0 count draws must survive initialization for impact/spark allocation");
        PropFragmentTables tables;
        tables.positive.fill(32768);
        tables.signed_values.fill(16384);
        tables.color_indices.fill(1);
        PropFragments fragments(tables, 2);
        HorizonTransform root{};
        root[0] = root[5] = root[10] = root[15] = 65536;
        root[13] = 10 * 65536;
        const auto event = fragments.break_sign(root, {65536, 999999, 0});
        expect(event && event->event_kind == 21 && event->local_player && event->strength_q16 == 65536 && event->position_q16 == std::array<std::int32_t, 3>{-25281, 689066, 0},
            "Sign break must preserve original grid/sound position and ignore vertical car velocity");
        expect(fragments.size() == 37 && tables.cursor == 0, "One sign must emit37 fragments and consume111 cyclic table values");
        const auto first = fragments.triangles().back();
        expect(fragments.presentation_triangles(0).back().vertices == first.vertices, "New sign fragments must cut presentation history to their spawn geometry");
        expect(first.vertices[0] == std::array<std::int32_t, 3>{-55016, 771055, -4358} && first.color == 0xff9ba2a8,
            "Original sign shard shape, first-row placement or weather color changed");
        fragments.update();
        const auto next = fragments.triangles().back();
        expect(next.vertices[0] == std::array<std::int32_t, 3>{-15695, 790719, 2195}, "First fragment tick must apply sourceXZ velocity, lift and midpoint normalization");
        const auto presentation_cursor = tables.cursor;
        for (const auto fps : {60u, 144u}) {
            for (std::uint32_t sample = 0; sample * 25 < fps; ++sample) {
                const auto alpha = float(sample * 25) / float(fps);
                const auto triangle = fragments.presentation_triangles(alpha).back();
                for (std::size_t vertex = 0; vertex < 3; ++vertex) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        const auto expected = double(first.vertices[vertex][axis]) + double(next.vertices[vertex][axis] - first.vertices[vertex][axis]) * alpha;
                        expect(std::abs(double(triangle.vertices[vertex][axis]) - expected) <= 1, "Sign fragment presentation must expose intermediate60/144Hz flight geometry");
                    }
                }
                expect(triangle.color == next.color, "Fragment palette remains discrete during presentation");
            }
        }
        expect(tables.cursor == presentation_cursor && fragments.triangles().back().vertices == next.vertices && fragments.size() == 37,
            "Repeated fragment presentation must not move particles, expire records or consume random entries");
        (void)fragments.break_sign(root, {65536, 0, 0});
        expect(fragments.size() == 63 && !fragments.break_sign(root, {65536, 0, 0}), "Sign fragments must honor the original63-slot shared pool");
        const auto cursor = tables.cursor;
        fragments.clear();
        expect(fragments.size() == 0 && tables.cursor == cursor, "Scene reset clears fragments without resetting the shared cyclic table cursor");
        expect(fragments.presentation_triangles(.5f).empty(), "Clearing fragments must also discard every previous rendered record");
        (void)fragments.break_sign(root, {10 * 65536, 0, 0});
        for (unsigned tick = 0; tick < 201; ++tick) {
            fragments.update();
        }
        expect(fragments.size() == 0, "Original fragment lifetime must release every occupied slot");
        tables.counts.fill(2);
        tables.cursor = 0;
        ImpactFragmentRequest impact{{0, 10 * 65536, 0}, {}, 0x3333, {0, 0}};
        fragments.impact(impact);
        expect(fragments.size() == 0 && tables.cursor == 0, "Impact intensity at0x3333 must not allocate or consume table entries");
        impact.intensity_q16 = 65536;
        fragments.impact(impact);
        expect(fragments.size() == 10 && tables.cursor == 25, "Original full-strength burst must sum five count entries then consume two entries per fragment");
        const auto impact_first = fragments.triangles().back();
        expect(fragments.presentation_triangles(0).back().vertices == impact_first.vertices, "Reused fragment capacity must start at the new impact rather than old sign geometry");
        expect(impact_first.vertices[0] == std::array<std::int32_t, 3>{1983, 654224, -2179} && impact_first.color == 0xff120000,
            "Impact shards must use source five-shape scale0x5999 and the selected weather-scaled car palette");
        fragments.update();
        expect(fragments.triangles().back().vertices[0] == std::array<std::int32_t, 3>{4440, 659139, 278},
            "Impact first tick must apply source signed-tableXZ velocity and positive-tableY lift");
        for (unsigned tick = 1; tick < 25; ++tick) {
            fragments.update();
        }
        expect(fragments.size() == 10, "Impact fragments must retain their original25 advancing ticks");
        fragments.update();
        expect(fragments.size() == 0, "Impact fragments must expire separately from200-tick sign shards");
        (void)fragments.break_sign(root, {65536, 0, 0});
        tables.counts.fill(10);
        fragments.impact(impact);
        expect(fragments.size() == 63, "Impact shards must share the existing sign pool's63 records");
        const auto full_cursor = tables.cursor;
        fragments.impact(impact);
        expect(tables.cursor == full_cursor, "A full shared pool must reject impact bursts before table consumption");
        fragments.clear();
        impact.car_ids = {11, 11};
        fragments.impact(impact);
        expect(fragments.triangles().front().color == 0xffd6d6de, "4153c0 must leave its final Vectra2 palette outside the weather modulation interval");
        for (const bool collision : {false, true}) {
            auto ordinary_table = tables, light_table = tables;
            PropFragments ordinary(ordinary_table, 2), light(light_table, 2, true);
            if (collision) {
                ordinary.impact(impact);
                light.impact(impact);
            } else {
                (void)ordinary.break_sign(root, {65536, 0, 0});
                (void)light.break_sign(root, {65536, 0, 0});
            }
            ordinary.update();
            light.update();
            expect(ordinary.triangles().back().vertices == light.triangles().back().vertices,
                "CMLOGRAV must retain sign/impact spawn geometry and the first translation before gravity");
            ordinary.update();
            light.update();
            const auto normal = ordinary.triangles().back(), floating = light.triangles().back();
            for (std::size_t vertex = 0; vertex < 3; ++vertex) {
                expect(floating.vertices[vertex][1] - normal.vertices[vertex][1] == (collision ? 0x83 - 0xd : 0x7ae - 0xc4)
                           && floating.vertices[vertex][0] == normal.vertices[vertex][0] && floating.vertices[vertex][2] == normal.vertices[vertex][2],
                    "4153c0 must apply the exact independent low-gravity coefficients to sign shards and collision fragments");
            }
            expect(ordinary.size() == light.size() && ordinary_table.cursor == light_table.cursor && normal.color == floating.color,
                "CMLOGRAV must preserve fragment allocation, random consumption and palette selection");
        }
        std::cout << "PASS: original shared sign/impact fragments, tables, shapes, motion, palettes, pool and lifetimes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
