#include "assets/store.hpp"
#include "formats/glb.hpp"
#include "image/decode.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

std::string upper(std::string name) {
    for (auto& character : name) {
        if (character >= 'a' && character <= 'z') {
            character = char(character - 'a' + 'A');
        }
    }
    return name;
}

void unchanged(const Model& before, const Model& after) {
    expect(before.nodes.size() == after.nodes.size() && before.meshes.size() == after.meshes.size() && before.textures.size() == after.textures.size(),
        "Mega flags must preserve the complete original model tables");
    for (std::size_t index = 0; index < before.nodes.size(); ++index) {
        const auto& a = before.nodes[index];
        const auto& b = after.nodes[index];
        expect(a.parent == b.parent && a.transform == b.transform && a.id_flags == b.id_flags && a.variants == b.variants,
            "Mega flags must preserve original placements, IDs, hierarchy and all cloth variants");
    }
    for (std::size_t index = 0; index < before.meshes.size(); ++index) {
        const auto& a = before.meshes[index];
        const auto& b = after.meshes[index];
        expect(a.name == b.name && a.faces.size() == b.faces.size(), "Flag artwork must not add, replace or reorder source geometry");
        for (std::size_t face = 0; face < a.faces.size(); ++face) {
            expect(a.faces[face].texture == b.faces[face].texture && a.faces[face].color == b.faces[face].color, "Flag artwork must retain source material slots and colors");
            // GLB packs vertices by first use. Compare the actual ordered
            // triangle corners so harmless table compaction cannot hide a
            // changed position, normal, UV seam or winding.
            for (unsigned corner = 0; corner < 3; ++corner) {
                const auto& original = a.vertices.at(a.faces[face].indices[corner]);
                const auto& generated = b.vertices.at(b.faces[face].indices[corner]);
                for (unsigned axis = 0; axis < 3; ++axis) {
                    expect(std::abs(original.position[axis] - generated.position[axis]) < .0001f && std::abs(original.normal[axis] - generated.normal[axis]) < .0001f,
                        "Flag triangle corners must retain source positions, normals and winding");
                }
                expect(original.u == generated.u && original.v == generated.v, "Flag UVs must remain unchanged");
            }
        }
    }
    for (std::size_t index = 0; index < before.textures.size(); ++index) {
        expect(before.textures[index].name == after.textures[index].name, "Flag materials must keep their original names and slots");
    }
}

SceneResources isolated(const SceneResources& source, const SceneSetup& setup, const PropActor& actor) {
    SceneResources scene{};
    scene.setup = setup;
    scene.flags = source.flags;
    scene.flags.actors = {actor};
    for (std::uint32_t index = 0; index < scene.flags.model.nodes.size(); ++index) {
        if (index != actor.animation_node) {
            scene.flags.model.nodes[index].variants.clear();
        }
    }
    return scene;
}

CameraInputs camera(const Model& model, std::uint32_t node, bool reverse) {
    const auto matrices = model_world_transforms(model, unit);
    const auto& mesh = model.meshes.at(model.nodes.at(node).variants.at(0));
    expect(!mesh.faces.empty(), "Flag camera requires an authored cloth face");
    std::array<std::array<float, 3>, 3> points;
    for (unsigned corner = 0; corner < 3; ++corner) {
        const auto& p = mesh.vertices.at(mesh.faces.front().indices[corner]).position;
        const auto& matrix = matrices.at(node);
        for (unsigned axis = 0; axis < 3; ++axis) {
            points[corner][axis] = p[0] * matrix[axis] + p[1] * matrix[4 + axis] + p[2] * matrix[8 + axis] + matrix[12 + axis];
        }
    }
    std::array<float, 3> center{}, normal{};
    for (unsigned axis = 0; axis < 3; ++axis) {
        // Aim at the actual authored cloth face in its original world placement.
        center[axis] = (points[0][axis] + points[1][axis] + points[2][axis]) / 3;
        const auto j = (axis + 1) % 3, k = (axis + 2) % 3;
        normal[axis] = (points[1][j] - points[0][j]) * (points[2][k] - points[0][k]) - (points[1][k] - points[0][k]) * (points[2][j] - points[0][j]);
    }
    const auto length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    expect(length > 0, "Flag cloth must define a camera plane");
    std::array<SceneCamera, 1> authored{};
    std::array<std::int32_t, 3> target;
    for (unsigned axis = 0; axis < 3; ++axis) {
        target[axis] = std::int32_t(center[axis] * 65536);
        authored[0].transform[12 + axis] = std::int32_t((center[axis] + normal[axis] / length * (reverse ? -4.f : 4.f)) * 65536);
    }
    CameraInputs result{FinishCamera(authored).world_to_view(target), default_projection(4, 640, 480)};
    result.hide_player = true;
    return result;
}

void probe(gpu::Renderer& renderer, const SceneResources& original, const SceneResources& mega, const PropActor& actor, const std::string& country) {
    const auto node = actor.animation_node;
    auto source = isolated(original, mega.setup, actor);
    auto generated = isolated(mega, mega.setup, actor);
    SceneView before(renderer, source), after(renderer, generated);
    SourceRandom random(7);
    FlagAnimation animation(generated.flags, random);
    auto inputs = camera(generated.flags.model, node, false);
    if (toca::test::materialize_world(after.frame(inputs, scene_lighting(mega.setup), {unit}, animation.variants(), {})).indices.empty()) {
        inputs = camera(generated.flags.model, node, true);
    }
    std::array<std::vector<std::uint32_t>, 3> images;
    std::set<std::uint8_t> observed;
    for (unsigned step = 0; step < 3; ++step) {
        const auto phase = animation.variants()[node];
        expect(observed.insert(phase).second, "Four original ticks must advance to a different cloth phase");
        const auto initial_tick = animation.updates();
        const auto draw = [&](SceneView& view, const std::string& label) {
            const auto frame = view.frame(inputs, scene_lighting(mega.setup), {unit}, animation.variants(), {});
            expect(toca::test::materialize_world(frame).indices.size() >= 6 && renderer.draw(frame, false), "Each native animated cloth phase must reach actual GPU pixels");
            renderer.save_screenshot("flags-" + country + "-phase" + std::to_string(phase) + "-" + label + ".png");
            const auto pixels = renderer.readback();
            expect(std::ranges::count_if(pixels,
                       [](auto pixel) {
                           return pixel != gpu::background;
                       })
                       > 100,
                "Flag closeup must contain visible original cloth pixels");
            return pixels;
        };
        const auto source_pixels = draw(before, "original");
        images[phase] = draw(after, "mega");
        expect(source_pixels != images[phase], "Each phase must visibly consume its new artwork through the native scene loader");
        expect(animation.updates() == initial_tick, "Rendering the original and Mega flags must not advance the source animation clock");
        for (unsigned tick = 0; tick < 4; ++tick) {
            animation.update();
        }
    }
    expect(observed.size() == 3 && images[0] != images[1] && images[1] != images[2] && images[0] != images[2],
        "The real flag controller must show three distinct authored phase images");
    std::cout << "PASS " << country << " original hierarchy, source four-tick animation and three visible Mega cloth phases\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 3, "Usage: toca-mega-flags-render-tests CANONICAL_ROOT MEGA_ROOT");
        const AssetStore store(argv[1]);
        const auto original = load_scene_resources(store, resolve_setup({.track_id = 0, .weather_choice = 1, .detail = 3}));
        const auto mega = load_scene_resources(store, resolve_setup({.track_id = 0, .weather_choice = 1, .detail = 4}), argv[2]);
        const auto bundled = load_glb(std::filesystem::path(argv[2]) / "scene/track-0/flags.glb");
        unchanged(original.flags.model, mega.flags.model);
        unchanged(bundled, mega.flags.model);
        // Export adds FREE11 for every nonempty family. Check the three
        // exceptional source layouts without loading or rendering full tracks.
        for (const auto& [track, prefix] : std::array<std::pair<unsigned, const char*>, 3>{{{1, "silv"}, {3, "bran"}, {9, "land"}}}) {
            const auto archive = store.open_archive(std::string("data/") + prefix + "free.bfp");
            const auto flags = decode_props(archive.member(11).bytes(), PropKind::flags);
            const auto path = std::filesystem::path(argv[2]) / "scene" / ("track-" + std::to_string(track)) / "flags.glb";
            if (flags.model.meshes.empty()) {
                expect(!std::filesystem::exists(path), "An empty source flag family must not acquire generated actors");
            } else {
                unchanged(flags.model, load_glb(path));
            }
        }
        expect(original.flags.actors.size() == mega.flags.actors.size(), "Mega flags must retain the original controller registrations");
        for (std::size_t index = 0; index < original.flags.actors.size(); ++index) {
            expect(original.flags.actors[index].root_node == mega.flags.actors[index].root_node
                       && original.flags.actors[index].animation_node == mega.flags.actors[index].animation_node,
                "Mega flags must retain source actor roots and animation targets");
        }
        std::set<std::string> expected{"LIFLAG1", "LIFLAG2", "LIFLAG3", "LIFLAGB1", "LIFLAGB2", "LIFLAGB3", "LIFLAGA1", "LIFLAGA2", "LIFLAGA3"};
        for (std::size_t index = 0; index < mega.flags.model.textures.size(); ++index) {
            const auto& texture = mega.flags.model.textures[index];
            if (!expected.erase(upper(texture.name))) {
                continue;
            }
            expect(texture.encoding == ImageEncoding::webp && texture.bmp != original.flags.model.textures[index].bmp,
                "All nine flag images must load their replacement WebP artwork in their original slots");
            const auto image = toca::decode_model_texture(texture);
            expect(image && image->w > 32 && image->w <= 512 && image->w == image->h, "All nine flag phases must decode square authored artwork within the Mega limit");
        }
        expect(expected.empty(), "Every original French, Portuguese and UK cloth phase must be present");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated animated Mega flags check");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA animated Mega flags", 640, 480, 0), SDL_DestroyWindow);
        expect(bool(window), "Create private flag render window");
        gpu::Renderer renderer(window.get());
        for (const std::string country : {"FRA", "POR", "UK"}) {
            const auto actor = std::ranges::find_if(mega.flags.actors, [&](const auto& value) {
                if (value.animation_node == UINT32_MAX) {
                    return false;
                }
                const auto& node = mega.flags.model.nodes.at(value.animation_node);
                return node.variants.size() == 3 && upper(mega.flags.model.meshes.at(node.variants.front()).name).starts_with(country + "_");
            });
            expect(actor != mega.flags.actors.end(), "Each source country requires its real three-phase flag actor");
            probe(renderer, original, mega, *actor, country);
        }
        std::cout << "PASS: native Mega flag GLB, nine original material slots and all three animated phases per country\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
