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

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::array<std::string_view, 4> materials{"DA03STL2", "DA03STL3", "DA03STL4", "DA03STL5"};
constexpr std::array<std::size_t, 4> source_faces{8, 4, 16, 2};

struct StlCase {
    std::uint32_t track;
    std::size_t mesh;
    std::array<std::size_t, 1> nodes;
};

constexpr std::array cases{StlCase{0, 0, {1}}, StlCase{5, 0, {1}}};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string material_name(std::string name) {
    // Material identity also works on GP and Short's different texture slots.
    for (auto& character : name) {
        if (character >= 'a' && character <= 'z') {
            character = char(character - 'a' + 'A');
        }
    }
    return name;
}

auto texture(const Model& model, std::string_view name) {
    return std::ranges::find(model.textures, name, [](const auto& candidate) {
        return material_name(candidate.name);
    });
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

std::vector<std::size_t> chunks(const SceneResources& scene, const StlCase& pilot) {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < scene.objects_baked.chunks.size(); ++index) {
        const auto& chunk = scene.objects_baked.chunks[index];
        if (chunk.mesh.name == "DA03STL_1") {
            expect(
                chunk.variant_index == 0 && std::ranges::find(pilot.nodes, chunk.node_index) != pilot.nodes.end(), "STL must retain its original baked node/variant registrations");
            result.push_back(index);
        }
    }
    expect(result.size() == pilot.nodes.size(), "The original stl placements must reach the baked scene");
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
        const auto name = material_name(model.textures.at(std::size_t(face.texture)).name);
        const auto material = std::ranges::find(materials, name);
        expect(material != materials.end(), "STL faces must use only its four source-specific materials");
        triangle[cursor++] = float(material - materials.begin());
        for (const auto color : face.color) {
            triangle[cursor++] = float(color);
        }
        result.push_back(triangle);
    }
    std::ranges::sort(result);
    return result;
}

void source_report(const SceneResources& original, const StlCase& pilot) {
    const auto& mesh = original.objects.meshes.at(pilot.mesh);
    expect(mesh.name == "DA03STL_1" && mesh.vertices.size() == 52 && mesh.faces.size() == 30, "STL fixture must select the original thirty-triangle startlight assembly");
    const auto box = bounds(mesh);
    std::cout << "SOURCE stl track" << pilot.track << " mesh=" << pilot.mesh << " nodes=" << pilot.nodes[0] << " bounds=";
    for (const auto coordinate : box) {
        std::cout << coordinate << ',';
    }
    std::cout << " (+Z up; +Y sponsor/light face; -Y rear; +/-X ends)\n";
    for (std::size_t index = 0; index < materials.size(); ++index) {
        const auto name = materials[index];
        const auto source_texture = texture(original.objects, name);
        expect(source_texture != original.objects.textures.end(), "STL source must contain all four original materials");
        const auto slot = source_texture - original.objects.textures.begin();
        expect(std::ranges::count(mesh.faces, slot, &ModelFace::texture) == std::ptrdiff_t(source_faces[index]),
            "STL cameras/material roles must use the canonical support, perimeter and sponsor/light-face bindings");
        const auto image = toca::decode_model_texture(*source_texture);
        expect(bool(image), "STL source artwork must decode");
        gpu::check(SDL_SavePNG(image.get(), ("stl-track" + std::to_string(pilot.track) + "-source-" + std::string(name) + ".png").c_str()), "Save original stl material");
    }
}

void verify_key_mask(SDL_Surface* source, SDL_Surface* replacement) {
    toca::Surface before(SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    toca::Surface after(SDL_ConvertSurface(replacement, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    expect(before && after, "STL source/replacement opacity must decode to comparable RGBA pixels");
    // Check uniform mask interiors at source texel centers. Boundary texels
    // may use half-source-pixel antialiasing instead of recreating pixel steps.
    // Opaque dark paint and lamp ink must remain solid.
    for (int y = 0; y < before->h; ++y) {
        for (int x = 0; x < before->w; ++x) {
            const auto* a = static_cast<const std::uint8_t*>(before->pixels) + y * before->pitch + x * 4;
            const bool key = a[0] == 0 && a[1] == 0 && a[2] == 0;
            bool boundary = false;
            constexpr std::array<std::array<int, 2>, 4> neighbors{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
            for (const auto offset : neighbors) {
                const int nx = x + offset[0], ny = y + offset[1];
                if (nx >= 0 && nx < before->w && ny >= 0 && ny < before->h) {
                    const auto* neighbor = static_cast<const std::uint8_t*>(before->pixels) + ny * before->pitch + nx * 4;
                    boundary |= (neighbor[0] == 0 && neighbor[1] == 0 && neighbor[2] == 0) != key;
                }
            }
            if (boundary) {
                continue;
            }
            const auto dx = std::min(after->w - 1, (2 * x + 1) * after->w / (2 * before->w));
            const auto dy = std::min(after->h - 1, (2 * y + 1) * after->h / (2 * before->h));
            const auto* b = static_cast<const std::uint8_t*>(after->pixels) + dy * after->pitch + dx * 4;
            expect((b[3] < 128) == key, "STL artwork must preserve original keyed openings and opaque paint at source mask-interior texel centers");
        }
    }
}

void verify(const SceneResources& original, const SceneResources& mega, const StlCase& pilot, const std::filesystem::path& mega_root) {
    const auto& before = original.objects;
    const auto& after = mega.objects;
    expect(before.nodes.size() == after.nodes.size() && before.meshes.size() == after.meshes.size(), "STL presentation must preserve source mesh/node slots");
    for (std::size_t index = 0; index < before.nodes.size(); ++index) {
        const auto& a = before.nodes[index];
        const auto& b = after.nodes[index];
        expect(a.parent == b.parent && a.transform == b.transform && a.id_flags == b.id_flags && a.variants == b.variants,
            "STL presentation must retain exact hierarchy, placements, sector flags and variants");
    }
    for (const auto node : pilot.nodes) {
        expect(before.nodes.at(node).variants == std::vector<std::uint16_t>{std::uint16_t(pilot.mesh)} && before.nodes.at(node).parent == std::int16_t(node - 1),
            "STL fixture requires its original root/mesh node pairs");
    }
    const auto& source = before.meshes.at(pilot.mesh);
    const auto& detailed = after.meshes.at(pilot.mesh);
    const auto bundled = load_glb(mega_root / "scene" / ("track-" + std::to_string(pilot.track)) / "objects.glb");
    const auto& packed = bundled.meshes.at(pilot.mesh);
    expect(detailed.name == source.name && detailed.vertices.size() == packed.vertices.size() && detailed.faces.size() == packed.faces.size()
               && std::memcmp(detailed.vertices.data(), packed.vertices.data(), detailed.vertices.size() * sizeof(ModelVertex)) == 0
               && triangles(after, pilot.mesh) == triangles(bundled, pilot.mesh),
        "STL must use the actual bundled generated GLB through the normal detail4 resource loader");
    expect(detailed.vertices.size() < 65536, "STL must remain within the CM3D vertex-index limit");
    const auto source_triangles = triangles(before, pilot.mesh), detailed_triangles = triangles(after, pilot.mesh);
    expect(detailed_triangles == source_triangles, "STL artwork replacement must preserve every source position, UV, material, color and winding");
    for (const auto name : materials) {
        const auto source_texture = texture(before, name);
        const auto replacement = texture(after, name);
        expect(source_texture != before.textures.end() && replacement != after.textures.end() && source_texture - before.textures.begin() == replacement - after.textures.begin()
                   && source_texture->name == replacement->name && replacement->encoding == ImageEncoding::webp,
            "STL must retain each original named material slot with accepted WebP artwork");
        const auto image = toca::decode_model_texture(*replacement);
        const auto original_image = toca::decode_model_texture(*source_texture);
        expect(image && image->w > 0 && image->h > 0 && image->w <= 512 && image->h <= 512, "STL artwork must decode within the 512-pixel Mega limit");
        expect(original_image && image->w * original_image->h == image->h * original_image->w, "STL artwork must preserve each source atlas aspect ratio");
        verify_key_mask(original_image.get(), image.get());
        std::cout << "MATERIAL stl track" << pilot.track << ' ' << name << " slot=" << replacement - after.textures.begin() << ' ' << original_image->w << 'x' << original_image->h
                  << " -> " << image->w << 'x' << image->h << '\n';
    }
    const auto a = bounds(source), b = bounds(detailed);
    for (std::size_t index = 0; index < a.size(); ++index) {
        expect(std::abs(a[index] - b[index]) < .0001f, "STL presentation must preserve the source startlight envelope");
    }
    expect(original.objects_baked.chunks.size() == mega.objects_baked.chunks.size(), "STL must preserve original baked chunk count");
    const auto original_chunks = chunks(original, pilot), mega_chunks = chunks(mega, pilot);
    expect(original_chunks == mega_chunks, "The STL placements must retain their original baked chunk slots");
    for (const auto index : original_chunks) {
        const auto& old_chunk = original.objects_baked.chunks[index];
        const auto& new_chunk = mega.objects_baked.chunks[index];
        expect(old_chunk.node_index == new_chunk.node_index && old_chunk.variant_index == new_chunk.variant_index && old_chunk.origin_xz == new_chunk.origin_xz
                   && new_chunk.mesh.faces.size() == detailed.faces.size(),
            "Each stl placement must consume the selected presentation at its original sector origin");
        const auto old_box = bounds(old_chunk.mesh), new_box = bounds(new_chunk.mesh);
        expect(std::abs(old_box[1] - new_box[1]) < .02f, "Every world-space STL placement must retain its source mounting height");
    }
    expect(original.collision_hpc == mega.collision_hpc && original.route_points == mega.route_points && original.ground.sector_centers == mega.ground.sector_centers,
        "STL presentation must preserve original terrain contacts, routes and sector data");
    std::cout << "PASS stl track" << pilot.track << " original placements, " << source.faces.size() << " original / " << detailed.faces.size() << " Mega triangles, "
              << "unchanged source envelope and material mappings\n";
}

SceneResources comparison(const SceneResources& original, const SceneResources& mega, const StlCase& pilot, unsigned phase, bool context, std::size_t selected) {
    auto scene = context ? mega : SceneResources{};
    scene.setup = mega.setup;
    scene.ground.sector_centers = original.ground.sector_centers;
    auto& target = scene.objects_baked;
    if (context) {
        const auto affected = chunks(original, pilot);
        if (phase == 0) {
            for (const auto index : affected) {
                target.chunks.at(index) = original.objects_baked.chunks.at(index);
            }
        }
        if (phase == 0) {
            // Append source materials and rebind only this pilot chunk, keeping
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
        target.chunks.push_back((phase == 0 ? original : mega).objects_baked.chunks.at(selected));
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

// The hanging assembly root is at world Y5.5; nearby road is around Y4.4.
// Local +Z points up, so driver/context offsets keep the eye above the road.
constexpr std::array views{View{"front", {.8f, 11, 1.2f}, {.8f, 0, 1.2f}}, View{"back", {.8f, -11, 1.2f}, {.8f, 0, 1.2f}}, View{"left-side", {-7, 2, 2}, {.8f, 0, 1.2f}},
    View{"right-side", {9, 2, 2}, {.8f, 0, 1.2f}}, View{"top", {.8f, 1, 9}, {.8f, 0, 1.2f}}, View{"front-close", {.8f, 5.8f, 1.2f}, {.8f, 0, 1.2f}, false, 1920, 1080},
    View{"front-oblique-close", {5, 5.8f, 3.4f}, {.8f, 0, 1.2f}, false, 1920, 1080}, View{"back-close", {.8f, -5.8f, 1.2f}, {.8f, 0, 1.2f}, false, 1920, 1080},
    View{"back-oblique-close", {-4, -5, 3.2f}, {.8f, 0, 1.2f}, false, 1920, 1080}, View{"driver", {.8f, 25, .1f}, {.8f, 0, 1.2f}},
    View{"context", {6, 13, .6f}, {.8f, 0, 1.2f}, true, 1920, 1080}};

void pixels(SDL_Window* window, gpu::Renderer& renderer, const SceneResources& original, const SceneResources& mega, const StlCase& pilot, bool original_only) {
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
    const auto prefix = "stl-track" + std::to_string(pilot.track);
    std::ofstream report(prefix + "-gpu.csv");
    report << "view,width,height,original_triangles,mega_triangles,material_pixels,source_pixels\n";
    for (const auto& probe : views) {
        // GP receives the complete closeup/size coverage. Short independently
        // proves the shared object in its own actual surroundings and front view.
        if (pilot.track == 5 && probe.name != "front" && probe.name != "context") {
            continue;
        }
        gpu::check(SDL_SetWindowSize(window, probe.width, probe.height), "Resize stl render target");
        gpu::check(SDL_SyncWindow(window), "Synchronize stl render target");
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
        constexpr std::array labels{"original", "mega"};

        struct Result {
            std::vector<std::uint32_t> pixels;
            std::size_t triangles;
        };

        std::array<Result, 2> result;
        for (unsigned phase = 0; phase < (original_only ? 1u : 2u); ++phase) {
            auto scene = comparison(original, mega, pilot, phase, probe.context, selected);
            SceneView rendered(renderer, scene);
            SourceRandom random(7);
            FlagAnimation flags(scene.flags, random);
            HorizonActors actors(scene.moving_horizon);
            auto frame = rendered.frame(camera, scene_lighting(mega.setup), {unit}, flags.variants(), actors.frame());
            const auto projected = toca::test::materialize_world(frame);
            const auto triangles = projected.indices.size() / 3;
            expect(!projected.indices.empty(), "Each STL camera must expose the actual startlight geometry");
            gpu::check(renderer.draw(frame, false), "Draw stl comparison");
            renderer.save_screenshot(prefix + "-" + std::string(probe.name) + "-" + labels[phase] + ".png");
            result[phase] = {renderer.readback(), triangles};
            expect(result[phase].pixels.size() == std::size_t(probe.width * probe.height), "STL comparison must use identical full-size render targets");
            std::cout << "BUDGET stl track" << pilot.track << ' ' << probe.name << ' ' << labels[phase] << ' ' << probe.width << 'x' << probe.height << ": " << triangles
                      << " triangles, " << projected.vertices.size() << " vertices, " << frame.draws.size() << " draws\n";
        }
        if (original_only) {
            continue;
        }
        std::size_t material = 0, covered = 0;
        for (std::size_t pixel = 0; pixel < result[1].pixels.size(); ++pixel) {
            material += result[0].pixels[pixel] != result[1].pixels[pixel];
            covered += result[0].pixels[pixel] != gpu::background;
        }
        expect(covered > 0 && material > 0, "Each STL camera must expose its replacement artwork on the original startlight assembly");
        report << probe.name << ',' << probe.width << ',' << probe.height << ',' << result[0].triangles << ',' << result[1].triangles << ',' << material << ',' << covered << '\n';
        std::cout << "PASS stl track" << pilot.track << ' ' << probe.name << " material_pixels=" << material << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool original_only = argc == 3 && std::string_view(argv[1]) == "--original-only";
        expect(original_only || argc == 3, "Usage: toca-mega-stl-render-tests CANONICAL_ROOT MEGA_ROOT | --original-only CANONICAL_ROOT");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated stl check");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Donington startlight comparison", 1280, 720, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create isolated stl window");
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
                    expect(shared == geometry, "GP and Short must share original stl surfaces/UVs using their own named material slots");
                }
            }
            pixels(window.get(), renderer, original, mega, pilot, original_only);
        }
        std::cout << "PASS: Donington GP/Short startlight "
                  << (original_only ? "original startlight/material baseline" : "source-preserving presentation, four artworks and two-phase GPU acceptance") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
