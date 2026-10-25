#include "assets/store.hpp"
#include "scene/resources.hpp"
#include "scene/sun.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SunCamera camera(std::uint32_t width = 640, std::uint32_t height = 480, float dx = 0, float dy = 0, float depth = 100) {
    auto matrix = identity;
    const auto source = sun_world_position(0);
    matrix[12] = -source[0] + dx;
    matrix[13] = -source[1] + dy;
    matrix[14] = -source[2] + depth;
    return {matrix, {2, 2, .25f, 500, 1, -.25f, width, height}};
}

void synthetic() {
    expect(sun_world_position(0) == std::array<float, 3>{1600, 1000, 2400} && sun_world_position(5) == sun_world_position(0),
        "Sun point must retain44eda9's distinct X/Z and Y scaling and shared Donington source");
    expect(sun_world_position(1) == std::array<float, 3>{3200, 2000, -1280} && sun_world_position(9) == std::array<float, 3>{32, 10, 32},
        "Silverstone and the no-case9 initial sun point changed");
    const auto centered = lens_quads(camera(), 0);
    expect(centered.size() == 7, "Visible projected sun must enable exactly seven lens slots");
    for (std::size_t index = 0; index < centered.size(); ++index) {
        const auto& quad = centered[index];
        const auto side = float(lens_dimensions[index]);
        expect(quad.texture == index && quad.x == 320 - side / 2 && quad.y == 240 - side / 2 && quad.width == side && quad.height == side,
            "The centered flare must keep original slot order and source pixel dimensions");
        expect(std::abs(quad.u0 - .1f / side) < .00001f && std::abs(quad.u1 - (side + .1f) / side) < .00001f,
            "Lens image boundaries must preserve490cb0's tenth-pixel source UV bias");
    }
    const auto shifted = lens_quads(camera(640, 480, 10), 0);
    expect(shifted.size() == 7 && shifted[0].x == 393 && shifted[1].x == 418 && shifted[2].x == 263 && shifted[6].x == 131,
        "Original lens spacing must use integer0x6666 steps: two beyond the sun and five beyond center");
    const auto scaled = lens_quads(camera(1280, 960), 0);
    expect(scaled.size() == 7 && scaled[0].x == centered[0].x * 2 && scaled[0].width == centered[0].width * 2, "Higher resolution must scale the authored lens layout uniformly");
    const auto wide = lens_quads(camera(1280, 720), 0);
    expect(wide.size() == 7 && wide[0].width == 48 && wide[0].height == 48 && std::abs(wide[0].x + 24 - 640) < .001f,
        "Widescreen must retain round sprites and the actual viewport center");
    expect(
        lens_quads(camera(640, 480, 0, 0, 1), 0).empty() && lens_quads(camera(640, 480, 0, 0, -1), 0).empty(), "491950 must reject projected depth at or behind one original unit");
    expect(!lens_quads(camera(640, 480, 53), 0).empty() && lens_quads(camera(640, 480, 56), 0).empty(),
        "Partial64px sun-rectangle intersection must gate all seven flares without inventing an occlusion test");
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    auto result = UINT32_C(0xffffffff);
    for (const auto byte : bytes) {
        result ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            result = (result >> 1) ^ ((result & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return result ^ UINT32_C(0xffffffff);
}

void canonical(const char* root, const char* mega_root) {
    const AssetStore store(root);
    constexpr std::array<std::uint32_t, 7> checksums{0x6e9f1519, 0xcc0fa5e2, 0xdd59d764, 0x332bbd6a, 0x9ff65b16, 0x2ed70168, 0x04139227};
    for (std::uint32_t weather = 1; weather <= 5; ++weather) {
        const auto resources = load_sun_resources(store, resolve_setup({.weather_choice = weather}));
        expect(resources.textures.size() == (weather == 1 ? 7 : 0), "Only resolved sunny weather may load the original lens texture set");
        for (std::size_t index = 0; index < resources.textures.size(); ++index) {
            expect(crc32(resources.textures[index].bmp) == checksums[index], "Lens source slot must keep its exact embedded BMP palette and CRC32");
        }
    }
    for (const auto selection : {SceneSelection{.track_id = 9, .weather_choice = 5}, SceneSelection{.stars = true}, SceneSelection{.toon = true}}) {
        expect(load_sun_resources(store, resolve_setup(selection)).textures.size() == 7, "Lavaland and special skies use original sunny lens-resource setup");
    }
    if (mega_root) {
        const auto original = load_sun_resources(store, resolve_setup({.weather_choice = 1}));
        const auto scene = load_scene_resources(store, resolve_setup({.weather_choice = 1, .detail = 4}), mega_root);
        expect(scene.sun.textures.size() == 7, "Normal scene loader must retain all seven lens resources");
        for (std::size_t index = 0; index < original.textures.size(); ++index) {
            std::ostringstream key;
            key << std::hex << std::setw(8) << std::setfill('0') << crc32(original.textures[index].bmp);
            const auto path = std::filesystem::path(mega_root) / "scene" / (key.str() + ".webp");
            std::ifstream input(path, std::ios::binary);
            const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            expect(!bytes.empty() && scene.sun.textures[index].bmp == bytes && scene.sun.textures[index].encoding == ImageEncoding::webp,
                "Normal scene loader must install the actual lens WebP identified by the original BMP CRC32");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        synthetic();
        if (argc > 1) {
            canonical(argv[1], argc > 2 ? argv[2] : nullptr);
        }
        std::cout << "PASS: original lens points, slots, projection gates, spacing and widescreen layout\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
