#include "simulation/motion.hpp"

#include "scene/car_light_glow.hpp"
#include "scene/engine_effects.hpp"
#include "scene/surface_effects.hpp"
#include "simulation/fixed.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/source_snapshot.hpp"
#include "simulation/source_transform.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>

namespace toca::simulation {
namespace {
using namespace fixed;
constexpr Vector gravity{0, -0x8000, 0};
constexpr std::array<std::int32_t, 8> ratios{0, 0x2bd70, 0x20f5c, 0x1a3d7, 0x14f5c, 0x11eb8, 0x10000, -0x2e147};
constexpr std::array<std::int32_t, 10> brake_split{0x9999, 0x8ccc, 0xcccc, 0xa666, 0xe666, 0x8ccc, 0xa666, 0xa666, 0x8000, 0x9999};
constexpr std::array<std::int32_t, 4> lateral_shape{0x40000, 0x40000, 0x18000, 0x40000};
constexpr std::array<std::int32_t, 4> longitudinal_shape{0x41999, 0x3cccc, 0x18000, 0x40000};
constexpr std::array<std::int32_t, 4> slip_factor{0, 0, 0, 0xcccc};
constexpr std::array<std::int32_t, 4> rolling_drag{0, 0, 0x1999, 0xcccc};
constexpr std::array<std::array<std::int32_t, 4>, 3> lateral_friction{{{0x28f5, 0x28f5, 0x1eb8, 0x3333}, {0x2666, 0x2666, 0x1c29, 0x30a4}, {0x23d7, 0x23d7, 0x199a, 0x2e15}}};
constexpr std::array<std::array<std::int32_t, 4>, 3> longitudinal_friction{{{0x28f5, 0x28f5, 0x1999, 0x23d7}, {0x2666, 0x2666, 0x170a, 0x2148}, {0x23d7, 0x23d7, 0x147b, 0x1eb9}}};

bool opposite(std::int32_t a, std::int32_t b) {
    return (a > 0 && b < 0) || (a < 0 && b > 0);
}

std::int32_t limited(std::int32_t value, std::int32_t limit) {
    return std::clamp(value, neg(limit), limit);
}

Vector tangent_with_negative_product(const Vector& value, const Vector& normal) {
    // 412040/40db10/40d830 add the separately rounded negative product. Subtracting
    // a positive product can cross a square-root lookup bucket on slopes.
    return add(value, scale(normal, neg(dot(value, normal))));
}

std::uint8_t surface_class(std::uint16_t flags) {
    // 42e6f0: the water-marked branch retains only the low seven bits;
    // other out-of-range material words fall back to class zero.
    if (flags & 0x80) {
        flags &= 0x7f;
    }
    return flags <= 3 ? std::uint8_t(flags) : 0;
}

std::array<Vector, 8> local_corners(const Vector& half) {
    const auto x = half[0], y = half[1], z = half[2];
    return {{{x, -y, z}, {x, -y, -z}, {-x, -y, z}, {-x, -y, -z}, {x, y, z}, {x, y, -z}, {-x, y, z}, {-x, y, -z}}};
}

struct ContactStep {
    std::array<bool, 8> active{};
    std::array<std::uint8_t, 8> surface{}, indices{};
    std::array<Vector, 8> velocity{}, normal_force{}, tyre_force{};
    std::array<std::int32_t, 8> side_limit{}, forward_limit{};
    std::uint32_t count = 0;
    Vector normal{0, one, 0}, total_force{};
    bool tyre_contact = false;
    bool airborne = false;
    std::int32_t mass_per_contact = 0;
};
} // namespace

struct Motion::Impl {
    const HpcSurface& surface;
    const scene::BarrierData* barriers;
    Vector pending_barrier_torque{};
    Vector pending_prop_force{}, pending_prop_torque{};
    Vector pending_car_force{}, pending_car_torque{};
    Vector distant_heading{};
    const std::uint32_t car_id, friction_profile;
    const bool manual_selection, low_gravity;
    const std::uint32_t weather_id;
    std::optional<AiDriver> ai;
    std::uint32_t ai_route_point = 0;
    bool ai_owned = false, rival_ai_observed = false, player_identity = true;
    bool ai_barrier_contact = false;
    std::optional<DriverAxes> prepared_ai_axes;
    bool ai_reverse_prepared = false;
    bool pending_full_ground = false, pending_distant_ground = false, body_prepared = false, suppress_ai_prepared = false;
    std::optional<MotionState> saved_before_ground;
    std::optional<Controls> saved_controls_before_ground;
    bool barrier_contacts_enabled = true;
    const Vector half;
    const std::array<Vector, 8> corners;
    const std::int32_t mass, inverse_mass;
    Controls controls;
    MotionState state;
    DamageContext& damage_context;
    SurfaceRoughness& roughness;
    std::uint32_t actor_id = 0;
    std::uint8_t model_detail = 0;
    std::uint16_t damage_registration = 0;
    bool human_identity = true;
    bool qualifying_session = false;
    std::uint32_t frame_brake_lights = 0;
    bool network_input = false;
    bool network_participant = false;
    bool damage_feedback_enabled = false;
    Vector ground_dot{0, one, 0};
    //40f5c0's retained240..260 patch offsets and2bc latch survive distant
    //steps. Keep them together for source publication, including stale values.
    std::array<Vector, 3> tire_patch_offsets{};
    bool tire_patches_shifted = false;
    std::array<bool, 8> previous_contact{};
    ContactStep cached_contact;

    Impl(const HpcSurface& source, const scene::BodyPose& initial, std::uint32_t car, bool manual, std::uint32_t weather, bool qualifying, DamageContext& damage,
        SurfaceRoughness& shared_roughness, const scene::BarrierData* barrier_data, bool low_gravity_enabled)
        : surface(source), barriers(barrier_data), car_id(car), friction_profile(weather == 3 || weather == 4 ? 2u
                                                                                 : weather == 5               ? 1u
                                                                                                              : 0u),
          manual_selection(manual), low_gravity(low_gravity_enabled), weather_id(weather), half(scene::car_half_dimensions(car)), corners(local_corners(half)),
          mass((car == 0      ? 970
                   : car == 1 ? 1100
                   : car == 8 ? 2000
                              : 1000)
                   * one
               + 120 * one),
          inverse_mass(reciprocal(mass)), controls(car, manual), damage_context(damage), roughness(shared_roughness) {
        // 4512b0's weather switch sets rain/snow flags for3/4, and the
        // additional4f2994 flag for5. 42e440 selects128d0(2) for3/4,
        // 128d0(1) for5 and128d0(0) for sunny/cloudy1/2.
        if (weather < 1 || weather > 5) {
            throw std::runtime_error("Motion requires resolved original weather 1..5");
        }
        state.body = initial;
        qualifying_session = qualifying;
        state.previous_position_q16 = initial.position;
        // 412040: qualifying starts at cccc along the grounded X basis;
        // race starts at zero. Wheel angular velocity is four times that speed.
        const auto initial_speed = qualifying ? 0xcccc : 0;
        state.velocity_q16 = scale(normalized(tangent_with_negative_product(initial.basis[0], initial.basis[1])), initial_speed);
        // The speed record itself is still zero until the first 40caf0 step.
        state.speed_q16 = 0;
        state.signed_speed_q16 = 0;
        state.wheel_speed_q16.fill(bits(std::uint32_t(initial_speed) << 2));
        controls.finish_update(drivetrain());
        for (std::size_t i = 0; i < 8; ++i) {
            previous_contact[i] = i < 4;
            cached_contact.active[i] = i < 4;
            const auto flags = surface_class(initial.surfaces[i].flags);
            cached_contact.surface[i] = flags;
            if (i < 4) {
                state.surface_ids[i] = std::uint8_t(flags);
                state.wheel_contact[i] = true;
            }
        }
        cached_contact.count = 4;
        //412040 writes index i to521+i, distinct from10110's descending list.
        cached_contact.indices = {0, 1, 2, 3};
        cached_contact.normal = initial.ground_normal;
        cached_contact.tyre_contact = true;
        state.contact_count = 4;
    }

    DrivetrainInputs drivetrain() const {
        return {state.wheel_speed_q16, fixed::abs(state.speed_q16), dot(state.velocity_q16, state.body.basis[0]), controls.start_held()};
    }

    void supports(ContactStep& contact) {
        contact.count = 0;
        for (std::size_t remaining = 8; remaining > 0; --remaining) {
            const auto i = remaining - 1;
            contact.active[i] = std::int64_t(state.body.surfaces[i].height) + 0x1999 >= state.body.contacts[i][1];
            if (contact.active[i]) {
                contact.indices[contact.count++] = std::uint8_t(i);
            }
        }
        contact.airborne = contact.count == 0;
    }

    // 102e0: preserve the original sign-crossing stabilization and its local
    // angular-component removal; this is not a replacement surface alignment.
    void stabilize(const Vector& normal) {
        auto& basis = state.body.basis;
        auto& angular = state.angular_velocity_q16;
        std::array<Vector, 3> projected{}, axes{};
        std::array<std::int32_t, 3> rate{};
        std::array<bool, 3> crossed{}, corrected{};
        for (std::size_t i = 0; i < 3; ++i) {
            axes[i] = normalized(cross(basis[i], normal));
            rate[i] = dot(source_transform::local(axes[i], basis), angular);
            const auto value = dot(normal, basis[i]);
            crossed[i] = opposite(ground_dot[i], value);
            ground_dot[i] = value;
            projected[i] = normalized(project(basis[i], normal));
        }
        state.rollover.observe(ground_dot);
        if (fixed::abs(rate[0]) < 0x8000 && crossed[0]) {
            basis[0] = projected[0];
            basis[2] = normalized(project(basis[2], basis[0]));
            basis[1] = normalized(cross(basis[2], basis[0]));
            corrected[0] = true;
        }
        if (fixed::abs(rate[2]) < 0x8000 && crossed[2]) {
            basis[2] = projected[2];
            basis[1] = normalized(project(basis[1], basis[2]));
            basis[0] = normalized(cross(basis[1], basis[2]));
            corrected[2] = true;
        }
        if (fixed::abs(rate[1]) < 0xccc && crossed[1]) {
            basis[1] = projected[1];
            basis[0] = normalized(project(basis[0], basis[1]));
            basis[2] = normalized(cross(basis[0], basis[1]));
            corrected[1] = true;
        }
        // 411a55..411ead removes a component only for a single corrected
        // axis. Concurrent axis corrections retain angular velocity/yaw.
        if (std::count(corrected.begin(), corrected.end(), true) == 1) {
            const auto index = std::size_t(std::find(corrected.begin(), corrected.end(), true) - corrected.begin());
            angular = project(angular, normalized(cross(projected[index], normal)));
        }
        if (fixed::abs(ground_dot[1]) > 0xfc28 || fixed::abs(ground_dot[1]) < 0x2c8b) {
            const auto component = scale(normal, dot(normal, angular));
            angular = add(component, scale(sub(angular, component), 0xf333));
        }
    }

    bool ground(ContactStep& contact) {
        auto& body = state.body;
        const auto previous_count = cached_contact.count;
        state.previous_rumble = state.rumble;
        for (std::size_t i = 0; i < 8; ++i) {
            const auto sample = surface.sample_retaining_cached(body.contacts[i][0], body.contacts[i][2], body.surfaces[i].triangle);
            if (!sample) {
                state.boundary = MotionBoundary::missing_surface;
                return false;
            }
            body.surfaces[i] = *sample;
            const auto flags = surface_class(sample->flags);
            contact.surface[i] = flags;
            //42e6f0 consumes the process-wide sequence in contact-index order.
            if ((flags == 2 || flags == 3) && state.speed_q16 > 0x8000) {
                body.surfaces[i].height = add(body.surfaces[i].height, roughness.next());
            }
            if (i < 4) {
                state.surface_ids[i] = std::uint8_t(flags);
                state.rumble[i] = flags == 1 && previous_contact[i];
            }
        }
        supports(contact);
        if (contact.count) {
            // 40bbc0: the contact plane follows the dominant vertical body axis.
            std::size_t second = 1, third = 2;
            const auto x = fixed::abs(body.basis[0][1]), y = fixed::abs(body.basis[1][1]), z = fixed::abs(body.basis[2][1]);
            if (x > y && x > z) {
                third = 4;
            } else if (z > y) {
                second = 2;
                third = 4;
            }
            auto a = body.contacts[0], b = body.contacts[second], c = body.contacts[third];
            a[1] = body.surfaces[0].height;
            b[1] = body.surfaces[second].height;
            c[1] = body.surfaces[third].height;
            auto normal = cross(sub(c, a), sub(b, a));
            if (normal[1] < 0) {
                normal = scale(normal, -one);
            }
            contact.normal = normalized(normal);
            if (contact.normal == Vector{}) {
                state.boundary = MotionBoundary::unsupported_contact;
                return false;
            }
            // 10190 chooses the highest active surface correction, including a
            // negative nearest correction; it never samples the body center.
            auto shift = std::numeric_limits<std::int32_t>::min();
            for (std::size_t i = 0; i < 8; ++i) {
                if (contact.active[i]) {
                    shift = std::max(shift, sub(body.surfaces[i].height, body.contacts[i][1]));
                }
            }
            body.position[1] = add(body.position[1], shift);
            for (auto& point : body.contacts) {
                point[1] = add(point[1], shift);
            }
            supports(contact);
            if (previous_count > 0) {
                stabilize(contact.normal);
                scene::refresh_body_contacts(body, car_id);
            } else {
                // 40f5c0: the first support after flight uses the incoming
                // normal speed, before the following velocity projection.
                if (dot(contact.normal, state.velocity_q16) < -0x3333) {
                    Vector local_point{};
                    for (std::size_t i = 0; i < corners.size(); ++i) {
                        if (contact.active[i]) {
                            local_point = add(local_point, corners[i]);
                        }
                    }
                    local_point = scale(local_point, reciprocal(std::int32_t(contact.count << 16)));
                    DamageInput landing;
                    landing.contact.impulse_q16 = {0x3333, 0, 0};
                    // Unlike409fa0, this caller does not restore actor+1f0.
                    // The next40caf0 consumes it before40b360 clears it.
                    pending_prop_force = landing.contact.impulse_q16;
                    landing.contact.local_contact_q16 = local_point;
                    landing.car_id = car_id;
                    // The original still calls419ad0 (advancing its cycle),
                    // then overrides its event with5. Its fixed .2 impulse is
                    // below the .3 visual threshold, so it cannot dent panels.
                    landing.feedback_enabled = damage_feedback_enabled;
                    const auto hit = damage_context.apply(state.damage, landing);
                    state.impacts.push_back({add(body.position, source_transform::world(local_point, body.basis)), 5, hit.strength_q16, human_identity, UINT32_MAX});
                }
                for (std::size_t i = 0; i < 3; ++i) {
                    ground_dot[i] = dot(contact.normal, body.basis[i]);
                }
            }
            state.velocity_q16 = project(state.velocity_q16, contact.normal);
        }
        previous_contact = contact.active;
        state.contact_count = contact.count;
        for (std::size_t i = 0; i < 4; ++i) {
            state.wheel_contact[i] = contact.active[i];
        }
        const auto local_normal = source_transform::local(contact.normal, body.basis);
        const auto up = fixed::abs(local_normal[1]);
        contact.tyre_contact = local_normal[1] >= 0x107 && fixed::abs(local_normal[0]) <= mul(up, 0x93cd) && fixed::abs(local_normal[2]) <= mul(up, 0x13116);
        return true;
    }

    void point_velocities(ContactStep& contact) const {
        // 40c5f0: multiply the positive half extents first. Negating an already
        // rounded product is deliberately different from multiplying -extent.
        const auto& w = state.angular_velocity_q16;
        const auto& b = state.body.basis;
        const std::array<std::int32_t, 6> q{mul(w[1], half[2]), mul(w[2], half[1]), mul(w[2], half[0]), mul(w[0], half[2]), mul(w[0], half[1]), mul(w[1], half[0])};
        for (std::size_t j = 0; j < 3; ++j) {
            const auto a = mul(b[0][j], q[0]), d = mul(b[0][j], q[1]), e = mul(b[1][j], q[2]), f = mul(b[1][j], q[3]), g = mul(b[2][j], q[4]), h = mul(b[2][j], q[5]);
            contact.velocity[0][j] = add(add(sub(sub(sub(e, f), g), h), d), a);
            contact.velocity[1][j] = add(add(sub(sub(sub(f, a), g), h), e), d);
            contact.velocity[2][j] = add(add(sub(sub(sub(h, e), f), g), d), a);
            contact.velocity[3][j] = add(add(sub(sub(sub(h, a), e), g), f), d);
            for (std::size_t i = 0; i < 4; ++i) {
                contact.velocity[7 - i][j] = neg(contact.velocity[i][j]);
            }
            for (auto& v : contact.velocity) {
                v[j] = add(v[j], state.velocity_q16[j]);
            }
        }
    }

    void normal_forces(ContactStep& contact) const {
        contact.total_force = scale(gravity, mass);
        if (contact.count == 0) {
            return;
        }
        contact.mass_per_contact = mass / std::int32_t(contact.count);
        const auto normal_load = neg(mul(add(contact.mass_per_contact, 30 * one), dot(gravity, contact.normal)));
        //40be80 traverses the retained count/index list, not the support flags.
        //412040/distant actors may retain eight zero flags with only four indices.
        for (std::size_t remaining = contact.count; remaining > 0; --remaining) {
            const auto i = contact.indices[remaining - 1];
            if (contact.count != 4) {
                contact.normal_force[i] = scale(contact.normal, normal_load);
            }
            const auto surface_id = contact.surface[i];
            const auto longitudinal = longitudinal_friction[friction_profile][surface_id], lateral = lateral_friction[friction_profile][surface_id];
            if (i < 4 && contact.tyre_contact) {
                const auto& offsets = controls.state().tire_offsets_q16;
                const auto axle = i < 2 ? 0u : 1u;
                contact.forward_limit[i] = mul(normal_load, add(longitudinal, offsets[4 + axle]));
                contact.side_limit[i] = mul(normal_load, add(lateral, offsets[axle]));
            } else {
                contact.forward_limit[i] = mul(normal_load, mul(longitudinal, longitudinal_shape[surface_id]));
                contact.side_limit[i] = mul(normal_load, mul(lateral, lateral_shape[surface_id]));
            }
        }
        // Keep the original addition of the normal component, then factor .1.
        contact.total_force = scale(add(contact.total_force, scale(contact.normal, dot(contact.normal, contact.total_force))), 0x199a);
    }

    void wheels(ContactStep& contact, const TimeStep& time) {
        const auto& control = controls.state();
        const auto& basis = state.body.basis;
        const auto front = source_transform::world({sine(1024 + control.steering_angle), 0, neg(sine(control.steering_angle))}, basis);
        const auto front_side = cross(front, basis[1]);
        std::array<Vector, 4> forward{}, side{};
        std::array<bool, 4> valid{};
        for (std::size_t i = 0; i < 4; ++i) {
            const auto projected = tangent_with_negative_product(i < 2 ? front : basis[0], contact.normal);
            valid[i] = dot(projected, projected) != 0;
            forward[i] = normalized(projected);
            side[i] = normalized(tangent_with_negative_product(i < 2 ? front_side : basis[2], contact.normal));
            state.wheel_forward_q16[i] = forward[i];
            state.wheel_side_q16[i] = side[i];
        }
        auto brake = control.brake_q16;
        if (control.raw_throttle_q16 == 0 && control.raw_brake_q16 == 0 && control.gear != 0) {
            brake = 0x1999;
        }
        // 40db10: +148 open-clutch start hold replaces both axle brake
        // moments with dynamics+40 (40a770 initializes it to5.0 Q16).
        const auto front_brake = controls.start_held() ? 0x50000 : mul(brake, brake_split[car_id]),
                   rear_brake = controls.start_held() ? 0x50000 : mul(brake, sub(one, brake_split[car_id]));
        const auto force_scale = mul(contact.mass_per_contact, time.reciprocal_q16), slip_scale = mul(time.reciprocal_q16, 0xf000), wheel_step = mul(time.delta_q16, 0x11113);
        std::int32_t longitudinal_extra = 0, lateral_extra = 0;
        for (int wheel = 3; wheel >= 0; --wheel) {
            const auto i = std::size_t(wheel);
            const auto old_speed = state.wheel_speed_q16[i];
            auto drive = control.wheel_drive_q16[i];
            auto longitudinal_speed = 0;
            const auto factor = slip_factor[contact.surface[i]];
            if (contact.active[i] && contact.tyre_contact) {
                const auto braking = i < 2 ? front_brake : rear_brake;
                if (old_speed > 0) {
                    drive = sub(drive, braking);
                } else if (old_speed < 0) {
                    drive = add(drive, braking);
                }
                if (valid[i]) {
                    longitudinal_speed = dot(forward[i], contact.velocity[i]);
                    const auto slip = sub(bits(std::uint32_t(longitudinal_speed) << 2), old_speed);
                    auto feedback = bits(std::uint32_t(mul(slip, slip_scale)) << 2);
                    feedback = limited(feedback, contact.forward_limit[i]);
                    if (factor) {
                        feedback = mul(feedback, sub(one, factor));
                    }
                    drive = add(drive, feedback >> 2);
                }
            }
            auto& speed = state.wheel_speed_q16[i];
            speed = add(speed, mul(drive, wheel_step));
            const auto front_drive = car_id == 1 ? 0xa666 : car_id == 8 ? 0x8000 : car_id == 9 ? 0 : one;
            if (control.gear == 0 || control.gear_pending || front_drive == (i < 2 ? 0 : one)) {
                if (opposite(old_speed, speed)) {
                    speed = 0;
                }
            } else if (opposite(ratios[std::size_t(control.gear)], speed)) {
                speed = 0;
            }
            if (!contact.active[i] || !contact.tyre_contact) {
                continue;
            }
            const auto tangent_gravity = scale(tangent_with_negative_product(scale(gravity, contact.mass_per_contact), contact.normal), 0x199a);
            auto longitudinal_demand = 0;
            if (valid[i]) {
                const auto slip = sub(bits(std::uint32_t(longitudinal_speed) << 2), speed);
                state.longitudinal_slip_q16[i] = slip;
                if (fixed::abs(slip) < one) {
                    longitudinal_extra = neg(dot(forward[i], tangent_gravity));
                    const auto opposing = mul(longitudinal_speed, force_scale);
                    if (!opposite(longitudinal_extra, opposing)) {
                        longitudinal_extra = fixed::abs(opposing) < fixed::abs(longitudinal_extra) ? sub(longitudinal_extra, opposing) : 0;
                    }
                    const auto ratio = ratios[std::size_t(control.gear)];
                    if ((opposite(ratio, longitudinal_extra) || ratio == 0) && control.brake_q16 == 0) {
                        longitudinal_extra = 0;
                    }
                }
                longitudinal_demand = neg(mul(mul(slip, 0x4000), force_scale));
                if (factor) {
                    longitudinal_demand = mul(longitudinal_demand, sub(one, factor));
                }
            }
            const auto lateral_speed = dot(side[i], contact.velocity[i]);
            state.lateral_slip_q16[i] = lateral_speed;
            if (fixed::abs(lateral_speed) < one) {
                lateral_extra = neg(dot(side[i], tangent_gravity));
            }
            longitudinal_demand = add(longitudinal_demand, longitudinal_extra);
            const auto lateral_demand = add(neg(mul(lateral_speed, force_scale)), lateral_extra);
            const auto largest = std::max(fixed::abs(longitudinal_demand), fixed::abs(lateral_demand));
            Vector force{};
            std::int32_t magnitude = 0;
            if (largest <= one) {
                force = add(scale(forward[i], longitudinal_demand), scale(side[i], lateral_demand));
                magnitude = dot(force, force);
            } else {
                const auto inverse = reciprocal(largest);
                force = add(scale(forward[i], mul(longitudinal_demand, inverse)), scale(side[i], mul(lateral_demand, inverse)));
                magnitude = mul(dot(force, force), largest);
            }
            const auto direction = normalized(force);
            auto long_limit = contact.forward_limit[i], side_limit = contact.side_limit[i];
            const auto axle = i < 2 ? 0u : 1u;
            const auto ls = add(longitudinal_shape[contact.surface[i]], control.tire_offsets_q16[6 + axle]),
                       ss = add(lateral_shape[contact.surface[i]], control.tire_offsets_q16[2 + axle]);
            if (fixed::abs(longitudinal_demand) < 0x6666) {
                long_limit = mul(long_limit, ls);
                side_limit = mul(side_limit, ss);
            } else if (fixed::abs(lateral_demand) >= 0x6666) {
                auto angle = atan_ratio(mul(fixed::abs(longitudinal_demand), ss), fixed::abs(lateral_demand));
                auto component = mul(ss, sine(1024 + angle));
                side_limit = mul(side_limit, sqrt(std::uint32_t(add(mul(component, component), mul(sine(angle), sine(angle))))));
                angle = atan_ratio(mul(fixed::abs(longitudinal_demand), ls), fixed::abs(lateral_demand));
                component = mul(ls, sine(1024 + angle));
                long_limit = mul(long_limit, sqrt(std::uint32_t(add(mul(component, component), mul(sine(angle), sine(angle))))));
            }
            if (side_limit < magnitude) {
                magnitude = long_limit;
            } else if (factor == 0) {
                state.longitudinal_slip_q16[i] = 0;
                state.lateral_slip_q16[i] = 0;
            }
            if (factor) {
                state.longitudinal_slip_q16[i] = mul(state.longitudinal_slip_q16[i], factor);
                state.lateral_slip_q16[i] = mul(state.lateral_slip_q16[i], factor);
            }
            contact.tyre_force[i] = scale(direction, magnitude);
            const auto drag = rolling_drag[contact.surface[i]];
            if (drag) {
                const auto speed_at_contact = dot(contact.velocity[i], forward[i]);
                if (speed_at_contact) {
                    contact.tyre_force[i] = add(contact.tyre_force[i], scale(forward[i], neg(mul(speed_at_contact >> 2, mul(force_scale, drag)))));
                }
            }
        }
    }

    void body_friction(ContactStep& contact) const {
        for (std::size_t i = 0; i < 8; ++i) {
            if (!contact.active[i] || (i < 4 && contact.tyre_contact)) {
                continue;
            }
            const auto tangent = tangent_with_negative_product(contact.velocity[i], contact.normal);
            const auto amount = limited(neg(mul(dot(tangent, tangent), contact.mass_per_contact)) >> 2, contact.forward_limit[i]);
            contact.tyre_force[i] = add(contact.tyre_force[i], scale(normalized(tangent), amount));
        }
    }

    void integrate(const ContactStep& contact, const TimeStep& time, const ControlsInput& input) {
        auto total = contact.total_force;
        Vector torque{};
        //40caf0 consumes the source's independent retained contact list in
        //reverse. Normal10110 lists are descending; initial/stale lists need not be.
        for (std::size_t remaining = contact.count; remaining > 0; --remaining) {
            const auto i = contact.indices[remaining - 1];
            total = add(total, contact.tyre_force[i]);
            torque = add(torque, cross(source_transform::local(add(contact.normal_force[i], contact.tyre_force[i]), state.body.basis), corners[i]));
        }
        scene::advance_car_visual_angles(state.car_visual_angles, contact.count, torque[0], torque[2]);
        total = sub(total, scale(state.velocity_q16, fixed::abs(state.speed_q16)));
        // 40caf0 consumes post contacts' car+1f0 after inverse mass; their
        // car+1fc moment shares the delayed barrier-torque integration point.
        const auto dv = scale(scale(add(add(scale(total, inverse_mass), pending_prop_force), pending_car_force), 0x3333), time.delta_q16);
        state.velocity_q16 = add(state.velocity_q16, dv);
        state.speed_q16 = length(state.velocity_q16);
        for (std::size_t i = 0; i < 3; ++i) {
            //40caf0 scales the complete raw body+1fc moment after all callers
            //have accumulated it, including42b570 vehicle contacts.
            const auto moment = mul(add(add(pending_barrier_torque[i], pending_prop_torque[i]), pending_car_torque[i]), mass);
            state.angular_velocity_q16[i] = add(state.angular_velocity_q16[i], mul(neg(mul(add(torque[i], moment), i == 1 ? 2 : 1)), time.delta_q16));
        }
        auto displacement = sub(scale(state.velocity_q16, time.delta_q16), scale(dv, time.delta_q16 / 2));
        if (contact.count > 0 && state.speed_q16 < 0xccc) {
            displacement = {};
        }
        advance_player_righting(state.rollover, ground_dot[1], state.body.basis[2][1], state.angular_velocity_q16);
        std::array<std::int16_t, 3> angles{};
        for (std::size_t i = 0; i < 3; ++i) {
            angles[i] = std::bit_cast<std::int16_t>(std::uint16_t(mul(mul(state.angular_velocity_q16[i], time.delta_q16), 0x28b)));
        }
        if (contact.count == 4 && fixed::abs(angles[0]) < 10) {
            angles[0] = 0;
        }
        if (contact.count > 0 && !input.left && !input.right && fixed::abs(angles[1]) < 10) {
            angles[1] = 0;
        }
        if (contact.count > 0 && fixed::abs(angles[2]) < 10) {
            angles[2] = 0;
        }
        state.body.position = add(state.body.position, displacement);
        rotate(state.body.basis, angles);
        scene::refresh_body_contacts(state.body, car_id);
        state.signed_speed_q16 = dot(state.velocity_q16, state.body.basis[0]);
    }

    void restore_source_snapshot(const SourceSnapshot& snapshot, bool cold = false) {
        const auto& raw = snapshot.body;
        const auto& dynamics = snapshot.dynamics;
        state = {};
        auto vector = [&](std::size_t offset) {
            return Vector{source_i32(raw, offset), source_i32(raw, offset + 4), source_i32(raw, offset + 8)};
        };
        state.body.position = vector(0xbc);
        state.previous_position_q16 = vector(0x268);
        state.velocity_q16 = vector(0xc8);
        state.speed_q16 = source_i32(raw, 0xe0);
        state.published_brake_q16 = source_i32(raw, 0x4e8);
        frame_brake_lights = source_u32(raw, 0x508);
        state.angular_velocity_q16 = vector(0xe4);
        state.car_visual_angles = {std::bit_cast<std::int16_t>(source_u16(dynamics, 0x278)), std::bit_cast<std::int16_t>(source_u16(dynamics, 0x27a))};
        ground_dot = vector(0xb0);
        state.rollover.upside_down = source_i32(dynamics, 0x2a0) != 0;
        state.rollover.sideways = source_i32(dynamics, 0x2a4) != 0;
        state.rollover.pitched = source_i32(dynamics, 0x2a8) != 0;
        state.rollover.player_righting = source_i32(dynamics, 0x31c) != 0;
        state.rollover.ticks = {dynamics[0x331], dynamics[0x332], dynamics[0x333]};
        for (std::size_t i = 0; i < 3; ++i) {
            state.body.basis[i] = vector(0x150 + i * 12);
        }
        scene::refresh_body_contacts(state.body, car_id);
        cached_contact = {};
        cached_contact.normal = vector(0x1e4);
        state.body.ground_normal = cached_contact.normal;
        for (std::size_t i = 0; i < 8; ++i) {
            state.body.contacts[i] = vector(0xf0 + i * 12);
            state.body.surfaces[i].height = source_i32(raw, 0x1c4 + i * 4);
            state.body.surfaces[i].triangle = source_u16(dynamics, 0x264 + i * 2);
            state.body.surfaces[i].flags = dynamics[0x328 + i];
            cached_contact.surface[i] = surface_class(dynamics[0x328 + i]);
            cached_contact.active[i] = raw[0x517 + i] == 0;
            if (i < 4) {
                state.wheel_contact[i] = cached_contact.active[i];
                state.surface_ids[i] = cached_contact.surface[i];
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    state.wheel_forward_q16[i][axis] = source_i32(dynamics, (i < 2 ? 0x1b4 : 0x1c0) + axis * 4);
                    state.wheel_side_q16[i][axis] = source_i32(dynamics, (i < 2 ? 0x1cc : 0x1d8) + axis * 4);
                }
                state.wheel_speed_q16[i] = source_i32(dynamics, 0xfc + i * 4);
                state.longitudinal_slip_q16[i] = source_i32(dynamics, 0x1e4 + i * 4);
                state.lateral_slip_q16[i] = source_i32(dynamics, 0x1f4 + i * 4);
                state.rumble[i] = source_i32(dynamics, 0x280 + i * 4) != 0;
                state.previous_rumble[i] = source_i32(dynamics, 0x290 + i * 4) != 0;
            }
        }
        //412040 initializes count4 while upper support bytes can retain0.
        //444ee0 also leaves these full-body records stale. The count is an
        //independent source field, not an invariant derived from all8 flags.
        if (raw[0x51f] > 8) {
            throw std::runtime_error("Original snapshot contact count exceeds8 body corners");
        }
        cached_contact.count = raw[0x51f];
        cached_contact.airborne = raw[0x520] != 0;
        for (std::size_t i = 0; i < cached_contact.indices.size(); ++i) {
            cached_contact.indices[i] = raw[0x521 + i];
            if (i < cached_contact.count && cached_contact.indices[i] >= 8) {
                throw std::runtime_error("Original contact index exceeds eight body corners");
            }
        }
        for (std::size_t i = 0; i < tire_patch_offsets.size(); ++i) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                tire_patch_offsets[i][axis] = source_i32(dynamics, 0x240 + i * 12 + axis * 4);
            }
        }
        tire_patches_shifted = source_i32(dynamics, 0x2bc) != 0;
        state.contact_count = cached_contact.count;
        state.tyre_gate = source_i32(dynamics, 0x2c0) != 0;
        barrier_contacts_enabled = source_i32(raw, 0x510) != 0;
        previous_contact = cached_contact.active;
        state.signed_speed_q16 = dot(state.velocity_q16, state.body.basis[0]);
        state.speed_integer = source_i32(snapshot.gameplay, 0xcc) >> 16;
        pending_barrier_torque = {};
        pending_prop_force = vector(0x1f0);
        pending_prop_torque = vector(0x1fc);
        pending_car_force = {};
        pending_car_torque = {};
        if (snapshot.has_barrier_recovery) {
            const auto& recovery = snapshot.barrier_recovery;
            auto& target = state.barrier_recovery;
            target.active = source_u32(recovery, 0x30) != 0;
            target.has_secondary = source_u32(recovery, 0x2c) != 0;
            if (target.active) {
                target.head = source_u32(recovery, 0x0c);
                target.secondary_segment = target.has_secondary ? source_u32(recovery, 0x28) : UINT32_MAX;
                if (!barriers || target.head >= barriers->segments().size() || (target.has_secondary && target.secondary_segment >= barriers->segments().size())) {
                    throw std::runtime_error("Original recovery record has an invalid relocated barrier index");
                }
                for (std::size_t i = 0; i < 3; ++i) {
                    target.primary_push[i] = source_i32(recovery, i * 4);
                    target.saved_velocity[i] = source_i32(recovery, 0x10 + i * 4);
                    target.secondary_push[i] = source_i32(recovery, 0x1c + i * 4);
                }
            }
        }
        if (snapshot.has_damage) {
            restore_source_damage(state.damage, snapshot.damage_levels, snapshot.broken_features);
        }
        damage_feedback_enabled = std::bit_cast<std::int8_t>(raw[0x53b]) != -1;
        state.damage.feedback_side = raw[0x514];
        state.damage.feedback_strength = source_u16(raw, 0x4ec);
        if (cold) {
            controls.restore_cold_source_snapshot(snapshot);
        } else {
            controls.restore_source_snapshot(snapshot);
        }
        state.previous_engine_speed_q16 = source_i32(dynamics, 0x128);
        ai_owned = false;
    }

    //409c30 accumulates 4095b0 moments and dispatches 409fa0/419ad0
    //for both full and distant bodies, independently of their ground pass.
    void consume_barrier_response(const BarrierContactResult& result) {
        pending_barrier_torque = result.angular_impulse_q16;
        state.barrier_effects = result.effects;
        state.barrier_contact_count = result.contacts;
        state.recovery_entry_count += result.recovery_entered ? 1u : 0u;
        state.recovery_exit_count += result.recovery_exited ? 1u : 0u;
        for (const auto& collision : result.damage_contacts) {
            DamageInput damage_input;
            damage_input.contact = collision;
            for (std::size_t axis = 0; axis < half.size(); ++axis) {
                damage_input.inverse_half_dimensions_q16[axis] = reciprocal(half[axis]);
            }
            damage_input.car_id = car_id;
            damage_input.debris_status = [this] {
                return damage_context.parts().status();
            };
            damage_input.detach = [this](auto slot) {
                damage_context.parts().detach(slot, {state.body, state.velocity_q16, car_id, true, model_detail, actor_id, damage_registration});
            };
            damage_input.feedback_enabled = damage_feedback_enabled;
            const auto hit = damage_context.apply(state.damage, damage_input);
            if (hit.event_kind != 0) {
                const auto strength = mul(hit.strength_q16, 0xe666);
                state.impacts.push_back({collision.position_q16, hit.event_kind, strength, human_identity, collision.barrier_index});
                if (hit.event_kind != 1) {
                    state.impacts.push_back({collision.position_q16, 1, strength, human_identity, collision.barrier_index});
                }
            }
        }
    }

    //444830 ->4448b0/444ab0 followed by444ee0. The distant car's
    // longitudinal speed is integrated from its mapped gas/brake/drag and
    // gear ratio; its direction blends the steered and retained forward axes.
    void update_distant(const TimeStep& time, bool defer_ground, bool mapped = false) {
        if ((!mapped && (!ai_owned || !ai)) || time.delta_q16 <= 0 || time.delta_q16 > 0x30000) {
            throw std::runtime_error("Distant body requires AI ownership and bounded source time");
        }
        state.prop_impact_q16 = 0;
        if (state.boundary != MotionBoundary::none) {
            return;
        }
        const auto previous = state.body.position;
        state.previous_position_q16 = previous;
        state.impacts.clear();
        state.barrier_effects = {};
        state.barrier_contact_count = 0;
        state.speed_integer = mul(state.speed_q16, 0x640000) >> 16;
        if (mapped) {
            (void)controls.begin_source_update(time, drivetrain(), false);
        } else {
            if (!rival_ai_observed) {
                ai->observe(state.body, mul(state.speed_q16, 0x640000), ai_route_point);
            }
            const auto axes = ai_axes(state.rollover.upside_down || state.rollover.sideways || state.rollover.pitched);
            controls.begin_ai_update(axes, time, drivetrain(), false);
        }
        rival_ai_observed = false;
        const auto& control = controls.state();
        auto acceleration = control.throttle_q16 == 0 ? -0x6e9 : mul(control.throttle_q16, 0x20c4);
        if (state.speed_q16 != 0) {
            acceleration = sub(acceleration, mul(control.brake_q16, 0x30a3));
        }
        acceleration = sub(acceleration, mul(state.speed_q16, 0x49b));
        acceleration = mul(mul(acceleration, ratios[std::size_t(control.gear)]), 0x11113) / 4;
        acceleration = std::clamp(acceleration, -0xa3d7, 0xa3d7);
        auto wheel = add(bits(std::uint32_t(state.speed_q16) << 2), acceleration);
        if (control.gear == 6) {
            const auto maximum = mul(0x9851e, 0x10000);
            if (fixed::abs(wheel) > maximum) {
                wheel = maximum;
            }
        }
        if ((control.gear == 7 && wheel > 0) || (control.gear != 0 && control.gear != 7 && wheel < 0)) {
            wheel = 0;
        }
        state.wheel_speed_q16.fill(wheel);
        state.speed_q16 = wheel >> 2;
        const auto front = source_transform::world({sine(1024 + control.steering_angle), 0, neg(sine(control.steering_angle))}, state.body.basis);
        const auto half_speed = state.speed_q16 / 2;
        state.velocity_q16 = add(scale(front, half_speed), scale(state.body.basis[0], half_speed));
        distant_heading = normalized(add(scale(scale(front, half_speed), 0x5999), state.body.basis[0]));
        const auto translation = scale(state.velocity_q16, time.delta_q16);
        state.body.position = add(state.body.position, translation);
        //444ab0 translates the existing eight points without reconstructing.
        for (auto& point : state.body.contacts) {
            point = add(point, translation);
        }
        //444830 clears body+1f0..204 after movement and before 409c30.
        //444ee0 leaves the new moment intact for a later full 40caf0 pass.
        pending_barrier_torque = {};
        pending_prop_force = {};
        pending_prop_torque = {};
        pending_car_force = {};
        pending_car_torque = {};
        if (barriers && barrier_contacts_enabled) {
            const auto sector = barriers->sector(state.body.position);
            state.barrier_sector = sector ? std::int32_t(*sector) : -1;
            const auto retained_points = state.body.contacts;
            const auto response = resolve_barriers(*barriers, state.body, state.velocity_q16, previous, car_id, state.barrier_recovery, control.gear == 7);
            if (response.contacts == 0 && !state.barrier_recovery.active) {
                state.body.contacts = retained_points;
            }
            consume_barrier_response(response);
        }
        controls.finish_distant_update(wheel);
        pending_distant_ground = true;
        if (!defer_ground) {
            finish_distant_ground();
        }
    }

    void finish_distant_ground() {
        if (!pending_distant_ground || state.boundary != MotionBoundary::none) {
            return;
        }
        pending_distant_ground = false;
        const auto sample = surface.prop_sample_retaining_cached(state.body.position[0], state.body.position[2], state.body.surfaces[0].triangle);
        if (!sample) {
            state.boundary = MotionBoundary::missing_surface;
            return;
        }
        state.body.surfaces[0].triangle = sample->triangle;
        state.body.position[1] = add(sample->height, half[1]);
        if ((sample->flags & 0x7f) != 1) {
            state.body.ground_normal = sample->normal;
        }
        state.body.basis[1] = state.body.ground_normal;
        // 444ee0 writes the same car+1e4 normal consumed by40b360 after
        // the distant actor is promoted back to fullbody.
        cached_contact.normal = state.body.ground_normal;
        state.body.basis[0] = normalized(project(distant_heading, state.body.basis[1]));
        state.body.basis[2] = normalized(cross(state.body.basis[0], state.body.basis[1]));
        scene::refresh_body_contacts(state.body, car_id);
        //444ee0 also aliases body+1d8 (full-body height5) with the
        //vertical corner expression immediately before40c280.
        state.body.surfaces[5].height =
            add(sub(sub(mul(half[0], state.body.basis[0][1]), mul(half[1], state.body.basis[1][1])), mul(half[2], state.body.basis[2][1])), state.body.position[1]);
        //444ee0 writes only the centre query's cached triangle/material.
        //Other full-body sample heights/caches and support flags/count retain
        //their prior values until40f5c0 runs after a later full-body promotion.
        state.body.surfaces[0].flags = std::uint16_t(sample->flags & 0x7f);
        cached_contact.surface[0] = std::uint8_t(sample->flags & 0x7f);
        state.surface_ids[0] = cached_contact.surface[0];
        state.signed_speed_q16 = dot(state.velocity_q16, state.body.basis[0]);
        state.impact_count += state.impacts.size();
    }

    //403dd0 copies body+e0 into signed game+cc with*100.4448b0's
    //wheel>>2 may be negative; only physical drag/drivetrain use its magnitude.
    DriverAxes ai_axes(bool suppressed = false, bool advance_recovery = true) {
        if (prepared_ai_axes) {
            const auto axes = *prepared_ai_axes;
            prepared_ai_axes.reset();
            return axes;
        }
        AiControlContext context;
        context.player = player_identity;
        context.suppressed = suppressed;
        context.barrier_hit = ai_barrier_contact;
        ai->set_control_context(context);
        const auto& previous = controls.state();
        if (advance_recovery) {
            const DriverAxes axes{previous.raw_left_q16, previous.raw_right_q16, previous.raw_throttle_q16, previous.raw_brake_q16};
            ai->synchronize_source_physical_controls(previous.gear, previous.reverse_pedal, &axes);
        }
        return ai->axes(state.body, mul(state.speed_q16, 0x640000), previous.gear, advance_recovery);
    }

    void update(ControlsInput input, const TimeStep& time, bool mapped = false, bool defer_ground = false) {
        if (time.delta_q16 <= 0 || time.delta_q16 > 0x30000 || time.reciprocal_q16 <= 0) {
            throw std::runtime_error("Motion requires a positive bounded source time step");
        }
        state.impacts.clear();
        state.prop_impact_q16 = 0;
        state.barrier_effects = {};
        state.barrier_contact_count = 0;
        state.barrier_sector = -1;
        if (state.boundary != MotionBoundary::none) {
            return;
        }
        state.speed_integer = mul(state.speed_q16, 0x640000) >> 16;
        state.previous_position_q16 = state.body.position;
        const auto saved = state;
        const auto saved_controls = controls;
        state.previous_engine_speed_q16 = controls.state().engine_speed_q16;
        ContactStep contact;
        contact.active = cached_contact.active;
        contact.surface = cached_contact.surface;
        contact.count = cached_contact.count;
        contact.indices = cached_contact.indices;
        contact.normal = cached_contact.normal;
        const auto local_normal = source_transform::local(contact.normal, state.body.basis);
        const auto up = fixed::abs(local_normal[1]);
        contact.tyre_contact = local_normal[1] >= 0x107 && fixed::abs(local_normal[0]) <= mul(up, 0x93cd) && fixed::abs(local_normal[2]) <= mul(up, 0x13116);
        state.tyre_gate = contact.tyre_contact;
        if (!body_prepared) {
            suppress_ai_prepared = state.rollover.upside_down || state.rollover.sideways || state.rollover.pitched;
            if (prepare_rollover(state.rollover, player_identity, state.body, state.angular_velocity_q16)) {
                return;
            }
        }
        const bool suppress_ai = suppress_ai_prepared;
        body_prepared = false;
        point_velocities(contact);
        if (ai && (!network_participant || ai_owned) && !rival_ai_observed) {
            ai->observe(state.body, mul(state.speed_q16, 0x640000), ai_route_point);
        }
        rival_ai_observed = false;
        if (network_input && !ai_owned) {
            input = controls.begin_network_update(time, drivetrain());
        } else if (mapped) {
            input = controls.begin_source_update(time, drivetrain());
        } else if (ai_owned) {
            const auto axes = ai_axes(suppress_ai);
            controls.begin_ai_update(axes, time, drivetrain());
            ai->apply_source_motion_steering_gate(state.speed_q16, true);
            input = {axes.throttle_q16 != 0, axes.brake_q16 != 0, axes.left_q16 != 0, axes.right_q16 != 0};
        } else {
            controls.begin_update(input, time, drivetrain());
        }
        normal_forces(contact);
        wheels(contact, time);
        body_friction(contact);
        integrate(contact, time, input);
        controls.finish_update(drivetrain());
        pending_car_force = {};
        pending_car_torque = {};
        pending_barrier_torque = {};
        if (barriers && barrier_contacts_enabled) {
            if (!state.barrier_recovery.active) {
                const auto sector = barriers->sector(state.body.position);
                state.barrier_sector = sector ? std::int32_t(*sector) : -1;
            }
            const auto result = resolve_barriers(*barriers, state.body, state.velocity_q16, saved.body.position, car_id, state.barrier_recovery, controls.state().gear == 7);
            consume_barrier_response(result);
        }
        pending_prop_force = {};
        pending_prop_torque = {};
        pending_full_ground = true;
        saved_before_ground = saved;
        saved_controls_before_ground = saved_controls;
        if (!defer_ground) {
            finish_full_ground();
        }
    }

    void finish_full_ground() {
        if (!pending_full_ground || state.boundary != MotionBoundary::none) {
            return;
        }
        pending_full_ground = false;
        // 421aa0 -> 421980 -> 415240: HPC/contact correction follows forces,
        // and supplies cached support/planes for the next original update.
        ContactStep next_contact;
        next_contact.normal = cached_contact.normal;
        //10110 overwrites only the live prefix; unused source list entries
        //remain retained even when the following support count shrinks.
        next_contact.indices = cached_contact.indices;
        if (!ground(next_contact)) {
            const auto boundary = state.boundary;
            if (saved_before_ground) {
                state = *saved_before_ground;
                controls = *saved_controls_before_ground;
            }
            state.boundary = boundary;
            saved_before_ground.reset();
            saved_controls_before_ground.reset();
            return;
        }
        state.impact_count += state.impacts.size();
        cached_contact = next_contact;
        state.body.ground_normal = next_contact.normal;
        state.signed_speed_q16 = dot(state.velocity_q16, state.body.basis[0]);
        scene::refresh_body_contacts(state.body, car_id);
        //40f5c0 ends by moving the four lower box points to the tire patches.
        //40b360 undoes the stored tire offsets before the next force pass.
        if (ground_dot[1] > 0xe666) {
            tire_patches_shifted = true;
            tire_patch_offsets = {scale(state.body.basis[0], 0x13333), scale(state.body.basis[2], 0x3333), scale(state.body.basis[0], 0xe666)};
            const auto& [front, side, rear] = tire_patch_offsets;
            state.body.contacts[0] = sub(sub(state.body.contacts[0], front), side);
            state.body.contacts[1] = add(sub(state.body.contacts[1], front), side);
            state.body.contacts[2] = sub(add(state.body.contacts[2], rear), side);
            state.body.contacts[3] = add(add(state.body.contacts[3], rear), side);
        }
        const auto strength = std::clamp(mul(state.speed_q16, 0x9851e), 0, one);
        state.rumble_strength_q16.fill(strength);
        //40f5c0 consumes the assigned source FFB slot's direction, then
        //clears body+514 while retaining the strength at+4ec.
        if (damage_feedback_enabled) {
            state.wheel_feedback_side = state.damage.feedback_side;
            state.wheel_feedback_strength = std::bit_cast<std::int16_t>(state.damage.feedback_strength);
            state.damage.feedback_side = 0;
        }
        saved_before_ground.reset();
        saved_controls_before_ground.reset();
    }
};

Motion::Motion(const HpcSurface& surface, const scene::BodyPose& body, std::uint32_t car, bool manual, std::uint32_t weather, bool qualifying, DamageContext& damage,
    SurfaceRoughness& roughness, const scene::BarrierData* barriers, bool low_gravity)
    : impl_(std::make_unique<Impl>(surface, body, car, manual, weather, qualifying, damage, roughness, barriers, low_gravity)) {}

Motion::~Motion() = default;

RemoteControls Motion::network_controls() const {
    return impl_->controls.network_controls();
}

RemoteMotionState Motion::network_state() const {
    const auto& state = impl_->state;
    const auto& controls = impl_->controls.state();
    RemoteMotionState result;
    result.position = state.body.position;
    result.velocity = state.velocity_q16;
    result.angular_velocity = state.angular_velocity_q16;
    result.basis = state.body.basis;
    for (auto& row : result.basis) {
        for (auto& value : row) {
            value = std::clamp(value, -65536, 65536);
        }
    }
    const auto signed_six = [](std::int16_t value) {
        return std::int16_t((std::uint16_t(value) & 0x3f) ^ 0x20) - 0x20;
    };
    result.visual_angles = {std::int16_t(signed_six(state.car_visual_angles.local_x)), std::int16_t(signed_six(state.car_visual_angles.absolute_z))};
    result.steering_angle = std::uint16_t(controls.steering_angle) & 0xfff;
    result.engine_speed_q16 = std::uint32_t(controls.engine_speed_q16) & 0xfffff;
    if (controls.throttle_q16 > 0) {
        result.throttle_q16 = std::uint16_t(controls.throttle_q16);
    }
    result.controls = network_controls();
    return result;
}

void Motion::apply_network_controls(const RemoteControls& input) {
    impl_->controls.apply_network_controls(input);
    impl_->network_input = true;
    impl_->network_participant = true;
}

void Motion::apply_network_state(const RemoteMotionState& input) {
    validate_remote_motion(input);
    auto& state = impl_->state;
    //43b7d0 retains integration history and pending forces. Only the next
    //physical update refreshes speed and ground supports after correction.
    state.body.position = correct_remote_position(state.body.position, input.position);
    state.body.basis = input.basis;
    state.car_visual_angles = {input.visual_angles[0], input.visual_angles[1]};
    scene::refresh_body_contacts(state.body, impl_->car_id);
    state.velocity_q16 = input.velocity;
    state.angular_velocity_q16 = input.angular_velocity;
    impl_->controls.apply_network_state(input, impl_->human_identity);
    impl_->network_input = true;
    impl_->network_participant = true;
}

void Motion::apply_network_gear(std::uint8_t gear, bool reverse) {
    impl_->controls.apply_network_gear(gear, reverse);
}

void Motion::set_steering_profile(std::uint32_t profile) {
    impl_->controls.set_steering_profile(profile);
}

void Motion::set_network_participant() {
    impl_->network_participant = true;
}

void Motion::apply_network_damage(const std::array<std::uint8_t, 17>& variants) {
    if (std::any_of(variants.begin(), variants.end(), [](auto value) {
            return value > 3;
        })) {
        throw std::runtime_error("Network visual damage variant exceeds original0..3 range");
    }
    //43bb60 changes mesh variants only; it does not create mechanical forces,
    //broken-feature state or shared detached parts on the receiving process.
    impl_->state.damage.levels = variants;
}

void Motion::restore_source_snapshot(const SourceSnapshot& snapshot) {
    impl_->restore_source_snapshot(snapshot);
}

void Motion::publish_source_cold_context(SourceSnapshot& snapshot) const {
    SourceSnapshot generated;
    publish_source_physical_state(generated);
    impl_->controls.publish_source_cold_context(generated);
    copy_source_cold_actor_context(snapshot, generated);
}

void Motion::restore_cold_source_snapshot(const SourceSnapshot& snapshot) {
    auto generated = snapshot;
    publish_source_cold_context(generated);
    impl_->restore_source_snapshot(generated, true);
}

ControllerHandoffState Motion::controller_handoff_state() const {
    return impl_->controls.controller_handoff_state();
}

void Motion::publish_source_control_calibration(SourceSnapshot& snapshot) const {
    impl_->controls.publish_source_calibration(snapshot);
}

void Motion::publish_warm_grid_context(SourceSnapshot& snapshot) const {
    publish_source_physical_state(snapshot);
    const auto& controls = impl_->controls.state();
    const auto handoff = impl_->controls.controller_handoff_state();
    const auto store = [](auto& bytes, std::size_t offset, std::uint32_t value) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            bytes[offset + byte] = std::uint8_t(value >> (byte * 8));
        }
    };
    store(snapshot.gameplay, 4, 1);
    store(snapshot.gameplay, 8, !impl_->ai_owned);
    for (std::size_t index = 0; index < handoff.saved_modes.size(); ++index) {
        store(snapshot.gameplay, 0x60 + index * 4, std::uint32_t(handoff.saved_modes[index]));
    }
    for (const auto& [offset, value] : std::array<std::pair<std::size_t, std::int32_t>, 8>{{{0x11c, controls.throttle_q16}, {0x128, impl_->state.previous_engine_speed_q16},
             {0x12c, controls.engine_drive_q16}, {0x138, controls.raw_left_q16}, {0x13c, controls.raw_right_q16}, {0x140, controls.raw_throttle_q16},
             {0x144, controls.raw_brake_q16}, {0x14c, std::bit_cast<std::int32_t>(handoff.queued_shift)}}}) {
        store(snapshot.dynamics, offset, std::uint32_t(value));
    }
    snapshot.limiter_active = controls.limiter_active;
    snapshot.limiter_load_q16 = impl_->controls.limiter_load_q16();
}

void Motion::restore_generated_grid_controls(const SourceSnapshot& snapshot) {
    impl_->controls.restore_source_snapshot(snapshot);
    impl_->state.previous_engine_speed_q16 = source_i32(snapshot.dynamics, 0x128);
    impl_->state.previous_position_q16 = {source_i32(snapshot.body, 0x268), source_i32(snapshot.body, 0x26c), source_i32(snapshot.body, 0x270)};
    impl_->state.published_brake_q16 = source_i32(snapshot.body, 0x4e8);
}

void Motion::publish_source_physical_state(SourceSnapshot& snapshot) const {
    const auto& actor = *impl_;
    const auto& state = actor.state;
    auto& body = snapshot.body;
    auto& dynamics = snapshot.dynamics;
    const auto store = [](auto& bytes, std::size_t offset, std::uint32_t value, std::size_t width = 4) {
        for (std::size_t byte = 0; byte < width; ++byte) {
            bytes[offset + byte] = std::uint8_t(value >> (byte * 8));
        }
    };
    const auto vector = [&](auto& bytes, std::size_t offset, const auto& values) {
        for (std::size_t index = 0; index < values.size(); ++index) {
            store(bytes, offset + index * 4, std::uint32_t(values[index]));
        }
    };
    vector(body, 0xb0, actor.ground_dot);
    vector(body, 0xbc, state.body.position);
    vector(body, 0xc8, state.velocity_q16);
    store(body, 0xe0, std::uint32_t(state.speed_q16));
    vector(body, 0xe4, state.angular_velocity_q16);
    vector(body, 0x1e4, actor.cached_contact.normal);
    vector(body, 0x1f0, source_pending_acceleration());
    vector(body, 0x1fc, source_pending_moment());
    vector(body, 0x268, state.previous_position_q16);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        vector(body, 0x150 + axis * 12, state.body.basis[axis]);
        vector(dynamics, 0x240 + axis * 12, actor.tire_patch_offsets[axis]);
    }
    for (std::size_t index = 0; index < 8; ++index) {
        vector(body, 0xf0 + index * 12, state.body.contacts[index]);
        store(body, 0x1c4 + index * 4, std::uint32_t(state.body.surfaces[index].height));
        body[0x517 + index] = !actor.cached_contact.active[index];
        body[0x521 + index] = actor.cached_contact.indices[index];
        store(dynamics, 0x264 + index * 2, state.body.surfaces[index].triangle, 2);
        dynamics[0x328 + index] = actor.cached_contact.surface[index];
    }
    body[0x51f] = std::uint8_t(actor.cached_contact.count);
    body[0x520] = actor.cached_contact.airborne;
    for (std::size_t axle = 0; axle < 2; ++axle) {
        vector(dynamics, 0x1b4 + axle * 12, state.wheel_forward_q16[axle * 2]);
        vector(dynamics, 0x1cc + axle * 12, state.wheel_side_q16[axle * 2]);
    }
    vector(dynamics, 0xfc, state.wheel_speed_q16);
    vector(dynamics, 0x1e4, state.longitudinal_slip_q16);
    vector(dynamics, 0x1f4, state.lateral_slip_q16);
    store(dynamics, 0x278, std::uint16_t(state.car_visual_angles.local_x), 2);
    store(dynamics, 0x27a, std::uint16_t(state.car_visual_angles.absolute_z), 2);
    vector(dynamics, 0x280, state.rumble);
    vector(dynamics, 0x290, state.previous_rumble);
    store(dynamics, 0x2a0, state.rollover.upside_down);
    store(dynamics, 0x2a4, state.rollover.sideways);
    store(dynamics, 0x2a8, state.rollover.pitched);
    store(dynamics, 0x2bc, actor.tire_patches_shifted);
    store(dynamics, 0x2c0, state.tyre_gate);
    store(dynamics, 0x31c, state.rollover.player_righting);
    for (std::size_t index = 0; index < state.rollover.ticks.size(); ++index) {
        dynamics[0x331 + index] = state.rollover.ticks[index];
    }
    store(body, 0x4ec, state.damage.feedback_strength, 2);
    body[0x514] = state.damage.feedback_side;
}

void Motion::prepare_source_local_input(const SourceLocalInputBoundary& boundary) {
    auto& controls = impl_->controls;
    controls.set_source_start_hold(boundary.start_held);
    if (boundary.reset_player_tires) {
        controls.reset_player_tire_offsets();
    }
    if (boundary.begin_launch) {
        controls.begin_launch_adjustment();
    }
    if (boundary.advance_launch) {
        controls.advance_launch_adjustment();
    }
}

void Motion::set_source_start_hold(bool held) {
    impl_->controls.set_source_start_hold(held);
}

void Motion::update_source_distant(const SourceInput& input, const TimeStep& time, bool defer_ground) {
    impl_->controls.set_source_input(input);
    impl_->update_distant(time, defer_ground, true);
}

std::array<std::int32_t, 3> Motion::source_pending_acceleration() const {
    return add(impl_->pending_prop_force, impl_->pending_car_force);
}

std::array<std::int32_t, 3> Motion::source_pending_moment() const {
    return add(add(impl_->pending_barrier_torque, impl_->pending_prop_torque), impl_->pending_car_torque);
}

void Motion::update_source_snapshot(const TimeStep& time, bool defer_ground) {
    impl_->update({}, time, true, defer_ground);
}

void Motion::update_source_input(const SourceInput& input, const TimeStep& time, bool defer_ground) {
    impl_->controls.set_source_input(input);
    impl_->update({}, time, true, defer_ground);
}

void Motion::set_start_hold(bool held) {
    impl_->controls.set_start_hold(held);
}

bool Motion::start_held() const {
    return impl_->controls.start_held();
}

void Motion::prepare_ai(AiDriver driver) {
    if (impl_->ai_owned) {
        throw std::runtime_error("Cannot replace an active AI driver");
    }
    //434f00 initializes front lights from the weather globals.402be0 later
    //changes controller ownership without resetting this retained light latch.
    driver.initialize_headlights(impl_->weather_id >= 3);
    AiTrafficContext traffic;
    traffic.qualifying = impl_->qualifying_session;
    traffic.headlight_gates = impl_->weather_id >= 3;
    driver.set_traffic_context(traffic);
    impl_->ai = std::move(driver);
}

void Motion::set_ai_route_point(std::uint32_t next_point) {
    impl_->ai_route_point = next_point;
}

void Motion::set_pit_target(std::optional<std::array<std::int32_t, 3>> target) {
    if (!impl_->ai) {
        throw std::runtime_error("Pit guidance requires source route and AIBIN configuration");
    }
    impl_->ai->set_pit_target(target);
}

void Motion::set_barrier_contacts_enabled(bool enabled) {
    // 409c30 observes vehicle+510. Ground and prop contacts remain active.
    impl_->barrier_contacts_enabled = enabled;
}

void Motion::handoff_to_ai(bool finished) {
    if (!impl_->ai) {
        throw std::runtime_error("AI handoff requires source route and AIBIN configuration");
    }
    //402be0 changes ownership without writing body+508. Preserve that
    //shared output until the AI mapper performs its next lamp write.
    auto mapped = impl_->ai->mapped_control_state();
    mapped.brake_lights = brake_lights();
    impl_->ai->restore_source_controls(mapped);
    // A finish can occur during pit guidance. Refresh the finished regime even
    // when this actor already owns AI controls; preserve its live body state.
    impl_->ai->handoff(impl_->controls.state().gear, finished);
    impl_->controls.handoff_to_ai(impl_->ai->tire_offset_q16());
    impl_->ai_owned = true;
}

void Motion::restore_player_control() {
    const auto lamps = brake_lights();
    impl_->controls.restore_player_control();
    if (impl_->controls.controller_handoff_state().human != 0) {
        impl_->ai_owned = false;
        impl_->frame_brake_lights = lamps;
    }
}

void Motion::source_handoff_to_ai(std::int32_t tire_base_q16) {
    impl_->controls.handoff_to_ai(tire_base_q16);
}

void Motion::restore_qualifying_control() {
    const auto lamps = brake_lights();
    impl_->controls.restore_qualifying_control();
    if (impl_->controls.controller_handoff_state().human != 0) {
        impl_->ai_owned = false;
        impl_->frame_brake_lights = lamps;
    }
}

bool Motion::ai_controlled() const {
    return impl_->ai_owned;
}

bool Motion::ai_headlights() const {
    return impl_->ai && impl_->ai_owned && impl_->ai->headlights_requested();
}

bool Motion::brake_lights() const {
    if (impl_->ai_owned) {
        return impl_->ai && impl_->ai->brake_lights_requested();
    }
    return impl_->frame_brake_lights != 0;
}

void Motion::publish_frame_brake_pedal() {
    publish_source_frame_brake_pedals(std::span(&impl_->controls.state().raw_brake_q16, 1), std::span(&impl_->state.published_brake_q16, 1));
}

void Motion::publish_frame_brake_lights(std::uint32_t view) {
    const auto& controls = impl_->controls.state();
    impl_->frame_brake_lights =
        source_frame_brake_lights(brake_lights(), std::int32_t(impl_->controls.controller_handoff_state().human), impl_->state.published_brake_q16, controls.reverse_pedal, view);
}

void Motion::reset(const scene::BodyPose& initial_body, bool qualifying) {
    auto next = std::make_unique<Impl>(impl_->surface, initial_body, impl_->car_id, impl_->manual_selection, impl_->weather_id, qualifying, impl_->damage_context, impl_->roughness,
        impl_->barriers, impl_->low_gravity);
    next->actor_id = impl_->actor_id;
    next->human_identity = impl_->human_identity;
    next->damage_feedback_enabled = impl_->damage_feedback_enabled;
    next->model_detail = impl_->model_detail;
    next->damage_registration = impl_->damage_registration;
    next->controls.set_start_hold(!qualifying);
    impl_ = std::move(next);
}

void Motion::reset_for_race(const scene::BodyPose& initial_body) {
    reset(initial_body, false);
}

void Motion::reset_for_qualifying(const scene::BodyPose& initial_body) {
    reset(initial_body, true);
}

void Motion::prepare_first_race_step(const scene::BodyPose& body) {
    auto& actor = *impl_;
    actor.state.body = body;
    actor.cached_contact.normal = body.ground_normal;
    for (std::size_t corner = 0; corner < body.surfaces.size(); ++corner) {
        const auto material = surface_class(body.surfaces[corner].flags);
        actor.cached_contact.surface[corner] = material;
        if (corner < actor.state.surface_ids.size()) {
            actor.state.surface_ids[corner] = material;
        }
    }
    //40b360/444830 replace previous position before consuming it. Ground-dot,
    //support flags/list tails and other inherited caches keep their contract.
}

void Motion::prepare_first_qualifying_step(const scene::BodyPose& body) {
    prepare_first_race_step(body);
    impl_->state.velocity_q16 = scale(normalized(tangent_with_negative_product(body.basis[0], body.basis[1])), 0xcccc);
    impl_->state.wheel_speed_q16.fill(0x33330);
}

void Motion::release_ai_start() {
    if (!impl_->ai) {
        throw std::runtime_error("AI start release requires a prepared driver");
    }
    impl_->controls.set_source_start_hold(false);
    impl_->ai->release_start();
}

void Motion::set_ai_release_counter(std::uint32_t value) {
    if (impl_->ai) {
        impl_->ai->set_release_counter(value);
    }
}

void Motion::set_ai_start_wait(bool grid, bool reaction) {
    if (impl_->ai) {
        impl_->ai->set_start_wait(grid, reaction);
    }
}

void Motion::observe_rival_ai() {
    if (!impl_->ai || !impl_->ai_owned) {
        throw std::runtime_error("Rival route observation requires AI ownership");
    }
    observe_ai_route();
}

void Motion::observe_ai_route() {
    if (impl_->ai) {
        impl_->ai->observe_route(impl_->state.body, mul(impl_->state.speed_q16, 0x640000), impl_->ai_route_point);
        impl_->rival_ai_observed = true;
    }
}

void Motion::recover_ai_controls() {
    if (!impl_->ai_owned || !impl_->ai) {
        return;
    }
    const auto& previous = impl_->controls.state();
    const DriverAxes axes{previous.raw_left_q16, previous.raw_right_q16, previous.raw_throttle_q16, previous.raw_brake_q16};
    impl_->ai->synchronize_source_physical_controls(previous.gear, previous.reverse_pedal, &axes);
    impl_->ai->advance_source_reverse_recovery(mul(impl_->state.speed_q16, 0x640000), impl_->ai_barrier_contact);
    impl_->ai_reverse_prepared = true;
}

void Motion::prepare_ai_controls() {
    if (!impl_->ai_owned || !impl_->ai) {
        return;
    }
    if (impl_->prepared_ai_axes) {
        throw std::runtime_error("AI controls were already prepared for this body step");
    }
    if (!impl_->ai_reverse_prepared) {
        throw std::runtime_error("AI reverse pass must precede descending control preparation");
    }
    impl_->prepared_ai_axes = impl_->ai_axes(impl_->state.rollover.upside_down || impl_->state.rollover.sideways || impl_->state.rollover.pitched, false);
}

void Motion::decide_rival_traffic(const AiTraffic& self, std::span<const AiTraffic> fleet, std::int32_t route_length_q16) {
    if (impl_->ai) {
        impl_->ai->decide_traffic(impl_->state.body, mul(impl_->state.speed_q16, 0x640000), self, fleet, route_length_q16);
    }
}

void Motion::decide_rival_traffic_brake(const AiTraffic& self, std::span<const AiTraffic> fleet) {
    if (impl_->ai) {
        impl_->ai->decide_traffic_brake(mul(impl_->state.speed_q16, 0x640000), self, fleet);
    }
}

AiTraffic Motion::ai_traffic_state() const {
    const auto speed = mul(impl_->state.speed_q16, 0x640000);
    auto result = impl_->ai ? impl_->ai->traffic_state(impl_->state.body, speed, impl_->ai_route_point) : AiTraffic{impl_->state.body, speed, impl_->ai_route_point};
    result.human = !impl_->ai_owned;
    return result;
}

std::uint8_t Motion::traffic_mode() const {
    return impl_->ai ? impl_->ai->traffic_mode() : 1;
}

void Motion::set_rival(bool distant) {
    if (!impl_->ai) {
        throw std::runtime_error("Rival body requires prepared AI");
    }
    impl_->player_identity = false;
    impl_->ai->set_rival(true, distant);
}

void Motion::update_distant(const TimeStep& time, bool defer_ground) {
    impl_->update_distant(time, defer_ground);
}

void Motion::add_car_contact(
    const std::array<std::int32_t, 3>& velocity_delta, const std::array<std::int32_t, 3>& local_moment, const std::array<std::int32_t, 3>& position_delta) {
    impl_->pending_car_force = add(impl_->pending_car_force, velocity_delta);
    if (dot(impl_->pending_car_torque, impl_->state.angular_velocity_q16) >= 0) {
        impl_->pending_car_torque = add(impl_->pending_car_torque, local_moment);
    }
    for (auto& component : impl_->pending_car_torque) {
        component = std::clamp(component, -2 * one, 2 * one);
    }
    shift_car_contact(position_delta);
}

void Motion::shift_car_contact(const std::array<std::int32_t, 3>& position_delta) {
    // 42b570 translates the retained world corners, preserving their rounding.
    auto& body = impl_->state.body;
    body.position = add(body.position, position_delta);
    for (auto& corner : body.contacts) {
        corner = add(corner, position_delta);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        body.object_to_world[12 + axis] = float(body.position[axis]) / one;
    }
}

const std::array<std::int32_t, 3>& Motion::pending_car_moment() const {
    return impl_->pending_car_torque;
}

DamageResult Motion::apply_car_damage(const std::array<std::int32_t, 3>& local_arm, bool feedback_enabled) {
    auto& actor = *impl_;
    DamageInput input;
    input.contact.impulse_q16 = actor.pending_car_force;
    input.contact.local_contact_q16 = local_arm;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        input.inverse_half_dimensions_q16[axis] = reciprocal(actor.half[axis]);
    }
    input.car_id = actor.car_id;
    input.feedback_enabled = feedback_enabled;
    input.detach = [&actor](auto slot) {
        actor.damage_context.parts().detach(slot, {actor.state.body, actor.state.velocity_q16, actor.car_id, true, actor.model_detail, actor.actor_id, actor.damage_registration});
    };
    return actor.damage_context.apply(actor.state.damage, input);
}

void Motion::set_damage_feedback_enabled(bool enabled) {
    impl_->damage_feedback_enabled = enabled;
}

bool Motion::damage_feedback_enabled() const {
    return impl_->damage_feedback_enabled;
}

void Motion::set_actor_identity(std::uint32_t actor, bool human) {
    if (actor >= 16) {
        throw std::runtime_error("Damage owner requires an original actor index0..15");
    }
    impl_->actor_id = actor;
    impl_->human_identity = human;
}

void Motion::set_model_detail(std::uint8_t detail) {
    impl_->model_detail = detail;
}

void Motion::set_damage_registration(std::uint16_t sector) {
    impl_->damage_registration = sector;
}

void Motion::apply_prop_response(const PropResponse& response) {
    impl_->pending_prop_force = add(impl_->pending_prop_force, response.force_q16);
    impl_->pending_prop_torque = add(impl_->pending_prop_torque, response.torque_q16);
    impl_->state.prop_impact_q16 = std::max(impl_->state.prop_impact_q16, std::clamp(response.impact_strength_q16, 0, one));
}

void Motion::begin_tick() {
    impl_->prepared_ai_axes.reset();
    impl_->ai_reverse_prepared = false;
    impl_->ai_barrier_contact = impl_->state.barrier_contact_count != 0;
    impl_->body_prepared = false;
    impl_->suppress_ai_prepared = false;
    impl_->pending_full_ground = false;
    impl_->pending_distant_ground = false;
    impl_->saved_before_ground.reset();
    impl_->saved_controls_before_ground.reset();
    impl_->state.impacts.clear();
    impl_->state.prop_impact_q16 = 0;
    impl_->state.wheel_feedback_side = 0;
    impl_->state.wheel_feedback_strength = 0;
    impl_->state.barrier_effects = {};
    impl_->state.barrier_contact_count = 0;
    impl_->state.barrier_sector = -1;
}

void Motion::update(const ControlsInput& input, const TimeStep& time, bool defer_ground) {
    impl_->update(input, time, false, defer_ground);
}

bool Motion::prepare_body_update(bool player_identity) {
    impl_->player_identity = player_identity;
    auto& rollover = impl_->state.rollover;
    impl_->suppress_ai_prepared = rollover.upside_down || rollover.sideways || rollover.pitched;
    impl_->body_prepared = true;
    return !prepare_rollover(rollover, player_identity, impl_->state.body, impl_->state.angular_velocity_q16);
}

void Motion::finish_ground(BodyMode mode) {
    switch (mode) {
        case BodyMode::full:
            //415240 still updates support after415090's early dispatcher
            //return, including actors that did not integrate this tick.
            impl_->pending_full_ground = true;
            impl_->finish_full_ground();
            break;
        case BodyMode::distant:
            impl_->pending_distant_ground = true;
            impl_->finish_distant_ground();
            break;
        case BodyMode::crashing:
            settle_rollover();
            break;
        case BodyMode::crashed:
        case BodyMode::removed:
            break;
    }
}

void Motion::settle_rollover() {
    auto& actor = *impl_;
    auto& state = actor.state;
    if (state.boundary != MotionBoundary::none) {
        return;
    }
    state.previous_position_q16 = state.body.position;
    state.velocity_q16 = {};
    state.angular_velocity_q16 = {};
    state.wheel_speed_q16 = {};
    actor.pending_car_force = {};
    actor.pending_car_torque = {};
    actor.pending_barrier_torque = {};
    actor.pending_prop_force = {};
    actor.pending_prop_torque = {};
    actor.controls.clear_steering_angle();
    if (actor.barriers && actor.barrier_contacts_enabled) {
        const auto response =
            resolve_barriers(*actor.barriers, state.body, state.velocity_q16, state.previous_position_q16, actor.car_id, state.barrier_recovery, actor.controls.state().gear == 7);
        state.barrier_contact_count = response.contacts;
        state.barrier_effects = response.effects;
    }
    const auto sample = actor.surface.prop_sample_retaining_cached(state.body.position[0], state.body.position[2], state.body.surfaces[0].triangle);
    if (!sample) {
        state.boundary = MotionBoundary::missing_surface;
        return;
    }
    state.body.surfaces[0].triangle = sample->triangle;
    if ((sample->flags & 0x7f) != 1) {
        state.body.ground_normal = sample->normal;
    }
    actor.cached_contact.normal = state.body.ground_normal;
    settle_rollover_pose(state.body, actor.car_id, sample->height, state.rollover);
    state.surface_ids[0] = surface_class(sample->flags & 0x7f);
}

scene::CarLightGroundInput Motion::car_light_ground_input(bool distant_body) const {
    const auto& state = impl_->state;
    scene::CarLightGroundInput result;
    //4608b0 publishes physical+f0/+1c4 into render+464/+42c. These
    //sampled heights stay independent of whether a wheel is touching.
    for (std::size_t wheel = 0; wheel < result.wheel_position.size(); ++wheel) {
        result.wheel_position[wheel] = state.body.contacts[wheel];
        result.ground_height[wheel] = state.body.surfaces[wheel].height;
    }
    result.surface = state.surface_ids;
    result.body_height = impl_->half[1]; //40a770 body+24.
    result.distant_body = distant_body; //body+4f0==1, independent of airborne.
    return result;
}

scene::EngineEffectVehicle Motion::engine_effect_vehicle() const {
    const auto& state = impl_->state;
    const auto& controls = impl_->controls.state();
    scene::EngineEffectVehicle result;
    result.position = state.body.position;
    result.basis = state.body.basis;
    result.engine_speed_q16 = controls.engine_speed_q16;
    result.previous_engine_speed_q16 = state.previous_engine_speed_q16;
    result.car_type = std::uint8_t(impl_->car_id);
    result.gear = controls.gear;
    result.actor = std::uint8_t(impl_->actor_id);
    result.local_player = impl_->human_identity;
    return result;
}

scene::SurfaceEffectVehicle Motion::surface_effect_vehicle() const {
    const auto& state = impl_->state;
    scene::SurfaceEffectVehicle result;
    result.position = state.body.position;
    result.velocity = state.velocity_q16;
    result.support_normal = state.body.ground_normal;
    result.basis = state.body.basis;
    // 40a770's vehicle+38 is the source lookup radius of the half-extents.
    result.body_radius = fixed::length(impl_->half);
    result.front_drive = impl_->car_id == 1 ? 0xa666 : impl_->car_id == 8 ? 0x8000 : impl_->car_id == 9 ? 0 : 0x10000;
    result.gear_ratio = ratios[std::size_t(impl_->controls.state().gear)];
    result.airborne = state.contact_count == 0;
    result.tyre_gate = state.tyre_gate;
    result.barrier_recovery = state.barrier_recovery.active;
    // Local-player identity survives AI handoffs; View2 shows one plume.
    result.secondary_plume = false;
    for (std::size_t wheel = 0; wheel < result.wheels.size(); ++wheel) {
        result.wheels[wheel] = {state.body.contacts[wheel], state.wheel_forward_q16[wheel], state.wheel_side_q16[wheel], state.body.surfaces[wheel].height,
            state.longitudinal_slip_q16[wheel], state.lateral_slip_q16[wheel], state.surface_ids[wheel], !state.wheel_contact[wheel]};
    }
    return result;
}

scene::GroundScrapeInput Motion::ground_scrape_input() const {
    scene::GroundScrapeInput result;
    result.body = impl_->state.body;
    result.velocity = impl_->state.velocity_q16;
    result.active = impl_->cached_contact.active;
    result.surface_ids = impl_->cached_contact.surface;
    result.car_id = impl_->car_id;
    result.tyre_contact = impl_->state.tyre_gate;
    result.rain = impl_->weather_id == 3 || impl_->weather_id == 5;
    result.snow = impl_->weather_id == 4;
    // The local full-physics actor retains source ownership+4f0==0, including
    // AI handoff. Linked-barrier recovery uses a separate state and is no gate.
    result.visible = impl_->state.boundary == MotionBoundary::none;
    return result;
}

const MotionState& Motion::state() const {
    return impl_->state;
}

std::int32_t Motion::retained_ground_up_q16() const {
    return impl_->ground_dot[1];
}

const VehicleState& Motion::controls() const {
    return impl_->controls.state();
}

std::int32_t Motion::limiter_load_q16() const {
    return impl_->controls.limiter_load_q16();
}

PedalFilterState Motion::pedal_filter_state() const {
    return impl_->controls.pedal_filter_state();
}

SteeringFilterState Motion::steering_filter_state() const {
    return impl_->controls.steering_filter_state();
}

const DamageState& Motion::damage() const {
    return impl_->state.damage;
}

void Motion::apply_car_boost(const CarBoostResult& parameters) {
    impl_->controls.apply_car_boost(parameters);
}

} // namespace toca::simulation
