#include "simulation/barrier_contact.hpp"

#include "simulation/fixed.hpp"
#include "simulation/source_transform.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>

namespace toca::simulation {
namespace {
using namespace fixed;

bool opposite_sign(std::int32_t first, std::int32_t second) {
    return (first < 0 && second > 0) || (first > 0 && second < 0);
}

struct Selected {
    const scene::BarrierSegment* segment = nullptr;
    std::size_t corner = 0;
};

void shift(scene::BodyPose& body, const Vector& correction) {
    body.position = add(body.position, correction);
    for (auto& point : body.contacts) {
        point = add(point, correction);
    }
}

std::int32_t divide(std::int32_t value, std::int32_t divisor) {
    return bits(std::uint32_t(std::int64_t(value) * one / divisor));
}

// 4086b0/408a00/409020: test both finite segment endpoints against the
// horizontal body box and move away along the previous-center endpoint ray.
bool endpoint(const Vector& point, scene::BodyPose& body, const Vector& previous, const Vector& half) {
    const auto delta = sub(point, body.position);
    const auto broad = bits(std::uint32_t(half[0]) << 2);
    for (auto value : delta) {
        if (fixed::abs(value) > broad) {
            return false;
        }
    }
    const auto radius = length(half);
    if (dot(delta, delta) >= mul(radius, radius)) {
        return false;
    }
    const auto& upright = fixed::abs(body.basis[0][1]) < 0xfd71 ? body.basis[0] : body.basis[1];
    const auto forward = normalized(Vector{upright[0], 0, upright[2]});
    const Vector side{forward[2], 0, neg(forward[0])};
    std::int32_t forward_half = 0, side_half = 0;
    for (const auto& corner : body.contacts) {
        const auto arm = sub(corner, body.position);
        forward_half = std::max(forward_half, dot(forward, arm));
        side_half = std::max(side_half, dot(side, arm));
    }
    const auto forward_position = dot(forward, delta), side_position = dot(side, delta);
    if (fixed::abs(forward_position) > forward_half || fixed::abs(side_position) > side_half) {
        return false;
    }
    const auto old_delta = sub(point, previous);
    const auto old_distance = length(Vector{old_delta[0], 0, old_delta[2]});
    if (old_distance < 1) {
        return false;
    }
    const auto direction = scale(Vector{old_delta[0], 0, old_delta[2]}, reciprocal(old_distance));
    const auto forward_direction = dot(forward, direction), side_direction = dot(side, direction);
    auto margin = std::int32_t(0x7d000000);
    const auto consider = [&](std::int32_t direction_value, std::int32_t extent, std::int32_t position) {
        if (direction_value > 0) {
            margin = std::min(margin, divide(sub(extent, position), direction_value));
        } else if (direction_value < 0) {
            margin = std::min(margin, divide(fixed::abs(add(position, extent)), neg(direction_value)));
        }
    };
    consider(forward_direction, forward_half, forward_position);
    consider(side_direction, side_half, side_position);
    if (margin == 0x7d000000) {
        return false;
    }
    shift(body, scale(scale(direction, -one), margin));
    return true;
}

// 4091e0: retain previous-center side and reject movement away from the plane.
std::optional<bool> approaching(const scene::BarrierSegment& segment, const scene::BodyPose& body, const Vector& previous, const Vector& velocity, const Vector& half) {
    const auto old_distance = dot(sub(segment.first, previous), segment.normal);
    const auto distance = dot(sub(segment.first, body.position), segment.normal);
    if ((distance <= 0 || old_distance >= 0) && (distance >= 0 || old_distance <= 0)) {
        if ((distance < 0 && distance < old_distance) || (distance > 0 && old_distance < distance)) {
            return std::nullopt;
        }
    }
    const auto speed = dot(segment.normal, velocity);
    if ((old_distance < 0 && speed > 0) || (old_distance > 0 && speed < 0)) {
        return std::nullopt;
    }
    const auto radius = length(half);
    if (fixed::abs(distance) > mul(radius, 0x20000)) {
        return std::nullopt;
    }
    return old_distance >= 0;
}

bool plane(const scene::BarrierSegment& segment, scene::BodyPose& body, Vector& velocity, const Vector& previous, const Vector& half, Vector& pending_torque, Selected& selected) {
    const auto old_positive = approaching(segment, body, previous, velocity, half);
    if (!old_positive) {
        return false;
    }
    std::array<std::int32_t, 8> distance{};
    std::array<std::size_t, 8> negative_bracket{}, positive_bracket{};
    std::size_t negatives = 0, positives = 0, negative_count = 0, positive_count = 0;
    const Vector tangent{neg(segment.normal[2]), 0, segment.normal[0]};
    for (std::size_t i = 0; i < 8; ++i) {
        const auto from_first = sub(segment.first, body.contacts[i]), from_second = sub(segment.second, body.contacts[i]);
        const auto first = dot(tangent, from_first), second = dot(tangent, from_second);
        distance[i] = dot(from_first, segment.normal);
        if (distance[i] < 0) {
            ++negative_count;
        } else {
            ++positive_count;
        }
        if (first == 0 || second == 0 || opposite_sign(first, second)) {
            if (distance[i] < 0) {
                negative_bracket[negatives++] = i;
            } else {
                positive_bracket[positives++] = i;
            }
        }
    }
    std::optional<std::size_t> chosen;
    auto displacement = *old_positive ? std::int32_t(0x27100000) : std::int32_t(-0x27100000);
    const auto choose = [&](std::size_t index) {
        if ((*old_positive && distance[index] < displacement) || (!*old_positive && distance[index] > displacement)) {
            displacement = distance[index];
            chosen = index;
        }
    };
    if (positives == 0 || negatives == 0) {
        if (positives == 0 && negatives == 0) {
            return false;
        }
        if (negative_count != 0 || *old_positive) {
            if (positive_count != 0 || !*old_positive) {
                return false;
            }
        }
        for (std::size_t i = 0; i < 8; ++i) {
            choose(i);
        }
    } else {
        if (*old_positive) {
            for (std::size_t i = 0; i < negatives; ++i) {
                choose(negative_bracket[i]);
            }
        } else {
            for (std::size_t i = 0; i < positives; ++i) {
                choose(positive_bracket[i]);
            }
        }
    }
    if (!chosen) {
        return false;
    }
    selected = {&segment, *chosen};
    if (displacement) {
        shift(body, scale(segment.normal, displacement));
    }
    const auto normal_speed = dot(segment.normal, velocity);
    if (normal_speed == 0) {
        return true;
    }
    const auto normal_velocity = scale(segment.normal, normal_speed);
    const auto change = sub(scale(normal_velocity, -0x1999), normal_velocity);
    velocity = add(velocity, change);
    auto impulse = scale(change, 0x20000);
    const auto magnitude = length(impulse);
    if (magnitude > 0x34ccc) {
        impulse = scale(scale(impulse, reciprocal(magnitude)), 0x34ccc);
    }
    //4095b0 uses47b250 for both transforms: sum unshifted products and
    //round once to nearest/even, as the source x87 matrix consumer does.
    const auto local_impulse = source_transform::local(impulse, body.basis);
    auto arm = sub(body.contacts[*chosen], body.position);
    arm[1] = add(arm[1], body.position[1] < body.contacts[*chosen][1] ? -0x40000 : -0x28000);
    const auto torque = cross(local_impulse, source_transform::local(arm, body.basis));
    for (std::size_t i = 0; i < 3; ++i) {
        pending_torque[i] = add(pending_torque[i], std::clamp(torque[i], -0x1f40000, 0x1f40000));
    }
    return true;
}

void impact(const Selected& selected, const scene::BodyPose& body, const Vector& velocity, BarrierContactResult& result) {
    if (!selected.segment) {
        return;
    }
    const auto& segment = *selected.segment;
    const auto signed_normal_speed = dot(segment.normal, velocity);
    const auto normal_speed = fixed::abs(signed_normal_speed);
    const auto temporary = mul(normal_speed, segment.kind == 1 ? 0x100000 : 0x50000);
    Vector representative = selected.corner % 4 < 2 ? Vector{0x1e666, -0x3333, 0xcccc} : Vector{-0x21999, 0x1333, 0xcccc};
    if (selected.corner % 2) {
        representative[2] = neg(representative[2]);
    }
    const auto position = add(body.position, source_transform::world(representative, body.basis));
    const auto distance = add(mul(segment.normal[0], sub(segment.first[0], body.position[0])), mul(segment.normal[2], sub(segment.first[2], body.position[2])));
    result.damage_contacts.push_back({{temporary, 0, 0}, source_transform::local(scale(segment.normal, distance), body.basis), position, segment.source_index});
    auto& effects = result.effects;
    effects.position_q16 = position;
    if (normal_speed > 0x8000) {
        effects.fragment_strength_q16 = std::min(mul(sub(normal_speed, 0x8000), 0x40000), one);
    }
    if (segment.kind != 1) {
        return;
    }
    const auto tangent = sub(velocity, scale(segment.normal, signed_normal_speed));
    const auto speed = length(tangent);
    if (speed <= 0x4000) {
        return;
    }
    effects.tangent_velocity_q16 = tangent;
    effects.scrape_strength_q16 = std::min(speed, one);
    const auto excess = sub(speed, 0x4000);
    effects.spark_strength_q16 = std::min(mul(excess, 0x30000), one);
    effects.spark_direction_q16 = scale(tangent, reciprocal(excess));
    if (dot(effects.spark_direction_q16, velocity) > 0) {
        effects.spark_direction_q16 = scale(effects.spark_direction_q16, -one);
    }
}

Vector line_projection(const scene::BarrierSegment& segment, const Vector& point) {
    const auto& c = segment.line;
    if (segment.axis == 1) {
        return {sub(sub(mul(point[2], c[3]), c[5]), mul(c[4], point[0])), point[1], add(sub(c[8], mul(c[7], point[0])), mul(c[6], point[2]))};
    }
    if (segment.axis == 2) {
        return {c[0], point[1], point[2]};
    }
    if (segment.axis == 3) {
        return {point[0], point[1], c[0]};
    }
    throw std::runtime_error("Barrier recovery has an invalid prepared axis");
}

// 406ec0 / 408040: inclusive forbidden-side test using the prepared line,
// deliberately independent of the finite-segment entry brackets.
bool forbidden(const scene::BarrierSegment& segment, const Vector& point) {
    const auto line = segment.axis == 1 ? add(mul(segment.line[0], point[0]), segment.line[1]) : segment.line[0];
    const auto position = point[segment.axis == 2 ? 0 : 2];
    return segment.orientation ? position >= line : position <= line;
}

Vector recovery_push(const scene::BarrierSegment& segment) {
    auto normal = segment.normal;
    const auto component = normal[segment.axis == 2 ? 0 : 2];
    if ((segment.orientation && component > 0) || (!segment.orientation && component < 0)) {
        normal = scale(normal, -one);
    }
    return scale(normal, 0x7ae);
}

template <class Consumer> void chain(const scene::BarrierData& data, std::uint32_t head, Consumer consume) {
    const auto segments = data.segments();
    auto current = head;
    for (std::size_t count = 0; count < segments.size(); ++count) {
        if (current >= segments.size() || segments[current].kind < 2) {
            throw std::runtime_error("Barrier recovery has an invalid chain index");
        }
        const auto& segment = segments[current];
        if (!consume(segment, count == 0)) {
            return;
        }
        current = segment.next_index;
        if (current == head) {
            return;
        }
    }
    throw std::runtime_error("Barrier recovery chain does not close");
}

bool recovery_entry(const scene::BarrierSegment& segment, const scene::BodyPose& body, const Vector& previous, const Vector& velocity, const Vector& half) {
    const auto old_positive = approaching(segment, body, previous, velocity, half);
    if (!old_positive) {
        return false;
    }
    const Vector tangent{neg(segment.normal[2]), 0, segment.normal[0]};
    std::size_t negatives = 0, positives = 0, negative_bracket = 0, positive_bracket = 0;
    for (const auto& point : body.contacts) {
        const auto from_first = sub(segment.first, point), from_second = sub(segment.second, point);
        const auto first = dot(tangent, from_first), second = dot(tangent, from_second);
        const bool negative = dot(from_first, segment.normal) < 0;
        if (negative) {
            ++negatives;
        } else {
            ++positives;
        }
        if (first == 0 || second == 0 || opposite_sign(first, second)) {
            if (negative) {
                ++negative_bracket;
            } else {
                ++positive_bracket;
            }
        }
    }
    return (negative_bracket && positive_bracket) || ((negative_bracket || positive_bracket) && ((negatives == 0 && !*old_positive) || (positives == 0 && *old_positive)));
}

bool outside_recovery(const scene::BarrierData& data, const BarrierRecovery& recovery, const scene::BodyPose& body) {
    bool outside = true;
    chain(data, recovery.head, [&](const auto& segment, bool) {
        if (segment.kind == 2) {
            outside = std::none_of(body.contacts.begin(), body.contacts.end(), [&](const auto& point) {
                return forbidden(segment, point);
            });
            if (outside) {
                return false;
            }
        }
        return true;
    });
    return outside;
}

void nearest_recovery(const scene::BarrierData& data, BarrierRecovery& recovery, const scene::BodyPose& body) {
    std::int32_t best = 0x7d000000;
    std::optional<std::uint32_t> selected;
    chain(data, recovery.head, [&](const auto& segment, bool first) {
        if (segment.kind != 2) {
            return true;
        }
        const auto projected = line_projection(segment, body.position);
        bool bracket = true;
        if (segment.axis != 2) {
            bracket = std::min(segment.first[0], segment.second[0]) <= projected[0] && projected[0] <= std::max(segment.first[0], segment.second[0]);
        }
        if (bracket && segment.axis != 3) {
            bracket = std::min(segment.first[2], segment.second[2]) <= projected[2] && projected[2] <= std::max(segment.first[2], segment.second[2]);
        }
        if (bracket) {
            const auto distance = length(sub(projected, body.position));
            if (distance < best) {
                best = distance;
                // 4071a2..4071bf: the first iteration really copies a null
                // local alias; later iterations copy the current segment.
                selected = first ? std::nullopt : std::optional(segment.source_index);
            }
        }
        return true;
    });
    if (selected) {
        recovery.head = *selected;
        recovery.primary_push = recovery_push(data.segments()[*selected]);
    }
}

void secondary_recovery(const scene::BarrierData& data, BarrierRecovery& recovery, scene::BodyPose& body, Vector& velocity) {
    auto largest = std::int32_t(-0xa0000);
    Vector correction{};
    chain(data, recovery.head, [&](const auto& segment, bool) {
        if (segment.kind != 3) {
            return true;
        }
        for (const auto& point : body.contacts) {
            if (forbidden(segment, point)) {
                const auto candidate = sub(line_projection(segment, point), point);
                const auto magnitude = length(candidate);
                if (magnitude > largest) {
                    largest = magnitude;
                    correction = candidate;
                }
            }
        }
        if (largest <= 0) {
            return true;
        }
        shift(body, correction);
        const auto removed = scale(segment.normal, dot(segment.normal, velocity));
        velocity = sub(velocity, removed);
        if (recovery.has_secondary && recovery.secondary_segment == segment.source_index) {
            velocity = add(velocity, recovery.secondary_push);
            recovery.saved_velocity = add(recovery.saved_velocity, recovery.secondary_push);
            return false;
        }
        Vector tangent{neg(segment.normal[2]), 0, segment.normal[0]};
        if (dot(tangent, recovery.primary_push) < 0) {
            tangent = scale(tangent, -one);
        }
        // 408040 transfers removed normal speed to a tangent aligned with
        // the primary push, then remembers the separate small tangent push.
        velocity = add(velocity, scale(tangent, length(removed)));
        recovery.secondary_push = scale(tangent, 0x7ae);
        recovery.secondary_segment = segment.source_index;
        recovery.has_secondary = true;
        return false;
    });
}

void recovery_basis(scene::BodyPose& body, const BarrierRecovery& recovery, bool reverse_gear) {
    const Vector horizontal{recovery.saved_velocity[0], 0, recovery.saved_velocity[2]};
    const auto magnitude = length(horizontal);
    if (magnitude <= 0) {
        return;
    }
    const auto target = scale(horizontal, reciprocal(reverse_gear ? neg(magnitude) : magnitude));
    const Vector current{body.basis[0][0], 0, body.basis[0][2]};
    const auto current_length = length(current);
    if (current_length <= 0) {
        return;
    }
    const auto current_direction = scale(current, reciprocal(current_length));
    const auto difference = sub(target, current_direction);
    const auto difference_length = length(difference);
    auto forward = target;
    if (difference_length >= 0x199a) {
        forward = add(current_direction, scale(scale(difference, reciprocal(difference_length)), 0x1999));
    }
    forward[1] = body.basis[0][1];
    body.basis[0] = normalized(forward);
    const auto up_dot = dot(body.basis[1], body.basis[0]);
    if (up_dot > 0) {
        body.basis[1] = normalized(sub(body.basis[1], scale(body.basis[0], up_dot)));
    }
    body.basis[2] = normalized(cross(body.basis[0], body.basis[1]));
}

void publish_recovery_pose(scene::BodyPose& body) {
    // 4074a0 publishes the basis but does not rebuild the existing eight
    // corners before the separate HPC completion consumes them.
    body.object_to_world = {};
    body.object_to_world[15] = 1;
    for (std::size_t i = 0; i < 3; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            body.object_to_world[i * 4 + j] = float(body.basis[i][j]) / one;
        }
        body.object_to_world[12 + i] = float(body.position[i]) / one;
    }
}

void update_recovery(const scene::BarrierData& data, BarrierRecovery& recovery, scene::BodyPose& body, Vector& velocity, bool reverse_gear) {
    if (outside_recovery(data, recovery, body)) {
        recovery.active = false;
        return;
    }
    nearest_recovery(data, recovery, body);
    secondary_recovery(data, recovery, body, velocity);
    const auto speed = length(velocity);
    if (speed > one) {
        velocity = scale(scale(velocity, reciprocal(speed)), one);
    }
    velocity = add(velocity, recovery.primary_push);
    recovery.saved_velocity = add(recovery.saved_velocity, recovery.primary_push);
    recovery_basis(body, recovery, reverse_gear);
    publish_recovery_pose(body);
}
} // namespace

BarrierContactResult resolve_barriers(const scene::BarrierData& data, scene::BodyPose& body, std::array<std::int32_t, 3>& velocity, const std::array<std::int32_t, 3>& previous,
    std::uint32_t car_id, BarrierRecovery& recovery, bool reverse_gear) {
    BarrierContactResult result;
    if (recovery.active) {
        update_recovery(data, recovery, body, velocity, reverse_gear);
        result.recovery_exited = !recovery.active;
        return result;
    }
    const auto half = scene::car_half_dimensions(car_id);
    Selected selected;
    const auto segments = data.segments();
    // 409c30 snapshots the current sector once, before any body correction,
    // then consumes its ascending 47de00 pointer list in reverse order.
    const auto candidates = data.candidates(body.position);
    for (std::size_t count = candidates.size(); count > 0; --count) {
        const auto& segment = segments[candidates[count - 1]];
        if (segment.kind >= 2) {
            continue;
        }
        const bool first = endpoint(segment.first, body, previous, half), second = endpoint(segment.second, body, previous, half);
        const bool side = plane(segment, body, velocity, previous, half, result.angular_impulse_q16, selected);
        if (first || second || side) {
            ++result.contacts;
        }
    }
    for (std::size_t count = candidates.size(); count > 0; --count) {
        const auto& segment = segments[candidates[count - 1]];
        if (segment.kind == 2 && recovery_entry(segment, body, previous, velocity, half)) {
            recovery.active = true;
            recovery.has_secondary = false;
            recovery.head = segment.source_index;
            recovery.saved_velocity = velocity;
            recovery.primary_push = recovery_push(segment);
            result.recovery_entered = true;
            break;
        }
    }
    impact(selected, body, velocity, result);
    scene::refresh_body_contacts(body, car_id);
    return result;
}
} // namespace toca::simulation
