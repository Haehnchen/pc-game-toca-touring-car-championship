#include "scene/mega_ground_uv.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace toca::scene;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void mapping_contract() {
    constexpr std::array grass_names{"GRASS", "GRASS2", "NUGRASS", "GRASS2", "GRASS", "GRASS", "GRASS2", "GRASS2", "GRASS3"};
    for (std::uint32_t track = 0; track < grass_names.size(); ++track) {
        const auto grass = mega_ground_uv_mapping(true, track, grass_names[track]);
        expect(grass && grass->repeat && grass->tile_world_units == 26.f, "Each real track must retain its canonical median grass tile density");
        for (const auto material : {grass_names[track], "GRAVEL", "gravel", "NLSAND", "DARKGRS"}) {
            const auto selected = mega_ground_uv_mapping(true, track, material);
            if (!selected) {
                continue;
            }
            const auto mapping = *selected;
            const auto size = mapping.tile_world_units;
            const auto a = mapping.coordinates({size / 2, 3.f, -size});
            const auto b = mapping.coordinates({size / 2, 95.f, -size});
            expect(a == b && a[0] == .5f && a[1] == -1.f, "Shared world XZ positions must have identical signed UVs across faces and chunks");
            const auto next = mapping.coordinates({size * 1.5f, -7.f, 0.f});
            expect(next[0] - a[0] == 1.f && next[1] - a[1] == 1.f, "Tile coordinates must stay unwrapped across positive and negative boundaries");
            // Compare an arbitrary point on a shared edge with its interpolated
            // corner UVs, as both neighboring terrain triangles will render it.
            const std::array<float, 3> first{-11.f, 2.f, 17.f}, second{37.f, 4.f, -31.f};
            const auto first_uv = mapping.coordinates(first), second_uv = mapping.coordinates(second);
            const auto midpoint = mapping.coordinates({13.f, 3.f, -7.f});
            for (std::size_t axis = 0; axis < 2; ++axis) {
                expect(std::abs(midpoint[axis] - (first_uv[axis] + second_uv[axis]) / 2) < .000001f, "World UV mapping must remain affine across shared triangle edges");
            }
        }
    }
    expect(mega_ground_uv_mapping(true, 0, "GRAVEL")->tile_world_units == 16.f && mega_ground_uv_mapping(true, 4, "GRAVEL")->tile_world_units == 18.f
               && mega_ground_uv_mapping(true, 2, "DARKGRS")->tile_world_units == 37.f && mega_ground_uv_mapping(true, 3, "NLSAND")->tile_world_units == 13.f,
        "Distinct neutral source images must retain their individually measured scales");
}

void projection_support() {
    const auto mapping = *mega_ground_uv_mapping(true, 0, "GRASS");
    std::array<std::array<float, 3>, 3> points{{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}};
    expect(mapping.supports_triangle(points), "Level ground must support world XZ mapping");
    std::swap(points[1], points[2]);
    expect(mapping.supports_triangle(points), "The slope guard must preserve both authored windings");
    points = {{{0, 0, 0}, {1, 0, 0}, {0, 1, 1}}};
    expect(mapping.supports_triangle(points), "Ordinary banks must support world XZ mapping");
    points[2] = {0, 2, 1};
    expect(!mapping.supports_triangle(points), "Steep ground must retain its source UVs instead of extending planar stretch");
    points[2] = {0, 1, 0};
    expect(!mapping.supports_triangle(points), "Vertical terrain closures must retain their authored mapping");
    points[2] = points[1];
    expect(!mapping.supports_triangle(points), "Zero-area faces must retain their authored mapping");
    points[2] = {0, std::numeric_limits<float>::infinity(), 1};
    expect(!mapping.supports_triangle(points), "Nonfinite source positions must not select a world UV projection");
}

void scope() {
    unsigned selected_count = 0;
    for (std::uint32_t track = 0; track < 10; ++track) {
        for (const auto material : {"GRASS", "GRASS2", "GRASS3", "NUGRASS", "DARKGRS", "GRAVEL", "gravel", "NLSAND"}) {
            expect(!mega_ground_uv_mapping(false, track, material), "Original profiles must keep all authored ground UVs");
            selected_count += bool(mega_ground_uv_mapping(true, track, material));
            expect(track != 9 || !mega_ground_uv_mapping(true, track, material), "Lavaland has no neutral grass/gravel slots");
        }
        for (const auto material : {"G&G", "GG", "NGG", "g&g", "G&G2", "TRACK", "GRID_L", "GRID_R", "START_L", "STRIP", "DLINE", "RUMBLE2", "SCRUB", "LAVA03"}) {
            expect(!mega_ground_uv_mapping(true, track, material), "Painted markings, mixed boundaries and directed surfaces must retain their authored UVs");
        }
    }
    expect(selected_count == 18, "The audited neutral ground inventory has exactly18 selected material slots");
    for (const auto material : {"GRASS", "GRAVEL"}) {
        const auto gp = *mega_ground_uv_mapping(true, 0, material), short_track = *mega_ground_uv_mapping(true, 5, material);
        expect(gp.coordinates({-17.f, 3.f, 95.f}) == short_track.coordinates({-17.f, 3.f, 95.f}), "Donington variants must use exactly the same material mapping");
    }
}
} // namespace

int main() {
    try {
        mapping_contract();
        projection_support();
        scope();
        std::cout << "PASS: neutral world UV scale, repeated boundaries, shared-edge continuity, slope guard and original/marking scope\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
