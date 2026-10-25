#include "scene/car_shadow.hpp"
#include "scene/shadow_support.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca::scene;
using namespace toca::simulation::fixed;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

CarShadowInput fixture() {
    CarShadowInput input;
    input.support_normal = input.current_ground_normal = input.body_up = {0, one, 0};
    input.first_axis = {one, 0, 0};
    input.fallback_axis = {0, one, 0};
    input.horizontal_light = {-40000, 0, -40000};
    for (unsigned i = 0; i < input.contact_points.size(); ++i) {
        input.contact_points[i] = {i & 1 ? 2 * one : -2 * one, i & 2 ? one / 2 : -one / 2, i & 4 ? 3 * one / 2 : -3 * one / 2};
    }
    return input;
}

void core() {
    auto input = fixture();
    const auto base = car_shadow_footprint(input);
    expect(base.type == CarShadowType::rectangle, "Wet source shadow must retain its type0 footprint");
    for (unsigned i = 0; i < 4; ++i) {
        expect(base.points[i][1] == 0 && std::abs(std::abs(base.points[i][0]) - 2 * one) < 128 && std::abs(std::abs(base.points[i][2]) - 3 * one / 2) < 128,
            "Source shadow must project the actual contact box onto its support plane");
    }
    input.first_axis = {0, one, 0};
    input.fallback_axis = {one, 0, 0};
    expect(car_shadow_footprint(input).points == base.points, "Near-vertical first axis must select the actual fallback body-up axis");
    input = fixture();
    input.support_normal = input.current_ground_normal = normalized(Vector{-one / 4, one, 0});
    const auto sloped = car_shadow_footprint(input);
    for (unsigned index = 0; index < 4; ++index) {
        expect(
            std::abs(dot(sloped.points[index], input.support_normal)) < 512, "All projected footprint corners must retain their sloped support plane with original fixed rounding");
    }
    input = fixture();
    input.directional = true;
    expect(car_shadow_footprint(input).type == CarShadowType::two_sides, "Upright dry car must use its original extended silhouette");
    input.body_up[1] = 0x4ccc;
    expect(car_shadow_footprint(input).type == CarShadowType::one_side, "Original type1 threshold must select the side strip below0x4ccd");
    input.body_up[1] = 0x4ccd;
    expect(car_shadow_footprint(input).type == CarShadowType::two_sides, "Original type2 threshold must include0x4ccd");
}

void recovered_records() {
    auto input = fixture();
    input.directional = true;
    // Independently executed437770 decompilation, with the input above.
    // Preserve every consumed position, including the source edge order.
    constexpr std::array<ShadowVector, 11> silhouette{{{131016, 0, 98262}, {131016, 0, -98262}, {-131016, 0, -98262}, {-131016, 0, 98262}, {96258, 0, -133020}, {10600, 0, -133020},
        {-73076, 0, -142618}, {-150719, 0, -139739}, {-164343, 0, -133020}, {-165774, 0, -133020}, {-165774, 0, 63504}}};
    expect(car_shadow_footprint(input).points == silhouette, "Original Honda silhouette record must retain all eleven fixed-point positions");
    input.body_up[1] = 0x4ccc;
    const auto strip = car_shadow_footprint(input);
    expect(strip.points[4] == ShadowVector{96258, 0, -133020} && strip.points[5] == ShadowVector{-165774, 0, -133020} && strip.points[6] == ShadowVector{-165774, 0, 63504},
        "Type1 must retain the source's three translated edge points in source order");
    input = fixture();
    input.car_id = 9;
    expect(car_shadow_footprint(input).points[0] == ShadowVector{131016, 0, 108086}, "Bonus car9 must retain its original wider support footprint");
    input.car_id = 0;
    input.special_state_18 = true;
    expect(car_shadow_footprint(input).points[0] == ShadowVector{275132, 0, 137565}, "Source actor0 state18 footprint scaling changed");
    input.actor_index = 1;
    input.special_state_18 = false;
    const auto ordinary = car_shadow_footprint(input);
    input.special_state_18 = true;
    expect(car_shadow_footprint(input).points == ordinary.points, "Source state18 scaling must apply only to actor0");
}

void adapter() {
    BodyPose body{};
    body.position = {7 * one, 3 * one, -2 * one};
    body.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    body.ground_normal = {0, one, 0};
    body.contacts = fixture().contact_points;
    for (auto& point : body.contacts) {
        point = add(point, body.position);
    }
    for (auto& sample : body.surfaces) {
        sample.height = 0;
        sample.normal_ratio = {0, one, 0};
    }
    auto setup = resolve_setup({.weather_choice = 1});
    const auto input = car_shadow_input(body, setup);
    expect(input.support_position == body.position && input.support_normal == body.ground_normal && input.contact_points == body.contacts && input.first_axis == body.basis[0]
               && input.fallback_axis == body.basis[1] && input.current_ground_normal == body.ground_normal && input.body_up == body.basis[1]
               && input.center_sample == ShadowVector{body.contacts[5][0], 0, body.contacts[5][2]} && input.distance_q16 == 0,
        "4608b0 snapshots must retain actual body axes, contact5 ground height and cached support normal");
    const auto grounded = car_shadow_footprint(input);
    body.position[1] += 5 * one;
    for (auto& point : body.contacts) {
        point[1] += 5 * one;
    }
    const auto airborne = car_shadow_footprint(car_shadow_input(body, setup));
    expect(grounded.points == airborne.points && grounded.type == airborne.type, "Airborne car shadow must stay on its actual ground plane with dormant distance scalar zero");
    setup.weather = 3;
    expect(!car_shadow_input(body, setup).directional, "Rain must select the original rectangular shadow");
    // 44eda9 scales lightX/Z<<5 after49da90;434f00 normalizes those raw
    // coordinates with wrapped32-bit fixed squares, not a unit float vector.
    constexpr std::array<std::array<std::int32_t, 2>, 10> sunlight{{{-419200, -628800}, {-1635200, 654080}, {-693600, -693600}, {520320, 390240}, {3472800, 5788000},
        {-419200, -628800}, {-1139200, -626560}, {1881600, -1182720}, {1636800, -2182400}, {-46304, -46304}}};
    for (unsigned track = 0; track < sunlight.size(); ++track) {
        setup = resolve_setup({.track_id = track, .weather_choice = 1});
        const auto actual = car_shadow_input(body, setup).horizontal_light;
        expect(actual == ShadowVector{sunlight[track][0], 0, sunlight[track][1]}, "Original scaled light coordinates and wrapped fixed normalization changed");
    }
}
} // namespace

int main() {
    try {
        core();
        recovered_records();
        adapter();
        std::cout << "PASS: original shadow types, support snapshots and airborne ground anchoring\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
