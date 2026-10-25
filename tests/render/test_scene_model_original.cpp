#include "assets/store.hpp"
#include "frontend/resources.hpp"
#include "image/decode.hpp"
#include "scene/body.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/follow_camera.hpp"
#include "scene/loading.hpp"
#include "scene/shadow_support.hpp"
#include "scene/view.hpp"
#include "scene_models/fixtures.hpp"
#include "scene_models/gpu_fixture.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <cstring>
#include <iostream>

namespace {
using namespace toca::test::scene_models;

void flag_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& source) {
    using namespace toca::scene;
    // Isolate one actual authored Donington flag with a diagnostic camera;
    // the normal qualifying camera is checked separately below on every track.
    const auto actor = std::ranges::find_if(source.flags.actors, [](const auto& entry) {
        return entry.animation_node != UINT32_MAX;
    });
    expect(actor != source.flags.actors.end(), "Canonical flag probe requires an authored animated flag");
    const auto node = actor->animation_node;
    const auto matrices = model_world_transforms(source.flags.model, unit);
    const auto& mesh = source.flags.model.meshes.at(source.flags.model.nodes.at(node).variants.at(0));
    const auto point = [&](std::uint16_t index) {
        const auto& position = mesh.vertices.at(index).position;
        const auto& matrix = matrices.at(node);
        std::array<float, 3> result;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result[axis] = position[0] * matrix[axis] + position[1] * matrix[4 + axis] + position[2] * matrix[8 + axis] + matrix[12 + axis];
        }
        return result;
    };
    const auto& face = mesh.faces.at(0);
    const auto a = point(face.indices[0]), b = point(face.indices[1]), c = point(face.indices[2]);
    std::array<float, 3> center{}, normal{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        center[axis] = (a[axis] + b[axis] + c[axis]) / 3;
        const auto j = (axis + 1) % 3, k = (axis + 2) % 3;
        normal[axis] = (b[j] - a[j]) * (c[k] - a[k]) - (b[k] - a[k]) * (c[j] - a[j]);
    }
    const auto length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    expect(length > 0, "Authored flag probe face must define a camera direction");
    std::array<std::int32_t, 3> target;
    std::array<SceneCamera, 1> cameras{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        target[axis] = std::int32_t(center[axis] * 65536.f);
        cameras[0].transform[12 + axis] = std::int32_t((center[axis] + normal[axis] / length * 5) * 65536.f);
    }
    const FinishCamera look(cameras);
    const CameraInputs camera{look.world_to_view(target), default_projection(3, 640, 480)};
    SceneResources isolated{};
    isolated.setup = source.setup;
    isolated.flags = source.flags;
    SceneView view(renderer, isolated);
    toca::SourceRandom random(7);
    FlagAnimation flags(isolated.flags, random);
    std::array<std::vector<std::uint32_t>, 3> pixels;
    for (std::size_t frame = 0; frame < pixels.size(); ++frame) {
        expect(renderer.draw(view.frame(camera, scene_lighting(source.setup), {unit}, flags.variants(), {}), false), "Canonical flag probe target was unavailable");
        pixels[frame] = renderer.readback();
        renderer.save_screenshot("flag-probe-variant" + std::to_string(flags.first_frame()) + ".png");
        for (unsigned tick = 0; tick < 4; ++tick) {
            flags.update();
        }
    }
    expect(pixels[0] != pixels[1] && pixels[1] != pixels[2] && pixels[0] != pixels[2], "All three original flag meshes must produce different real GPU pixels");
}

void low_detail_sky_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& source) {
    using namespace toca::scene;
    // Actual loaded Donington sky geometry must be suppressed at detail0/1.
    // Independent 44f0a3 ->486e75 constants, converted from packed BGR.
    constexpr std::array<std::uint32_t, 5> colors{0xff64b4d0, 0xffb2d9e7, 0xffa4adbe, 0xff4a616b, 0xff28313a};
    for (std::uint32_t detail = 0; detail < 2; ++detail) {
        for (std::uint32_t weather = 1; weather <= colors.size(); ++weather) {
            SceneResources isolated{};
            isolated.setup = source.setup;
            isolated.setup.selection.detail = detail;
            isolated.setup.weather = weather;
            isolated.horizon = source.horizon;
            SceneView view(renderer, isolated);
            const CameraInputs camera{unit, default_projection(detail, 640, 480)};
            const auto frame = view.frame(camera, scene_lighting(isolated.setup), {unit}, {}, {});
            expect(frame.draws.size() == 1 && frame.vertices.size() == 4 && !frame.draws[0].state.texture && !frame.draws[0].state.depth && !frame.draws[0].state.depth_write,
                "Detail0/1 must replace the actual textured horizon with a flat background that cannot write depth");
            expect(renderer.draw(frame, false), "Original low-detail sky target was unavailable");
            expect(std::ranges::all_of(renderer.readback(),
                       [&](auto pixel) {
                           return pixel == colors[weather - 1];
                       }),
                "The low-detail sky must retain the original weather-specific clear RGB over the entire drawable");
        }
        SceneResources lava{};
        lava.setup = resolve_setup({.track_id = 9, .weather_choice = 4, .detail = detail});
        lava.horizon = source.horizon;
        SceneView view(renderer, lava);
        const CameraInputs camera{unit, default_projection(detail, 640, 480)};
        expect(renderer.draw(view.frame(camera, scene_lighting(lava.setup), {unit}, {}, {}), false), "Original low-detail Lavaland sky target was unavailable");
        expect(std::ranges::all_of(renderer.readback(),
                   [](auto pixel) {
                       return pixel == 0xffad590c;
                   }),
            "Low-detail Lavaland must use its original fixed clear color regardless of requested weather");
    }
    std::cout << "PASS: original detail0/1 sky suppression, weather clear colors and Lavaland exception through real GPU readback\n";
}

void vectra_rival_pixels(toca::gpu::Renderer& renderer, const toca::AssetStore& store, const toca::scene::SceneResources& source) {
    using namespace toca::scene;
    // 44df6e loads a distinct model/texture family for actor5, while both
    // actors retain physical car2. Exercise the real SceneView cache/bindings.
    const auto first = toca::frontend::load_car_models(store, rival_model_id(4, 2));
    const auto second = toca::frontend::load_car_models(store, rival_model_id(5, 2));
    SceneResources isolated{};
    isolated.setup = source.setup;
    isolated.setup.selection.detail = 3;
    const std::array bindings{RivalModel{4, &first}, RivalModel{5, &second}};
    SceneView view(renderer, isolated, nullptr, nullptr, nullptr, bindings);
    std::array<SceneCamera, 1> cameras{};
    cameras[0].transform[12] = 8 * 65536;
    cameras[0].transform[13] = 6 * 65536;
    cameras[0].transform[14] = -12 * 65536;
    const FinishCamera look(cameras);
    const CameraInputs camera{look.world_to_view({0, 0, 0}), default_projection(3, 640, 480)};
    std::array poses{RivalPose{4, {unit}}, RivalPose{5, {unit}}};
    poses[0].car.object_to_world[12] = -3;
    poses[1].car.object_to_world[12] = 3;
    const auto frame = [&](const CameraInputs& observer, std::span<const RivalPose> visible) {
        view.update_visibility(observer, {unit}, {}, {}, nullptr, visible);
        return view.frame(observer, scene_lighting(isolated.setup), {unit}, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, visible);
    };
    const auto left = frame(camera, std::span(poses).first(1));
    const auto right = frame(camera, std::span(poses).last(1));
    const auto both = frame(camera, poses);
    const auto textures = [](const toca::gpu::Frame& value) {
        std::vector<SDL_GPUTexture*> result;
        for (const auto& draw : value.draws) {
            if (draw.state.texture) {
                result.push_back(draw.state.texture);
            }
        }
        return result;
    };
    const auto first_textures = textures(left), second_textures = textures(right);
    expect(!first_textures.empty() && !second_textures.empty(), "Both original Vectra bindings must produce textured rival geometry");
    expect(std::ranges::none_of(first_textures,
               [&](auto texture) {
                   return std::ranges::find(second_textures, texture) != second_textures.end();
               }),
        "The two original Vectras must retain separate uploaded texture resources");
    const auto render = [&](const toca::gpu::Frame& value) {
        expect(renderer.draw(value, false), "Canonical Vectra rival target was unavailable");
        return renderer.readback();
    };
    const auto left_pixels = render(left), right_pixels = render(right), both_pixels = render(both);
    renderer.save_screenshot("vectra-rival-pair.png");
    std::size_t first_coverage = 0, second_coverage = 0;
    for (std::size_t pixel = 0; pixel < both_pixels.size(); ++pixel) {
        first_coverage += both_pixels[pixel] != right_pixels[pixel];
        second_coverage += both_pixels[pixel] != left_pixels[pixel];
    }
    expect(first_coverage > 100 && second_coverage > 100, "Both distinct Vectra bindings must contribute visible GPU pixels in the same frame");
    poses[1].car.object_to_world = poses[0].car.object_to_world;
    expect(render(frame(camera, std::span(poses).last(1))) != left_pixels, "The second Vectra livery must differ at the same actual camera and body pose");

    // At detail3, a rival beyond20units selects its medium model. Its wheels
    // retain steering but must not inherit high-model spin or node300 tilt.
    cameras[0].transform[12] = 0;
    cameras[0].transform[13] = 4 * 65536;
    cameras[0].transform[14] = -30 * 65536;
    const FinishCamera distant_look(cameras);
    const CameraInputs distant{distant_look.world_to_view({0, 0, 0}), default_projection(3, 640, 480)};
    for (int tick = 0; tick < 11; ++tick) {
        view.update_visibility(distant, {unit}, {}, {}, nullptr, poses);
    }
    const auto medium = frame(distant, poses);
    for (auto& pose : poses) {
        pose.car.wheel_angles = {1024, 1024, 1024, 1024};
        pose.car.visual_angles = {1024, 1024};
    }
    const auto spun = frame(distant, poses);
    const auto medium_projected = toca::test::materialize_world(medium), spun_projected = toca::test::materialize_world(spun);
    expect(!medium_projected.vertices.empty() && medium_projected.vertices.size() == spun_projected.vertices.size()
               && std::memcmp(medium_projected.vertices.data(), spun_projected.vertices.data(), medium_projected.vertices.size() * sizeof(toca::gpu::Vertex)) == 0,
        "A real medium rival at global detail3 must ignore high-model wheel spin and cosmetic body tilt");
    std::cout << "PASS: both original Vectra rival caches, separate livery textures, visible GPU pixels and selected-LOD animation\n";
}

void horizon_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& source) {
    using namespace toca::scene;
    SceneResources isolated{};
    isolated.setup = source.setup;
    isolated.moving_horizon = source.moving_horizon;
    HorizonActors actors(isolated.moving_horizon);
    SceneView view(renderer, isolated);
    const auto& model = isolated.moving_horizon.rotorcraft;
    const auto& initial = actors.frame().rotorcraft;
    const auto matrices = horizon_actor_world_transforms(model, initial);
    expect(!model.nodes.empty(), "Canonical HOR probe requires an authored rotorcraft");
    // Aim at an actual visible rotor face, preserving authored geometry and
    // controller placement. This isolates mesh switching from the normal car view.
    const auto rotor_node = std::size_t(isolated.moving_horizon.rotorcraft_child) + 1;
    const auto& node = model.nodes.at(rotor_node);
    const auto& mesh = model.meshes.at(node.variants.at(0));
    const auto& face = mesh.faces.at(0);
    std::array<std::array<float, 3>, 3> points;
    for (std::size_t corner = 0; corner < points.size(); ++corner) {
        const auto& p = mesh.vertices.at(face.indices[corner]).position;
        const auto& matrix = matrices.at(rotor_node);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            points[corner][axis] = p[0] * matrix[axis] + p[1] * matrix[4 + axis] + p[2] * matrix[8 + axis] + matrix[12 + axis];
        }
    }
    std::array<float, 3> center{}, normal{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        center[axis] = (points[0][axis] + points[1][axis] + points[2][axis]) / 3;
        const auto j = (axis + 1) % 3, k = (axis + 2) % 3;
        normal[axis] = (points[1][j] - points[0][j]) * (points[2][k] - points[0][k]) - (points[1][k] - points[0][k]) * (points[2][j] - points[0][j]);
    }
    const auto length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    expect(length > 0, "Canonical HOR probe face must define a camera direction");
    for (auto& component : normal) {
        component /= length;
    }
    // A horizontal rotor face needs a small horizontal camera displacement to
    // keep the source's look-at right vector defined.
    if (std::abs(normal[0]) + std::abs(normal[2]) < .1f) {
        normal[0] += .25f;
    }
    std::array<std::int32_t, 3> target;
    std::array<SceneCamera, 1> cameras{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        target[axis] = std::int32_t(center[axis] * 65536.f);
        cameras[0].transform[12 + axis] = std::int32_t((center[axis] + normal[axis] * 10) * 65536.f);
    }
    const FinishCamera look(cameras);
    const CameraInputs camera{look.world_to_view(target), default_projection(3, 640, 480)};
    const auto render = [&](const HorizonFrame& frame, const char* filename) {
        const auto scene = view.frame(camera, scene_lighting(source.setup), {unit}, {}, frame);
        expect(!toca::test::materialize_world(scene).vertices.empty(), "Authored moving HOR geometry did not reach the projection pass");
        expect(renderer.draw(scene, false), "Canonical HOR probe target was unavailable");
        renderer.save_screenshot(filename);
        return renderer.readback();
    };
    const auto first = render(actors.frame(), "horizon-probe-rotor0.png");
    actors.update();
    const auto second = render(actors.frame(), "horizon-probe-rotor1.png");
    expect(first != second, "Original rotor sibling meshes must produce different real GPU pixels");
    // Compare an unchanged rotor phase before/after movement, so that this
    // second pixel difference requires the controller transform itself.
    const auto before = actors.frame();
    for (unsigned tick = 0; tick < 120; ++tick) {
        actors.update();
    }
    expect(before.rotorcraft.transforms != actors.frame().rotorcraft.transforms, "Original rotorcraft controller did not move its world/body transforms");
    const auto moved = render(actors.frame(), "horizon-probe-moving.png");
    expect(second != moved, "Original HOR motion must change real GPU pixels at the same rotor phase");
    auto hidden = actors.frame();
    std::ranges::fill(hidden.moving.visible, 0);
    std::ranges::fill(hidden.rotorcraft.visible, 0);
    expect(toca::test::materialize_world(view.frame(camera, scene_lighting(source.setup), {unit}, {}, hidden)).vertices.empty(),
        "HOR visibility masks must suppress their selected mesh nodes");
}

void car_shadow_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& resources, const toca::scene::BodyPose& body) {
    using namespace toca::scene;
    // Inspect the same front-wheel/body cutout exposed by the qualifying turn.
    std::array<SceneCamera, 1> cameras{};
    cameras[0].transform[12] = body.position[0] + 4 * 65536;
    cameras[0].transform[13] = body.position[1] + 2 * 65536;
    cameras[0].transform[14] = body.position[2] - 4 * 65536;
    const FinishCamera look(cameras);
    const CameraInputs camera{look.world_to_view(body.position), default_projection(3, 640, 480)};
    SceneView view(renderer, resources);
    HorizonActors actors(resources.moving_horizon);
    CarPose car{body.object_to_world};
    expect(renderer.draw(view.frame(camera, scene_lighting(resources.setup), car, std::vector<std::uint8_t>(resources.flags.model.nodes.size()), actors.frame()), false),
        "Car side comparison target was unavailable");
    const auto before = renderer.readback();
    renderer.save_screenshot("car-side-before-shadow.png");
    auto rain = resources.setup;
    rain.weather = 3; // Isolate the original rectangle branch in this cutout check.
    car.shadow = car_shadow_footprint(car_shadow_input(body, rain));
    const auto frame = view.frame(camera, scene_lighting(resources.setup), car, std::vector<std::uint8_t>(resources.flags.model.nodes.size()), actors.frame());
    const auto expected = toca::test::materialize_world(frame);
    expect(std::ranges::count_if(expected.draws,
               [](const auto& draw) {
                   return !draw.state.depth && draw.state.blend && !draw.state.key;
               })
               == 2,
        "Original shadow capability branch must submit two alpha triangles without depth writes or binary color keying");
    expect(renderer.draw(frame, false), "Shadow comparison target was unavailable");
    const auto after = renderer.readback();
    expect(renderer.draw(expected, false) && renderer.readback() == after, "Car shadow pixels must match the independent CPU projection consumer exactly");
    renderer.save_screenshot("car-side-with-shadow.png");
    std::size_t darkened = 0;
    for (std::size_t index = 0; index < before.size(); ++index) {
        if (before[index] == after[index]) {
            continue;
        }
        bool darker = true;
        for (const unsigned shift : {0u, 8u, 16u}) {
            darker &= ((after[index] >> shift) & 255) <= ((before[index] >> shift) & 255);
        }
        expect(darker, "Original alpha-black shadow must darken covered terrain without replacing bodywork");
        ++darkened;
    }
    expect(darkened > 40, "Actual car shadow did not darken visible terrain around the wheel/body cutout");
    const auto gap = std::size_t(306 * 640 + 444);
    expect((after[gap] & 255) * 4 < (before[gap] & 255) * 3, "Honda front wheel-to-bumper cutout must show its original dark undercar shadow");
}

void lavaland_cone_pixels(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& source) {
    using namespace toca::scene;
    expect(source.cones.model.meshes.size() == 1 && source.cones.model.meshes[0].name == "GARCON_1" && source.cones.model.meshes[0].faces.size() == 14
               && source.cones.model.textures.size() == 2 && source.cones.model.textures[0].name == "GARG01" && source.cones.model.textures[1].name == "GARG02",
        "Lavaland obstacle probe requires the canonical gargoyle shape and both source materials");
    const toca::HpcSurface surface(source.collision_hpc);
    PropFrame props;
    props.cones.visible.assign(source.cones.model.nodes.size(), 1);
    for (const auto& node : source.cones.model.nodes) {
        auto& matrix = props.cones.transforms.emplace_back();
        for (std::size_t field = 0; field < matrix.size(); ++field) {
            matrix[field] = std::int32_t(node.transform[field] * 65536.f);
        }
    }
    for (const auto& actor : source.cones.actors) {
        auto& matrix = props.cones.transforms.at(actor.root_node);
        const auto sample = surface.prop_sample(matrix[12], matrix[14]);
        expect(bool(sample), "Canonical gargoyle must have an actual supporting road plane");
        matrix[13] = sample->height + 0x75c2; // 458ac0's ordinary upright support, unchanged by rendering.
    }
    const auto& root = props.cones.transforms.at(source.cones.actors.at(0).root_node);
    const std::array<std::int32_t, 3> target{root[12], root[13], root[14]};
    std::array<SceneCamera, 1> cameras{};
    cameras[0].transform[12] = target[0] + 0x1cccc;
    cameras[0].transform[13] = target[1] + 0x7333;
    cameras[0].transform[14] = target[2] - 0x24ccc;
    const FinishCamera look(cameras);
    CameraInputs camera{look.world_to_view(target), default_projection(3, 640, 480)};
    camera.registered_camera_position = target;
    camera.hide_player = true;
    toca::SourceRandom random(7);
    FlagAnimation flags(source.flags, random);
    HorizonActors actors(source.moving_horizon);
    const auto render = [&](const SceneResources& resources) {
        SceneView view(renderer, resources);
        expect(renderer.draw(view.frame(camera, scene_lighting(source.setup), {unit}, flags.variants(), actors.frame(), props), false), "Lavaland obstacle target was unavailable");
        return renderer.readback();
    };
    auto expected = source;
    // Independently audited source bounds [-9356,50379]/65536: center41023/131072.
    for (auto& vertex : expected.cones.model.meshes[0].vertices) {
        vertex.position[1] -= 41023.f / 131072.f;
    }
    auto uncorrected = source;
    // Diagnostic label bypasses only the GARCON preparation; pixels/materials stay original.
    uncorrected.cones.model.meshes[0].name = "uncorrected-GARCON";
    const auto actual_pixels = render(source), expected_pixels = render(expected), floating_pixels = render(uncorrected);
    expect(actual_pixels == expected_pixels, "SceneView must draw the gargoyle at the independently corrected visual origin");
    std::size_t changed = 0;
    for (std::size_t pixel = 0; pixel < actual_pixels.size(); ++pixel) {
        changed += actual_pixels[pixel] != floating_pixels[pixel];
    }
    expect(changed > 100, "The close native gargoyle probe must visibly distinguish its grounded origin from the floating source pivot");
    std::cout << "PASS: Lavaland gargoyle source shape/materials and grounded SceneView pixels at unchanged cone support\n";
}

void reference(const std::filesystem::path& root) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    GpuFixture gpu("TOCA source scene models", 640, 480);
    auto& renderer = gpu.renderer;
    for (std::uint32_t track = 0; track < tracks.size(); ++track) {
        const auto& fixture = tracks[track];
        const auto name = fixture.name;
        const LoadingView loading(store, renderer, track);
        expect(renderer.draw(loading.frame(0, 1), true), "Private loading target was unavailable");
        const auto loading_pixels = renderer.readback();
        const auto archive = store.open_archive("data/loadhigh.bfp");
        const auto artwork = toca::decode_bmp(archive.member(fixture.loading_member).bytes());
        toca::Surface expected(SDL_ConvertSurface(artwork.get(), SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        expect(bool(expected) && expected->w == 640 && expected->h == 480 && loading_pixels.size() == 640 * 480, "Original loading artwork target size changed");
        for (int y = 0; y < expected->h; ++y) {
            for (int x = 0; x < expected->w; ++x) {
                std::uint32_t color;
                std::memcpy(&color, static_cast<const std::uint8_t*>(expected->pixels) + y * expected->pitch + x * 4, 4);
                expect(loading_pixels[std::size_t(y * expected->w + x)] == (color | 0xff000000u), "Loading view must display the selected track's exact source artwork");
            }
        }
        renderer.save_screenshot(std::string("loading-") + name + ".png");
        const auto resources = load_scene_resources(store, resolve_setup({.track_id = track}));
        const toca::HpcSurface surface(resources.collision_hpc);
        const auto start = single_qualifying_start(resources.route_points);
        const auto body = initial_body_pose(surface, start, 0);
        FollowCamera follow(qualifying_camera_heading(track));
        follow.update(body, true); // 45eb60 applies the first State4 slew before presentation.
        const CameraInputs camera{follow.world_to_view(body), default_projection(3, 640, 480)};
        toca::SourceRandom random(7);
        FlagAnimation flags(resources.flags, random);
        HorizonActors actors(resources.moving_horizon);
        SceneView view(renderer, resources);
        const auto light = scene_lighting(resources.setup);
        const auto frame = view.frame(camera, light, {body.object_to_world}, flags.variants(), actors.frame());
        const auto projected = toca::test::materialize_world(frame);
        auto distant = body.object_to_world;
        distant[12] += 10000;
        const auto absent = view.frame(camera, light, {distant}, flags.variants(), actors.frame());
        expect(projected.vertices.size() > toca::test::materialize_world(absent).vertices.size(),
            "Selected initial Honda must contribute visible geometry through original follow camera");
        expect(std::ranges::any_of(frame.draws,
                   [](const auto& draw) {
                       return !draw.state.depth;
                   }),
            "Camera-relative horizon draw was absent");
        expect(renderer.draw(frame, false), "Private scene target was unavailable");
        renderer.save_screenshot(std::string("scene-models-") + name + ".png");
        const auto pixels = renderer.readback();
        expect(std::ranges::count_if(pixels,
                   [](auto color) {
                       return color != toca::gpu::background;
                   })
                   > 1000,
            "Scene did not reach real GPU readback");
        std::cout << "PASS: " << name << " loading artwork, initial Honda, scenery/props and horizon: " << projected.vertices.size() << " projected vertices, "
                  << projected.indices.size() / 3 << " triangles, " << flags.actor_count() << " flag controllers\n";
        if (track == 0) {
            low_detail_sky_pixels(renderer, resources);
            vectra_rival_pixels(renderer, store, resources);
            flag_pixels(renderer, resources);
            car_shadow_pixels(renderer, resources, body);
        }
        if (track == 3) {
            horizon_pixels(renderer, resources);
        }
        if (track == 9) {
            lavaland_cone_pixels(renderer, resources);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (!(argc == 2)) {
            throw std::runtime_error("Usage: toca-scene-model-original-tests CANONICAL_ROOT");
        }
        reference(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
