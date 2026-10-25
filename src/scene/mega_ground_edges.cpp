#include "scene/mega_ground_edges.hpp"

#include "scene/mega_ground_transitions.hpp"
#include "scene/mega_ground_uv.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace toca::scene {
namespace {
using Position = std::array<float, 3>;
using Edge = std::array<Position, 2>;

struct Face {
    std::size_t chunk, face;
    bool mixed, neutral;
};

Edge edge(Position first, Position second) {
    if (second < first) {
        std::swap(first, second);
    }
    return {first, second};
}

void prepare_edge_normals(const Terrain& terrain, const std::vector<Face>& faces, const std::map<Position, std::vector<std::size_t>>& incident, const std::set<Position>& boundary,
    std::vector<std::vector<std::optional<Position>>>& normals) {
    for (const auto& position : boundary) {
        const auto& entries = incident.at(position);
        if (!std::ranges::all_of(entries, [&](std::size_t id) {
                return faces[id].neutral;
            })) {
            continue;
        }
        Position average{};
        std::set<std::pair<std::size_t, std::uint16_t>> copies;
        for (const auto id : entries) {
            const auto& face = faces[id];
            const auto& mesh = terrain.chunks[face.chunk].mesh;
            for (const auto vertex : mesh.faces[face.face].indices) {
                if (mesh.vertices[vertex].position == position && copies.emplace(face.chunk, vertex).second) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        average[axis] += mesh.vertices[vertex].normal[axis];
                    }
                }
            }
        }
        const auto length = std::hypot(average[0], average[1], average[2]);
        if (!std::isfinite(length) || length < 1.e-6f) {
            continue;
        }
        for (auto& component : average) {
            component /= length;
        }
        const auto smooth = std::ranges::all_of(copies, [&](const auto& copy) {
            const auto& normal = terrain.chunks[copy.first].mesh.vertices[copy.second].normal;
            const auto magnitude = std::hypot(normal[0], normal[1], normal[2]);
            return std::isfinite(magnitude) && magnitude > 1.e-6f && (normal[0] * average[0] + normal[1] * average[1] + normal[2] * average[2]) / magnitude >= .9659258f;
        });
        if (smooth) {
            for (const auto& [chunk, vertex] : copies) {
                normals[chunk][vertex] = average;
            }
        }
    }
}

template <typename Faces> struct PairedEdges {
    using NearKey = std::array<double, 3>;
    const Faces& direct;
    double tolerance;
    std::map<NearKey, std::set<std::size_t>> nearby;

    PairedEdges(const Faces& source, double radius = .002) : direct(source), tolerance(radius) {
        for (std::size_t id = 0; id < direct.size(); ++id) {
            for (const auto& vertex : direct[id].source) {
                nearby[near_key(vertex.position)].insert(id);
            }
        }
    }

    NearKey near_key(const Position& position) const {
        return {std::floor(double(position[0]) / tolerance), std::floor(double(position[1]) / tolerance), std::floor(double(position[2]) / tolerance)};
    }

    bool near(const Position& a, const Position& b) const {
        return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) <= float(tolerance);
    }

    Position plane(std::size_t id) const {
        const auto& source = direct[id].source;
        Position a{}, b{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            a[axis] = source[1].position[axis] - source[0].position[axis];
            b[axis] = source[2].position[axis] - source[0].position[axis];
        }
        Position normal{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        const auto length = std::hypot(normal[0], normal[1], normal[2]);
        for (auto& component : normal) {
            component /= length;
        }
        return normal;
    }

    std::set<std::size_t> users(std::size_t id, std::size_t corner) const {
        const auto& a = direct[id].source[corner].position;
        const auto& b = direct[id].source[(corner + 1) % 3].position;
        const auto key = near_key(a);
        std::set<std::size_t> matches;
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                for (int z = -1; z <= 1; ++z) {
                    const auto found = nearby.find({key[0] + x, key[1] + y, key[2] + z});
                    if (found == nearby.end()) {
                        continue;
                    }
                    for (const auto candidate : found->second) {
                        for (std::size_t edge_index = 0; edge_index < 3; ++edge_index) {
                            const auto& c = direct[candidate].source[edge_index].position;
                            const auto& d = direct[candidate].source[(edge_index + 1) % 3].position;
                            if ((near(a, c) && near(b, d)) || (near(a, d) && near(b, c))) {
                                matches.insert(candidate);
                            }
                        }
                    }
                }
            }
        }
        if (matches.size() != 2) {
            return std::set<std::size_t>{};
        }
        return matches;
    }

    std::set<std::size_t> operator()(std::size_t id, std::size_t corner, bool allow_exact = false) const {
        const auto matches = users(id, corner);
        if (matches.empty()) {
            return matches;
        }
        const auto& a = direct[id].source[corner].position;
        const auto& b = direct[id].source[(corner + 1) % 3].position;
        if (allow_exact) {
            for (const auto candidate : matches) {
                if (candidate == id) {
                    continue;
                }
                for (std::size_t edge_index = 0; edge_index < 3; ++edge_index) {
                    if (edge(a, b) == edge(direct[candidate].source[edge_index].position, direct[candidate].source[(edge_index + 1) % 3].position)) {
                        return matches;
                    }
                }
            }
        }
        const auto first = plane(*matches.begin()), second = plane(*matches.rbegin());
        const auto cosine = std::abs(first[0] * second[0] + first[1] * second[1] + first[2] * second[2]);
        return cosine >= .9659258f ? matches : std::set<std::size_t>{};
    }
};

double side_xz(const Position& a, const Position& b, const Position& p) {
    if ((a[0] == b[0] && a[2] == b[2]) || (a[0] == p[0] && a[2] == p[2]) || (b[0] == p[0] && b[2] == p[2])) {
        return 0;
    }
    return (double(b[0]) - a[0]) * (double(p[2]) - a[2]) - (double(b[2]) - a[2]) * (double(p[0]) - a[0]);
}

std::optional<MegaGroundGapBridge> prepare_gap_bridge(const std::array<ModelVertex, 3>& mixed, std::size_t edge_index, const std::array<ModelVertex, 3>& plain,
    std::size_t source_chunk, std::size_t source_face, std::int16_t texture) {
    const auto& a = mixed[edge_index].position;
    const auto& b = mixed[(edge_index + 1) % 3].position;
    std::optional<std::array<std::size_t, 2>> indices;
    for (std::size_t first = 0; first < 3; ++first) {
        for (std::size_t second = 0; second < 3; ++second) {
            if (first == second) {
                continue;
            }
            const auto& c = plain[first].position;
            const auto& d = plain[second].position;
            if (std::hypot(a[0] - c[0], a[1] - c[1], a[2] - c[2]) <= .01f && std::hypot(b[0] - d[0], b[1] - d[1], b[2] - d[2]) <= .01f) {
                if (indices) {
                    return {};
                }
                indices = {{first, second}};
            }
        }
    }
    if (!indices) {
        return {};
    }
    const auto& c = plain[(*indices)[0]].position;
    const auto& d = plain[(*indices)[1]].position;
    const auto mixed_side = side_xz(a, b, mixed[(edge_index + 2) % 3].position);
    const auto plain_side = side_xz(c, d, plain[3 - (*indices)[0] - (*indices)[1]].position);
    if (mixed_side * plain_side >= 0) {
        return {};
    }
    // Clip each unchanged boundary segment to the outside half-plane of its
    // opposite source triangle. This removes the overlap lobe of a bowtie pair
    // while retaining the genuine open strip; no raster/position expansion.
    const auto interval = [](const Position& first, const Position& last, const Position& opposite_first, const Position& opposite_last, double interior) {
        std::array<double, 2> range{0, 1};
        const auto start = -std::copysign(1., interior) * side_xz(opposite_first, opposite_last, first);
        const auto end = -std::copysign(1., interior) * side_xz(opposite_first, opposite_last, last);
        if (start < 0 && end < 0) {
            return std::array<double, 2>{1, 0};
        }
        if (start < 0) {
            range[0] = start / (start - end);
        }
        if (end < 0) {
            range[1] = start / (start - end);
        }
        return range;
    };
    const auto mixed_range = interval(a, b, c, d, plain_side);
    const auto plain_range = interval(c, d, a, b, mixed_side);
    if (mixed_range[0] >= mixed_range[1] || plain_range[0] >= plain_range[1]) {
        return {};
    }
    const auto vertex = [&](const Position& first, const Position& last, double fraction) {
        MegaGroundTransitionVertex result{{}, {}, 255};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            result.position[axis] = float(double(first[axis]) + (double(last[axis]) - first[axis]) * fraction);
        }
        result.source_weights[(*indices)[0]] = float(1 - fraction);
        result.source_weights[(*indices)[1]] = float(fraction);
        return result;
    };
    std::array quad{vertex(a, b, mixed_range[0]), vertex(a, b, mixed_range[1]), vertex(c, d, plain_range[1]), vertex(c, d, plain_range[0])};
    // A bowtie's two clipped edges meet at one mathematical XZ crossing.
    // Round that crossing once for both vertices; their individual source-edge
    // heights remain intact. Independent casts can otherwise create a tiny
    // reversed lobe whose convexity depends on compiler optimization.
    if (mixed_range[0] > 0 && plain_range[0] > 0) {
        quad[3].position[0] = quad[0].position[0];
        quad[3].position[2] = quad[0].position[2];
    }
    if (mixed_range[1] < 1 && plain_range[1] < 1) {
        quad[2].position[0] = quad[1].position[0];
        quad[2].position[2] = quad[1].position[2];
    }
    const auto orientation = side_xz(plain[0].position, plain[1].position, plain[2].position);
    const auto projected_area = side_xz(quad[0].position, quad[1].position, quad[2].position) + side_xz(quad[0].position, quad[2].position, quad[3].position);
    if (projected_area == 0) {
        return {};
    }
    for (std::size_t corner = 0; corner < 4; ++corner) {
        if (side_xz(quad[corner].position, quad[(corner + 1) % 4].position, quad[(corner + 2) % 4].position) * projected_area < 0) {
            return {};
        }
    }
    MegaGroundGapBridge bridge{source_chunk, source_face, texture, {}};
    for (std::size_t corner = 1; corner + 1 < quad.size(); ++corner) {
        MegaGroundTransitionTriangle triangle{quad[0], quad[corner], quad[corner + 1]};
        std::array<double, 3> first{}, second{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            first[axis] = double(triangle[1].position[axis]) - triangle[0].position[axis];
            second[axis] = double(triangle[2].position[axis]) - triangle[0].position[axis];
        }
        if (std::hypot(first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0]) == 0) {
            continue;
        }
        if (projected_area * orientation < 0) {
            std::swap(triangle[1], triangle[2]);
        }
        bridge.triangles.push_back(triangle);
    }
    return bridge.triangles.empty() ? std::nullopt : std::optional(std::move(bridge));
}

void index_bridges(const Terrain& terrain, MegaGroundEdges& result) {
    if (result.bridges.empty()) {
        return;
    }
    result.bridge_indices.resize(terrain.chunks.size());
    for (std::size_t chunk = 0; chunk < terrain.chunks.size(); ++chunk) {
        result.bridge_indices[chunk].resize(terrain.chunks[chunk].mesh.faces.size());
    }
    for (std::size_t index = 0; index < result.bridges.size(); ++index) {
        const auto& bridge = result.bridges[index];
        result.bridge_indices.at(bridge.source_chunk).at(bridge.source_face).push_back(index);
    }
}

template <typename Faces> void exclude_source_coverage(MegaGroundGapBridge& bridge, std::size_t primary, std::size_t opposite, const Faces& faces) {
    struct Corner {
        std::array<double, 3> position, weights;
    };

    using Polygon = std::vector<Corner>;
    const auto area = [](const Polygon& polygon) {
        double result = 0;
        for (std::size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
            const auto& a = polygon[0].position;
            const auto& b = polygon[corner].position;
            const auto& c = polygon[corner + 1].position;
            result += (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]);
        }
        return std::abs(result);
    };
    const auto clip = [](const Polygon& polygon, const Position& a, const Position& b, double sign) {
        Polygon output;
        if (polygon.empty()) {
            return output;
        }
        const auto distance = [&](const Corner& corner) {
            const auto& p = corner.position;
            if ((p[0] == a[0] && p[2] == a[2]) || (p[0] == b[0] && p[2] == b[2])) {
                return 0.;
            }
            return sign * ((double(b[0]) - a[0]) * (p[2] - a[2]) - (double(b[2]) - a[2]) * (p[0] - a[0]));
        };
        auto previous = polygon.back();
        auto previous_distance = distance(previous);
        for (const auto& current : polygon) {
            const auto current_distance = distance(current);
            if ((current_distance >= 0) != (previous_distance >= 0)) {
                const auto fraction = previous_distance / (previous_distance - current_distance);
                Corner crossing{};
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    crossing.position[axis] = previous.position[axis] + (current.position[axis] - previous.position[axis]) * fraction;
                    crossing.weights[axis] = previous.weights[axis] + (current.weights[axis] - previous.weights[axis]) * fraction;
                }
                output.push_back(crossing);
            }
            if (current_distance >= 0) {
                output.push_back(current);
            }
            previous = current;
            previous_distance = current_distance;
        }
        return output;
    };
    std::vector<Polygon> cells;
    for (const auto& triangle : bridge.triangles) {
        Polygon polygon;
        for (const auto& vertex : triangle) {
            Corner corner{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                corner.position[axis] = vertex.position[axis];
                corner.weights[axis] = vertex.source_weights[axis];
            }
            polygon.push_back(corner);
        }
        cells.push_back(std::move(polygon));
    }
    // The pair kernel already removes its two owner interiors. Other source
    // triangles can still occupy tiny end regions. Subtract their footprints
    // into disjoint convex cells rather than covering them with opaque layers.
    for (std::size_t id = 0; id < faces.size(); ++id) {
        if (id == primary || id == opposite) {
            continue;
        }
        const auto& source = faces[id].source;
        const auto orientation = side_xz(source[0].position, source[1].position, source[2].position);
        if (orientation == 0) {
            continue;
        }
        std::vector<Polygon> uncovered;
        for (const auto& cell : cells) {
            bool separated = false;
            for (const auto axis : {0u, 2u}) {
                const auto [low, high] = std::ranges::minmax_element(cell, {}, [&](const Corner& corner) {
                    return corner.position[axis];
                });
                const auto [source_low, source_high] = std::ranges::minmax_element(source, {}, [&](const ModelVertex& vertex) {
                    return vertex.position[axis];
                });
                separated |= high->position[axis] <= source_low->position[axis] || low->position[axis] >= source_high->position[axis];
            }
            if (separated) {
                uncovered.push_back(cell);
                continue;
            }
            auto intersection = cell;
            for (std::size_t edge = 0; edge < 3 && !intersection.empty(); ++edge) {
                intersection = clip(intersection, source[edge].position, source[(edge + 1) % 3].position, std::copysign(1., orientation));
            }
            if (area(intersection) == 0) {
                uncovered.push_back(cell);
                continue;
            }
            const auto& a = source[0].position;
            const auto& b = source[1].position;
            const auto& c = source[2].position;
            const auto nx = (double(b[1]) - a[1]) * (double(c[2]) - a[2]) - (double(b[2]) - a[2]) * (double(c[1]) - a[1]);
            const auto nz = (double(b[0]) - a[0]) * (double(c[1]) - a[1]) - (double(b[1]) - a[1]) * (double(c[0]) - a[0]);
            std::size_t below = 0, above = 0;
            for (const auto& corner : intersection) {
                // ny is the negative of this XZ orientation. Preserve unrelated
                // source layers above/below the gap instead of subtracting
                // their footprint merely because it overlaps in projection.
                const auto height = a[1] + (nx * (corner.position[0] - a[0]) + nz * (corner.position[2] - a[2])) / orientation;
                below += corner.position[1] < height - .01;
                above += corner.position[1] > height + .01;
            }
            if (below == intersection.size() || above == intersection.size()) {
                uncovered.push_back(cell);
                continue;
            }
            auto interior = cell;
            for (std::size_t edge = 0; edge < 3 && !interior.empty(); ++edge) {
                const auto& a = source[edge].position;
                const auto& b = source[(edge + 1) % 3].position;
                auto outside = clip(interior, a, b, -std::copysign(1., orientation));
                if (area(outside) > 0) {
                    uncovered.push_back(std::move(outside));
                }
                interior = clip(interior, a, b, std::copysign(1., orientation));
            }
        }
        cells = std::move(uncovered);
    }
    bridge.triangles.clear();
    for (const auto& cell : cells) {
        for (std::size_t index = 1; index + 1 < cell.size(); ++index) {
            MegaGroundTransitionTriangle triangle{};
            const std::array corners{cell[0], cell[index], cell[index + 1]};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    triangle[corner].position[axis] = float(corners[corner].position[axis]);
                    triangle[corner].source_weights[axis] = float(corners[corner].weights[axis]);
                }
                triangle[corner].opacity = 255;
            }
            if (side_xz(triangle[0].position, triangle[1].position, triangle[2].position) != 0) {
                bridge.triangles.push_back(triangle);
            }
        }
    }
}

void prepare_lavaland_gaps(const Terrain& terrain, MegaGroundEdges& result) {
    struct SourceFace {
        std::size_t chunk, face;
        std::string_view material;
        bool eligible;
        std::array<ModelVertex, 3> source;
    };

    std::vector<SourceFace> faces;
    for (std::size_t chunk = 0; chunk < terrain.chunks.size(); ++chunk) {
        const auto& mesh = terrain.chunks[chunk].mesh;
        for (std::size_t index = 0; index < mesh.faces.size(); ++index) {
            const auto& face = mesh.faces[index];
            const auto* texture = face.texture < 0 ? nullptr : &terrain.textures.at(std::size_t(face.texture));
            const std::array source{mesh.vertices.at(face.indices[0]), mesh.vertices.at(face.indices[1]), mesh.vertices.at(face.indices[2])};
            const auto finite = std::ranges::all_of(source, [](const ModelVertex& vertex) {
                return std::isfinite(vertex.u) && std::isfinite(vertex.v) && std::ranges::all_of(vertex.position, [](float value) {
                    return std::isfinite(value);
                });
            });
            if (!finite) {
                return;
            }
            // Original opaque road/wall/lava surfaces only. Their authored
            // bends are not neutral smoothing or world-UV candidates.
            const auto eligible = texture && texture->encoding == ImageEncoding::webp && face.color[3] == 255 && (terrain.chunks[chunk].face_flags.at(index) & 0x80) == 0;
            faces.push_back({chunk, index, texture ? std::string_view(texture->name) : std::string_view{}, eligible, source});
        }
    }
    const auto allowed = [](std::string_view primary, std::string_view other) {
        return (primary == "TRACK05" && (other == "WALL04" || other == "WALL04A")) || (primary == "LAVA03" && other == "LAVA03") || (primary == "WALL04A" && other == "WALL04A");
    };
    const PairedEdges pairs(faces, .01);
    std::set<std::array<std::size_t, 2>> visited;
    for (std::size_t id = 0; id < faces.size(); ++id) {
        const auto& primary = faces[id];
        if (!primary.eligible) {
            continue;
        }
        for (std::size_t corner = 0; corner < 3; ++corner) {
            // Exactly two complete nearby source edges, counting every
            // material as a potential third-user barrier. No plane-angle
            // averaging: the narrow bridge follows both unchanged edges.
            const auto users = pairs.users(id, corner);
            if (users.empty()) {
                continue;
            }
            const auto other_id = *users.begin() == id ? *users.rbegin() : *users.begin();
            const auto& other = faces[other_id];
            if (!other.eligible || !allowed(primary.material, other.material) || !visited.insert({std::min(id, other_id), std::max(id, other_id)}).second) {
                continue;
            }
            const auto& a = primary.source[corner].position;
            const auto& b = primary.source[(corner + 1) % 3].position;
            for (std::size_t opposite = 0; opposite < 3; ++opposite) {
                const auto& c = other.source[opposite].position;
                const auto& d = other.source[(opposite + 1) % 3].position;
                if (!((pairs.near(a, c) && pairs.near(b, d)) || (pairs.near(a, d) && pairs.near(b, c)))) {
                    continue;
                }
                const auto distance = [](const Position& first, const Position& second) {
                    return std::hypot(first[0] - second[0], first[1] - second[1], first[2] - second[2]);
                };
                const auto offset = std::min(std::max(distance(a, c), distance(b, d)), std::max(distance(a, d), distance(b, c)));
                // The audited coarse export gaps are above .002. One-unit
                // fixed-point source-rounding joins are deliberately outside
                // this package; this is an endpoint policy, never an area or
                // raster expansion epsilon.
                if (offset <= .002f) {
                    continue;
                }
                if (auto bridge =
                        prepare_gap_bridge(other.source, opposite, primary.source, primary.chunk, primary.face, terrain.chunks[primary.chunk].mesh.faces[primary.face].texture)) {
                    exclude_source_coverage(*bridge, id, other_id, faces);
                    if (!bridge->triangles.empty()) {
                        result.bridges.push_back(std::move(*bridge));
                    }
                }
            }
        }
    }
    index_bridges(terrain, result);
}

float transition_opacity(const MegaGroundTransition& transition, const Position& weights) {
    for (const auto& triangle : transition.gravel) {
        const auto& a = triangle[0].source_weights;
        const auto& b = triangle[1].source_weights;
        const auto& c = triangle[2].source_weights;
        const auto determinant = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
        if (std::abs(determinant) < 1.e-12f) {
            continue;
        }
        const auto second = ((weights[1] - a[1]) * (c[2] - a[2]) - (weights[2] - a[2]) * (c[1] - a[1])) / determinant;
        const auto third = ((b[1] - a[1]) * (weights[2] - a[2]) - (b[2] - a[2]) * (weights[1] - a[1])) / determinant;
        const auto first = 1 - second - third;
        if (std::min({first, second, third}) >= -1.e-5f) {
            return std::clamp((first * triangle[0].opacity + second * triangle[1].opacity + third * triangle[2].opacity) / 255, 0.f, 1.f);
        }
    }
    return 0;
}

std::vector<std::vector<std::vector<MegaGroundTransitionCap>>> prepare_transition_caps(const Terrain& terrain, std::uint32_t track_id, std::vector<MegaGroundGapBridge>& bridges) {
    std::vector<std::vector<std::vector<MegaGroundTransitionCap>>> result;
    const auto profile = mega_ground_transition_profile(track_id);
    if (!profile || profile->cap_width == 0) {
        return result;
    }
    const auto mapping = *mega_ground_uv_mapping(true, track_id, profile->grass);

    struct CapFace {
        std::array<ModelVertex, 3> source;
        std::size_t chunk, face;
        bool mixed, grass, gravel;
        std::optional<MegaGroundTransition> transition;
    };

    std::vector<CapFace> faces;
    result.resize(terrain.chunks.size());
    for (std::size_t chunk_index = 0; chunk_index < terrain.chunks.size(); ++chunk_index) {
        const auto& chunk = terrain.chunks[chunk_index];
        result[chunk_index].resize(chunk.mesh.faces.size());
        for (std::size_t face_index = 0; face_index < chunk.mesh.faces.size(); ++face_index) {
            const auto& face = chunk.mesh.faces[face_index];
            std::array<ModelVertex, 3> source;
            std::array<Position, 3> positions;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                source[corner] = chunk.mesh.vertices.at(face.indices[corner]);
                positions[corner] = source[corner].position;
            }
            const auto* texture = face.texture < 0 ? nullptr : &terrain.textures.at(std::size_t(face.texture));
            const auto eligible = texture && texture->encoding == ImageEncoding::webp && mapping.supports_triangle(positions) && (chunk.face_flags.at(face_index) & 0x80) == 0;
            const auto mixed = eligible && texture->name == profile->mixed;
            const auto grass = eligible && texture->name == profile->grass;
            const auto gravel = eligible && texture->name == profile->gravel;
            faces.push_back(
                {source, chunk_index, face_index, mixed, grass, gravel, mixed ? prepare_mega_ground_transition(track_id, source, profile->feather_half_width) : std::nullopt});
        }
    }
    // This tolerance is confined to Brands' neutral cap seeds. Normal sets
    // and ordinary direct/propagation topology keep their exact/.002 rules.
    const PairedEdges cap_pairs(faces, .01), propagation(faces);
    for (std::size_t id = 0; id < faces.size(); ++id) {
        const auto& seed = faces[id];
        if (!seed.mixed || !seed.transition) {
            continue;
        }
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto matches = cap_pairs(id, corner, true);
            if (matches.size() != 2) {
                continue;
            }
            const auto other = *matches.begin() == id ? *matches.rbegin() : *matches.begin();
            if (!faces[other].grass && !faces[other].gravel) {
                continue;
            }
            const auto gravel = faces[other].gravel;
            bool conflict = false;
            for (unsigned step = 0; step <= 32; ++step) {
                Position weights{};
                weights[corner] = 1 - float(step) / 32;
                weights[(corner + 1) % 3] = float(step) / 32;
                conflict |= std::abs(transition_opacity(*seed.transition, weights) - float(gravel)) > .001f;
            }
            if (!conflict) {
                continue;
            }
            const auto& plain = faces[other];
            if (const auto bridge = prepare_gap_bridge(seed.source, corner, plain.source, plain.chunk, plain.face, terrain.chunks[plain.chunk].mesh.faces[plain.face].texture)) {
                bridges.push_back(*bridge);
            }
            const auto positions = edge(seed.source[corner].position, seed.source[(corner + 1) % 3].position);
            auto normal = cap_pairs.plane(id);
            Position a{}, b{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                a[axis] = seed.source[1].position[axis] - seed.source[0].position[axis];
                b[axis] = seed.source[2].position[axis] - seed.source[0].position[axis];
            }
            const auto area = std::hypot(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]);
            const auto length = std::hypot(positions[1][0] - positions[0][0], positions[1][1] - positions[0][1], positions[1][2] - positions[0][2]);
            MegaGroundTransitionCap cap{{positions, std::min(profile->cap_width, .45f * area / length), normal}, gravel};
            for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
                for (const auto& face : faces) {
                    if (face.mixed || (gravel ? face.gravel : face.grass)) {
                        continue;
                    }
                    for (const auto& vertex : face.source) {
                        const auto& p = vertex.position;
                        const auto& q = positions[endpoint];
                        if (std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]) <= .01f) {
                            cap.edge.taper[endpoint] = true;
                        }
                    }
                }
            }
            std::vector<bool> visited(faces.size());
            std::vector<std::size_t> queue{id};
            visited[id] = true;
            for (std::size_t item = 0; item < queue.size(); ++item) {
                const auto next = queue[item];
                const auto& face = faces[next];
                if (prepare_mega_ground_direct_band(face.source, std::span(&cap.edge, 1)).empty()) {
                    continue;
                }
                result[face.chunk][face.face].push_back(cap);
                for (std::size_t side = 0; side < 3; ++side) {
                    for (const auto neighbor : propagation(next, side, true)) {
                        if (!visited[neighbor] && faces[neighbor].mixed) {
                            visited[neighbor] = true;
                            queue.push_back(neighbor);
                        }
                    }
                }
            }
        }
    }
    return result;
}
} // namespace

MegaGroundEdges prepare_mega_ground_edges(const Terrain& terrain, std::uint32_t track_id) {
    MegaGroundEdges result;
    if (track_id == 9) {
        prepare_lavaland_gaps(terrain, result);
        return result;
    }
    const auto materials = mega_ground_transition_profile(track_id);
    if (!materials) {
        return result;
    }
    const auto mapping = *mega_ground_uv_mapping(true, track_id, materials->grass);
    std::vector<Face> faces;
    std::map<Edge, std::vector<std::size_t>> edges;
    std::map<Position, std::vector<std::size_t>> incident;
    bool has_mixed = false;
    for (std::size_t chunk_index = 0; chunk_index < terrain.chunks.size(); ++chunk_index) {
        const auto& chunk = terrain.chunks[chunk_index];
        for (std::size_t face_index = 0; face_index < chunk.mesh.faces.size(); ++face_index) {
            const auto& face = chunk.mesh.faces[face_index];
            std::array<Position, 3> positions;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                positions[corner] = chunk.mesh.vertices.at(face.indices[corner]).position;
            }
            const auto* texture = face.texture < 0 ? nullptr : &terrain.textures.at(std::size_t(face.texture));
            const auto name = texture ? std::string_view(texture->name) : std::string_view{};
            const auto mixed = texture && texture->encoding == ImageEncoding::webp && name == materials->mixed;
            const auto neutral = texture && texture->encoding == ImageEncoding::webp && (mixed || name == materials->grass || name == materials->gravel)
                                 && mapping.supports_triangle(positions) && (chunk.face_flags.at(face_index) & 0x80) == 0;
            const auto id = faces.size();
            faces.push_back({chunk_index, face_index, mixed && neutral, neutral});
            has_mixed |= mixed && neutral;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                edges[edge(positions[corner], positions[(corner + 1) % 3])].push_back(id);
                incident[positions[corner]].push_back(id);
            }
        }
    }
    if (!has_mixed) {
        return result;
    }
    result.faces.resize(terrain.chunks.size());
    result.normals.resize(terrain.chunks.size());
    for (std::size_t chunk = 0; chunk < terrain.chunks.size(); ++chunk) {
        result.faces[chunk].resize(terrain.chunks[chunk].mesh.faces.size());
        result.normals[chunk].resize(terrain.chunks[chunk].mesh.vertices.size());
    }
    for (const auto& face : faces) {
        result.faces[face.chunk][face.face] = face.mixed;
    }
    std::set<Position> boundary;
    for (const auto& [positions, entries] : edges) {
        if (entries.size() != 2) {
            continue;
        }
        const auto first_mixed = faces[entries[0]].mixed, second_mixed = faces[entries[1]].mixed;
        if (first_mixed != second_mixed && faces[entries[first_mixed ? 1 : 0]].neutral) {
            boundary.insert(positions.begin(), positions.end());
        }
    }
    prepare_edge_normals(terrain, faces, incident, boundary, result.normals);
    result.caps = prepare_transition_caps(terrain, track_id, result.bridges);
    index_bridges(terrain, result);
    return result;
}

std::optional<MegaGroundDirectProfile> mega_ground_direct_profile(std::uint32_t track_id) noexcept {
    if (track_id == 1) {
        return MegaGroundDirectProfile{"GRASS2", "GRAVEL", .3f};
    }
    if (track_id == 2) {
        return MegaGroundDirectProfile{"NUGRASS", "GRAVEL", .3f};
    }
    if (track_id == 3) {
        return MegaGroundDirectProfile{"GRASS2", "NLSAND", .3f};
    }
    return {};
}

MegaGroundDirectEdges prepare_mega_ground_direct_edges(const Terrain& terrain, std::uint32_t track_id) {
    MegaGroundDirectEdges result;
    const auto profile = mega_ground_direct_profile(track_id);
    if (!profile) {
        return result;
    }
    const auto mapping = *mega_ground_uv_mapping(true, track_id, profile->grass);
    const auto mixed_profile = mega_ground_transition_profile(track_id);

    struct DirectFace {
        std::array<ModelVertex, 3> source;
        bool grass, gravel;
    };

    std::vector<Face> faces;
    std::vector<DirectFace> direct;
    std::map<Position, std::vector<std::size_t>> incident;
    for (std::size_t chunk_index = 0; chunk_index < terrain.chunks.size(); ++chunk_index) {
        const auto& chunk = terrain.chunks[chunk_index];
        for (std::size_t face_index = 0; face_index < chunk.mesh.faces.size(); ++face_index) {
            const auto& face = chunk.mesh.faces[face_index];
            std::array<ModelVertex, 3> source;
            std::array<Position, 3> positions;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                source[corner] = chunk.mesh.vertices.at(face.indices[corner]);
                positions[corner] = source[corner].position;
            }
            const auto* texture = face.texture < 0 ? nullptr : &terrain.textures.at(std::size_t(face.texture));
            const auto name = texture ? std::string_view(texture->name) : std::string_view{};
            const auto eligible = texture && texture->encoding == ImageEncoding::webp && mapping.supports_triangle(positions) && (chunk.face_flags.at(face_index) & 0x80) == 0;
            const auto grass = eligible && name == profile->grass;
            const auto gravel = eligible && name == profile->gravel;
            const auto neutral = grass || gravel || (eligible && mixed_profile && name == mixed_profile->mixed);
            const auto id = faces.size();
            faces.push_back({chunk_index, face_index, false, neutral});
            direct.push_back({source, grass, gravel});
            for (std::size_t corner = 0; corner < 3; ++corner) {
                incident[positions[corner]].push_back(id);
            }
        }
    }
    const PairedEdges paired_faces(direct);
    std::vector<std::size_t> seeds;
    std::set<Position> boundary;
    std::map<Edge, std::size_t> candidates;
    for (std::size_t id = 0; id < faces.size(); ++id) {
        if (!direct[id].grass) {
            continue;
        }
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto matches = paired_faces(id, corner);
            if (matches.size() == 2 && std::ranges::any_of(matches, [&](std::size_t other) {
                    return direct[other].gravel;
                })) {
                candidates.emplace(edge(direct[id].source[corner].position, direct[id].source[(corner + 1) % 3].position), id);
            }
        }
    }
    for (const auto& [positions, seed] : candidates) {
        const auto& source = direct[seed].source;
        Position a{}, b{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            a[axis] = source[1].position[axis] - source[0].position[axis];
            b[axis] = source[2].position[axis] - source[0].position[axis];
        }
        Position normal{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        const auto area = std::hypot(normal[0], normal[1], normal[2]);
        const auto length = std::hypot(positions[1][0] - positions[0][0], positions[1][1] - positions[0][1], positions[1][2] - positions[0][2]);
        const auto width = std::min(profile->width, .45f * area / length);
        for (auto& component : normal) {
            component /= area;
        }
        MegaGroundDirectSegment segment{positions, width, normal};
        for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
            const auto& position = positions[endpoint];
            boundary.insert(position);
            // Canonical marking copies may differ by a few 1e-4 world units.
            // This guard only tapers caps; topology and lighting stay exact.
            for (std::size_t id = 0; id < faces.size() && !segment.taper[endpoint]; ++id) {
                if (faces[id].neutral) {
                    continue;
                }
                for (const auto& vertex : direct[id].source) {
                    if (std::hypot(vertex.position[0] - position[0], vertex.position[1] - position[1], vertex.position[2] - position[2]) <= .002f) {
                        segment.taper[endpoint] = true;
                    }
                }
            }
        }
        result.edges.push_back(segment);
        seeds.push_back(seed);
    }
    if (result.edges.empty()) {
        return result;
    }
    result.gravel.resize(terrain.chunks.size());
    result.normals.resize(terrain.chunks.size());
    for (std::size_t chunk = 0; chunk < terrain.chunks.size(); ++chunk) {
        result.gravel[chunk].resize(terrain.chunks[chunk].mesh.faces.size());
        result.normals[chunk].resize(terrain.chunks[chunk].mesh.vertices.size());
    }
    std::vector<std::vector<MegaGroundDirectSegment>> influences(faces.size());
    for (std::size_t segment = 0; segment < result.edges.size(); ++segment) {
        std::vector<bool> visited(faces.size());
        std::vector<std::size_t> queue{seeds[segment]};
        visited[seeds[segment]] = true;
        for (std::size_t item = 0; item < queue.size(); ++item) {
            const auto id = queue[item];
            const auto& face = direct[id];
            if (prepare_mega_ground_direct_band(face.source, std::span(&result.edges[segment], 1)).empty()) {
                continue;
            }
            influences[id].push_back(result.edges[segment]);
            for (std::size_t corner = 0; corner < 3; ++corner) {
                for (const auto neighbor : paired_faces(id, corner)) {
                    if (!visited[neighbor] && direct[neighbor].grass) {
                        visited[neighbor] = true;
                        queue.push_back(neighbor);
                    }
                }
            }
        }
    }
    for (std::size_t id = 0; id < faces.size(); ++id) {
        if (!influences[id].empty()) {
            const auto& face = faces[id];
            result.gravel[face.chunk][face.face] = prepare_mega_ground_direct_band(direct[id].source, influences[id]);
        }
    }
    prepare_edge_normals(terrain, faces, incident, boundary, result.normals);
    return result;
}
} // namespace toca::scene
