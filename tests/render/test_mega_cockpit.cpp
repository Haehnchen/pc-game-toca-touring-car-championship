#include "assets/store.hpp"
#include "formats/hpc.hpp"
#include "scene/body.hpp"
#include "scene/follow_camera.hpp"
#include "scene/view.hpp"

#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::size_t different(std::span<const std::uint32_t> a, std::span<const std::uint32_t> b) {
    expect(a.size() == b.size(), "Cockpit targets must match");
    std::size_t count = 0;
    for (std::size_t index = 0; index < a.size(); ++index) {
        count += a[index] != b[index];
    }
    return count;
}

template <typename Values> void json_values(std::ostream& output, const Values& values) {
    output << '[';
    bool comma = false;
    for (const auto value : values) {
        output << (std::exchange(comma, true) ? "," : "") << value;
    }
    output << ']';
}

void export_grip(const std::filesystem::path& directory, unsigned car, std::string_view pose, std::string_view profile, const toca::scene::CockpitResources& resources,
    const toca::scene::CockpitResources& original, const toca::scene::CockpitFrame& frame, const std::array<float, 16>& driver_view) {
    std::filesystem::create_directories(directory);
    const auto stem = "car" + std::to_string(car) + '-' + std::string(pose) + '-' + std::string(profile);
    std::ofstream output(directory / (stem + ".json"));
    output << std::setprecision(9) << "{\"car\":" << car << ",\"pose\":" << std::quoted(std::string(pose)) << ",\"profile\":" << std::quoted(std::string(profile))
           << ",\"coordinate_system\":\"Actual Cockpit::frame with identity body: +X car forward/away from driver, +Y up, +Z lateral; column-major "
              "matrices\",\"driver_world_to_view\":";
    json_values(output, driver_view);
    output << ",\"roles\":[";
    bool role_comma = false;
    for (const auto role : {0u, 1u, 4u}) {
        const auto& model = resources.models[role];
        output << (std::exchange(role_comma, true) ? "," : "") << "{\"role\":" << role << ",\"matrix\":";
        json_values(output, frame.transforms[role]);
        if (role < 2) {
            output << ",\"source_anchor\":";
            json_values(output, original.models[role].meshes.at(2).vertices.at(0).position);
        }
        output << ",\"textures\":[";
        bool texture_comma = false;
        for (std::size_t index = 0; index < model.textures.size(); ++index) {
            const auto& texture = model.textures[index];
            const auto filename = stem + "-role" + std::to_string(role) + '-' + std::to_string(index) + (texture.encoding == toca::ImageEncoding::webp ? ".webp" : ".bmp");
            std::ofstream image(directory / filename, std::ios::binary);
            image.write(reinterpret_cast<const char*>(texture.bmp.data()), std::streamsize(texture.bmp.size()));
            output << (std::exchange(texture_comma, true) ? "," : "") << "{\"name\":" << std::quoted(texture.name) << ",\"file\":" << std::quoted(filename) << '}';
        }
        output << "],\"meshes\":[";
        std::vector<glm::mat4> transforms;
        bool mesh_comma = false;
        for (std::size_t index = 0; index < model.nodes.size(); ++index) {
            const auto& node = model.nodes[index];
            expect(node.parent < 0 || std::size_t(node.parent) < transforms.size(), "Diagnostic export requires runtime node order");
            transforms.push_back(
                (node.parent < 0 ? glm::make_mat4(frame.transforms[role].data()) : transforms.at(std::size_t(node.parent))) * glm::make_mat4(node.transform.data()));
            if (node.variants.empty() || node.variants.front() == 0xffff) {
                continue;
            }
            const auto& mesh = model.meshes.at(node.variants.front());
            output << (std::exchange(mesh_comma, true) ? "," : "") << "{\"name\":" << std::quoted(mesh.name) << ",\"node\":" << index << ",\"matrix\":";
            json_values(output, std::span(glm::value_ptr(transforms.back()), 16));
            output << ",\"vertices\":[";
            bool vertex_comma = false;
            for (const auto& vertex : mesh.vertices) {
                const auto point = transforms.back() * glm::vec4(glm::make_vec3(vertex.position.data()), 1.f);
                const auto normal = glm::normalize(glm::mat3(transforms.back()) * glm::make_vec3(vertex.normal.data()));
                output << (std::exchange(vertex_comma, true) ? "," : "");
                json_values(output, std::array{point.x, point.y, point.z, vertex.u, vertex.v, normal.x, normal.y, normal.z});
            }
            output << "],\"faces\":[";
            bool face_comma = false;
            for (const auto& face : mesh.faces) {
                output << (std::exchange(face_comma, true) ? "," : "");
                json_values(output, std::array{int(face.indices[0]), int(face.indices[1]), int(face.indices[2]), int(face.texture)});
            }
            output << "]}";
        }
        output << "]}";
    }
    output << "]}\n";
    expect(bool(output), "Write actual runtime grip diagnostic");
}

void export_grips(const char* root, const char* mega, const std::filesystem::path& directory) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    unsigned exports = 0;
    for (const auto car : {0u, 1u, 9u}) {
        const auto original = load_cockpit_resources(store, car);
        const auto upgraded = load_cockpit_resources(store, car, mega);
        FollowCamera camera(0, false, car, false, 4);
        for (unsigned press = 0; press < 3; ++press) {
            camera.cycle_view();
        }
        const auto driver_view = camera.world_to_view({.object_to_world = identity});
        for (const auto& [name, steering] : {std::pair{"normal", 0}, std::pair{"left", -398}, std::pair{"right", 398}, std::pair{"shift", 398}}) {
            if (car == 9 && std::string_view(name) == "shift") {
                continue;
            }
            Cockpit animation(car, 1);
            for (unsigned tick = 0; tick < 20; ++tick) {
                animation.update(std::int16_t(steering));
            }
            if (std::string_view(name) == "shift") {
                animation.shift();
                for (unsigned tick = 0; tick < 30 && animation.shift_phase() == 1; ++tick) {
                    animation.update(std::int16_t(steering));
                }
                expect(animation.shift_phase() == 3, "Diagnostic shift pose must be the actual lever hold");
            }
            const auto frame = animation.frame(identity, false, false);
            export_grip(directory, car, name, "original", original, original, frame, driver_view);
            export_grip(directory, car, name, "mega", upgraded, original, frame, driver_view);
            exports += 2;
        }
    }
    std::cout << "Exported" << exports << " original/Mega runtime grip poses for background multi-angle inspection to " << directory << '\n';
}

void review_dashboard(toca::gpu::Renderer& renderer, SDL_Window* window, const toca::AssetStore& store, const toca::scene::SceneResources& source, const char* mega, unsigned car) {
    using namespace toca::scene;
    const toca::HpcSurface surface(source.collision_hpc);
    const auto body = initial_body_pose(surface, single_qualifying_start(source.route_points), car);
    SceneResources resources;
    resources.setup = source.setup;
    resources.car = source.car;
    resources.cockpit = source.cockpit;
    const auto upgraded = load_cockpit_resources(store, car, mega);
    auto previous = upgraded;
    previous.models[2] = source.cockpit.models[2];
    FollowCamera follow(qualifying_camera_heading(0), false, car, false, 4);
    for (unsigned press = 0; press < 3; ++press) {
        follow.cycle_view();
    }
    for (unsigned tick = 0; tick < 20; ++tick) {
        follow.update_cockpit(body, {}, {}, 0);
    }
    auto sizes = std::vector<std::array<unsigned, 2>>{{640, 480}, {960, 540}, {960, 600}};
    if (car == 0) {
        sizes.push_back({1920, 1080});
    }
    for (const auto size : sizes) {
        toca::gpu::check(SDL_SetWindowSize(window, int(size[0]), int(size[1])), "Resize dashboard inspection");
        toca::gpu::check(SDL_SyncWindow(window), "Synchronize dashboard inspection");
        SceneView original(renderer, resources);
        SceneView before(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, &previous);
        SceneView after(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, &upgraded);
        CameraInputs camera{follow.world_to_view(body), selected_view_projection(4, size[0], size[1], 1), {}, 1};
        CarPose pose{body.object_to_world};
        const auto stem = "dashboard-car" + std::to_string(car) + '-' + std::to_string(size[0]) + 'x' + std::to_string(size[1]);
        const auto draw = [&](SceneView& view, const std::string& suffix) {
            expect(renderer.draw(view.frame(camera, scene_lighting(source.setup), pose, {}, {}), false), "Dashboard target unavailable");
            renderer.save_screenshot(stem + suffix + ".png");
            return renderer.readback();
        };
        for (unsigned tick = 0; tick < 12; ++tick) {
            for (auto* view : {&original, &before, &after}) {
                view->update_visibility(camera, pose, {});
            }
        }
        draw(original, "-original");
        const auto baseline = draw(before, "-before");
        const auto changed = draw(after, "-mega");
        expect(different(baseline, changed) > 100 || car != 0, "Honda's real Mega dashboard must change visible driver-view pixels");
        expect(different(baseline, changed) == 0 || car == 0, "Unregistered Audi/Flex dashboards must retain identical source pixels");
        if (car == 0 && size[0] == 960 && size[1] == 600) {
            camera.cockpit_variant = true;
            expect(different(draw(before, "-before-chun"), draw(after, "-mega-chun")) == 0, "CMCHUN must hide the full new dashboard with the original source mask");
            camera.cockpit_variant = false;
            camera.large_hands = true;
            expect(different(draw(before, "-before-handy"), draw(after, "-mega-handy")) > 100, "CMHANDY must retain the new unscaled dashboard behind its enlarged hands");
            camera.large_hands = false;
            for (const auto steering : {-398, 398}) {
                pose.steering_angle = std::int16_t(steering);
                for (unsigned tick = 0; tick < 12; ++tick) {
                    before.update_visibility(camera, pose, {});
                    after.update_visibility(camera, pose, {});
                }
                const auto turn = steering < 0 ? "-left" : "-right";
                expect(different(draw(before, std::string("-before") + turn), draw(after, std::string("-mega") + turn)) > 100,
                    "Dashboard joins and readability must remain visible behind both actual steering limits");
            }
        }
        std::cout << "PASS: source/prior/Mega dashboard car" << car << ' ' << size[0] << 'x' << size[1] << " original transforms, alpha and source masks\n";
    }
}

void render(const char* root, const char* mega, bool dashboards = false) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    toca::gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private Mega cockpit fixture");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Mega cockpit check", 640, 480, 0), SDL_DestroyWindow);
    toca::gpu::check(bool(window), "Create private cockpit window");
    toca::gpu::Renderer renderer(window.get());
    const auto cars = dashboards ? std::vector<unsigned>{0, 1, 9} : std::vector<unsigned>{0, 1, 2, 3, 4, 5, 6, 7, 9};
    for (const auto car : cars) {
        const auto source = load_scene_resources(store, resolve_setup({.car_id = car, .weather_choice = 3, .detail = 4}));
        if (dashboards) {
            review_dashboard(renderer, window.get(), store, source, mega, car);
            continue;
        }
        const toca::HpcSurface surface(source.collision_hpc);
        const auto body = initial_body_pose(surface, single_qualifying_start(source.route_points), car);
        SceneResources resources;
        resources.setup = source.setup;
        resources.car = source.car;
        resources.cockpit = source.cockpit;
        const auto upgraded = load_cockpit_resources(store, car, mega);
        auto wiper_only = source.cockpit;
        wiper_only.models[3] = upgraded.models[3];
        auto shared_only = upgraded;
        shared_only.models[0] = source.cockpit.models[0];
        shared_only.models[1] = source.cockpit.models[1];
        FollowCamera follow(qualifying_camera_heading(0), false, car, false, 4);
        for (unsigned press = 0; press < 3; ++press) {
            follow.cycle_view();
        }
        for (unsigned tick = 0; tick < 20; ++tick) {
            follow.update_cockpit(body, {}, {}, 0);
        }
        const auto lighting = scene_lighting(source.setup);
        const std::vector<std::array<unsigned, 2>> sizes =
            car == 0 || car == 1 || car == 9 ? std::vector<std::array<unsigned, 2>>{{640, 480}, {960, 540}, {960, 600}} : std::vector<std::array<unsigned, 2>>{{960, 600}};
        for (const auto size : sizes) {
            toca::gpu::check(SDL_SetWindowSize(window.get(), int(size[0]), int(size[1])), "Resize private cockpit target");
            toca::gpu::check(SDL_SyncWindow(window.get()), "Synchronize cockpit target");
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
            }
            SceneView original(renderer, resources);
            SceneView wiper(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, &wiper_only);
            SceneView shared(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, &shared_only);
            SceneView both(renderer, resources, nullptr, nullptr, nullptr, {}, nullptr, &upgraded);
            CameraInputs camera{follow.world_to_view(body), selected_view_projection(4, size[0], size[1], 1), {}, 1};
            CarPose pose{body.object_to_world};
            const std::string stem = "mega-cockpit-car" + std::to_string(car) + '-' + std::to_string(size[0]) + 'x' + std::to_string(size[1]);
            const auto draw = [&](SceneView& view, const std::string& suffix) {
                expect(renderer.draw(view.frame(camera, lighting, pose, {}, {}), false), "Private cockpit render target unavailable");
                renderer.save_screenshot(stem + suffix + ".png");
                return renderer.readback();
            };
            for (unsigned tick = 0; tick < 12; ++tick) {
                for (auto* view : {&original, &wiper, &shared, &both}) {
                    view->update_visibility(camera, pose, {});
                }
            }
            const auto baseline = draw(original, "-original");
            const auto wiper_pixels = draw(wiper, "-wiper-only");
            const auto shared_pixels = draw(shared, "-shared-only");
            const auto full = draw(both, "-mega");
            expect(different(baseline, wiper_pixels) > 3, "The new wiper must affect actual cockpit pixels at both driver sides and every aspect");
            expect(different(wiper_pixels, shared_pixels) > 100, "The new wheel must affect actual cockpit pixels at both driver sides and every aspect");
            expect(different(shared_pixels, full) > 100, "New glove/forearm geometry and artwork must affect visible pixels on both driver sides and Flex");
            camera.large_hands = true;
            const auto handy = draw(both, "-handy");
            expect(different(full, handy) > 100, "CMHANDY must visibly scale new Mega gloves using original transforms");
            camera.large_hands = false;
            camera.cockpit_variant = true;
            const auto hidden_original = draw(original, "-original-chun");
            const auto hidden_wiper = draw(wiper, "-wiper-chun");
            expect(different(hidden_original, hidden_wiper) == 0, "CMCHUN must hide the new wiper with the original dashboard");
            camera.cockpit_variant = false;
            for (const auto steering : {-398, 398}) {
                pose.steering_angle = std::int16_t(steering);
                for (unsigned tick = 0; tick < 8; ++tick) {
                    both.update_visibility(camera, pose, {});
                }
                const auto turned = draw(both, steering < 0 ? "-left" : "-right");
                expect(different(full, turned) > 100, "New wheel and original hands must retain visible steering in both directions");
            }
            both.gear_changed();
            if (car != 9) {
                for (unsigned tick = 0; tick < 30 && both.cockpit_shift_phase() == 1; ++tick) {
                    both.update_visibility(camera, pose, {});
                }
                expect(both.cockpit_shift_phase() == 3, "New Mega shift hand must reach the original lever position");
                draw(both, "-shift");
                for (unsigned tick = 0; tick < 60 && both.cockpit_shift_phase() != 0; ++tick) {
                    both.update_visibility(camera, pose, {});
                }
                expect(both.cockpit_shift_phase() == 0, "Mega shifting hand must return and regrip the Mega wheel");
            } else {
                expect(both.cockpit_shift_phase() == 0, "Flex must retain its parked lever hand");
            }
            std::cout << "PASS: Mega hands/wheel/wiper car" << car << ' ' << size[0] << 'x' << size[1] << " actual pixels, source masks and steering/shifts\n";
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 5 && std::string_view(argv[3]) == "--export-grip") {
            export_grips(argv[1], argv[2], argv[4]);
            return 0;
        }
        if (argc == 4 && std::string_view(argv[3]) == "--dashboard-review") {
            render(argv[1], argv[2], true);
            return 0;
        }
        if (argc != 3) {
            throw std::runtime_error("Expected canonical data root and bundled Mega directory [--export-grip DIRECTORY]");
        }
        render(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
