#include "scene/horizon_actors.hpp"

#include "formats/cm3d.hpp"
#include "scene/matrix.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <stdexcept>
#include <utility>

namespace toca::scene {
namespace {
using namespace simulation::fixed;

HorizonTransform identity() {
    HorizonTransform result{};
    result[0] = result[5] = result[10] = result[15] = one;
    return result;
}

HorizonTransform source_transform(const ModelNode& node) {
    HorizonTransform result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        // 494a40 converts original fixed fields to float;43cf60 restores them.
        const auto value = std::nearbyint(double(node.transform[i]) * one);
        if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
            throw std::runtime_error("Horizon: actor matrix exceeds the original fixed range");
        }
        result[i] = std::int32_t(value);
    }
    return result;
}

void position(HorizonTransform& matrix, const Vector& value) {
    std::copy(value.begin(), value.end(), matrix.begin() + 12);
}

void basis(HorizonTransform& matrix, const Basis& value) {
    for (std::size_t i = 0; i < 3; ++i) {
        std::copy(value[i].begin(), value[i].end(), matrix.begin() + std::ptrdiff_t(i * 4));
    }
}

// 47b150 keeps the scaled integer products in x87 until its final ROUND.
// Keep it separate from the per-product arithmetic shifts used by motion.
Vector float_world(const Vector& vector, const Basis& axes) {
    Vector result{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto y = double(axes[1][i]) / one * (double(vector[1]) / one);
        const auto x = double(axes[0][i]) / one * (double(vector[0]) / one);
        const auto z = double(axes[2][i]) / one * (double(vector[2]) / one);
        result[i] = std::int32_t(std::nearbyint(((y + x) + z) * one));
    }
    return result;
}

Basis yaw_rotation(std::int32_t angle) {
    // 47fc00 axis(0,1,0), including the original sine table identities.
    const auto s = sine(-angle), c = sine(1024 - angle);
    return {Vector{c, 0, s}, Vector{0, one, 0}, Vector{neg(s), 0, c}};
}

Basis flight_basis(const Vector& direction) {
    const Vector negative = scale(direction, -one);
    return {normalized(Vector{negative[2], 0, neg(negative[0])}), negative, Vector{0, -one, 0}};
}

std::vector<std::uint32_t> children(const Model& model, std::uint32_t root) {
    std::vector<std::uint32_t> result;
    for (std::uint32_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].parent == std::int16_t(root)) {
            result.push_back(i);
        }
    }
    return result;
}

HorizonModelFrame model_frame(const Model& model) {
    HorizonModelFrame result;
    result.visible.resize(model.nodes.size(), 1);
    for (const auto& node : model.nodes) {
        result.transforms.push_back(source_transform(node));
    }
    return result;
}

using FloatMatrix = std::array<float, 16>;
using FloatVector = std::array<float, 3>;

FloatMatrix camera_local() {
    // 49a6f0 ->47f8f0 ->47fe20 applies local Y, local Z, then local X.
    // Preserve the source's float Rodrigues products and lookup-table scales;
    // fixed::rotate normalizes its outputs and is not this node consumer.
    std::array<FloatVector, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    constexpr std::array<std::int32_t, 3> angles{0x71, 0xfce4, 0x400};
    for (const auto axis : {1u, 2u, 0u}) {
        const auto u = axes[axis];
        const auto sn = float(sine(-angles[axis])) / one, cs = float(sine(1024 - angles[axis])) / one;
        std::array<FloatVector, 3> rotation{};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                rotation[row][column] = row == column ? (1.f - u[row] * u[row]) * cs + u[row] * u[row] : (1.f - cs) * u[row] * u[column];
            }
        }
        rotation[0][1] -= sn * u[2];
        rotation[1][0] += sn * u[2];
        rotation[0][2] += sn * u[1];
        rotation[2][0] -= sn * u[1];
        rotation[1][2] -= sn * u[0];
        rotation[2][1] += sn * u[0];
        for (std::size_t row = 0; row < 3; ++row) {
            if (row != axis) {
                const auto previous = axes[row];
                for (std::size_t column = 0; column < 3; ++column) {
                    axes[row][column] = previous[0] * rotation[0][column] + previous[1] * rotation[1][column] + previous[2] * rotation[2][column];
                }
            }
        }
    }
    FloatMatrix result{};
    for (std::size_t row = 0; row < 3; ++row) {
        std::copy(axes[row].begin(), axes[row].end(), result.begin() + std::ptrdiff_t(row * 4));
    }
    result[12] = 10;
    result[13] = 1;
    result[14] = -4;
    result[15] = 1;
    return result;
}

HorizonCamera actor_camera(const HorizonTransform& anchor, bool rotorcraft) {
    static const auto local = camera_local();
    FloatMatrix parent{};
    for (std::size_t index = 0; index < parent.size(); ++index) {
        parent[index] = float(anchor[index]) / one;
    }
    const auto world = matrix::multiply(local, parent);
    const auto inverse = matrix::inverse(world, "Horizon actor camera has no invertible source basis");
    HorizonCamera result;
    result.rotorcraft = rotorcraft;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.position_q16[axis] = std::int32_t(std::nearbyint(double(world[12 + axis]) * one));
        result.anchor_position_q16[axis] = anchor[12 + axis];
        result.published_world_position[axis] = world[12 + axis];
    }
    result.world_to_view = inverse;
    return result;
}

struct MovingRoute {
    std::int32_t distance;
    Vector start, end, velocity;
    bool oriented;
    std::int16_t spin;
    std::int32_t speed;
    bool animated;
};

std::vector<MovingRoute> moving_routes(std::uint32_t track) {
    const auto v = [](std::uint32_t x, std::uint32_t y, std::uint32_t z) -> Vector {
        return {bits(x), bits(y), bits(z)};
    };
    // 43d330's original 200-byte controllers in registration order.
    switch (track) {
        case 0:
        case 5:
            return {{0x7daee14, v(0x3e80000, 0x5c0000, 0xff9c0000), v(0xfc180000, 0xb40000, 0x5a0000), v(0xfffd03d8, 0x21c9, 0x4830), true, 0, 0x30000, false},
                {0x3e68000, v(0xff380000, 0x3a0000, 0xfe7a0000), v(0xfd4e0000, 0x3a0000, 0x1e00000), v(0xffffda4a, 0, 0x42e3), true, 0, 0x4ccc, false}};
        case 1:
            return {{0x56e0000, v(0x2820000, 0x780000, 0xfeda0000), v(0xfd140000, 0x780000, 0xfeda0000), v(0xfffff334, 0, 0), false, 1, 0xccc, false},
                {0x5770000, v(0x2910000, 0x5a0000, 0xd70000), v(0xfd1a0000, 0x5a0000, 0xd70000), v(0xffff0000, 0, 0), true, 0, 0x10000, true}};
        case 2:
            return {{0x2151c28, v(0x1a90000, 0xfffdcccd, 0x810000), v(0x32f0000, 0x280000, 0x1ea0000), v(0xbb64, 0x1439, 0xad4f), true, 0, 0x10000, true},
                {0x2353d70, v(0xfcbe0000, 0x1e0000, 0xfeb60000), v(0xfec70000, 0xffff0000, 0xff8f0000), v(0xec08, 0xfffff1ec, 0x624d), true, 0, 0x10000, true},
                {0x1f1cf5c, v(0xfe450000, 0x230000, 0xfd730000), v(0xfe950000, 0xfffecccd, 0xff5d0000), v(0x2937, 0xffffed50, 0xfbe7), true, 0, 0x10000, true}};
        case 6:
            return {{0x5810000, v(0xfd440000, 0x320000, 0x8a0000), v(0x2c50000, 0x320000, 0x8a0000), v(0x11eb, 0, 0), false, 2, 0x11eb, false},
                {0x5810000, v(0xfd440000, 0x3c0000, 0xfe880000), v(0x2c50000, 0x3c0000, 0xfe880000), v(0xccc, 0, 0), false, -1, 0xccc, false},
                {0x5810000, v(0xfd440000, 0x370000, 0x14c0000), v(0x2c50000, 0x370000, 0x14c0000), v(0xf5c, 0, 0), false, 1, 0xf5c, false}};
        case 8:
            return {{0x35c0000, v(0xfdb50000, 0x370000, 0xfe400000), v(0xfdb50000, 0x370000, 0x19c0000), v(0, 0, 0xccc), false, 1, 0xccc, false}};
        default:
            return {};
    }
}

struct RotorRoute {
    std::array<Vector, 9> points;
    Vector forward, side, climb;
    std::int32_t radius;
    std::array<std::int32_t, 6> lengths;
};

RotorRoute rotor_route(std::uint32_t track) {
    // 43e630: route vertices, two horizontal directions, initial climb,
    // corner radius and the six original phase distances.
    switch (track) {
        case 1:
            return {{{{0x4d0000, 0x399999, -0x1310000}, {-0xf20000, 0x4b0000, -0x1310000}, {-0x1420000, 0x4b0000, -0xe10000}, {-0x1420000, 0x4b0000, 0x1590000},
                        {-0xf20000, 0x4b0000, 0x1a90000}, {0x11f0000, 0x4b0000, 0x1a90000}, {0x16f0000, 0x4b0000, 0x1590000}, {0x16f0000, 0x4b0000, -0xe10000},
                        {0x11f0000, 0x4b0000, -0x1310000}}},
                {-one, 0, 0}, {0, 0, one}, {bits(0xffff0042), 0xdd2, 0}, 0x500000, {0x13f7851, 0x23a0000, 0x2110000, 0x23a0000, 0xd20000, 0x116666}};
        case 3:
            return {{{{-0x900000, 0x94ccc, -0xb20000}, {-0x1120000, 0x1e0000, -0xb20000}, {-0x13a0000, 0x1e0000, -0x8a0000}, {-0x13a0000, 0x1e0000, 0x520000},
                        {-0x1120000, 0x1e0000, 0x7a0000}, {0x9c0000, 0x1e0000, 0x7a0000}, {0xc40000, 0x1e0000, 0x520000}, {0xc40000, 0x1e0000, -0x8a0000},
                        {0x9c0000, 0x1e0000, -0xb20000}}},
                {-one, 0, 0}, {0, 0, one}, {bits(0xffff0313), 0x2831, 0}, 0x280000, {0x83a3d7, 0xdc0000, 0x1ae0000, 0xdc0000, 0x12c0000, 0x14b333}};
        case 4:
            return {{{{-0x3c0000, 0x124ccc, 0x1e0000}, {-0x3c0000, 0x260000, 0xfa0000}, {-0xa0000, 0x260000, 0x12c0000}, {0x21c0000, 0x260000, 0x12c0000},
                        {0x24e0000, 0x260000, 0xfa0000}, {0x24e0000, 0x260000, -0xe60000}, {0x21c0000, 0x260000, -0x1180000}, {-0xa0000, 0x260000, -0x1180000},
                        {-0x3c0000, 0x260000, -0xe60000}}},
                {0, 0, one}, {one, 0, 0}, {0, 0x16c8, 0xfef9}, 0x320000, {0xdce147, 0x2260000, 0x1e00000, 0x2260000, 0x1040000, 0x13b333}};
        case 7:
            return {{{{0x500000, 0x14ccc, 0x640000}, {0x10e0000, 0xa0000, 0x640000}, {0x1720000, 0xa0000, 0}, {0x1720000, 0xa0000, -0xaa0000}, {0x10e0000, 0xa0000, -0x10e0000},
                        {0x280000, 0xa0000, -0x10e0000}, {-0x3c0000, 0xa0000, -0xaa0000}, {-0x3c0000, 0xa0000, 0}, {0x280000, 0xa0000, 0x640000}}},
                {one, 0, 0}, {0, 0, -one}, {0xffbe, 0xbc6, 0}, 0x640000, {0xbe3333, 0xaa0000, 0xe60000, 0xaa0000, 0x280000, 0x8b333}};
        case 8:
            return {
                {{{0x6e0000, 0x130000, 0x500000}, {0x6e0000, 0x1f0000, 0x960000}, {0x8c0000, 0x1f0000, 0xb40000}, {0x2300000, 0x1f0000, 0xb40000}, {0x24e0000, 0x1f0000, 0x960000},
                    {0x24e0000, 0x1f0000, -0x960000}, {0x2300000, 0x1f0000, -0xb40000}, {0x8c0000, 0x1f0000, -0xb40000}, {0x6e0000, 0x1f0000, -0x960000}}},
                {0, 0, one}, {one, 0, 0}, {0, 0x2b43, 0xfc6a}, 0x1e0000, {0x47051e, 0x1a40000, 0x12c0000, 0x1a40000, 0xe60000, 0xc0000}};
        default:
            throw std::runtime_error("Horizon: no source rotorcraft route for track");
    }
}

void visibility(HorizonModelFrame& frame, const std::vector<std::uint32_t>& nodes, std::uint8_t selected) {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        frame.visible.at(nodes[i]) = std::uint8_t(i == selected);
    }
}

HorizonTransform interpolate_transform(const HorizonTransform& previous, const HorizonTransform& current, float alpha) {
    if (alpha == 0) {
        return previous;
    }
    if (previous == current) {
        return current;
    }
    auto result = current;
    for (std::size_t axis = 12; axis < 15; ++axis) {
        result[axis] = std::int32_t(std::llround(std::lerp(double(previous[axis]), double(current[axis]), double(alpha))));
    }
    bool basis_changed = false;
    glm::mat3 first, second;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (std::size_t component = 0; component < 3; ++component) {
            const auto index = axis * 4 + component;
            basis_changed = basis_changed || previous[index] != current[index];
            first[axis][component] = float(previous[index]) / one;
            second[axis][component] = float(current[index]) / one;
        }
    }
    // Static imported nodes can contain scale. Moving translation alone must
    // never normalize or otherwise alter their exact original basis.
    if (!basis_changed) {
        return result;
    }
    const auto first_sign = glm::determinant(first) < 0 ? -1.f : 1.f;
    const auto second_sign = glm::determinant(second) < 0 ? -1.f : 1.f;
    if (first_sign != second_sign) {
        return current;
    }
    glm::vec3 first_scale, second_scale;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        first_scale[axis] = glm::length(first[axis]);
        second_scale[axis] = glm::length(second[axis]);
        if (first_scale[axis] == 0 || second_scale[axis] == 0) {
            return current;
        }
        first[axis] /= first_scale[axis];
        second[axis] /= second_scale[axis];
    }
    // Preserve handedness when converting the original node basis to a proper
    // rotation. Slerp takes the shortest turn without shrinking the model.
    first[0] *= first_sign;
    second[0] *= second_sign;
    auto rotation = glm::mat3_cast(glm::normalize(glm::slerp(glm::normalize(glm::quat_cast(first)), glm::normalize(glm::quat_cast(second)), alpha)));
    rotation[0] *= first_sign;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        rotation[axis] *= std::lerp(first_scale[axis], second_scale[axis], alpha);
        for (std::size_t component = 0; component < 3; ++component) {
            result[axis * 4 + component] = std::int32_t(std::lround(double(rotation[axis][component]) * one));
        }
    }
    return result;
}
} // namespace

HorizonTransform interpolate_horizon_transform(const HorizonTransform& previous, const HorizonTransform& current, float alpha) {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Pose presentation requires a finite fraction");
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    return alpha == 1 ? current : interpolate_transform(previous, current, alpha);
}

HorizonActorResources decode_horizon_actors(std::span<const std::uint8_t> moving, std::span<const std::uint8_t> rotorcraft, const SceneSetup& setup) {
    HorizonActorResources result;
    result.setup = setup;
    const auto track = setup.selection.track_id;
    const bool dry = setup.weather == 1 || setup.weather == 2;
    if (track == 0 || track == 5 || (dry && (track == 1 || track == 2 || track == 6 || track == 8))) {
        result.moving = parse_cm3d(moving);
        for (std::uint32_t i = 0; i < result.moving.nodes.size(); ++i) {
            auto& node = result.moving.nodes[i];
            if (node.parent < 0 && (node.id_flags & 0xffffffu) != 998 && (node.id_flags & 0xffffffu) != 999) {
                result.moving_roots.push_back(i);
                node.id_flags = (node.id_flags & 0xff000000u) | 0x2f4;
            }
        }
        if (result.moving_roots.size() > 3) {
            throw std::runtime_error("Horizon: source moving-controller capacity exceeded");
        }
    }
    if (dry && (track == 1 || track == 3 || track == 4 || track == 7 || track == 8)) {
        result.rotorcraft = parse_cm3d(rotorcraft);
        if (result.rotorcraft.nodes.empty()) {
            throw std::runtime_error("Horizon: rotorcraft is missing its original child actor");
        }
        ModelNode parent{-1, {}, 0, {}};
        parent.transform[0] = parent.transform[5] = parent.transform[10] = parent.transform[15] = 1;
        for (auto& node : result.rotorcraft.nodes) {
            node.parent = std::int16_t(node.parent + 1);
        }
        result.rotorcraft.nodes.insert(result.rotorcraft.nodes.begin(), std::move(parent));
        result.rotorcraft_root = 0;
        result.rotorcraft_child = 1;
        result.rotorcraft.nodes[1].id_flags = (result.rotorcraft.nodes[1].id_flags & 0xff000000u) | 0x2f4;
    }
    return result;
}

struct HorizonActors::State {
    struct Moving {
        MovingRoute route;
        std::uint32_t node;
        Vector current;
        std::vector<std::uint32_t> animated_nodes;
        std::int32_t distance = 0, wait = 0;
        std::uint16_t angle = 0;
        std::uint8_t animation = 0;
    };

    HorizonActorResources resources;
    HorizonFrame frame, previous_frame;
    std::vector<std::uint8_t> moving_cuts;
    std::vector<Moving> actors;
    RotorRoute rotor{};
    std::vector<std::uint32_t> rotor_nodes;
    Vector rotor_position{}, rotor_velocity{}, turn_direction{}, turn_center{};
    Basis rotor_basis{}, child_basis{}, saved_child_basis{}, turn_rotation{};
    std::int32_t speed = 0, distance = 0, angle = 0, deceleration = 0, landing_acceleration = 0;
    std::int16_t turn_increment = 0;
    std::uint8_t phase = 0, vertex = 0, animation = 0;
    std::int32_t wait = 100;

    explicit State(const HorizonActorResources& source) : resources(source) {
        reset();
    }

    void reset() {
        frame = {};
        frame.moving = model_frame(resources.moving);
        frame.rotorcraft = model_frame(resources.rotorcraft);
        moving_cuts.assign(frame.moving.transforms.size(), 0);
        actors.clear();
        const auto routes = moving_routes(resources.setup.selection.track_id);
        if (resources.moving_roots.size() > routes.size()) {
            throw std::runtime_error("Horizon: more loaded moving actors than original route definitions");
        }
        for (std::size_t i = 0; i < resources.moving_roots.size(); ++i) {
            const auto root = resources.moving_roots[i];
            Moving actor{routes[i], root, routes[i].start, {}};
            if (actor.route.animated) {
                const auto descendants = children(resources.moving, root);
                if (descendants.size() < 4) {
                    throw std::runtime_error("Horizon: animated moving actor requires four sibling frames");
                }
                actor.animated_nodes.assign(descendants.begin(), descendants.begin() + 4);
                visibility(frame.moving, actor.animated_nodes, 0);
            }
            auto& matrix = frame.moving.transforms.at(root);
            const auto direction = normalized(Vector{actor.route.velocity[0], 0, actor.route.velocity[2]});
            basis(matrix, actor.route.oriented ? flight_basis(direction) : Basis{Vector{0, 0, one}, Vector{-one, 0, 0}, Vector{0, -one, 0}});
            position(matrix, actor.current);
            actors.push_back(std::move(actor));
        }
        frame.moving_count = std::uint32_t(actors.size());
        if (resources.rotorcraft_root == UINT32_MAX) {
            publish_camera();
            previous_frame = frame;
            return;
        }
        rotor = rotor_route(resources.setup.selection.track_id);
        rotor_nodes = children(resources.rotorcraft, resources.rotorcraft_child);
        if (rotor_nodes.size() < 4) {
            throw std::runtime_error("Horizon: rotorcraft requires four original rotor siblings");
        }
        rotor_nodes.resize(4);
        visibility(frame.rotorcraft, rotor_nodes, 0);
        rotor_position = rotor.points[0];
        rotor_velocity = {};
        turn_direction = turn_center = {};
        rotor_basis = flight_basis(rotor.forward);
        child_basis = saved_child_basis = {Vector{one, 0, 0}, Vector{0, one, 0}, Vector{0, 0, one}};
        speed = distance = angle = 0;
        phase = vertex = animation = 0;
        wait = 100;
        deceleration = -std::int32_t(INT64_C(0x28f50000) / mul(rotor.lengths[4], 0x20000));
        landing_acceleration = std::int32_t(INT64_C(0x28f0000) / rotor.lengths[5]);
        const auto corner_speed = std::int32_t(INT64_C(0x66660000) / mul(mul(rotor.radius, 0x323d7), 0x20000));
        turn_increment = std::int16_t(mul(corner_speed, 0x1680000) / 0x1680);
        turn_rotation = yaw_rotation(turn_increment);
        frame.rotorcraft.transforms.at(resources.rotorcraft_child) = identity();
        publish_rotor();
        publish_camera();
        previous_frame = frame;
    }

    void publish_camera() {
        frame.camera_zero.reset();
        if (resources.rotorcraft_root != UINT32_MAX) {
            // 43e2a0 attaches camera to the extra root, not the banked child.
            frame.camera_zero = actor_camera(frame.rotorcraft.transforms.at(resources.rotorcraft_root), true);
        } else if (!resources.moving_roots.empty()) {
            const auto track = resources.setup.selection.track_id;
            // 43cf60 excludes the helicopter tracks even if their moving HOR
            // group has a balloon/airship. Source camera0 follows the first root.
            if (track != 1 && track != 3 && track != 4 && track != 7 && track != 8) {
                frame.camera_zero = actor_camera(frame.moving.transforms.at(resources.moving_roots.front()), false);
            }
        }
    }

    void publish_rotor() {
        auto& parent = frame.rotorcraft.transforms.at(resources.rotorcraft_root);
        auto& child = frame.rotorcraft.transforms.at(resources.rotorcraft_child);
        basis(parent, rotor_basis);
        position(parent, rotor_position);
        basis(child, child_basis);
        frame.rotorcraft_state = phase;
        frame.rotorcraft_speed = speed;
        frame.rotorcraft_position = rotor_position;
    }

    void update_moving() {
        for (auto actor = actors.begin(); actor != actors.end();) {
            if (actor->route.animated) {
                actor->animation = std::uint8_t((actor->animation + 1) & 3);
                visibility(frame.moving, actor->animated_nodes, actor->animation);
            }
            auto& matrix = frame.moving.transforms.at(actor->node);
            if (actor->wait == 1) {
                actor->wait = 0;
                actor->current = actor->route.start;
                position(matrix, actor->current);
                moving_cuts[actor->node] = 1;
            } else if (actor->wait < 2) {
                actor->distance = add(actor->distance, actor->route.speed);
                if (actor->distance < actor->route.distance) {
                    actor->current = add(actor->current, actor->route.velocity);
                    position(matrix, actor->current);
                    if (!actor->route.oriented) {
                        actor->angle = std::uint16_t(actor->angle + actor->route.spin);
                        const auto s = sine(actor->angle), c = sine(actor->angle + 1024);
                        basis(matrix, {Vector{s, 0, c}, Vector{neg(c), 0, s}, Vector{0, -one, 0}});
                    }
                } else {
                    actor->distance = 0;
                    actor->wait = 10;
                    actor->current = actor->route.end;
                    if (!actor->route.oriented) {
                        // 43dfc0 removes only the controller.74436 never copies
                        // its final CPU transform, leaving the last drawn pose.
                        actor = actors.erase(actor);
                        continue;
                    }
                    position(matrix, actor->current);
                }
            } else {
                --actor->wait;
            }
            ++actor;
        }
        frame.moving_count = std::uint32_t(actors.size());
    }

    void tilt() {
        // 43fd70: initial acceleration/approach pitches child around X.
        const auto amount = std::min(mul(speed, 0x40000), one);
        const auto a = std::int32_t((std::uint32_t(amount) * 0x38) >> 16);
        const auto s = sine(a), c = sine(a + 1024);
        child_basis[1] = {0, c, neg(s)};
        child_basis[2] = {0, s, c};
    }

    void bank() {
        // 43fe00: triangle45-degree bank through each90-degree turn.
        auto degrees = angle * 0x1680;
        if (degrees > 0x2d0000) {
            degrees = 0x5a0000 - degrees;
        }
        const auto amount = std::min(mul(degrees, 0x5a1), one);
        const auto a = mul(amount, 0x200);
        if (abs(a) < 2) {
            child_basis = saved_child_basis;
            return;
        }
        const auto rotation = yaw_rotation(a);
        for (std::size_t i = 0; i < 3; ++i) {
            child_basis[i] = normalized(float_world(saved_child_basis[i], rotation));
        }
    }

    void begin_turn(std::uint8_t point, const Vector& radial, std::int32_t center_radius) {
        vertex = point;
        rotor_position = rotor.points[vertex];
        angle = 0;
        turn_direction = radial;
        turn_center = add(rotor_position, scale(radial, center_radius));
        phase = 1;
        saved_child_basis = child_basis;
    }

    void straight_basis(const Vector& negative_direction) {
        rotor_basis[0] = {negative_direction[2], 0, neg(negative_direction[0])};
        rotor_basis[1] = negative_direction;
    }

    void update_rotor() {
        animation = std::uint8_t((animation + 1) & 3);
        visibility(frame.rotorcraft, rotor_nodes, animation);
        switch (phase) {
            case 0:
                if (wait > 0) {
                    --wait;
                    break;
                }
                if (speed != 0x6666) {
                    speed = std::min(add(speed, 0x147), 0x6666);
                    rotor_velocity = scale(rotor.climb, speed);
                }
                distance = add(distance, speed);
                if (distance > rotor.lengths[0]) {
                    begin_turn(1, scale(rotor.side, -one), -rotor.radius);
                } else {
                    rotor_position = add(rotor_position, rotor_velocity);
                }
                tilt();
                if (phase == 1) {
                    saved_child_basis = child_basis;
                }
                // The source breaks after entering phase1; its first turn
                // increment is on the next update, not this takeoff tick.
                break;
            case 1:
                angle = add(angle, turn_increment);
                if (angle < 1024) {
                    turn_direction = normalized(float_world(turn_direction, turn_rotation));
                    rotor_position = add(turn_center, scale(turn_direction, rotor.radius));
                    rotor_basis[0] = turn_direction;
                    rotor_basis[1] = {neg(turn_direction[2]), 0, turn_direction[0]};
                    bank();
                    break;
                }
                {
                    const auto previous = vertex;
                    ++vertex;
                    rotor_position = rotor.points[vertex];
                    distance = 0;
                    if (vertex == 8) {
                        rotor_velocity = scale(rotor.forward, speed);
                        straight_basis(scale(rotor.forward, -one));
                        phase = 4;
                    } else {
                        const auto direction = previous == 1 ? rotor.side : previous == 3 ? scale(rotor.forward, -one) : scale(rotor.side, -one);
                        rotor_velocity = scale(direction, speed);
                        straight_basis(scale(direction, -one));
                        phase = 2;
                    }
                    bank();
                }
                break;
            case 2:
                distance = add(distance, speed);
                if (distance <= rotor.lengths[vertex >> 1]) {
                    rotor_position = add(rotor_position, rotor_velocity);
                } else if (vertex == 2) {
                    begin_turn(3, rotor.forward, -rotor.radius);
                } else if (vertex == 4) {
                    begin_turn(5, rotor.side, -rotor.radius);
                } else if (vertex == 6) {
                    begin_turn(7, scale(rotor.forward, -one), -rotor.radius);
                }
                break;
            case 3:
                if (wait > 0) {
                    --wait;
                    break;
                }
                {
                    bool stopped = false;
                    if (rotor.lengths[5] < mul(distance, 0x20000)) {
                        speed = sub(speed, landing_acceleration);
                        stopped = speed < 1;
                    } else {
                        speed = add(speed, landing_acceleration);
                    }
                    distance = add(distance, speed);
                    rotor_velocity = {0, neg(speed), 0};
                    if (distance < rotor.lengths[5] && !stopped) {
                        rotor_position = add(rotor_position, rotor_velocity);
                    } else {
                        speed = distance = 0;
                        rotor_position = rotor.points[0];
                        phase = 0;
                        wait = 100;
                    }
                }
                break;
            case 4: {
                const auto next_speed = add(speed, deceleration);
                rotor_velocity = scale(rotor.forward, next_speed);
                distance = add(distance, next_speed);
                if (next_speed > 0 && distance < rotor.lengths[4]) {
                    speed = next_speed;
                    rotor_position = add(rotor_position, rotor_velocity);
                } else {
                    rotor_position[0] = rotor.points[0][0];
                    rotor_position[2] = rotor.points[0][2];
                    speed = distance = 0;
                    vertex = 0;
                    child_basis = {Vector{one, 0, 0}, Vector{0, one, 0}, Vector{0, 0, one}};
                    phase = 3;
                    wait = 100;
                }
                tilt();
            } break;
            default:
                throw std::runtime_error("Horizon: invalid source rotorcraft state");
        }
        publish_rotor();
    }
};

HorizonActors::HorizonActors(const HorizonActorResources& resources) : state_(std::make_unique<State>(resources)) {}

HorizonActors::~HorizonActors() = default;
HorizonActors::HorizonActors(HorizonActors&&) noexcept = default;
HorizonActors& HorizonActors::operator=(HorizonActors&&) noexcept = default;

void HorizonActors::reset() {
    state_->reset();
}

void HorizonActors::update() {
    state_->previous_frame = state_->frame;
    std::ranges::fill(state_->moving_cuts, 0);
    ++state_->frame.updates;
    state_->update_moving();
    if (state_->resources.rotorcraft_root != UINT32_MAX) {
        state_->update_rotor();
    }
    state_->publish_camera();
}

const HorizonFrame& HorizonActors::frame() const {
    return state_->frame;
}

HorizonFrame HorizonActors::presentation_frame(float alpha) const {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Horizon presentation requires a finite fraction");
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    auto result = state_->frame;
    if (alpha == 1) {
        return result;
    }
    for (std::size_t node = 0; node < result.moving.transforms.size(); ++node) {
        if (!state_->moving_cuts[node]) {
            result.moving.transforms[node] = interpolate_transform(state_->previous_frame.moving.transforms[node], result.moving.transforms[node], alpha);
        }
    }
    for (std::size_t node = 0; node < result.rotorcraft.transforms.size(); ++node) {
        result.rotorcraft.transforms[node] = interpolate_transform(state_->previous_frame.rotorcraft.transforms[node], result.rotorcraft.transforms[node], alpha);
    }
    if (result.camera_zero) {
        const auto rotorcraft = result.camera_zero->rotorcraft;
        const auto& anchor = rotorcraft ? result.rotorcraft.transforms.at(state_->resources.rotorcraft_root) : result.moving.transforms.at(state_->resources.moving_roots.front());
        result.camera_zero = actor_camera(anchor, rotorcraft);
    }
    return result;
}
} // namespace toca::scene
