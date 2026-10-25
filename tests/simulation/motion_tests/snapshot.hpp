#pragma once

#include "fixtures.hpp"

namespace toca::test::motion {
using namespace toca::simulation;

// Warm source aliases are authored in the fixture, never produced by the publisher under test.
class SnapshotFixture {
    const HpcSurface floor{driving_plane(0)};
    DamageContext damage{floor};
    SurfaceRoughness live_roughness, replay_roughness;
    const scene::BodyPose body = scene::initial_body_pose(floor, {{-60 * unit, 50 * unit}, 0, 0}, 0);
    Motion live{floor, body, 0, true, 1, false, damage, live_roughness};
    Motion replay{floor, body, 0, true, 1, false, damage, replay_roughness};
    SourceSnapshot snapshot;
    static constexpr TimeStep half_step{0x8000, 0x20000};
    SourceSnapshot distant_seed() const;

public:
    SnapshotFixture();
    void damage_feedback();
    void handoff_launch();
    void barrier_moment();
    void support_publication();
    void continued_inputs();
    void prepare_continuation();
};

} // namespace toca::test::motion
