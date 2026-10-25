#include "assets/store.hpp"
#include "formats/model_validation.hpp"
#include "image/decode.hpp"
#include "scene/body.hpp"
#include "scene/championship_finale_view.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/shadow_support.hpp"
#include "world_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

bool close(float first, float second) {
    return std::abs(first - second) < .00002f;
}

using Matrix = std::array<float, 16>;
using Textures = std::vector<toca::gpu::Texture>;

struct SourceActor {
    const toca::Model* model;
    std::vector<Matrix> transforms;
    const Textures* textures;
};

Textures source_textures(toca::gpu::Renderer& renderer, const toca::Model& model) {
    Textures result;
    for (const auto& texture : model.textures) {
        result.push_back(renderer.upload(toca::decode_bmp(texture.bmp).get(), true));
    }
    return result;
}

bool same_model(const toca::Model& a, const toca::Model& b) {
    if (!toca::model_structure_matches(a, b) || !toca::model_node_transforms_match(a, b)) {
        return false;
    }
    for (std::size_t i = 0; i < a.textures.size(); ++i) {
        if (a.textures[i].encoding != b.textures[i].encoding || a.textures[i].bmp != b.textures[i].bmp) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.meshes.size(); ++i) {
        const auto& x = a.meshes[i];
        const auto& y = b.meshes[i];
        if (x.vertices.size() != y.vertices.size() || x.faces.size() != y.faces.size()) {
            return false;
        }
        for (std::size_t v = 0; v < x.vertices.size(); ++v) {
            if (x.vertices[v].position != y.vertices[v].position || x.vertices[v].normal != y.vertices[v].normal || x.vertices[v].u != y.vertices[v].u
                || x.vertices[v].v != y.vertices[v].v) {
                return false;
            }
        }
        for (std::size_t f = 0; f < x.faces.size(); ++f) {
            if (x.faces[f].indices != y.faces[f].indices || x.faces[f].texture != y.faces[f].texture || x.faces[f].color != y.faces[f].color) {
                return false;
            }
        }
    }
    return true;
}

void unchanged_teams(const toca::scene::ChampionshipFinaleResources& source, const toca::scene::ChampionshipFinaleResources& candidate) {
    expect(source.credits == candidate.credits, "Mega Finale must preserve the exact CHAMPFIN credits stream");
    for (std::size_t team = 0; team < source.teams.size(); ++team) {
        expect(same_model(source.teams[team], candidate.teams[team]), "Mega vehicle replacement must preserve all eight original driver models/textures/placements");
    }
}

void check_mega_resources(const toca::AssetStore& store, const std::filesystem::path& directory, const toca::scene::ChampionshipFinaleResources& original,
    const toca::scene::ChampionshipFinaleResources& upgraded) {
    using namespace toca;
    using namespace toca::scene;
    const std::array<const Model*, 2> sources{&original.pace, &original.transporter}, replacements{&upgraded.pace, &upgraded.transporter};
    for (std::size_t role = 0; role < sources.size(); ++role) {
        const auto& source = *sources[role];
        const auto& replacement = *replacements[role];
        expect(model_structure_matches(source, replacement) && model_node_transforms_match(source, replacement),
            "Both actual Finale GLBs must preserve source mesh/material slots, node flags, parents, pivots and variants");
        expect(!same_model(source, replacement), "Both Finale roles must load actual generated assets instead of silently falling back");
        for (const auto& image : replacement.textures) {
            expect(image.encoding == ImageEncoding::webp && image.bmp.size() > 20, "Finale GLBs must embed their final explicit-alpha WebP images");
            expect(bool(decode_model_texture(image)), "Every real Finale GLB image must decode through the encoding-aware model path");
        }
        const auto wheel = role == 0 ? 3u : 1u;
        const auto radius = role == 0 ? 22937.f / 65536 : 30277.f / 65536;
        const auto width = role == 0 ? .25f : 21626.f / 65536;
        float maximum_radius = 0, minimum_z = 1, maximum_z = -1;
        for (const auto& vertex : replacement.meshes.at(wheel).vertices) {
            maximum_radius = std::max(maximum_radius, std::hypot(vertex.position[0], vertex.position[1]));
            minimum_z = std::min(minimum_z, vertex.position[2]);
            maximum_z = std::max(maximum_z, vertex.position[2]);
        }
        expect(std::abs(maximum_radius - radius) < 3.f / 65536 && close(maximum_z - minimum_z, width),
            "Finale wheels must retain the distinct measured Pace/Truck source radii and axial widths");
        const auto& front = replacement.nodes.at(2).transform;
        const auto& rear = replacement.nodes.at(4).transform;
        expect(close(front[12] - rear[12], role == 0 ? 171048.f / 65536 : 291110.f / 65536)
                   && close(front[14] - replacement.nodes.at(3).transform[14], role == 0 ? 91750.f / 65536 : 138412.f / 65536),
            "Pace and transporter must keep their own original wheelbase, track and four animation pivots");
    }
    unchanged_teams(original, upgraded);
    const auto work = std::filesystem::current_path() / ("finale-role-fallback-" + std::to_string(SDL_GetTicksNS()));
    std::filesystem::create_directories(work / "finale");

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{work};

    constexpr std::array<std::string_view, 2> roles{"pace-car", "transporter"};
    const auto restore = [&] {
        for (const auto role : roles) {
            const auto filename = std::string(role) + ".glb";
            std::filesystem::copy_file(directory / "finale" / filename, work / "finale" / filename, std::filesystem::copy_options::overwrite_existing);
        }
    };
    for (std::size_t broken_role = 0; broken_role < roles.size(); ++broken_role) {
        for (const bool corrupt : {false, true}) {
            restore();
            const auto path = work / "finale" / (std::string(roles[broken_role]) + ".glb");
            if (corrupt) {
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                file << "broken Finale GLB";
                file.close();
            } else {
                std::filesystem::remove(path);
            }
            const auto fallback = load_championship_finale_resources(store, work);
            expect(same_model(fallback.pace, broken_role == 0 ? original.pace : upgraded.pace)
                       && same_model(fallback.transporter, broken_role == 1 ? original.transporter : upgraded.transporter),
                "Missing/corrupt Finale asset must fall back only its own role while the other real GLB remains loaded");
            unchanged_teams(original, fallback);
        }
    }
}

toca::gpu::Frame isolate_role(const toca::gpu::Frame& frame, const toca::Model& model, const toca::scene::ChampionshipFinaleCarPose& pose) {
    toca::scene::CarPose car{pose.object_to_world};
    car.steering_angle = pose.steering_angle;
    car.wheel_angles = pose.wheel_angles;
    const auto transforms = toca::scene::car_world_transforms(model, car, 3);
    toca::gpu::Frame isolated;
    isolated.world_views = frame.world_views;
    isolated.model_instances = frame.model_instances;
    for (const auto& draw : frame.draws) {
        if (draw.mesh && draw.model_instance < frame.model_instances.size()) {
            const auto& instance = frame.model_instances[draw.model_instance];
            if (instance.ambient_flags[3] == 16 && std::ranges::find(transforms, instance.object_to_world) != transforms.end()) {
                isolated.draws.push_back(draw);
            }
        }
    }
    expect(!isolated.draws.empty(), "Actual Finale view must submit the selected role's resident geometry");
    return isolated;
}

void mega_convoy_views(toca::gpu::Renderer& renderer, const toca::scene::SceneResources& scene, const toca::scene::ChampionshipFinaleResources& original,
    const toca::scene::ChampionshipFinaleResources& upgraded, const char* artwork_directory, std::span<const std::uint8_t> flags, const toca::scene::HorizonFrame& actors) {
    using namespace toca;
    using namespace toca::scene;
    constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    constexpr unsigned width = 1920, height = 1080;

    struct View {
        const char* name;
        std::array<float, 3> eye;
    };

    for (const bool transporter : {false, true}) {
        const auto role = std::string(transporter ? "transporter" : "pace-car");
        const float length = transporter ? 4.446396f : 2.239548f;
        const std::array<View, 6> cameras{
            {{"front", {length + 4, .6f, 0}}, {"rear", {-length - 4, .6f, 0}}, {"left", {0, .6f, length * 2.2f + 2}}, {"right", {0, .6f, -length * 2.2f - 2}},
                {"front-slant", {length * 1.7f + 2, 1.4f, length * 1.7f + 2}}, {"rear-slant", {-length * 1.7f - 2, 1.4f, -length * 1.7f - 2}}}};
        for (const bool replacement : {false, true}) {
            const auto& resources = replacement ? upgraded : original;
            const auto& model = transporter ? resources.transporter : resources.pace;
            ChampionshipFinaleView view(renderer, scene, resources, 0, artwork_directory);
            ChampionshipFinaleFrame state;
            state.pace.object_to_world = state.transporter.object_to_world = identity;
            (transporter ? state.pace : state.transporter).object_to_world[12] = 1000;
            state.teams = {0, 1, 2};
            const auto& pose = transporter ? state.transporter : state.pace;
            const auto prefix = "finale-" + role + (replacement ? "-mega-" : "-original-");
            for (const auto& camera : cameras) {
                std::array<SceneCamera, 1> authored{};
                std::array<std::int32_t, 3> target{};
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    authored[0].transform[12 + axis] = std::int32_t(std::lround(camera.eye[axis] * 65536));
                }
                FinishCamera finish(authored);
                state.camera_world_to_view = finish.world_to_view(target);
                state.camera_position_q16 = finish.position();
                const auto frame = view.frame(state, flags, actors, width, height);
                const auto isolated = isolate_role(frame, model, pose);
                expect(renderer.draw_offscreen(isolated, {width, height}), "Finale role close-view target unavailable");
                const auto baseline = renderer.readback();
                expect(std::count_if(baseline.begin(), baseline.end(),
                           [](auto p) {
                               return p != gpu::background;
                           })
                           > 1000,
                    "Each Finale source/Mega camera must show actual selected vehicle pixels");
                renderer.save_screenshot(prefix + camera.name + ".png");
                if (std::string_view(camera.name) == "front-slant") {
                    auto moved = state;
                    auto& animated = transporter ? moved.transporter : moved.pace;
                    animated.steering_angle = 0x180;
                    animated.wheel_angles = {0x180, 0x220, 0x2c0, 0x340};
                    const auto stationary_transforms = car_world_transforms(model, CarPose{pose.object_to_world}, 3);
                    CarPose changed{animated.object_to_world};
                    changed.steering_angle = animated.steering_angle;
                    changed.wheel_angles = animated.wheel_angles;
                    const auto animated_transforms = car_world_transforms(model, changed, 3);
                    for (std::size_t node = 2; node <= 5; ++node) {
                        expect(animated_transforms[node] != stationary_transforms[node], "All four original Finale wheel pivots must execute steering/rolling animation");
                        expect(animated_transforms[node][12] == stationary_transforms[node][12] && animated_transforms[node][13] == stationary_transforms[node][13]
                                   && animated_transforms[node][14] == stationary_transforms[node][14],
                            "Finale wheel animation must retain each original contact/axle pivot");
                    }
                    const auto moved_frame = view.frame(moved, flags, actors, width, height);
                    expect(renderer.draw_offscreen(isolate_role(moved_frame, model, animated), {width, height}), "Animated Finale role target unavailable");
                    expect(renderer.readback() != baseline, "Real Finale steering/rolling must change the rendered wheel pixels");
                    renderer.save_screenshot(prefix + "steer-roll.png");
                }
            }
        }
    }
}

std::array<SourceActor, 5> source_actors(const toca::scene::ChampionshipFinaleResources& resources, const toca::scene::ChampionshipFinaleFrame& state, const Textures& pace,
    const Textures& transporter, const std::array<Textures, 8>& teams) {
    using namespace toca::scene;
    const auto car_pose = [](const ChampionshipFinaleCarPose& source) {
        CarPose result{source.object_to_world};
        result.steering_angle = source.steering_angle;
        result.wheel_angles = source.wheel_angles;
        result.visual_angles = source.visual_angles;
        return result;
    };
    std::array<SourceActor, 5> result{{{&resources.pace, car_world_transforms(resources.pace, car_pose(state.pace), 3), &pace},
        {&resources.transporter, car_world_transforms(resources.transporter, car_pose(state.transporter), 3), &transporter}}};
    for (std::uint32_t rank = 0; rank < state.teams.size(); ++rank) {
        const auto team = state.teams[rank];
        result[rank + 2] = {
            &resources.teams.at(team), model_world_transforms(resources.teams.at(team), championship_finale_trophy_transform(state.transporter, rank)), &teams.at(team)};
    }
    return result;
}

// Retain the former finale CPU transform independently of ModelGeometry and
// ModelInstance. Its translation-first additions differ from ordinary cars.
std::array<float, 3> source_transform(const std::array<float, 3>& value, const Matrix& matrix, bool position) {
    std::array<float, 3> result{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result[axis] = position ? matrix[12 + axis] : 0;
        for (std::size_t component = 0; component < 3; ++component) {
            result[axis] += value[component] * matrix[component * 4 + axis];
        }
    }
    return result;
}

struct ActorComparison {
    toca::gpu::Frame resident, expected;
    std::array<std::vector<std::shared_ptr<const toca::gpu::WorldMesh>>, 5> meshes;
    std::vector<SDL_GPUTexture*> reference_textures;
};

ActorComparison compare_source_actors(const toca::gpu::Frame& frame, std::size_t world_draws, const std::array<SourceActor, 5>& actors, const toca::scene::CameraInputs& camera,
    const toca::scene::Lighting& lighting, const toca::scene::ChampionshipFinaleDrawCounts& counts) {
    using namespace toca;
    using namespace toca::scene;
    ActorComparison result;
    result.resident.world_views = frame.world_views;
    result.resident.model_instances = frame.model_instances;
    std::size_t next_draw = world_draws;
    for (std::size_t actor_index = 0; actor_index < actors.size(); ++actor_index) {
        const auto& actor = actors[actor_index];
        const auto first_projected = result.expected.vertices.size();
        std::uint32_t source_triangles = 0;
        for (std::size_t node_index = 0; node_index < actor.model->nodes.size(); ++node_index) {
            const auto& node = actor.model->nodes[node_index];
            if (node.variants.empty() || node.variants.front() == 0xffff) {
                continue;
            }
            const auto& mesh = actor.model->meshes.at(node.variants.front());
            const auto& matrix = actor.transforms.at(node_index);
            source_triangles += std::uint32_t(mesh.faces.size());
            std::size_t face_index = 0;
            while (face_index < mesh.faces.size()) {
                expect(next_draw < frame.draws.size(), "Finale resident model lost a source material run");
                const auto& draw = frame.draws[next_draw++];
                const auto texture_slot = mesh.faces[face_index].texture;
                auto end = face_index + 1;
                while (end < mesh.faces.size() && mesh.faces[end].texture == texture_slot) {
                    ++end;
                }
                expect(draw.mesh && draw.model_instance < frame.model_instances.size() && draw.instance_count == 1 && !draw.overlay && draw.first == face_index * 6
                           && draw.count == (end - face_index) * 6 && draw.vertex_offset == 0,
                    "Finale nodes must retain resident source face order and material-run boundaries");
                expect(draw.mesh->vertices.size() == mesh.faces.size() * 3 && draw.mesh->attributes.size() == mesh.faces.size() * 3,
                    "Finale resident nodes must retain all source corners and normals");
                const auto& instance = frame.model_instances[draw.model_instance];
                expect(instance.object_to_world == matrix && instance.ambient_flags[3] == 16
                           && instance.direction_diffuse == std::array<float, 4>{lighting.direction[0], lighting.direction[1], lighting.direction[2], lighting.diffuse}
                           && instance.ambient_flags[0] == lighting.ambient[0] && instance.ambient_flags[1] == lighting.ambient[1]
                           && instance.ambient_flags[2] == lighting.ambient[2],
                    "Finale instance must preserve independent source hierarchy, lighting and translation-first arithmetic");
                bool palette = false, clamp = false;
                if (texture_slot >= 0) {
                    const auto& texture = actor.model->textures.at(std::size_t(texture_slot));
                    const auto decoded = decode_model_texture(texture);
                    palette = SDL_ISPIXELFORMAT_INDEXED(decoded->format);
                    clamp = texture.encoding == ImageEncoding::bmp;
                }
                const gpu::State expected_state{.texture = texture_slot < 0 ? nullptr : actor.textures->at(std::size_t(texture_slot)).get(),
                    .depth = true,
                    .key = texture_slot >= 0,
                    .clamp = clamp,
                    .palette = palette};
                auto actual_state = draw.state;
                expect((actual_state.texture != nullptr) == (expected_state.texture != nullptr), "Finale material must retain its source texture slot");
                actual_state.texture = expected_state.texture;
                expect(actual_state == expected_state, "Finale material must retain canonical depth, key and sampler settings");
                result.resident.draws.push_back(draw);
                result.reference_textures.push_back(expected_state.texture);
                result.meshes[actor_index].push_back(draw.mesh);
                for (; face_index < end; ++face_index) {
                    const auto& face = mesh.faces[face_index];
                    const auto material = face.texture >= 0 ? std::array<std::uint8_t, 4>{255, 255, 255, 255} : face.color;
                    const auto packed = (std::uint32_t(material[0]) << 16) | (std::uint32_t(material[1]) << 8) | material[2] | (std::uint32_t(material[3]) << 24);
                    std::array<LitVertex, 3> triangle;
                    for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
                        const auto& source = mesh.vertices.at(face.indices[corner]);
                        const auto& vertex = draw.mesh->vertices[face_index * 3 + corner];
                        const auto& attribute = draw.mesh->attributes[face_index * 3 + corner];
                        expect(vertex.position == source.position && vertex.color == packed && vertex.u == source.u && vertex.v == source.v && attribute.normal == source.normal,
                            "Finale immutable geometry must retain source positions, normals, material colors and UV seams");
                        triangle[corner] = {source_transform(source.position, matrix, true), model_vertex_color(source_transform(source.normal, matrix, false), material, lighting),
                            source.u, source.v};
                    }
                    append_projected_triangle(result.expected, expected_state, triangle, camera);
                }
            }
        }
        expect(
            counts.model_triangles[actor_index] == source_triangles && source_triangles != 0, "Finale draw counts must report all submitted source triangles before GPU culling");
        expect(result.expected.vertices.size() > first_projected, "Finale fixture camera must retain visible source triangles for each convoy actor and podium placement");
    }
    expect(next_draw < frame.draws.size() && frame.draws[next_draw].overlay, "Finale resident actors must precede upright credit overlays");
    const auto projected = test::materialize_world(result.resident);
    expect(projected.vertices.size() == result.expected.vertices.size() && projected.indices == result.expected.indices
               && std::memcmp(projected.vertices.data(), result.expected.vertices.data(), projected.vertices.size() * sizeof(gpu::Vertex)) == 0,
        "Resident finale projection must exactly match the independent former CPU transform, lighting and clipping consumer");
    return result;
}

std::vector<std::uint32_t> compare_pixels(toca::gpu::Renderer& renderer, const ActorComparison& comparison, std::uint32_t width, std::uint32_t height) {
    expect(renderer.draw_offscreen(comparison.expected, {width, height}), "Finale CPU oracle target unavailable");
    const auto expected = renderer.readback();
    expect(renderer.draw_offscreen(comparison.resident, {width, height}), "Finale resident actor target unavailable");
    const auto actual = renderer.readback();
    expect(renderer.last_frame_timing().model_draw_calls > 0 && renderer.last_frame_timing().model_instances >= 5,
        "Finale comparison must execute actual GPU model draws for the convoy and three attached drivers");
    expect(actual == expected, "Canonical finale actors must exactly match independent CPU-oracle GPU pixels");
    expect(std::count_if(actual.begin(), actual.end(),
               [](auto pixel) {
                   return pixel != toca::gpu::background;
               })
               > 100,
        "Canonical finale convoy must contribute visible textured GPU pixels");
    return actual;
}

void synthetic() {
    using namespace toca::scene;
    ChampionshipFinaleCarPose truck;
    truck.object_to_world = {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 12, 4, -9, 1};
    constexpr std::array<std::array<std::int32_t, 3>, 3> offsets{{{-35127, 34078, 0}, {-112984, 24838, 18677}, {26017, 18022, 15400}}};
    for (std::uint32_t rank = 0; rank < 3; ++rank) {
        const auto matrix = championship_finale_trophy_transform(truck, rank);
        expect(close(matrix[12], 12 + float(offsets[rank][2]) / 65536) && close(matrix[13], 4 + float(offsets[rank][1]) / 65536)
                   && close(matrix[14], -9 - float(offsets[rank][0]) / 65536),
            "Top-three driver placement must rotate and translate the exact local source offset with its transporter parent");
        for (std::size_t component = 0; component < 12; ++component) {
            expect(matrix[component] == truck.object_to_world[component], "Attached drivers must inherit the transporter basis");
        }
    }
}

void reference(const char* root, const char* artwork_directory, const char* mega_models = nullptr) {
    using namespace toca;
    using namespace toca::scene;
    gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize private finale view");

    struct Quit {
        ~Quit() {
            SDL_Quit();
        }
    } quit;

    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Championship finale", 640, 480, SDL_WINDOW_RESIZABLE), SDL_DestroyWindow);
    gpu::check(bool(window), "Create private finale window");
    gpu::Renderer renderer(window.get());
    const AssetStore store(root);
    const auto resources = load_championship_finale_resources(store);
    expect(resources.pace.nodes.size() == 8 && resources.transporter.nodes.size() == 7 && resources.pace.nodes[0].id_flags == 506 && resources.transporter.nodes[0].id_flags == 507
               && resources.pace.textures.size() == 5 && resources.transporter.textures.size() == 1,
        "Finale resources must load the actual CHAMPFIN pace car and transporter without substituting race-car models");
    constexpr std::array<std::string_view, 8> names{"HONDADR_1", "AUDIDR_1", "S40DR_1", "VECTRADR_1", "NISSANDR_1", "MONDR_1", "RENDR_1", "P406DR_1"};
    for (std::size_t team = 0; team < names.size(); ++team) {
        expect(resources.teams[team].meshes.size() == 1 && resources.teams[team].meshes[0].name == names[team] && resources.teams[team].nodes.size() == 2
                   && resources.teams[team].textures.size() == 1,
            "All eight actual driver models must retain original sequential CHAMPFIN order, hierarchy and team textures");
    }
    expect(std::string(resources.credits.begin(), resources.credits.end()).starts_with("PAGE\r\nCREDITS\r\nIN ALPHABETICAL ORDER\r\nPAGE"),
        "The real PAGE-delimited credits must reach the scene owner");
    expect(resources.fonts[0].line_height == 36 && resources.fonts[1].line_height == 28 && resources.fonts[0].find('C') && resources.fonts[1].find('\''),
        "Finale must retain the original BIG640/LIL640 headings, rows and credit punctuation");
    const auto scene = load_scene_resources(store, resolve_setup({.track_id = 1, .weather_choice = 1}));
    const HpcSurface ground(scene.collision_hpc);
    const auto starts = championship_finale_starts(scene);
    const auto truck_body = initial_body_pose(ground, starts[0], 0), pace_body = initial_body_pose(ground, starts[1], 1);
    ChampionshipFinaleFrame state;
    state.transporter.object_to_world = truck_body.object_to_world;
    state.pace.object_to_world = pace_body.object_to_world;
    // Authored test camera frames every convoy object in one image. Runtime
    // camera phase/timing coverage belongs to the native finale acceptance.
    auto target = truck_body.position;
    for (std::size_t axis = 0; axis < target.size(); ++axis) {
        target[axis] += (pace_body.position[axis] - target[axis]) / 2;
    }
    target[1] += 65536;
    std::array<SceneCamera, 1> authored{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        authored[0].transform[12 + axis] = target[axis];
    }
    authored[0].transform[12] += 12 * 65536;
    authored[0].transform[13] += 10 * 65536;
    authored[0].transform[14] += 32 * 65536;
    FinishCamera camera(authored);
    camera.update(target);
    state.camera_world_to_view = camera.world_to_view(target);
    state.camera_position_q16 = camera.position();
    state.teams = {0, 0, 7}; // Two podium drivers from one team still create two instances.
    state.credits = {{"CREDITS", true}, {"IN ALPHABETICAL ORDER", false}};
    SourceRandom random(12345);
    FlagAnimation flags(scene.flags, random);
    HorizonActors actors(scene.moving_horizon);
    ChampionshipFinaleView view(renderer, scene, resources, 0);
    SceneView ordinary(renderer, scene);
    const auto pace_textures = source_textures(renderer, resources.pace), transporter_textures = source_textures(renderer, resources.transporter);
    std::array<Textures, 8> team_textures;
    for (std::size_t team = 0; team < team_textures.size(); ++team) {
        team_textures[team] = source_textures(renderer, resources.teams[team]);
    }
    const auto lighting = scene_lighting(scene.setup);
    for (const auto size : {std::array{640, 480}, std::array{960, 540}, std::array{960, 600}}) {
        gpu::check(SDL_SetWindowSize(window.get(), size[0], size[1]), "Resize finale target");
        gpu::check(SDL_SyncWindow(window.get()), "Synchronize finale drawable");
        SDL_PumpEvents();
        CameraInputs inputs{state.camera_world_to_view, default_projection(3, size[0], size[1])};
        inputs.registered_camera_position = state.camera_position_q16;
        CarPose regular_car{truck_body.object_to_world};
        regular_car.shadow = car_shadow_footprint(car_shadow_input(truck_body, scene.setup));
        inputs.hide_player = true;
        const auto hidden_with_shadow = ordinary.frame(inputs, scene_lighting(scene.setup), regular_car, flags.variants(), actors.frame());
        regular_car.shadow.reset();
        const auto hidden_without_shadow = ordinary.frame(inputs, scene_lighting(scene.setup), regular_car, flags.variants(), actors.frame());
        expect(hidden_with_shadow.vertices.size() == hidden_without_shadow.vertices.size() && hidden_with_shadow.indices == hidden_without_shadow.indices,
            "Finale world-only selection must suppress the ordinary local car's shadow as well as its model");
        inputs.hide_player = false;
        const auto visible_car = ordinary.frame(inputs, scene_lighting(scene.setup), regular_car, flags.variants(), actors.frame());
        expect(test::materialize_world(visible_car).vertices.size() > test::materialize_world(hidden_without_shadow).vertices.size(),
            "The world-only mask must remove a normally visible local race car");
        auto frame = view.frame(state, flags.variants(), actors.frame(), size[0], size[1]);
        const auto counts = view.draw_counts();
        const auto comparison = compare_source_actors(
            frame, hidden_without_shadow.draws.size(), source_actors(resources, state, pace_textures, transporter_textures, team_textures), inputs, lighting, counts);
        expect(comparison.meshes[2] == comparison.meshes[3], "Repeated same-team podium drivers must share immutable GPU meshes across distinct placements");
        expect(counts.world_vertices == hidden_without_shadow.world_vertex_count(), "Finale world must match the ordinary scene with its local car hidden");
        expect(counts.credit_glyphs == 26, "Finale must submit all original credit glyphs without a race HUD");
        for (std::size_t model = 0; model < counts.model_triangles.size(); ++model) {
            if (counts.model_triangles[model] == 0) {
                throw std::runtime_error("Finale fixture camera lost CHAMPFIN instance " + std::to_string(model));
            }
        }
        expect(renderer.draw(frame, false), "Private finale target unavailable");
        expect(renderer.last_frame_timing().model_draw_calls > 0 && renderer.last_frame_timing().model_instances >= 5,
            "Canonical finale images must execute the resident convoy GPU model path");
        renderer.save_screenshot("championship-finale-" + std::to_string(size[0]) + "x" + std::to_string(size[1]) + ".png");
        const auto baseline_pixels = compare_pixels(renderer, comparison, size[0], size[1]);
        const auto reflected = view.frame(state, flags.variants(), actors.frame(), size[0], size[1], true, true);
        expect(reflected.vertices.size() == frame.vertices.size(), "Showdown reflection must retain complete scene and credit geometry");
        const auto geometry = frame.vertices.size() - std::size_t(counts.credit_glyphs) * 4;
        for (std::size_t index = 0; index < frame.vertices.size(); ++index) {
            const auto& source = frame.vertices[index];
            const auto& result = reflected.vertices[index];
            if (index < geometry) {
                expect(
                    close(result.x, float(size[0]) - source.x) && close(result.y, float(size[1]) - source.y), "Source mirror flags must reflect only the projected convoy world");
            } else {
                expect(result.x == source.x && result.y == source.y && result.u == source.u && result.v == source.v,
                    "Credits must retain upright original glyphs after world reflection");
            }
        }
        const auto projected = test::materialize_world(frame), projected_reflection = test::materialize_world(reflected);
        expect(projected.vertices.size() == projected_reflection.vertices.size(), "Showdown reflection must retain resident world triangles");
        for (std::size_t index = frame.vertices.size(); index < projected.vertices.size(); ++index) {
            const auto& source = projected.vertices[index];
            const auto& result = projected_reflection.vertices[index];
            expect(close(result.x, float(size[0]) - source.x) && close(result.y, float(size[1]) - source.y) && result.z == source.z && result.diffuse == source.diffuse,
                "Source mirror flags must reflect resident world geometry while retaining its depth and lighting");
        }
        for (std::uint32_t team = 0; team < 8; ++team) {
            auto selected = state;
            selected.teams = {team, team, team};
            const auto team_frame = view.frame(selected, flags.variants(), actors.frame(), size[0], size[1]);
            const auto team_comparison = compare_source_actors(team_frame, hidden_without_shadow.draws.size(),
                source_actors(resources, selected, pace_textures, transporter_textures, team_textures), inputs, lighting, view.draw_counts());
            expect(team_comparison.meshes[2] == team_comparison.meshes[3] && team_comparison.meshes[3] == team_comparison.meshes[4],
                "Every original team must share its resident upload among all three independently transformed attachments");
            if (size[0] == 640) {
                (void)compare_pixels(renderer, team_comparison, size[0], size[1]);
            }
            expect(std::all_of(view.draw_counts().model_triangles.begin(), view.draw_counts().model_triangles.end(),
                       [](auto count) {
                           return count != 0;
                       }),
                "Each original team must support all three transporter placements independently");
        }
        if (size[0] == 640) {
            auto moved = state;
            moved.pace.object_to_world[12] += .5f;
            moved.transporter.object_to_world[14] += .75f;
            moved.pace.steering_angle = 0x180;
            moved.transporter.wheel_angles[0] = 0x240;
            moved.camera_world_to_view[12] += .375f;
            auto moved_camera = inputs;
            moved_camera.world_to_view = moved.camera_world_to_view;
            moved_camera.hide_player = true;
            const auto moved_world = ordinary.frame(moved_camera, lighting, regular_car, flags.variants(), actors.frame());
            auto reusable = frame;
            // Warm once before recording allocation identity: draw runs and
            // source triangle counts stay fixed while pose/camera inputs vary.
            view.frame_into(reusable, moved, flags.variants(), actors.frame(), size[0], size[1]);
            const auto* vertices = reusable.vertices.data();
            const auto* indices = reusable.indices.data();
            const auto* draws = reusable.draws.data();
            const auto* projections = reusable.world_views.data();
            const auto* instances = reusable.model_instances.data();
            moved.pace.object_to_world[12] += .125f;
            moved.transporter.wheel_angles[1] = 0x180;
            view.frame_into(reusable, moved, flags.variants(), actors.frame(), size[0], size[1]);
            expect(reusable.vertices.data() == vertices && reusable.indices.data() == indices && reusable.draws.data() == draws && reusable.world_views.data() == projections
                       && reusable.model_instances.data() == instances,
                "Finale frame_into must reuse warmed TL, draw, projection and instance storage");
            const auto moved_comparison = compare_source_actors(reusable, moved_world.draws.size(),
                source_actors(resources, moved, pace_textures, transporter_textures, team_textures), moved_camera, lighting, view.draw_counts());
            expect(moved_comparison.meshes == comparison.meshes, "Finale camera, wheel and convoy poses must reuse every immutable source model upload");
            for (std::size_t actor_index = 0; actor_index < comparison.meshes.size(); ++actor_index) {
                for (std::size_t mesh = 0; mesh < comparison.meshes[actor_index].size(); ++mesh) {
                    expect(moved_comparison.meshes[actor_index][mesh]->buffer == comparison.meshes[actor_index][mesh]->buffer,
                        "Finale animation must retain the actual uploaded GPU buffer identity");
                }
            }
            expect(compare_pixels(renderer, moved_comparison, size[0], size[1]) != baseline_pixels, "Finale pose/camera instance changes must alter actual GPU pixels");
            expect(compare_pixels(renderer, comparison, size[0], size[1]) == baseline_pixels,
                "Later finale frames must not mutate an older queued frame's pose, camera, lighting or mesh bytes");

            ActorComparison retained;
            std::weak_ptr<const gpu::WorldMesh> lifetime;
            {
                ChampionshipFinaleView temporary(renderer, scene, resources, 0);
                const auto temporary_frame = temporary.frame(state, flags.variants(), actors.frame(), size[0], size[1]);
                retained = compare_source_actors(temporary_frame, hidden_without_shadow.draws.size(),
                    source_actors(resources, state, pace_textures, transporter_textures, team_textures), inputs, lighting, temporary.draw_counts());
                lifetime = retained.resident.draws.front().mesh;
                // Frame retains meshes, while texture ownership belongs to
                // the view. Preserve equivalent independent source textures
                // before destroying that owner; test the mesh lifetime only.
                for (std::size_t draw = 0; draw < retained.resident.draws.size(); ++draw) {
                    retained.resident.draws[draw].state.texture = retained.reference_textures[draw];
                }
            }
            expect(!lifetime.expired() && compare_pixels(renderer, retained, size[0], size[1]) == baseline_pixels,
                "Retained finale meshes must render the old pose after their view and model cache are destroyed");
            retained = {};
            expect(lifetime.expired(), "Finale immutable model uploads must release after their final queued frame owner");
        }
    }
    gpu::check(SDL_SetWindowSize(window.get(), 1920, 1080), "Resize Mega finale target");
    gpu::check(SDL_SyncWindow(window.get()), "Synchronize Mega finale target");
    SDL_PumpEvents();
    const auto mega_scene = load_scene_resources(store, resolve_setup({.track_id = 1, .weather_choice = 1, .detail = graphics_mega_detail}));
    ChampionshipFinaleView mega(renderer, mega_scene, resources, 0, artwork_directory);
    const auto canonical = view.frame(state, flags.variants(), actors.frame(), 1920, 1080);
    const auto replacement = mega.frame(state, flags.variants(), actors.frame(), 1920, 1080);
    const auto glyphs = mega.draw_counts().credit_glyphs;
    expect(glyphs == view.draw_counts().credit_glyphs && glyphs == 26, "Mega finale must retain every canonical heading and credit-row glyph");
    const auto first_original = canonical.vertices.size() - glyphs * 4, first_replacement = replacement.vertices.size() - glyphs * 4;
    for (std::size_t index = 0; index < glyphs * 4; ++index) {
        const auto& a = canonical.vertices[first_original + index];
        const auto& b = replacement.vertices[first_replacement + index];
        expect(a.x == b.x && a.y == b.y && a.z == b.z && a.rhw == b.rhw && a.u == b.u && a.v == b.v && a.diffuse == b.diffuse && a.specular == b.specular,
            "Mega credit headings must preserve exact layout, source glyphs and white vertex colors");
    }
    const auto smooth = std::count_if(replacement.draws.begin(), replacement.draws.end(), [](const auto& draw) {
        return draw.state.linear && draw.state.clamp && draw.state.blend && !draw.state.key && !draw.state.depth_write;
    });
    expect(smooth == glyphs, "Both BIG640 headings and LIL640 credit rows must use smooth white artwork with the original black edges");
    expect(renderer.draw(replacement, false), "Private Mega finale target unavailable");
    renderer.save_screenshot("championship-finale-mega-1920x1080.png");
    if (mega_models) {
        const auto upgraded = load_championship_finale_resources(store, mega_models);
        check_mega_resources(store, mega_models, resources, upgraded);
        mega_convoy_views(renderer, mega_scene, resources, upgraded, artwork_directory, flags.variants(), actors.frame());
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        synthetic();
        if (argc == 3 || argc == 4) {
            reference(argv[1], argv[2], argc == 4 ? argv[3] : nullptr);
        } else if (argc != 1) {
            throw std::runtime_error("Usage: toca-championship-finale-view-tests [CANONICAL_ROOT ARTWORK_DIRECTORY [MEGA_MODELS_DIRECTORY]]");
        }
        std::cout << "PASS: original finale models, attached top-three teams, shared world renderer and upright credits\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
