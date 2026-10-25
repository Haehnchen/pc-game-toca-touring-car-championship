#pragma once

#include "fixtures.hpp"

namespace toca::test::motion {
using namespace toca::simulation;

// Each check uses the same floor/damage/roughness in the complete original sequence.
class BodyFixture {
    const HpcSurface floor{plane()};
    DamageContext damage{floor};
    SurfaceRoughness roughness;
    const scene::HorizontalPlacement start{{-24 * unit, -24 * unit}, 0, 0};
    const scene::BodyPose body = scene::initial_body_pose(floor, start, 0);

public:
    void coverage();
    void flight_landing();
    void drivetrain();
    void barriers_materials();
};

} // namespace toca::test::motion
