#include "simulation/car_contacts.hpp"

#include "simulation/fixed.hpp"
#include "simulation/source_snapshot.hpp"
#include "simulation/source_transform.hpp"

#include <algorithm>

namespace toca::simulation {
namespace {
using namespace fixed;

struct ContactPlane {
    Vector normal{}, first_arm{}, second_arm{};
    bool longitudinal = false;
};

Vector clamp_arm(Vector arm) {
    // 428790, initialized by415a10. The lower Y side remains unbounded.
    arm[0] = std::clamp(arm[0], -0x21999, 0x1e666);
    arm[1] = std::min(arm[1], 0xa666);
    arm[2] = std::clamp(arm[2], -0xcccc, 0xcccc);
    return arm;
}

class ContactPass {
    struct Neighbors {
        // Each successful pair is registered symmetrically once, so an actor
        // has at most the other 15 actors in original contact order.
        std::array<std::uint8_t, 15> ids;
        std::uint8_t count = 0;

        std::span<const std::uint8_t> active() const {
            return std::span(ids).first(count);
        }

        void append(std::uint32_t actor) {
            ids[count++] = std::uint8_t(actor);
        }
    };

    std::span<const CarContactActor> actors_;
    std::array<Neighbors, 16> neighbors_;
    std::array<bool, 256>& touching_;
    CarContactEffects& effects_;
    const CarContactOptions& options_;

    static std::size_t pair(std::uint32_t first, std::uint32_t second) {
        return first * 16 + second;
    }

    Motion& actor(std::uint32_t id) const {
        return *actors_[id].motion;
    }

    void shift(std::uint32_t id, const Vector& delta) {
        actor(id).shift_car_contact(delta);
        // 42b570 walks this actor's prior contacts once; no recursive traversal
        // or deduplication between the two actors' lists.
        for (const auto neighbor : neighbors_[id].active()) {
            actor(neighbor).shift_car_contact(delta);
        }
    }

    void separate(std::uint32_t moving, std::uint32_t target, Vector correction) {
        const auto& basis = actor(target).state().body.basis;
        const bool moving_fixed = actor(moving).state().barrier_contact_count != 0;
        const bool target_fixed = actor(target).state().barrier_contact_count != 0;
        if (moving_fixed != target_fixed) {
            if (target_fixed) {
                shift(moving, source_transform::world(correction, basis));
            } else {
                shift(target, source_transform::world(scale(correction, -one), basis));
            }
        } else {
            // Multiply each penetration component, then halve it. Halving the
            // scalar first changes the original signed fixed-point rounding.
            correction = scale(correction, 0x8000);
            const auto first = source_transform::world(correction, basis);
            const auto second = source_transform::world(scale(correction, -one), basis);
            shift(moving, first);
            shift(target, second);
        }
    }

    void center_arms(std::uint32_t first, std::uint32_t second, ContactPlane& plane) const {
        const auto& a = actor(first).state().body;
        const auto& b = actor(second).state().body;
        const auto delta = sub(a.position, b.position);
        auto first_arm = source_transform::local(delta, a.basis);
        auto second_arm = source_transform::local(delta, b.basis);
        // 42ada0 transforms before signed division (toward zero).
        for (std::size_t axis = 0; axis < 3; ++axis) {
            first_arm[axis] = -(first_arm[axis] / 2);
            second_arm[axis] /= 2;
        }
        plane.first_arm = clamp_arm(first_arm);
        plane.second_arm = clamp_arm(second_arm);
    }

    bool corners(std::uint32_t moving_id, std::uint32_t target_id, ContactPlane& plane) {
        const auto& moving = actor(moving_id).state();
        const auto& target = actor(target_id).state();
        if (length(source_transform::local(sub(moving.body.position, target.body.position), target.body.basis)) < 1) {
            return false;
        }
        const auto direction = normalized(source_transform::local(sub(moving.previous_position_q16, target.previous_position_q16), target.body.basis));
        const auto bounds = add(scene::car_half_dimensions(actors_[target_id].car_id), Vector{0x28f, 0x28f, 0x28f});
        auto maximum = -10 * one;
        bool contained = false;
        for (const auto& corner : moving.body.contacts) {
            // Preserve47b250's two separately rounded transforms.
            const auto point = add(source_transform::local(sub(corner, moving.body.position), target.body.basis),
                source_transform::local(sub(moving.body.position, target.body.position), target.body.basis));
            if (fixed::abs(point[0]) > bounds[0] || fixed::abs(point[1]) > bounds[1] || fixed::abs(point[2]) > bounds[2]) {
                continue;
            }
            contained = true;
            auto distance = 0x7d000000;
            std::optional<std::size_t> selected;
            // 42ba80 tests X,Z,Y in that order, preserving ties.
            for (const auto axis : {0u, 2u, 1u}) {
                if (fixed::abs(direction[axis]) <= 0x28f) {
                    continue;
                }
                const auto edge = direction[axis] > 0 ? sub(bounds[axis], point[axis]) : fixed::abs(add(bounds[axis], point[axis]));
                const auto amount = std::int32_t((std::int64_t(edge) * one) / fixed::abs(direction[axis]));
                if (amount < distance) {
                    distance = amount;
                    selected = axis;
                }
            }
            if (!selected) {
                distance = 0; // 42ba80 returns zero without selecting a normal.
            }
            if (distance > maximum) {
                maximum = distance;
                if (selected) {
                    plane.normal = target.body.basis[*selected];
                    plane.longitudinal = plane.longitudinal || *selected == 0;
                }
            }
        }
        if (maximum > 0) {
            separate(moving_id, target_id, scale(direction, maximum));
        }
        if (contained) {
            center_arms(moving_id, target_id, plane);
        }
        return contained;
    }

    bool endpoint(std::uint32_t moving_id, std::uint32_t target_id, ContactPlane& plane) const {
        auto& motion = actor(moving_id);
        const auto& moving = motion.state();
        const auto& target = actor(target_id).state();
        const auto own_half = scene::car_half_dimensions(actors_[moving_id].car_id);
        const auto target_half = scene::car_half_dimensions(actors_[target_id].car_id);
        for (const auto sign : {1, -1}) {
            // 40a770 initializes the fallback probes at +/-one third half-X.
            const auto offset = source_transform::world(Vector{sign * (own_half[0] / 3), 0, 0}, moving.body.basis);
            const auto point = add(source_transform::local(offset, target.body.basis), source_transform::local(sub(moving.body.position, target.body.position), target.body.basis));
            if (fixed::abs(point[0]) > target_half[0] || fixed::abs(point[1]) > target_half[1] || fixed::abs(point[2]) > target_half[2]) {
                continue;
            }
            // 42a820 restores this actor only, without the42b570 neighbor walk.
            motion.shift_car_contact(sub(moving.previous_position_q16, moving.body.position));
            const Vector arm{sign * own_half[0] * 2, 0, 0};
            const auto contact = add(moving.body.position, source_transform::world(arm, moving.body.basis));
            plane.first_arm = clamp_arm(arm);
            plane.second_arm = clamp_arm(source_transform::local(sub(contact, target.body.position), target.body.basis));
            const auto center = source_transform::local(sub(moving.body.position, target.body.position), target.body.basis);
            const auto axis = fixed::abs(center[0]) > fixed::abs(center[1]) && fixed::abs(center[0]) > fixed::abs(center[2])   ? 0u
                              : fixed::abs(center[1]) > fixed::abs(center[0]) && fixed::abs(center[1]) > fixed::abs(center[2]) ? 1u
                                                                                                                               : 2u;
            plane.normal = target.body.basis[axis];
            plane.longitudinal = plane.longitudinal || axis == 0;
            return true;
        }
        return false;
    }

    std::optional<ContactPlane> overlap(std::uint32_t first, std::uint32_t second) {
        const auto a = scene::car_half_dimensions(actors_[first].car_id), b = scene::car_half_dimensions(actors_[second].car_id);
        const auto delta = sub(actor(first).state().body.position, actor(second).state().body.position);
        const auto radius = add(length(a), length(b));
        for (const auto component : delta) {
            if (fixed::abs(component) > a[0] * 4) {
                return std::nullopt;
            }
        }
        if (dot(delta, delta) >= mul(radius, radius)) {
            return std::nullopt;
        }
        ContactPlane plane;
        const bool first_hit = corners(first, second, plane);
        const bool second_hit = corners(second, first, plane);
        if (second_hit) {
            std::swap(plane.first_arm, plane.second_arm);
        }
        if (first_hit || second_hit || endpoint(first, second, plane)) {
            return plane;
        }
        if (endpoint(second, first, plane)) {
            std::swap(plane.first_arm, plane.second_arm);
            return plane;
        }
        return std::nullopt;
    }

    static Vector moment(const MotionState& state, const Vector& impulse, const Vector& arm, bool strong) {
        const auto local_impulse = source_transform::local(impulse, state.body.basis);
        auto result = cross(local_impulse, arm);
        if (strong) {
            result = add(scale(cross(local_impulse, Vector{0, -one, 0}), 0x40000), scale(result, 0x13333));
        }
        return result;
    }

    void effects(std::uint32_t first, std::uint32_t second, const ContactPlane& plane, std::int32_t relative_speed) {
        auto& a = actor(first);
        auto& b = actor(second);
        const auto position = add(a.state().body.position, source_transform::world(plane.first_arm, a.state().body.basis));
        const bool human = actors_[first].local_player || actors_[second].local_player;
        const bool visible = actors_[first].visible && actors_[second].visible;
        const auto velocities = add(a.state().velocity_q16, b.state().velocity_q16);
        auto tangent = normalized(Vector{neg(plane.normal[2]), 0, plane.normal[0]});
        const auto relative = sub(a.state().velocity_q16, b.state().velocity_q16);
        if (dot(relative, tangent) > 0) {
            tangent = scale(tangent, -one);
        }
        auto& touching = touching_[pair(first, second)];
        if (touching) {
            if (plane.longitudinal) {
                return;
            }
            if (human) {
                const bool handle = options_.scrape_handle && options_.scrape_handle(first, second);
                effects_.audio.push_back({handle ? CarContactAudioAction::scrape_update : CarContactAudioAction::scrape_start, first, second, position, handle ? 11u : 10u,
                    std::min(one, mul(mul(relative_speed, 0x106), 0x1f40000)), true});
            }
            const auto strength = fixed::abs(dot(relative, tangent));
            if (visible && options_.queued_sparks + effects_.sparks.size() < 10 && strength > 0x1999) {
                effects_.sparks.push_back({position, tangent, scale(velocities, 0x8000), std::min(one, strength), false});
            }
            return;
        }
        touching = true;
        //428b50 publishes both directed second words.403dd0 later reads
        //each actor's own row for its ahead/behind traffic decision.
        touching_[pair(second, first)] = true;
        effects_.audio.push_back({CarContactAudioAction::reset_pair, first, second});
        DamageResult first_hit, second_hit;
        if (actors_[first].full_body) {
            first_hit = a.apply_car_damage(plane.first_arm, a.damage_feedback_enabled());
        }
        if (actors_[second].full_body) {
            second_hit = b.apply_car_damage(plane.second_arm, b.damage_feedback_enabled());
        }
        if (first_hit.event_kind == 0 && second_hit.event_kind == 0) {
            return;
        }
        if (first_hit.event_kind == 0 || first_hit.event_kind == 1) {
            if (second_hit.event_kind != 0 && second_hit.event_kind != 1) {
                first_hit.event_kind = 1;
            }
        } else {
            second_hit.event_kind = 1;
        }
        const auto sound = [&](const DamageResult& hit) {
            if (hit.event_kind != 0) {
                effects_.audio.push_back({CarContactAudioAction::impact, first, second, position, hit.event_kind, hit.strength_q16, human});
            }
        };
        if (first_hit.event_kind == second_hit.event_kind) {
            first_hit.strength_q16 = std::max(first_hit.strength_q16, second_hit.strength_q16);
            sound(first_hit);
        } else {
            sound(first_hit);
            sound(second_hit);
        }
        if (visible && options_.queued_fragments + effects_.fragments.size() < 10) {
            const auto palette = [&](std::uint32_t id) {
                return std::uint8_t(actors_[id].car_id == 2 && !actors_[id].local_player ? 11u : actors_[id].car_id);
            };
            effects_.fragments.push_back({position, scale(velocities, 0x4000), first_hit.strength_q16, {palette(first), palette(second)}});
        }
    }

public:
    ContactPass(std::span<const CarContactActor> actors, std::array<bool, 256>& touching, CarContactEffects& effects, const CarContactOptions& options)
        : actors_(actors), touching_(touching), effects_(effects), options_(options) {}

    std::vector<CarContact> resolve(const ContactCandidateGroups& groups) {
        std::vector<CarContact> contacts;
        for (const auto& group : groups.ordered()) {
            for (std::uint32_t i = 0; i < group.count; ++i) {
                for (auto j = i + 1; j < group.count; ++j) {
                    const auto first = std::uint32_t(group.actors[i]), second = std::uint32_t(group.actors[j]);
                    if (first >= actors_.size() || second >= actors_.size() || first == second) {
                        throw std::runtime_error("Car contact candidate group contains an invalid actor");
                    }
                    if (!actors_[first].active || !actors_[second].active) {
                        continue;
                    }
                    const auto neighbors = neighbors_[first].active();
                    if (std::ranges::find(neighbors, second) != neighbors.end()) {
                        continue;
                    }
                    auto& a = actor(first);
                    auto& b = actor(second);
                    if (a.state().velocity_q16 == Vector{} && b.state().velocity_q16 == Vector{}) {
                        continue; // 428820 excludes two completely stationary bodies.
                    }
                    const auto collision = overlap(first, second);
                    if (!collision) {
                        auto& touching = touching_[pair(first, second)];
                        if (touching) {
                            touching = false;
                            touching_[pair(second, first)] = false;
                            if (options_.scrape_handle && options_.scrape_handle(first, second)) {
                                effects_.audio.push_back({CarContactAudioAction::scrape_stop, first, second, {}, 12});
                            }
                        }
                        continue;
                    }
                    // 428b50 restitution1 exchanges original normal velocities.
                    const auto impact = sub(dot(a.state().velocity_q16, collision->normal), dot(b.state().velocity_q16, collision->normal));
                    auto first_impulse = scale(collision->normal, neg(impact));
                    auto second_impulse = scale(collision->normal, impact);
                    if (a.state().contact_count > 0 && first_impulse[1] < 0) {
                        second_impulse[1] = sub(second_impulse[1], first_impulse[1]);
                        first_impulse[1] = 0;
                    }
                    if (b.state().contact_count > 0 && second_impulse[1] < 0) {
                        first_impulse[1] = sub(first_impulse[1], second_impulse[1]);
                        second_impulse[1] = 0;
                    }
                    const auto relative_speed = length(sub(a.state().velocity_q16, b.state().velocity_q16));
                    const bool strong = relative_speed > one;
                    a.add_car_contact(first_impulse, moment(a.state(), first_impulse, collision->first_arm, strong));
                    // The secondary strong branch depends on the FIRST actor's
                    // accumulated, gated and clamped moment, not relative speed alone.
                    const bool secondary_strong = strong && length(a.pending_car_moment()) > 0x5998;
                    b.add_car_contact(second_impulse, moment(b.state(), second_impulse, collision->second_arm, secondary_strong));
                    contacts.push_back(
                        {first, second, scale(add(a.state().body.position, b.state().body.position), 0x8000), std::min(one, mul(mul(relative_speed, 0x106), 0x1f40000))});
                    effects(first, second, *collision, relative_speed);
                    neighbors_[first].append(second);
                    neighbors_[second].append(first);
                }
            }
        }
        return contacts;
    }
};
} // namespace

std::vector<CarContact> CarContacts::resolve(std::span<const CarContactActor> actors, const ContactCandidateGroups& groups, const CarContactOptions& options) {
    if (actors.size() > 16 || std::any_of(actors.begin(), actors.end(), [](const auto& actor) {
            return actor.motion == nullptr || actor.car_id >= 10;
        })) {
        throw std::runtime_error("Car contact pass requires up to16 original car bodies");
    }
    effects_ = {};
    if (!options.enabled) {
        return {};
    }
    return ContactPass(actors, touching_, effects_, options).resolve(groups);
}

const CarContactEffects& CarContacts::effects() const {
    return effects_;
}

void CarContacts::reset() {
    touching_ = {};
    effects_ = {};
}

void CarContacts::restore_source_touching(std::span<const std::uint8_t, 0x800> raw) {
    for (std::size_t i = 0; i < touching_.size(); ++i) {
        touching_[i] = source_i32(raw, i * 8 + 4) != 0;
    }
    effects_ = {};
}

const std::array<bool, 256>& CarContacts::source_touching() const {
    return touching_;
}
} // namespace toca::simulation
