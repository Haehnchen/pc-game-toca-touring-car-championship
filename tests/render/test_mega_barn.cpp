#include "assets/store.hpp"
#include "formats/glb.hpp"
#include "image/decode.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::array<std::string_view, 4> materials{"DA50BAR1", "DA50BAR3", "DA50BAR4", "DA50BAR2"};

struct BarnCase {
    std::uint32_t track;
    std::size_t mesh;
    std::array<std::size_t, 2> nodes;
};

constexpr std::array cases{BarnCase{0, 13, {39, 41}}, BarnCase{5, 12, {41, 43}}};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::array<float, 6> bounds(const ModelMesh& mesh) {
    std::array<float, 6> result{32768, 32768, 32768, -32768, -32768, -32768};
    for (const auto& vertex : mesh.vertices) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result[axis] = std::min(result[axis], vertex.position[axis]);
            result[axis + 3] = std::max(result[axis + 3], vertex.position[axis]);
        }
    }
    return result;
}

std::vector<std::size_t> chunks(const SceneResources& scene, const BarnCase& pilot) {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < scene.objects_baked.chunks.size(); ++index) {
        const auto& chunk = scene.objects_baked.chunks[index];
        if (chunk.mesh.name == "DA50BARN_1") {
            expect(chunk.variant_index == 0 && std::ranges::find(pilot.nodes, chunk.node_index) != pilot.nodes.end(),
                "Barn must retain its two original baked node/variant registrations");
            result.push_back(index);
        }
    }
    expect(result.size() == pilot.nodes.size(), "Both original barn placements must reach the baked scene");
    return result;
}

using Triangle = std::array<float, 20>;

std::vector<Triangle> triangles(const Model& model, std::size_t mesh_index) {
    std::vector<Triangle> result;
    const auto& mesh = model.meshes.at(mesh_index);
    for (const auto& face : mesh.faces) {
        Triangle triangle{};
        std::size_t cursor = 0;
        for (const auto index : face.indices) {
            const auto& vertex = mesh.vertices.at(index);
            for (const auto coordinate : vertex.position) {
                triangle[cursor++] = coordinate;
            }
            triangle[cursor++] = vertex.u;
            triangle[cursor++] = vertex.v;
        }
        const auto name = model.textures.at(std::size_t(face.texture)).name;
        const auto material = std::ranges::find(materials, name);
        expect(material != materials.end(), "Barn faces must use only its four source-specific materials");
        triangle[cursor++] = float(material - materials.begin());
        for (const auto color : face.color) {
            triangle[cursor++] = float(color);
        }
        result.push_back(triangle);
    }
    std::ranges::sort(result);
    return result;
}

void source_report(const SceneResources& original, const BarnCase& pilot) {
    const auto& mesh = original.objects.meshes.at(pilot.mesh);
    expect(mesh.name == "DA50BARN_1" && mesh.faces.size() == 14, "Barn fixture must select the original fourteen-triangle building");
    const auto box = bounds(mesh);
    std::ofstream report("barn-track" + std::to_string(pilot.track) + "-source.csv");
    report << "face,material,corner,x,y,z,u,v\n";
    for (std::size_t index = 0; index < mesh.faces.size(); ++index) {
        const auto& face = mesh.faces[index];
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& vertex = mesh.vertices.at(face.indices[corner]);
            report << index << ',' << original.objects.textures.at(std::size_t(face.texture)).name << ',' << corner;
            for (const auto coordinate : vertex.position) {
                report << ',' << coordinate;
            }
            report << ',' << vertex.u << ',' << vertex.v << '\n';
        }
    }
    std::cout << "SOURCE barn track" << pilot.track << " mesh=" << pilot.mesh << " nodes=" << pilot.nodes[0] << ',' << pilot.nodes[1] << " bounds=";
    for (const auto coordinate : box) {
        std::cout << coordinate << ',';
    }
    std::cout << " (-Z up; +Y doorway; -Y rear; +/-X long walls)\n";
    for (const auto name : materials) {
        const auto texture = std::ranges::find(original.objects.textures, name, &ModelTexture::name);
        expect(texture != original.objects.textures.end(), "Barn source must contain all four original materials");
        const auto image = toca::decode_model_texture(*texture);
        expect(bool(image), "Barn source artwork must decode");
        gpu::check(SDL_SavePNG(image.get(), ("barn-track" + std::to_string(pilot.track) + "-source-" + std::string(name) + ".png").c_str()), "Save original barn material");
    }
}

void verify(const SceneResources& original, const SceneResources& mega, const BarnCase& pilot, const std::filesystem::path& mega_root) {
    const auto& before = original.objects;
    const auto& after = mega.objects;
    expect(before.nodes.size() == after.nodes.size() && before.meshes.size() == after.meshes.size(), "Barn presentation must preserve source mesh/node slots");
    for (std::size_t index = 0; index < before.nodes.size(); ++index) {
        const auto& a = before.nodes[index];
        const auto& b = after.nodes[index];
        expect(a.parent == b.parent && a.transform == b.transform && a.id_flags == b.id_flags && a.variants == b.variants,
            "Barn presentation must retain exact hierarchy, placements, sector flags and variants");
    }
    for (const auto node : pilot.nodes) {
        expect(before.nodes.at(node).variants == std::vector<std::uint16_t>{std::uint16_t(pilot.mesh)} && before.nodes.at(node).parent == std::int16_t(node - 1),
            "Barn fixture requires its original root/mesh node pairs");
    }
    const auto& source = before.meshes.at(pilot.mesh);
    const auto& detailed = after.meshes.at(pilot.mesh);
    const auto bundled = load_glb(mega_root / "scene" / ("track-" + std::to_string(pilot.track)) / "objects.glb");
    const auto& packed = bundled.meshes.at(pilot.mesh);
    expect(detailed.name == source.name && detailed.vertices.size() == packed.vertices.size() && detailed.faces.size() == packed.faces.size()
               && std::memcmp(detailed.vertices.data(), packed.vertices.data(), detailed.vertices.size() * sizeof(ModelVertex)) == 0
               && triangles(after, pilot.mesh) == triangles(bundled, pilot.mesh),
        "Barn must use the actual bundled generated GLB through the normal detail4 resource loader");
    expect(detailed.faces.size() == 382 && detailed.vertices.size() < 65536, "Barn must add genuine bounded roof, window and trim geometry");
    const auto source_triangles = triangles(before, pilot.mesh), detailed_triangles = triangles(after, pilot.mesh);
    expect(std::ranges::includes(detailed_triangles, source_triangles), "Barn must preserve the four source facade/roof mappings, their physical surfaces and winding");
    for (const auto name : materials) {
        const auto source_texture = std::ranges::find(before.textures, name, &ModelTexture::name);
        const auto replacement = std::ranges::find(after.textures, name, &ModelTexture::name);
        expect(source_texture != before.textures.end() && replacement != after.textures.end() && source_texture - before.textures.begin() == replacement - after.textures.begin()
                   && replacement->encoding == ImageEncoding::webp,
            "Barn must retain each original named material slot with accepted WebP artwork");
        const auto image = toca::decode_model_texture(*replacement);
        expect(
            image && image->w >= 128 && image->h >= 64 && image->w <= 512 && image->h <= 512, "Barn artwork must use a useful authored resolution within the 512-pixel Mega limit");
    }
    const auto a = bounds(source), b = bounds(detailed);
    for (std::size_t index = 0; index < a.size(); ++index) {
        expect(std::abs(a[index] - b[index]) < .2f, "Barn details must stay within twenty centimeters of the original building envelope");
    }
    // Local -Z is up. The wall feet must remain at the source ground plane;
    // roof/trim detail may grow upward without lowering the whole building.
    expect(std::abs(a[5] - b[5]) < .02f, "Barn must remain attached at its original wall-foot height");
    std::size_t depth_vertices = 0;
    for (const auto& vertex : detailed.vertices) {
        float nearest = std::numeric_limits<float>::max();
        for (const auto& face : source.faces) {
            const auto& origin = source.vertices.at(face.indices[0]);
            float distance = 0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                distance += (vertex.position[axis] - origin.position[axis]) * origin.normal[axis];
            }
            nearest = std::min(nearest, std::abs(distance));
        }
        depth_vertices += nearest > .01f;
    }
    expect(depth_vertices >= 24, "Barn additions must add physical depth rather than only subdividing the original planes");
    expect(original.objects_baked.chunks.size() == mega.objects_baked.chunks.size(), "Barn must preserve original baked chunk count");
    const auto original_chunks = chunks(original, pilot), mega_chunks = chunks(mega, pilot);
    expect(original_chunks == mega_chunks, "Both barn placements must retain their original baked chunk slots");
    for (const auto index : original_chunks) {
        const auto& old_chunk = original.objects_baked.chunks[index];
        const auto& new_chunk = mega.objects_baked.chunks[index];
        expect(old_chunk.node_index == new_chunk.node_index && old_chunk.variant_index == new_chunk.variant_index && old_chunk.origin_xz == new_chunk.origin_xz
                   && new_chunk.mesh.faces.size() == detailed.faces.size(),
            "Each barn placement must consume the detailed geometry at its original sector origin");
        const auto old_box = bounds(old_chunk.mesh), new_box = bounds(new_chunk.mesh);
        expect(std::abs(old_box[1] - new_box[1]) < .02f, "Every world-space barn placement must retain its source ground attachment");
    }
    expect(original.collision_hpc == mega.collision_hpc && original.route_points == mega.route_points && original.ground.sector_centers == mega.ground.sector_centers,
        "Barn presentation must preserve original terrain contacts, routes and sector data");
    std::cout << "PASS barn track" << pilot.track << " two original placements, " << source.faces.size() << " original / " << detailed.faces.size() << " Mega triangles, "
              << depth_vertices << " vertices away from source planes\n";
}

SceneResources comparison(const SceneResources& original, const SceneResources& mega, const BarnCase& pilot, unsigned phase, bool context, std::size_t selected) {
    auto scene = context ? mega : SceneResources{};
    scene.setup = mega.setup;
    scene.ground.sector_centers = original.ground.sector_centers;
    auto& target = scene.objects_baked;
    if (context) {
        const auto affected = chunks(original, pilot);
        if (phase < 2) {
            for (const auto index : affected) {
                target.chunks.at(index) = original.objects_baked.chunks.at(index);
            }
        }
        if (phase == 0) {
            // Append source materials and rebind only these two chunks, keeping
            // any neighboring material consumers and all Mega surroundings fixed.
            std::vector<std::int16_t> remap(original.objects_baked.textures.size(), -1);
            for (const auto index : affected) {
                for (auto& face : target.chunks.at(index).mesh.faces) {
                    auto& slot = remap.at(std::size_t(face.texture));
                    if (slot < 0) {
                        slot = std::int16_t(target.textures.size());
                        target.textures.push_back(original.objects_baked.textures.at(std::size_t(face.texture)));
                    }
                    face.texture = slot;
                }
            }
        }
    } else {
        target.chunks.push_back((phase == 2 ? mega : original).objects_baked.chunks.at(selected));
        const auto& textures = (phase == 0 ? original : mega).objects_baked.textures;
        std::vector<std::int16_t> remap(textures.size(), -1);
        for (auto& face : target.chunks.front().mesh.faces) {
            auto& slot = remap.at(std::size_t(face.texture));
            if (slot < 0) {
                slot = std::int16_t(target.textures.size());
                target.textures.push_back(textures.at(std::size_t(face.texture)));
            }
            face.texture = slot;
        }
    }
    return scene;
}

struct View {
    std::string_view name;
    std::array<float, 3> origin, target;
    bool context = false;
    int width = 1280, height = 720;
};

constexpr std::array views{View{"front", {0, 30, -4}, {0, 0, -3}}, View{"back", {0, -30, -4}, {0, 0, -3}}, View{"left-side", {-29, 1, -4}, {0, 0, -3}},
    View{"right-side", {29, 1, -4}, {0, 0, -3}}, View{"front-oblique", {23, 27, -11}, {0, 0, -3}}, View{"back-oblique", {-23, -27, -11}, {0, 0, -3}},
    View{"roof", {12, 6, -27}, {0, 0, -4}}, View{"driver", {0, 65, -1.5f}, {0, 0, -3}}, View{"context", {15, 35, -3}, {0, 0, -3}, true},
    View{"front-1080p", {0, 30, -4}, {0, 0, -3}, false, 1920, 1080}, View{"context-1080p", {15, 35, -3}, {0, 0, -3}, true, 1920, 1080},
    View{"front-1440p", {0, 30, -4}, {0, 0, -3}, false, 2560, 1440}, View{"context-1440p", {15, 35, -3}, {0, 0, -3}, true, 2560, 1440}};

void pixels(SDL_Window* window, gpu::Renderer& renderer, const SceneResources& original, const SceneResources& mega, const BarnCase& pilot, bool original_only, bool cleanup) {
    const auto selected = chunks(original, pilot).front();
    const auto node = original.objects_baked.chunks[selected].node_index;
    const auto& placement = original.objects.nodes.at(std::size_t(original.objects.nodes.at(node).parent)).transform;
    const auto world = [&](const std::array<float, 3>& point) {
        std::array<std::int32_t, 3> result{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result[axis] = std::int32_t((point[0] * placement[axis] + point[1] * placement[4 + axis] + point[2] * placement[8 + axis] + placement[12 + axis]) * 65536);
        }
        return result;
    };
    const auto prefix = "barn-track" + std::to_string(pilot.track);
    std::ofstream report(prefix + "-gpu.csv");
    report << "view,width,height,original_triangles,ai_triangles,mega_triangles,material_pixels,geometry_pixels,source_pixels\n";
    std::size_t total_geometry = 0, total_extra_triangles = 0;
    for (const auto& probe : views) {
        if (cleanup && probe.name != "front-oblique" && probe.name != "context") {
            continue;
        }
        // GP receives the complete closeup/size coverage. Short independently
        // proves the shared object in its own actual surroundings and front view.
        if (!cleanup && pilot.track == 5 && probe.name != "front" && probe.name != "context") {
            continue;
        }
        gpu::check(SDL_SetWindowSize(window, probe.width, probe.height), "Resize barn render target");
        gpu::check(SDL_SyncWindow(window), "Synchronize barn render target");
        SDL_PumpEvents();
        std::array<SceneCamera, 1> cameras{};
        const auto origin = world(probe.origin), target = world(probe.target);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            cameras[0].transform[12 + axis] = origin[axis];
        }
        const FinishCamera look(cameras);
        CameraInputs camera{look.world_to_view(target), default_projection(4, probe.width, probe.height)};
        camera.registered_camera_position = target;
        camera.hide_player = true;
        constexpr std::array labels{"original", "ai-original-geometry", "mega"};

        struct Result {
            std::vector<std::uint32_t> pixels;
            std::size_t triangles;
        };

        std::array<Result, 3> result;
        for (unsigned phase = 0; phase < (original_only ? 1u : 3u); ++phase) {
            auto scene = comparison(original, mega, pilot, phase, probe.context, selected);
            SceneView rendered(renderer, scene);
            SourceRandom random(7);
            FlagAnimation flags(scene.flags, random);
            HorizonActors actors(scene.moving_horizon);
            const auto frame = rendered.frame(camera, scene_lighting(mega.setup), {unit}, flags.variants(), actors.frame());
            const auto projected = toca::test::materialize_world(frame);
            expect(!projected.indices.empty(), "Barn must reach actual GPU geometry in each camera");
            gpu::check(renderer.draw(frame, false), "Draw barn comparison");
            renderer.save_screenshot(prefix + "-" + std::string(probe.name) + "-" + labels[phase] + ".png");
            result[phase] = {renderer.readback(), projected.indices.size() / 3};
            expect(result[phase].pixels.size() == std::size_t(probe.width * probe.height), "Barn comparison must use identical full-size render targets");
            std::cout << "BUDGET barn track" << pilot.track << ' ' << probe.name << ' ' << labels[phase] << ' ' << probe.width << 'x' << probe.height << ": "
                      << projected.indices.size() / 3 << " triangles, " << projected.vertices.size() << " vertices, " << frame.draws.size() << " draws\n";
        }
        if (original_only) {
            continue;
        }
        std::size_t material = 0, geometry = 0, covered = 0;
        for (std::size_t pixel = 0; pixel < result[2].pixels.size(); ++pixel) {
            material += result[0].pixels[pixel] != result[1].pixels[pixel];
            geometry += result[1].pixels[pixel] != result[2].pixels[pixel];
            covered += result[0].pixels[pixel] != gpu::background;
        }
        expect(covered > 500 && material > 50, "Each barn camera must expose its accepted artwork on the actual building");
        if (!probe.context) {
            total_geometry += geometry;
            total_extra_triangles += result[2].triangles > result[1].triangles ? result[2].triangles - result[1].triangles : 0;
        }
        if (probe.name == "roof" || probe.name == "left-side" || probe.name == "right-side" || probe.name == "front-oblique" || probe.name == "back-oblique") {
            expect(geometry > 20 && result[2].triangles > result[1].triangles + 8, "Barn side/roof views must show real additional geometry independently of its artwork");
        }
        report << probe.name << ',' << probe.width << ',' << probe.height << ',' << result[0].triangles << ',' << result[1].triangles << ',' << result[2].triangles << ','
               << material << ',' << geometry << ',' << covered << '\n';
        std::cout << "PASS barn track" << pilot.track << ' ' << probe.name << " material_pixels=" << material << " geometry_pixels=" << geometry << '\n';
    }
    if (!original_only && pilot.track == 0) {
        expect(total_geometry > 300 && total_extra_triangles > 32, "Barn closeups must show actual added depth with the same accepted artwork");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool original_only = argc == 3 && std::string_view(argv[1]) == "--original-only";
        const bool cleanup = argc == 4 && std::string_view(argv[3]) == "--cleanup";
        expect(original_only || argc == 3 || cleanup, "Usage: toca-mega-barn-render-tests CANONICAL_ROOT MEGA_ROOT [--cleanup] | --original-only CANONICAL_ROOT");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated barn check");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Donington barn comparison", 1280, 720, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create isolated barn window");
        gpu::Renderer renderer(window.get());
        const AssetStore store(argv[original_only ? 2 : 1]);
        std::vector<Triangle> shared;
        for (const auto& pilot : cases) {
            const auto original = load_scene_resources(store, resolve_setup({.track_id = pilot.track, .weather_choice = 1, .detail = 3}));
            source_report(original, pilot);
            const auto mega = original_only ? original : load_scene_resources(store, resolve_setup({.track_id = pilot.track, .weather_choice = 1, .detail = 4}), argv[2]);
            if (!original_only) {
                verify(original, mega, pilot, argv[2]);
                const auto geometry = triangles(mega.objects, pilot.mesh);
                if (shared.empty()) {
                    shared = geometry;
                } else {
                    expect(shared == geometry, "GP and Short must share detailed barn surfaces/UVs using their own named material slots");
                }
            }
            pixels(window.get(), renderer, original, mega, pilot, original_only, cleanup);
        }
        std::cout << "PASS: Donington GP/Short barn "
                  << (original_only ? "original architecture/material baseline" : "bundled geometry, four artworks and three-phase GPU acceptance") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
