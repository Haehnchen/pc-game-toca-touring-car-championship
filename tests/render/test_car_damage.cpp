#include "assets/store.hpp"
#include "frontend/resources.hpp"
#include "scene/car_damage.hpp"
#include "scene/finish_camera.hpp"
#include "scene/view.hpp"
#include "simulation/damage_parts.hpp"
#include "world_geometry.hpp"

#include <iostream>

namespace {
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::array<std::uint32_t, 17> component_nodes{5, 6, 8, 9, 12, 15, 16, 17, 21, 22, 23, 13, 20, 24, 25, 10, 11};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void synthetic() {
    using namespace toca::scene;
    CarDamageLevels levels{};
    for (std::size_t slot = 0; slot < levels.size(); ++slot) {
        levels[slot] = 1 + slot % 3;
    }
    for (std::size_t slot = 0; slot < component_nodes.size(); ++slot) {
        expect(car_damage_node(std::uint8_t(slot)) == component_nodes[slot] && car_damage_variant(component_nodes[slot], levels) == levels[slot],
            "Original41a280 damage slot must address its exact model node");
    }
    expect(car_damage_variant(709, levels) == levels[3], "Original low-detail node709 must share high/medium slot3");
    for (const auto id : {1u, 2u, 3u, 4u, 7u, 14u, 30u, 300u, 501u, 502u, 503u, 730u}) {
        expect(car_damage_variant(id, levels) == 0, "Damage must preserve the source's wheel, chassis and bonus-car mesh selection");
    }
    toca::simulation::DetachedPart part;
    part.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
    part.position = {10 * 65536, 20 * 65536, 30 * 65536};
    const auto front = car_damage_part_transform(part, false), reverse = car_damage_part_transform(part, true);
    expect(front[12] == float(10 * 65536 - 0x19999) / 65536 && front[13] == float(20 * 65536 + 0x3333) / 65536 && front[14] == float(30 * 65536 - 0xb333) / 65536
               && front[12] == reverse[12] && front[13] == reverse[13] && front[14] == reverse[14] && reverse[10] == -1 && reverse[0] == 1 && reverse[5] == 1,
        "Original detached front/rear faces must share their world origin while using companion pivots and reversed Z bases");
    std::cout << "PASS: original high/medium damage slots and low-detail slot3 mapping\n";
}

void reference(const char* root) {
    using namespace toca;
    using namespace toca::scene;
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private car damage check");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA original damage meshes", 640, 480, 0), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private damage window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(root);
    std::array<SceneCamera, 1> cameras{};
    cameras[0].transform[12] = 5 * 65536;
    cameras[0].transform[13] = 2 * 65536;
    cameras[0].transform[14] = -5 * 65536;
    const FinishCamera look(cameras);
    const CameraInputs camera{look.world_to_view({0, 0, 0}), default_projection(3, 640, 480)};
    for (std::uint32_t car_id = 0; car_id <= 10; ++car_id) {
        SceneResources resources;
        resources.car = frontend::load_car_models(store, car_id);
        // Corpus validation includes all three detail families, including low
        // meshes that the original local-player LOD policy does not display.
        for (std::size_t model_index = 0; model_index < resources.car.models.size(); ++model_index) {
            for (const auto& node : resources.car.models[model_index].nodes) {
                for (std::uint8_t level = 0; level <= 3; ++level) {
                    CarDamageLevels levels;
                    levels.fill(level);
                    const auto variant = car_damage_variant(node.id_flags, levels);
                    if (!node.variants.empty()) {
                        expect(variant < node.variants.size(), "Source damage variant is missing from a canonical car/detail family");
                    }
                }
            }
        }
        for (const auto detail : {3u, 1u}) {
            resources.setup = resolve_setup({.car_id = car_id == 10 ? 2 : car_id, .weather_choice = 1, .detail = detail});
            SceneView view(renderer, resources);
            const auto light = scene_lighting(resources.setup);
            expect(renderer.draw(view.frame(camera, light, {identity}, {}, {}), false), "Intact damage comparison target was unavailable");
            const auto intact = renderer.readback();
            if (car_id == 0) {
                renderer.save_screenshot("intact-car0-detail" + std::to_string(detail) + ".png");
            }
            for (const std::uint8_t level : {1, 2, 3}) {
                CarPose car{identity};
                car.damage_levels.fill(level);
                const auto frame = view.frame(camera, light, car, {}, {});
                expect(renderer.draw(frame, false), "Original damaged mesh comparison target was unavailable");
                const auto damaged = renderer.readback();
                const bool changed = damaged != intact;
                const bool ordinary = car_id < 8 || car_id == 10;
                expect(changed == (ordinary && (detail == 3 || level >= 2)), "Canonical damage pixels must follow each LOD's authored variants and original bonus-car exclusions");
                if (level == 2 && (car_id == 0 || car_id == 2 || car_id == 8)) {
                    renderer.save_screenshot("damage-car" + std::to_string(car_id) + "-detail" + std::to_string(detail) + ".png");
                }
            }
            if (car_id == 0) {
                CarPose car{identity};
                car.damage_levels.fill(3);
                const auto attached = view.frame(camera, light, car, {}, {});
                expect(renderer.draw(attached, false), "Detached component baseline target was unavailable");
                const auto before = renderer.readback();
                simulation::DetachedPart part;
                part.active = true;
                part.car_id = car_id;
                part.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
                part.position = {-65536, 65536, -3 * 65536};
                const auto detached = view.frame(camera, light, car, {}, {}, {}, {}, nullptr, std::span(&part, 1));
                expect(test::materialize_world(detached).vertices.size() > test::materialize_world(attached).vertices.size(),
                    "Original variant2 detached geometry must reach both high and medium scene paths");
                expect(renderer.draw(detached, false), "Detached component target was unavailable");
                const auto after = renderer.readback();
                expect(after != before, "Original detached high-detail mesh faces did not alter real GPU pixels");
                renderer.save_screenshot("detached-car0-detail" + std::to_string(detail) + ".png");
                part.position[0] += 65536;
                expect(renderer.draw(view.frame(camera, light, car, {}, {}, {}, {}, nullptr, std::span(&part, 1)), false), "Moving detached component target was unavailable");
                expect(renderer.readback() != after, "Detached mesh pose must follow its published source physics transform");
                part.active = false;
                expect(renderer.draw(view.frame(camera, light, car, {}, {}, {}, {}, nullptr, std::span(&part, 1)), false), "Retired detached component target was unavailable");
                expect(renderer.readback() == before, "Retiring a source detached part must remove its geometry from the ordinary scene frame");
            }
        }
    }
    // Detached owners survive removal of their actor pose. The alternate actor5
    // Vectra keeps its own model/texture family even though its physical type is2.
    SceneResources fleet_resources;
    fleet_resources.car = frontend::load_car_models(store, 0);
    fleet_resources.setup = resolve_setup({.car_id = 0, .weather_choice = 1, .detail = 3});
    const auto vectra = frontend::load_car_models(store, 2), alternate = frontend::load_car_models(store, 10);
    const std::array bindings{RivalModel{4, &vectra}, RivalModel{5, &alternate}};
    SceneView fleet(renderer, fleet_resources, nullptr, nullptr, nullptr, bindings);
    const auto light = scene_lighting(fleet_resources.setup);
    bool livery_changed = false;
    for (std::uint8_t slot = 0; slot < 5; ++slot) {
        simulation::DetachedPart part;
        part.active = true;
        part.car_id = 2;
        part.actor_id = 4;
        part.source_slot = slot;
        part.type = std::array<std::uint8_t, 5>{0, 1, 3, 2, 4}[slot];
        part.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
        part.position = {-65536, 65536, -3 * 65536};
        const auto baseline = fleet.frame(camera, light, {identity}, {}, {});
        const auto ordinary = fleet.frame(camera, light, {identity}, {}, {}, {}, {}, nullptr, std::span(&part, 1));
        expect(test::materialize_world(ordinary).vertices.size() > test::materialize_world(baseline).vertices.size(),
            "A removed rival's detached component must retain its original geometry in the shared scene pool");
        expect(renderer.draw(ordinary, false), "Rival detached component target was unavailable");
        const auto pixels = renderer.readback();
        part.actor_id = 5;
        expect(renderer.draw(fleet.frame(camera, light, {identity}, {}, {}, {}, {}, nullptr, std::span(&part, 1)), false),
            "Alternate rival detached component target was unavailable");
        livery_changed |= renderer.readback() != pixels;
        if (slot == 0) {
            renderer.save_screenshot("detached-rival5-vectra.png");
        }
    }
    expect(livery_changed, "Actor5 detached parts must use the alternate canonical Vectra livery rather than the local Honda or ordinary Vectra");
    // Team identity belongs to the registered model, not to a transient pose.
    // Actor1 models split's default first driver; actor3 explicitly uses B.
    const std::array paired_bindings{RivalModel{1, &fleet_resources.car}, RivalModel{3, &fleet_resources.car, true}};
    SceneView paired(renderer, fleet_resources, nullptr, nullptr, nullptr, paired_bindings);
    auto paired_camera = camera;
    paired_camera.hide_player = true;
    CarPose local{identity};
    const auto car_pixels = [&](std::uint32_t actor) {
        // Both poses deliberately retain their default flag. Resource binding
        // chooses the team livery while models/textures stay shared.
        const std::array poses{RivalPose{actor, {identity}}};
        expect(renderer.draw(paired.frame(paired_camera, light, local, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, poses), false),
            "Paired-driver target was unavailable");
        return renderer.readback();
    };
    expect(car_pixels(1) != car_pixels(3), "Explicit team binding must select driver B independently of pose flags or actor parity");
    simulation::DetachedPart roof;
    roof.active = true;
    roof.car_id = 0;
    roof.actor_id = 1;
    roof.source_slot = 3; // Node9 includes the original roof number.
    roof.type = 2;
    roof.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
    roof.position = {-65536, 65536, -3 * 65536};
    const auto roof_pixels = [&](std::span<const RivalPose> poses = {}) {
        expect(renderer.draw(paired.frame(paired_camera, light, local, {}, {}, {}, {}, nullptr, std::span(&roof, 1), {}, nullptr, nullptr, nullptr, false, nullptr, poses), false),
            "Paired-driver detached roof target was unavailable");
        return renderer.readback();
    };
    const auto first_roof = roof_pixels();
    roof.actor_id = 3;
    const auto second_roof = roof_pixels();
    expect(first_roof != second_roof, "Removed driver B must retain Num20 on its detached roof instead of falling back to local Num10");
    CarPose distant{identity};
    distant.object_to_world[12] = 10000;
    const std::array distant_actor{RivalPose{3, distant}};
    expect(roof_pixels(distant_actor) == second_roof, "Removing an actor pose must not change its surviving detached roof livery");
    roof.actor_id = 1;
    local.second_driver = true;
    expect(roof_pixels() == first_roof, "A split rival's default livery must not inherit the local player's independent preview selection");
    std::cout << "PASS: stable team livery for shared rival models and roofs after actor removal\n";
    std::cout << "PASS: canonical damage variants across11 car triples and actual high/medium damage pixels\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 3 && std::string_view(argv[1]) == "--reference") {
            reference(argv[2]);
        } else {
            throw std::runtime_error("Usage: toca-car-damage-render-tests [--reference CANONICAL_ROOT]");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
