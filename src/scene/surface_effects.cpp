#include "scene/surface_effects.hpp"

#include "simulation/fixed.hpp"
#include "simulation/source_transform.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace toca::scene {
namespace fixed = simulation::fixed;

namespace {
using namespace simulation::fixed;

bool driven(const SurfaceEffectVehicle& vehicle, std::size_t wheel) {
    return vehicle.front_drive < 0x8000 ? wheel >= 2 : wheel < 2;
}

std::int32_t angle_degrees(std::int32_t cosine) {
    const auto index = std::uint32_t(std::min(fixed::abs(cosine), 0xffff)) >> 4;
    // 478c48..478c6b converts the integer index to float, divides by the
    // DOUBLE4095 constant, stores a double argument, then truncates asin*65536.
    // The resulting4096 signed16 entries are DAT53d840, not a continuous angle.
    const auto radians = std::int32_t(std::asin(double(float(index)) / 4095.0) * one);
    auto arc = mul(radians, 0x28b);
    if (cosine < 0) {
        arc = neg(arc);
    }
    return fixed::abs(1024 - arc) * 0x1680;
}

SurfaceEffectSprite sprite(const Vector& center, std::int32_t radius, std::int32_t horizontal, std::int32_t shade0, std::uint8_t shade1) {
    return {center, {neg(radius), neg(horizontal), 0}, {radius, horizontal, 0}, std::uint8_t(shade0), shade1};
}
} // namespace

EffectPose effect_pose(std::uint8_t actor, const std::array<float, 16>& source, const std::array<float, 16>& rendered) {
    EffectPose result;
    result.actor = actor;
    result.unchanged = source == rendered;
    if (!result.unchanged) {
        const auto delta = glm::dmat4(glm::make_mat4(rendered.data())) * glm::inverse(glm::dmat4(glm::make_mat4(source.data())));
        std::copy_n(glm::value_ptr(delta), result.source_to_render.size(), result.source_to_render.begin());
    }
    return result;
}

SurfaceEffectVector effect_position(const SurfaceEffectVector& position, std::uint8_t actor, std::span<const EffectPose> poses) {
    const auto pose = std::ranges::find(poses, actor, &EffectPose::actor);
    if (pose == poses.end() || pose->unchanged) {
        return position;
    }
    SurfaceEffectVector result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        double value = pose->source_to_render[12 + axis] * 65536;
        for (std::size_t component = 0; component < 3; ++component) {
            value += pose->source_to_render[component * 4 + axis] * position[component];
        }
        result[axis] = std::int32_t(std::llround(value));
    }
    return result;
}

SurfaceEffects::SurfaceEffects(PropFragmentTables& tables, const SceneSetup& setup, bool low_gravity)
    : tables_(tables), wet_(setup.weather >= 3), gravity_(low_gravity ? 0 : 0x83), shade_(setup.weather == 5   ? 160
                                                                                          : wet_               ? 180
                                                                                          : setup.weather == 1 ? 255
                                                                                                               : 240) {
    if (setup.weather < 1 || setup.weather > 5 || tables.cursor >= tables.positive.size()) {
        throw std::runtime_error("Surface effects require resolved original weather and a valid random-table cursor");
    }
    reset();
}

void SurfaceEffects::next() {
    tables_.cursor = (tables_.cursor + 1) % 37;
}

SurfaceEffectVector SurfaceEffects::slip(const SurfaceEffectWheel& wheel) const {
    // 4316d0 deliberately flattens both tyre axes before these products.
    const Vector forward{wheel.forward[0], 0, wheel.forward[2]}, side{wheel.side[0], 0, wheel.side[2]};
    return add(scale(forward, wheel.longitudinal_slip), scale(side, wheel.lateral_slip));
}

SurfaceEffects::Particle& SurfaceEffects::allocate(std::size_t group) {
    const auto slot = free_.back();
    free_.pop_back();
    groups_[group].insert(groups_[group].begin(), slot);
    particles_[slot] = {};
    ++emitted_;
    return particles_[slot];
}

void SurfaceEffects::reset() {
    for (auto& group : groups_) {
        group.clear();
    }
    free_.clear();
    for (std::uint8_t slot = 0; slot < 80; ++slot) {
        free_.push_back(slot);
    }
    emitters_ = {};
}

void SurfaceEffects::advance(std::span<const SurfaceEffectVehicle> vehicles) {
    for (const auto& vehicle : vehicles) {
        if (vehicle.actor >= emitters_.size()) {
            throw std::runtime_error("Surface effects support the original sixteen car slots");
        }
        emitters_[vehicle.actor].vehicle = vehicle;
    }
    ++updates_;
    for (std::size_t group = 0; group < groups_.size(); ++group) {
        auto& slots = groups_[group];
        for (auto iterator = slots.begin(); iterator != slots.end();) {
            auto& particle = particles_[*iterator];
            const bool ballistic = group < 3;
            if (particle.remaining == 0 || (ballistic && sub(particle.initial_y, particle.position[1]) > 0x1998)) {
                free_.push_back(*iterator);
                iterator = slots.erase(iterator);
                continue;
            }
            particle.previous_position = particle.position;
            particle.previous_radius = particle.radius;
            particle.previous_alpha = particle.alpha;
            particle.has_previous = true;
            particle.position = add(particle.position, particle.velocity);
            if (ballistic) {
                particle.velocity[1] = sub(particle.velocity[1], gravity_);
            } else {
                particle.radius = add(particle.radius, particle.growth);
                particle.position[1] = add(particle.position[1], particle.growth);
                particle.alpha = std::uint8_t(particle.alpha - particle.decay);
            }
            --particle.remaining;
            if (group == 3) {
                const auto& vehicle = emitters_[particle.actor].vehicle;
                const auto& wheel = vehicle.wheels[particle.wheel];
                const auto delta = sub(particle.position, wheel.position);
                const auto normal = scale(vehicle.support_normal, dot(vehicle.support_normal, delta));
                const auto planar = sub(delta, normal);
                const auto limit = (10 - particle.remaining) * 0xb333;
                if (length(planar) > limit) {
                    particle.position = add(add(wheel.position, scale(normalized(planar), limit)), normal);
                }
            }
            ++iterator;
        }
    }
}

void SurfaceEffects::debris(const SurfaceEffectVehicle& vehicle, std::size_t wheel_index, std::size_t group) {
    const auto& wheel = vehicle.wheels[wheel_index];
    if ((group == 1 && !driven(vehicle, wheel_index)) || wheel.longitudinal_slip == 0 || free_.empty()) {
        return;
    }
    const auto longitudinal = wheel.longitudinal_slip;
    if (((longitudinal > 0 && vehicle.gear_ratio > 0) || (longitudinal < 0 && vehicle.gear_ratio < 0)) && fixed::abs(wheel.lateral_slip) < fixed::abs(longitudinal)) {
        return;
    }
    const auto slip_velocity = slip(wheel);
    const auto speed = length(slip_velocity);
    if (speed < 1) {
        return;
    }
    if (group == 2 && wet_ && tables_.cursor % 2 == 0) {
        group = 0;
    }
    const auto strength = std::min(mul(speed, 0x23851), one), launch_speed = mul(strength, 0x47ae);
    next();
    const auto launch = scale(scale(slip_velocity, reciprocal(speed)), launch_speed);
    const auto vertical = mul(launch_speed, mul(tables_.positive[tables_.cursor], 0x9999));
    const auto jitter = mul(tables_.signed_values[tables_.cursor], 0x1999);
    Vector side{wheel.side[0], 0, wheel.side[2]};
    auto velocity = add(add(launch, Vector{mul(neg(launch[2]), jitter), 0, mul(launch[0], jitter)}), scale(side, dot(side, vehicle.velocity)));
    velocity[1] = vertical;
    const auto local = simulation::source_transform::local(velocity, vehicle.basis);
    const bool rejected = (wheel_index < 2 ? local[0] < 0 : local[0] > 0) && (wheel_index % 2 == 0 ? local[2] < 0 : local[2] > 0);
    if (!rejected) {
        auto& particle = allocate(group);
        particle.position = wheel.position;
        particle.position[1] = wheel.ground_height;
        particle.initial_y = wheel.ground_height;
        particle.velocity = velocity;
        particle.radius = 0x1999;
        particle.remaining = 15;
        particle.visibility = vehicle.visibility;
        particle.actor = vehicle.actor;
        particle.wheel = std::uint8_t(wheel_index);
    }
    next();
}

void SurfaceEffects::cloud(const SurfaceEffectVehicle& vehicle, std::size_t wheel_index, bool dust) {
    const auto adjusted = add(slip(vehicle.wheels[wheel_index]), scale(vehicle.velocity, dust ? -0x8000 : 0));
    if (dust && !driven(vehicle, wheel_index)) {
        return;
    }
    const auto excess = sub(length(adjusted), dust ? 0xccc : 0x5999);
    if (excess <= 0 || free_.empty()) {
        return;
    }
    if (dust && (vehicle.front_drive < 0x8000 ? vehicle.gear_ratio < 0 : vehicle.gear_ratio > 0)) {
        wheel_index = (wheel_index + 2) & 3;
    }
    const auto& wheel = vehicle.wheels[wheel_index];
    auto& particle = allocate(dust ? 4 : 3);
    particle.position = wheel.position;
    if (dust) {
        particle.position = add(particle.position, scale(vehicle.basis[2], wheel_index % 2 == 0 ? -0x6666 : 0x6666));
    }
    particle.radius = dust ? 0x147a : 0x1999;
    particle.position[1] = add(wheel.ground_height, particle.radius);
    const auto direction = normalized(Vector{neg(adjusted[2]), 0, adjusted[0]});
    const auto random = tables_.signed_values[tables_.cursor];
    particle.velocity = scale(direction, mul(random, dust ? 0xccc : 0x51e));
    particle.alpha = dust ? 200 : 128;
    particle.decay = dust ? 10 : 11;
    particle.remaining = 10;
    particle.visibility = vehicle.visibility;
    particle.actor = vehicle.actor;
    particle.wheel = std::uint8_t(wheel_index);
    const auto scaled_excess = mul(excess, dust ? 0x8000 : 0x640000), growth_jitter = mul(random, dust ? 0xccc : 0x28f);
    particle.velocity[1] = 0;
    particle.growth = scaled_excess <= one ? mul(add(scaled_excess, growth_jitter), 0xf5c) : add(0xf5c, growth_jitter);
    next();
}

void SurfaceEffects::road_spray(Emitter& emitter, std::size_t wheel_index, const Vector& projected, std::int32_t speed, bool& suppressed) {
    if (suppressed) {
        return;
    }
    const auto& vehicle = emitter.vehicle;
    const auto& forward = vehicle.wheels[2].forward;
    if (!emitter.trail) {
        const auto projection = dot(forward, projected);
        if ((projection > 0 && vehicle.gear_ratio < 0) || (projection < 0 && vehicle.gear_ratio > 0) || speed < 1) {
            suppressed = true;
            return;
        }
        const auto direction = scale(projected, reciprocal(vehicle.gear_ratio < 0 ? neg(speed) : speed));
        const auto angle = angle_degrees(dot(direction, forward));
        if (angle > 0x59ffff) {
            suppressed = true;
            return;
        }
        const auto factor = std::min(mul(speed, 2 * one), one);
        const auto opacity = mul(sub(one, std::min(mul(angle, 0x2d0), one)), 100 * one);
        emitter.trail_opacity = mul(opacity, factor) / one;
        emitter.trail_decay = (mul(factor, 15 * one) + 5 * one) / one;
        emitter.trail_growth = mul(factor, 0x1999);
        emitter.trail_step = scale(forward, std::clamp(neg(projection), -0x4ccc, 0x4ccc));
        emitter.trail = true;
    }
    emitter.wheels[wheel_index] = true;
}

void SurfaceEffects::plume(Emitter& emitter, const Vector& direction, std::int32_t speed) {
    bool grass = false;
    for (const auto& wheel : emitter.vehicle.wheels) {
        grass |= wheel.surface == 2;
    }
    if (wet_ || emitter.active || grass) {
        if (speed <= 0x3333) {
            if (!wet_) {
                emitter.active = false;
                emitter.opacity = 0;
            }
        } else {
            const auto factor = std::min(sub(speed, 0x3333), one);
            std::int32_t opacity = 0, second_opacity = 0;
            if (wet_) {
                const auto variation = mul(tables_.positive[tables_.cursor], 10 * one);
                next();
                const auto base = mul(factor, 50 * one), candidate = sub(base, variation);
                const auto selected = candidate > 10 * one ? candidate : base;
                opacity = selected / one;
                // The second puff uses selected opacity, after source uVar10
                // has been replaced by the above branch, not the base value.
                second_opacity = mul(selected, 0xe666) / one;
            } else {
                if (grass) {
                    next();
                }
                const auto target = grass ? 254 : 0;
                opacity = emitter.opacity + std::clamp(target - emitter.opacity, -10, 10);
                second_opacity = mul(opacity * one, 0xe666) / one;
            }
            if (opacity == 0) {
                emitter.active = false;
                emitter.opacity = 0;
            } else {
                emitter.radius = add(sub(mul(factor, 0x7333), mul(tables_.positive[tables_.cursor], 0x1999)), 0x8000);
                next();
                emitter.second_radius = mul(emitter.radius, 0x13333);
                emitter.opacity = opacity;
                emitter.second_opacity = second_opacity;
                emitter.offset = scale(direction, neg(mul(factor, 0x18000)));
                emitter.offset[1] = add(emitter.offset[1], sub(emitter.second_radius, emitter.radius));
                emitter.active = true;
            }
        }
    }
    if (emitter.active) {
        const auto& vehicle = emitter.vehicle;
        const auto& wheel = vehicle.wheels[0];
        const auto delta = sub(vehicle.position, wheel.position);
        emitter.center = add(add(wheel.position, scale(direction, neg(vehicle.body_radius))), project(delta, vehicle.support_normal));
        emitter.center[1] = add(emitter.center[1], emitter.radius);
        emitter.second = add(emitter.center, emitter.offset);
    }
}

void SurfaceEffects::emit(std::span<const SurfaceEffectVehicle> vehicles) {
    for (const auto& vehicle : vehicles) {
        if (vehicle.actor >= emitters_.size()) {
            throw std::runtime_error("Surface effects support the original sixteen car slots");
        }
        auto& emitter = emitters_[vehicle.actor];
        emitter.vehicle = vehicle;
        if (vehicle.visibility == 0) {
            emitter.active = emitter.trail = false;
            emitter.wheels = {};
            continue;
        }
        if (wet_) {
            emitter.active = emitter.trail = false;
            emitter.wheels = {};
        }
        auto projected = vehicle.velocity;
        const auto normal_speed = dot(vehicle.support_normal, vehicle.velocity);
        if (normal_speed > 0) {
            projected = sub(projected, scale(vehicle.support_normal, normal_speed));
        }
        const auto speed = length(projected);
        if (!vehicle.barrier_recovery) {
            if (vehicle.airborne && sub(vehicle.wheels[0].position[1], vehicle.wheels[0].ground_height) >= 0x8000) {
                continue;
            }
            if (speed <= 0 || !vehicle.tyre_gate) {
                continue;
            }
            bool suppressed = false;
            for (std::size_t wheel_index = 0; wheel_index < 4; ++wheel_index) {
                const auto& wheel = vehicle.wheels[wheel_index];
                if (fixed::abs(sub(wheel.position[1], wheel.ground_height)) >= 0x3333) {
                    continue;
                }
                switch (wheel.surface) {
                    case 0:
                        if (wet_) {
                            road_spray(emitter, wheel_index, projected, speed, suppressed);
                        } else {
                            cloud(vehicle, wheel_index, false);
                        }
                        break;
                    case 2:
                        debris(vehicle, wheel_index, 2);
                        break;
                    case 3:
                        debris(vehicle, wheel_index, 1);
                        if (!wet_) {
                            cloud(vehicle, wheel_index, true);
                        }
                        break;
                }
            }
            plume(emitter, scale(projected, reciprocal(speed)), speed);
        } else if (wet_ && speed > 0) {
            plume(emitter, scale(projected, reciprocal(speed)), speed);
        }
    }
}

void SurfaceEffects::build_frame(SurfaceEffectFrame& result, float alpha, std::span<const EffectPose> poses, std::uint8_t view_mask) const {
    constexpr std::array textures{
        SurfaceEffectTexture::spray, SurfaceEffectTexture::mud, SurfaceEffectTexture::gravel, SurfaceEffectTexture::grass, SurfaceEffectTexture::smoke, SurfaceEffectTexture::dust};
    result.updates = updates_;
    result.emitted = emitted_;
    result.particles = 80 - free_.size();
    result.plumes = 0;
    result.kind_mask = 0;
    for (std::size_t batch = 0; batch < 6; ++batch) {
        result.batches[batch].texture = textures[batch];
        result.batches[batch].flags = batch == 0 ? 6 : 2;
        result.batches[batch].sprites.clear();
    }
    auto& spray = result.batches[0].sprites;
    if (wet_) {
        // 4347d0 visits every base plume before any wheel trails, cap199.
        for (const auto& emitter : emitters_) {
            if (!emitter.active || !(emitter.vehicle.visibility & view_mask)) {
                continue;
            }
            if (spray.size() == 199) {
                break;
            }
            spray.push_back(sprite(effect_position(emitter.center, emitter.vehicle.actor, poses), emitter.radius, mul(emitter.radius, 0x16666), emitter.opacity, shade_));
            if (emitter.vehicle.secondary_plume && spray.size() < 199) {
                spray.push_back(sprite(
                    effect_position(emitter.second, emitter.vehicle.actor, poses), emitter.second_radius, mul(emitter.second_radius, 0x16666), emitter.second_opacity, shade_));
            }
        }
        for (const auto& emitter : emitters_) {
            if (!emitter.trail || !(emitter.vehicle.visibility & view_mask)) {
                continue;
            }
            for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                if (!emitter.wheels[wheel] || emitter.trail_opacity < 1 || spray.size() == 199) {
                    continue;
                }
                auto center = emitter.vehicle.wheels[wheel].position;
                center[1] = add(center[1], 0x1999);
                auto radius = std::int32_t(0x3333), opacity = emitter.trail_opacity;
                spray.push_back(sprite(effect_position(center, emitter.vehicle.actor, poses), radius, 0x3333, opacity, shade_));
                for (int sample = 0; sample < 2 && spray.size() < 199; ++sample) {
                    opacity -= emitter.trail_decay;
                    if (opacity < 1) {
                        break;
                    }
                    center = add(center, emitter.trail_step);
                    center[1] = add(center[1], emitter.trail_growth);
                    radius = add(radius, emitter.trail_growth);
                    spray.push_back(sprite(effect_position(center, emitter.vehicle.actor, poses), radius, 0x3333, opacity, shade_));
                }
            }
        }
    }
    result.plumes = spray.size();
    for (std::size_t group = 0; group < groups_.size(); ++group) {
        auto& sprites = result.batches[group + 1].sprites;
        for (const auto slot : groups_[group]) {
            const auto& particle = particles_[slot];
            if (particle.visibility & view_mask) {
                auto center = particle.position;
                auto radius = particle.radius;
                auto opacity = particle.alpha;
                if (particle.has_previous && alpha != 1) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        center[axis] = std::int32_t(std::llround(std::lerp(double(particle.previous_position[axis]), double(center[axis]), double(alpha))));
                    }
                    radius = std::int32_t(std::llround(std::lerp(double(particle.previous_radius), double(radius), double(alpha))));
                    opacity = std::uint8_t(std::lround(std::lerp(float(particle.previous_alpha), float(opacity), alpha)));
                } else if (!particle.has_previous) {
                    center = effect_position(center, particle.actor, poses);
                }
                sprites.push_back(sprite(center, radius, radius, opacity, shade_));
            }
        }
    }
    for (const auto& batch : result.batches) {
        if (!batch.sprites.empty()) {
            result.kind_mask |= 1u << std::uint8_t(batch.texture);
        }
    }
}

const SurfaceEffectFrame& SurfaceEffects::frame(std::uint8_t view_mask) const {
    build_frame(frame_, 1, {}, view_mask);
    return frame_;
}

SurfaceEffectFrame SurfaceEffects::presentation_frame(float alpha, std::span<const EffectPose> poses, std::uint8_t view_mask) const {
    SurfaceEffectFrame result;
    presentation_frame_into(result, alpha, poses, view_mask);
    return result;
}

void SurfaceEffects::presentation_frame_into(SurfaceEffectFrame& result, float alpha, std::span<const EffectPose> poses, std::uint8_t view_mask) const {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Surface presentation requires a finite fraction");
    }
    build_frame(result, std::clamp(alpha, 0.f, 1.f), poses, view_mask);
}
} // namespace toca::scene
