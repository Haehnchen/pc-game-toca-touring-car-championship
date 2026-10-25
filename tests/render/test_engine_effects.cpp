#include "assets/store.hpp"
#include "scene/engine_effects_render.hpp"
#include "world_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::int32_t one = 65536;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EngineEffectResources synthetic_resources() {
    // Runtime-only1x1 RGB BMP isolates Flags6 blending without a surrogate asset.
    std::vector<std::uint8_t> bmp(58);
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            bmp[offset + byte] = std::uint8_t(value >> (8 * byte));
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
    return {std::move(bmp)};
}

EngineEffectFrame single() {
    EngineEffectFrame result;
    result.sprites.push_back({{0, 0, 4 * one}, {-one, -one, 0}, {one, one, 0}, 128, 0});
    result.visibility.push_back(1);
    return result;
}

std::vector<std::uint32_t> render(gpu::Renderer& renderer, const gpu::Frame& frame, const std::string& name = {}) {
    auto submitted = frame;
    if (submitted.draws.empty()) {
        submitted.quad({}, 0, 0, 1, 1, gpu::background);
    }
    expect(renderer.draw(submitted, false), "Engine effects require their private GPU drawable");
    if (!name.empty()) {
        renderer.save_screenshot(name);
    }
    return renderer.readback();
}

std::size_t visible(const std::vector<std::uint32_t>& pixels) {
    return std::ranges::count_if(pixels, [](auto color) {
        return color != gpu::background;
    });
}

void masks_budget_and_order(gpu::Renderer& renderer, const EngineEffectsView& view, const CameraInputs& camera) {
    auto effects = single();
    effects.sprites.push_back(effects.sprites[0]);
    effects.sprites.back().center[0] = one;
    effects.visibility.push_back(2);
    gpu::Frame selected, other, rejected;
    expect(view.append(selected, camera, effects, 126) == 127 && selected.dynamic_world_vertices.size() == 6 && selected.vertices.empty() && selected.draws.size() == 1
               && selected.draws[0].dynamic_world && !selected.draws[0].mesh,
        "BANG2 must filter the original view masks before admitting the complete batch into slot127");
    expect(view.append(other, camera, effects, 126, 2) == 127 && other.dynamic_world_vertices.size() == 6 && other.draws.size() == 1
               && selected.dynamic_world_vertices[0].position[0] < other.dynamic_world_vertices[0].position[0],
        "A different view bit must select the other original sprite before queue admission");
    effects.visibility[1] = 1;
    expect(view.append(rejected, camera, effects, 126) == 126 && rejected.draws.empty(), "An overflowing BANG2 batch must be discarded in full");
    gpu::Frame full;
    expect(view.append(full, camera, single(), 127) == 127 && full.draws.empty(), "The source127-sprite queue cannot admit an extra exhaust sprite");
    auto close = single();
    close.sprites[0] = {{0, 0, one}, {-2 * one, -2 * one, 0}, {2 * one, 2 * one, 0}, 200, 0};
    gpu::Frame clipped;
    expect(view.append(clipped, camera, close) == 1 && clipped.draws.empty(), "497180 close-camera rejection must still consume the source sprite slot");

    SurfaceEffectResources wheel_resources;
    wheel_resources.textures.fill(synthetic_resources().bang_bmp);
    const SurfaceEffectsView wheels(renderer, wheel_resources);
    const SurfaceEffectBatch spray{SurfaceEffectTexture::spray, 6, single().sprites}, mud{SurfaceEffectTexture::mud, 2, single().sprites};
    gpu::Frame ordered;
    auto queued = wheels.append_batch(ordered, camera, spray);
    queued = view.append(ordered, camera, single(), queued);
    queued = wheels.append_batch(ordered, camera, mud, queued);
    expect(queued == 3 && ordered.dynamic_world_vertices.size() == 18 && ordered.draws.size() == 3 && ordered.draws[0].state.screen && ordered.draws[1].state.screen
               && !ordered.draws[2].state.screen && ordered.draws[0].state.texture != ordered.draws[1].state.texture
               && ordered.draws[1].state.texture != ordered.draws[2].state.texture,
        "446f20's independent spray/BANG2/mud submissions must retain their exact draw order, texture and blend mode");
    gpu::Frame limited;
    queued = wheels.append_batch(limited, camera, spray, 125);
    queued = view.append(limited, camera, single(), queued);
    queued = wheels.append_batch(limited, camera, mud, queued);
    expect(queued == 127 && limited.dynamic_world_vertices.size() == 12 && limited.draws.size() == 2, "Exhaust and wheel sprite batches must share the same bounded queue");
}

void blend_depth_pixels(gpu::Renderer& renderer, const EngineEffectsView& view, const CameraInputs& camera) {
    gpu::Frame frame;
    frame.quad({}, 0, 0, 640, 480, 0xff204060);
    view.append(frame, camera, single());
    const auto& state = frame.draws.back().state;
    expect(state.depth && !state.depth_write && state.blend && state.key && state.screen && state.texture,
        "Original hardware BANG2 must be keyed, screen blended and depth tested without depth writes");
    const auto pixels = render(renderer, frame, "engine-screen.png");
    expect(pixels == render(renderer, test::materialize_world(frame)), "Engine sprite pixels must equal the original CPU clipping/projection path");
    const auto center = pixels[240 * 640 + 320];
    constexpr std::array expected{88, 88, 106};
    for (unsigned channel = 0; channel < 3; ++channel) {
        expect(
            std::abs(int((center >> (8 * (2 - channel))) & 255) - expected[channel]) <= 2, "Actual BANG2 pixels must use ONE/INVSRCCOLOR with source shade0 as RGB and alpha255");
    }
    frame.quad({.depth = true}, 250, 150, 140, 180, 0xff0000ff, .99f);
    expect(render(renderer, frame)[240 * 640 + 320] == 0xff0000ff, "Engine sprites cannot leave depth writes that occlude later triangles");
    gpu::Frame foreground;
    foreground.quad({.depth = true}, 0, 0, 640, 480, 0xffff0000, .5f);
    const auto baseline = render(renderer, foreground);
    view.append(foreground, camera, single());
    expect(render(renderer, foreground) == baseline, "Engine sprites must be hidden by existing foreground depth");
}

void generated_flames(gpu::Renderer& renderer, const EngineEffectsView& view, const CameraInputs& camera) {
    PropFragmentTables tables;
    tables.positive.fill(one / 2);
    EngineEffects effects(tables);
    for (int tick = 0; tick < 902; ++tick) {
        effects.advance();
    }
    EngineEffectVehicle car;
    // Translate the actual Honda anchor to the camera center, without changing
    // source-emitted geometry, expansion, brightness or transformation math.
    car.position = {148307, 27656, 4 * one - 23789};
    car.engine_speed_q16 = one;
    car.gear = 1;
    const std::array cars{car};
    effects.emit(cars);
    const auto image = [&](const std::string& name) {
        gpu::Frame frame;
        view.append(frame, camera, effects.frame());
        return render(renderer, frame, name);
    };
    const auto first = image("engine-source-first.png");
    expect(effects.frame().sprites.size() == 1 && effects.frame().sounds.size() == 1 && visible(first) > 4,
        "The actual source RPM trigger must emit one visible original BANG2 sprite and its event17");
    effects.advance();
    effects.emit(cars);
    const auto second = image("engine-source-second.png");
    expect(second != first && visible(second) > 4 && effects.frame().sounds.empty(), "The second source tick must move/expand/fade actual exhaust pixels without repeating audio");
    effects.advance();
    effects.emit(cars);
    const auto third = image("engine-source-third.png");
    expect(third != second && visible(third) > 4, "The final source phase must retain its own radius, offset and screen brightness");
    effects.advance();
    effects.emit(cars);
    expect(visible(image("engine-source-expired.png")) == 0, "The original three-call burst must leave no persistent smoke geometry");

    EngineEffects special(tables, GameMode::time_trial);
    car.car_type = 8;
    car.position = {149356, -6553, 4 * one};
    car.gear = 0;
    car.engine_speed_q16 = 0;
    const std::array special_cars{car};
    special.emit(special_cars);
    gpu::Frame twin;
    view.append(twin, camera, special.frame());
    expect(special.frame().sprites.size() == 2 && visible(render(renderer, twin, "engine-source-type8-time-trial.png")) > 10,
        "TimeTrial type8's two source sprites must reach the original-texture GPU path even with a cold neutral engine");
    special.reset();
    gpu::Frame cleared;
    view.append(cleared, camera, special.frame());
    expect(visible(render(renderer, cleared)) == 0, "Original grid reset must clear every pending/visible engine sprite");
}

void aspects(SDL_Window* window, gpu::Renderer& renderer, const EngineEffectsView& view) {
    float previous_ratio = 0;
    for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
        gpu::check(SDL_SetWindowSize(window, size[0], size[1]), "Resize private engine effects target");
        gpu::check(SDL_SyncWindow(window), "Synchronize private engine effects target");
        SDL_PumpEvents();
        const CameraInputs camera{identity, default_projection(3, size[0], size[1])};
        gpu::Frame frame;
        view.append(frame, camera, single());
        const auto pixels = render(renderer, frame, "engine-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
        expect(pixels.size() == std::size_t(size[0] * size[1]), "Engine sprite target must follow the real drawable size");
        int left = size[0], right = -1, top = size[1], bottom = -1;
        for (int y = 0; y < size[1]; ++y) {
            for (int x = 0; x < size[0]; ++x) {
                if (pixels[std::size_t(y * size[0] + x)] != gpu::background) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
            }
        }
        expect(right > left && bottom > top, "Original exhaust billboard must reach visible pixels at each supported aspect");
        const auto ratio = float(right - left + 1) / float(bottom - top + 1);
        expect(!previous_ratio || std::abs(previous_ratio - ratio) < .03f, "Shared wide projection must preserve engine billboard shape");
        previous_ratio = ratio;
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc <= 2, "Usage: engine-effects-render-tests [canonical-root]");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private engine effects fixture");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA exhaust flames", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private engine effects GPU window");
        gpu::Renderer renderer(window.get());
        const CameraInputs camera{identity, default_projection(3, 640, 480)};
        const EngineEffectsView synthetic(renderer, synthetic_resources());
        masks_budget_and_order(renderer, synthetic, camera);
        blend_depth_pixels(renderer, synthetic, camera);
        if (argc == 2) {
            const EngineEffectsView canonical(renderer, load_engine_effect_resources(AssetStore(argv[1])));
            generated_flames(renderer, canonical, camera);
        }
        aspects(window.get(), renderer, synthetic);
        std::cout << "PASS original exhaust sprite masks, screen pixels, depth, shared127 budget, source updates and aspects\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
