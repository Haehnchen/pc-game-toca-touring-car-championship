#include "assets/store.hpp"
#include "frontend/car_name.hpp"
#include "frontend/resources.hpp"
#include "scene/finish_camera.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <string_view>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Closeup {
    std::string label;
    std::array<float, 3> eye{}, target{};
    float scale = 1;
    std::string state{};
};

struct Options {
    std::filesystem::path root, model;
    std::uint32_t render_id = 7;
    bool qa = false, reference = false, motion = true, lod = false;
    std::string livery = "both", mode = "compare", damage = "all";
    std::vector<Closeup> closeups{};
};

Options options(int argc, char** argv) {
    expect(argc >= 3, "Expected CANONICAL_ROOT MODEL --render-id 0..10 [--qa] [--reference] [--livery first|second|both] [--mode compare|original|mega|textures] [--motion on|off] "
                      "[--damage all|0..3] [--lod]");
    Options result{.root = argv[1], .model = argv[2]};
    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next = [&]() {
            expect(i + 1 < argc, "Missing preview option value");
            return std::string(argv[++i]);
        };
        if (arg == "--qa") {
            result.qa = true;
        } else if (arg == "--lod") {
            result.lod = true;
        } else if (arg == "--reference") {
            result.reference = true;
        } else if (arg == "--render-id") {
            result.render_id = std::uint32_t(std::stoul(next()));
        } else if (arg == "--livery") {
            result.livery = next();
        } else if (arg == "--mode") {
            result.mode = next();
        } else if (arg == "--damage") {
            result.damage = next();
        } else if (arg == "--motion") {
            const auto value = next();
            expect(value == "on" || value == "off", "Motion requires on|off");
            result.motion = value == "on";
        } else if (arg == "--closeup" || arg == "--closeup-state") {
            Closeup camera{.label = next()};
            expect(camera.label.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos && !camera.label.empty(), "Invalid close-up name");
            if (arg == "--closeup-state") {
                camera.state = next();
            }
            for (auto& value : camera.eye) {
                value = std::stof(next());
            }
            for (auto& value : camera.target) {
                value = std::stof(next());
            }
            camera.scale = std::stof(next());
            result.closeups.push_back(camera);
        } else {
            throw std::runtime_error("Unknown vehicle preview option: " + std::string(arg));
        }
    }
    expect(result.render_id <= 10, "Render ID must be 0..10");
    expect(result.livery == "first" || result.livery == "second" || result.livery == "both", "Invalid driver livery");
    expect(result.mode == "compare" || result.mode == "original" || result.mode == "mega" || result.mode == "textures", "Invalid preview mode");
    expect(result.damage == "all" || result.damage == "0" || result.damage == "1" || result.damage == "2" || result.damage == "3", "Invalid damage level");
    expect(result.model.filename() == "car-" + std::to_string(result.render_id) + ".glb", "Model filename must match the render ID");
    return result;
}

CameraInputs camera_at(const std::array<float, 3>& eye, const std::array<float, 3>& target, unsigned width, unsigned height, float scale = 1) {
    std::array<SceneCamera, 1> cameras{};
    std::array<std::int32_t, 3> aim{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        cameras[0].transform[12 + axis] = std::int32_t(std::lround(eye[axis] * 65536));
        aim[axis] = std::int32_t(std::lround(target[axis] * 65536));
    }
    CameraInputs result{FinishCamera(cameras).world_to_view(aim), default_projection(3, width, height)};
    result.projection.x *= scale;
    result.projection.y *= scale;
    return result;
}

void check_player_names(gpu::Renderer& renderer, const AssetStore& store, const SceneResources& original, const SceneResources& upgraded, unsigned width, unsigned height) {
    const auto camera = camera_at({-6, 1.5f, 0}, {}, width, height);
    const CarPose pose{identity};
    for (const auto* resources : {&original, &upgraded}) {
        const bool mega = resources->setup.selection.detail == graphics_mega_detail;
        const frontend::CarNameFont font(store, mega);
        const auto alpha = font.render("ALPHA"), bravo = font.render("BRAVO"), blank = font.render("ALPHA", false);
        expect(alpha->w == (mega ? 256 : 128) && alpha->h == (mega ? 32 : 16), "Mega names must use exactly twice the original raster resolution");
        const auto light = scene_lighting(resources->setup);
        SceneView first(renderer, *resources);
        SceneView second(renderer, *resources, nullptr, nullptr, nullptr, {}, nullptr, nullptr, true, first.model_textures(), first.model_meshes());
        const auto pixels = [&](SceneView& view, const std::string& suffix = "") {
            expect(renderer.draw(view.frame(camera, light, pose, {}, {}), false), "Named vehicle drawable unavailable");
            if (!suffix.empty()) {
                renderer.save_screenshot("car-" + std::to_string(resources->car.car_id) + "-name-" + std::string(resources == &original ? "original-" : "mega-") + suffix + ".png");
            }
            return renderer.readback();
        };
        const auto source = pixels(first);
        first.set_player_name(renderer, alpha.get());
        const auto alpha_frame = test::materialize_world(first.frame(camera, light, pose, {}, {}));
        const auto antialiased_names = [](const gpu::Frame& frame) {
            return std::ranges::any_of(frame.draws, [](const auto& draw) {
                return draw.state.blend && draw.state.linear && draw.state.clamp && draw.state.alpha_cutoff == 0;
            });
        };
        expect(antialiased_names(alpha_frame) == mega, "Only Mega name faces must retain continuous alpha with linear clamped sampling");
        const auto named = pixels(first, "alpha");
        expect(named != source, "Personal name must replace the car's visible source rear name");
        second.set_player_name(renderer, bravo.get());
        expect(pixels(second, "bravo") != named, "Shared same-car models must show each player's own name");
        expect(pixels(first) == named, "Naming another player must preserve the first player's shared-model name");
        first.set_player_name(renderer, bravo.get());
        const auto bravo_frame = test::materialize_world(first.frame(camera, light, pose, {}, {}));
        expect(alpha_frame.indices == bravo_frame.indices
                   && std::ranges::equal(alpha_frame.vertices, bravo_frame.vertices,
                       [](const auto& a, const auto& b) {
                           return a.x == b.x && a.y == b.y && a.z == b.z && a.rhw == b.rhw && a.u == b.u && a.v == b.v && a.diffuse == b.diffuse && a.specular == b.specular;
                       }),
            "Changing a personal name must preserve vehicle geometry, UVs and lighting");
        expect(pixels(first) != named, "Updating a personal name must change the actual rendered letters");
        first.set_player_name(renderer, blank.get());
        const auto cleared = pixels(first, "glyphs-off");
        expect(cleared != source && cleared != named, "The source 320-width gate must clear old driver lettering without drawing glyphs");
        first.set_player_name(renderer, nullptr);
        expect(pixels(first) == source, "Removing a personal-name override must restore the canonical driver texture");

        const std::array bindings{RivalModel{1, &resources->car, false}, RivalModel{2, &resources->car, true}};
        SceneView rivals(renderer, *resources, nullptr, nullptr, nullptr, bindings, nullptr, nullptr, true, first.model_textures(), first.model_meshes());
        rivals.set_rival_name(renderer, 1, alpha.get());
        rivals.set_rival_name(renderer, 2, bravo.get());
        for (unsigned lod = 0; lod < 3; ++lod) {
            // Detail3 changes at20/40; Mega retains its40/80 thresholds.
            const float distance = resources == &original ? std::array{10.f, 30.f, 60.f}[lod] : std::array{20.f, 60.f, 100.f}[lod];
            auto rival_camera = camera_at({-distance, distance * .25f, 0}, {}, width, height, distance / 6);
            rival_camera.hide_player = true;
            const auto rival_pixels = [&](std::uint32_t actor) {
                const std::array poses{RivalPose{actor, pose}};
                for (unsigned tick = 0; tick < 11; ++tick) {
                    rivals.update_visibility(rival_camera, pose, {}, {}, nullptr, poses);
                }
                expect(rivals.rival_model_detail(actor) == lod, "Named rival must retain the original distance-based detail selection");
                const auto frame = rivals.frame(rival_camera, light, pose, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, poses);
                expect(renderer.draw(frame, false), "Named rival drawable unavailable");
                return renderer.readback();
            };
            const auto named_rival = rival_pixels(1);
            rivals.set_rival_name(renderer, 1, nullptr);
            const auto source_rival = rival_pixels(1);
            expect((named_rival != source_rival) == (lod == 0), "Only the touring car HIGH's source name panels may receive dynamic lettering");
            rivals.set_rival_name(renderer, 1, alpha.get());
            if (lod == 0) {
                expect(rival_pixels(2) != named_rival, "Two actors sharing a model must retain their separate personal names");
                expect(rival_pixels(1) == named_rival, "Rendering the second named actor must not overwrite the first actor's name");
            }
        }
        expect(pixels(first) == source, "Rival name updates must preserve the local car's canonical source livery");

        first.set_ghost_model(renderer, resources->car);
        first.set_player_name(renderer, alpha.get());
        first.set_ghost_name(renderer, alpha.get());
        auto ghost_camera = camera;
        ghost_camera.hide_player = true;
        const auto ghost_pixels = [&](bool same_car) {
            const auto frame = first.frame(ghost_camera, light, pose, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, &pose, same_car);
            expect(renderer.draw(frame, false), "Named Ghost drawable unavailable");
            return renderer.readback();
        };
        const auto loaded = ghost_pixels(false);
        expect(ghost_pixels(true) == loaded, "A Ghost's name must remain the same when it reuses the player's car model");
        first.set_ghost_name(renderer, bravo.get());
        const auto saved = ghost_pixels(false);
        expect(saved != loaded, "The saved Ghost's own name must reach its rendered rear panel");
        expect(ghost_pixels(true) == saved, "A saved Ghost sharing the player's car model must retain its own driver name");
        first.set_ghost_name(renderer, alpha.get());
        expect(ghost_pixels(false) == loaded && ghost_pixels(true) == loaded, "Publishing a new best must replace the old Ghost's name in both model paths");
        if (mega && resources->car.car_id == 7) {
            const frontend::CarNameFont original_font(store);
            const auto original_name = original_font.render("ALPHA");
            Surface enlarged(SDL_ScaleSurface(original_name.get(), 256, 32, SDL_SCALEMODE_NEAREST), SDL_DestroySurface);
            gpu::check(bool(enlarged), "Prepare former car-name raster for comparison");
            const auto close = camera_at({-6, 1.5f, 0}, {}, width, height, 2.5f);
            const auto capture = [&](SDL_Surface* bitmap, const char* path) {
                first.set_player_name(renderer, bitmap);
                expect(renderer.draw(first.frame(close, light, pose, {}, {}), false), "Close car name drawable unavailable");
                renderer.save_screenshot(path);
                return renderer.readback();
            };
            const auto before = capture(enlarged.get(), "car-7-name-mega-font1x.png");
            const auto after = capture(alpha.get(), "car-7-name-mega-font2x.png");
            expect(before != after, "Authored double-resolution glyphs must visibly replace the original name pixels");
            auto fallback = original;
            fallback.setup.selection.detail = graphics_mega_detail;
            SceneView fallback_view(renderer, fallback);
            fallback_view.set_player_name(renderer, alpha.get());
            const auto fallback_frame = fallback_view.frame(close, light, pose, {}, {});
            expect(antialiased_names(fallback_frame), "Mega names on original HIGH fallback models must retain the same smooth sampling");
            expect(renderer.draw(fallback_frame, false), "Named Mega fallback drawable unavailable");
        }
    }
    std::cout << "Personal names: original/Mega car" << original.car.car_id << ", shared local/rival models, source LODs, blank gate and Ghosts passed\n";
}

void run(const Options& args) {
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private vehicle preview");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    const auto width = args.qa ? 2560u : 1280u, height = args.qa ? 1440u : 720u;
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA vehicle comparison", int(width), int(height), 0), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private vehicle window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(args.root);
    SceneResources original, upgraded;
    original.setup = upgraded.setup = resolve_setup({.car_id = args.render_id == 10 ? 2 : args.render_id, .weather_choice = 1, .detail = 3});
    const auto started = std::chrono::steady_clock::now();
    original.car = frontend::load_car_models(store, args.render_id);
    const auto loaded_original = std::chrono::steady_clock::now();
    upgraded.setup.selection.detail = 4;
    upgraded.car = frontend::load_car_models(store, args.render_id, args.model.parent_path());
    const auto loaded_mega = std::chrono::steady_clock::now();
    std::cout << "load_us original=" << std::chrono::duration_cast<std::chrono::microseconds>(loaded_original - started).count()
              << " mega=" << std::chrono::duration_cast<std::chrono::microseconds>(loaded_mega - loaded_original).count() << '\n';
    const auto counts = [](const Model& model) {
        std::size_t visible = 0, stored = 0, vertices = 0;
        for (const auto& node : model.nodes) {
            if (!node.variants.empty() && node.variants.front() != 65535) {
                visible += model.meshes.at(node.variants.front()).faces.size();
            }
        }
        for (const auto& mesh : model.meshes) {
            stored += mesh.faces.size();
            vertices += mesh.vertices.size();
        }
        return std::array{visible, stored, vertices};
    };
    const auto old_counts = counts(original.car.models[0]), new_counts = counts(upgraded.car.models[0]);
    std::cout << "model_counts original=" << old_counts[0] << ',' << old_counts[1] << ',' << old_counts[2] << " mega=" << new_counts[0] << ',' << new_counts[1] << ','
              << new_counts[2] << " (visible triangles, stored triangles, vertices)\n";
    expect(!upgraded.car.models[0].textures.empty() && upgraded.car.models[0].textures[0].encoding == ImageEncoding::webp,
        "Explicit vehicle replacement fell back instead of loading the requested GLB");
    SceneResources textures = original;
    textures.car.models[0].textures = upgraded.car.models[0].textures;
    SceneView before(renderer, original), after(renderer, upgraded), material_only(renderer, textures);
    const auto light = scene_lighting(original.setup);
    const auto prefix = "car-" + std::to_string(args.render_id) + "-";
    if (!args.reference) {
        auto marked = upgraded.car;
        std::size_t cavity_faces = 0;
        for (const auto& mesh : marked.models[0].meshes) {
            const auto count = std::ranges::count(mesh.faces, true, &ModelFace::ghost_hidden);
            cavity_faces += count;
            expect(count == 0 || mesh.ghost_positions.size() == mesh.vertices.size(), "Mega inner housings require authored Ghost positions");
        }
        expect(cavity_faces > 0, "Mega vehicle must identify its inner wheel housings for Ghost playback");
        const CarPose ghost{identity};
        auto ghost_camera = camera_at({6, 1.5f, -6}, {}, width, height);
        ghost_camera.hide_player = true;
        const auto ghost_frame = [&](bool use_player_car = false) {
            return after.frame(ghost_camera, light, ghost, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, &ghost, use_player_car);
        };
        const auto ghost_opacity = [](const gpu::Frame& source) {
            const auto frame = test::materialize_world(source);
            std::array<std::size_t, 2> counts{};
            for (const auto& command : frame.draws) {
                if (!command.state.blend || !command.state.depth) {
                    continue;
                }
                expect(command.state.depth_write, "Mega Ghost and reduced housings must retain scene depth writes");
                for (std::size_t offset = command.first; offset < command.first + command.count; ++offset) {
                    const auto alpha = frame.vertices.at(std::size_t(command.vertex_offset) + frame.indices.at(offset)).diffuse >> 24;
                    expect(alpha == 96 || alpha == 150, "Mega Ghost opacity must distinguish inner housings and wheels from the ordinary exterior");
                    ++counts[alpha == 96 ? 0 : 1];
                }
            }
            return counts;
        };
        auto rear_camera = camera_at({-6, 1.5f, 0}, {}, width, height);
        rear_camera.hide_player = true;
        const auto rear_ghost_frame = [&]() {
            return after.frame(rear_camera, light, ghost, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, &ghost);
        };
        // Recreate the prior visible policy at the same cameras and depth state.
        auto omitted = upgraded.car;
        for (auto& mesh : omitted.models[0].meshes) {
            std::erase_if(mesh.faces, [](const auto& face) {
                return face.ghost_hidden;
            });
            mesh.ghost_positions.clear();
        }
        after.set_ghost_model(renderer, omitted);
        expect(renderer.draw(ghost_frame(), false), "Prior Ghost preview drawable unavailable");
        renderer.save_screenshot(prefix + "ghost-without-interiors.png");
        expect(renderer.draw(rear_ghost_frame(), false), "Prior rear Ghost preview drawable unavailable");
        renderer.save_screenshot(prefix + "ghost-rear-without-interiors.png");
        after.set_ghost_model(renderer, marked);
        const auto loaded = ghost_frame(), new_best = ghost_frame(true);
        const auto counts = ghost_opacity(loaded);
        expect(counts[0] > 0 && counts[1] > 0, "Mega Ghost must restore visible inner housings with lower opacity than its exterior");
        const auto loaded_projected = test::materialize_world(loaded), best_projected = test::materialize_world(new_best);
        expect(ghost_opacity(new_best) == counts && loaded_projected.indices == best_projected.indices
                   && std::ranges::equal(loaded_projected.vertices, best_projected.vertices,
                       [](const auto& a, const auto& b) {
                           return a.x == b.x && a.y == b.y && a.z == b.z && a.rhw == b.rhw && a.diffuse == b.diffuse && a.specular == b.specular && a.u == b.u && a.v == b.v;
                       }),
            "New-best and loaded Ghosts must use identical contracted housings and opacity");
        expect(renderer.draw(loaded, false), "Mega Ghost preview drawable unavailable");
        renderer.save_screenshot(prefix + "ghost.png");
        const auto rear_frame = rear_ghost_frame();
        const auto rear_counts = ghost_opacity(rear_frame);
        expect(rear_counts[0] > 0 && rear_counts[1] > 0, "Rear Ghost view must retain visible translucent inner housings and exterior");
        expect(renderer.draw(rear_frame, false), "Rear Mega Ghost preview drawable unavailable");
        renderer.save_screenshot(prefix + "ghost-rear.png");
        for (auto& mesh : marked.models[0].meshes) {
            for (auto& face : mesh.faces) {
                face.ghost_hidden = false;
            }
        }
        after.set_ghost_model(renderer, marked);
        const auto unmarked_counts = ghost_opacity(ghost_frame());
        expect(unmarked_counts[0] > 0 && unmarked_counts[0] < counts[0] && unmarked_counts[1] > counts[1],
            "Clearing inner-housing markers must restore their ordinary opacity while Mega wheels remain translucent");
    }
    const auto draw = [&](SceneView& view, const CameraInputs& camera, const CarPose& pose, const std::string& name = "", bool contrast = false) {
        auto frame = view.frame(camera, light, pose, {}, {});
        if (contrast) {
            frame.quad({.depth_write = false}, 0, 0, float(width), float(height), 0xff648c43);
            std::rotate(frame.draws.begin(), frame.draws.end() - 1, frame.draws.end());
        }
        expect(renderer.draw(frame, false), "Vehicle preview drawable unavailable");
        if (!name.empty()) {
            renderer.save_screenshot(prefix + name + ".png");
        }
        return renderer.readback();
    };
    bool original_livery_changed = false, upgraded_livery_changed = false;
    for (const bool second : {false, true}) {
        if ((args.livery == "first" && second) || (args.livery == "second" && !second)) {
            continue;
        }
        const std::string driver = second ? "second-" : "first-";
        CarPose intact{identity};
        intact.second_driver = second;
        std::vector<std::pair<std::string, CarPose>> states;
        if (args.motion) {
            for (const auto angle : {-398, 398}) {
                auto steering = intact;
                // Native steering limit, approximately35 degrees each way.
                steering.steering_angle = angle;
                states.emplace_back(angle < 0 ? "steering-left" : "steering-right", steering);
            }
            for (unsigned wheel = 0; wheel < 4; ++wheel) {
                auto rolling = intact;
                rolling.wheel_angles[wheel] = 512;
                states.emplace_back("wheel-" + std::to_string(wheel + 1), rolling);
            }
            auto tilt = intact;
            tilt.visual_angles = {20, -20};
            states.emplace_back("body-tilt", tilt);
        }
        for (unsigned level = 1; level <= 3; ++level) {
            if (args.damage == "all" || args.damage == std::to_string(level)) {
                auto damaged = intact;
                damaged.damage_levels.fill(std::uint8_t(level));
                states.emplace_back("damage-" + std::to_string(level), damaged);
            }
        }
        std::vector<bool> original_changed(states.size()), upgraded_changed(states.size());
        for (unsigned angle = 0; angle < 8; ++angle) {
            const auto radians = angle * std::numbers::pi_v<float> / 4;
            auto camera = camera_at({7 * std::cos(radians), 1.8f, -7 * std::sin(radians)}, {}, width, height);
            const auto framing = test::materialize_world(after.frame(camera, light, intact, {}, {}));
            float extent_x = 1, extent_y = 1;
            for (const auto& vertex : framing.vertices) {
                extent_x = std::max(extent_x, std::abs(vertex.x - width * .5f));
                extent_y = std::max(extent_y, std::abs(vertex.y - height * .5f));
            }
            const auto scale = std::min(width * .42f / extent_x, height * .42f / extent_y);
            camera.projection.x *= scale;
            camera.projection.y *= scale;
            const auto suffix = std::to_string(angle * 45);
            const auto old_pixels = draw(before, camera, intact, args.mode == "compare" || args.mode == "original" ? driver + "original-" + suffix : "");
            const auto new_pixels = draw(after, camera, intact, args.mode == "compare" || args.mode == "mega" ? driver + "mega-" + suffix : "");
            if (!second && args.livery == "both") {
                auto teammate = intact;
                teammate.second_driver = true;
                original_livery_changed = original_livery_changed || draw(before, camera, teammate) != old_pixels;
                upgraded_livery_changed = upgraded_livery_changed || draw(after, camera, teammate) != new_pixels;
            }
            if (!args.reference) {
                expect(framing.indices.size()
                           > test::materialize_world(before.frame(camera_at({7 * std::cos(radians), 1.8f, -7 * std::sin(radians)}, {}, width, height), light, intact, {}, {}))
                               .indices.size(),
                    "Mega replacement did not add visible geometry");
                expect(new_pixels != old_pixels, "Mega replacement did not change visible pixels");
            }
            if (args.mode == "textures") {
                draw(material_only, camera, intact, driver + "textures-" + suffix);
            }
            for (std::size_t state = 0; state < states.size(); ++state) {
                const auto& [name, pose] = states[state];
                const auto old_state = draw(before, camera, pose, angle == 1 && (args.mode == "compare" || args.mode == "original") ? driver + "original-" + name : "");
                const auto new_state = draw(after, camera, pose, angle == 1 && (args.mode == "compare" || args.mode == "mega") ? driver + "mega-" + name : "");
                original_changed[state] = original_changed[state] || old_state != old_pixels;
                upgraded_changed[state] = upgraded_changed[state] || new_state != new_pixels;
            }
        }
        for (std::size_t state = 0; state < states.size(); ++state) {
            // Some bonus families reuse every damage binding or hide individual
            // wheels. Only demand the differences supported by the source car.
            expect(!original_changed[state] || upgraded_changed[state], "Replacement lost a source-supported motion or damage state");
            std::cout << driver << states[state].first << ": source=" << original_changed[state] << " replacement=" << upgraded_changed[state] << '\n';
        }
        for (const auto& closeup : args.closeups) {
            const auto camera = camera_at(closeup.eye, closeup.target, width, height, closeup.scale);
            auto pose = intact;
            if (!closeup.state.empty()) {
                const auto state = std::ranges::find_if(states, [&](const auto& value) {
                    return value.first == closeup.state;
                });
                expect(state != states.end(), "Close-up requests an unavailable motion/damage state");
                pose = state->second;
            }
            draw(before, camera, pose, driver + "original-" + closeup.label, true);
            draw(after, camera, pose, driver + "mega-" + closeup.label, true);
        }
        if (args.lod) {
            // Exercise the actual rival distance selector and its ten-tick hold.
            // Mega applies MSAA even to original MEDIUM/LOW models; compare
            // the canonical models with the same rendering policy.
            auto lod_original = original;
            lod_original.setup.selection.detail = upgraded.setup.selection.detail;
            const std::array old_binding{RivalModel{1, &lod_original.car, second}};
            const std::array new_binding{RivalModel{1, &upgraded.car, second}};
            SceneView old_rival(renderer, lod_original, nullptr, nullptr, nullptr, old_binding);
            SceneView new_rival(renderer, upgraded, nullptr, nullptr, nullptr, new_binding);
            const std::array poses{RivalPose{1, intact}};
            for (unsigned lod = 0; lod < 3; ++lod) {
                const float distance = std::array{20.f, 60.f, 100.f}[lod];
                auto camera = camera_at({distance * .70710678f, distance * .25f, -distance * .70710678f}, {}, width, height, distance / 5);
                camera.hide_player = true;
                const auto capture = [&](SceneView& view, const std::string& kind) {
                    for (unsigned tick = 0; tick < 11; ++tick) {
                        view.update_visibility(camera, intact, {}, {}, nullptr, poses);
                    }
                    expect(view.rival_model_detail(1) == lod, "Rival did not select its distance-based detail level");
                    auto frame = view.frame(camera, light, intact, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, poses);
                    expect(!test::materialize_world(frame).indices.empty(), "Rival disappeared at a model-detail transition");
                    expect(renderer.draw(frame, false), "Rival detail preview unavailable");
                    renderer.save_screenshot(prefix + driver + kind + "-lod-" + std::to_string(lod) + ".png");
                    return renderer.readback();
                };
                const auto old_pixels = capture(old_rival, "original");
                const auto new_pixels = capture(new_rival, "mega");
                expect(lod == 0 || old_pixels == new_pixels, "Mega changed an original medium/low rival model");
                expect(lod != 0 || args.reference || old_pixels != new_pixels, "Nearby rival did not use the Mega model");
            }
            std::cout << driver << "LOD: Mega HIGH and unchanged original MEDIUM/LOW rendered\n";
        }
    }
    if (args.livery == "both") {
        const auto livery = frontend::car_livery_materials(original.car.models[0], args.render_id);
        expect(livery.number_a < 0 || original_livery_changed, "Canonical driver pair did not produce distinct source pictures");
        expect(!original_livery_changed || upgraded_livery_changed, "Replacement lost the second driver's visible names or numbers");
    }
    if ((args.render_id == 2 || args.render_id == 7 || args.render_id == 10) && !args.reference) {
        check_player_names(renderer, store, original, upgraded, width, height);
    }
    if (args.render_id == 4 && !args.reference) {
        const CarPose intact{identity};
        const auto camera = camera_at({6, 1.5f, -6}, {}, width, height);
        const auto ford_pixels = draw(after, camera, intact, "ford-before-bonus");
        for (const auto bonus_id : {8u, 9u}) {
            const auto canonical = frontend::load_car_models(store, bonus_id);
            SceneResources bonus = original;
            bonus.setup.selection.car_id = bonus_id;
            bonus.setup.selection.detail = 4;
            bonus.car = frontend::load_car_models(store, bonus_id, args.model.parent_path());
            expect(bonus.car.car_id == bonus_id && bonus.car.member_indices == canonical.member_indices, "Bonus selection retained the Ford resource identity");
            for (std::size_t lod = 0; lod < canonical.models.size(); ++lod) {
                const auto& source = canonical.models[lod];
                const auto& model = bonus.car.models[lod];
                if (lod == 0 && std::filesystem::exists(args.model.parent_path() / ("car-" + std::to_string(bonus_id) + ".glb"))) {
                    // A bonus car with its own Mega HIGH model must load that one, never the Ford's.
                    expect(
                        model.textures.size() == source.textures.size() && model.textures[0].name == source.textures[0].name && model.textures[0].encoding == ImageEncoding::webp,
                        "Bonus selection must load its own Mega model");
                    continue;
                }
                expect(counts(model) == counts(source) && model.textures.size() == source.textures.size(), "Bonus fallback changed the canonical model structure");
                for (std::size_t slot = 0; slot < source.textures.size(); ++slot) {
                    expect(model.textures[slot].encoding == ImageEncoding::bmp && model.textures[slot].bmp == source.textures[slot].bmp, "Ford artwork leaked into a bonus model");
                }
            }
            SceneView bonus_view(renderer, bonus);
            draw(bonus_view, camera, intact, "bonus-" + std::to_string(bonus_id) + "-original-fallback");
        }
        expect(draw(after, camera, intact, "ford-return-after-bonus") == ford_pixels, "Ford changed after loading the two bonus cars");
    }
    if (args.render_id == 10 && !args.reference) {
        // Actor5 keeps physical car2 and its own render resource. Exercise both
        // Vectras in one view so independent material tables and LODs are used.
        auto cleland_source = frontend::load_car_models(store, 2);
        auto cleland_mega = frontend::load_car_models(store, 2, args.model.parent_path());
        expect(cleland_mega.models[0].textures[0].encoding == ImageEncoding::webp, "Cleland companion must use its accepted Mega model");
        const auto has_texture = [](const Model& model, std::string_view name) {
            return std::ranges::any_of(model.textures, [&](const auto& texture) {
                return texture.name == name;
            });
        };
        expect(has_texture(cleland_mega.models[0], "Num8") && has_texture(upgraded.car.models[0], "Num88") && has_texture(cleland_mega.models[0], "ListA")
                   && has_texture(upgraded.car.models[0], "ListB"),
            "Vectras lost their independent source name/number materials");
        expect(rival_model_id(4, 2) == 2 && rival_model_id(5, 2) == 10, "Warwick must keep physical car2 and render car10");
        const auto fallback = frontend::load_car_models(store, 10, args.model.parent_path() / "missing-v10");
        expect(fallback.models[0].textures[0].encoding == ImageEncoding::bmp && fallback.models[0].textures[0].bmp == original.car.models[0].textures[0].bmp,
            "Missing V10 must fall back to its own canonical Warwick model");
        const std::array source_bindings{RivalModel{4, &cleland_source}, RivalModel{5, &original.car}};
        const std::array mega_bindings{RivalModel{4, &cleland_mega}, RivalModel{5, &upgraded.car}};
        const std::array fallback_bindings{RivalModel{4, &cleland_mega}, RivalModel{5, &fallback}};
        auto fleet_resources = upgraded;
        fleet_resources.car = cleland_mega;
        SceneView source_pair(renderer, fleet_resources, nullptr, nullptr, nullptr, source_bindings);
        SceneView mega_pair(renderer, fleet_resources, nullptr, nullptr, nullptr, mega_bindings);
        SceneView fallback_pair(renderer, fleet_resources, nullptr, nullptr, nullptr, fallback_bindings);
        CarPose cleland_pose{identity}, warwick_pose{identity};
        cleland_pose.object_to_world[14] = -1.2f;
        warwick_pose.object_to_world[14] = 1.2f;
        const std::array poses{RivalPose{4, cleland_pose}, RivalPose{5, warwick_pose}};
        for (unsigned lod = 0; lod < 3; ++lod) {
            const float distance = std::array{20.f, 60.f, 100.f}[lod];
            auto camera = camera_at({distance * .70710678f, distance * .3f, -distance * .70710678f}, {}, width, height, distance / 7);
            camera.hide_player = true;
            const auto capture = [&](SceneView& view, const std::string& kind) {
                for (unsigned tick = 0; tick < 11; ++tick) {
                    view.update_visibility(camera, cleland_pose, {}, {}, nullptr, poses);
                }
                expect(view.rival_model_detail(4) == lod && view.rival_model_detail(5) == lod, "Both Vectras must change LOD independently");
                const auto frame = view.frame(camera, light, cleland_pose, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, poses);
                expect(!test::materialize_world(frame).indices.empty() && renderer.draw(frame, false), "Vectra pair preview unavailable");
                renderer.save_screenshot(prefix + "pair-" + kind + "-lod-" + std::to_string(lod) + ".png");
                return renderer.readback();
            };
            const auto source_pixels = capture(source_pair, "original");
            const auto mega_pixels = capture(mega_pair, "mega");
            expect(lod == 0 ? source_pixels != mega_pixels : source_pixels == mega_pixels, "Vectra pair changed canonical lower detail or lost Mega HIGH");
            if (lod == 0) {
                expect(capture(fallback_pair, "fallback") != mega_pixels, "Missing V10 did not retain Cleland Mega beside Warwick fallback");
                camera = camera_at({8, 2.2f, -8}, {}, width, height);
                camera.hide_player = true;
                const auto driving_source = capture(source_pair, "original-driving");
                expect(capture(mega_pair, "mega-driving") != driving_source, "Vectra Mega pair must remain distinct at driving distance");
            }
        }
        std::cout << "PASS: Cleland8 and Warwick88 together, independent HIGH/MEDIUM/LOW and Warwick-only fallback\n";
    }
    std::cout << "PASS: render ID " << args.render_id << ", eight views per driver, explicit GLB, source-supported motion and damage\n";
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
