#include "assets/store.hpp"
#include "core/crc32.hpp"
#include "image/decode.hpp"
#include "scene/finish_camera.hpp"
#include "scene/flag_animation.hpp"
#include "scene/view.hpp"
#include "world_geometry.hpp"

#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace {
using namespace toca;
using namespace toca::scene;
constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr unsigned width = 640, height = 480;

struct Member {
    std::size_t node, mesh;
    float distance;
    std::array<float, 3> route, surface;
};

struct Probe {
    std::uint32_t track;
    std::string id;
    unsigned kind;
    std::size_t camera;
    std::vector<Member> members;
};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::array<float, 29>> triangle_attributes(const ModelMesh& mesh) {
    // GLB compaction changes index/storage order, not exact triangle attributes.
    std::vector<std::array<float, 29>> result;
    for (const auto& face : mesh.faces) {
        std::array<float, 29> triangle{};
        unsigned cursor = 0;
        for (const auto index : face.indices) {
            const auto& vertex = mesh.vertices[index];
            for (const auto value : vertex.position) {
                triangle[cursor++] = value;
            }
            for (const auto value : vertex.normal) {
                triangle[cursor++] = value;
            }
            triangle[cursor++] = vertex.u;
            triangle[cursor++] = vertex.v;
        }
        triangle[cursor++] = float(face.texture);
        for (const auto color : face.color) {
            triangle[cursor++] = float(color);
        }
        result.push_back(triangle);
    }
    std::ranges::sort(result);
    return result;
}

bool same_mesh(const ModelMesh& a, const ModelMesh& b) {
    return a.name == b.name && a.faces.size() == b.faces.size() && triangle_attributes(a) == triangle_attributes(b);
}

std::uint32_t geometry_crc32(const ModelMesh& mesh) {
    std::vector<std::uint8_t> bytes;
    for (const auto& triangle : triangle_attributes(mesh)) {
        for (const auto value : triangle) {
            // Canonical little-endian binary32; numeric equality treats signed
            // zero alike, so normalize it before computing the stable CRC32.
            const auto bits = std::bit_cast<std::uint32_t>(value == 0.f ? 0.f : value);
            for (unsigned shift = 0; shift < 32; shift += 8) {
                bytes.push_back(std::uint8_t(bits >> shift));
            }
        }
    }
    return crc32(bytes);
}

std::string checksum(std::span<const std::uint8_t> bytes) {
    std::ostringstream s;
    s << std::hex << std::setw(8) << std::setfill('0') << crc32(bytes);
    return s.str();
}

std::size_t chunk_index(const SceneResources& scene, const Member& member) {
    const auto& chunks = scene.trees_baked.chunks;
    const auto p = std::ranges::find(chunks, member.node, &TerrainChunk::node_index);
    expect(p != chunks.end() && p->mesh.name == scene.trees.meshes.at(member.mesh).name, "Audit selection does not match canonical baked woodland binding");
    return std::size_t(p - chunks.begin());
}

std::array<float, 3> center(const ModelMesh& mesh) {
    std::array<float, 3> lo{32768, 32768, 32768}, hi{-32768, -32768, -32768};
    for (const auto& v : mesh.vertices) {
        for (unsigned a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], v.position[a]);
            hi[a] = std::max(hi[a], v.position[a]);
        }
    }
    return {(lo[0] + hi[0]) * .5f, (lo[1] + hi[1]) * .5f, (lo[2] + hi[2]) * .5f};
}

CameraInputs camera(const SceneResources& original, const Probe& probe, bool fallback = false) {
    const auto& member = probe.members.front();
    const auto& mesh = original.trees_baked.chunks[chunk_index(original, member)].mesh;
    auto target = center(mesh);
    std::array<SceneCamera, 1> origins{};
    if (probe.kind == 0 && !fallback) {
        expect(probe.camera < original.cameras.size(), "Dense overview needs actual authored camera");
        origins[0] = original.cameras[probe.camera];
    } else {
        target[0] = member.surface[0];
        target[2] = member.surface[2];
        // Aim at the actual carrier near the route witness; keep the camera at
        // source road height plus eye height, rather than moving the forest.
        for (unsigned a = 0; a < 3; ++a) {
            origins[0].transform[12 + a] = std::int32_t((member.route[a] + (a == 1 ? 1.2f : 0)) * 65536);
        }
    }
    std::array<std::int32_t, 3> q{};
    for (unsigned a = 0; a < 3; ++a) {
        q[a] = std::int32_t(target[a] * 65536);
    }
    const FinishCamera look(origins);
    return {look.world_to_view(q), default_projection(4, width, height), look.position()};
}

std::vector<std::uint32_t> render(gpu::Renderer& renderer, const SceneResources& scene, const CameraInputs& camera, const std::string& file = {}) {
    SceneView view(renderer, scene);
    SourceRandom random(7);
    FlagAnimation flags(scene.flags, random);
    HorizonActors actors(scene.moving_horizon);
    auto frame = view.frame(camera, scene_lighting(scene.setup), {unit}, flags.variants(), actors.frame());
    expect(!toca::test::materialize_world(frame).indices.empty(), "Overview scene must reach actual GPU geometry");
    expect(renderer.draw(frame, false), "Overview GPU target unavailable");
    if (!file.empty()) {
        renderer.save_screenshot(file);
    }
    return renderer.readback();
}

// Fixed visible source bindings from the native overview, including exact
// single-member route witnesses for the three Oulton density findings.
std::vector<Probe> selections() {
    return {{0, "donington-gp", 0, 3, {{74, 26, 59.1879f, {-675.1879f, 2.9680f, 79.4951f}, {-729.3784f, 3.0531f, 103.2984f}}}},
        {1, "silverstone-12", 1, 5, {{75, 12, 35.6080f, {371.6380f, 59.2359f, 337.8621f}, {405.6248f, 59.3397f, 348.4839f}}}},
        {2, "thruxton-47", 0, 12, {{429, 47, 46.6525f, {8.0998f, -11.0660f, -305.6192f}, {20.4402f, -11.0671f, -350.6099f}}}},
        {3, "brands-37", 1, 8, {{114, 37, 31.2533f, {182.3453f, -1.0923f, -43.2744f}, {151.1759f, .7934f, -44.5717f}}}},
        {4, "oulton-111", 1, 1, {{278, 111, 8.4842f, {121.3777f, 18.8330f, -285.7503f}, {123.6967f, 19.2755f, -293.8994f}}}},
        {4, "oulton-98", 1, 3, {{252, 98, 14.1699f, {-3.7162f, 18.0076f, -233.2773f}, {-16.0796f, 18.0076f, -240.2008f}}}},
        {4, "oulton-9", 1, 26, {{43, 9, 15.3768f, {431.0363f, 22.2249f, 290.7185f}, {428.8628f, 22.2249f, 305.9410f}}}},
        {5, "donington-short", 0, 3, {{74, 26, 59.1879f, {-675.1879f, 2.9680f, 79.4951f}, {-729.3784f, 3.0531f, 103.2984f}}}}};
}

// Keep the actual native alpha interpretation and source UV interpolation,
// while making RGB/lighting irrelevant to the diagnostic occupancy readback.
// This never changes an authored asset or the context screenshots.
gpu::Texture alpha_texture(gpu::Renderer& renderer, const ModelTexture& texture, bool opaque) {
    auto decoded = toca::decode_model_texture(texture);
    toca::Surface white(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    expect(bool(white), "Convert diagnostic alpha surface");
    for (int y = 0; y < white->h; ++y) {
        auto* row = static_cast<Uint8*>(white->pixels) + y * white->pitch;
        for (int x = 0; x < white->w; ++x) {
            auto* pixel = row + x * 4;
            const auto alpha = opaque ? Uint8(255) : texture.encoding == ImageEncoding::bmp ? Uint8((pixel[0] | pixel[1] | pixel[2]) == 0 ? 0 : 255) : pixel[3];
            pixel[0] = pixel[1] = pixel[2] = 255;
            pixel[3] = alpha;
        }
    }
    return renderer.upload(white.get(), false);
}

using Mask = std::vector<bool>;

struct AlphaPass {
    Mask mask;
    std::vector<std::uint32_t> pixels;
};

struct AlphaOptions {
    bool opaque = false, clamp = false;
};

AlphaPass alpha_pass(gpu::Renderer& renderer, const Terrain& terrain, std::size_t chunk, const CameraInputs& cam, const std::vector<std::uint32_t>& empty, const std::string& file,
    AlphaOptions options = {}) {
    std::vector<gpu::Texture> textures;
    for (const auto& texture : terrain.textures) {
        textures.push_back(alpha_texture(renderer, texture, options.opaque));
    }
    gpu::Frame frame;
    for (const auto& face : terrain.chunks.at(chunk).mesh.faces) {
        expect(face.texture >= 0, "Density selection must use an actual cutout material");
        std::array<LitVertex, 3> triangle;
        for (unsigned c = 0; c < 3; ++c) {
            const auto& v = terrain.chunks.at(chunk).mesh.vertices.at(face.indices[c]);
            triangle[c] = {v.position, 0xffffffffu, v.u, v.v};
        }
        append_projected_triangle(frame,
            {.texture = textures.at(face.texture).get(),
                .depth = true,
                .key = true,
                // Match SceneView: source BMPs use nearest sampling, authored
                // WebPs use linear cutouts. Filtering the BMP here invents a
                // wrap seam between its opaque bottom and transparent top.
                .linear = terrain.textures.at(face.texture).encoding == ImageEncoding::webp,
                .clamp = options.clamp,
                .alpha_cutoff = terrain.textures.at(face.texture).encoding == ImageEncoding::webp ? .5f : 0.f},
            triangle, cam);
    }
    expect(!frame.indices.empty(), "Selected density carrier must reach native projection");
    expect(renderer.draw(frame, false), "Density alpha pass GPU target unavailable");
    if (!file.empty()) {
        renderer.save_screenshot(file);
    }
    AlphaPass result{{}, renderer.readback()};
    expect(result.pixels.size() == empty.size(), "Density alpha readback extent differs");
    for (std::size_t i = 0; i < empty.size(); ++i) {
        result.mask.push_back(result.pixels[i] != empty[i]);
    }
    return result;
}

std::size_t count(const Mask& mask) {
    return std::ranges::count(mask, true);
}

struct Bounds {
    int x0 = int(width), y0 = int(height), x1 = -1, y1 = -1;
};

Bounds bounds(const Mask& mask) {
    Bounds b;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            if (mask[y * width + x]) {
                b.x0 = std::min(b.x0, int(x));
                b.x1 = std::max(b.x1, int(x));
                b.y0 = std::min(b.y0, int(y));
                b.y1 = std::max(b.y1, int(y));
            }
        }
    }
    return b;
}

void verify(const SceneResources& source, const SceneResources& mega, const std::filesystem::path& directory, std::map<std::string, std::vector<std::uint8_t>>& aliases,
    std::ofstream& material_csv, unsigned track) {
    expect(source.trees.nodes.size() == mega.trees.nodes.size() && source.trees.meshes.size() == mega.trees.meshes.size()
               && source.trees.textures.size() == mega.trees.textures.size(),
        "Density package must preserve source slots");
    for (std::size_t i = 0; i < source.trees.nodes.size(); ++i) {
        const auto& a = source.trees.nodes[i];
        const auto& b = mega.trees.nodes[i];
        expect(a.parent == b.parent && a.transform == b.transform && a.id_flags == b.id_flags && a.variants == b.variants, "Density package changed source node");
    }
    std::map<std::size_t, std::uint32_t> pilots;
    if (track == 4) {
        pilots[14] = 0xbff44486u;
        pilots[15] = 0x7fddda90u;
        pilots[100] = 0x76c0c3f7u;
        pilots[101] = 0x1178153eu;
    }
    if (track == 6) {
        pilots[23] = 0xaef22678u;
        pilots[24] = 0x0e84d71eu;
    }
    if (track == 8) {
        pilots[0] = 0x5d40f7b0u;
        pilots[19] = 0x5d3853a8u;
    }
    for (std::size_t i = 0; i < source.trees.meshes.size(); ++i) {
        if (pilots.contains(i)) {
            expect(geometry_crc32(mega.trees.meshes[i]) == pilots.at(i), "Accepted geometry pilot changed during density work");
        } else {
            expect(same_mesh(source.trees.meshes[i], mega.trees.meshes[i]), "Density artwork must not change any source geometry");
        }
    }
    expect(source.trees_baked.chunks.size() == mega.trees_baked.chunks.size(), "Density package changed baked count");
    for (std::size_t i = 0; i < source.trees_baked.chunks.size(); ++i) {
        const auto& a = source.trees_baked.chunks[i];
        const auto& b = mega.trees_baked.chunks[i];
        expect(a.node_index == b.node_index && a.variant_index == b.variant_index && a.origin_xz == b.origin_xz && a.mesh.name == b.mesh.name,
            "Density package changed baked registration");
        if (!pilots.contains(source.trees.nodes.at(a.node_index).variants.at(a.variant_index))) {
            expect(same_mesh(a.mesh, b.mesh), "Density package changed unconverted baked carrier");
        }
    }
    for (std::size_t i = 0; i < source.trees.textures.size(); ++i) {
        const auto& a = source.trees.textures[i];
        const auto& b = mega.trees.textures[i];
        expect(a.name == b.name && b.encoding == ImageEncoding::webp, "Density material slot/encoding differs");
        const bool shared = a.name == "LLRTS1" || (a.name == "LLRTS2" && checksum(a.bmp) != "df4e2739");
        if (!shared) {
            continue; // SAP1 and accepted Snet-specific pilot materials remain separate.
        }
        std::ifstream f(directory / "scene" / (checksum(a.bmp) + ".webp"), std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        expect(!bytes.empty() && bytes == b.bmp, "Density bundle must use exact source-CRC override");
        const auto identity = checksum(a.bmp);
        // Matching names can have different canonical palettes and accepted
        // track-specific artwork. Share only the exact source bitmap identity.
        if (aliases.contains(identity)) {
            expect(aliases.at(identity) == bytes, "Identical woodland source pixels must share the exact authored material across consumers");
        } else {
            aliases[identity] = bytes;
        }
        const auto image = toca::decode_model_texture(b);
        expect(image->w <= 512 && image->h <= 512 && bytes.size() <= 256 * 1024, "Density material exceeds texture budget");
        material_csv << track << ',' << i << ',' << a.name << ',' << checksum(a.bmp) << ',' << checksum(bytes) << ',' << image->w << ',' << image->h << ',' << bytes.size() << '\n';
    }
}

struct Result {
    std::vector<std::uint32_t> source, mega;
    bool passed;
};

Result probe(gpu::Renderer& renderer, const SceneResources& source, const SceneResources& mega, const Probe& p, const CameraInputs& cam, const std::vector<std::uint32_t>& empty,
    std::ofstream& csv) {
    const auto& member = p.members.front();
    const auto index = chunk_index(source, member);
    const auto prefix = "woodland-density-" + p.id;
    auto context = mega;
    context.trees = source.trees;
    context.trees_baked = source.trees_baked;
    const auto original = render(renderer, context, cam, prefix + "-original.png");
    const auto full = render(renderer, mega, cam, prefix + "-mega.png");
    auto absent = mega;
    absent.trees.nodes.at(member.node).variants.clear();
    std::erase_if(absent.trees_baked.chunks, [&](const auto& c) {
        return c.node_index == member.node;
    });
    const auto without = render(renderer, absent, cam);
    std::size_t visible = 0;
    for (std::size_t i = 0; i < full.size(); ++i) {
        visible += full[i] != without[i];
    }
    const auto old = alpha_pass(renderer, source.trees_baked, index, cam, empty, prefix + "-alpha-original.png");
    const auto authored = alpha_pass(renderer, mega.trees_baked, index, cam, empty, prefix + "-alpha-mega.png");
    const auto carrier = alpha_pass(renderer, source.trees_baked, index, cam, empty, {}, {.opaque = true});
    std::size_t retained = 0, inside = 0, lost_inside = 0, added = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto i = y * width + x;
            retained += old.mask[i] && authored.mask[i];
            added += !old.mask[i] && authored.mask[i];
            bool inner = x >= 2 && x + 2 < width && y >= 2 && y + 2 < height;
            if (inner) {
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        inner = inner && old.mask[(int(y) + dy) * width + int(x) + dx];
                    }
                }
            }
            inside += inner;
            lost_inside += inner && !authored.mask[i];
        }
    }
    const auto n = count(old.mask), m = count(authored.mask), solid = count(carrier.mask);
    const auto a = bounds(old.mask), b = bounds(authored.mask), c = bounds(carrier.mask);
    const int dx = 2 + int(std::ceil((c.x1 - c.x0 + 1) * .03f)), dy = 2 + int(std::ceil((c.y1 - c.y0 + 1) * .03f));
    const bool silhouette = std::abs(a.x0 - b.x0) <= dx && std::abs(a.x1 - b.x1) <= dx && std::abs(a.y0 - b.y0) <= dy && std::abs(a.y1 - b.y1) <= dy;
    const bool retention = n > 0 && retained * 100 >= n * 90, interior = inside > 0 && lost_inside * 100 <= inside * 5;
    csv << p.id << ',' << p.track << ',' << member.node << ',' << member.mesh << ',' << member.distance << ',' << visible << ',' << n << ',' << m << ',' << solid << ',' << retained
        << ',' << inside << ',' << lost_inside << ',' << added << ',' << dx << ',' << dy << ',' << silhouette << ',' << retention << ',' << interior << '\n';
    csv.flush();
    std::cout << "DENSITY " << p.id << " visible=" << visible << " retained=" << retained << '/' << n << " interior_lost=" << lost_inside << '/' << inside
              << " silhouette=" << silhouette << '\n';
    expect(visible > 20 && n > 100 && inside > 100, "Density probe needs real context and isolated source visibility");
    // Capture all eight selected consumers before enforcing the aggregate gate,
    // so a failed candidate still gives a complete, focused diagnosis.
    return {old.pixels, authored.pixels, retention && interior && silhouette};
}

struct SharedSlot {
    ModelTexture source, authored;
    unsigned track, slot;
};

bool quad_probe(gpu::Renderer& renderer, const SharedSlot& slot, const std::vector<std::uint32_t>& empty, std::ofstream& csv) {
    const auto key = checksum(slot.source.bmp), prefix = "woodland-density-slot-" + key;
    Terrain source, mega;
    TerrainChunk chunk;
    chunk.mesh.name = "native-static-UV-quad";
    // cm3d_bake.hpp and scene/resources.cpp clamp static source/GLB UVs to
    // [.02, .99]. Inspect that same consumed domain for both material phases.
    chunk.mesh.vertices = {
        {{-8, -2, 12}, {0, 0, -1}, .02f, .99f}, {{8, -2, 12}, {0, 0, -1}, .99f, .99f}, {{8, 2, 12}, {0, 0, -1}, .99f, .02f}, {{-8, 2, 12}, {0, 0, -1}, .02f, .02f}};
    chunk.mesh.faces = {{{2, 1, 0}, 0, {255, 255, 255, 255}}, {{3, 2, 0}, 0, {255, 255, 255, 255}}};
    source.chunks = {chunk};
    source.textures = {slot.source};
    mega.chunks = {chunk};
    mega.textures = {slot.authored};
    const CameraInputs cam{unit, default_projection(4, width, height)};
    // Clamp this isolated material inspection to its atlas; actual scene
    // carriers retain their authored repeat policy in the probes above.
    const auto old = alpha_pass(renderer, source, 0, cam, empty, prefix + "-original.png", {.clamp = true});
    const auto authored = alpha_pass(renderer, mega, 0, cam, empty, prefix + "-mega.png", {.clamp = true});
    const auto carrier = alpha_pass(renderer, source, 0, cam, empty, {}, {.opaque = true, .clamp = true});
    std::size_t retained = 0, inside = 0, lost_inside = 0, added = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto i = y * width + x;
            retained += old.mask[i] && authored.mask[i];
            added += !old.mask[i] && authored.mask[i];
            bool inner = x >= 2 && x + 2 < width && y >= 2 && y + 2 < height;
            if (inner) {
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        inner = inner && old.mask[(int(y) + dy) * width + int(x) + dx];
                    }
                }
            }
            inside += inner;
            lost_inside += inner && !authored.mask[i];
        }
    }
    const auto n = count(old.mask), m = count(authored.mask), solid = count(carrier.mask);
    const auto a = bounds(old.mask), b = bounds(authored.mask), c = bounds(carrier.mask);
    const int dx = 2 + int(std::ceil((c.x1 - c.x0 + 1) * .03f)), dy = 2 + int(std::ceil((c.y1 - c.y0 + 1) * .03f));
    const bool silhouette = std::abs(a.x0 - b.x0) <= dx && std::abs(a.x1 - b.x1) <= dx && std::abs(a.y0 - b.y0) <= dy && std::abs(a.y1 - b.y1) <= dy;
    const bool retention = retained * 100 >= n * 90, interior = lost_inside * 100 <= inside * 5;
    csv << slot.track << ',' << slot.slot << ',' << slot.source.name << ',' << key << ',' << checksum(slot.authored.bmp) << ',' << n << ',' << m << ',' << solid << ',' << retained
        << ',' << inside << ',' << lost_inside << ',' << added << ',' << silhouette << ',' << retention << ',' << interior << '\n';
    csv.flush();
    std::cout << "SLOT " << key << " retained=" << retained << '/' << n << " interior_lost=" << lost_inside << '/' << inside << " silhouette=" << silhouette << '\n';
    expect(n > 100 && inside > 100, "Native static-UV quad must show substantial material coverage");
    return retention && interior && silhouette;
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 3, "Usage: toca-woodland-density-tests CANONICAL_ROOT MEGA_ROOT");
        gpu::check(SDL_Init(SDL_INIT_VIDEO), "Initialize isolated woodland density");

        struct Quit {
            ~Quit() {
                SDL_Quit();
            }
        } quit;

        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("TOCA woodland density", width, height, 0), SDL_DestroyWindow);
        gpu::check(bool(window), "Create density GPU window");
        gpu::Renderer renderer(window.get());
        gpu::Frame blank;
        // A valid offscreen draw submits the native clear without covering it.
        blank.quad({}, -8, -8, 1, 1);
        expect(renderer.draw(blank, false), "Density blank target unavailable");
        const auto empty = renderer.readback();
        expect(!empty.empty()
                   && std::ranges::all_of(empty,
                       [&](auto pixel) {
                           return pixel == empty.front();
                       }),
            "Density clear target must be uniform");
        const AssetStore store(argv[1]);
        const auto probes = selections();
        std::ofstream csv("woodland-density.csv"), materials("woodland-density-materials.csv"), slots_csv("woodland-density-slots.csv");
        slots_csv << "track,slot,name,source_bmp_crc32,output_crc32,source_alpha_pixels,mega_alpha_pixels,carrier_pixels,retained_pixels,source_interior_pixels,lost_interior_"
                     "pixels,added_pixels,silhouette_pass,retention_pass,interior_pass\n";
        csv << "probe,track,node,mesh,min_route_distance_m,context_visible_pixels,source_alpha_pixels,mega_alpha_pixels,carrier_pixels,retained_pixels,source_interior_pixels,lost_"
               "interior_pixels,added_pixels,bbox_tolerance_x,bbox_tolerance_y,silhouette_pass,retention_pass,interior_pass\n";
        materials << "track,slot,name,source_bmp_crc32,output_crc32,width,height,bytes\n";
        std::map<std::string, std::vector<std::uint8_t>> aliases;
        std::map<std::string, SharedSlot> shared_slots;
        bool all_passed = true;
        std::optional<CameraInputs> reuse_camera;
        std::optional<Result> gp;
        std::optional<Terrain> gp_terrain;
        for (unsigned track = 0; track < 9; ++track) {
            if (track == 7) {
                continue;
            }
            const auto original = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 2, .detail = 3}, 2));
            const auto mega = load_scene_resources(store, resolve_setup({.track_id = track, .weather_choice = 2, .detail = 4}, 2), argv[2]);
            verify(original, mega, argv[2], aliases, materials, track);
            for (unsigned i = 0; i < original.trees.textures.size(); ++i) {
                const auto& a = original.trees.textures[i];
                const auto key = checksum(a.bmp);
                if (a.name == "LLRTS1" || (a.name == "LLRTS2" && key != "df4e2739")) {
                    shared_slots.try_emplace(key, SharedSlot{a, mega.trees.textures[i], track, i});
                }
            }
            for (const auto& p : probes) {
                if (p.track == track) {
                    auto cam = camera(original, p);
                    if (track == 0) {
                        reuse_camera = cam;
                    }
                    if (track == 5) {
                        expect(bool(reuse_camera), "Missing GP density camera");
                        cam = *reuse_camera;
                    }
                    const auto result = probe(renderer, original, mega, p, cam, empty, csv);
                    all_passed = all_passed && result.passed;
                    if (track == 0) {
                        gp = result;
                        gp_terrain = mega.trees_baked;
                    }
                    if (track == 5) {
                        expect(gp && gp->source == result.source && gp->mega == result.mega, "Donington source and authored alpha passes must be identically reused");
                        expect(gp_terrain && gp_terrain->chunks.size() == mega.trees_baked.chunks.size(), "Donington density chunk count changed");
                        for (std::size_t i = 0; i < gp_terrain->chunks.size(); ++i) {
                            const auto& a = gp_terrain->chunks[i];
                            const auto& b = mega.trees_baked.chunks[i];
                            expect(a.node_index == b.node_index && a.variant_index == b.variant_index && a.origin_xz == b.origin_xz && same_mesh(a.mesh, b.mesh),
                                "Donington TREES geometry reuse differs");
                        }
                    }
                }
            }
        }
        const std::set<std::string> expected_slots{"d546b0c4", "075bd9c8", "b202f15b", "c1dee425", "c2bf9d3b", "bec80580", "6f02e8d1", "95df9a74", "7d89443a", "4bdfc08a"};
        expect(aliases.size() == expected_slots.size(), "All ten canonical woodland material identities must be exercised");
        std::set<std::string> actual_slots;
        for (const auto& [key, slot] : shared_slots) {
            actual_slots.insert(key);
            all_passed = quad_probe(renderer, slot, empty, slots_csv) && all_passed;
        }
        expect(actual_slots == expected_slots, "All10 distinct shared source BMP slots must be measured");
        expect(all_passed, "Density candidate violates source-relative retention/interior/silhouette gates; see complete CSV");
        std::cout << "PASS: eight native woodland density views,10 source-UV quads, both shared motifs, exact geometry/pilots/aliases and Donington TREES reuse; side/end geometry "
                     "remains separate\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
