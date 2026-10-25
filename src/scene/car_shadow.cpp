#include "scene/car_shadow.hpp"

#include "simulation/fixed.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::scene {
namespace {
using namespace simulation::fixed;

// 434f00 initializes these four-point side profiles at4dadd0, stride0x30.
// The middle coordinate is zero for every car and is not read by437770.
constexpr std::array<std::array<std::array<std::int32_t, 2>, 4>, 10> profiles{{
    {{{0xf999, 0}, {0x3333, 0x8000}, {-0xfc28, 0x5999}, {-0x1b1eb, 0}}},
    {{{0xdeb8, 0}, {0x147a, 0x828f}, {-0xfae1, 0x828f}, {-0x1b5c2, 0}}},
    {{{0x11999, 0}, {0x51eb, 0x70a3}, {-0x10ccc, 0x547a}, {-0x1a666, 0}}},
    {{{0xd333, 0}, {0x4e14, 0x70a3}, {-0xf47a, 0x570a}, {-0x19ae1, 0}}},
    {{{0x110a3, 0}, {0x5d70, 0x8000}, {-0x1251e, 0x6147}, {-0x1f1eb, 0}}},
    {{{0xc3d7, 0}, {0x6000, 0x6147}, {-0x108f5, 0x4a3d}, {-0x17999, 0}}},
    {{{0xef5c, 0}, {0x71eb, 0x7333}, {-0xe000, 0x7333}, {-0x19333, 0}}},
    {{{0x107ae, 0}, {0x35c2, 0x6147}, {-0x12666, 0x68f5}, {-0x1b851, 0}}},
    {{{0xdced, 0}, {0x43d7, 0xbced}, {-0xe3d7, 0xcb43}, {-0x170a3, 0}}},
    {{{0x1451e, 0}, {0xfae1, 0x6666}, {-0x7d70, 0x6666}, {-0x10ccc, 0}}},
}};
} // namespace

CarShadowFootprint car_shadow_footprint(const CarShadowInput& input) {
    using namespace simulation::fixed;
    if (input.car_id >= profiles.size()) {
        throw std::runtime_error("Original car shadow profile index is outside0..9");
    }
    const auto& normal = input.support_normal;
    const auto alignment = dot(input.first_axis, normal);
    const auto across = normalized(project(abs(alignment) < 0xfd71 ? input.first_axis : input.fallback_axis, normal));
    const auto along = cross(across, normal);
    std::int32_t half_across = 0, half_along = 0;
    for (const auto& point : input.contact_points) {
        const auto relative = sub(point, input.support_position);
        half_across = std::max(half_across, dot(relative, across));
        half_along = std::max(half_along, dot(relative, along));
    }
    if (input.car_id == 9) {
        half_along = mul(half_along, 0x11999);
    }
    if (input.actor_index == 0 && input.special_state_18) {
        half_across = mul(half_across, 0x21999);
        half_along = mul(half_along, 0x16666);
    }
    const auto x = scale(across, half_across), z = scale(along, half_along);
    std::array<Vector, 4> rectangle{add(x, z), sub(x, z), sub(Vector{}, add(x, z)), sub(z, x)};
    const auto center = add(input.support_position, scale(normal, dot(sub(input.center_sample, input.support_position), normal)));
    const auto shrink = sub(one, std::min(mul(input.distance_q16, -0xccc), 0x9999));
    for (auto& point : rectangle) {
        point = add(center, scale(point, shrink));
    }
    CarShadowFootprint result;
    if (!input.directional) {
        std::copy(rectangle.begin(), rectangle.end(), result.points.begin());
        return result;
    }

    // Horizontal sunlight is first projected against the current ground normal,
    // normalized using the source midpoint-sqrt table, then extended0.75.
    const auto extension = scale(normalized(project(input.horizontal_light, input.current_ground_normal)), 0xc000);
    const auto light_across = dot(across, extension), light_along = dot(along, extension);
    unsigned base_start, translated_start, first_midpoint;
    bool reverse_height;
    if (light_across < 0 || (light_across == 0 && light_along > 0)) {
        if (light_along > 0) {
            base_start = 1;
            translated_start = 2;
            first_midpoint = 5;
            reverse_height = false;
        } else {
            base_start = 0;
            translated_start = 1;
            first_midpoint = 4;
            reverse_height = true;
        }
    } else if (light_along > 0) {
        base_start = 2;
        translated_start = 3;
        first_midpoint = 4;
        reverse_height = false;
    } else {
        base_start = 3;
        translated_start = 0;
        first_midpoint = 5;
        reverse_height = true;
    }
    for (unsigned index = 0; index < 4; ++index) {
        result.points[index] = rectangle[(base_start + index) % 4];
        if (index < 3) {
            result.points[index + 4] = add(rectangle[(translated_start + index) % 4], extension);
        }
    }
    const auto half_edge = scale(sub(result.points[first_midpoint], result.points[first_midpoint + 1]), 0x8000);
    const auto anchor = add(result.points[first_midpoint + 1], half_edge);
    const auto thickness = mul(length(half_edge), 0x3333);
    const auto height = dot(input.current_ground_normal, input.body_up);
    result.type = CarShadowType::one_side;
    if (height < 0x4ccd) {
        return result;
    }
    const auto height_scale = mul(mul(sub(height, 0x4ccc), 0x16dd2), shrink);
    const auto horizontal = normalized(Vector{dot(input.horizontal_light, across), 0, dot(input.horizontal_light, along)});
    const auto ratio = horizontal[0], magnitude = abs(ratio);
    const auto& profile = profiles[input.car_id];
    std::array<Vector, 4> outline;
    for (unsigned index = 0; index < outline.size(); ++index) {
        auto x_local = profile[index][0];
        if (index == 1 || index == 2) {
            const auto difference = ratio > 0 ? sub(profile[0][0], profile[1][0]) : sub(profile[3][0], profile[2][0]);
            x_local = add(x_local, mul(difference, magnitude));
        }
        x_local = mul(add(x_local, mul(thickness, ratio)), shrink);
        auto z_local = mul(profile[index][1], height_scale);
        z_local = sub(z_local, mul(z_local, magnitude));
        if (reverse_height) {
            z_local = neg(z_local);
        }
        outline[index] = add(anchor, add(scale(across, x_local), scale(along, z_local)));
    }
    // Preserve the original four sign cases and their shared edge positions.
    // These indices are consumed directly by498430's type2 silhouette fan.
    const auto old_five = result.points[5], old_six = result.points[6];
    if (ratio >= 0 && light_along <= 0) {
        result.points[10] = old_six;
        std::copy(outline.begin(), outline.end(), result.points.begin() + 6);
    } else if (ratio <= 0 && light_along <= 0) {
        result.points[9] = old_five;
        result.points[10] = old_six;
        std::copy(outline.begin(), outline.end(), result.points.begin() + 5);
    } else if (ratio <= 0 && light_along >= 0) {
        result.points[10] = old_six;
        std::reverse_copy(outline.begin(), outline.end(), result.points.begin() + 6);
    } else {
        result.points[9] = old_five;
        result.points[10] = old_six;
        std::reverse_copy(outline.begin(), outline.end(), result.points.begin() + 5);
    }
    result.type = CarShadowType::two_sides;
    return result;
}
} // namespace toca::scene
