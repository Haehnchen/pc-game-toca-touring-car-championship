#include "scene/mega_ground_transitions.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace toca::scene {
namespace {
// Read-only Oulton GROUND.C3D G&G BMP, CRC32 77248025, 32x64. Columns
// come from the first pixel above the midpoint of each row's end-color
// medians. Simplifying the three-row median curve to within two source pixels
// removes isolated pixel spikes; both ends and the inward plateau stay fixed.
// These are material-mask coordinates, never replacement bitmap pixels.
struct ShorePoint {
    std::uint8_t row, column;
};

constexpr std::array<ShorePoint, 11> oulton_shoreline{{{0, 14}, {6, 12}, {15, 16}, {24, 8}, {27, 8}, {29, 15}, {31, 12}, {41, 12}, {48, 17}, {53, 13}, {63, 16}}};

// GP/Short share the exact32x64 G&G source, CRC32 e1616322. Its grass and
// gravel have similar luminance: select each row's split by minimum summed
// squared RGB distance to the two endpoint palettes, then simplify to within
// two source pixels. Preserve both endpoints and the inward rows35–40 plateau.
constexpr std::array<ShorePoint, 10> donington_shoreline{{{0, 11}, {4, 11}, {11, 18}, {35, 9}, {40, 9}, {43, 15}, {48, 13}, {51, 17}, {58, 18}, {63, 15}}};

// Silverstone GROUND.C3D GG BMP, CRC32 f351a508, 32x64. Endpoint RGB
// palette segmentation, three-row median and 1.5-pixel simplification retain
// both ends and the rows35–41 inward plateau within1.35 source pixels. Grass
// occupies low U and gravel high U. The .065 half-width retains the partial-UV
// grass boundary in chunk283 face81 (minimum shoreline clearance .0695343).
constexpr std::array<ShorePoint, 10> silverstone_shoreline{{{0, 11}, {4, 11}, {9, 17}, {12, 18}, {35, 9}, {41, 9}, {43, 13}, {52, 17}, {59, 17}, {63, 14}}};

// Brands Hatch NGG BMP, CRC32 ff25f7d0,64x16. Grass is low V and sand
// high V; its boundary varies along U. RGB palette segmentation and source-row
// simplification retain the measured contour within .715 source pixels.
constexpr std::array<ShorePoint, 10> brands_shoreline{{{0, 6}, {12, 7}, {16, 10}, {21, 10}, {28, 6}, {40, 6}, {43, 3}, {57, 4}, {58, 6}, {63, 6}}};

// Knockhill g&g BMP, CRC32 2890fd36,32x64. Grass is low U and gravel
// high U. Simplify the raw endpoint-palette RGB splits within two source
// pixels, retaining the isolated inward dip at row31 (maximum error1.8).
constexpr std::array<ShorePoint, 16> knockhill_shoreline{
    {{0, 16}, {3, 15}, {5, 21}, {9, 18}, {11, 23}, {14, 19}, {17, 23}, {19, 20}, {20, 12}, {30, 18}, {31, 12}, {32, 16}, {47, 15}, {53, 19}, {60, 15}, {63, 17}}};

// Snetterton G&G2 BMP, CRC32 fcea437a,32x64. Grass is low U and gravel
// high U. Raw endpoint-palette RGB splits simplify within two source pixels,
// preserving the isolated row28 dip and the narrow row46–48 excursion.
constexpr std::array<ShorePoint, 23> snetterton_shoreline{{{0, 13}, {8, 16}, {12, 12}, {14, 16}, {16, 12}, {17, 14}, {25, 10}, {27, 13}, {28, 7}, {31, 16}, {33, 13}, {36, 16},
    {37, 13}, {38, 17}, {39, 15}, {40, 18}, {46, 11}, {47, 19}, {48, 19}, {56, 9}, {57, 12}, {58, 10}, {63, 15}}};

struct TransitionProfile {
    std::uint32_t track_id;
    MegaGroundTransitionProfile rendering;
    std::span<const ShorePoint> shoreline;
    bool swap_axes = false;
    float columns = 32, rows = 64;
};

constexpr std::array<TransitionProfile, 7> profiles{{{0, {"G&G", "GRASS", "GRAVEL", .12f}, donington_shoreline}, {1, {"GG", "GRASS2", "GRAVEL", .065f}, silverstone_shoreline},
    {3, {"NGG", "GRASS2", "NLSAND", .12f, .3f}, brands_shoreline, true, 16, 64}, {4, {"G&G", "GRASS", "GRAVEL", .12f}, oulton_shoreline},
    {5, {"G&G", "GRASS", "GRAVEL", .12f}, donington_shoreline}, {7, {"g&g", "GRASS2", "gravel", .12f}, knockhill_shoreline},
    {8, {"G&G2", "GRASS3", "GRAVEL", .12f}, snetterton_shoreline}}};

const TransitionProfile* transition_profile(std::uint32_t track_id) noexcept {
    const auto profile = std::ranges::find(profiles, track_id, &TransitionProfile::track_id);
    return profile == profiles.end() ? nullptr : &*profile;
}

constexpr float epsilon = 1.e-6f;

struct Corner {
    std::array<float, 3> weights;
    float u, v;
};

using Polygon = std::vector<Corner>;

Corner interpolate(const Corner& a, const Corner& b, float fraction) {
    Corner result;
    for (std::size_t index = 0; index < result.weights.size(); ++index) {
        result.weights[index] = a.weights[index] + (b.weights[index] - a.weights[index]) * fraction;
    }
    result.u = a.u + (b.u - a.u) * fraction;
    result.v = a.v + (b.v - a.v) * fraction;
    return result;
}

Corner interpolate_precise(const Corner& a, const Corner& b, double fraction) {
    Corner result;
    for (std::size_t index = 0; index < result.weights.size(); ++index) {
        result.weights[index] = float(double(a.weights[index]) + (double(b.weights[index]) - a.weights[index]) * fraction);
    }
    result.u = float(double(a.u) + (double(b.u) - a.u) * fraction);
    result.v = float(double(a.v) + (double(b.v) - a.v) * fraction);
    return result;
}

// Each half-plane is affine in the original UV triangle. Intersections are
// barycentric combinations of that triangle, so no collision height changes.
template <bool precise = false, typename Distance> Polygon clip(std::span<const Corner> input, Distance distance) {
    Polygon output;
    if (input.empty()) {
        return output;
    }
    auto previous = input.back();
    auto previous_distance = distance(previous);
    for (const auto& current : input) {
        const auto current_distance = distance(current);
        if ((current_distance >= 0) != (previous_distance >= 0)) {
            if constexpr (precise) {
                const auto fraction = double(previous_distance) / (double(previous_distance) - current_distance);
                output.push_back(interpolate_precise(previous, current, fraction));
            } else {
                const auto fraction = previous_distance / (previous_distance - current_distance);
                output.push_back(interpolate(previous, current, fraction));
            }
        }
        if (current_distance >= 0) {
            output.push_back(current);
        }
        previous = current;
        previous_distance = current_distance;
    }
    return output;
}

MegaGroundTransitionVertex vertex(const Corner& corner, const std::array<ModelVertex, 3>& source, float opacity) {
    MegaGroundTransitionVertex result{{}, corner.weights, std::uint8_t(std::clamp(std::lround(opacity * 255.f), 0l, 255l))};
    for (std::size_t axis = 0; axis < result.position.size(); ++axis) {
        for (std::size_t index = 0; index < source.size(); ++index) {
            result.position[axis] += source[index].position[axis] * corner.weights[index];
        }
    }
    return result;
}

template <typename Opacity> void triangulate(const Polygon& polygon, const std::array<ModelVertex, 3>& source, Opacity opacity, std::vector<MegaGroundTransitionTriangle>& result) {
    for (std::size_t index = 1; index + 1 < polygon.size(); ++index) {
        const std::array corners{polygon[0], polygon[index], polygon[index + 1]};
        const auto area = (corners[1].u - corners[0].u) * (corners[2].v - corners[0].v) - (corners[1].v - corners[0].v) * (corners[2].u - corners[0].u);
        if (std::abs(area) <= epsilon * epsilon) {
            continue;
        }
        auto& triangle = result.emplace_back();
        for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
            triangle[corner] = vertex(corners[corner], source, opacity(corners[corner]));
        }
    }
}

using Affine = std::array<float, 3>;

float evaluate(const Affine& values, const Corner& corner) {
    float result = 0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        result += values[index] * corner.weights[index];
    }
    return result;
}

float polygon_area(const Polygon& polygon) {
    float area = 0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const auto& a = polygon[index];
        const auto& b = polygon[(index + 1) % polygon.size()];
        area += a.u * b.v - a.v * b.u;
    }
    return std::abs(area) * .5f;
}

struct DirectCell {
    Polygon polygon;
    Affine opacity;
};

Polygon convex_hull(Polygon points) {
    std::ranges::sort(points, [](const Corner& a, const Corner& b) {
        return std::pair(a.u, a.v) < std::pair(b.u, b.v);
    });
    points.erase(std::unique(points.begin(), points.end(),
                     [](const Corner& a, const Corner& b) {
                         return std::abs(a.u - b.u) < 1.e-7f && std::abs(a.v - b.v) < 1.e-7f;
                     }),
        points.end());
    if (points.size() < 3) {
        return {};
    }
    const auto turn = [](const Corner& a, const Corner& b, const Corner& c) {
        return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
    };
    Polygon hull;
    for (const auto& point : points) {
        while (hull.size() >= 2 && turn(hull[hull.size() - 2], hull.back(), point) <= 0) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    const auto lower = hull.size();
    for (std::size_t index = points.size() - 1; index-- > 0;) {
        const auto& point = points[index];
        while (hull.size() > lower && turn(hull[hull.size() - 2], hull.back(), point) <= 0) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    hull.pop_back();
    return hull;
}

void merge_direct_cells(std::vector<DirectCell>& cells) {
    std::erase_if(cells, [](const DirectCell& cell) {
        return std::ranges::none_of(cell.polygon, [&](const Corner& corner) {
            return evaluate(cell.opacity, corner) > epsilon;
        });
    });
    for (std::size_t first = 0; first < cells.size(); ++first) {
        cells[first].polygon = convex_hull(std::move(cells[first].polygon));
        for (std::size_t second = first + 1; second < cells.size();) {
            bool same_alpha = true;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                same_alpha &= std::abs(cells[first].opacity[corner] - cells[second].opacity[corner]) < 1.e-6f;
            }
            std::size_t shared = 0;
            if (same_alpha) {
                for (const auto& a : cells[first].polygon) {
                    shared += std::ranges::any_of(cells[second].polygon, [&](const Corner& b) {
                        return std::abs(a.u - b.u) < 1.e-7f && std::abs(a.v - b.v) < 1.e-7f;
                    });
                }
            }
            if (shared < 2) {
                ++second;
                continue;
            }
            auto combined = cells[first].polygon;
            combined.insert(combined.end(), cells[second].polygon.begin(), cells[second].polygon.end());
            auto hull = convex_hull(std::move(combined));
            if (std::abs(polygon_area(hull) - polygon_area(cells[first].polygon) - polygon_area(cells[second].polygon)) > 5.e-8f) {
                ++second;
                continue;
            }
            cells[first].polygon = std::move(hull);
            cells.erase(cells.begin() + std::ptrdiff_t(second));
            second = first + 1;
        }
    }
}

// Overlay one convex affine-alpha region onto a disjoint partition. Outside
// pieces keep their old alpha; inside uses max(old,new), split at equality.
// Thus endpoint caps from several segments never add translucent layers.
void overlay_direct_region(std::vector<DirectCell>& cells, const std::vector<Affine>& constraints, const Affine& opacity, bool minimum = false) {
    std::vector<DirectCell> next;
    for (const auto& cell : cells) {
        auto overlap = cell.polygon;
        for (const auto& constraint : constraints) {
            overlap = clip<true>(overlap, [&](const Corner& corner) {
                return evaluate(constraint, corner);
            });
        }
        if (polygon_area(overlap) <= epsilon * epsilon) {
            next.push_back(cell);
            continue;
        }
        auto remaining = cell.polygon;
        for (const auto& constraint : constraints) {
            const auto distance = [&](const Corner& corner) {
                return evaluate(constraint, corner);
            };
            const auto minimum = distance(std::ranges::min(remaining, {}, distance));
            if (minimum >= 0) {
                continue;
            }
            auto outside = clip<true>(remaining, [&](const Corner& corner) {
                return -distance(corner);
            });
            if (polygon_area(outside) > epsilon * epsilon) {
                next.push_back({std::move(outside), cell.opacity});
            }
            remaining = clip<true>(remaining, distance);
        }
        Affine difference;
        for (std::size_t index = 0; index < difference.size(); ++index) {
            difference[index] = minimum ? cell.opacity[index] - opacity[index] : opacity[index] - cell.opacity[index];
        }
        const auto distance = [&](const Corner& corner) {
            return evaluate(difference, corner);
        };
        const auto minimum = distance(std::ranges::min(overlap, {}, distance));
        const auto maximum = distance(std::ranges::max(overlap, {}, distance));
        if (maximum <= 0) {
            next.push_back({std::move(overlap), cell.opacity});
        } else if (minimum >= 0) {
            next.push_back({std::move(overlap), opacity});
        } else {
            auto old = clip<true>(overlap, [&](const Corner& corner) {
                return -distance(corner);
            });
            auto updated = clip<true>(overlap, distance);
            if (polygon_area(old) > epsilon * epsilon) {
                next.push_back({std::move(old), cell.opacity});
            }
            if (polygon_area(updated) > epsilon * epsilon) {
                next.push_back({std::move(updated), opacity});
            }
        }
    }
    cells = std::move(next);
}

bool overlay_segment(std::vector<DirectCell>& cells, const std::array<ModelVertex, 3>& source, const MegaGroundDirectSegment& segment, bool minimum = false) {
    if (!std::isfinite(segment.width) || segment.width <= 0) {
        return false;
    }
    std::array<float, 3> along{}, across{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        along[axis] = segment.endpoints[1][axis] - segment.endpoints[0][axis];
    }
    const auto length = std::hypot(along[0], along[1], along[2]);
    if (!std::isfinite(length) || length <= epsilon) {
        return false;
    }
    for (auto& component : along) {
        component /= length;
    }
    across = {segment.normal[1] * along[2] - segment.normal[2] * along[1], segment.normal[2] * along[0] - segment.normal[0] * along[2],
        segment.normal[0] * along[1] - segment.normal[1] * along[0]};
    const auto across_length = std::hypot(across[0], across[1], across[2]);
    if (!std::isfinite(across_length) || across_length <= epsilon) {
        return false;
    }
    for (auto& component : across) {
        component /= across_length;
    }
    // Rebuild the perpendicular plane normal so both cross-section axes
    // are unit length even if a caller supplies a nonunit source normal.
    const std::array normal{along[1] * across[2] - along[2] * across[1], along[2] * across[0] - along[0] * across[2], along[0] * across[1] - along[1] * across[0]};
    std::vector<Affine> distances(6 + segment.taper[0] + segment.taper[1]);
    for (std::size_t corner = 0; corner < source.size(); ++corner) {
        float longitudinal = 0, lateral = 0, height = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto offset = source[corner].position[axis] - segment.endpoints[0][axis];
            longitudinal += offset * along[axis];
            lateral += offset * across[axis];
            height += offset * normal[axis];
        }
        distances[0][corner] = lateral;
        distances[1][corner] = -lateral;
        distances[2][corner] = height;
        distances[3][corner] = -height;
        distances[4][corner] = -longitudinal;
        distances[5][corner] = longitudinal - length;
        std::size_t taper = 6;
        if (segment.taper[0]) {
            distances[taper++][corner] = segment.width - longitudinal;
        }
        if (segment.taper[1]) {
            distances[taper][corner] = segment.width - (length - longitudinal);
        }
    }
    for (std::size_t dominant = 0; dominant < distances.size(); ++dominant) {
        if (std::find(distances.begin(), distances.begin() + std::ptrdiff_t(dominant), distances[dominant]) != distances.begin() + std::ptrdiff_t(dominant)) {
            continue;
        }
        std::vector<Affine> constraints;
        Affine opacity;
        for (std::size_t corner = 0; corner < source.size(); ++corner) {
            opacity[corner] = 1 - distances[dominant][corner] / segment.width;
        }
        constraints.push_back(opacity);
        for (const auto& distance : distances) {
            Affine constraint;
            for (std::size_t corner = 0; corner < source.size(); ++corner) {
                constraint[corner] = distances[dominant][corner] - distance[corner];
            }
            constraints.push_back(constraint);
        }
        if (minimum) {
            for (auto& value : opacity) {
                value = 1 - value;
            }
        }
        overlay_direct_region(cells, constraints, opacity, minimum);
    }
    return true;
}

std::vector<MegaGroundTransitionTriangle> triangulate_cells(std::vector<DirectCell>& cells, const std::array<ModelVertex, 3>& source) {
    std::vector<MegaGroundTransitionTriangle> result;
    merge_direct_cells(cells);
    for (const auto& cell : cells) {
        if (std::ranges::none_of(cell.polygon, [&](const Corner& corner) {
                return evaluate(cell.opacity, corner) > epsilon;
            })) {
            continue;
        }
        triangulate(
            cell.polygon, source,
            [&](const Corner& corner) {
                return evaluate(cell.opacity, corner);
            },
            result);
    }
    // Keep shared source-edge reconstruction independent of float summation
    // order. The mixed-strip path retains its accepted interpolation unchanged.
    for (auto& triangle : result) {
        for (auto& vertex : triangle) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                double position = 0;
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    position += double(source[corner].position[axis]) * vertex.source_weights[corner];
                }
                vertex.position[axis] = float(position);
            }
        }
    }
    // Subpixel cell intersections can collapse when world positions round to
    // float at canonical track coordinates. They contribute no drawable area.
    std::erase_if(result, [](const MegaGroundTransitionTriangle& triangle) {
        std::array<double, 3> a{}, b{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            a[axis] = double(triangle[1].position[axis]) - triangle[0].position[axis];
            b[axis] = double(triangle[2].position[axis]) - triangle[0].position[axis];
        }
        return std::hypot(a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]) <= 1.e-10;
    });
    return result;
}

std::vector<MegaGroundTransitionTriangle> apply_transition_caps(
    const std::array<ModelVertex, 3>& source, const std::vector<MegaGroundTransitionTriangle>& gravel, std::span<const MegaGroundTransitionCap> caps) {
    const Polygon original{{{1, 0, 0}, 0, 0}, {{0, 1, 0}, 1, 0}, {{0, 0, 1}, 0, 1}};
    std::vector<DirectCell> cells{{original, {0, 0, 0}}};
    for (const auto& triangle : gravel) {
        Polygon polygon;
        for (const auto& vertex : triangle) {
            polygon.push_back({vertex.source_weights, vertex.source_weights[1], vertex.source_weights[2]});
        }
        const auto& a = polygon[0];
        const auto& b = polygon[1];
        const auto& c = polygon[2];
        const auto determinant = double(b.u - a.u) * (c.v - a.v) - double(b.v - a.v) * (c.u - a.u);
        if (std::abs(determinant) <= 1.e-12) {
            continue;
        }
        const auto alpha = double(triangle[0].opacity) / 255;
        const auto second = double(triangle[1].opacity) / 255 - alpha;
        const auto third = double(triangle[2].opacity) / 255 - alpha;
        const auto du = (second * (c.v - a.v) - third * (b.v - a.v)) / determinant;
        const auto dv = ((b.u - a.u) * third - (c.u - a.u) * second) / determinant;
        const auto origin = alpha - du * a.u - dv * a.v;
        const Affine opacity{float(origin), float(origin + du), float(origin + dv)};
        std::vector<Affine> constraints;
        for (std::size_t edge = 0; edge < 3; ++edge) {
            const auto& first = polygon[edge];
            const auto& last = polygon[(edge + 1) % 3];
            Affine constraint;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                constraint[corner] = ((last.u - first.u) * (original[corner].v - first.v) - (last.v - first.v) * (original[corner].u - first.u)) * (determinant > 0 ? 1.f : -1.f);
            }
            constraints.push_back(constraint);
        }
        overlay_direct_region(cells, constraints, opacity);
    }
    for (const auto gravel_cap : {true, false}) {
        for (const auto& cap : caps) {
            if (cap.gravel == gravel_cap && !overlay_segment(cells, source, cap.edge, !cap.gravel)) {
                return {};
            }
        }
    }
    return triangulate_cells(cells, source);
}
} // namespace

std::optional<MegaGroundTransitionProfile> mega_ground_transition_profile(std::uint32_t track_id) noexcept {
    const auto* profile = transition_profile(track_id);
    return profile ? std::optional(profile->rendering) : std::nullopt;
}

bool is_mega_ground_transition(bool mega, std::uint32_t track_id, const ModelTexture& texture) noexcept {
    const auto* profile = transition_profile(track_id);
    return mega && profile && texture.name == profile->rendering.mixed && texture.encoding == ImageEncoding::webp;
}

std::optional<MegaGroundTransition> prepare_mega_ground_transition(
    std::uint32_t track_id, const std::array<ModelVertex, 3>& source, float feather_half_width, std::span<const MegaGroundTransitionCap> caps) {
    const auto* profile = transition_profile(track_id);
    if (!profile || !std::isfinite(feather_half_width) || feather_half_width <= 0 || feather_half_width > .2f) {
        return {};
    }
    const auto shoreline = profile->shoreline;
    std::array<Corner, 3> original;
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (!std::isfinite(source[index].u) || !std::isfinite(source[index].v) || !std::ranges::all_of(source[index].position, [](float value) {
                return std::isfinite(value);
            })) {
            return {};
        }
        original[index] = {{}, profile->swap_axes ? source[index].v : source[index].u, profile->swap_axes ? source[index].u : source[index].v};
        original[index].weights[index] = 1;
    }
    const auto area = (original[1].u - original[0].u) * (original[2].v - original[0].v) - (original[1].v - original[0].v) * (original[2].u - original[0].u);
    if (std::abs(area) <= epsilon) {
        return {};
    }
    MegaGroundTransition result;
    for (std::size_t index = 0; index < original.size(); ++index) {
        result.grass[index] = vertex(original[index], source, 1);
    }
    // Selected row centers plus clamped end caps partition each source
    // triangle into strips of feather and solid gravel, so
    // translucent coverage never overlaps itself or neighboring source faces.
    for (std::size_t segment = 0; segment <= shoreline.size(); ++segment) {
        const auto& low = shoreline[segment == 0 ? 0 : segment - 1];
        const auto& high = shoreline[std::min(segment, shoreline.size() - 1)];
        const auto low_v = segment == 0 ? 0.f : (float(low.row) + .5f) / profile->rows;
        const auto high_v = segment == shoreline.size() ? 1.f : (float(high.row) + .5f) / profile->rows;
        const auto low_u = float(low.column) / profile->columns;
        const auto high_u = float(high.column) / profile->columns;
        const auto slope = (high_u - low_u) / (high_v - low_v);
        const auto distance = [&](const Corner& corner) {
            return corner.u - (low_u + (corner.v - low_v) * slope);
        };
        auto strip = clip(original, [&](const Corner& corner) {
            return corner.v - low_v;
        });
        strip = clip(strip, [&](const Corner& corner) {
            return high_v - corner.v;
        });
        if (strip.size() < 3) {
            continue;
        }
        auto feather = clip(strip, [&](const Corner& corner) {
            return distance(corner) + feather_half_width;
        });
        feather = clip(feather, [&](const Corner& corner) {
            return feather_half_width - distance(corner);
        });
        triangulate(
            feather, source,
            [&](const Corner& corner) {
                return (distance(corner) + feather_half_width) / (2 * feather_half_width);
            },
            result.gravel);
        const auto solid = clip(strip, [&](const Corner& corner) {
            return distance(corner) - feather_half_width;
        });
        triangulate(
            solid, source,
            [](const Corner&) {
                return 1.f;
            },
            result.gravel);
    }
    if (!caps.empty()) {
        result.gravel = apply_transition_caps(source, result.gravel, caps);
    }
    return result;
}

std::vector<MegaGroundTransitionTriangle> prepare_mega_ground_direct_band(const std::array<ModelVertex, 3>& source, std::span<const MegaGroundDirectSegment> segments) {
    Polygon original(3);
    for (std::size_t index = 0; index < source.size(); ++index) {
        if (!std::ranges::all_of(source[index].position, [](float value) {
                return std::isfinite(value);
            })) {
            return {};
        }
        original[index] = {{}, index == 1 ? 1.f : 0.f, index == 2 ? 1.f : 0.f};
        original[index].weights[index] = 1;
    }
    std::array<float, 3> first{}, second{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        first[axis] = source[1].position[axis] - source[0].position[axis];
        second[axis] = source[2].position[axis] - source[0].position[axis];
    }
    const std::array cross{first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0]};
    const auto source_area = std::hypot(cross[0], cross[1], cross[2]);
    if (!std::isfinite(source_area) || source_area <= epsilon) {
        return {};
    }
    std::vector<DirectCell> cells{{original, {0, 0, 0}}};
    for (const auto& segment : segments) {
        if (!overlay_segment(cells, source, segment)) {
            return {};
        }
    }
    return triangulate_cells(cells, source);
}

std::uint32_t mega_ground_transition_color(const MegaGroundTransitionVertex& vertex, const std::array<std::uint32_t, 3>& source_colors) noexcept {
    auto result = std::uint32_t(vertex.opacity) << 24;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        float value = 0;
        for (std::size_t index = 0; index < source_colors.size(); ++index) {
            value += float((source_colors[index] >> shift) & 255) * vertex.source_weights[index];
        }
        result |= std::uint32_t(std::clamp(std::lround(value), 0l, 255l)) << shift;
    }
    return result;
}
} // namespace toca::scene
