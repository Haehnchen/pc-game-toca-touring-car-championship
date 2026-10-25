#include "assets/store.hpp"
#include "core/random.hpp"
#include "scene/weather_render.hpp"
#include "simulation/fixed.hpp"
#include "world_geometry.hpp"

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

std::vector<std::uint32_t> render(gpu::Renderer& renderer, const gpu::Frame& frame, const char* name = nullptr) {
    expect(renderer.draw(frame, false), "Weather fixture requires its private drawable");
    if (name) {
        renderer.save_screenshot(name);
    }
    const auto pixels = renderer.readback();
    if (!frame.dynamic_world_vertices.empty() || !frame.dynamic_world_lines.empty()) {
        auto reference = test::materialize_world(frame);
        if (reference.draws.empty()) {
            reference.quad({}, 0, 0, 640, 480, gpu::background);
        }
        expect(renderer.draw(reference, false), "CPU weather presentation reference target unavailable");
        expect(pixels == renderer.readback(), "Weather source geometry pixels must exactly equal CPU line/triangle clipping and projection");
    }
    return pixels;
}

std::size_t visible(const std::vector<std::uint32_t>& pixels) {
    return std::ranges::count_if(pixels, [](auto color) {
        return color != gpu::background;
    });
}

void source_geometry(const CameraInputs& camera, const WeatherView& view) {
    WeatherSprite sprite{{0, 0, 4 * one}, {-one / 4, -one / 2, 0}, {one / 4, one / 2, 0}};
    const auto vertices = weather_sprite_vertices(sprite, weather_camera(camera.world_to_view));
    expect(vertices[0].position == std::array<float, 3>{-.5f, -.25f, 4} && vertices[3].position == std::array<float, 3>{.5f, .25f, 4} && vertices[0].u == 0 && vertices[0].v == 1
               && vertices[3].u == 1 && vertices[3].v == 0 && vertices[0].color == 0xfeffffff,
        "497070 must retain vertical/horizontal source word order, UV winding and alpha254/brightness255");
    WeatherFrame sprites;
    sprites.sprites.push_back(sprite);
    gpu::Frame submitted;
    view.append_sprites(submitted, camera, sprites);
    expect(submitted.vertices.empty() && submitted.indices.empty() && submitted.dynamic_world_vertices.size() == 6 && submitted.draws.size() == 1
               && submitted.draws[0].dynamic_world && !submitted.draws[0].mesh,
        "Weather billboards must submit six prelit source corners without CPU projection or resident meshes");
    constexpr std::array order{0u, 3u, 2u, 0u, 1u, 3u};
    for (std::size_t corner = 0; corner < order.size(); ++corner) {
        const auto& source = vertices[order[corner]];
        const auto& actual = submitted.dynamic_world_vertices[corner];
        expect(actual.position == source.position && actual.color == source.color && actual.u == source.u && actual.v == source.v,
            "Weather GPU submission must retain source triangle order, packed shade and UVs");
    }
    WeatherFrame crossing;
    crossing.lines.push_back({{-1000, 0, 0x1999}, {1000, 0, one}, 0xff969696});
    gpu::Frame line;
    view.append_lines(line, camera, crossing);
    expect(line.vertices.empty() && line.indices.empty() && line.dynamic_world_vertices.empty() && line.dynamic_world_lines.size() == 2 && line.draws.size() == 1
               && line.draws[0].dynamic_world && !line.draws[0].mesh && line.draws[0].first == 0 && line.draws[0].count == 2 && line.draws[0].vertex_offset == 0
               && line.draws[0].state.lines && line.draws[0].state.depth && line.draws[0].state.depth_write && line.draws[0].state.blend,
        "Rain must submit two source endpoints as a hardware line with inherited depth writes and no CPU projection");
    for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
        const auto& source = endpoint ? crossing.lines[0].to : crossing.lines[0].from;
        const auto& actual = line.dynamic_world_lines[endpoint];
        const std::array<float, 3> position{float(source[0]) / one, float(source[1]) / one, float(source[2]) / one};
        expect(actual.position == position && actual.color == (endpoint ? 0x00969696u : 0xff969696u) && actual.u == 0 && actual.v == 0,
            "Rain source stream must retain Q16 endpoint order, RGB, fade and zero UVs");
    }
    const auto clipped = test::materialize_world(line);
    expect(clipped.vertices.size() == 2 && clipped.vertices[0].z == 0 && clipped.vertices[0].rhw == 4 && (clipped.vertices[0].diffuse >> 24) < 255
               && clipped.vertices[1].diffuse == 0x00969696,
        "Rain near clipping must interpolate its opaque-to-transparent fade");
    crossing.lines[0] = {{0, 0, -one}, {0, 0, -one / 2}, 0xff969696};
    gpu::Frame hidden;
    view.append_lines(hidden, camera, crossing);
    expect(hidden.dynamic_world_lines.size() == 2 && test::materialize_world(hidden).vertices.empty(),
        "Rain behind near must retain source endpoints while GPU clipping rejects its projected geometry");
    crossing.lines[0].color = 0xff123456;
    view.append_lines(hidden, camera, crossing);
    expect(hidden.draws.size() == 1 && hidden.draws[0].count == 4 && hidden.dynamic_world_lines.size() == 4 && hidden.dynamic_world_lines[2].color == 0xff123456
               && hidden.dynamic_world_lines[3].color == 0x00123456,
        "Consecutive rain lines must merge their material without losing per-line packed colors or source order");
}

void gpu_states(gpu::Renderer& renderer, const CameraInputs& camera, const WeatherView& view) {
    WeatherFrame line;
    line.lines.push_back({{0, one, 4 * one}, {0, -one, 4 * one}, 0xff969696});
    gpu::Frame line_frame;
    view.append_lines(line_frame, camera, line);
    const auto rain = render(renderer, line_frame, "weather-rain-line.png");
    expect(visible(rain) > 100 && visible(rain) < 400, "Rain must rasterize a thin hardware line with a visible alpha gradient");
    gpu::Frame large_scene;
    large_scene.vertices.resize(65535);
    view.append_lines(large_scene, camera, line);
    view.append_lines(large_scene, camera, line);
    expect(large_scene.vertices.size() == 65535 && large_scene.indices.empty() && large_scene.dynamic_world_lines.size() == 4 && large_scene.draws.size() == 1
               && large_scene.draws[0].dynamic_world && large_scene.draws[0].first == 0 && large_scene.draws[0].count == 4 && large_scene.draws[0].vertex_offset == 0,
        "Rain after a large scene must retain an independent contiguous world-line stream");
    auto repeated_rain = line_frame;
    view.append_lines(repeated_rain, camera, line);
    expect(render(renderer, large_scene) == render(renderer, repeated_rain), "Rain pixels changed when appended after a large Mega scene");
    for (const auto depth : {one / 4 - one / 64, one / 4, one / 4 + one / 64}) {
        WeatherFrame near_line;
        near_line.lines.push_back({{0, one / 32, depth}, {0, -one / 32, depth}, 0xff969696});
        gpu::Frame near_frame;
        view.append_lines(near_frame, camera, near_line);
        expect(near_frame.dynamic_world_lines.size() == 2, "Near-plane rain rejection must happen after source queue submission");
        const auto reference = test::materialize_world(near_frame);
        expect((depth < one / 4 ? reference.vertices.empty() : reference.vertices.size() == 2),
            "Hardware-line near clipping must reject below near and retain endpoints exactly on near");
        const auto pixels = render(renderer, near_frame);
        expect(depth < one / 4 ? visible(pixels) == 0 : visible(pixels) > 20, "Rain below, on and above near must match the CPU line visibility rule");
    }
    for (const auto reversed : {false, true}) {
        WeatherFrame crossing;
        const WeatherVector outside{-one / 32, one / 32, one / 8}, inside{one / 32, -one / 32, one / 2};
        crossing.lines.push_back({reversed ? inside : outside, reversed ? outside : inside, 0xff969696});
        gpu::Frame crossing_frame;
        view.append_lines(crossing_frame, camera, crossing);
        expect(visible(render(renderer, crossing_frame)) > 20, "Rain crossing near in either endpoint order must retain clipped pixels and fade");
    }
    const auto row_energy = [&](unsigned y) {
        unsigned sum = 0;
        for (unsigned x = 318; x <= 321; ++x) {
            sum += (rain[y * 640 + x] >> 16) & 255;
        }
        return sum;
    };
    expect(row_energy(170) > row_energy(300), "Original first endpoint must be opaque and the second transparent");

    gpu::Frame obstacle;
    obstacle.quad({.depth = true}, 280, 130, 80, 85, 0xffff0000, .5f);
    const auto baseline = render(renderer, obstacle);
    view.append_lines(obstacle, camera, line);
    const auto occluded_line = render(renderer, obstacle, "weather-rain-occluded.png");
    expect(occluded_line != baseline, "Visible rain outside the occluder disappeared");
    for (unsigned y = 140; y < 210; ++y) {
        for (unsigned x = 300; x < 340; ++x) {
            expect(occluded_line[y * 640 + x] == baseline[y * 640 + x], "Rain must test existing geometry depth");
        }
    }

    // The real snow image exercises keying and depth without a substitute art.
    WeatherFrame sprite;
    sprite.sprites.push_back({{0, 0, 4 * one}, {-one, -one, 0}, {one, one, 0}});
    gpu::Frame sprite_frame;
    view.append_sprites(sprite_frame, camera, sprite);
    expect(!sprite_frame.draws.empty()
               && std::ranges::all_of(sprite_frame.draws,
                   [](const auto& draw) {
                       return draw.state.depth && !draw.state.depth_write && draw.state.blend && draw.state.key && !draw.state.lines && !draw.state.fog;
                   }),
        "Weather sprites must test depth, preserve black key and bypass fog/depth writes");
    const auto snow = render(renderer, sprite_frame, "weather-snow-sprite.png");
    expect(snow == render(renderer, test::materialize_world(sprite_frame)), "Weather sprite pixels must equal the original CPU clipping/projection path");
    expect(visible(snow) > 100, "Original snow texture did not produce keyed GPU pixels");
    for (const auto depth : {one / 4 - one / 64, one / 4, one / 4 + one / 64}) {
        WeatherFrame near_sprite;
        near_sprite.sprites.push_back({{0, 0, depth}, {-one / 32, -one / 32, 0}, {one / 32, one / 32, 0}});
        gpu::Frame near_frame;
        near_frame.quad({}, 0, 0, 640, 480, gpu::background);
        view.append_sprites(near_frame, camera, near_sprite);
        expect(near_frame.dynamic_world_vertices.size() == 6, "Near-plane weather rejection must remain in the GPU after source queue submission");
        expect(render(renderer, near_frame) == render(renderer, test::materialize_world(near_frame)),
            "Weather sprites below, on and above near must match original CPU rejection and depth projection");
    }
    std::size_t covered_snow = 0;
    for (unsigned y = 160; y < 320; ++y) {
        for (unsigned x = 260; x < 380; ++x) {
            covered_snow += snow[y * 640 + x] != gpu::background;
        }
    }
    expect(covered_snow > 100, "Depth-write probe must cover actual opaque snow texels");
    sprite_frame.quad({.depth = true}, 250, 150, 140, 180, 0xff0000ff, .99f);
    const auto overwritten = render(renderer, sprite_frame, "weather-sprite-no-depth-write.png");
    for (unsigned y = 160; y < 320; ++y) {
        for (unsigned x = 260; x < 380; ++x) {
            expect(overwritten[y * 640 + x] == 0xff0000ff, "497180 sprites must leave depth available to the later triangle pass");
        }
    }
    gpu::Frame front;
    front.quad({.depth = true}, 0, 0, 640, 480, 0xffff0000, .5f);
    const auto front_only = render(renderer, front);
    view.append_sprites(front, camera, sprite);
    const auto covered = render(renderer, front, "weather-sprite-occluded.png");
    expect(covered == front_only, "Weather sprites behind nearer opaque geometry must remain occluded");

    auto line_writes = line_frame;
    line_writes.quad({.depth = true}, 280, 130, 80, 210, 0xff0000ff, .99f);
    const auto retained = render(renderer, line_writes);
    expect(std::ranges::count_if(retained,
               [](auto color) {
                   return color != gpu::background && color != 0xff0000ff;
               })
               > 100,
        "47cb80 lines must retain the depth writes restored after the sprite pass");

    WeatherFrame flash;
    flash.lightning_alpha = 180;
    gpu::Frame lightning;
    view.append_lightning(lightning, camera, flash);
    const auto bright = render(renderer, lightning, "weather-lightning.png");
    expect(visible(bright) == bright.size() && ((bright.front() >> 16) & 255) > 180, "Original fullscreen white lightning alpha did not reach the GPU");
}

void moving_effects(gpu::Renderer& renderer, const CameraInputs& camera, const WeatherView& view) {
    for (const auto kind : {0u, 1u, 2u, 3u}) {
        SceneSetup setup{};
        setup.weather = kind == 1 ? 4 : kind == 3 ? 5 : 3;
        setup.selection.cat_dog = kind == 2;
        SourceRandom random(7);
        auto tables = make_prop_fragment_tables(random);
        Weather weather(tables, setup, random, weather_camera(camera.world_to_view));
        weather.advance();
        weather.update_view(weather_camera(camera.world_to_view));
        const auto frame = [&] {
            gpu::Frame result;
            view.append_sprites(result, camera, weather.frame());
            view.append_lines(result, camera, weather.frame());
            return result;
        };
        const auto initial = frame();
        const auto before = render(renderer, initial);
        expect(visible(before) > 20, "Original precipitation state did not generate visible GPU pixels");
        for (unsigned tick = 0; tick < 3; ++tick) {
            weather.advance();
            weather.update_view(weather_camera(camera.world_to_view));
        }
        const auto after = render(renderer, frame());
        expect(after != before, "Original precipitation updates must visibly move rain/snow/CatsDogs/storm geometry");
        if (kind == 2) {
            auto dogs = weather.frame(), cats = dogs;
            for (auto& sprite : dogs.sprites) {
                sprite.texture = WeatherTexture::dogs;
            }
            for (auto& sprite : cats.sprites) {
                sprite.texture = WeatherTexture::cats;
            }
            gpu::Frame dog_frame, cat_frame;
            view.append_sprites(dog_frame, camera, dogs);
            view.append_sprites(cat_frame, camera, cats);
            expect(render(renderer, dog_frame, "weather-dogs.png") != render(renderer, cat_frame, "weather-cats.png"),
                "Original Cats/Dogs texture branches must change actual pixels");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 2, "Expected canonical data root");
        const AssetStore store(argv[1]);
        const auto resources = load_weather_resources(store);
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private weather GPU fixture");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA weather effects", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private weather GPU window");
        gpu::Renderer renderer(window.get());
        const WeatherView view(renderer, resources);
        const CameraInputs camera{identity, default_projection(3, 640, 480)};
        source_geometry(camera, view);
        gpu_states(renderer, camera, view);
        moving_effects(renderer, camera, view);
        std::cout << "PASS: original weather lines, sprite textures, motion, occlusion, depth-write separation and lightning through real GPU\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
