#include "simulation/prop_contacts.hpp"
#include "snapshot.hpp"

#include <algorithm>

namespace toca::test::motion {
using namespace toca::simulation;

void SnapshotFixture::barrier_moment() {
    const auto distant_snapshot = distant_seed();
    //444830 discards old force/moment before409c30, but retains the new
    //4095b0 moment through444ee0. Both modes dispatch419ad0 and impact audio.
    Bytes wall_points(96);
    put32(wall_points, 4, 1);
    put32(wall_points, 16, -16 * unit);
    put32(wall_points, 28, 16 * unit);
    wall_points[8 + 0x55] = 1;
    constexpr std::array<std::array<std::int32_t, 2>, 4> sectors{{{-32 * unit, 32 * unit}, {32 * unit, 32 * unit}, {-32 * unit, -32 * unit}, {32 * unit, -32 * unit}}};
    const toca::scene::BarrierData wall(wall_points, sectors);
    const auto wall_body = toca::scene::initial_body_pose(floor, {{-toca::scene::car_half_dimensions(0)[0] - unit / 32, 0}, 0, 0}, 0);
    auto wall_snapshot = distant_snapshot;
    wall_snapshot.has_damage = false;
    const auto wall_vector = [&](std::size_t offset, const fixed::Vector& values) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            word(wall_snapshot.body, offset + axis * 4, values[axis]);
        }
    };
    wall_vector(0xbc, wall_body.position);
    wall_vector(0xc8, {8 * unit, 0, 0});
    wall_vector(0xe4, {});
    wall_vector(0xb0, {0, unit, 0});
    wall_vector(0x1e4, {0, unit, 0});
    //These old contact impulses must not contribute to a distant update.
    wall_vector(0x1f0, {123, -456, 789});
    wall_vector(0x1fc, {-321, 654, -987});
    word(wall_snapshot.body, 0xe0, 8 * unit);
    wall_snapshot.dynamics[0x330] = 1;
    for (std::size_t i = 0; i < 3; ++i) {
        wall_vector(0x150 + i * 12, wall_body.basis[i]);
    }
    for (std::size_t i = 0; i < 8; ++i) {
        wall_vector(0xf0 + i * 12, wall_body.contacts[i]);
        word(wall_snapshot.body, 0x1c4 + i * 4, wall_body.surfaces[i].height);
        const auto triangle = wall_body.surfaces[i].triangle;
        wall_snapshot.dynamics[0x264 + i * 2] = std::uint8_t(triangle);
        wall_snapshot.dynamics[0x265 + i * 2] = std::uint8_t(triangle >> 8);
    }
    for (const bool promote : {false, true}) {
        DamageContext wall_damage(floor), free_damage(floor);
        SurfaceRoughness wall_roughness, free_roughness;
        Motion collision(floor, wall_body, 0, false, 1, false, wall_damage, wall_roughness, &wall);
        Motion unobstructed(floor, wall_body, 0, false, 1, false, free_damage, free_roughness);
        collision.restore_source_snapshot(wall_snapshot);
        collision.set_actor_identity(5, false);
        unobstructed.restore_source_snapshot(wall_snapshot);
        collision.update_source_distant({}, half_step, true);
        unobstructed.update_source_distant({}, half_step, true);
        auto expected_body = unobstructed.state().body;
        auto expected_velocity = unobstructed.state().velocity_q16;
        BarrierRecovery recovery;
        const auto response = resolve_barriers(wall, expected_body, expected_velocity, wall_body.position, 0, recovery, false);
        expect(response.contacts != 0 && response.angular_impulse_q16 != fixed::Vector{} && response.damage_contacts.size() == 1,
            "The distant regression must exercise an ordinary wall contact with both a raw moment and damage request");
        expect(collision.state().body.position == expected_body.position && collision.state().velocity_q16 == expected_velocity
                   && collision.source_pending_moment() == response.angular_impulse_q16 && collision.source_pending_acceleration() == fixed::Vector{},
            "444830 must discard old impulses before barriers and preserve the exact new raw moment independently of damage/audio requests");
        DamageState expected_damage;
        DamageInput damage_input;
        damage_input.contact = response.damage_contacts.front();
        for (std::size_t axis = 0; axis < 3; ++axis) {
            damage_input.inverse_half_dimensions_q16[axis] = fixed::reciprocal(toca::scene::car_half_dimensions(0)[axis]);
        }
        damage_input.feedback_enabled = false;
        apply_damage(expected_damage, damage_input);
        expect(wall_damage.cycle() == expected_damage.cycle && collision.state().damage.levels != DamageState{}.levels && !collision.state().impacts.empty()
                   && collision.state().impacts.back().kind == 1 && collision.state().impacts.back().strength_q16 > 0 && !collision.state().impacts.back().local_player,
            "Distant barriers must use the shared damage cycle, panel selection and owned impact publication just like full bodies");
        const auto event_count = collision.state().impacts.size();
        collision.finish_ground(BodyMode::distant);
        expect(collision.state().boundary == MotionBoundary::none && collision.source_pending_moment() == response.angular_impulse_q16
                   && collision.state().impact_count == event_count && wall_damage.cycle() == expected_damage.cycle,
            "444ee0 must retain the pending raw barrier moment and publish its impacts once without repeating damage");
        collision.set_barrier_contacts_enabled(false);
        if (promote) {
            DamageContext control_damage(floor);
            SurfaceRoughness control_roughness;
            Motion without_moment(floor, wall_body, 0, false, 1, false, control_damage, control_roughness, &wall);
            without_moment.restore_source_snapshot(wall_snapshot);
            without_moment.set_actor_identity(5, false);
            without_moment.update_source_distant({}, half_step, true);
            without_moment.finish_ground(BodyMode::distant);
            expect(without_moment.state().body.position == collision.state().body.position && without_moment.state().body.basis == collision.state().body.basis
                       && without_moment.state().velocity_q16 == collision.state().velocity_q16
                       && without_moment.state().angular_velocity_q16 == collision.state().angular_velocity_q16
                       && without_moment.source_pending_moment() == collision.source_pending_moment(),
                "The promotion control must reach identical post-wall physics before removing only the delayed moment");
            //Cancel the total raw moment through the existing additive contact
            //API. No position, velocity, wheel or control state changes here.
            without_moment.apply_prop_response({.torque_q16 = fixed::scale(response.angular_impulse_q16, -unit)});
            expect(without_moment.source_pending_moment() == fixed::Vector{}, "The promotion control must have no retained moment");
            without_moment.set_barrier_contacts_enabled(false);
            without_moment.update_source_input({}, half_step, true);
            collision.update_source_input({}, half_step, true);
            expect(collision.state().angular_velocity_q16 != without_moment.state().angular_velocity_q16 && collision.source_pending_moment() == fixed::Vector{}
                       && without_moment.source_pending_moment() == fixed::Vector{},
                "The first full40caf0 must consume the distant barrier moment: identical ordinary forces without that moment must produce a different angular velocity");
        } else {
            collision.update_source_distant({}, half_step, true);
            expect(collision.source_pending_moment() == fixed::Vector{} && collision.source_pending_acceleration() == fixed::Vector{},
                "A consecutive distant update must discard its predecessor's unconsumed wall moment before new barriers");
        }
        expect(wall_damage.cycle() == expected_damage.cycle && collision.state().impacts.empty() && collision.state().impact_count == event_count,
            "A later contact-free physics pass must not repeat the previous wall damage or audio event");
    }
}

void SnapshotFixture::support_publication() {
    const auto distant_snapshot = distant_seed();
    for (const bool full : {false, true}) {
        for (const auto speed : {-0x2555, 0x2555}) {
            auto signed_snapshot = full ? snapshot : distant_snapshot;
            word(signed_snapshot.body, 0xe0, speed);
            signed_snapshot.dynamics[0x330] = speed < 0 ? 7 : 1;
            SurfaceRoughness signed_roughness;
            Motion signed_motion(floor, body, 0, full, 1, false, damage, signed_roughness);
            signed_motion.restore_source_snapshot(signed_snapshot);
            expect(signed_motion.ai_traffic_state().speed_q16 == fixed::mul(speed, 0x640000),
                "The native AI traffic bridge must retain the sign of body+e0 times100 independently of the physical dispatch mode");
            if (full) {
                signed_motion.update_source_input({}, half_step, true);
            } else {
                signed_motion.update_source_distant({}, half_step, true);
            }
            expect(signed_motion.state().boundary == MotionBoundary::none && signed_motion.state().speed_integer == (speed < 0 ? -15 : 14),
                "Both dispatch paths must publish signed retained body speed times100 with arithmetic-shift rounding before integrating this tick");
        }
    }
    SurfaceRoughness distant_roughness;
    Motion distant(floor, body, 0, false, 1, false, damage, distant_roughness);
    distant.restore_source_snapshot(distant_snapshot);
    const auto retained_samples = distant.state().body.surfaces;
    const auto retained_support = distant.state().wheel_contact;
    distant.update_source_distant({.throttle_q16 = 0x8000}, half_step, true);
    const auto integrated_x = distant.state().body.position[0];
    distant.finish_ground(BodyMode::distant);
    expect(distant.state().boundary == MotionBoundary::none && distant.state().contact_count == 4 && distant.state().wheel_contact == retained_support
               && distant.state().body.position[0] == integrated_x && distant.state().body.position[1] == 2 * unit + toca::scene::car_half_dimensions(0)[1],
        "Real mapped distant integration/ground must preserve source count4 and support flags while placing the body on the actual centre plane");
    for (std::size_t i = 0; i < 8; ++i) {
        expect((i == 5 || distant.state().body.surfaces[i].height == retained_samples[i].height)
                   && (i == 0 || distant.state().body.surfaces[i].triangle == retained_samples[i].triangle),
            "444ee0 retains other full-body heights and non-centre caches while writing its aliased height5");
    }
    expect(distant.state().body.surfaces[5].height == distant.state().body.contacts[1][1],
        "444ee0 body+1d8 aliases the height5 slot with the lowest signed corner expression before40c280");
    {
        auto published_seed = distant_snapshot;
        //Distant actors retain independent support flags/list/count and the
        //last full-ground patch offsets, including unused contact-list bytes.
        std::fill_n(published_seed.body.begin() + 0x517, 8, 0);
        for (std::size_t index = 4; index < 8; ++index) {
            published_seed.body[0x521 + index] = std::uint8_t(11 - index);
        }
        word(published_seed.dynamics, 0x2bc, 1);
        for (std::size_t offset = 0x240; offset < 0x264; offset += 4) {
            word(published_seed.dynamics, offset, std::int32_t(offset));
        }
        SurfaceRoughness publication_roughness;
        Motion publication(floor, body, 0, false, 1, false, damage, publication_roughness);
        publication.restore_source_snapshot(published_seed);
        publication.update_source_distant({.throttle_q16 = 0x8000}, half_step, true);
        publication.finish_ground(BodyMode::distant);
        SourceSnapshot published;
        published.body.fill(0xa5);
        published.dynamics.fill(0xa5);
        published.gameplay.fill(0xa5);
        const auto driver_gameplay = published.gameplay;
        publication.publish_source_physical_state(published);
        expect(published.body[0x51f] == 4 && published.body[0x520] == 0
                   && std::equal(published.body.begin() + 0x517, published.body.begin() + 0x51f, published_seed.body.begin() + 0x517)
                   && std::equal(published.body.begin() + 0x521, published.body.begin() + 0x529, published_seed.body.begin() + 0x521)
                   && std::equal(published.dynamics.begin() + 0x240, published.dynamics.begin() + 0x264, published_seed.dynamics.begin() + 0x240),
            "Grouped distant publication must preserve independent stale supports, unused index bytes and full-ground suspension offsets");
        expect(published.body[0x510] == 0xa5 && published.body[0x515] == 0xa5 && published.body[0x529] == 0xa5 && published.dynamics[0x27c] == 0xa5
                   && published.dynamics[0x330] == 0xa5 && source_u32(published.dynamics, 0x140) == 0xa5a5a5a5 && published.gameplay == driver_gameplay,
            "Physical publication must preserve adjacent scene/configuration fields and generated driver controls");
        publication.add_car_contact({0x123, 0x234, 0x345}, {0x456, 0x567, 0x678});
        publication.publish_source_physical_state(published);
        expect(source_i32(published.body, 0x1f0) == 0x123 && source_i32(published.body, 0x204) == 0x678,
            "Grouped publication exposes contacts' delayed force and moment before their following integration");
        publication.begin_tick();
        publication.update_source_input({.throttle_q16 = 0x8000}, half_step, true);
        publication.publish_source_physical_state(published);
        expect(source_i32(published.body, 0x1f0) == 0 && source_i32(published.body, 0x204) == 0 && published.body[0x51b] == 0,
            "Full integration consumes delayed contact forces while retaining the preceding support cache until ground");
        publication.finish_ground(BodyMode::full);
        publication.publish_source_physical_state(published);
        expect(publication.state().boundary == MotionBoundary::none && published.body[0x51b] == 1 && published.body[0x528] == published_seed.body[0x528]
                   && source_i32(published.dynamics, 0x240) != 0x240 && source_i32(published.dynamics, 0x2bc) == 1,
            "Promotion ground refreshes support/patch state while retaining the untouched contact-list tail");
    }
    //A real first full pass after distant motion retains eight zero flags but
    //only four stored indices. Flags still gate wheels/body friction; the
    //independent count/list gates force limits and integrated contact moments.
    auto consistent_snapshot = distant_snapshot;
    word(consistent_snapshot.body, 0x4f0, 0);
    for (std::size_t i = 4; i < 8; ++i) {
        consistent_snapshot.body[0x517 + i] = 1;
    }
    auto stale_snapshot = consistent_snapshot;
    std::fill_n(stale_snapshot.body.begin() + 0x517, 8, 0);
    SurfaceRoughness consistent_roughness, stale_roughness;
    Motion consistent(floor, body, 0, false, 1, false, damage, consistent_roughness);
    Motion stale(floor, body, 0, false, 1, false, damage, stale_roughness);
    consistent.restore_source_snapshot(consistent_snapshot);
    stale.restore_source_snapshot(stale_snapshot);
    const SourceInput promoted_input{.throttle_q16 = 0x8000, .right_q16 = 0x4000};
    consistent.update_source_input(promoted_input, half_step, true);
    stale.update_source_input(promoted_input, half_step, true);
    expect(stale.state().body.position == consistent.state().body.position && stale.state().velocity_q16 == consistent.state().velocity_q16
               && stale.state().angular_velocity_q16 == consistent.state().angular_velocity_q16 && stale.state().wheel_speed_q16 == consistent.state().wheel_speed_q16,
        "40be80/40caf0 must consume the four retained indices on first promotion while eight stale flags remain independently available to wheel/friction consumers");
}

} // namespace toca::test::motion
