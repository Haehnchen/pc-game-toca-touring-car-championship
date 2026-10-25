#include "core/random.hpp"
#include "scene/finish_camera.hpp"
#include "scene/ground_scrape.hpp"
#include "scene/view.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
using namespace toca::simulation;
constexpr std::int32_t one = 65536;
using Bytes = std::vector<std::uint8_t>;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = std::uint8_t(value);
    bytes.at(offset + 1) = std::uint8_t(value >> 8);
}

void put32(Bytes& bytes, std::size_t offset, std::int32_t value) {
    put16(bytes, offset, std::uint16_t(value));
    put16(bytes, offset + 2, std::uint16_t(std::uint32_t(value) >> 16));
}

Bytes plane(std::uint16_t flags = 0) {
    // The small real HPC fixture from test_motion: two triangles covering the
    // full128-unit square, with ordinary source plane/index-tree records.
    Bytes bytes(106);
    put32(bytes, 0, -128 * one);
    put32(bytes, 4, -128 * one);
    put16(bytes, 8, 1);
    put16(bytes, 20, 2);
    put16(bytes, 24, 4);
    constexpr std::array<std::array<std::int32_t, 2>, 4> vertices{{{-64 * one, -64 * one}, {-64 * one, 64 * one}, {64 * one, -64 * one}, {64 * one, 64 * one}}};
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        put32(bytes, 28 + index * 12, vertices[index][0]);
        put32(bytes, 32 + index * 12, 2 * one);
        put32(bytes, 36 + index * 12, vertices[index][1]);
    }
    put16(bytes, 76, 2);
    constexpr std::array<std::array<std::uint16_t, 3>, 2> faces{{{0, 1, 2}, {2, 1, 3}}};
    for (std::size_t index = 0; index < faces.size(); ++index) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            put16(bytes, 80 + index * 8 + corner * 2, faces[index][corner]);
        }
        put16(bytes, 86 + index * 8, flags);
    }
    put16(bytes, 96, 2);
    put16(bytes, 100, 1);
    put16(bytes, 102, 1);
    put16(bytes, 104, 1);
    return bytes;
}

BodyPose rolled_body(const HpcSurface& surface) {
    auto body = initial_body_pose(surface, {{0, 0}, 0, 0}, 0);
    // A proper90-degree rotation about the forwardX axis, preserving handedness.
    // Motion must classify the actual side supports itself on normal updates.
    body.basis = {{{one, 0, 0}, {0, 0, one}, {0, -one, 0}}};
    refresh_body_contacts(body, 0);
    return body;
}

GroundScrapeRequests update_to_scrape(Motion& motion, DamageContext& damage) {
    for (unsigned tick = 0; tick < 8; ++tick) {
        motion.update({}, {});
        damage.advance();
        expect(motion.state().boundary == MotionBoundary::none, "Rolled motion must retain actual HPC coverage and a finite support plane");
        const auto input = motion.ground_scrape_input();
        const auto requests = ground_scrape(input);
        if (requests.count != 0) {
            expect(!input.tyre_contact
                       && std::ranges::any_of(input.active,
                           [](auto active) {
                               return active;
                           }),
                "Ground spark requests must originate in Motion's real body-contact classification and active HPC supports");
            for (const auto& request : std::span(requests.requests).first(requests.count)) {
                expect(request.ground && request.position[1] >= 2 * one - 0x3333 && request.position[1] <= 2 * one + 0x3333,
                    "Original ground mode must publish surface-level contact positions rather than the car center");
            }
            return requests;
        }
    }
    throw std::runtime_error("The actual rolled Motion/HPC path never reached the original ground scrape producer");
}

std::vector<std::uint32_t> render(gpu::Renderer& renderer, gpu::Frame frame, const char* screenshot = nullptr) {
    if (frame.draws.empty()) {
        frame.quad({}, 0, 0, 640, 480, gpu::background);
    }
    expect(renderer.draw(frame, false), "Ground scrape fixture requires its private GPU drawable");
    if (screenshot) {
        renderer.save_screenshot(screenshot);
    }
    return renderer.readback();
}

std::size_t visible(const std::vector<std::uint32_t>& pixels) {
    return std::ranges::count_if(pixels, [](auto color) {
        return color != gpu::background;
    });
}
} // namespace

int main() {
    try {
        const HpcSurface road(plane());
        const auto body = rolled_body(road);
        SurfaceRoughness roughness;
        DamageContext damage(road);
        Motion motion(road, body, 0, true, 1, true, damage, roughness);
        const auto requests = update_to_scrape(motion, damage);
        SourceRandom random(432210);
        auto tables = make_prop_fragment_tables(random);
        ContactSparks sparks(tables);
        for (const auto& request : std::span(requests.requests).first(requests.count)) {
            sparks.emit(request);
        }
        expect(sparks.size() > 0 && !sparks.lines().empty(), "Real Motion ground requests must allocate the original ground-mode streaks");

        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private ground scrape integration check");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Motion ground scrape", 640, 480, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create private ground scrape window");
        gpu::Renderer renderer(window.get());
        SceneResources resources;
        SceneView view(renderer, resources);
        std::array<SceneCamera, 1> cameras{};
        const auto target = motion.state().body.position;
        cameras[0].transform[12] = target[0] - 6 * one;
        cameras[0].transform[13] = target[1] + 4 * one;
        cameras[0].transform[14] = target[2] - 6 * one;
        const FinishCamera look(cameras);
        const CameraInputs camera{look.world_to_view(target), default_projection(3, 640, 480)};
        const auto frame = [&] {
            return view.frame(camera, {}, {motion.state().body.object_to_world}, {}, {}, {}, {}, nullptr, {}, sparks.lines());
        };
        const auto emitted = render(renderer, frame(), "ground-scrape-motion-emitted.png");
        expect(visible(emitted) > 0, "Motion/HPC/432210/418570 ground streaks must reach actual SceneView GPU pixels");
        const auto position = motion.state().body.position;
        const auto cursor = tables.cursor;
        const auto count = sparks.size();
        const auto published = sparks.lines().front();
        // A paused scene does not dispatch a simulation tick. Repeated normal
        // frame construction/rendering must preserve Motion and particle state.
        expect(render(renderer, frame()) == emitted && render(renderer, frame()) == emitted && motion.state().body.position == position && sparks.size() == count
                   && tables.cursor == cursor && sparks.lines().front().from == published.from && sparks.lines().front().to == published.to,
            "Paused/repeated presentation must not advance Motion, ground streaks or the shared effect table");
        sparks.update();
        expect(render(renderer, frame(), "ground-scrape-motion-moving.png") != emitted, "The next explicit418430 tick must move real ground-mode GPU streaks");
        for (unsigned tick = 1; tick < 7; ++tick) {
            sparks.update();
        }
        expect(sparks.size() == 0 && visible(render(renderer, frame(), "ground-scrape-motion-retired.png")) == 0,
            "Original six-tick ground streaks must retire from the normal SceneView frame");
        for (const auto& request : std::span(requests.requests).first(requests.count)) {
            sparks.emit(request);
        }
        expect(sparks.size() > 0, "Reset check requires live source ground streaks");
        motion.reset_for_race(initial_body_pose(road, {{0, 0}, 0, 0}, 0));
        sparks.clear();
        motion.update({}, {});
        damage.advance();
        expect(ground_scrape(motion.ground_scrape_input()).count == 0 && sparks.size() == 0 && visible(render(renderer, frame())) == 0,
            "Grid reset must restore normal wheel supports and clear the ground streak pool");
        std::cout << "PASS: real rolled Motion/HPC ground scrape requests through original spark pool and SceneView GPU, pause, flight, retirement and reset\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
