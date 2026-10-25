#include "assets/store.hpp"
#include "frontend/resources.hpp"
#include "scene/finish_camera.hpp"
#include "scene/follow_camera.hpp"
#include "scene/view.hpp"
#include "scene/weather.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <iostream>
#include <set>
#include <string>

namespace {
using namespace toca;
using namespace toca::scene;
using Matrix = std::array<float, 16>;
using Point = std::array<std::int32_t, 3>;
constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr unsigned width = 1280, height = 720;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

CameraInputs camera_at(bool front, unsigned distance = 5) {
    std::array<SceneCamera, 1> cameras{};
    cameras[0].transform[12] = (front ? 1 : -1) * std::int32_t(distance) * 65536;
    cameras[0].transform[13] = std::int32_t(.35f * 65536);
    CameraInputs result{FinishCamera(cameras).world_to_view({0, 6553, 0}), default_projection(3, width, height)};
    if (distance > 5) {
        result.projection.x *= float(distance) / 5;
        result.projection.y *= float(distance) / 5;
        result.hide_player = true;
    }
    return result;
}

std::array<float, 3> transform(const std::array<float, 3>& point, const Matrix& matrix) {
    std::array<float, 3> result{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        result[axis] = matrix[axis] * point[0] + matrix[4 + axis] * point[1] + matrix[8 + axis] * point[2] + matrix[12 + axis];
    }
    return result;
}

// Independent Honda lens coordinates from434f00, including its medium offsets.
// Construct a white GPU mask without using the lamp-frame implementation.
gpu::Frame expected_lenses(const CameraInputs& camera, const Matrix& matrix, unsigned detail, bool front, bool multisample) {
    constexpr std::array<Point, 6> rear{
        {{-148045, 9109, -37879}, {-148503, 5111, -37552}, {-146931, 9175, -46137}, {-147389, 5111, -46333}, {-140377, 9175, -50069}, {-140639, 5111, -50987}}};
    constexpr std::array<Point, 6> medium_rear{
        {{-147062, 9436, -37879}, {-147520, 5438, -37552}, {-145948, 9502, -46137}, {-146406, 5438, -46333}, {-139394, 9502, -50069}, {-139656, 5438, -50987}}};
    constexpr std::array<Point, 4> headlights{{{142409, -1441, 24641}, {145555, -7471, 21430}, {134282, -1179, 43515}, {137625, -7405, 43581}}};
    gpu::Frame result;
    result.multisample = multisample;
    for (unsigned side = 0; side < 2; ++side) {
        const auto triangle = [&](std::array<unsigned, 3> indices) {
            if (side != 0) {
                std::swap(indices[1], indices[2]);
            }
            std::array<LitVertex, 3> vertices{};
            for (unsigned corner = 0; corner < 3; ++corner) {
                const auto& source = front ? headlights[indices[corner]] : (detail < 2 ? medium_rear : rear)[indices[corner]];
                std::array<float, 3> point{float(source[0]) / 65536, float(source[1]) / 65536, float(source[2]) / 65536};
                if (side != 0) {
                    point[2] = -point[2];
                }
                vertices[corner] = {transform(point, matrix), 0xffffffff};
            }
            append_projected_triangle(result, {}, vertices, camera);
        };
        if (!front && detail >= 2) {
            triangle({2, 4, 3});
            triangle({5, 3, 4});
        }
        triangle({0, 2, 1});
        triangle({3, 1, 2});
    }
    return result;
}

bool near_mask(const std::vector<Uint32>& mask, unsigned x, unsigned y) {
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const auto px = int(x) + dx, py = int(y) + dy;
            if (px >= 0 && py >= 0 && px < int(width) && py < int(height) && (mask[unsigned(py) * width + unsigned(px)] & 0xffffffu) != 0x101820u) {
                return true;
            }
        }
    }
    return false;
}

void check_car(gpu::Renderer& renderer, const frontend::CarModels& models, unsigned detail, const std::string& label, bool rival, unsigned& failures) {
    SceneResources resources{};
    resources.setup = resolve_setup({.car_id = 0, .weather_choice = 1, .detail = detail});
    if (!rival) {
        resources.car = models;
    }
    const std::array binding{RivalModel{1, &models}};
    SceneView view(renderer, resources, nullptr, nullptr, nullptr, rival ? std::span<const RivalModel>(binding) : std::span<const RivalModel>{});
    const auto light = scene_lighting(resources.setup);
    CarPose pose{identity};
    // A large authored tilt makes a detached physical-root overlay unambiguous.
    // MEDIUM must ignore it; source HIGH and Mega attach to node300 instead.
    pose.visual_angles = {128, -128};
    const auto render = [&](const CameraInputs& camera, const CarPose& state, const std::string& name) {
        const std::array rivals{RivalPose{1, state}};
        const auto poses = rival ? std::span<const RivalPose>(rivals) : std::span<const RivalPose>{};
        const CarPose hidden{identity};
        for (unsigned tick = 0; tick < (rival ? 11u : 1u); ++tick) {
            view.update_visibility(camera, rival ? hidden : state, {}, {}, nullptr, poses);
        }
        if (rival) {
            expect(view.rival_model_detail(1) == 1, label + ": distant rival must select MEDIUM");
        }
        const auto frame = view.frame(camera, light, rival ? hidden : state, {}, {}, {}, {}, nullptr, {}, {}, nullptr, nullptr, nullptr, false, nullptr, poses);
        expect(renderer.draw(frame, false), "Car-light render target unavailable");
        if (!name.empty()) {
            renderer.save_screenshot(label + "-" + name + ".png");
        }
        const auto pixels = renderer.readback();
        if (state.lights.headlights || state.lights.tail || state.lights.brake) {
            expect(!frame.dynamic_world_vertices.empty()
                       && std::ranges::any_of(frame.draws,
                           [](const auto& draw) {
                               return draw.dynamic_world && !draw.mesh && draw.state.depth && !draw.state.depth_always;
                           }),
                label + ": illuminated lenses must submit prelit world triangles with their source depth state");
            expect(renderer.draw(test::materialize_world(frame), false), "CPU lens reference target unavailable");
            expect(pixels == renderer.readback(), label + ": production lens pixels must exactly equal original CPU clipping/projection");
        }
        return pixels;
    };
    for (const std::string state : {"tail", "brake", "headlights"}) {
        const bool front = state == "headlights";
        // Probe the actual MEDIUM family under each profile's distance gates.
        const auto camera = camera_at(front, rival ? (detail == 4 ? 60u : 30u) : 5u);
        const auto unlit = render(camera, pose, state + "-off");
        auto illuminated = pose;
        illuminated.lights = {front, !front, state == "brake"};
        const auto pixels = render(camera, illuminated, state);
        Matrix attached = pose.object_to_world;
        if (!rival && detail >= 2) {
            const auto matrices = car_world_transforms(models.models[0], pose, detail);
            bool found = false;
            for (std::size_t index = 0; index < models.models[0].nodes.size(); ++index) {
                if ((models.models[0].nodes[index].id_flags & 0xffffffu) == 300) {
                    attached = matrices[index];
                    found = true;
                    break;
                }
            }
            expect(found, "Canonical HIGH model must expose body node300");
        }
        // The original coordinate table uses global graphics detail even when
        // the rival's selected MEDIUM model requires the physical-root matrix.
        expect(renderer.draw(expected_lenses(camera, attached, detail, front, detail == 4), false), "Expected lamp mask target unavailable");
        const auto mask = renderer.readback();
        std::size_t changed = 0, correct_color = 0, attached_pixels = 0;
        std::array<std::size_t, 2> sides{};
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                const auto index = y * width + x;
                if (pixels[index] == unlit[index]) {
                    continue;
                }
                ++changed;
                ++sides[x >= width / 2];
                attached_pixels += near_mask(mask, x, y);
                const auto r = (pixels[index] >> 16) & 255u, g = (pixels[index] >> 8) & 255u, b = pixels[index] & 255u;
                if (detail == 4 && !rival) {
                    correct_color += front ? r > 80 && g > 70 && b > 50 : r > 60 && r > g * 2 && r > b * 2;
                } else {
                    correct_color += front ? r > 245 && g > 245 && b >= 220 && b < 240 : (state == "brake" ? r > 245 : r >= 195 && r <= 205) && g < 10 && b < 10;
                }
            }
        }
        const bool visible = changed >= 16 && sides[0] >= 4 && sides[1] >= 4;
        const bool colored = correct_color >= 8 && correct_color * 2 >= changed;
        const bool attached_correctly = changed != 0 && attached_pixels * 100 >= changed * 98;
        std::cout << label << ' ' << state << ": changed=" << changed << " correct_color=" << correct_color << " attached=" << attached_pixels << " left/right=" << sides[0] << '/'
                  << sides[1] << '\n';
        if (!visible || !colored || !attached_correctly) {
            ++failures;
            std::cerr << "FAIL: " << label << ' ' << state << ": visible=" << visible << " color=" << colored << " body_attachment=" << attached_correctly << '\n';
        }
    }
}

void check_chase_lenses(gpu::Renderer& renderer, const frontend::CarModels& models, unsigned detail, const std::string& label, unsigned& failures) {
    SceneResources resources{};
    resources.setup = resolve_setup({.car_id = 0, .weather_choice = 1, .detail = detail});
    resources.car = models;
    SceneView view(renderer, resources);
    BodyPose body{};
    body.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
    body.object_to_world = identity;
    const FollowCamera follow(0, false, 0, false, detail);
    const CameraInputs camera{follow.world_to_view(body), default_projection(detail, width, height)};
    const auto light = scene_lighting(resources.setup);
    for (const bool tilted : {false, true}) {
        CarPose pose{identity};
        pose.visual_angles = tilted ? CarVisualAngles{20, -20} : CarVisualAngles{};
        const auto prefix = label + (tilted ? "-chase-tilted-" : "-chase-level-");
        const auto render = [&](CarLights lamps, const std::string& state) {
            pose.lights = lamps;
            view.update_visibility(camera, pose, {});
            expect(renderer.draw(view.frame(camera, light, pose, {}, {}), false), "Normal chase lamp target unavailable");
            renderer.save_screenshot(prefix + state + ".png");
            return renderer.readback();
        };
        const auto off = render({}, "off");
        const auto tail = render({.tail = true}, "tail");
        const auto brake = render({.tail = true, .brake = true}, "brake");
        std::array<std::size_t, 2> red_lenses{}, brighter_lenses{};
        std::size_t blue = 0;
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                const auto index = y * width + x;
                if (off[index] == brake[index]) {
                    continue;
                }
                const auto r = (brake[index] >> 16) & 255u, g = (brake[index] >> 8) & 255u, b = brake[index] & 255u;
                const auto dim_r = (tail[index] >> 16) & 255u;
                red_lenses[x >= width / 2] += detail == 4 ? r > 100 && r > g * 2 && r > b * 2 : r > 245 && g < 10 && b < 10;
                // Exercise the real body, depth test and normal View2 camera.
                // Both visible lens sections must brighten when braking, with
                // no dependency on the independent polygon-mask assertion.
                brighter_lenses[x >= width / 2] += detail == 4 ? r > dim_r + 10 && r > g * 2 && r > b * 2 : r > dim_r + 30 && g < 10 && b < 10;
                blue += b > 150 && r < 30 && g < 30;
            }
        }
        const bool visible = red_lenses[0] >= 8 && red_lenses[1] >= 8;
        const bool brighter = brighter_lenses[0] >= 8 && brighter_lenses[1] >= 8;
        std::cout << prefix << "red_lenses=" << red_lenses[0] << '/' << red_lenses[1] << " brighter_lenses=" << brighter_lenses[0] << '/' << brighter_lenses[1] << " blue=" << blue
                  << '\n';
        if (!visible || !brighter || blue != 0) {
            ++failures;
            std::cerr << "FAIL: " << prefix << "normal rear chase must show both red lenses brightening during braking\n";
        }
    }
}

void check_lens_coverage(const frontend::CarModels& models, const std::string& label) {
    const auto lenses = mega_car_lamp_lenses(models.models[0], models.car_id);
    std::array<bool, 4> covered{};
    for (const auto& lens : lenses) {
        expect(lens.broken_index < covered.size() && lens.front == (lens.broken_index < 2), label + ": lens must retain its source front/rear damage-side identity");
        covered[lens.broken_index] = true;
    }
    expect(covered == (models.car_id == 8 ? std::array{false, false, false, false} : std::array{true, true, true, true}),
        label + ": every emitting car must retain both headlight and both rear-lamp lens sides; only Type8 has none");
}

void check_glow_car(gpu::Renderer& renderer, const AssetStore& store, const frontend::CarModels& models, unsigned detail, const std::string& label) {
    SceneResources resources{};
    resources.setup = resolve_setup({.car_id = std::min(models.car_id, 9u), .weather_choice = 1, .detail = detail});
    resources.car = models;
    SceneView bare(renderer, resources);
    const auto images = store.open_archive("data/image640.bfp");
    for (std::size_t index = 0; index < 2; ++index) {
        const auto bytes = images.member(17 + index).bytes();
        resources.car_glow_bmps[index].assign(bytes.begin(), bytes.end());
    }
    SceneView glowing(renderer, resources);
    CarPose pose{identity};
    pose.visual_angles = {20, -20};
    const auto camera = camera_at(false);
    bool compared_glow = false;
    const auto render = [&](SceneView& view, CarLights lights) {
        pose.lights = lights;
        view.update_visibility(camera, pose, {});
        const auto& frame = view.frame(camera, scene_lighting(resources.setup), pose, {}, {});
        expect(renderer.draw(frame, false), "Fleet lamp target unavailable");
        const auto pixels = renderer.readback();
        if (!compared_glow && &view == &glowing && lights.brake && models.car_id == 0) {
            expect(!frame.dynamic_world_vertices.empty()
                       && std::ranges::any_of(frame.draws,
                           [](const auto& draw) {
                               return draw.dynamic_world && !draw.mesh && draw.state.screen && draw.state.key && !draw.state.depth_write;
                           }),
                label + ": lamp halos must use prelit dynamic world corners with their source screen material");
            expect(renderer.draw(test::materialize_world(frame), false), "CPU glow reference target unavailable");
            expect(pixels == renderer.readback(), label + ": lamp halo pixels must equal original CPU clipping/projection");
            compared_glow = true;
        }
        return pixels;
    };
    const auto off = render(bare, {});
    const auto lenses = render(bare, {.brake = true});
    const auto tail = render(glowing, {.tail = true});
    const auto brake = render(glowing, {.brake = true});
    renderer.save_screenshot(label + "-brake-glow.png");
    const auto broken = render(glowing, {.brake = true, .broken = {false, false, true, true}});
    std::array<std::size_t, 2> halo{}, brighter{};
    std::set<Uint32> lens_colors;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto index = y * width + x;
            expect(broken[index] == off[index], label + ": broken rear lights must suppress both lens emission and halo");
            halo[x >= width / 2] += ((brake[index] >> 16) & 255) > ((lenses[index] >> 16) & 255);
            brighter[x >= width / 2] += ((brake[index] >> 16) & 255) > ((tail[index] >> 16) & 255);
            if (lenses[index] != off[index]) {
                lens_colors.insert(lenses[index] & 0xffffff);
            }
        }
    }
    if (models.car_id == 8) {
        expect(brake == off, "Original tank has no rear lamps or halo");
    } else {
        expect(halo[0] > 8 && halo[1] > 8, label + ": both rear lamps need their original soft halo");
        expect(brighter[0] > 8 && brighter[1] > 8, label + ": braking must brighten both tail lamps and halos");
        if (detail == 4 && models.car_id != 9) {
            expect(lens_colors.size() > 12, label + ": Mega must preserve visible lens texture instead of flat red polygons");
        }
    }
    std::cout << label << ": halo=" << halo[0] << '/' << halo[1] << " brighter=" << brighter[0] << '/' << brighter[1] << " lens_colors=" << lens_colors.size() << '\n';
    if (detail == 4 && models.car_id == 0) {
        // Model UVs retain the source mirrored-repeat convention, including
        // V outside0..1. Emission must sample the same glass after a full period.
        auto repeated = resources;
        repeated.car_glow_bmps = {};
        for (auto& mesh : repeated.car.models[0].meshes) {
            for (auto& vertex : mesh.vertices) {
                vertex.u += 2;
                vertex.v += 2;
            }
        }
        SceneView periodic(renderer, repeated);
        const auto shifted = render(periodic, {.brake = true});
        std::size_t sampled = 0, matched = 0;
        for (std::size_t index = 0; index < lenses.size(); ++index) {
            if (lenses[index] != off[index]) {
                ++sampled;
                bool same = true;
                for (unsigned shift : {0u, 8u, 16u}) {
                    same &= std::abs(int((lenses[index] >> shift) & 255) - int((shifted[index] >> shift) & 255)) <= 3;
                }
                matched += same;
            }
        }
        expect(sampled > 100 && matched * 100 >= sampled * 99, "Mega emission must retain the original glass UV wrapping and lens pattern");
    }
    if (detail == 4 && (models.car_id < 8 || models.car_id == 10)) {
        pose.damage_levels[15] = pose.damage_levels[16] = 3;
        const auto damaged_off = render(bare, {});
        expect(render(bare, {.brake = true}) == damaged_off, label + ": a removed rear lens must not leave intact Mega emission behind");
    }
}

gpu::Frame expected_ground_mask(const CameraInputs& camera, const Matrix& attached, bool front, float ground_height, bool multisample) {
    gpu::Frame result;
    result.multisample = multisample;
    const auto axes = weather_camera(camera.world_to_view);
    // Independent Honda434f00 centers and reflected extents.442530 keeps
    // these images facing the camera;436c80 mirrors their centers in height.
    const Point center = front ? Point{150798, -4259, 32505} : Point{-154795, 6422, 43253};
    const float vertical = float(front ? 0xb328 : 0x8f56) / 65536, horizontal = float(front ? 0x3ffc : 0x3331) / 65536;
    for (const float side : {1.f, -1.f}) {
        auto point = transform({float(center[0]) / 65536, float(center[1]) / 65536, side * float(center[2]) / 65536}, attached);
        const auto offset = ground_height - point[1];
        point[1] = ground_height + (front ? offset : std::min(offset + float(0x3333) / 65536, -float(0x4ccc) / 65536));
        std::array<LitVertex, 4> vertices;
        for (unsigned corner = 0; corner < 4; ++corner) {
            vertices[corner] = {point, 0xffffffff};
            for (unsigned axis = 0; axis < 3; ++axis) {
                vertices[corner].position[axis] +=
                    float(axes.right[axis]) / 65536 * (corner & 2 ? horizontal : -horizontal) + float(axes.up[axis]) / 65536 * (corner & 1 ? vertical : -vertical);
            }
        }
        for (const auto& indices : {std::array{0u, 3u, 2u}, std::array{0u, 1u, 3u}}) {
            append_projected_triangle(result, {}, {vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]}, camera);
        }
    }
    return result;
}

void check_ground_car(gpu::Renderer& renderer, const AssetStore& store, const frontend::CarModels& models, unsigned detail, const std::string& label) {
    SceneResources resources{};
    resources.setup = resolve_setup({.car_id = 0, .weather_choice = 3, .detail = detail});
    resources.car = models;
    const auto images = store.open_archive("data/image640.bfp");
    for (std::size_t index = 0; index < 2; ++index) {
        const auto bytes = images.member(17 + index).bytes();
        resources.car_glow_bmps[index].assign(bytes.begin(), bytes.end());
    }
    SceneView wet(renderer, resources);
    auto dry_resources = resources;
    dry_resources.setup = resolve_setup({.car_id = 0, .weather_choice = 1, .detail = detail});
    SceneView dry(renderer, dry_resources);
    CarPose pose{identity};
    pose.object_to_world[13] = .5f;
    Matrix attached = identity;
    const auto matrices = car_world_transforms(models.models[0], pose, detail);
    for (std::size_t index = 0; index < models.models[0].nodes.size(); ++index) {
        if ((models.models[0].nodes[index].id_flags & 0xffffffu) == 300) {
            attached = matrices[index];
        }
    }
    for (const bool front : {false, true}) {
        const auto camera = camera_at(front);
        pose.lights = front ? CarLights{.headlights = true} : CarLights{.brake = true};
        const auto render = [&](SceneView& view, unsigned reflection_triangles) {
            view.update_visibility(camera, pose, {});
            const auto frame = view.frame(camera, scene_lighting(resources.setup), pose, {}, {});
            unsigned actual = 0;
            for (const auto& draw : frame.draws) {
                if (draw.state.depth_always) {
                    expect(draw.dynamic_world && !draw.mesh && draw.count % 6 == 0 && draw.state.depth && draw.state.depth_write && draw.state.blend && draw.state.key
                               && draw.state.screen,
                        label + ": flags1 reflection must retain prelit triangles, ALWAYS depth writes and screen blending");
                    actual += draw.count / 6;
                }
            }
            expect(actual == reflection_triangles, label + ": wet-ground queue must stay separate from the visible halos");
            expect(renderer.draw(frame, false), "Wet-ground lamp target unavailable");
            const auto pixels = renderer.readback();
            if (reflection_triangles != 0) {
                expect(renderer.draw(test::materialize_world(frame), false), "CPU wet-ground lamp reference target unavailable");
                expect(pixels == renderer.readback(), label + ": production ALWAYS-depth reflection pixels must exactly equal CPU clipping/projection");
            }
            return pixels;
        };
        pose.light_ground.reset();
        const auto bare = render(wet, 0);
        pose.light_ground.emplace();
        const auto reflected = render(wet, 4);
        renderer.save_screenshot(label + (front ? "-head-ground.png" : "-brake-ground.png"));
        expect(renderer.draw(expected_ground_mask(camera, attached, front, 0, detail == 4), false), "Expected reflected lamp mask unavailable");
        const auto mask = renderer.readback();
        std::array<std::size_t, 2> changed{}, placed{};
        std::array<std::uint64_t, 2> energy{};
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                const auto index = y * width + x, side = unsigned(x >= width / 2);
                if (reflected[index] != bare[index]) {
                    ++changed[side];
                    placed[side] += near_mask(mask, x, y);
                    for (const unsigned shift : {0u, 8u, 16u}) {
                        energy[side] += unsigned(std::max(0, int((reflected[index] >> shift) & 255) - int((bare[index] >> shift) & 255)));
                    }
                }
            }
        }
        for (unsigned side = 0; side < 2; ++side) {
            expect(changed[side] > 16 && placed[side] * 100 >= changed[side] * 98, label + ": both ground images must follow the source contact-height placement");
        }
        pose.light_ground->surface.fill(1);
        expect(render(wet, 0) == bare, label + ": non-road wheel surfaces must suppress ground images without removing halos");
        pose.light_ground->surface.fill(0);
        for (auto& wheel : pose.light_ground->wheel_position) {
            wheel[1] = 2 * 65536;
        }
        expect(render(wet, 4) == bare, label + ": airborne wheel distance must fade the ground image while retaining the halo");
        pose.light_ground.emplace();
        const auto dry_ground = render(dry, 0);
        pose.light_ground.reset();
        expect(render(dry, 0) == dry_ground, label + ": dry weather must not emit wet-ground images");
        if (!front) {
            pose.lights = {.tail = true};
            const auto tail_bare = render(wet, 0);
            pose.light_ground.emplace();
            const auto tail = render(wet, 4);
            std::array<std::uint64_t, 2> tail_energy{};
            for (unsigned y = 0; y < height; ++y) {
                for (unsigned x = 0; x < width; ++x) {
                    const auto index = y * width + x;
                    for (const unsigned shift : {0u, 8u, 16u}) {
                        tail_energy[x >= width / 2] += unsigned(std::max(0, int((tail[index] >> shift) & 255) - int((tail_bare[index] >> shift) & 255)));
                    }
                }
            }
            expect(energy[0] > tail_energy[0] && energy[1] > tail_energy[1], label + ": braking must brighten each wet-ground image");
        }
        std::cout << label << (front ? " head" : " rear") << " ground=" << changed[0] << '/' << changed[1] << " placed=" << placed[0] << '/' << placed[1] << '\n';
    }
}

void run(int argc, char** argv) {
    expect(argc == 2 || argc == 3, "Expected CANONICAL_ROOT [MEGA_MODEL_DIRECTORY]");
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated car-light test");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA car lights", width, height, 0), SDL_DestroyWindow);
    gpu::check(bool(window), "Create isolated car-light window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(argv[1]);
    const auto original = frontend::load_car_models(store, 0);
    unsigned failures = 0;
    check_car(renderer, original, 3, "original-high", false, failures);
    check_car(renderer, original, 1, "original-medium", false, failures);
    check_car(renderer, original, 3, "original-medium-rival", true, failures);
    check_chase_lenses(renderer, original, 3, "original-high", failures);
    check_ground_car(renderer, store, original, 3, "original-high");
    for (unsigned car = 0; car <= 10; ++car) {
        const auto models = car == 0 ? original : frontend::load_car_models(store, car);
        check_lens_coverage(models, "mega-original-fallback-car" + std::to_string(car));
        check_glow_car(renderer, store, models, 3, "original-car" + std::to_string(car));
    }
    for (unsigned detail : {0u, 1u, 2u}) {
        check_glow_car(renderer, store, original, detail, "original-detail" + std::to_string(detail));
    }
    if (argc == 3) {
        const auto mega = frontend::load_car_models(store, 0, argv[2]);
        expect(!mega.models[0].textures.empty() && mega.models[0].textures[0].encoding == ImageEncoding::webp, "Requested Mega Honda must load its GLB");
        check_car(renderer, mega, 4, "mega-high", false, failures);
        check_car(renderer, mega, 4, "mega-medium-rival", true, failures);
        check_chase_lenses(renderer, mega, 4, "mega-high", failures);
        check_ground_car(renderer, store, mega, 4, "mega-high");
        for (unsigned car = 0; car <= 10; ++car) {
            const auto models = car == 0 ? mega : frontend::load_car_models(store, car, argv[2]);
            check_lens_coverage(models, "mega-car" + std::to_string(car));
            check_glow_car(renderer, store, models, 4, "mega-car" + std::to_string(car));
        }
    }
    expect(failures == 0, std::to_string(failures) + " GPU lamp cases failed color, visibility or body attachment");
    std::cout << "PASS: real SceneView tail/brake/headlight colors and HIGH/MEDIUM body attachment\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        run(argc, argv);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
