#include "assets/store.hpp"
#include "scene/surface_effects_render.hpp"
#include "simulation/fixed.hpp"
#include "world_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr auto one = simulation::fixed::one;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    auto crc = UINT32_C(0xffffffff);
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

SurfaceEffectResources synthetic_resources() {
    // A1x1 uncompressed24-bit BMP isolates the blend equation from artwork.
    std::vector<std::uint8_t> bmp(58);
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            bmp[offset + byte] = std::uint8_t(value >> (byte * 8));
        }
    };
    bmp[0] = 'B';
    bmp[1] = 'M';
    put(2, 58);
    put(10, 54);
    put(14, 40);
    put(18, 1);
    put(22, 1);
    bmp[26] = 1;
    bmp[28] = 24;
    put(34, 4);
    bmp[54] = 32;
    bmp[55] = 64;
    bmp[56] = 128;
    SurfaceEffectResources result;
    result.textures.fill(bmp);
    return result;
}

SurfaceEffectSprite descriptor() {
    return {{0, 0, 4 * one}, {-one, -one, 0}, {one, one, 0}, 128, 200};
}

SurfaceEffectFrame single(std::uint8_t flags, SurfaceEffectTexture texture = SurfaceEffectTexture::smoke) {
    SurfaceEffectFrame result;
    result.batches[0] = {texture, flags, {descriptor()}};
    return result;
}

std::vector<std::uint32_t> render(gpu::Renderer& renderer, const gpu::Frame& frame, const char* name = nullptr) {
    expect(renderer.draw(frame, false), "Surface effects require their private GPU drawable");
    if (name) {
        renderer.save_screenshot(name);
    }
    return renderer.readback();
}

std::size_t visible(const std::vector<std::uint32_t>& pixels) {
    return std::ranges::count_if(pixels, [](auto color) {
        return color != gpu::background;
    });
}

void source_fields(const SurfaceEffectsView& view, const CameraInputs& camera) {
    const auto sprite = descriptor();
    const auto normal = surface_effect_sprite_vertices(sprite, 2, {}), screen = surface_effect_sprite_vertices(sprite, 6, {});
    expect(normal[0].position == std::array<float, 3>{-1, -1, 4} && normal[3].position == std::array<float, 3>{1, 1, 4} && normal[0].u == 0 && normal[0].v == 1 && normal[3].u == 1
               && normal[3].v == 0 && normal[0].color == 0x80c8c8c8 && screen[0].color == 0xff808080,
        "497180 must interpret raw source shade bytes differently for Flags2 and Flags6");
    auto rotated = WeatherCamera{};
    rotated.right = {0, 0, -one};
    expect(surface_effect_sprite_vertices(sprite, 2, rotated)[0].position == std::array<float, 3>{0, -1, 5}, "Surface billboards must use the actual source camera basis");

    SurfaceEffectFrame batches;
    batches.batches[0] = {SurfaceEffectTexture::spray, 6, std::vector<SurfaceEffectSprite>(103, sprite)};
    batches.batches[1] = {SurfaceEffectTexture::mud, 2, {sprite}};
    batches.batches[2] = {SurfaceEffectTexture::gravel, 2, std::vector<SurfaceEffectSprite>(101, sprite)};
    batches.batches[3] = {SurfaceEffectTexture::grass, 2, {sprite}};
    gpu::Frame budget;
    expect(view.append(budget, camera, batches, 25) == 127 && budget.dynamic_world_vertices.size() == 102 * 6 && budget.draws.size() == 2 && !budget.draws.front().state.screen
               && budget.draws.front().state.texture != budget.draws.back().state.texture,
        "497070 must count prior weather and reject an overflowing entire batch while permitting later smaller batches");
    auto clipped = single(2);
    clipped.batches[0].sprites[0] = {{0, 0, one}, {-2 * one, -2 * one, 0}, {2 * one, 2 * one, 0}, 128, 200};
    gpu::Frame rejected;
    expect(view.append(rejected, camera, clipped) == 1 && rejected.dynamic_world_vertices.empty() && rejected.draws.empty(),
        "Large close-camera source sprites must consume queue space despite497180 rejection");
    for (const auto flags : {2u, 6u}) {
        gpu::Frame submitted;
        view.append(submitted, camera, single(std::uint8_t(flags)));
        expect(submitted.vertices.empty() && submitted.indices.empty() && submitted.dynamic_world_vertices.size() == 6 && submitted.draws.size() == 1
                   && submitted.draws[0].dynamic_world && !submitted.draws[0].mesh,
            "Surface particles must retain six prelit source corners without CPU projection or resident meshes");
        const auto& expected = flags == 2 ? normal : screen;
        constexpr std::array order{0u, 3u, 2u, 0u, 1u, 3u};
        for (std::size_t corner = 0; corner < order.size(); ++corner) {
            const auto& source = expected[order[corner]];
            const auto& actual = submitted.dynamic_world_vertices[corner];
            expect(actual.position == source.position && actual.color == source.color && actual.u == source.u && actual.v == source.v,
                "Surface GPU corners must preserve triangle order, Flags2/6 shade and UVs");
        }
    }
}

void blend_and_depth(gpu::Renderer& renderer, const SurfaceEffectsView& view, const CameraInputs& camera) {
    for (const auto flags : {2u, 6u}) {
        const auto effects = single(std::uint8_t(flags));
        gpu::Frame frame;
        frame.quad({}, 0, 0, 640, 480, 0xff204060);
        view.append(frame, camera, effects);
        const auto& state = frame.draws.back().state;
        expect(state.depth && !state.depth_write && state.blend && state.key && state.screen == (flags == 6), "Source particle depth and blend states changed");
        const auto pixels = render(renderer, frame, flags == 6 ? "surface-screen-spray.png" : "surface-alpha-particle.png");
        expect(pixels == render(renderer, test::materialize_world(frame)), "Surface sprite pixels must equal the original CPU clipping/projection path for Flags2/6");
        const auto pixel = pixels[240 * 640 + 320];
        const std::array<int, 3> expected = flags == 6 ? std::array{88, 88, 106} : std::array{66, 57, 60};
        for (unsigned channel = 0; channel < 3; ++channel) {
            expect(
                std::abs(int((pixel >> ((2 - channel) * 8)) & 255) - expected[channel]) <= 2, "Actual GPU must use ONE/INVSRCCOLOR for Flags6 and SRCALPHA/INVSRCALPHA for Flags2");
        }
        frame.quad({.depth = true}, 250, 150, 140, 180, 0xff0000ff, .99f);
        const auto overwritten = render(renderer, frame);
        expect(overwritten[240 * 640 + 320] == 0xff0000ff, "Surface sprites must not write depth before later triangles");
        gpu::Frame front;
        front.quad({.depth = true}, 0, 0, 640, 480, 0xffff0000, .5f);
        const auto baseline = render(renderer, front);
        view.append(front, camera, effects);
        expect(render(renderer, front) == baseline, "Source surface sprites must test existing opaque geometry depth");
    }
}

void canonical_assets(const AssetStore& store, gpu::Renderer& renderer, const CameraInputs& camera) {
    // Independent4463e0 directory/track audit, CRC32 IEEE/zlib over original BMPs.
    constexpr std::array<std::array<std::uint32_t, 2>, 10> terrain{{{0x24d323f0, 0x149dca20}, {0x950c9627, 0x1e026c9b}, {0x24d323f0, 0x149dca20}, {0x7c5ca251, 0x66930bc7},
        {0x7c5ca251, 0x66930bc7}, {0x24d323f0, 0x149dca20}, {0x7a85f0ec, 0x66930bc7}, {0x24d323f0, 0x149dca20}, {0x7c5ca251, 0x66930bc7}, {0x7a85f0ec, 0x66930bc7}}};
    for (std::uint32_t track = 0; track < 10; ++track) {
        for (std::uint32_t weather = 1; weather <= 5; ++weather) {
            const SceneSetup setup{{.track_id = track}, weather, 0};
            const auto resources = load_surface_effect_resources(store, setup);
            const std::array<std::uint32_t, 6> expected{0x5e4adea6, terrain[track][0], 0xf44ac585, 0xe45cf274, terrain[track][1], 0x67a4c1d6};
            for (std::size_t texture = 0; texture < resources.textures.size(); ++texture) {
                expect(crc32(resources.textures[texture]) == expected[texture], "Track/weather selection must retain the actual original surface texture family");
            }
        }
    }
    const auto resources = load_surface_effect_resources(store, SceneSetup{{.track_id = 0}, 3, 0});
    const SurfaceEffectsView view(renderer, resources);
    std::vector<std::vector<std::uint32_t>> images;
    for (const auto texture : {SurfaceEffectTexture::smoke, SurfaceEffectTexture::dust, SurfaceEffectTexture::spray, SurfaceEffectTexture::mud, SurfaceEffectTexture::gravel,
             SurfaceEffectTexture::grass}) {
        const auto effects = single(texture == SurfaceEffectTexture::spray ? 6 : 2, texture);
        gpu::Frame frame;
        view.append(frame, camera, effects);
        const auto name = "surface-texture-" + std::to_string(int(texture)) + ".png";
        images.push_back(render(renderer, frame, name.c_str()));
        expect(visible(images.back()) > 100, "Original surface texture must produce visible keyed/blended GPU pixels");
    }
    expect(images[0] != images[1] && images[1] != images[2] && images[2] != images[3] && images[3] != images[4] && images[4] != images[5],
        "Distinct original smoke/dust/spray/mud/gravel/grass art must reach real pixels");
}

SurfaceEffectVehicle emitting_vehicle(std::uint8_t surface, std::int32_t slip) {
    SurfaceEffectVehicle result;
    result.position = {0, 0, 6 * one};
    result.velocity = {one, 0, 0};
    result.body_radius = 2 * one;
    for (std::size_t wheel = 0; wheel < 4; ++wheel) {
        auto& target = result.wheels[wheel];
        target.position = {wheel < 2 ? one : -one, 0, 6 * one + (wheel % 2 == 0 ? -one : one)};
        target.forward = {one, 0, 0};
        target.side = {0, 0, one};
        target.surface = surface;
        target.longitudinal_slip = slip;
    }
    return result;
}

void emitted_motion(const AssetStore& store, gpu::Renderer& renderer, const CameraInputs& camera) {
    for (const auto kind : {0u, 1u, 2u}) {
        // Same source-validated contact fixtures as the CPU test, translated
        // six units forward so that every authored wheel is in this camera.
        PropFragmentTables tables;
        tables.positive.fill(one / 2);
        const SceneSetup setup{{.track_id = 0}, kind + 1, 0};
        SurfaceEffects effects(tables, setup);
        const SurfaceEffectsView view(renderer, load_surface_effect_resources(store, setup));
        std::array cars{emitting_vehicle(kind == 1 ? 1 : 0, kind == 1 ? 0 : one)};
        if (kind == 1) {
            cars[0].wheels[0].surface = 3;
            cars[0].wheels[0].longitudinal_slip = -one;
        }
        effects.advance(cars);
        effects.emit(cars);
        const auto initial = effects.frame();
        const auto expected = kind == 0 ? 1u : kind == 1 ? (1u << 1) | (1u << 4) : 1u << 2;
        expect(initial.kind_mask == expected, "Actual vehicle contact/slip must select smoke, gravel/dust or wet spray before GPU submission");
        gpu::Frame before;
        view.append(before, camera, initial);
        const auto prefix = "surface-emitted-" + std::to_string(kind);
        const auto pixels = render(renderer, before, (prefix + "-initial.png").c_str());
        expect(visible(pixels) > 25, "Actual source-emitted particles must reach visible original-texture GPU pixels");
        if (kind == 2) {
            cars[0].position[0] += one;
            for (auto& wheel : cars[0].wheels) {
                wheel.position[0] += one;
            }
            effects.advance(cars);
            effects.emit(cars);
        } else {
            for (int tick = 0; tick < 4; ++tick) {
                effects.advance(cars);
            }
        }
        gpu::Frame after;
        view.append(after, camera, effects.frame());
        const auto moved = render(renderer, after, (prefix + "-moved.png").c_str());
        std::size_t differences = 0;
        for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel) {
            differences += pixels[pixel] != moved[pixel];
        }
        expect(differences > 25, "Explicit source updates must visibly move/grow/fade emitted particles or follow the wet vehicle");
        effects.reset();
        gpu::Frame cleared;
        cleared.quad({}, 0, 0, 640, 480, gpu::background);
        view.append(cleared, camera, effects.frame());
        expect(visible(render(renderer, cleared)) == 0, "Original grid reset must remove every emitted GPU particle and vehicle-bound plume");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 2, "Expected canonical data root");
        const AssetStore store(argv[1]);
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private surface effects fixture");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA surface effects", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private surface effects GPU window");
        gpu::Renderer renderer(window.get());
        const CameraInputs camera{identity, default_projection(3, 640, 480)};
        const SurfaceEffectsView view(renderer, synthetic_resources());
        source_fields(view, camera);
        blend_and_depth(renderer, view, camera);
        canonical_assets(store, renderer, camera);
        emitted_motion(store, renderer, camera);
        std::cout << "PASS: original surface texture families, source batch limit, alpha/screen blend and depth states through real GPU\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
