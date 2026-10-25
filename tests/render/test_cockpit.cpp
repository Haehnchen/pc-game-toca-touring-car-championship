#include "assets/store.hpp"
#include "formats/hpc.hpp"
#include "scene/body.hpp"
#include "scene/follow_camera.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t different(std::span<const std::uint32_t> a, std::span<const std::uint32_t> b) {
    expect(a.size() == b.size(), "Cockpit pixel targets must have equal dimensions");
    std::size_t count = 0;
    for (std::size_t index = 0; index < a.size(); ++index) {
        count += a[index] != b[index];
    }
    return count;
}

void reference(const char* root) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    toca::gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated cockpit check");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA original cockpit check", 640, 480, 0), SDL_DestroyWindow);
    toca::gpu::check(bool(window), "Create isolated cockpit window");
    toca::gpu::Renderer renderer(window.get());
    for (const auto car_id : {0u, 1u, 9u}) {
        const auto resources = load_scene_resources(store, resolve_setup({.car_id = car_id, .weather_choice = 1}));
        const toca::HpcSurface surface(resources.collision_hpc);
        const auto body = initial_body_pose(surface, single_qualifying_start(resources.route_points), car_id);
        const CarPose pose{body.object_to_world};
        HorizonActors actors(resources.moving_horizon);
        const auto light = scene_lighting(resources.setup);
        const std::vector<std::uint8_t> flags(resources.flags.model.nodes.size());
        FollowCamera follow(qualifying_camera_heading(0), false, car_id);
        follow.cycle_view();
        follow.cycle_view();
        follow.cycle_view();
        expect(follow.selected_view() == 1, "Source cockpit test must enter view1 through the normal camera cycle");
        for (unsigned tick = 0; tick < 20; ++tick) {
            follow.update_cockpit(body, {}, {}, 0);
        }
        for (const auto dimensions : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
            SceneView view(renderer, resources);
            toca::gpu::check(SDL_SetWindowSize(window.get(), dimensions[0], dimensions[1]), "Resize isolated cockpit target");
            toca::gpu::check(SDL_SyncWindow(window.get()), "Synchronize isolated cockpit size");
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
            }
            CameraInputs camera{follow.world_to_view(body), selected_view_projection(3, dimensions[0], dimensions[1], 1), {}, 1};
            view.update_visibility(camera, pose, actors.frame());
            const auto render = [&](const char* suffix) {
                const auto frame = view.frame(camera, light, pose, flags, actors.frame());
                expect(!toca::test::materialize_world(frame).vertices.empty(), "Canonical cockpit frame contains no geometry");
                const bool dashboard = std::ranges::any_of(frame.draws, [](const auto& draw) {
                    return draw.mesh && draw.model_instance != UINT32_MAX && !draw.state.depth && !draw.state.depth_write;
                });
                expect(dashboard != camera.cockpit_variant, "494a40 DASH meshes must disable both depth testing and writes; CMCHUN must remove those draws");
                expect(renderer.draw(frame, false), "Canonical cockpit target is unavailable");
                renderer.save_screenshot("cockpit-car" + std::to_string(car_id) + "-" + std::to_string(dimensions[0]) + "x" + std::to_string(dimensions[1]) + suffix + ".png");
                return renderer.readback();
            };
            const auto normal = render("");
            camera.cockpit_variant = true;
            const auto chun = render("-chun");
            camera.cockpit_variant = false;
            camera.large_hands = true;
            const auto handy = render("-handy");
            camera.large_hands = false;
            expect(different(normal, chun) > 500, "CMCHUN must visibly remove the original dashboard/wiper at every aspect");
            expect(different(normal, handy) > 100, "CMHANDY must visibly enlarge the original driver gloves at every aspect");
            auto steering = pose;
            steering.steering_angle = 170;
            for (unsigned tick = 0; tick < 8; ++tick) {
                view.update_visibility(camera, steering, actors.frame());
            }
            const auto moved = render("-steered");
            expect(different(normal, moved) > 100, "Original steering wheel and attached gloves must move with actual road-wheel steering");
            view.gear_changed();
            if (car_id != 9) {
                expect(view.cockpit_shift_phase() == 1, "Actual gear commit must start the original cockpit reach");
                for (unsigned tick = 0; tick < 30 && view.cockpit_shift_phase() == 1; ++tick) {
                    view.update_visibility(camera, steering, actors.frame());
                }
                expect(view.cockpit_shift_phase() == 3, "Original cockpit hand must reach its gear lever");
                const auto lever = render("-shift-lever");
                expect(different(moved, lever) > 100, "Original gear-change reach must move visible glove pixels");
                for (unsigned tick = 0; tick < 50 && view.cockpit_shift_phase() != 0; ++tick) {
                    view.update_visibility(camera, steering, actors.frame());
                }
                expect(view.cockpit_shift_phase() == 0, "Original cockpit hand must return and regrip the steering wheel");
                const auto regrip = render("-shift-regrip");
                expect(different(lever, regrip) > 100, "Original return must visibly restore the wheel grip");
            } else {
                expect(view.cockpit_shift_phase() == 0, "Original Flex cockpit must retain its permanently parked lever glove");
            }
            std::cout << "PASS: car" << car_id << ' ' << dimensions[0] << 'x' << dimensions[1] << " cockpit, CMCHUN, CMHANDY and steering pixels\n";
        }
        if (car_id == 0) {
            SceneResources exterior;
            exterior.setup = resources.setup;
            exterior.car = resources.car;
            SceneView body_view(renderer, exterior);
            for (const auto selected : {1u, 2u, 3u, 4u, 5u}) {
                const CameraInputs camera{selected == 1 ? follow.world_to_view(body) : race_world_to_view(body, follow.heading(), std::uint16_t(selected), car_id, false),
                    selected_view_projection(3, 960, 600, std::uint16_t(selected)), {}, std::uint16_t(selected)};
                const auto frame = body_view.frame(camera, light, pose, {}, {});
                const auto projected = toca::test::materialize_world(frame);
                expect(projected.vertices.empty() == (selected == 1), "Source local-car geometry must disappear in cockpit1 and retain body/hood in follow/bonnet/rear views");
                if (!projected.vertices.empty()) {
                    expect(renderer.draw(frame, false), "Original exterior mask target is unavailable");
                    renderer.save_screenshot("cockpit-body-mask-view" + std::to_string(selected) + ".png");
                }
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected canonical data root");
        }
        reference(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
