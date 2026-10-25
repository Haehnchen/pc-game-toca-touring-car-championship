#include "assets/store.hpp"
#include "frontend/resources.hpp"
#include "scene/finish_camera.hpp"
#include "scene/view.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <string_view>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr unsigned width = 1152, height = 720;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Options {
    std::filesystem::path root, models, output;
    std::uint32_t car = 0;
    bool second = false;
    float yaw = 0, eye_height = 0, x = 0, y = 0, w = 0, h = 0;
};

Options options(int argc, char** argv) {
    expect(argc == 12, "Expected CANONICAL_ROOT MODEL_DIR CAR_ID first|second YAW_DEGREES EYE_HEIGHT X Y W H OUTPUT.png");
    Options result{.root = argv[1], .models = argv[2], .output = std::filesystem::weakly_canonical(argv[11])};
    std::size_t parsed = 0;
    const auto car = std::stoul(argv[3], &parsed);
    expect(parsed == std::string_view(argv[3]).size() && car <= 10, "Car ID must be 0..10");
    result.car = std::uint32_t(car);
    const std::string_view livery = argv[4];
    expect(livery == "first" || livery == "second", "Livery must be first or second");
    result.second = livery == "second";
    const auto number = [&](unsigned argument) {
        std::size_t consumed = 0;
        const float value = std::stof(argv[argument], &consumed);
        expect(consumed == std::string_view(argv[argument]).size() && std::isfinite(value), "Camera and placement require finite numbers");
        return value;
    };
    result.yaw = number(5);
    result.eye_height = number(6);
    expect(std::abs(result.eye_height) <= 100, "Eye height must remain within 100 model units");
    result.x = number(7);
    result.y = number(8);
    result.w = number(9);
    result.h = number(10);
    expect(result.w > 0 && result.h > 0 && result.x < width && result.y < height && std::isfinite(result.x + result.w) && std::isfinite(result.y + result.h)
               && result.x + result.w > 0 && result.y + result.h > 0,
        "Placement rectangle must be positive and intersect the canvas");
    const auto build = std::filesystem::weakly_canonical(std::filesystem::path(__FILE__).parent_path().parent_path() / "build");
    const auto relative = result.output.lexically_relative(build);
    expect(!relative.empty() && *relative.begin() != ".." && result.output.extension() == ".png", "Original/model reference PNGs must remain under local build/");
    return result;
}

CameraInputs camera_at(const Options& args) {
    const auto yaw = std::remainder(args.yaw, 360.f) * std::numbers::pi_v<float> / 180.f;
    const std::array<float, 3> eye{7 * std::cos(yaw), args.eye_height, -7 * std::sin(yaw)};
    std::array<SceneCamera, 1> cameras{};
    for (std::size_t axis = 0; axis < eye.size(); ++axis) {
        cameras[0].transform[12 + axis] = std::int32_t(std::lround(eye[axis] * 65536));
    }
    return {FinishCamera(cameras).world_to_view({}), default_projection(3, width, height)};
}

void run(const Options& args) {
    // tools/run-isolated.py supplies the owned Xvfb display and dummy audio driver.
    const char* audio = SDL_getenv("SDL_AUDIODRIVER");
    expect(audio && std::string_view(audio) == "dummy", "Run through tools/run-isolated.py --display --screen 1152x720x24 (muted)");
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize garage reference export");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA garage model reference", width, height, 0), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private garage reference window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(args.root);
    SceneResources resources;
    resources.setup = resolve_setup({.car_id = args.car == 10 ? 2 : args.car, .weather_choice = 1, .detail = 4});
    resources.car = frontend::load_car_models(store, args.car, args.models);
    const bool mega = std::ranges::any_of(resources.car.models[0].textures, [](const auto& texture) {
        return texture.encoding == ImageEncoding::webp;
    });
    SceneView view(renderer, resources);
    CarPose pose{identity};
    pose.second_driver = args.second;
    auto model = view.frame(camera_at(args), scene_lighting(resources.setup), pose, {}, {});
    expect(!model.vertices.empty() && !model.indices.empty(), "Vehicle model produced no drawable geometry");
    float left = std::numeric_limits<float>::max(), top = left, right = -left, bottom = -left;
    for (const auto& vertex : model.vertices) {
        expect(std::isfinite(vertex.x) && std::isfinite(vertex.y), "Vehicle projection produced nonfinite vertices");
        left = std::min(left, vertex.x);
        top = std::min(top, vertex.y);
        right = std::max(right, vertex.x);
        bottom = std::max(bottom, vertex.y);
    }
    expect(right > left && bottom > top, "Vehicle has no projected extent");
    const float scale = std::min(args.w / (right - left), args.h / (bottom - top));
    const float x = args.x + (args.w - (right - left) * scale) * .5f, y = args.y + (args.h - (bottom - top) * scale) * .5f;
    for (auto& vertex : model.vertices) {
        vertex.x = x + (vertex.x - left) * scale;
        vertex.y = y + (vertex.y - top) * scale;
    }
    const auto read = [&](std::uint32_t color) {
        auto frame = model;
        frame.quad({.depth_write = false}, 0, 0, width, height, color);
        std::rotate(frame.draws.begin(), frame.draws.end() - 1, frame.draws.end());
        gpu::check(renderer.draw_offscreen(frame, {width, height}), "Draw garage reference matte");
        return renderer.readback();
    };
    const auto black = read(0xff000000), white = read(0xffffffff);
    expect(black.size() == width * height && white.size() == black.size(), "Unexpected matte readback size");
    std::vector<std::uint32_t> rgba(black.size());
    std::size_t visible = 0, translucent = 0;
    for (std::size_t index = 0; index < black.size(); ++index) {
        std::array<int, 3> difference{};
        for (unsigned channel = 0; channel < 3; ++channel) {
            const auto shift = channel * 8;
            difference[channel] = int((white[index] >> shift) & 255) - int((black[index] >> shift) & 255);
        }
        std::ranges::sort(difference);
        const auto alpha = std::clamp(255 - difference[1], 0, 255);
        if (alpha == 0) {
            continue;
        }
        ++visible;
        translucent += alpha < 255;
        rgba[index] = std::uint32_t(alpha) << 24;
        for (unsigned channel = 0; channel < 3; ++channel) {
            const auto shift = channel * 8;
            const auto premultiplied = int((black[index] >> shift) & 255);
            const auto value = std::clamp((premultiplied * 255 + alpha / 2) / alpha, 0, 255);
            rgba[index] |= std::uint32_t(value) << shift;
        }
    }
    expect(visible > 0 && visible < rgba.size(), "Vehicle matte must contain visible artwork and transparency");
    std::filesystem::create_directories(args.output.parent_path());
    gpu::Surface surface(SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ARGB8888, rgba.data(), width * 4), SDL_DestroySurface);
    gpu::check(bool(surface), "Create alpha reference surface");
    gpu::check(SDL_SavePNG(surface.get(), args.output.string().c_str()), "Save alpha reference PNG");
    std::cout << "Garage model reference: source=" << (mega ? "mega" : "original") << " car=" << args.car << " livery=" << (args.second ? "second" : "first") << " yaw=" << args.yaw
              << " eye_height=" << args.eye_height << " rectangle=" << args.x << ',' << args.y << ',' << args.w << ',' << args.h << " fitted=" << x << ',' << y << ','
              << (right - left) * scale << ',' << (bottom - top) * scale << " visible_pixels=" << visible << " translucent_pixels=" << translucent
              << " output=" << args.output.string() << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        run(options(argc, argv));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
