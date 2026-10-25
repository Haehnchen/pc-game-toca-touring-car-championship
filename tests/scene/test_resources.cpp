#include "assets/store.hpp"
#include "core/crc32.hpp"
#include "formats/terrain.hpp"
#include "scene/barriers.hpp"
#include "scene/grid_lines.hpp"
#include "scene/resources.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, message);
}

void put16(Bytes& bytes, std::size_t position, std::uint16_t value) {
    bytes.at(position) = std::uint8_t(value);
    bytes.at(position + 1) = std::uint8_t(value >> 8);
}

void put32(Bytes& bytes, std::size_t position, std::uint32_t value) {
    put16(bytes, position, std::uint16_t(value));
    put16(bytes, position + 2, std::uint16_t(value >> 16));
}

void identity(Bytes& bytes, std::size_t position) {
    for (std::size_t index = 0; index < 4; ++index) {
        put32(bytes, position + index * 20, 65536);
    }
}

Bytes terrain() {
    constexpr std::size_t mesh = 196, vertices = mesh + 48, faces = vertices + 3 * 32, end = faces + 24;
    Bytes bytes(end);
    std::memcpy(bytes.data(), "CM3D", 4);
    put32(bytes, 4, 100);
    put32(bytes, 8, 32);
    put16(bytes, 12, 2);
    put16(bytes, 14, 1);
    put32(bytes, 20, 32);
    put32(bytes, 24, mesh);
    put32(bytes, 28, end);
    put32(bytes, 32, 80);
    put16(bytes, 36, 0xffff);
    identity(bytes, 40);
    put32(bytes, 40 + 12 * 4, 1000 * 65536); // Must not become a parent transform.
    put32(bytes, 112, 84);
    identity(bytes, 120);
    put32(bytes, 120, 32768);
    put32(bytes, 120 + 12 * 4, 2 * 65536);
    put32(bytes, 120 + 13 * 4, 3 * 65536);
    put32(bytes, 120 + 14 * 4, 4 * 65536);
    bytes[112 + 76] = 1;
    put32(bytes, mesh + 12, end - mesh);
    put16(bytes, mesh + 16, 3);
    put16(bytes, mesh + 18, 1);
    const std::array<std::array<std::int32_t, 5>, 3> coordinates{{{-65535, 0, 0, 229376, -98304}, {0, 65536, 0, 98304, -32768}, {0, 0, 65536, -32768, 0}}};
    for (std::size_t index = 0; index < coordinates.size(); ++index) {
        for (std::size_t field = 0; field < 5; ++field) {
            put32(bytes, vertices + index * 32 + field * 4, std::bit_cast<std::uint32_t>(coordinates[index][field]));
        }
        put32(bytes, vertices + index * 32 + 20, std::bit_cast<std::uint32_t>(std::int32_t(-65535)));
        put32(bytes, vertices + index * 32 + 28, 65536);
    }
    put16(bytes, faces + 4, 1);
    put16(bytes, faces + 6, 2);
    put16(bytes, faces + 8, 0xffff);
    bytes[faces + 13] = 255;
    bytes[faces + 23] = 0x80;
    return bytes;
}

Bytes mega_scene_model(std::uint32_t flags) {
    std::string json = R"({"asset":{"version":"2.0"},"extensionsUsed":["EXT_texture_webp"],"extensionsRequired":["EXT_texture_webp"],
"buffers":[{"byteLength":138}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96,"byteStride":32},{"buffer":0,"byteOffset":96,"byteLength":6},{"buffer":0,"byteOffset":104,"byteLength":34}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[2,0,2]},{"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
"images":[{"name":"MegaSurface","mimeType":"image/webp","bufferView":2}],"textures":[{"extensions":{"EXT_texture_webp":{"source":0}}}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"baseColorTexture":{"index":0}}}],
"meshes":[{"name":"prop","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
"nodes":[{"matrix":[2,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1],"children":[1],"extras":{"toca_id_flags":)";
    json +=
        std::to_string(flags)
        + R"(,"toca_variants":[]}},{"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,1000,0,0,1],"mesh":0,"extras":{"toca_id_flags":1,"toca_variants":[0]}}],"scenes":[{"nodes":[0]}],"scene":0})";
    json.append((4 - json.size() % 4) % 4, ' ');
    Bytes bytes(12 + 8 + json.size() + 8 + 140);
    put32(bytes, 0, 0x46546c67);
    put32(bytes, 4, 2);
    put32(bytes, 8, std::uint32_t(bytes.size()));
    put32(bytes, 12, std::uint32_t(json.size()));
    put32(bytes, 16, 0x4e4f534a);
    std::memcpy(bytes.data() + 20, json.data(), json.size());
    const auto offset = 20 + json.size();
    put32(bytes, offset, 140);
    put32(bytes, offset + 4, 0x004e4942);
    constexpr std::array<float, 24> vertices{0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 1, 0, 0, 0, 2, 0, 1, 0, 0, 1};
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        put32(bytes, offset + 8 + index * 4, std::bit_cast<std::uint32_t>(vertices[index]));
    }
    put16(bytes, offset + 8 + 98, 1);
    put16(bytes, offset + 8 + 100, 2);
    const Bytes image{0x52, 0x49, 0x46, 0x46, 0x1a, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x4c, 0x0e, 0x00, 0x00, 0x00, 0x2f, 0x01, 0x00, 0x00, 0x10, 0x0f,
        0x10, 0x11, 0xf3, 0x1f, 0x22, 0x23, 0xfa, 0x1f};
    std::copy(image.begin(), image.end(), bytes.begin() + std::ptrdiff_t(offset + 8 + 104));
    return bytes;
}

void mega_resources() {
    const auto directory = std::filesystem::current_path() / "scene-mega-fixture";
    expect(std::filesystem::create_directory(directory), "Synthetic Mega directory already exists");

    struct Cleanup {
        std::filesystem::path directory;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};

    const auto write = [](const std::filesystem::path& path, const Bytes& bytes) {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        expect(bool(output), "Cannot write synthetic Mega resource");
    };
    std::filesystem::create_directories(directory / "scene/track-0");
    const auto generated = mega_scene_model(0x20000000);
    const Bytes image(generated.end() - 36, generated.end() - 2);
    const Bytes canonical_texture{1, 2, 3};
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(8) << toca::crc32(canonical_texture);
    write(directory / "scene" / (name.str() + ".webp"), image);
    write(directory / "scene/track-0/objects.glb", generated);
    write(directory / "scene/track-0/signs.glb", mega_scene_model(0));

    Bytes palette(1024), partial(1078);
    for (std::size_t index = 0; index < 256; ++index) {
        std::fill_n(palette.begin() + std::ptrdiff_t(index * 4), 3, std::uint8_t(index));
    }
    partial[0] = 'B';
    partial[1] = 'M';
    put32(partial, 2, std::uint32_t(partial.size()));
    put32(partial, 10, 1074);
    put32(partial, 14, 40);
    put32(partial, 18, 2);
    put32(partial, 22, 1);
    put16(partial, 26, 1);
    put16(partial, 28, 8);
    put32(partial, 34, 4);
    put32(partial, 46, 255);
    std::copy_n(palette.begin(), 1020, partial.begin() + 54);
    partial[1074] = 255;
    partial[1075] = 17;
    Bytes normalized(partial.begin(), partial.begin() + 54);
    normalized.insert(normalized.end(), palette.begin(), palette.end());
    normalized.insert(normalized.end(), partial.begin() + 1074, partial.end());
    put32(normalized, 2, std::uint32_t(normalized.size()));
    put32(normalized, 10, 1078);
    put32(normalized, 46, 256);
    std::ostringstream horizon_key;
    horizon_key << std::hex << std::setfill('0') << std::setw(8) << toca::crc32(normalized);
    write(directory / "scene" / (horizon_key.str() + ".webp"), image);

    toca::scene::SceneResources resources;
    resources.setup = toca::scene::resolve_setup({.detail = 4});
    resources.ground.textures.push_back({"same-name", canonical_texture});
    resources.ground.sector_centers.push_back({7, 11});
    resources.fence.textures = resources.ground.textures;
    resources.fence_baked.textures = resources.ground.textures;
    resources.trees.textures.push_back({"same-name", {9, 9, 9}});
    resources.moving_horizon.texture_palette = palette;
    resources.moving_horizon.moving.textures.push_back({"partial-bank", partial});
    resources.moving_horizon.rotorcraft.textures = resources.moving_horizon.moving.textures;
    auto unmapped = partial;
    unmapped[1075] = 18;
    resources.moving_horizon.moving.textures.push_back({"unmapped-partial", unmapped});
    resources.objects.nodes = {{-1, {2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 2, 3, 4, 1}, 0x20000000, {}}, {0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1000, 0, 0, 1}, 1, {0}}};
    resources.objects.meshes.push_back({"prop", {}, {}});
    resources.objects_baked.chunks.push_back({.node_index = 1, .variant_index = 0, .origin_xz = {2 * 65536, 4 * 65536}, .mesh = {}, .face_flags = {}});
    resources.signs.model = resources.objects;
    resources.signs.model.nodes[0].id_flags = 0;
    resources.signs.actors.push_back({0, 1});
    const auto original = resources;
    toca::scene::apply_mega_scene_resources(resources, directory);
    expect(resources.ground.textures[0].bmp == image && resources.ground.textures[0].encoding == toca::ImageEncoding::webp && resources.fence.textures[0].bmp == image
               && resources.fence_baked.textures[0].bmp == image && resources.trees.textures[0].encoding == toca::ImageEncoding::bmp
               && resources.trees.textures[0].bmp == Bytes({9, 9, 9}),
        "CRC32 Mega textures must reach terrain and both scenery consumers without confusing identical resource names");
    expect(resources.moving_horizon.moving.textures[0].bmp == image && resources.moving_horizon.moving.textures[0].encoding == toca::ImageEncoding::webp
               && resources.moving_horizon.rotorcraft.textures[0].bmp == image && resources.moving_horizon.rotorcraft.textures[0].encoding == toca::ImageEncoding::webp
               && resources.moving_horizon.moving.textures[1].bmp == unmapped && resources.moving_horizon.texture_palette == palette,
        "HOR overrides must use the completed BMP CRC32 for both actor families while retaining unmapped original bytes and the shared palette");
    expect(resources.objects.meshes[0].vertices[1].position[0] == 2 && resources.objects.textures.size() == 1 && resources.objects.textures[0].name == "MegaSurface"
               && resources.objects.nodes[1].transform == original.objects.nodes[1].transform,
        "Mega scenery must install regenerated geometry and appended WebP materials while retaining source hierarchy");
    const auto& baked = resources.objects_baked.chunks.at(0);
    expect(baked.node_index == 1 && baked.variant_index == 0 && baked.origin_xz == original.objects_baked.chunks[0].origin_xz
               && baked.mesh.vertices[1].position == std::array<float, 3>{6, 3, 4} && baked.mesh.vertices[1].normal == std::array<float, 3>{0, 1, 0}
               && baked.mesh.vertices[0].u == .02f && baked.mesh.vertices[1].u == .99f && baked.face_flags == Bytes{0} && resources.objects_baked.textures[0].bmp == image,
        "Generated static scenery must replace its baked mesh using the original root placement and sector identity");
    expect(resources.signs.model.meshes[0].vertices[1].position[0] == 2 && resources.signs.actors[0].root_node == 0 && resources.signs.actors[0].animation_node == 1
               && resources.ground.sector_centers == original.ground.sector_centers,
        "Generated signs must retain actor bindings and the complete original physics/sector layout");

    auto populated = original;
    auto& source_bake = populated.objects_baked;
    source_bake.sector_centers = {{7, 11}, {-13, 17}};
    source_bake.textures = {{"discarded", {42}}};
    source_bake.chunks[0].mesh = {"discarded", {{{-100, -200, -300}, {1, 0, 0}, -.5f, 1.5f}}, {}, {{{9, 8, 7}}}};
    source_bake.chunks[0].face_flags = {0x80, 0x01};
    source_bake.chunks.push_back(source_bake.chunks[0]);
    source_bake.chunks[1].origin_xz = {-19, 23};
    toca::scene::apply_mega_scene_resources(populated, directory);
    expect(source_bake.sector_centers == std::vector<std::array<std::int32_t, 2>>{{7, 11}, {-13, 17}} && source_bake.chunks.size() == 2
               && source_bake.chunks[0].origin_xz == original.objects_baked.chunks[0].origin_xz && source_bake.chunks[1].origin_xz == std::array<std::int32_t, 2>{-19, 23}
               && source_bake.textures.size() == 1 && source_bake.textures[0].name == "MegaSurface" && source_bake.textures[0].bmp == image,
        "Fresh static replacement must retain complete sector/chunk metadata while replacing discarded source textures");
    for (const auto& chunk : source_bake.chunks) {
        expect(chunk.node_index == 1 && chunk.variant_index == 0 && chunk.mesh.name == "prop" && chunk.mesh.vertices.size() == 3 && chunk.mesh.faces.size() == 1
                   && chunk.mesh.vertices[1].position == std::array<float, 3>{6, 3, 4} && chunk.mesh.ghost_positions.empty() && chunk.face_flags == Bytes{0},
            "Fresh static replacement must preserve chunk order and use only replacement geometry and flags");
    }

    auto prefixed = original;
    prefixed.objects.textures.push_back({"MegaSurface", canonical_texture});
    toca::scene::apply_mega_scene_resources(prefixed, directory);
    expect(prefixed.objects.meshes[0].vertices.size() == 3 && prefixed.objects.textures[0].name == "MegaSurface" && prefixed.objects.textures[0].bmp == image,
        "A retained original texture prefix must permit Mega geometry and its shared artwork override");

    const auto reject_model = [&](const auto& mutate, const char* message) {
        auto incompatible = original;
        mutate(incompatible.objects);
        toca::scene::apply_mega_scene_resources(incompatible, directory);
        expect(incompatible.objects.meshes[0].vertices.empty() && incompatible.objects_baked.chunks[0].mesh.vertices.empty(), message);
    };
    reject_model(
        [](toca::Model& model) {
            model.nodes.push_back(model.nodes.back());
        },
        "A replacement with missing source nodes must retain original scenery and baked geometry");
    reject_model(
        [](toca::Model& model) {
            model.meshes.push_back({"other", {}, {}});
        },
        "A replacement with missing source meshes must retain original scenery and baked geometry");
    reject_model(
        [](toca::Model& model) {
            model.nodes[1].parent = -1;
        },
        "A replacement with changed parent bindings must retain original scenery and baked geometry");
    reject_model(
        [](toca::Model& model) {
            model.nodes[1].id_flags = 2;
        },
        "A replacement with changed node IDs must retain original scenery and baked geometry");
    reject_model(
        [](toca::Model& model) {
            model.nodes[1].variants.clear();
        },
        "A replacement with changed variants must retain original scenery and baked geometry");
    reject_model(
        [](toca::Model& model) {
            model.nodes[1].transform[14] += .01f;
        },
        "Scenery must reject even small pivot changes permitted only for vehicle wheels");
    reject_model(
        [](toca::Model& model) {
            model.meshes[0].name = "other";
        },
        "A replacement with changed mesh names must retain original scenery and baked geometry");
    reject_model(
        [&](toca::Model& model) {
            model.textures.push_back({"original", canonical_texture});
        },
        "A replacement with changed texture names must retain original scenery and baked geometry");
    reject_model(
        [&](toca::Model& model) {
            model.textures = {{"MegaSurface", canonical_texture}, {"other", canonical_texture}};
        },
        "A replacement must retain every original texture slot before appending new materials");
    auto high = original;
    high.setup.selection.detail = 3;
    toca::scene::apply_mega_scene_resources(high, directory);
    expect(high.ground.textures[0].encoding == toca::ImageEncoding::bmp && high.objects.meshes[0].vertices.empty(), "Very High must retain original scene assets");
}

void grid_lines() {
    using namespace toca::scene;
    constexpr std::int32_t q = 65536;
    SceneResources resources;
    resources.setup = resolve_setup({.weather_choice = 1});
    auto& hpc = resources.collision_hpc;
    hpc.resize(84);
    put32(hpc, 0, std::uint32_t(-128 * q));
    put32(hpc, 4, std::uint32_t(-128 * q));
    constexpr std::array<std::uint16_t, 10> cells{1, 0, 0, 0, 0, 0, 1, 0, 3, 0};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        put16(hpc, 8 + i * 2, cells[i]);
    }
    constexpr std::array<std::array<std::int32_t, 3>, 3> plane{{{-16 * q, 2 * q, -16 * q}, {-16 * q, 2 * q, 16 * q}, {16 * q, 2 * q, -16 * q}}};
    for (std::size_t i = 0; i < plane.size(); ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put32(hpc, 28 + i * 12 + axis * 4, std::uint32_t(plane[i][axis]));
        }
    }
    constexpr std::array<std::uint16_t, 10> face{1, 0, 0, 1, 2, 0x83, 1, 0, 1, 1};
    for (std::size_t i = 0; i < face.size(); ++i) {
        put16(hpc, 64 + i * 2, face[i]);
    }
    for (std::size_t i = 0; i < 16; ++i) {
        resources.grid[i] = {{{std::int32_t(20 + i) * q, 0}}, {{std::int32_t(20 + i) * q, q}}};
    }
    resources.route_points.resize(68);
    put32(resources.route_points, 0, 1);
    constexpr std::array<std::int32_t, 3> start{-3 * q, 4 * q, 5 * q}, end{6 * q, 7 * q, 8 * q};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        put32(resources.route_points, 4 + axis * 4, std::uint32_t(start[axis]));
        put32(resources.route_points, 40 + axis * 4, std::uint32_t(end[axis]));
    }
    const auto geometry = prepare_grid_lines(resources);
    expect(geometry.size() == 17 && geometry.front().from == std::array<std::int32_t, 3>{20 * q, 2 * q, 0} && geometry.front().to == std::array<std::int32_t, 3>{20 * q, 2 * q, q}
               && geometry.back().from == start && geometry.back().to == end,
        "44ab90 must retain cached-face0 SGRID support and the authored POINTS[0] endpoint heights/order");
    const auto near = queued_grid_lines(std::span(geometry).first(1), resources.setup, {0, 0, 0});
    expect(near.size() == 1 && near[0].color == 0xffaaaaaa, "443030 near DONL grid line must be opaque RGB170");
    const std::array<QueuedWorldLine, 3> limits{{{{5 * q, 0, 0}, {6 * q, 0, 0}}, {{80 * q, 0, 80 * q}, {81 * q, 0, 80 * q}}, {{91 * q, 0, 0}, {92 * q, 0, 0}}}};
    const auto clipped = queued_grid_lines(limits, resources.setup, {0, 0, 0});
    expect(clipped.size() == 1 && clipped[0].from == limits[2].from && clipped[0].color == 0xff181818,
        "44c5e0 must hide near/diagonal out-of-range endpoints while retaining the far-color component-box branch");
    resources.setup.weather = 3;
    expect(queued_grid_lines(std::span(geometry).first(1), resources.setup, {0, 0, 0})[0].color == 0xff878787, "44ab90 rain gain must truncate170*0xcccc before packing RGB135");
    resources.setup.selection.track_id = 9;
    expect(prepare_grid_lines(resources).empty() && queued_grid_lines(geometry, resources.setup, {0, 0, 0}).empty(), "Lavaland must omit the original queued grid/start lines");
}

void synthetic() {
    grid_lines();
    mega_resources();
    const auto bytes = terrain();
    const auto ground = toca::parse_terrain(bytes);
    expect(ground.chunks.size() == 1 && ground.chunks[0].node_index == 1, "Terrain did not retain its source node/variant identity");
    expect(ground.sector_centers == std::vector<std::array<std::int32_t, 2>>{{2 * 65536, 4 * 65536}}, "Sector centers must exclude the root and retain raw node translations");
    const auto& mesh = ground.chunks[0].mesh;
    expect(mesh.vertices[0].position == std::array<float, 3>{1.5f, 3, 4}, "Terrain must use direct placement and signed per-product fixed shifts");
    expect(mesh.vertices[0].normal == std::array<float, 3>{-.5f, 0, 1}, "Terrain normal transform must omit translation");
    expect(mesh.vertices[0].u == .99f && mesh.vertices[0].v == .02f && mesh.vertices[1].u == .5f && mesh.vertices[1].v == .5f && mesh.vertices[2].u == .02f
               && mesh.vertices[2].v == .99f,
        "Terrain must fold original U once, offset original V and clamp both");
    expect(ground.chunks[0].origin_xz == std::array<std::int32_t, 2>{2 * 65536, 4 * 65536} && ground.chunks[0].face_flags == std::vector<std::uint8_t>{0x80},
        "Terrain origin or source face flags were lost");
    rejects(
        [&] {
            (void)toca::parse_terrain(std::span(bytes).first(bytes.size() - 1));
        },
        "Truncated terrain was accepted");
    auto shared = bytes;
    shared.insert(shared.begin() + 196, 84, 0);
    put16(shared, 12, 3);
    put32(shared, 24, 280);
    put32(shared, 28, std::uint32_t(shared.size()));
    put32(shared, 196, 84);
    identity(shared, 204);
    put32(shared, 204, 32768);
    put32(shared, 204 + 12 * 4, 5 * 65536);
    shared[196 + 76] = 1;
    const auto repeated = toca::parse_terrain(shared);
    expect(repeated.chunks.size() == 2 && repeated.chunks[1].mesh.vertices[0].normal[0] == -.25f && repeated.chunks[1].mesh.vertices[0].position[0] == 4.5f,
        "Shared terrain meshes must mutate fixed normals cumulatively while retaining original positions");
    shared[196 + 76] = 0;
    put32(shared, 196, 80);
    shared.erase(shared.begin() + 276, shared.begin() + 280);
    put32(shared, 24, 276);
    put32(shared, 28, std::uint32_t(shared.size()));
    put32(shared, 204 + 12 * 4, std::bit_cast<std::uint32_t>(-5 * 65536));
    const auto empty_sector = toca::parse_terrain(shared);
    expect(empty_sector.chunks.size() == 1 && empty_sector.sector_centers == std::vector<std::array<std::int32_t, 2>>{{2 * 65536, 4 * 65536}, {-5 * 65536, 0}},
        "Empty terrain nodes must retain their signed sector centers in file order");

    Bytes grid(640);
    put32(grid, 0, 0xffff0000);
    put32(grid, 4, 2 * 65536);
    put32(grid, 8, 3 * 65536);
    put32(grid, 12, 0xfffc0000);
    put16(grid, 16, 2139);
    put16(grid, 18, 0x7e21);
    put32(grid, 31 * 20, 5 * 65536);
    const auto slots = toca::scene::decode_start_grid(grid);
    expect(slots[0].first_xz == std::array<std::int32_t, 2>{-65536, 2 * 65536} && slots[0].second_xz[1] == -4 * 65536 && slots[0].heading == 2139 && slots[0].reserved == 0x7e21
               && slots[31].first_xz[0] == 5 * 65536,
        "SGRID fields must retain little-endian signed fixed coordinates and heading");
    rejects(
        [&] {
            (void)toca::scene::decode_start_grid(std::span(grid).first(639));
        },
        "Incomplete SGRID records were accepted");

    Bytes cameras(112);
    std::memcpy(cameras.data(), "CM3D", 4);
    put32(cameras, 4, 100);
    put32(cameras, 8, 32);
    put16(cameras, 12, 1);
    put32(cameras, 20, 32);
    put32(cameras, 32, 80);
    identity(cameras, 40);
    put32(cameras, 40 + 14 * 4, 0xfffd0000);
    const auto decoded = toca::scene::decode_scene_cameras(cameras);
    expect(decoded.size() == 1 && decoded[0].transform[14] == -3 * 65536, "Transform-only camera resources lost their original matrices");
    rejects(
        [&] {
            (void)toca::scene::decode_scene_cameras(std::span(cameras).first(111));
        },
        "Truncated camera transform was accepted");

    auto inconsistent = toca::scene::resolve_setup({});
    inconsistent.horizon_index = 999;
    const toca::AssetStore store(std::filesystem::current_path());
    try {
        (void)toca::scene::load_scene_resources(store, inconsistent);
        throw std::logic_error("Inconsistent public scene setup was accepted");
    } catch (const std::runtime_error& error) {
        expect(std::string_view(error.what()).find("resolved weather/horizon") != std::string_view::npos, "Scene setup must be validated before opening archive files");
    }
    std::cout << "PASS: original terrain/grid/camera contracts and Mega geometry, static baking and CRC32 texture routing\n";
}

void reference(const std::filesystem::path& root) {
    const toca::AssetStore store(root);
    const auto high = toca::scene::load_scene_resources(store, toca::scene::resolve_setup({}));
    {
        // Actual443030 queue, sunny DONL NoQual view2/update3. Cloudy's
        // unchanged near/far line colors give the same functional records.
        const auto lines = toca::scene::queued_grid_lines(toca::scene::prepare_grid_lines(high), high.setup, {-9293674, 478175, -5359067});
        Bytes records(lines.size() * 28);
        for (std::size_t index = 0; index < lines.size(); ++index) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                put32(records, index * 28 + axis * 4, std::uint32_t(lines[index].from[axis]));
                put32(records, index * 28 + 12 + axis * 4, std::uint32_t(lines[index].to[axis]));
            }
            put32(records, index * 28 + 24, lines[index].color);
        }
        expect(lines.size() == 16 && toca::crc32(records) == 0xe715ba80u,
            "Runtime SGRID/POINTS support, camera-Q16 visibility and colors must match every actual443030 queued line");
    }
    expect(toca::crc32(high.car_glow_bmps[0]) == 0x3ac4f26cu && toca::crc32(high.car_glow_bmps[1]) == 0xe3bed0e7u,
        "Car lamps must load the original BGLOW/HGLOW images in rear/front order");
    const auto mega = toca::scene::load_scene_resources(store, toca::scene::resolve_setup({.detail = 4}), "missing-mega-models");
    expect(mega.setup.selection.detail == 4 && !mega.low_detail_points && mega.route_points == high.route_points && mega.objects.nodes.size() == high.objects.nodes.size()
               && mega.objects.textures.size() == high.objects.textures.size() && mega.ground.chunks.size() == high.ground.chunks.size()
               && mega.car.member_indices == high.car.member_indices && mega.car.models[0].meshes.size() == high.car.models[0].meshes.size(),
        "Mega without overrides must retain Very High terrain, full objects, routes and original high car model");
    expect(high.ground.chunks.size() == 304 && high.ground.textures.size() == 15 && high.trees.meshes.size() == 61 && high.fence.meshes.size() == 119,
        "Canonical Donington terrain/scenery structure changed");
    expect(high.objects.nodes.size() == 486 && high.objects.textures.size() == 132 && high.objects.trailing_null_byte && high.cameras.size() == 24,
        "Canonical high-detail objects or cameras changed");
    expect(high.collision_hpc.size() == 258694 && high.pits.points.size() == 34 && high.pits.reserved == 0 && !high.low_detail_points, "Canonical KEEP resources changed");
    expect(high.grid[0].first_xz == std::array<std::int32_t, 2>{-16861688, -3673931} && high.grid[0].heading == 2139, "Canonical grid record was decoded incorrectly");
    expect(high.car.member_indices == std::array<std::size_t, 3>{1, 2, 3} && high.horizon.meshes.size() == 1 && high.horizon.textures.size() == 1,
        "Canonical Honda triple or selected Donington horizon was not decoded");
    expect(high.horizon.meshes[0].name == "DO023D_1" && high.horizon.textures[0].name == "DO02", "Default Donington weather selected the wrong horizon identity");
    constexpr std::array<std::array<std::size_t, 4>, 10> prop_counts{
        {{29, 8, 29, 14}, {15, 10, 26, 0}, {14, 10, 17, 24}, {20, 5, 16, 0}, {19, 6, 20, 18}, {26, 8, 21, 14}, {24, 7, 34, 24}, {20, 5, 20, 27}, {26, 10, 21, 21}, {5, 0, 0, 24}}};
    const auto check_props = [&](const toca::scene::SceneResources& scene) {
        auto expected = prop_counts.at(scene.setup.selection.track_id);
        if (scene.setup.selection.detail == 0) {
            expected[3] = 0;
            expect(scene.flags.model.nodes.empty(), "Detail0 must skip FLAGS loading entirely");
        }
        expect(std::array{scene.cones.actors.size(), scene.posts.actors.size(), scene.signs.actors.size(), scene.flags.actors.size()} == expected,
            "Scene FREE prop registration must retain per-track counts and original detail/Lavaland skips");
        if (scene.setup.selection.track_id == 9) {
            expect(scene.posts.model.nodes.empty() && scene.signs.model.nodes.empty(), "Lavaland must skip POSTS/SIGNS resources");
        }
    };
    check_props(high);
    const auto keep = store.open_archive("data/donlkeep.bfp");
    expect(std::ranges::equal(high.route_points, keep.member(1).bytes()), "Highest detail must preserve KEEP POINTS");
    toca::scene::SceneSelection selection;
    selection.detail = 0;
    selection.weather_choice = 5;
    selection.car_id = 1;
    const auto low = toca::scene::load_scene_resources(store, toca::scene::resolve_setup(selection));
    check_props(low);
    const auto free = store.open_archive("data/donlfree.bfp");
    const auto check_route_records = [](std::span<const std::uint8_t> original, std::span<const std::uint8_t> reduced) {
        const auto count = std::uint32_t(original[0]) | (std::uint32_t(original[1]) << 8) | (std::uint32_t(original[2]) << 16) | (std::uint32_t(original[3]) << 24);
        const auto route_size = std::size_t(4) + std::size_t(count) * 64;
        expect(original.size() >= route_size && reduced.size() >= route_size && std::ranges::equal(original.first(route_size), reduced.first(route_size)),
            "LOWPOINT must preserve all original qualifying, session-checkpoint and AI route records while selecting its own barrier section");
    };
    check_route_records(high.route_points, low.route_points);
    expect(low.low_detail_points && std::ranges::equal(low.route_points, free.member(0).bytes()) && low.objects.nodes.size() == 160 && low.objects.textures.size() == 34
               && low.objects.trailing_null_byte,
        "Lower detail must select FREE LOWPOINT and ZEROBJ in original order");
    expect(low.car.member_indices == std::array<std::size_t, 3>{4, 5, 6} && low.horizon.meshes.size() == 2 && low.horizon.textures.size() == 2
               && low.horizon.meshes[0].name == "DO053D_1" && low.horizon.textures[0].name == "DO05",
        "Storm/Audi selection must change the original horizon and car triple");
    for (const auto detail : {1u, 2u}) {
        const auto reduced = toca::scene::load_scene_resources(store, toca::scene::resolve_setup({.detail = detail}));
        check_props(reduced);
        check_route_records(high.route_points, reduced.route_points);
        const toca::scene::BarrierData reduced_barriers(reduced.route_points, reduced.ground.sector_centers);
        expect(reduced.low_detail_points && std::ranges::equal(reduced.route_points, free.member(0).bytes()) && reduced.objects.nodes.size() == 306
                   && reduced.objects.textures.size() == 120 && reduced_barriers.segments().size() == 229,
            "Donington detail1/2 must load original LOWOBJ and LOWPOINT while retaining its authored flags");
    }
    expect(high.horizon_actors.size() == 17877 && high.horizon_auxiliary.size() == 36, "Original horizon auxiliary members were lost");
    expect(high.moving_horizon.moving_roots == std::vector<std::uint32_t>{0, 2} && high.moving_horizon.moving.meshes[0].name == "AIRMARK_1"
               && high.moving_horizon.rotorcraft.nodes.empty() && low.moving_horizon.moving_roots == high.moving_horizon.moving_roots,
        "Donington must decode both original moving actors in dry and wet weather while skipping its empty rotorcraft member");
    const auto donor = std::ranges::find_if(high.moving_horizon.moving.textures, [](const auto& texture) {
        return texture.name == "PSP1";
    });
    expect(donor != high.moving_horizon.moving.textures.end() && high.moving_horizon.texture_palette.size() == 1024
               && std::ranges::equal(high.moving_horizon.texture_palette, std::span(donor->bmp).subspan(54, 1024))
               && std::ranges::equal(std::span(high.moving_horizon.texture_palette).last(4), std::array<std::uint8_t, 4>{255, 255, 255, 0})
               && high.moving_horizon.texture_palette == low.moving_horizon.texture_palette,
        "Incomplete HOR textures must receive the exact complete original PSP1 palette, including its authored white index255");
    for (const auto& chunk : high.ground.chunks) {
        for (const auto& vertex : chunk.mesh.vertices) {
            expect(std::isfinite(vertex.position[0]) && std::isfinite(vertex.position[1]) && std::isfinite(vertex.position[2]) && vertex.u >= .02f && vertex.u <= .99f
                       && vertex.v >= .02f && vertex.v <= .99f,
                "Original terrain coordinates/UV consumer produced invalid output");
        }
    }

    struct TrackFixture {
        std::uint32_t id;
        const char* archive_prefix;
        const char* horizon_prefix;
        std::size_t chunks, textures, sectors, trees, fence, objects, object_textures, cameras, hpc, pits, actors, auxiliary, barriers;
        std::size_t low_objects, low_textures, zero_objects, zero_textures, low_barriers;
        std::uint32_t default_weather, default_horizon;
    };

    for (const auto& fixture : {
             TrackFixture{1, "data/silv", "SI", 323, 15, 484, 26, 93, 512, 119, 26, 230238, 3332, 43593, 23261, 241, 324, 102, 232, 57, 241, 5, 4},
             TrackFixture{2, "data/thru", "TH", 464, 13, 567, 59, 55, 452, 108, 27, 228704, 1348, 34597, 36, 284, 286, 88, 164, 39, 278, 2, 2},
             TrackFixture{3, "data/bran", "BH", 197, 13, 210, 61, 45, 860, 161, 19, 135324, 3204, 36, 23261, 204, 512, 144, 248, 40, 204, 2, 1},
             TrackFixture{4, "data/oult", "OP", 362, 16, 435, 131, 125, 632, 106, 36, 216412, 2564, 36, 23261, 257, 392, 85, 86, 43, 257, 1, 0},
             TrackFixture{5, "data/dons", "DO", 304, 15, 378, 61, 119, 482, 127, 24, 258694, 1668, 17877, 36, 230, 282, 107, 170, 44, 230, 3, 3},
             TrackFixture{6, "data/crof", "CR", 307, 11, 342, 51, 60, 596, 76, 26, 185582, 1604, 55009, 36, 283, 382, 48, 92, 33, 271, 2, 2},
             TrackFixture{7, "data/knoc", "KN", 234, 14, 234, 19, 66, 442, 98, 22, 125858, 1284, 36, 23261, 144, 316, 88, 90, 37, 144, 1, 0},
             TrackFixture{8, "data/snet", "SN", 325, 14, 351, 32, 61, 610, 160, 28, 165134, 2436, 18881, 23261, 235, 396, 116, 140, 46, 235, 2, 2},
             TrackFixture{9, "data/land", "LAVA", 109, 9, 120, 0, 3, 4, 3, 13, 187374, 1924, 43593, 23261, 43, 4, 3, 4, 3, 43, 1, 0},
         }) {
        const auto scene = toca::scene::load_scene_resources(store, toca::scene::resolve_setup({fixture.id, 0, 0, 3}));
        check_props(scene);
        constexpr std::array<std::uint32_t, 10> moving_counts{2, 2, 3, 0, 0, 2, 3, 0, 1, 0};
        const bool dry = scene.setup.weather == 1 || scene.setup.weather == 2;
        const auto expected_moving = dry || fixture.id == 0 || fixture.id == 5 ? moving_counts[fixture.id] : 0;
        const bool expected_rotorcraft = dry && (fixture.id == 1 || fixture.id == 3 || fixture.id == 4 || fixture.id == 7 || fixture.id == 8);
        expect(scene.moving_horizon.moving_roots.size() == expected_moving && !scene.moving_horizon.rotorcraft.nodes.empty() == expected_rotorcraft,
            "Scene loading must register the actual original HOR actor groups with their track/weather gates");
        expect(scene.moving_horizon.texture_palette == (expected_moving || expected_rotorcraft ? high.moving_horizon.texture_palette : std::vector<std::uint8_t>{}),
            "Enabled incomplete HOR textures must use the same original palette donor; skipped groups must not request it");
        expect(scene.ground.chunks.size() == fixture.chunks && scene.ground.textures.size() == fixture.textures && scene.ground.sector_centers.size() == fixture.sectors
                   && scene.trees.meshes.size() == fixture.trees && scene.fence.meshes.size() == fixture.fence,
            "Canonical track ground/scenery structure changed");
        expect(scene.objects.nodes.size() == fixture.objects && scene.objects.textures.size() == fixture.object_textures && scene.objects.trailing_null_byte
                   && scene.cameras.size() == fixture.cameras,
            "Canonical track high-detail objects or cameras changed");
        expect(scene.collision_hpc.size() == fixture.hpc && 4 + scene.pits.points.size() * 64 == fixture.pits && scene.pits.reserved == 0
                   && scene.horizon_actors.size() == fixture.actors && scene.horizon_auxiliary.size() == fixture.auxiliary,
            "Track KEEP/HOR resources were lost or replaced with another circuit's data");
        expect(scene.setup.weather == fixture.default_weather && scene.setup.horizon_index == fixture.default_horizon, "Track default must select its original weather/horizon");
        const auto prefix = std::string(fixture.archive_prefix);
        const auto track_keep = store.open_archive(prefix + "keep.bfp");
        const auto track_free = store.open_archive(prefix + "free.bfp");
        expect(std::ranges::equal(scene.route_points, track_keep.member(1).bytes()), "Track highest detail must retain KEEP POINTS");
        const toca::scene::BarrierData barriers(scene.route_points, scene.ground.sector_centers);
        expect(barriers.sector_count() == fixture.sectors && barriers.segments().size() == fixture.barriers,
            "Track must supply its original barrier segments and complete terrain-sector grid");
        for (const auto& segment : barriers.segments()) {
            if (segment.kind == 2 || segment.kind == 3) {
                expect(segment.next_index < barriers.segments().size(), "Track special barriers must retain their linked recovery groups");
            }
        }
        for (std::uint32_t detail = 0; detail < 3; ++detail) {
            const auto reduced = toca::scene::load_scene_resources(store, toca::scene::resolve_setup({fixture.id, 0, 0, detail}));
            check_props(reduced);
            check_route_records(scene.route_points, reduced.route_points);
            const toca::scene::BarrierData reduced_barriers(reduced.route_points, reduced.ground.sector_centers);
            expect(reduced.low_detail_points && std::ranges::equal(reduced.route_points, track_free.member(0).bytes())
                       && reduced.objects.nodes.size() == (detail == 0 ? fixture.zero_objects : fixture.low_objects)
                       && reduced.objects.textures.size() == (detail == 0 ? fixture.zero_textures : fixture.low_textures)
                       && reduced_barriers.segments().size() == fixture.low_barriers,
                "Reduced detail must consume LOWPOINT barrier data and the matching original object member");
        }
        for (std::uint32_t weather = 1; weather <= 5; ++weather) {
            for (const auto variant : {1u, 2u}) {
                const auto setup = toca::scene::resolve_setup({fixture.id, 0, weather, 3}, variant);
                const auto selected = toca::scene::load_scene_resources(store, setup);
                const auto name = fixture.id == 9 ? std::string("LAVA03_1") : std::string(fixture.horizon_prefix) + "0" + std::to_string(setup.horizon_index + 1) + "3D_1";
                expect(selected.horizon.meshes[0].name == name, "Explicit weather/cloudy variant selected the wrong original track horizon");
            }
        }
    }

    std::cout << "PASS: all ten original track KEEP/FREE/HOR families, all detail/weather choices, unchanged route records, terrain sectors and linked barriers\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Expected optional canonical data root");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
