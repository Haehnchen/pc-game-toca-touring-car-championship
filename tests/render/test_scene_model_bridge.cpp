#include "assets/store.hpp"
#include "formats/glb.hpp"
#include "image/decode.hpp"
#include "scene/finish_camera.hpp"
#include "scene/view.hpp"
#include "scene_models/fixtures.hpp"
#include "scene_models/gpu_fixture.hpp"
#include "world_geometry.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

namespace {
using namespace toca::test::scene_models;

void bridge_reference(const std::filesystem::path& root, const std::filesystem::path& mega_root, bool cleanup = false) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    const auto canonical = load_scene_resources(store, resolve_setup({.track_id = 0, .weather_choice = 1, .detail = 3}));
    const auto mega = load_scene_resources(store, resolve_setup({.track_id = 0, .weather_choice = 1, .detail = 4}), mega_root);
    constexpr std::size_t mesh_index = 22, root_index = 58, node_index = 59;
    const auto& source = canonical.objects.meshes.at(mesh_index);
    const auto& generated = mega.objects.meshes.at(mesh_index);
    expect(source.name == "DA16DUN_1" && generated.name == source.name, "Bridge probe must select the canonical Donington Dunlop mesh");
    const auto packed = toca::load_glb(mega_root / "scene/track-0/objects.glb");
    const auto& packed_mesh = packed.meshes.at(mesh_index);
    expect(generated.vertices.size() == packed_mesh.vertices.size() && generated.faces.size() == packed_mesh.faces.size()
               && std::memcmp(generated.vertices.data(), packed_mesh.vertices.data(), generated.vertices.size() * sizeof(toca::ModelVertex)) == 0,
        "Bridge probe must use the real generated GLB through the normal resource loader");
    for (const auto index : {root_index, node_index}) {
        const auto& before = canonical.objects.nodes.at(index);
        const auto& after = mega.objects.nodes.at(index);
        expect(before.parent == after.parent && before.transform == after.transform && before.id_flags == after.id_flags && before.variants == after.variants,
            "Mega bridge must retain its original root/child placement and mesh binding");
    }
    expect(canonical.objects.nodes.at(node_index).parent == std::int16_t(root_index) && canonical.objects.nodes.at(node_index).variants == std::vector<std::uint16_t>{mesh_index},
        "Canonical bridge root and mesh registration changed");
    // This pilot retains all 116 source faces and adds simple shoulders, barrel and
    // support details around them. Artwork mapping also survives vertex packing.
    const auto triangles = [](const toca::ModelMesh& mesh, std::int16_t slot) {
        std::vector<std::array<float, 15>> result;
        for (const auto& face : mesh.faces) {
            if (face.texture != slot) {
                continue;
            }
            std::array<float, 15> triangle{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& vertex = mesh.vertices.at(face.indices[corner]);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    triangle[corner * 5 + axis] = vertex.position[axis];
                }
                triangle[corner * 5 + 3] = vertex.u;
                triangle[corner * 5 + 4] = vertex.v;
            }
            result.push_back(triangle);
        }
        std::ranges::sort(result);
        return result;
    };
    constexpr std::array<const char*, 5> names{"DA16DUN0", "DA16DUN1", "DA16DUN4", "DA16DUN8", "DA16DUN9"};
    constexpr std::array<std::size_t, 5> face_counts{20, 4, 12, 12, 68};
    constexpr std::array<std::array<int, 2>, 5> sizes{{{512, 512}, {512, 128}, {256, 256}, {512, 128}, {512, 512}}};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto slot = std::int16_t(49 + index);
        const auto before = triangles(source, slot), after = triangles(generated, slot);
        expect(canonical.objects.textures.at(slot).name == names[index] && mega.objects.textures.at(slot).name == names[index] && before.size() == face_counts[index]
                   && !after.empty(),
            "Bridge must retain all five canonical material slots on actual geometry");
        const auto image = toca::decode_model_texture(mega.objects.textures.at(slot));
        expect(mega.objects.textures.at(slot).encoding == toca::ImageEncoding::webp && image->w == sizes[index][0] && image->h == sizes[index][1],
            "Mega bridge must load all five authored materials, including the slogan and supports");
        std::cout << "Bridge slot " << slot << ' ' << names[index] << ": " << image->w << 'x' << image->h << ", " << before.size() << " original / " << after.size()
                  << " Mega triangles\n";
    }
    expect(generated.faces.size() == 520 && generated.vertices.size() > source.vertices.size(), "Mega bridge must contain additional arch, shoulder and support geometry");
    expect(generated.faces.size() <= source.faces.size() + 2048 && generated.vertices.size() < 65536, "Detailed bridge must remain within its focused static-mesh geometry budget");
    expect(std::ranges::all_of(generated.faces,
               [](const auto& face) {
                   return face.texture >= 49 && face.texture <= 53;
               }),
        "Detailed bridge must use its five accepted artworks rather than unrelated object materials");
    // Barycentric samples verify the actual surface/UV correspondence rather
    // than triangle-record identity, allowing the replacement to subdivide it.
    const auto barycentric = [](const std::array<float, 3>& point, const toca::ModelVertex& a, const toca::ModelVertex& b, const toca::ModelVertex& c) {
        const auto determinant = (b.position[2] - c.position[2]) * (a.position[0] - c.position[0]) + (c.position[0] - b.position[0]) * (a.position[2] - c.position[2]);
        if (std::abs(determinant) < .000001f) {
            return std::array<float, 3>{-1, -1, -1};
        }
        const auto first = ((b.position[2] - c.position[2]) * (point[0] - c.position[0]) + (c.position[0] - b.position[0]) * (point[2] - c.position[2])) / determinant;
        const auto second = ((c.position[2] - a.position[2]) * (point[0] - c.position[0]) + (a.position[0] - c.position[0]) * (point[2] - c.position[2])) / determinant;
        return std::array<float, 3>{first, second, 1 - first - second};
    };
    constexpr std::array<std::array<float, 3>, 4> samples{{{1.f / 3, 1.f / 3, 1.f / 3}, {.6f, .2f, .2f}, {.2f, .6f, .2f}, {.2f, .2f, .6f}}};
    std::size_t artwork_samples = 0;
    for (const auto& face : source.faces) {
        const auto& a = source.vertices.at(face.indices[0]);
        const auto& b = source.vertices.at(face.indices[1]);
        const auto& c = source.vertices.at(face.indices[2]);
        if ((face.texture != 49 && face.texture != 53) || std::abs(a.position[1] - b.position[1]) > .0001f || std::abs(a.position[1] - c.position[1]) > .0001f) {
            continue;
        }
        for (const auto& weights : samples) {
            std::array<float, 3> point{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                point[axis] = weights[0] * a.position[axis] + weights[1] * b.position[axis] + weights[2] * c.position[axis];
            }
            const auto expected_u = weights[0] * a.u + weights[1] * b.u + weights[2] * c.u;
            const auto expected_v = weights[0] * a.v + weights[1] * b.v + weights[2] * c.v;
            const auto mapped = std::ranges::any_of(generated.faces, [&](const auto& candidate) {
                if (candidate.texture != face.texture) {
                    return false;
                }
                const auto& x = generated.vertices.at(candidate.indices[0]);
                const auto& y = generated.vertices.at(candidate.indices[1]);
                const auto& z = generated.vertices.at(candidate.indices[2]);
                if (std::abs(x.position[1] - point[1]) > .001f || std::abs(y.position[1] - point[1]) > .001f || std::abs(z.position[1] - point[1]) > .001f) {
                    return false;
                }
                const auto actual = barycentric(point, x, y, z);
                return std::ranges::all_of(actual,
                           [](auto value) {
                               return value >= -.0001f;
                           })
                       && std::abs(actual[0] * x.u + actual[1] * y.u + actual[2] * z.u - expected_u) < .0001f
                       && std::abs(actual[0] * x.v + actual[1] * y.v + actual[2] * z.v - expected_v) < .0001f;
            });
            expect(mapped, "Mega front and rear arch panels must preserve the accepted artwork at their physical source positions");
            ++artwork_samples;
        }
    }
    expect(artwork_samples >= 80, "Bridge artwork probes must cover both arch halves from front and rear");
    // The N crosses two different materials at the physical crown edge. Equal
    // image-edge rows are not equal heights: the original V offset and static
    // UV inset must both participate in this actual bundled-artwork check.
    const auto crown = std::ranges::max_element(source.vertices, {}, [](const auto& vertex) {
        return vertex.position[2];
    });
    const auto seam_endpoints = [&](std::int16_t slot) {
        std::array<const toca::ModelVertex*, 2> endpoints{};
        for (const auto& face : source.faces) {
            // Tread/thickness corners share these positions but use other UV
            // islands. Select the complete planar front before its seam edge.
            if (face.texture != slot || !std::ranges::all_of(face.indices, [&](auto index) {
                    return source.vertices.at(index).position[1] > 2.75f;
                })) {
                continue;
            }
            for (const auto index : face.indices) {
                const auto& vertex = source.vertices.at(index);
                if (std::abs(vertex.position[0] - crown->position[0]) > .0001f) {
                    continue;
                }
                if (!endpoints[0] || vertex.position[2] < endpoints[0]->position[2]) {
                    endpoints[0] = &vertex;
                }
                if (!endpoints[1] || vertex.position[2] > endpoints[1]->position[2]) {
                    endpoints[1] = &vertex;
                }
            }
        }
        expect(endpoints[0] && endpoints[1] && endpoints[0]->position[2] < endpoints[1]->position[2], "Bridge N probe requires the shared canonical crown edge");
        auto result = std::array{*endpoints[0], *endpoints[1]};
        for (auto& vertex : result) {
            vertex.u = std::clamp(vertex.u, .02f, .99f);
            vertex.v = std::clamp(vertex.v, .02f, .99f);
        }
        return result;
    };
    const auto seam0 = seam_endpoints(49), seam9 = seam_endpoints(53);
    expect(seam0[0].position[2] == seam9[0].position[2] && seam0[1].position[2] == seam9[1].position[2], "Both N halves must meet at the same physical heights");
    const auto seam_image = [&](std::size_t slot) {
        const auto decoded = toca::decode_model_texture(mega.objects.textures.at(slot));
        toca::Surface image(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_ARGB8888), SDL_DestroySurface);
        expect(bool(image), "Bridge N probe requires decoded bundled material pixels");
        return image;
    };
    const auto image0 = seam_image(49), image9 = seam_image(53);
    const auto bilinear_rgb = [](const SDL_Surface& image, double u, double v) {
        const auto x = u * image.w - .5, y = v * image.h - .5;
        const auto x0 = int(std::floor(x)), y0 = int(std::floor(y));
        const auto fx = x - x0, fy = y - y0;
        std::array<double, 3> result{};
        for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
                const auto column = std::clamp(x0 + dx, 0, image.w - 1), row = std::clamp(y0 + dy, 0, image.h - 1);
                std::uint32_t pixel;
                std::memcpy(&pixel, static_cast<const std::uint8_t*>(image.pixels) + row * image.pitch + column * 4, 4);
                const auto weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                for (std::size_t channel = 0; channel < result.size(); ++channel) {
                    result[channel] += double((pixel >> ((2 - channel) * 8)) & 255) * weight;
                }
            }
        }
        return result;
    };
    constexpr std::size_t seam_samples = 256;
    double seam_error = 0;
    for (std::size_t sample = 0; sample < seam_samples; ++sample) {
        const auto amount = double(sample) / double(seam_samples - 1);
        const auto first = bilinear_rgb(*image0, std::lerp(double(seam0[0].u), double(seam0[1].u), amount), std::lerp(double(seam0[0].v), double(seam0[1].v), amount));
        const auto second = bilinear_rgb(*image9, std::lerp(double(seam9[0].u), double(seam9[1].u), amount), std::lerp(double(seam9[0].v), double(seam9[1].v), amount));
        for (std::size_t channel = 0; channel < first.size(); ++channel) {
            seam_error += std::abs(first[channel] - second[channel]);
        }
    }
    seam_error /= double(seam_samples * 3);
    std::cout << "Bridge N seam: " << seam_samples << " equal-height bilinear samples, mean RGB difference " << seam_error << "/255\n";
    expect(seam_error < 12, "Bundled Dunlop N halves must join continuously at their canonical physical seam");
    const auto chunk = [](const toca::Terrain& terrain) -> const toca::TerrainChunk& {
        const auto found = std::ranges::find_if(terrain.chunks, [](const auto& entry) {
            return entry.node_index == node_index && entry.variant_index == 0;
        });
        expect(found != terrain.chunks.end() && found->mesh.name == "DA16DUN_1", "Bridge probe requires the original baked sector chunk");
        return *found;
    };
    const auto& original_chunk = chunk(canonical.objects_baked);
    const auto& mega_chunk = chunk(mega.objects_baked);
    expect(original_chunk.origin_xz == mega_chunk.origin_xz, "Mega bridge must retain its original world-sector registration");
    const auto bounds = [](const toca::ModelMesh& mesh) {
        std::array<float, 6> result{32768, 32768, 32768, -32768, -32768, -32768};
        for (const auto& vertex : mesh.vertices) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                result[axis] = std::min(result[axis], vertex.position[axis]);
                result[axis + 3] = std::max(result[axis + 3], vertex.position[axis]);
            }
        }
        return result;
    };
    const auto original_bounds = bounds(original_chunk.mesh), mega_bounds = bounds(mega_chunk.mesh);
    for (std::size_t axis = 0; axis < original_bounds.size(); ++axis) {
        expect(std::abs(original_bounds[axis] - mega_bounds[axis]) < .2f, "Mega bridge details must remain in the original assembled bridge envelope");
    }
    const auto source_bounds = bounds(source), detailed_bounds = bounds(generated);
    for (std::size_t axis = 0; axis < source_bounds.size(); ++axis) {
        expect(std::abs(source_bounds[axis] - detailed_bounds[axis]) < (axis % 3 == 1 ? .1f : .15f),
            "Rounded bridge shoulders and supports must preserve the source silhouette and road attachment");
    }
    std::vector<float> original_depth, detailed_depth;
    for (const auto& vertex : source.vertices) {
        original_depth.push_back(vertex.position[1]);
    }
    for (const auto& vertex : generated.vertices) {
        if (std::ranges::none_of(original_depth,
                [&](float value) {
                    return std::abs(value - vertex.position[1]) < .001f;
                })
            && std::ranges::none_of(detailed_depth, [&](float value) {
                   return std::abs(value - vertex.position[1]) < .001f;
               })) {
            detailed_depth.push_back(vertex.position[1]);
        }
    }
    expect(detailed_depth.size() >= 6, "Bridge detail must add real depth levels for rounded shoulders rather than coplanar subdivisions");
    // Check the original unobstructed central road opening through the complete
    // bridge depth. Triangle/rectangle SAT catches a face crossing the passage
    // even when none of its vertices or centroids lies inside that opening.
    const auto obstructs_passage = [](const toca::ModelMesh& mesh) {
        constexpr std::array<std::array<float, 2>, 4> passage{{{-12, .05f}, {12, .05f}, {12, 5.95f}, {-12, 5.95f}}};
        return std::ranges::any_of(mesh.faces, [&](const auto& face) {
            std::array<std::array<float, 2>, 3> points{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& position = mesh.vertices.at(face.indices[corner]).position;
                points[corner] = {position[0], position[2]};
            }
            std::array<std::array<float, 2>, 5> axes{{{1, 0}, {0, 1}}};
            for (std::size_t edge = 0; edge < 3; ++edge) {
                const auto& a = points[edge];
                const auto& b = points[(edge + 1) % 3];
                axes[edge + 2] = {a[1] - b[1], b[0] - a[0]};
            }
            for (const auto& axis : axes) {
                if (std::abs(axis[0]) + std::abs(axis[1]) < .000001f) {
                    continue;
                }
                const auto project = [&](const auto& point) {
                    return point[0] * axis[0] + point[1] * axis[1];
                };
                auto low = project(points[0]), high = low;
                for (const auto& point : points) {
                    low = std::min(low, project(point));
                    high = std::max(high, project(point));
                }
                auto opening_low = project(passage[0]), opening_high = opening_low;
                for (const auto& point : passage) {
                    opening_low = std::min(opening_low, project(point));
                    opening_high = std::max(opening_high, project(point));
                }
                if (high <= opening_low || low >= opening_high) {
                    return false;
                }
            }
            return true;
        });
    };
    expect(!obstructs_passage(source) && !obstructs_passage(generated), "Bridge geometry must preserve the original 24m-wide central passage below the banner");
    expect(canonical.collision_hpc == mega.collision_hpc && canonical.route_points == mega.route_points && canonical.ground.sector_centers == mega.ground.sector_centers
               && canonical.objects_baked.chunks.size() == mega.objects_baked.chunks.size(),
        "Bridge presentation must retain original collision, route and sector registrations");
    {
        const auto short_original = load_scene_resources(store, resolve_setup({.track_id = 5, .weather_choice = 1, .detail = 3}));
        const auto short_mega = load_scene_resources(store, resolve_setup({.track_id = 5, .weather_choice = 1, .detail = 4}), mega_root);
        constexpr std::size_t short_mesh_index = 21, short_node_index = 61;
        const auto& original = short_original.objects.meshes.at(short_mesh_index);
        const auto& replacement = short_mega.objects.meshes.at(short_mesh_index);
        const auto packed_short = toca::load_glb(mega_root / "scene/track-5/objects.glb");
        const auto& bundled = packed_short.meshes.at(short_mesh_index);
        expect(original.name == source.name && replacement.name == source.name && replacement.vertices.size() == generated.vertices.size()
                   && replacement.faces.size() == generated.faces.size()
                   && std::memcmp(replacement.vertices.data(), generated.vertices.data(), generated.vertices.size() * sizeof(toca::ModelVertex)) == 0
                   && bundled.vertices.size() == replacement.vertices.size() && bundled.faces.size() == replacement.faces.size()
                   && std::memcmp(replacement.vertices.data(), bundled.vertices.data(), replacement.vertices.size() * sizeof(toca::ModelVertex)) == 0,
            "Donington Short must load the same detailed bridge through its own bundled OBJECTS member and mesh slot");
        for (std::size_t index = 0; index < generated.faces.size(); ++index) {
            const auto& gp = generated.faces[index];
            const auto& short_face = replacement.faces[index];
            expect(gp.indices == short_face.indices && gp.color == short_face.color
                       && mega.objects.textures.at(gp.texture).name == short_mega.objects.textures.at(short_face.texture).name,
                "GP and Short must share bridge geometry and artwork despite their different material-slot indices");
        }
        for (const auto index : {short_node_index, std::size_t(short_original.objects.nodes.at(short_node_index).parent)}) {
            const auto& before = short_original.objects.nodes.at(index);
            const auto& after = short_mega.objects.nodes.at(index);
            expect(before.parent == after.parent && before.transform == after.transform && before.id_flags == after.id_flags && before.variants == after.variants,
                "Donington Short bridge must retain its own original hierarchy, placement and mesh binding");
        }
        expect(short_original.objects.nodes.at(short_node_index).variants == std::vector<std::uint16_t>{short_mesh_index} && !obstructs_passage(replacement)
                   && short_original.collision_hpc == short_mega.collision_hpc && short_original.route_points == short_mega.route_points
                   && short_original.ground.sector_centers == short_mega.ground.sector_centers,
            "Shared bridge must preserve Donington Short's passage, collision and route");
        std::cout << "PASS: Donington Short shared detailed bridge, original hierarchy, passage, collision and route\n";
    }
    GpuFixture gpu("TOCA Donington bridge comparison", 1280, 720);
    auto& renderer = gpu.renderer;
    auto& window = gpu.window;
    const auto& matrix = canonical.objects.nodes.at(root_index).transform;
    const auto world = [&](const std::array<float, 3>& local) {
        std::array<std::int32_t, 3> result;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result[axis] = std::int32_t((local[0] * matrix[axis] + local[1] * matrix[4 + axis] + local[2] * matrix[8 + axis] + matrix[12 + axis]) * 65536.f);
        }
        return result;
    };

    struct View {
        const char* name;
        std::array<float, 3> origin, target;
        bool context;
        int width = 1280, height = 720;
    };

    // Local Z is height. Keep both complete halves and their feet in the whole
    // views; the closer view exposes the center seam and reused panel islands.
    constexpr std::array<View, 16> views{{{"front", {0, -52, 8}, {0, 0, 7.5f}, false}, {"rear", {0, 52, 8}, {0, 0, 7.5f}, false}, {"center", {0, -24, 8.2f}, {0, 0, 8.2f}, false},
        {"left-side", {-34, -26, 8}, {0, 0, 7.5f}, false}, {"right-side", {34, 26, 8}, {0, 0, 7.5f}, false}, {"underside-front", {0, -10, 2}, {0, 0, 7.5f}, false},
        {"underside-rear", {0, 10, 2}, {0, 0, 7.5f}, false}, {"drive-front", {0, -95, 1.5f}, {0, 0, 7.5f}, false}, {"drive-rear", {0, 95, 1.5f}, {0, 0, 7.5f}, false},
        {"context", {-15, -55, 4}, {0, 0, 7.5f}, true}, {"n-front", {.029861f, -14, 12.5f}, {.029861f, 0, 12.5f}, false},
        {"n-rear", {.029861f, 14, 12.5f}, {.029861f, 0, 12.5f}, false}, {"front-1080p", {0, -52, 8}, {0, 0, 7.5f}, false, 1920, 1080},
        {"context-1080p", {-15, -55, 4}, {0, 0, 7.5f}, true, 1920, 1080}, {"front-1440p", {0, -52, 8}, {0, 0, 7.5f}, false, 2560, 1440},
        {"context-1440p", {-15, -55, 4}, {0, 0, 7.5f}, true, 2560, 1440}}};
    auto artwork_only = canonical;
    artwork_only.objects_baked.textures = mega.objects_baked.textures;
    std::size_t total_geometry_pixels = 0, total_added_triangles = 0;
    for (const auto& probe : views) {
        const std::string_view probe_name(probe.name);
        if (cleanup && probe_name != "left-side" && probe_name != "context") {
            continue;
        }
        const auto width = probe.width, height = probe.height;
        toca::gpu::check(SDL_SetWindowSize(window.get(), width, height), "Resize bridge comparison target");
        toca::gpu::check(SDL_SyncWindow(window.get()), "Synchronize bridge comparison target");
        SDL_PumpEvents();
        const auto target = world(probe.target), origin = world(probe.origin);
        std::array<SceneCamera, 1> cameras{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            cameras[0].transform[12 + axis] = origin[axis];
        }
        const FinishCamera look(cameras);
        CameraInputs camera{look.world_to_view(target), default_projection(3, width, height)};
        camera.registered_camera_position = target;
        camera.hide_player = true;
        const auto render = [&](const SceneResources& resources, std::string_view quality) {
            SceneResources isolated;
            if (probe.context) {
                // Keep the actual Mega surroundings fixed; only the pilot
                // bridge changes between the three full-scene comparisons.
                isolated = mega;
                if (quality != "mega") {
                    const auto found = std::ranges::find_if(isolated.objects_baked.chunks, [](const auto& entry) {
                        return entry.node_index == node_index && entry.variant_index == 0;
                    });
                    expect(found != isolated.objects_baked.chunks.end(), "Bridge scene comparison requires its Mega sector chunk");
                    *found = chunk(canonical.objects_baked);
                }
                if (quality == "very-high") {
                    for (std::size_t slot = 49; slot <= 53; ++slot) {
                        isolated.objects_baked.textures.at(slot) = canonical.objects_baked.textures.at(slot);
                    }
                }
            } else {
                isolated.setup = resources.setup;
                isolated.ground.sector_centers = canonical.ground.sector_centers;
                isolated.objects_baked.chunks.push_back(chunk(resources.objects_baked));
                isolated.objects_baked.textures = resources.objects_baked.textures;
            }
            // Hold the renderer policy (including MSAA) fixed as well as the
            // camera/light, so the second difference isolates actual geometry.
            isolated.setup = mega.setup;
            SceneView view(renderer, isolated);
            HorizonActors actors(isolated.moving_horizon);
            const auto frame = view.frame(camera, scene_lighting(resources.setup), {unit}, std::vector<std::uint8_t>(isolated.flags.model.nodes.size()), actors.frame());
            const auto projected = toca::test::materialize_world(frame);
            expect(projected.indices.size() > 30, "Donington bridge must reach actual GPU geometry in every comparison view");
            expect(renderer.draw(frame, false), "Bridge comparison render target was unavailable");
            renderer.save_screenshot("scene-bridge-" + std::string(probe.name) + "-" + std::string(quality) + ".png");
            std::cout << "BUDGET bridge " << probe.name << ' ' << quality << ' ' << width << 'x' << height << ": " << projected.indices.size() / 3 << " triangles, "
                      << projected.vertices.size() << " projected vertices, " << frame.draws.size() << " draws\n";
            return std::pair{renderer.readback(), projected.indices.size() / 3};
        };
        const auto before = render(canonical, "very-high"), textured = render(artwork_only, "ai-materials-original-geometry"), after = render(mega, "mega");
        expect(before.first.size() == std::size_t(width * height) && textured.first.size() == before.first.size() && after.first.size() == before.first.size(),
            "Bridge comparison must use identical full-size render targets");
        std::size_t material_changes = 0, geometry_changes = 0, covered = 0;
        for (std::size_t pixel = 0; pixel < before.first.size(); ++pixel) {
            material_changes += before.first[pixel] != textured.first[pixel];
            geometry_changes += textured.first[pixel] != after.first[pixel];
            covered += before.first[pixel] != toca::gpu::background;
        }
        expect(covered > 1000 && material_changes > 100, "Bridge comparison must expose original and accepted Mega artwork in actual GPU pixels");
        if (!probe.context) {
            total_geometry_pixels += geometry_changes;
            total_added_triangles += after.second > textured.second ? after.second - textured.second : 0;
        }
        const std::string_view name(probe.name);
        if (name == "left-side" || name == "right-side" || name == "underside-front" || name == "underside-rear") {
            expect(
                geometry_changes > 40 && after.second > textured.second + 8, "Oblique and passage views must expose additional shoulder and barrel geometry with the same artwork");
        }
        std::cout << "PASS: bridge " << probe.name << ", identical camera/projection, " << covered << " original pixels, " << material_changes << " artwork pixels, "
                  << geometry_changes << " geometry pixels, " << textured.second << " original / " << after.second << " Mega visible triangles\n";
    }
    expect(total_geometry_pixels > 500 && total_added_triangles > 32, "Bridge GPU comparisons must show genuine new geometry independently of artwork changes");
    std::cout << "PASS: Donington bridge original placement/passage, five accepted artworks, continuous N and Original/artwork-only/full-Mega GPU comparisons\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (!(argc == 3 || (argc == 4 && std::string_view(argv[3]) == "--cleanup"))) {
            throw std::runtime_error("Usage: toca-scene-model-bridge-tests CANONICAL_ROOT MEGA_ROOT [--cleanup]");
        }
        bridge_reference(argv[1], argv[2], argc == 4);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
