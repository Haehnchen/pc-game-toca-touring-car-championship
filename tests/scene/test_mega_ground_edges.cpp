#include "scene/mega_ground_edges.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;
using Point = std::array<float, 3>;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void add(Terrain& terrain, std::int16_t material, std::array<Point, 3> positions, Point normal) {
    TerrainChunk chunk;
    for (const auto& position : positions) {
        chunk.mesh.vertices.push_back({position, normal, 0, 0});
    }
    chunk.mesh.faces.push_back({{0, 1, 2}, material, {255, 255, 255, 255}});
    chunk.face_flags.push_back(0);
    terrain.chunks.push_back(std::move(chunk));
}

void check_bridge_indices(const Terrain& terrain, const MegaGroundEdges& edges) {
    if (edges.bridges.empty()) {
        expect(edges.bridge_indices.empty(), "No gap bridges must leave owner-index storage empty");
        return;
    }
    std::vector<unsigned> visits(edges.bridges.size());
    expect(edges.bridge_indices.size() == terrain.chunks.size(), "Bridge owner index must cover canonical chunk slots");
    for (std::size_t chunk = 0; chunk < edges.bridge_indices.size(); ++chunk) {
        expect(edges.bridge_indices[chunk].size() == terrain.chunks[chunk].mesh.faces.size(), "Bridge owner index must cover canonical face slots");
        for (std::size_t face = 0; face < edges.bridge_indices[chunk].size(); ++face) {
            for (const auto index : edges.bridge_indices[chunk][face]) {
                expect(index < edges.bridges.size() && edges.bridges[index].source_chunk == chunk && edges.bridges[index].source_face == face,
                    "Every indexed bridge must reference only its primary lighting/UV source face");
                ++visits[index];
            }
        }
    }
    for (const auto count : visits) {
        expect(count == 1, "Each bridge must render once through exactly one source owner");
    }
}

void lavaland_gaps() {
    Terrain terrain;
    terrain.textures = {{"WALL04A", {}, ImageEncoding::webp}, {"LAVA03", {}, ImageEncoding::webp}, {"TRACK05", {}, ImageEncoding::webp}, {"WALL04", {}, ImageEncoding::webp},
        {"WALL09", {}, ImageEncoding::webp}};
    const Point a{0, 0, 0}, b{10, 0, 0}, c{0, 0, 10};
    add(terrain, 2, {a, b, c}, {0, 1, 0});
    add(terrain, 0, {Point{0, 0, -.006f}, Point{10, 0, -.006f}, Point{10, -6, -10}}, {0, .8f, .6f});
    auto& source = terrain.chunks[0].mesh.vertices;
    source[0].u = .13f;
    source[0].v = .22f;
    source[1].u = .77f;
    source[1].v = .33f;
    source[2].u = .5f;
    source[2].v = .9f;
    const auto prepared = prepare_mega_ground_edges(terrain, 9);
    expect(prepared.faces.empty() && prepared.normals.empty() && prepared.caps.empty() && prepared.bridges.size() == 1,
        "Lavaland must close a genuine road/wall gap across its authored bend without neutral faces, collars or normal averaging");
    check_bridge_indices(terrain, prepared);
    const auto& bridge = prepared.bridges[0];
    expect(bridge.source_chunk == 0 && bridge.source_face == 0 && bridge.texture == 2 && bridge.triangles.size() == 2,
        "Road/wall gaps must use the actual TRACK05 source texture, lighting and authored UV edge");
    double area = 0;
    for (const auto& triangle : bridge.triangles) {
        const auto& p = triangle[0].position;
        const auto& q = triangle[1].position;
        const auto& r = triangle[2].position;
        area += std::abs((double(q[0]) - p[0]) * (double(r[2]) - p[2]) - (double(q[2]) - p[2]) * (double(r[0]) - p[0])) * .5;
        for (const auto& vertex : triangle) {
            expect(vertex.position[0] >= 0 && vertex.position[0] <= 10 && vertex.position[1] == 0 && vertex.position[2] >= -.006f && vertex.position[2] <= 0,
                "Lavaland bridge positions must lie strictly inside the unchanged source-edge gap");
            expect(vertex.opacity == 255 && vertex.source_weights[2] == 0 && vertex.source_weights[0] >= 0 && vertex.source_weights[1] >= 0
                       && std::abs(vertex.source_weights[0] + vertex.source_weights[1] - 1) < 1.e-6f,
                "Opaque bridge lighting and authored UVs must interpolate only the corresponding primary edge corners");
            const auto u = vertex.source_weights[0] * source[0].u + vertex.source_weights[1] * source[1].u;
            expect(std::abs(u - (.13f + .064f * vertex.position[0])) < 1.e-6f, "Source weights must retain the original non-world-mapped UV edge phase");
        }
    }
    expect(std::abs(area - .06) < 1.e-8, "Source-gap triangles must cover the genuine open strip once without overlapping either original face");
    expect(source[0].position == a && source[0].u == .13f && source[0].normal == Point{0, 1, 0} && terrain.chunks[1].mesh.vertices[0].normal == Point{0, .8f, .6f},
        "Lavaland source-gap caching must preserve all original positions, UVs and lighting normals");
    for (const std::int16_t material : {0, 3}) {
        auto wall = terrain;
        wall.chunks[1].mesh.faces[0].texture = material;
        expect(prepare_mega_ground_edges(wall, 9).bridges.size() == 1, "Both audited TRACK05/WALL04 variants may close bounded original gaps");
    }
    for (const std::int16_t material : {0, 1}) {
        auto same = terrain;
        same.chunks[0].mesh.faces[0].texture = same.chunks[1].mesh.faces[0].texture = material;
        const auto matching = prepare_mega_ground_edges(same, 9);
        expect(matching.bridges.size() == 1 && matching.bridges[0].texture == material, "Audited same-LAVA03 and same-WALL04A pairs must keep their primary source material");
        check_bridge_indices(same, matching);
    }
    auto unrelated = terrain;
    unrelated.chunks[1].mesh.faces[0].texture = 4;
    expect(prepare_mega_ground_edges(unrelated, 9).bridges.empty(), "Unaudited wall families must not receive automatic source-gap geometry");
    auto covered_edge = terrain;
    add(covered_edge, 4, {a, b, {0, 2, -10}}, {0, 1, 0});
    expect(prepare_mega_ground_edges(covered_edge, 9).bridges.empty(), "A third complete source edge must block gap bridging regardless of its material");
    auto occupied = terrain;
    add(occupied, 4, {Point{4, 0, -.01f}, Point{6, 0, -.01f}, Point{5, 0, .01f}}, {0, 1, 0});
    const auto trimmed = prepare_mega_ground_edges(occupied, 9);
    expect(trimmed.bridges.size() == 1, "A third source footprint must trim only the occupied part of an otherwise genuine gap");
    double uncovered_area = 0;
    for (const auto& triangle : trimmed.bridges[0].triangles) {
        const auto& p = triangle[0].position;
        const auto& q = triangle[1].position;
        const auto& r = triangle[2].position;
        const auto triangle_area = std::abs((double(q[0]) - p[0]) * (double(r[2]) - p[2]) - (double(q[2]) - p[2]) * (double(r[0]) - p[0])) * .5;
        expect(triangle_area > 0, "Trimmed source gaps must contain no degenerate projected triangles");
        uncovered_area += triangle_area;
    }
    expect(std::abs(uncovered_area - .0522) < 1.e-8, "Gap partition must subtract the third source triangle's .0078 area exactly once without overlap or missing free cells");
    check_bridge_indices(occupied, trimmed);
    auto overhead = occupied;
    for (auto& vertex : overhead.chunks[2].mesh.vertices) {
        vertex.position[1] = 10;
    }
    const auto separate_layer = prepare_mega_ground_edges(overhead, 9);
    expect(separate_layer.bridges.size() == 1 && separate_layer.bridges[0].triangles.size() == bridge.triangles.size(),
        "Unrelated source layers at another height must not trim a gap merely because their XZ footprints overlap");
    for (std::size_t triangle = 0; triangle < bridge.triangles.size(); ++triangle) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& expected = bridge.triangles[triangle][corner];
            const auto& actual = separate_layer.bridges[0].triangles[triangle][corner];
            expect(actual.position == expected.position && actual.source_weights == expected.source_weights && actual.opacity == expected.opacity,
                "An unrelated elevated source layer must preserve every cached bridge vertex and primary-source weight");
        }
    }
    auto rounding_only = terrain;
    for (auto& vertex : rounding_only.chunks[1].mesh.vertices) {
        vertex.position[2] += .005f;
    }
    const auto tiny = prepare_mega_ground_edges(rounding_only, 9);
    expect(tiny.bridges.empty(), "Source-rounding joins below the audited .002 coarse export offset must remain outside this package");
    check_bridge_indices(rounding_only, tiny);
    auto too_wide = terrain;
    for (auto& vertex : too_wide.chunks[1].mesh.vertices) {
        vertex.position[2] -= .02f;
    }
    expect(prepare_mega_ground_edges(too_wide, 9).bridges.empty(), "Source gaps beyond the explicit .01 endpoint bound must retain original geometry");
    auto overlap = terrain;
    for (auto& vertex : overlap.chunks[1].mesh.vertices) {
        vertex.position[2] += .012f;
    }
    expect(prepare_mega_ground_edges(overlap, 9).bridges.empty(), "Overlap-only source pairs must never acquire opaque bridge layers");
    auto translucent = terrain;
    translucent.chunks[1].mesh.faces[0].color[3] = 127;
    expect(prepare_mega_ground_edges(translucent, 9).bridges.empty(), "Source-gap bridges must never alter partially transparent source faces");
    auto fixed = terrain;
    fixed.chunks[1].face_flags[0] = 0x80;
    expect(prepare_mega_ground_edges(fixed, 9).bridges.empty(), "Fixed-lighting source faces must retain their original rendering");
    for (auto& texture : terrain.textures) {
        texture.encoding = ImageEncoding::bmp;
    }
    const auto original = prepare_mega_ground_edges(terrain, 9);
    expect(original.bridges.empty() && original.faces.empty(), "The original Lavaland BMP profile must retain its exact source geometry");
    check_bridge_indices(terrain, original);
    std::cout << "PASS: Lavaland bounded opaque source gaps, authored UV provenance, protected source edges and indexed ownership\n";
}

void direct_edges() {
    Terrain terrain;
    terrain.textures = {{"NUGRASS", {}, ImageEncoding::webp}, {"GRAVEL", {}, ImageEncoding::webp}, {"THWLINE", {}, ImageEncoding::webp}};
    const Point a{0, 0, 0}, b{10, 0, 0}, c{0, 0, 10};
    add(terrain, 0, {a, b, c}, {0, 1, 0});
    add(terrain, 1, {b, a, {10, 0, -10}}, {.1f, .9949874f, 0});
    const Point near_a{-.00005f, 0, 0}, near_c{-.00005f, 0, 10};
    add(terrain, 0, {near_a, near_c, {-10, 0, 0}}, {0, 1, 0});
    const auto cache = prepare_mega_ground_direct_edges(terrain, 2);
    expect(cache.edges.size() == 1 && cache.edges[0].width == .3f, "Direct neutral edges must use the accepted bounded world width");
    expect(!cache.gravel[0][0].empty() && cache.gravel[1][0].empty() && !cache.gravel[2][0].empty(),
        "Direct caps must propagate across a paired nearcoincident grass edge while staying off gravel source faces");
    expect(cache.normals[0][0] && cache.normals[0][0] == cache.normals[1][1] && !cache.normals[2][0],
        "Direct lighting must share exact endpoint copies without welding nearby source normals");
    expect(terrain.chunks[0].mesh.vertices[0].position == a && terrain.chunks[0].mesh.vertices[0].normal == Point{0, 1, 0},
        "Direct cached bands must preserve canonical source geometry and normals");
    auto near_gravel = terrain;
    for (auto& vertex : near_gravel.chunks[1].mesh.vertices) {
        vertex.position[1] -= .0001f;
    }
    const auto near_seed = prepare_mega_ground_direct_edges(near_gravel, 2);
    expect(near_seed.edges.size() == 1 && near_seed.edges[0].endpoints == std::array{a, b} && !near_seed.gravel[0][0].empty(),
        "Direct seeds must accept paired nearcoincident boundaries and remain exactly on the authored grass edge");
    expect(near_seed.normals[0][0] == Point{0, 1, 0} && !near_seed.normals[1][1], "Nearcoincident gravel copies must not be welded into the grass endpoint's exact normal set");
    auto ambiguous_seed = near_gravel;
    add(ambiguous_seed, 2, {a, b, {0, 0, -20}}, {0, 1, 0});
    expect(prepare_mega_ground_direct_edges(ambiguous_seed, 2).edges.empty(), "A third face at a nearcoincident direct edge must block seed creation");
    auto partial_seed = terrain;
    partial_seed.chunks[1].mesh.vertices[0].position = {5, 0, 0};
    expect(prepare_mega_ground_direct_edges(partial_seed, 2).edges.empty(), "A T-junction matching only part of an edge must not become a full direct seed");
    auto crease_seed = terrain;
    crease_seed.chunks[1].mesh.vertices[2].position = {10, 4, -10};
    expect(prepare_mega_ground_direct_edges(crease_seed, 2).edges.empty(), "Direct seed planes differing by more than fifteen degrees must preserve authored rendering");
    auto silverstone = terrain;
    silverstone.textures[0].name = "GRASS2";
    expect(prepare_mega_ground_direct_edges(silverstone, 1).edges.size() == 1, "Silverstone's exact plain material pair must prepare independently of its mixed profile");
    expect(!mega_ground_direct_profile(0) && !mega_ground_transition_profile(2), "Direct material profiles must remain independent from mixed-strip profiles");
    auto marked = terrain;
    add(marked, 2, {near_a, near_c, {10, 0, 20}}, {0, 1, 0});
    const auto barrier = prepare_mega_ground_direct_edges(marked, 2);
    expect(barrier.gravel[2][0].empty() && barrier.gravel[3][0].empty(), "An ambiguous grass/marking edge must block nearcoincident propagation and preserve all painted faces");
    expect(barrier.edges[0].taper[0] || barrier.edges[0].taper[1], "Nearcoincident painted endpoint copies must taper the direct cap");
    terrain.chunks[1].mesh.vertices[1].normal = {1, 0, 0};
    expect(!prepare_mega_ground_direct_edges(terrain, 2).normals[0][0], "A sharp direct endpoint crease must preserve source lighting");
    terrain.chunks[1].face_flags[0] = 0x80;
    expect(prepare_mega_ground_direct_edges(terrain, 2).edges.empty(), "Fixed-lighting direct material faces must retain authored rendering");
    terrain.chunks[1].face_flags[0] = 0;
    for (auto& texture : terrain.textures) {
        texture.encoding = ImageEncoding::bmp;
    }
    expect(prepare_mega_ground_direct_edges(terrain, 2).edges.empty(), "Original BMP profiles must not acquire direct band geometry");
    std::cout << "PASS: direct material pairs, nearby grass continuity, marking barriers, guarded lighting and immutable source\n";
}

void knockhill_edges() {
    Terrain terrain;
    // Deliberately reorder slots: lookup must use the audited names rather
    // than assuming canonical slots1/9/10 or another track's capitalization.
    terrain.textures = {{"WHITEL", {}, ImageEncoding::webp}, {"gravel", {}, ImageEncoding::webp}, {"g&g", {}, ImageEncoding::webp}, {"GRASS2", {}, ImageEncoding::webp}};
    const Point a{0, 0, 0}, b{10, 0, 0}, c{0, 0, 10};
    add(terrain, 2, {a, b, c}, {0, 1, 0});
    add(terrain, 3, {a, c, {-10, 0, 0}}, {.1f, .9949874f, 0});
    add(terrain, 1, {b, a, {10, 0, -10}}, {0, 1, 0});
    const auto prepared = prepare_mega_ground_edges(terrain, 7);
    expect(prepared.faces[0][0] && !prepared.faces[1][0] && !prepared.faces[2][0] && prepared.normals[0][2] == prepared.normals[1][1] && prepared.normals[0][2],
        "Knockhill must select only its exact mixed slot and share guarded lighting with its exact grass slot");
    expect(prepared.normals[0][1] && prepared.normals[0][1] == prepared.normals[2][0], "Knockhill's lowercase gravel slot must share the accepted boundary lighting");
    expect(prepared.caps.empty() && prepared.bridges.empty() && !mega_ground_direct_profile(7), "Knockhill's compatible strip boundaries need no caps, bridges or direct bands");
    for (const auto name : {"G&G", "GG", "NGG"}) {
        auto wrong = terrain;
        wrong.textures[2].name = name;
        expect(prepare_mega_ground_edges(wrong, 7).faces.empty(), "Other tracks' mixed names must not select Knockhill faces");
    }
    auto wrong_plain = terrain;
    wrong_plain.textures[1].name = "GRAVEL";
    expect(!prepare_mega_ground_edges(wrong_plain, 7).normals[0][1], "Other tracks' uppercase gravel slot must not qualify for Knockhill normal averaging");
    add(terrain, 0, {a, c, {-10, 0, -10}}, {0, 1, 0});
    const auto protected_edge = prepare_mega_ground_edges(terrain, 7);
    expect(!protected_edge.faces[3][0] && !protected_edge.normals[0][0] && !protected_edge.normals[0][2] && !protected_edge.normals[3][0],
        "Every copy of a Knockhill boundary touching WHITEL must preserve source lighting and painted geometry");
    expect(terrain.chunks[0].mesh.vertices[0].position == a && terrain.chunks[1].mesh.vertices[1].normal == Point{.1f, .9949874f, 0},
        "Knockhill's cache must leave canonical geometry and lighting data immutable");
    terrain.chunks[0].face_flags[0] = 0x80;
    expect(prepare_mega_ground_edges(terrain, 7).faces.empty(), "Fixed-lighting Knockhill strips must retain their authored rendering");
    terrain.chunks[0].face_flags[0] = 0;
    for (auto& texture : terrain.textures) {
        texture.encoding = ImageEncoding::bmp;
    }
    expect(prepare_mega_ground_edges(terrain, 7).faces.empty(), "Original Knockhill BMP profiles must not receive Mega geometry or normals");
    std::cout << "PASS: Knockhill exact material slots, cap-free cache, protected paint and immutable source\n";
}

void snetterton_edges() {
    Terrain terrain;
    terrain.textures = {{"RUMSTRIP", {}, ImageEncoding::webp}, {"GRASS3", {}, ImageEncoding::webp}, {"W_LINE", {}, ImageEncoding::webp}, {"G&G2", {}, ImageEncoding::webp},
        {"GRAVEL", {}, ImageEncoding::webp}};
    const Point a{0, 0, 0}, b{10, 0, 0}, c{0, 0, 10};
    add(terrain, 3, {a, b, c}, {0, 1, 0});
    add(terrain, 1, {a, c, {-10, 0, 0}}, {.1f, .9949874f, 0});
    add(terrain, 4, {b, a, {10, 0, -10}}, {0, 1, 0});
    const auto prepared = prepare_mega_ground_edges(terrain, 8);
    expect(prepared.faces[0][0] && !prepared.faces[1][0] && !prepared.faces[2][0] && prepared.normals[0][2] && prepared.normals[0][2] == prepared.normals[1][1]
               && prepared.normals[0][1] == prepared.normals[2][0],
        "Snetterton must resolve its exact mixed/grass/gravel names independently of numeric texture slots");
    expect(prepared.caps.empty() && prepared.bridges.empty() && !mega_ground_direct_profile(8), "Snetterton's compatible strips must not acquire caps, direct bands or bridges");
    auto wrong_mixed = terrain;
    wrong_mixed.textures[3].name = "G&G";
    expect(prepare_mega_ground_edges(wrong_mixed, 8).faces.empty(), "Snetterton must not select another track's mixed material");
    auto wrong_grass = terrain;
    wrong_grass.textures[1].name = "GRASS2";
    expect(!prepare_mega_ground_edges(wrong_grass, 8).normals[0][2], "Other tracks' grass names must not qualify for Snetterton boundary lighting");
    for (const std::int16_t material : {0, 2}) {
        auto painted = terrain;
        add(painted, material, {a, c, {-10, 0, -10}}, {0, 1, 0});
        const auto protected_edge = prepare_mega_ground_edges(painted, 8);
        expect(!protected_edge.faces[3][0] && !protected_edge.normals[0][0] && !protected_edge.normals[0][2] && !protected_edge.normals[3][0],
            "W_LINE and RUMSTRIP must each protect every incident source-normal copy and retain their own geometry");
    }
    expect(terrain.chunks[0].mesh.vertices[0].position == a && terrain.chunks[1].mesh.vertices[1].normal == Point{.1f, .9949874f, 0},
        "Snetterton's cached lighting must preserve canonical terrain positions and normals");
    terrain.chunks[0].face_flags[0] = 0x80;
    expect(prepare_mega_ground_edges(terrain, 8).faces.empty(), "Fixed-lighting Snetterton strips must retain authored rendering");
    terrain.chunks[0].face_flags[0] = 0;
    for (auto& texture : terrain.textures) {
        texture.encoding = ImageEncoding::bmp;
    }
    expect(prepare_mega_ground_edges(terrain, 8).faces.empty(), "Original Snetterton BMP profiles must retain their geometry and normals");
    std::cout << "PASS: Snetterton exact material slots, protected paint/rumble, cap-free cache and immutable source\n";
}

void brands_edges() {
    Terrain terrain;
    terrain.textures = {{"NGG", {}, ImageEncoding::webp}, {"NLSAND", {}, ImageEncoding::webp}, {"GRASS2", {}, ImageEncoding::webp}, {"WHITEL", {}, ImageEncoding::webp}};
    const Point a{0, 0, 0}, b{10, 0, 0}, c{10, 0, 10};
    add(terrain, 0, {a, b, c}, {0, 1, 0});
    terrain.chunks[0].mesh.vertices[0].u = .02f;
    terrain.chunks[0].mesh.vertices[0].v = .985f;
    terrain.chunks[0].mesh.vertices[1].u = .02f;
    terrain.chunks[0].mesh.vertices[1].v = .02f;
    terrain.chunks[0].mesh.vertices[2].u = .985f;
    terrain.chunks[0].mesh.vertices[2].v = .02f;
    add(terrain, 1, {a, c, {-5, 0, 5}}, {0, 1, 0});
    const auto ordinary = prepare_mega_ground_edges(terrain, 3);
    expect(ordinary.caps.size() == 2 && ordinary.caps[0][0].size() == 1 && ordinary.caps[1][0].empty() && ordinary.caps[0][0][0].gravel,
        "A diagonal NGG/plain sand conflict must receive one cap field while the plain face stays untouched");
    auto grass = terrain;
    grass.chunks[1].mesh.faces[0].texture = 2;
    expect(!prepare_mega_ground_edges(grass, 3).caps[0][0][0].gravel, "The opposite plain grass neighbor must select the grass ceiling");
    auto exact_bend = terrain;
    exact_bend.chunks[1].mesh.vertices[2].position[1] = 3;
    expect(
        !prepare_mega_ground_edges(exact_bend, 3).caps[0][0].empty(), "An exact authored neutral edge may receive its own-plane collar across a source bend above fifteen degrees");
    auto near_bend = exact_bend;
    for (auto& vertex : near_bend.chunks[1].mesh.vertices) {
        vertex.position[1] += .0056f;
    }
    expect(prepare_mega_ground_edges(near_bend, 3).caps[0][0].empty(), "A nearcoincident cap edge must reject a source plane bend above fifteen degrees");
    auto near = terrain;
    for (auto& vertex : near.chunks[1].mesh.vertices) {
        vertex.position[1] += .0056f;
    }
    expect(!prepare_mega_ground_edges(near, 3).caps[0][0].empty(), "The bounded Brands cap lookup must recover its audited neutral source gap above the direct-edge tolerance");
    expect(prepare_mega_ground_edges(near, 3).bridges.empty(), "A vertical-only edge displacement must not create a horizontal gap bridge");
    near.textures[0].name = "GRASS2";
    expect(prepare_mega_ground_direct_edges(near, 3).edges.empty(), "The wider cap-only lookup must not widen direct plain-material seed tolerances");
    auto marking = terrain;
    marking.chunks[1].mesh.faces[0].texture = 3;
    expect(prepare_mega_ground_edges(marking, 3).caps[0][0].empty(), "Painted neighbors must never become neutral cap seeds");
    add(marking, 1, {a, c, {-10, 0, 5}}, {0, 1, 0});
    expect(prepare_mega_ground_edges(marking, 3).caps[0][0].empty(), "A third painted user of a cap edge must block the neutral pairing");
    auto continuation = terrain;
    add(continuation, 0, {b, a, {0, 0, -10}}, {0, 1, 0});
    continuation.chunks[2].mesh.vertices[0].u = .02f;
    continuation.chunks[2].mesh.vertices[0].v = .02f;
    continuation.chunks[2].mesh.vertices[1].u = .02f;
    continuation.chunks[2].mesh.vertices[1].v = .985f;
    continuation.chunks[2].mesh.vertices[2].u = .985f;
    continuation.chunks[2].mesh.vertices[2].v = .985f;
    expect(!prepare_mega_ground_edges(continuation, 3).caps[2][0].empty(), "A shared world cap field must reach adjacent eligible NGG triangles to avoid endpoint seams");

    const auto signed_area = [](const Point& first, const Point& second, const Point& last) {
        return .5 * ((double(second[0]) - first[0]) * (double(last[2]) - first[2]) - (double(second[2]) - first[2]) * (double(last[0]) - first[0]));
    };
    const auto bridge_area = [&](const MegaGroundGapBridge& bridge) {
        double area = 0;
        for (const auto& triangle : bridge.triangles) {
            area += std::abs(signed_area(triangle[0].position, triangle[1].position, triangle[2].position));
            for (const auto& vertex : triangle) {
                expect(vertex.opacity == 255 && vertex.source_weights[2] == 0 && vertex.source_weights[0] >= 0 && vertex.source_weights[1] >= 0
                           && std::abs(vertex.source_weights[0] + vertex.source_weights[1] - 1) < 1.e-6f,
                    "Bridge colors must interpolate the corresponding opaque plain edge corners without extrapolation");
            }
        }
        return area;
    };
    auto gap = grass;
    for (auto& vertex : gap.chunks[1].mesh.vertices) {
        vertex.position[0] -= .006f;
    }
    const auto closed = prepare_mega_ground_edges(gap, 3);
    check_bridge_indices(gap, closed);
    expect(closed.bridges.size() == 1 && closed.bridges[0].triangles.size() == 2 && closed.bridges[0].source_chunk == 1 && closed.bridges[0].source_face == 0
               && closed.bridges[0].texture == 2,
        "An eligible open cap gap must cache at most two triangles with the actual plain material and lighting source");
    expect(std::abs(bridge_area(closed.bridges[0]) - .06) < 1.e-5, "Bridge triangles must cover the narrow open strip exactly once");
    auto bowtie = grass;
    bowtie.chunks[1].mesh.vertices[0].position[0] += .00006f;
    bowtie.chunks[1].mesh.vertices[1].position[0] -= .006f;
    const auto clipped = prepare_mega_ground_edges(bowtie, 3);
    expect(clipped.bridges.size() == 1 && clipped.bridges[0].triangles.size() <= 2, "A crossing edge pair must retain only its genuine gap lobe");
    expect(std::abs(bridge_area(clipped.bridges[0]) - .5 * 10 * .006 * .006 / .00606) < 1.e-5, "The bowtie overlap lobe must be clipped rather than covered by a bridge");
    const auto& plain = bowtie.chunks[1].mesh.vertices;
    for (const auto& triangle : clipped.bridges[0].triangles) {
        Point center{};
        for (const auto& vertex : triangle) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                center[axis] += vertex.position[axis] / 3;
            }
        }
        expect(signed_area(a, c, center) * signed_area(a, c, b) <= 0
                   && signed_area(plain[0].position, plain[1].position, center) * signed_area(plain[0].position, plain[1].position, plain[2].position) <= 0,
            "Bridge interiors must remain outside both unchanged source triangles");
    }
    auto overlap = grass;
    for (auto& vertex : overlap.chunks[1].mesh.vertices) {
        vertex.position[0] += .006f;
    }
    expect(prepare_mega_ground_edges(overlap, 3).bridges.empty(), "Overlapping neutral edges must not gain bridge geometry");
    auto wide_gap = grass;
    for (auto& vertex : wide_gap.chunks[1].mesh.vertices) {
        vertex.position[0] -= .02f;
    }
    expect(prepare_mega_ground_edges(wide_gap, 3).bridges.empty(), "A gap beyond the bounded neutral pairing distance must retain its source geometry");
    add(gap, 3, {a, c, {-10, 0, 5}}, {0, 1, 0});
    expect(prepare_mega_ground_edges(gap, 3).bridges.empty(), "A third painted face at a gap must block bridge creation");
    std::cout << "PASS: Brands diagonal cap cache, exact bends, guarded neutral gaps, painted barriers and mixed-face continuation\n";
    std::cout << "PASS: opaque neutral bridges, exact strip coverage, clipped bowtie overlaps, bounded pairing and plain lighting provenance\n";
}
} // namespace

int main() {
    try {
        direct_edges();
        lavaland_gaps();
        knockhill_edges();
        snetterton_edges();
        brands_edges();
        Terrain terrain;
        terrain.textures = {{"G&G", {}, ImageEncoding::webp}, {"GRASS", {}, ImageEncoding::webp}, {"GRAVEL", {}, ImageEncoding::webp}, {"STRIP", {}, ImageEncoding::webp}};
        const Point a{0, 0, 0}, b{10, 0, 0}, c{0, 0, 10}, d{10, 0, 10}, e{-10, 0, 0};
        add(terrain, 0, {a, b, c}, {0, 1, 0});
        add(terrain, 0, {b, d, c}, {0, 1, 0});
        add(terrain, 1, {a, c, e}, {.1f, .9949874f, 0});
        add(terrain, 2, {b, {20, 0, 0}, d}, {-.1f, .9949874f, 0});
        add(terrain, 3, {a, e, {-10, 0, -10}}, {0, 1, 0});
        const Point f{100, 0, 0}, g{101, 0, 0}, h{100, 0, 1};
        add(terrain, 0, {f, g, h}, {0, 1, 0});
        add(terrain, 1, {f, h, {99, 0, 0}}, {.1f, .9949874f, 0});
        const auto edges = prepare_mega_ground_edges(terrain, 0);
        expect(edges.faces[0][0] && edges.faces[1][0] && edges.faces[5][0] && !edges.faces[2][0] && !edges.faces[3][0] && !edges.faces[4][0],
            "All supported mixed patches must be selected while plain and painted faces stay outside the wider-band mask");
        expect(edges.normals[0][2] && edges.normals[0][2] == edges.normals[1][2] && edges.normals[0][2] == edges.normals[2][1],
            "Every incident copy of a safe boundary position must share its normal");
        expect(edges.normals[5][0] && edges.normals[5][0] == edges.normals[6][0] && edges.normals[5][2] == edges.normals[6][1],
            "Disconnected mixed patches must share lighting at their own exact grass boundary");
        expect(!edges.normals[5][1], "Mixed corners away from a shared plain-material boundary must preserve their authored normals");
        expect(!edges.normals[0][0] && !edges.normals[2][0] && !edges.normals[4][0], "Positions touching painted road materials must preserve authored lighting");
        expect(edges.normals[0][1] && edges.normals[0][1] == edges.normals[3][0] && edges.normals[1][1] && edges.normals[1][1] == edges.normals[3][2],
            "Gravel boundary must share lighting with the mixed layer");
        const auto& normal = *edges.normals[0][2];
        expect(std::abs(std::hypot(normal[0], normal[1], normal[2]) - 1) < 1.e-6f && normal[0] > 0 && normal[0] < .1f,
            "Shared lighting normal must remain normalized and between the original directions");
        expect(terrain.chunks[0].mesh.vertices[2].normal == Point{0, 1, 0} && terrain.chunks[2].mesh.vertices[1].normal == Point{.1f, .9949874f, 0},
            "Cached smoothing must leave every canonical source normal intact");
        for (const auto track_id : {0u, 4u, 5u}) {
            const auto supported = prepare_mega_ground_edges(terrain, track_id);
            expect(supported.faces == edges.faces && supported.normals == edges.normals, "All three accepted Mega tracks must receive the same topology and normal safety rules");
        }
        auto silverstone = terrain;
        expect(prepare_mega_ground_edges(silverstone, 1).faces.empty(), "Other tracks' mixed names must not substitute for Silverstone's audited GG material");
        silverstone.textures[0].name = "GG";
        silverstone.textures[1].name = "GRASS2";
        const auto silverstone_edges = prepare_mega_ground_edges(silverstone, 1);
        expect(silverstone_edges.faces == edges.faces && silverstone_edges.normals == edges.normals,
            "Silverstone's exact mixed and plain material pair must receive the accepted topology and normal safety rules");
        silverstone.chunks[2].face_flags[0] = 0x80;
        expect(!prepare_mega_ground_edges(silverstone, 1).normals[0][2], "Silverstone fixed-lighting boundaries must retain authored normals");
        for (const auto track_id : {2u, 6u, 9u}) {
            expect(prepare_mega_ground_edges(terrain, track_id).faces.empty(), "Unaudited tracks must preserve authored rendering");
        }
        terrain.chunks[2].mesh.vertices[1].normal = {1, 0, 0};
        const auto crease = prepare_mega_ground_edges(terrain, 0);
        expect(!crease.normals[0][2], "A sharp source crease must retain authored lighting");
        terrain.chunks[2].mesh.vertices[1].normal = {.1f, .9949874f, 0};
        terrain.chunks[2].face_flags[0] = 0x80;
        const auto fixed_lighting = prepare_mega_ground_edges(terrain, 0);
        expect(
            !fixed_lighting.normals[0][2] && fixed_lighting.faces[0][0], "One fixed-lighting incident face must block endpoint smoothing without suppressing eligible mixed faces");
        terrain.chunks[2].face_flags[0] = 0;
        terrain.chunks[5].face_flags[0] = 0x80;
        expect(!prepare_mega_ground_edges(terrain, 0).faces[5][0], "Fixed-lighting mixed faces must retain their authored band");
        terrain.chunks[5].face_flags[0] = 0;
        terrain.chunks[2].mesh.vertices[2].position = {0, 100, 0};
        expect(!prepare_mega_ground_edges(terrain, 0).normals[0][2], "One steep incident grass face must preserve endpoint lighting");
        terrain.chunks[2].mesh.vertices[2].position = e;
        terrain.chunks[5].mesh.vertices[1].position = {100, 100, 0};
        expect(!prepare_mega_ground_edges(terrain, 0).faces[5][0], "Steep mixed closure faces must remain outside the wider-band mask");
        terrain.chunks[5].mesh.vertices[1].position = g;
        terrain.chunks[6].mesh.vertices[0].position[0] += .0001f;
        expect(!prepare_mega_ground_edges(terrain, 0).normals[5][0], "Nearby coordinates must not be mistaken for an exactly shared boundary edge");
        terrain.chunks[6].mesh.vertices[0].position = f;
        terrain.chunks[6].mesh.vertices[0].normal = {0, 0, 0};
        expect(!prepare_mega_ground_edges(terrain, 0).normals[5][0], "Invalid source normals must prevent smoothing of all endpoint copies");
        terrain.chunks[6].mesh.vertices[0].normal = {.4694716f, .8829476f, 0};
        expect(prepare_mega_ground_edges(terrain, 0).normals[5][0].has_value(), "Directions within 14 degrees of the average must remain eligible");
        terrain.chunks[6].mesh.vertices[0].normal = {.5299193f, .8480481f, 0};
        expect(!prepare_mega_ground_edges(terrain, 0).normals[5][0], "Directions beyond 15 degrees of the average must preserve authored normals");
        terrain.chunks[6].mesh.vertices[0].normal = {.1f, .9949874f, 0};
        auto ambiguous = terrain;
        add(ambiguous, 1, {f, h, {99, 0, 2}}, {0, 1, 0});
        expect(!prepare_mega_ground_edges(ambiguous, 0).normals[5][0], "Edges with more than two incident faces must not create a smoothing boundary");
        for (auto& texture : terrain.textures) {
            texture.encoding = ImageEncoding::bmp;
        }
        expect(prepare_mega_ground_edges(terrain, 0).faces.empty(), "Original textures must retain their rendering");
        std::cout << "PASS: all accepted Mega ground patches, shared lighting, exact adjacency, marking/crease/steep guards and immutable source\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
