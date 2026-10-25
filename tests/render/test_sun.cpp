#include "assets/store.hpp"
#include "core/crc32.hpp"
#include "image/decode.hpp"
#include "scene/sun_render.hpp"

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

std::string checksum(std::span<const std::uint8_t> bytes) {
    std::ostringstream text;
    text << std::hex << std::setw(8) << std::setfill('0') << toca::crc32(bytes);
    return text.str();
}

void budget(const SunResources& resources, const char* label) {
    std::size_t encoded = 0, pixels = 0;
    for (std::size_t index = 0; index < resources.textures.size(); ++index) {
        const auto& texture = resources.textures[index];
        const auto side = lens_dimensions[index] * (texture.encoding == ImageEncoding::webp ? 2u : 1u);
        pixels += side * side;
        encoded += texture.bmp.size();
    }
    expect(resources.textures.size() == 7 && pixels * 4 <= 512 * 1024, "Selected seven-lens set exceeded its half-MiB RGBA8 budget");
    std::ofstream report("sun-budget.csv", std::ios::app);
    report << label << ',' << resources.textures.size() << ',' << encoded << ',' << pixels * 4 << '\n';
    std::cout << "BUDGET lenses=" << label << " resident_materials=" << resources.textures.size() << " encoded_bytes=" << encoded << " rgba8_bytes=" << pixels * 4 << '\n';
}

std::vector<std::uint32_t> draw(gpu::Renderer& renderer, const SunView& view, const CameraInputs& camera, bool linear, const char* screenshot) {
    gpu::Frame scene;
    scene.quad({.depth = true}, 0, 0, 640, 480, 0xff204060, 0);
    view.append(scene, camera, 0);
    expect(scene.draws.size() == 8, "Visible sun projection must submit seven lens sprites after world geometry");
    expect(!scene.draws.front().overlay, "Appending lenses must preserve the world's rendering phase");
    for (std::size_t index = 1; index < scene.draws.size(); ++index) {
        const auto& state = scene.draws[index].state;
        expect(scene.draws[index].overlay && state.blend && state.screen && !state.depth && !state.depth_write && !state.key && state.texture && state.linear == linear,
            "Lenses must retain RGB screen blending, original depth/alpha semantics and authored sampling");
    }
    gpu::check(renderer.draw(scene, false), "Draw lens screen blend");
    const auto pixels = renderer.readback();
    std::size_t bright = 0, unchanged = 0;
    for (const auto color : pixels) {
        if ((color & 0xffffff) == 0x204060) {
            ++unchanged;
        } else {
            ++bright;
            expect((color & 255) >= 0x60 && ((color >> 8) & 255) >= 0x40 && ((color >> 16) & 255) >= 0x20, "Screen blend must never darken the world beneath a lens");
        }
    }
    expect(bright > 100 && unchanged > 640 * 400, "Lens images must brighten localized pixels while RGB black outside remains neutral");
    renderer.save_screenshot(screenshot);
    scene.multisample = true;
    gpu::check(renderer.draw(scene, false), "Draw screen lenses after a requested multisample resolve");
    expect(renderer.readback() == pixels, "Resolving a uniformly colored world must preserve the exact one-sample screen lens overlay");
    return pixels;
}

SunResources replacements(const SunResources& source, const std::filesystem::path& directory) {
    SceneResources scene;
    scene.setup = resolve_setup({.weather_choice = 1, .detail = 4});
    scene.sun = source;
    apply_mega_scene_resources(scene, directory);
    expect(scene.sun.textures.size() == source.textures.size(), "Mega changed the seven original lens slots");
    for (std::size_t index = 0; index < source.textures.size(); ++index) {
        const auto& original = source.textures[index];
        const auto& actual = scene.sun.textures[index];
        const auto path = directory / "scene" / (checksum(original.bmp) + ".webp");
        std::ifstream stream(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        expect(actual.name == original.name && actual.encoding == ImageEncoding::webp && !bytes.empty() && actual.bmp == bytes,
            "Lens replacement fell back or differs from the exact source-CRC32 bundle file");
        const auto surface = toca::decode_model_texture(actual);
        expect(surface->w == int(lens_dimensions[index] * 2) && surface->h == surface->w && bytes.size() <= 256 * 1024,
            "Authored lens must double the source size within the256px/256KiB limit");
        std::size_t black = 0;
        for (int y = 0; y < surface->h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(surface->pixels) + y * surface->pitch;
            for (int x = 0; x < surface->w; ++x) {
                expect(row[x * 4 + 3] == 255, "Screen lens RGB pixels must stay opaque, including neutral black");
                black += row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2] < 6;
            }
        }
        const auto before = toca::decode_model_texture(original);
        gpu::Surface rgba(SDL_ConvertSurface(before.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        expect(bool(rgba), "Cannot inspect original lens RGB black area");
        std::size_t source_black = 0;
        for (int y = 0; y < rgba->h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(rgba->pixels) + y * rgba->pitch;
            for (int x = 0; x < rgba->w; ++x) {
                source_black += row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2] < 6;
            }
        }
        const auto fraction = double(black) / (surface->w * surface->h);
        const auto source_fraction = double(source_black) / (rgba->w * rgba->h);
        expect(black > 0 && fraction + .16 >= source_fraction, "Authored optical image lost its original neutral black surrounding area");
        std::cout << "LENS " << original.name << ' ' << path.string() << " output_crc32=" << checksum(bytes) << " bytes=" << bytes.size() << '\n';
    }
    return scene.sun;
}

void session_gates(gpu::Renderer& renderer, const SunResources& resources, const CameraInputs& camera) {
    SceneResources scene;
    scene.setup = resolve_setup({.weather_choice = 1, .detail = 4});
    scene.sun = resources;
    const SceneView single_view(renderer, scene, nullptr, nullptr, nullptr, {}, nullptr, nullptr, true);
    const SceneView split_view(renderer, scene, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false);
    const SceneView trial_view(renderer, scene, nullptr, nullptr, nullptr, {}, nullptr, nullptr, false);
    const auto single = single_view.frame(camera, scene_lighting(scene.setup), {identity}, {}, {});
    const auto split = split_view.frame(camera, scene_lighting(scene.setup), {identity}, {}, {});
    const auto trial = trial_view.frame(camera, scene_lighting(scene.setup), {identity}, {}, {});
    expect(single.draws.size() == 7 && split.draws.empty() && trial.draws.empty(),
        "Whole SceneView must emit all seven single-player flares and suppress them through the split/TimeTrial constructor gate");
    expect(single.multisample && std::ranges::all_of(single.draws, &gpu::Draw::overlay),
        "Mega SceneView must keep its lens sprites in the overlay phase after the sampled world resolves");
    gpu::Frame composed;
    composed.append_view(single, {0, 0, 640, 480});
    expect(composed.multisample && std::ranges::all_of(composed.draws, &gpu::Draw::overlay),
        "Composing the whole view must retain both the Mega sampling request and the lens overlay phase");
    gpu::check(renderer.draw(single, false), "Draw enabled single-player sun session");
    const auto enabled_pixels = renderer.readback();
    expect(std::ranges::count_if(enabled_pixels,
               [](auto pixel) {
                   return pixel != gpu::background;
               })
               > 100,
        "Full enabled SceneView must draw the actual lens texture pixels");
    for (const auto* disabled : {&split, &trial}) {
        gpu::Frame world;
        world.quad({.depth_write = false}, 0, 0, 640, 480, gpu::background);
        world.append_view(*disabled, {0, 0, 640, 480});
        gpu::check(renderer.draw(world, false), "Draw disabled sun session");
        const auto pixels = renderer.readback();
        expect(std::ranges::all_of(pixels,
                   [](auto pixel) {
                       return pixel == gpu::background;
                   }),
            "Disabled session emitted residual flare pixels through the full SceneView path");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 2 || argc == 3, "Usage: toca-sun-render-tests CANONICAL_ROOT [MEGA_ROOT]");
        const AssetStore store(argv[1]);
        const auto resources = load_sun_resources(store, resolve_setup({.weather_choice = 1}));
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private sun GPU fixture");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA original lens effects", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private sun GPU window");
        gpu::Renderer renderer(window.get());

        const SunView view(renderer, resources);
        budget(resources, "original");
        auto matrix = identity;
        const auto point = sun_world_position(0);
        matrix[12] = -point[0] + 10;
        matrix[13] = -point[1];
        matrix[14] = -point[2] + 100;
        const CameraInputs camera{matrix, {2, 2, .25f, 500, 1, -.25f, 640, 480}};
        const auto pixels = draw(renderer, view, camera, false, "sun-lens-screen-original.png");
        session_gates(renderer, resources, camera);
        if (argc == 3) {
            const auto authored = replacements(resources, argv[2]);

            const SunView mega(renderer, authored);
            budget(authored, "mega");
            const auto enhanced = draw(renderer, mega, camera, true, "sun-lens-screen-mega.png");
            expect(enhanced != pixels, "Authored lens output did not change actual GPU pixels");
            session_gates(renderer, authored, camera);
        }
        auto hidden = camera;
        hidden.world_to_view[14] = -point[2] - 1;
        gpu::Frame empty;
        view.append(empty, hidden, 0);
        expect(empty.draws.empty(), "Sun behind the camera must suppress the entire lens overlay");
        std::cout << "PASS: canonical seven-lens GPU screen blend, RGB black neutrality and original projection visibility\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
