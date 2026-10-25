#include "scene/prop_fragments.hpp"

#include "core/random.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
namespace {
using namespace simulation::fixed;
constexpr std::array<std::array<Vector, 3>, 5> shapes{{
    {{{5668, -3244, -6225}, {1717, 23258, -1507}, {-6048, -14974, 7733}}},
    {{{9692, -3244, 6055}, {-4849, 20532, -3027}, {-4849, -20532, -3027}}},
    {{{4712, 1723, 570}, {-11593, 5321, -2994}, {7385, -11010, 2418}}},
    {{{1723, -1146, -2444}, {6848, 7785, -4639}, {-4731, -7510, 7084}}},
    {{{5629, -766, 340}, {-7589, 8447, 5983}, {2457, -11645, -6324}}},
}};

std::int32_t fraction(SourceRandom& random) {
    return std::int32_t(std::int64_t(random.next()) * one / 32767);
}

void advance(PropFragmentTables& tables) {
    tables.cursor = (tables.cursor + 1) % 37;
}

std::uint32_t color(std::uint8_t index, std::uint32_t weather, std::uint8_t car = 10) {
    // 4153c0's twelve three-color palettes at4bea68, including sign10.
    constexpr std::array<std::array<std::uint32_t, 3>, 12> colors{{{0xbdbdd6, 0x180000, 0xce050a}, {0xb5bdce, 0x84949c, 0x181818}, {0xde1010, 0xce0008, 0xc60000},
        {0xd6d6d6, 0x313984, 0xd6b308}, {0x252c83, 0x315294, 0x39317b}, {0xceced6, 0xceced6, 0xaa0f15}, {0xc5ad5c, 0x00643a, 0x00331b}, {0xf7c600, 0x1029a5, 0xf7d608},
        {0x98815d, 0x3f3f0e, 0x0f0f00}, {0xf77b8c, 0xebdece, 0xf7b5c6}, {0xdde7ef, 0xcfd9e0, 0xc1cad1}, {0xf7f7f7, 0xd6d6de, 0xe7dee7}}};
    constexpr std::array<std::int32_t, 6> gains{0, 0xcccc, 0xc000, 0xb333, 0xb333, 0x8000};
    std::uint32_t result = 0xff000000;
    // The4153c0 modulation loop ends at4beaed: the final Vectra2 palette11
    // follows that interval and retains its original unscaled RGB bytes.
    const auto gain = car == 11 ? one : gains.at(weather);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const auto shift = (2 - channel) * 8;
        result |= std::uint32_t((std::int32_t((colors.at(car).at(index) >> shift) & 255) * gain) >> 16) << shift;
    }
    return result;
}
} // namespace

PropFragmentTables make_prop_fragment_tables(SourceRandom& random) {
    PropFragmentTables result;
    for (std::size_t index = 0; index < result.positive.size(); ++index) {
        result.positive[index] = add(mul(fraction(random), 0xcccc), 0x1999);
        result.signed_values[index] = add(mul(fraction(random), 0xcccc), 0x1999);
        if ((random.next() & 1) == 0) {
            result.signed_values[index] = neg(result.signed_values[index]);
        }
    }
    for (std::size_t index = 0; index < result.color_indices.size(); ++index) {
        result.counts[index] = std::uint8_t(add(mul(fraction(random), 0x90000), one) >> 16);
        result.color_indices[index] = std::uint8_t(std::min(mul(fraction(random), 0x30000) >> 16, 2));
    }
    return result;
}

PropFragments::PropFragments(PropFragmentTables& tables, std::uint32_t weather, bool low_gravity)
    : tables_(tables), weather_(weather), gravity_(low_gravity ? 0xc4 : 0x7ae), impact_gravity_(low_gravity ? 0xd : 0x83) {
    if (weather < 1 || weather > 5 || tables.cursor >= 37 || std::ranges::any_of(tables.color_indices, [](auto value) {
            return value > 2;
        })) {
        throw std::runtime_error("Sign fragments require original weather and cyclic random tables");
    }
}

std::optional<PropSoundEvent> PropFragments::break_sign(const HorizonTransform& root, const Vector& vehicle_velocity) {
    const auto count = std::min(std::size_t(37), std::size_t(63) - fragments_.size());
    if (count == 0) {
        return std::nullopt;
    }
    const auto grid = (sqrt(std::uint32_t(count * one)) + one - 1) / one;
    const Vector axis{root[0], root[1], root[2]};
    auto origin = add(Vector{root[12], root[13], root[14]}, scale(axis, -0xe667));
    origin[1] = add(origin[1], 0x1ccce);
    auto current = origin;
    const auto horizontal_step = scale(scale(axis, 0x1ccce), reciprocal(grid * one));
    const auto vertical_step = mul(reciprocal(grid * one), -0x1ccce);
    const auto velocity = Vector{vehicle_velocity[0], 0, vehicle_velocity[2]};
    const auto speed = length(velocity);
    if (speed > 0) {
        const auto lift = mul(speed, 0x9999);
        const auto direction = scale(velocity, reciprocal(speed));
        const auto lateral = scale(Vector{neg(direction[2]), 0, direction[0]}, mul(speed, 0x6666));
        std::int32_t column = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const auto cursor = tables_.cursor;
            Fragment fragment;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                fragment.triangle.vertices[corner] = add(current, scale(shapes[cursor % 5][corner], mul(tables_.signed_values[cursor], 0x2cccc)));
            }
            fragment.velocity = scale(velocity, add(tables_.positive[tables_.cursor], 0x1999));
            advance(tables_);
            fragment.velocity[1] = add(fragment.velocity[1], mul(lift, tables_.positive[tables_.cursor]));
            advance(tables_);
            fragment.velocity = add(fragment.velocity, scale(lateral, tables_.signed_values[tables_.cursor]));
            fragment.triangle.color = color(tables_.color_indices[tables_.cursor], weather_);
            fragment.initial_y = fragment.triangle.vertices[0][1];
            fragment.previous_triangle = fragment.triangle;
            fragments_.push_back(fragment);
            advance(tables_);
            if (++column == grid) {
                origin[1] = add(origin[1], vertical_step);
                current = origin;
                column = 0;
            } else {
                current = add(current, horizontal_step);
            }
        }
    }
    return PropSoundEvent{current, 0x15, std::min(speed, one), true};
}

void PropFragments::impact(const ImpactFragmentRequest& request) {
    if (request.intensity_q16 <= 0x3333 || fragments_.size() == 63) {
        return;
    }
    if (request.intensity_q16 > one || request.car_ids[0] > 11 || request.car_ids[1] > 11) {
        throw std::runtime_error("Impact fragments require original intensity and car palettes");
    }
    std::size_t count = 0;
    for (auto remaining = request.intensity_q16 / 0x3333; remaining != 0; --remaining) {
        advance(tables_);
        count += tables_.counts[tables_.cursor];
    }
    count = std::min(count, std::size_t(63) - fragments_.size());
    const auto strength = mul(request.intensity_q16, 0x4000);
    for (std::size_t index = 0; index < count; ++index) {
        const auto cursor = tables_.cursor;
        const auto speed = mul(strength, add(tables_.positive[cursor], 0x1999));
        Fragment fragment;
        for (std::size_t corner = 0; corner < 3; ++corner) {
            fragment.triangle.vertices[corner] = add(request.position, scale(shapes[cursor % 5][corner], 0x5999));
        }
        fragment.velocity[0] = mul(tables_.signed_values[cursor], speed);
        fragment.velocity[1] = mul(tables_.positive[cursor], speed);
        advance(tables_);
        fragment.velocity[2] = mul(tables_.signed_values[tables_.cursor], speed);
        fragment.velocity = add(fragment.velocity, request.velocity);
        const auto car = request.car_ids[tables_.positive[tables_.cursor] < 0x8000 ? 0 : 1];
        fragment.triangle.color = color(tables_.color_indices[tables_.cursor], weather_, car);
        fragment.initial_y = fragment.triangle.vertices[0][1];
        fragment.remaining = 25;
        fragment.impact = true;
        fragment.previous_triangle = fragment.triangle;
        fragments_.push_back(fragment);
        advance(tables_);
    }
}

void PropFragments::update() {
    std::erase_if(fragments_, [&](Fragment& fragment) {
        if (fragment.remaining == 0 || sub(fragment.triangle.vertices[0][1], fragment.initial_y) < -0x1998) {
            return true;
        }
        fragment.previous_triangle = fragment.triangle;
        for (auto& vertex : fragment.triangle.vertices) {
            vertex = add(vertex, fragment.velocity);
        }
        fragment.velocity[1] = sub(fragment.velocity[1], fragment.impact ? impact_gravity_ : gravity_);
        fragment.velocity[0] = sub(fragment.velocity[0], mul(fragment.velocity[0], 0xa3d));
        fragment.velocity[2] = sub(fragment.velocity[2], mul(fragment.velocity[2], 0xa3d));
        --fragment.remaining;
        return false;
    });
}

void PropFragments::clear() {
    fragments_.clear();
}

std::span<const PropFragmentTriangle> PropFragments::triangles() const {
    triangles_.clear();
    // 417cd0 prepends each allocation;418ac0 presents that linked-list order.
    for (auto fragment = fragments_.rbegin(); fragment != fragments_.rend(); ++fragment) {
        triangles_.push_back(fragment->triangle);
    }
    return triangles_;
}

std::vector<PropFragmentTriangle> PropFragments::presentation_triangles(float alpha) const {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Fragment presentation requires a finite fraction");
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    std::vector<PropFragmentTriangle> result;
    result.reserve(fragments_.size());
    for (auto fragment = fragments_.rbegin(); fragment != fragments_.rend(); ++fragment) {
        auto triangle = fragment->triangle;
        if (alpha != 1) {
            for (std::size_t vertex = 0; vertex < 3; ++vertex) {
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    triangle.vertices[vertex][axis] =
                        std::int32_t(std::llround(std::lerp(double(fragment->previous_triangle.vertices[vertex][axis]), double(triangle.vertices[vertex][axis]), double(alpha))));
                }
            }
        }
        result.push_back(triangle);
    }
    return result;
}

std::size_t PropFragments::size() const {
    return fragments_.size();
}
} // namespace toca::scene
