#include "scene/shadow_support.hpp"

#include "scene/catalog.hpp"
#include "simulation/fixed.hpp"

#include <stdexcept>

namespace toca::scene {
CarShadowInput car_shadow_input(const BodyPose& body, const SceneSetup& setup) {
    using namespace simulation::fixed;
    if (!track_supported(setup.selection.track_id) || setup.weather < 1 || setup.weather > 5) {
        throw std::runtime_error("Car shadow requires a supported track and resolved weather");
    }
    // 44eda9's authored X/Z light positions, then its <<5 world scaling.
    // Preserve the original fixed-point normalization, including wrapped squares.
    constexpr std::array<std::array<std::int32_t, 2>, 10> light_positions{{
        {50, 75},
        {100, -40},
        {75, 75},
        {-60, -45},
        {-75, -125},
        {50, 75},
        {100, 55},
        {-175, 110},
        {-75, 100},
        {1, 1},
    }};
    const auto& light = light_positions[setup.selection.track_id];
    CarShadowInput input;
    input.support_normal = body.ground_normal;
    input.support_position = body.position;
    input.contact_points = body.contacts;
    input.first_axis = body.basis[0];
    input.fallback_axis = body.basis[1];
    // Snapshot440 is height5, not a body coordinate; 4a0/4a8 are contact5 X/Z.
    input.center_sample = {body.contacts[5][0], body.surfaces[5].height, body.contacts[5][2]};
    input.current_ground_normal = body.ground_normal;
    input.body_up = body.basis[1];
    input.horizontal_light = scale(normalized(Vector{light[0] * 32 * one, 0, light[1] * 32 * one}), -one);
    input.car_id = setup.selection.car_id;
    input.directional = setup.weather <= 2;
    // 4dbe74 has no producer in1.1a and starts zero. Airborne projection uses
    // the sampled ground and retained support normal, not an invented shrink.
    return input;
}
} // namespace toca::scene
