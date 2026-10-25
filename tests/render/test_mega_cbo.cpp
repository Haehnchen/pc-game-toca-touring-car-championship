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
#include <numeric>
#include <span>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

struct View {
    std::string_view name;
    std::array<float, 3> origin, target;
    bool context = false;
    int width = 1280, height = 720;
    bool sign_only = false;
};

struct CboProfile {
    unsigned number;
    std::string_view mesh_name;
    std::span<const std::string_view> materials;
    std::span<const std::size_t> source_faces;
    std::array<std::size_t, 2> meshes, nodes;
    std::span<const View> views;
    std::size_t source_vertices;
    bool thin_shell = false;
    std::string_view sign_material = "DA28PIT3";

    std::string label() const {
        return "cbo" + std::to_string(number);
    }
};

constexpr std::array<std::string_view, 9> materials1{"DA28COM", "DA28COM6", "DA28COM7", "DA28COMA", "DA28PIT2", "DA28COM8", "DA28COM9", "DA27STB6", "DA02GSD9"};

constexpr std::array<std::size_t, 9> source_faces1{7, 4, 20, 4, 6, 6, 2, 3, 2};

constexpr std::array views1{View{"front", {0, -38, -8}, {0, 0, -8}}, View{"back", {0, 40, -8}, {0, 0, -8}}, View{"left-side", {-34, -8, -13}, {0, 0, -8}},
    View{"right-side", {34, -8, -13}, {0, 0, -8}}, View{"front-oblique", {26, -32, -23}, {0, 0, -8}}, View{"back-oblique", {-26, 32, -23}, {0, 0, -8}},
    View{"roof", {10, 3, -48}, {0, 1, -12}}, View{"driver", {0, -90, -1.5f}, {0, 1, -8}}, View{"context", {30, -45, -5}, {0, 1, -8}, true},
    View{"front-1080p", {0, -38, -8}, {0, 0, -8}, false, 1920, 1080}, View{"context-1080p", {30, -45, -5}, {0, 1, -8}, true, 1920, 1080},
    View{"front-1440p", {0, -38, -8}, {0, 0, -8}, false, 2560, 1440}, View{"context-1440p", {30, -45, -5}, {0, 1, -8}, true, 2560, 1440},
    View{"sign-front", {0, -23, -3.5f}, {0, -5.198f, -3.5f}, false, 1280, 720, true}, View{"sign-rear", {0, 13, -3.5f}, {0, -5.198f, -3.5f}, false, 1280, 720, true},
    View{"sign-edge", {20, 8, -4}, {0, -5.198f, -3.5f}, false, 1280, 720, true}};

constexpr std::array<std::string_view, 5> materials2{"DA28COMA", "DA28COM9", "DA28COMB", "DA28PIT3", "DA02GSD9"};

constexpr std::array<std::size_t, 5> source_faces2{8, 10, 8, 4, 2};

constexpr std::array views2{View{"front", {0, -58, -5.5f}, {0, 1, -4.5f}}, View{"back", {0, 62, -5.5f}, {0, 1, -4.5f}}, View{"left-side", {-50, -8, -11}, {0, 1, -4.5f}},
    View{"right-side", {50, -8, -11}, {0, 1, -4.5f}}, View{"front-oblique", {38, -43, -17}, {0, 1, -4.5f}}, View{"back-oblique", {-38, 43, -17}, {0, 1, -4.5f}},
    View{"roof", {15, 4, -48}, {0, 2, -7.4f}}, View{"driver", {0, -110, -1.5f}, {0, 1, -4.2f}}, View{"context", {35, -45, -3}, {0, 1, -4.5f}, true},
    View{"front-close", {0, -28, -5.5f}, {0, 1, -4.5f}, false, 1920, 1080}, View{"front-oblique-close", {21, -24, -12}, {0, 1, -4.5f}, false, 1920, 1080},
    View{"back-close", {0, 30, -5.5f}, {0, 1, -4.5f}, false, 1920, 1080}, View{"back-oblique-close", {-21, 24, -12}, {0, 1, -4.5f}, false, 1920, 1080},
    View{"roof-close", {8, -20, -30}, {0, 2, -7.4f}, false, 1920, 1080}, View{"context-1080p", {35, -45, -3}, {0, 1, -4.5f}, true, 1920, 1080},
    View{"sign-front", {0, -37, -3.56f}, {0, -4.263f, -3.56f}, false, 1280, 720, true}, View{"sign-rear", {0, 30, -3.56f}, {0, -4.263f, -3.56f}, false, 1280, 720, true},
    View{"sign-edge", {35, 15, -4.56f}, {0, -4.263f, -3.56f}, false, 1280, 720, true}};

constexpr std::array<std::string_view, 5> materials3{"DA28COMA", "DA28COM9", "DA28COM4", "DA28PIT3", "DA02GSD9"};

constexpr std::array<std::size_t, 5> source_faces3{8, 8, 10, 4, 2};

constexpr std::array views3{View{"front", {0, -58, -5.5f}, {0, 1, -4.5f}}, View{"back", {0, 62, -5.5f}, {0, 1, -4.5f}}, View{"left-side", {-50, -8, -11}, {0, 1, -4.5f}},
    View{"right-side", {50, -8, -11}, {0, 1, -4.5f}}, View{"front-oblique", {38, -43, -17}, {0, 1, -4.5f}}, View{"back-oblique", {-38, 43, -17}, {0, 1, -4.5f}},
    View{"roof", {15, 4, -48}, {0, 2, -7.4f}}, View{"driver", {0, -110, -1.5f}, {0, 1, -4.2f}}, View{"context", {35, -45, -3}, {0, 1, -4.5f}, true},
    View{"front-close", {0, -28, -5.5f}, {0, 1, -4.5f}, false, 1920, 1080}, View{"front-oblique-close", {21, -24, -12}, {0, 1, -4.5f}, false, 1920, 1080},
    View{"back-oblique-close", {-21, 24, -12}, {0, 1, -4.5f}, false, 1920, 1080}, View{"roof-close", {8, -20, -30}, {0, 2, -7.4f}, false, 1920, 1080},
    View{"front-1080p", {0, -58, -5.5f}, {0, 1, -4.5f}, false, 1920, 1080}, View{"context-1080p", {35, -45, -3}, {0, 1, -4.5f}, true, 1920, 1080},
    View{"front-1440p", {0, -58, -5.5f}, {0, 1, -4.5f}, false, 2560, 1440}, View{"context-1440p", {35, -45, -3}, {0, 1, -4.5f}, true, 2560, 1440},
    View{"sign-front", {0, -37, -2.97f}, {0, -4.263f, -2.97f}, false, 1280, 720, true}, View{"sign-rear", {0, 30, -2.97f}, {0, -4.263f, -2.97f}, false, 1280, 720, true},
    View{"sign-edge", {35, 15, -3.97f}, {0, -4.263f, -2.97f}, false, 1280, 720, true}};

constexpr std::array<std::string_view, 5> materials4{"DA28COM9", "DA28COMA", "DA28PIT3", "DA28COM4", "DA02GSD9"};

constexpr std::array<std::size_t, 5> source_faces4{8, 8, 2, 10, 2};

constexpr std::array views4{View{"front", {0, -58, -4.7f}, {0, 1, -3.5f}}, View{"back", {0, 62, -4.7f}, {0, 1, -3.5f}}, View{"left-side", {-50, -8, -11}, {0, 1, -3.5f}},
    View{"right-side", {50, -8, -11}, {0, 1, -3.5f}}, View{"front-oblique", {38, -43, -17}, {0, 1, -3.5f}}, View{"back-oblique", {-38, 43, -17}, {0, 1, -3.5f}},
    View{"roof", {15, 4, -48}, {0, 2, -6.3f}}, View{"driver", {0, -110, -1.5f}, {0, 1, -3.2f}}, View{"context", {35, -45, -3}, {0, 1, -3.5f}, true},
    View{"front-1080p", {0, -58, -4.7f}, {0, 1, -3.5f}, false, 1920, 1080}, View{"context-1080p", {35, -45, -3}, {0, 1, -3.5f}, true, 1920, 1080},
    View{"front-1440p", {0, -58, -4.7f}, {0, 1, -3.5f}, false, 2560, 1440}, View{"context-1440p", {35, -45, -3}, {0, 1, -3.5f}, true, 2560, 1440},
    View{"sign-front", {0, -37, -2.4f}, {0, -4.263f, -2.4f}, false, 1280, 720, true}, View{"sign-rear", {0, 30, -2.4f}, {0, -4.263f, -2.4f}, false, 1280, 720, true},
    View{"sign-edge", {35, 15, -3.4f}, {0, -4.263f, -2.4f}, false, 1280, 720, true}};

constexpr std::array<std::string_view, 5> materials5{"DA28COMA", "DA28COM9", "DA28COM4", "DA28PIT3", "DA02GSD9"};

constexpr std::array<std::size_t, 5> source_faces5{8, 8, 10, 4, 2};

constexpr std::array views5{View{"front", {0, -58, -4.7f}, {0, 1, -3.5f}}, View{"back", {0, 62, -4.7f}, {0, 1, -3.5f}}, View{"left-side", {-50, -8, -11}, {0, 1, -3.5f}},
    View{"right-side", {50, -8, -11}, {0, 1, -3.5f}}, View{"front-oblique", {38, -43, -17}, {0, 1, -3.5f}}, View{"back-oblique", {-38, 43, -17}, {0, 1, -3.5f}},
    View{"roof", {15, 4, -48}, {0, 2, -6.3f}}, View{"driver", {0, -110, -1.5f}, {0, 1, -3.2f}}, View{"context", {35, -45, -3}, {0, 1, -3.5f}, true},
    View{"front-1080p", {0, -58, -4.7f}, {0, 1, -3.5f}, false, 1920, 1080}, View{"context-1080p", {35, -45, -3}, {0, 1, -3.5f}, true, 1920, 1080},
    View{"front-1440p", {0, -58, -4.7f}, {0, 1, -3.5f}, false, 2560, 1440}, View{"context-1440p", {35, -45, -3}, {0, 1, -3.5f}, true, 2560, 1440},
    View{"sign-front", {0, -37, -1.8f}, {0, -4.263f, -1.8f}, false, 1280, 720, true}, View{"sign-rear", {0, 30, -1.8f}, {0, -4.263f, -1.8f}, false, 1280, 720, true},
    View{"sign-edge", {35, 15, -2.8f}, {0, -4.263f, -1.8f}, false, 1280, 720, true}};

constexpr std::array<std::string_view, 5> materials6{"DA28COMA", "DA28COM9", "DA28COM4", "DA28PIT3", "DA02GSD9"};

constexpr std::array<std::size_t, 5> source_faces6{8, 8, 4, 6, 8};

constexpr std::array views6{View{"front", {0, -58, -4.7f}, {0, 1, -3.5f}}, View{"back", {0, 62, -4.7f}, {0, 1, -3.5f}}, View{"left-side", {-50, -8, -11}, {0, 1, -3.5f}},
    View{"right-side", {50, -8, -11}, {0, 1, -3.5f}}, View{"front-oblique", {38, -43, -17}, {0, 1, -3.5f}}, View{"back-oblique", {-38, 43, -17}, {0, 1, -3.5f}},
    View{"roof", {15, 4, -48}, {0, 2, -6.3f}}, View{"driver", {0, -110, -1.5f}, {0, 1, -3.2f}}, View{"context", {35, -45, -3}, {0, 1, -3.5f}, true},
    View{"front-1080p", {0, -58, -4.7f}, {0, 1, -3.5f}, false, 1920, 1080}, View{"context-1080p", {35, -45, -3}, {0, 1, -3.5f}, true, 1920, 1080},
    View{"front-1440p", {0, -58, -4.7f}, {0, 1, -3.5f}, false, 2560, 1440}, View{"context-1440p", {35, -45, -3}, {0, 1, -3.5f}, true, 2560, 1440},
    View{"sign-front", {0, -37, -1.8f}, {0, -5.198f, -1.8f}, false, 1280, 720, true}, View{"sign-rear", {0, 30, -1.8f}, {0, -5.198f, -1.8f}, false, 1280, 720, true},
    View{"sign-edge", {35, 15, -2.8f}, {0, -5.198f, -1.8f}, false, 1280, 720, true}};

constexpr std::array profiles{CboProfile{1, "DA28CBO1_1", materials1, source_faces1, {48, 50}, {307, 347}, views1, 84, false, "DA28PIT2"},
    CboProfile{2, "DA28CBO2_1", materials2, source_faces2, {52, 54}, {341, 381}, views2, 61, true, "DA28PIT3"},
    CboProfile{3, "DA28CBO3_1", materials3, source_faces3, {51, 53}, {339, 379}, views3, 64, false, "DA28PIT3"},
    CboProfile{4, "DA28CBO4_1", materials4, source_faces4, {10, 9}, {33, 35}, views4, 59, false, "DA28PIT3"},
    CboProfile{5, "DA28CBO5_1", materials5, source_faces5, {11, 10}, {35, 37}, views5, 60, false, "DA28PIT3"},
    CboProfile{6, "DA28CBO6_1", materials6, source_faces6, {12, 11}, {37, 39}, views6, 63, false, "DA28PIT3"}};

struct CboCase {
    std::uint32_t track;
    std::size_t mesh;
    std::array<std::size_t, 1> nodes;
    const CboProfile* profile;
};

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

std::vector<std::size_t> chunks(const SceneResources& scene, const CboCase& pilot) {
    const auto& profile = *pilot.profile;
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < scene.objects_baked.chunks.size(); ++index) {
        const auto& chunk = scene.objects_baked.chunks[index];
        if (chunk.mesh.name == profile.mesh_name) {
            expect(
                chunk.variant_index == 0 && std::ranges::find(pilot.nodes, chunk.node_index) != pilot.nodes.end(), "CBO must retain its original baked node/variant registrations");
            result.push_back(index);
        }
    }
    expect(result.size() == pilot.nodes.size(), "The original cbo placements must reach the baked scene");
    return result;
}

using Triangle = std::array<float, 20>;

std::vector<Triangle> triangles(const Model& model, std::size_t mesh_index, const CboProfile& profile) {
    const auto materials = profile.materials;
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
        expect(material != materials.end(), "CBO faces must use only its source-specific materials");
        triangle[cursor++] = float(material - materials.begin());
        for (const auto color : face.color) {
            triangle[cursor++] = float(color);
        }
        result.push_back(triangle);
    }
    std::ranges::sort(result);
    return result;
}

void source_report(const SceneResources& original, const CboCase& pilot) {
    const auto& profile = *pilot.profile;
    const auto materials = profile.materials;
    const auto source_faces = profile.source_faces;
    const auto& mesh = original.objects.meshes.at(pilot.mesh);
    const auto face_count = std::accumulate(source_faces.begin(), source_faces.end(), std::size_t{0});
    expect(mesh.name == profile.mesh_name && mesh.faces.size() == face_count && mesh.vertices.size() == profile.source_vertices,
        "CBO fixture must select the original source geometry and material-face distribution");
    const auto box = bounds(mesh);
    std::ofstream report(profile.label() + "-track" + std::to_string(pilot.track) + "-source.csv");
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
    std::cout << "SOURCE " << profile.label() << " track" << pilot.track << " mesh=" << pilot.mesh << " nodes=" << pilot.nodes[0] << " bounds=";
    for (const auto coordinate : box) {
        std::cout << coordinate << ',';
    }
    std::cout << " (-Z up; -Y facade and billboard; +Y rear; +/-X ends)\n";
    for (std::size_t index = 0; index < materials.size(); ++index) {
        const auto name = materials[index];
        const auto source_texture = texture(original.objects, name);
        expect(source_texture != original.objects.textures.end(), "CBO source must contain all original materials");
        const auto slot = source_texture - original.objects.textures.begin();
        expect(std::ranges::count(mesh.faces, slot, &ModelFace::texture) == std::ptrdiff_t(source_faces[index]),
            "CBO cameras/material roles must use the canonical roof, facade, billboard and rear bindings");
        const auto image = toca::decode_model_texture(*source_texture);
        expect(bool(image), "CBO source artwork must decode");
        gpu::check(
            SDL_SavePNG(image.get(), (profile.label() + "-track" + std::to_string(pilot.track) + "-source-" + std::string(name) + ".png").c_str()), "Save original cbo material");
    }
}

void verify(const SceneResources& original, const SceneResources& mega, const CboCase& pilot, const std::filesystem::path& mega_root) {
    const auto& profile = *pilot.profile;
    const auto materials = profile.materials;
    const auto& before = original.objects;
    const auto& after = mega.objects;
    expect(before.nodes.size() == after.nodes.size() && before.meshes.size() == after.meshes.size(), "CBO presentation must preserve source mesh/node slots");
    for (std::size_t index = 0; index < before.nodes.size(); ++index) {
        const auto& a = before.nodes[index];
        const auto& b = after.nodes[index];
        expect(a.parent == b.parent && a.transform == b.transform && a.id_flags == b.id_flags && a.variants == b.variants,
            "CBO presentation must retain exact hierarchy, placements, sector flags and variants");
    }
    for (const auto node : pilot.nodes) {
        expect(before.nodes.at(node).variants == std::vector<std::uint16_t>{std::uint16_t(pilot.mesh)} && before.nodes.at(node).parent == std::int16_t(node - 1),
            "CBO fixture requires its original root/mesh node pairs");
    }
    const auto& source = before.meshes.at(pilot.mesh);
    const auto& detailed = after.meshes.at(pilot.mesh);
    const auto bundled = load_glb(mega_root / "scene" / ("track-" + std::to_string(pilot.track)) / "objects.glb");
    const auto& packed = bundled.meshes.at(pilot.mesh);
    expect(detailed.name == source.name && detailed.vertices.size() == packed.vertices.size() && detailed.faces.size() == packed.faces.size()
               && std::memcmp(detailed.vertices.data(), packed.vertices.data(), detailed.vertices.size() * sizeof(ModelVertex)) == 0
               && triangles(after, pilot.mesh, profile) == triangles(bundled, pilot.mesh, profile),
        "CBO must use the actual bundled generated GLB through the normal detail4 resource loader");
    expect(detailed.faces.size() > source.faces.size() && detailed.vertices.size() < 65536, "CBO must add genuine bounded roof, window and sign geometry");
    const auto source_triangles = triangles(before, pilot.mesh, profile), detailed_triangles = triangles(after, pilot.mesh, profile);
    expect(std::ranges::includes(detailed_triangles, source_triangles), "CBO must preserve the source facade/sign/roof mappings, their physical surfaces and winding");
    for (const auto name : materials) {
        const auto source_texture = texture(before, name);
        const auto replacement = texture(after, name);
        expect(source_texture != before.textures.end() && replacement != after.textures.end() && source_texture - before.textures.begin() == replacement - after.textures.begin()
                   && source_texture->name == replacement->name && replacement->encoding == ImageEncoding::webp,
            "CBO must retain each original named material slot with accepted WebP artwork");
        const auto image = toca::decode_model_texture(*replacement);
        const auto original_image = toca::decode_model_texture(*source_texture);
        expect(image && image->w >= (profile.thin_shell ? 1 : 128) && image->h >= (profile.thin_shell ? 1 : 64) && image->w <= 512 && image->h <= 512,
            "CBO artwork must use a useful authored resolution within the 512-pixel Mega limit");
        expect(original_image && image->w * original_image->h == image->h * original_image->w, "CBO artwork must preserve each source atlas aspect ratio");
        std::cout << "MATERIAL " << profile.label() << " track" << pilot.track << ' ' << name << " slot=" << replacement - after.textures.begin() << ' ' << original_image->w << 'x'
                  << original_image->h << " -> " << image->w << 'x' << image->h << '\n';
    }
    const auto a = bounds(source), b = bounds(detailed);
    for (std::size_t index = 0; index < a.size(); ++index) {
        expect(std::abs(a[index] - b[index]) < (profile.thin_shell ? .0001f : .2f), "CBO details must stay within twenty centimeters of the original building envelope");
    }
    // Local -Z is up. The base feet must remain at the source ground plane;
    // roof/trim detail may grow upward without lowering the whole building.
    expect(std::abs(a[5] - b[5]) < .02f, "CBO must remain attached at its original base-foot height");
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
    expect(profile.thin_shell || depth_vertices >= 24, "CBO additions must add physical depth rather than only subdividing the original planes");
    expect(original.objects_baked.chunks.size() == mega.objects_baked.chunks.size(), "CBO must preserve original baked chunk count");
    const auto original_chunks = chunks(original, pilot), mega_chunks = chunks(mega, pilot);
    expect(original_chunks == mega_chunks, "The CBO placements must retain their original baked chunk slots");
    for (const auto index : original_chunks) {
        const auto& old_chunk = original.objects_baked.chunks[index];
        const auto& new_chunk = mega.objects_baked.chunks[index];
        expect(old_chunk.node_index == new_chunk.node_index && old_chunk.variant_index == new_chunk.variant_index && old_chunk.origin_xz == new_chunk.origin_xz
                   && new_chunk.mesh.faces.size() == detailed.faces.size(),
            "Each cbo placement must consume the detailed geometry at its original sector origin");
        const auto old_box = bounds(old_chunk.mesh), new_box = bounds(new_chunk.mesh);
        expect(std::abs(old_box[1] - new_box[1]) < .02f, "Every world-space cbo placement must retain its source ground attachment");
    }
    expect(original.collision_hpc == mega.collision_hpc && original.route_points == mega.route_points && original.ground.sector_centers == mega.ground.sector_centers,
        "CBO presentation must preserve original terrain contacts, routes and sector data");
    std::cout << "PASS " << profile.label() << " track" << pilot.track << " original placements, " << source.faces.size() << " original / " << detailed.faces.size()
              << " Mega triangles, " << depth_vertices << " vertices away from source planes\n";
}

SceneResources comparison(const SceneResources& original, const SceneResources& mega, const CboCase& pilot, unsigned phase, bool context, std::size_t selected) {
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

void pixels(SDL_Window* window, gpu::Renderer& renderer, const SceneResources& original, const SceneResources& mega, const CboCase& pilot, bool original_only) {
    const auto& profile = *pilot.profile;
    const auto views = profile.views;
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
    const auto prefix = profile.label() + "-track" + std::to_string(pilot.track);
    std::ofstream report(prefix + "-gpu.csv");
    report << "view,width,height,original_triangles,ai_triangles,mega_triangles,material_pixels,geometry_pixels,source_pixels\n";
    std::size_t total_geometry = 0, total_extra_triangles = 0;
    for (const auto& probe : views) {
        // GP receives the complete closeup/size coverage. Short independently
        // proves the shared object in its own actual surroundings and front view.
        if (pilot.track == 5 && probe.name != "front" && probe.name != "context") {
            continue;
        }
        gpu::check(SDL_SetWindowSize(window, probe.width, probe.height), "Resize cbo render target");
        gpu::check(SDL_SyncWindow(window), "Synchronize cbo render target");
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
            if (probe.sign_only) {
                auto& chunk = scene.objects_baked.chunks.front();
                std::vector<ModelFace> faces;
                std::vector<std::uint8_t> flags;
                for (std::size_t index = 0; index < chunk.mesh.faces.size(); ++index) {
                    const auto& face = chunk.mesh.faces[index];
                    if (material_name(scene.objects_baked.textures.at(std::size_t(face.texture)).name) == profile.sign_material) {
                        faces.push_back(face);
                        flags.push_back(index < chunk.face_flags.size() ? chunk.face_flags[index] : 0);
                    }
                }
                chunk.mesh.faces = std::move(faces);
                chunk.face_flags = std::move(flags);
            }
            SceneView rendered(renderer, scene);
            SourceRandom random(7);
            FlagAnimation flags(scene.flags, random);
            HorizonActors actors(scene.moving_horizon);
            auto frame = rendered.frame(camera, scene_lighting(mega.setup), {unit}, flags.variants(), actors.frame());
            const auto projected = toca::test::materialize_world(frame);
            const auto triangles = projected.indices.size() / 3;
            // The original billboard has no rear surface. Preserve that real
            // baseline while requiring the replacement's actual solid backing.
            if (projected.indices.empty()) {
                expect(probe.sign_only, "CBO must reach actual GPU geometry in each building camera");
                frame.quad({}, 0, 0, float(probe.width), float(probe.height), gpu::background);
            }
            gpu::check(renderer.draw(frame, false), "Draw cbo comparison");
            renderer.save_screenshot(prefix + "-" + std::string(probe.name) + "-" + labels[phase] + ".png");
            result[phase] = {renderer.readback(), triangles};
            expect(result[phase].pixels.size() == std::size_t(probe.width * probe.height), "CBO comparison must use identical full-size render targets");
            std::cout << "GEOMETRY " << profile.label() << " track" << pilot.track << ' ' << probe.name << ' ' << labels[phase] << ' ' << probe.width << 'x' << probe.height << ": "
                      << triangles << " triangles, " << projected.vertices.size() << " vertices, " << frame.draws.size() << " draws\n";
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
        if (probe.name == "sign-rear" || probe.name == "sign-edge") {
            std::size_t solid = 0, red = 0;
            for (const auto pixel : result[2].pixels) {
                if (pixel == gpu::background) {
                    continue;
                }
                ++solid;
                const auto r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
                red += r > 10 && r * 100 > g * 120 && r * 100 > b * 120;
            }
            expect(result[2].triangles > 0 && solid > (profile.thin_shell ? 0u : 500u) && red * 100 > solid * 85,
                "CBO billboard must have a solid red backing and edge matching its source artwork's blank red field");
        } else {
            expect(covered > (profile.thin_shell ? 0u : 500u) && material > (profile.thin_shell ? 0u : 50u),
                "Each CBO camera must expose its accepted artwork on the actual building");
        }
        if (!probe.context) {
            total_geometry += geometry;
            total_extra_triangles += result[2].triangles > result[1].triangles ? result[2].triangles - result[1].triangles : 0;
        }
        if (!profile.thin_shell
            && (probe.name == "roof" || probe.name == "left-side" || probe.name == "right-side" || probe.name == "front-oblique" || probe.name == "back-oblique")) {
            expect(geometry > 20 && result[2].triangles > result[1].triangles, "CBO side/roof views must show real additional geometry independently of its artwork");
        }
        report << probe.name << ',' << probe.width << ',' << probe.height << ',' << result[0].triangles << ',' << result[1].triangles << ',' << result[2].triangles << ','
               << material << ',' << geometry << ',' << covered << '\n';
        std::cout << "PASS " << profile.label() << " track" << pilot.track << ' ' << probe.name << " material_pixels=" << material << " geometry_pixels=" << geometry << '\n';
    }
    if (!original_only && !profile.thin_shell && pilot.track == 0) {
        expect(total_geometry > 300 && total_extra_triangles > 0, "CBO closeups must show actual added depth with the same accepted artwork");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool original_only = argc >= 3 && std::string_view(argv[1]) == "--original-only";
        expect(argc == 3 || (argc == 5 && std::string_view(argv[3]) == "--model"),
            "Usage: toca-mega-cbo-render-tests CANONICAL_ROOT MEGA_ROOT [--model 1..6] | --original-only CANONICAL_ROOT [--model 1..6]");
        const int selected = argc == 5 ? std::stoi(argv[4]) : 0;
        expect(selected >= 0 && selected <= 6, "Select CBO1..6 or the complete package");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated building check");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA Donington building comparison", 1280, 720, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create isolated building window");
        gpu::Renderer renderer(window.get());
        const AssetStore store(argv[original_only ? 2 : 1]);
        for (const auto& profile : profiles) {
            if (selected && profile.number != unsigned(selected)) {
                continue;
            }
            std::vector<Triangle> shared;
            for (std::size_t track = 0; track < 2; ++track) {
                const CboCase pilot{track == 0 ? 0u : 5u, profile.meshes[track], {profile.nodes[track]}, &profile};
                const auto original = load_scene_resources(store, resolve_setup({.track_id = pilot.track, .weather_choice = 1, .detail = 3}));
                source_report(original, pilot);
                const auto mega = original_only ? original : load_scene_resources(store, resolve_setup({.track_id = pilot.track, .weather_choice = 1, .detail = 4}), argv[2]);
                if (!original_only) {
                    verify(original, mega, pilot, argv[2]);
                    const auto geometry = triangles(mega.objects, pilot.mesh, profile);
                    if (shared.empty()) {
                        shared = geometry;
                    } else {
                        expect(shared == geometry, "GP and Short must share detailed surfaces/UVs through their named material slots");
                    }
                }
                pixels(window.get(), renderer, original, mega, pilot, original_only);
            }
        }
        std::cout << "PASS: Donington building package, source mappings and three-phase GPU checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
