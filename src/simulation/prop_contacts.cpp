#include "simulation/prop_contacts.hpp"

#include "simulation/fixed.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace toca::simulation {
namespace {
using namespace fixed;
using Transform = scene::HorizonTransform;
constexpr Basis identity_basis{Vector{one, 0, 0}, Vector{0, one, 0}, Vector{0, 0, one}};

std::int32_t rounded(double value) {
    value = std::nearbyint(value);
    if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("Props: original matrix exceeds fixed range");
    }
    return std::int32_t(value);
}

Transform transform(const ModelNode& node) {
    Transform result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = rounded(double(node.transform[i]) * one);
    }
    return result;
}

Basis basis(const Transform& matrix) {
    return {Vector{matrix[0], matrix[1], matrix[2]}, Vector{matrix[4], matrix[5], matrix[6]}, Vector{matrix[8], matrix[9], matrix[10]}};
}

Vector position(const Transform& matrix) {
    return {matrix[12], matrix[13], matrix[14]};
}

void publish(Transform& matrix, const Basis& axes, const Vector& point) {
    for (std::size_t i = 0; i < 3; ++i) {
        std::copy(axes[i].begin(), axes[i].end(), matrix.begin() + std::ptrdiff_t(i * 4));
        matrix[12 + i] = point[i];
    }
}

scene::HorizonModelFrame model_frame(const Model& model) {
    scene::HorizonModelFrame result;
    result.visible.resize(model.nodes.size(), 1);
    for (const auto& node : model.nodes) {
        result.transforms.push_back(transform(node));
    }
    return result;
}

// 47b150/47b250 retain x87 products until final rounding. Conversion to
// binary32 between arithmetic operations changes several original contacts.
Vector float_world(const Vector& value, const Basis& axes) {
    Vector result{};
    for (std::size_t i = 0; i < 3; ++i) {
        result[i] = rounded((double(axes[1][i]) * value[1] + double(axes[0][i]) * value[0] + double(axes[2][i]) * value[2]) / one);
    }
    return result;
}

Vector float_local(const Vector& value, const Basis& axes) {
    Vector result{};
    for (std::size_t i = 0; i < 3; ++i) {
        result[i] = rounded((double(axes[i][1]) * value[1] + double(axes[i][0]) * value[0] + double(axes[i][2]) * value[2]) / one);
    }
    return result;
}

Basis rotation(const Vector& axis, std::int32_t angle) {
    // 47fc00, arbitrary source axis, with each Q16 product shifted separately.
    const auto s = sine(-angle), c = sine(1024 - angle), t = sub(one, c);
    Basis result{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto square = mul(axis[i], axis[i]);
        result[i][i] = add(mul(sub(one, square), c), square);
    }
    const auto xy = mul(mul(axis[0], axis[1]), t), xz = mul(mul(axis[0], axis[2]), t), yz = mul(mul(axis[1], axis[2]), t);
    result[0][1] = sub(xy, mul(axis[2], s));
    result[0][2] = add(xz, mul(axis[1], s));
    result[1][0] = add(xy, mul(axis[2], s));
    result[1][2] = sub(yz, mul(axis[0], s));
    result[2][0] = sub(xz, mul(axis[1], s));
    result[2][1] = add(yz, mul(axis[0], s));
    return result;
}

std::int32_t asin_lookup(std::int32_t value) {
    const auto index = std::uint32_t(std::min(fixed::abs(value), 0xffff)) >> 4;
    const auto entry = mul(std::int32_t(std::asin(double(index) / 4095.0) * one), 0x28b);
    return value < 0 ? -entry : entry;
}

std::int32_t atan_lookup(std::uint32_t index) {
    return mul(std::int32_t(std::atan(double(index) / 511.0) * one), 0x28b);
}

std::int32_t divide(std::int32_t numerator, std::int32_t denominator) {
    if (denominator == 0) {
        throw std::runtime_error("Props: original fixed division has a zero divisor");
    }
    return bits(std::uint32_t(std::int64_t(numerator) * one / denominator));
}

bool inside(const Vector& point, const Vector& half) {
    return fixed::abs(point[0]) <= half[0] && fixed::abs(point[1]) <= half[1] && fixed::abs(point[2]) <= half[2];
}

std::int32_t box_exit(const Vector& point, const Vector& direction, const Vector& half) {
    // 45d270: select the first of X,Z,Y on equal exit distances.
    auto result = std::int32_t(0x7d000000);
    bool found = false;
    for (const auto axis : {0u, 2u, 1u}) {
        if (direction[axis] >= 0x290 || direction[axis] <= -0x290) {
            const auto edge = direction[axis] >= 0x290 ? sub(half[axis], point[axis]) : fixed::abs(add(half[axis], point[axis]));
            const auto distance = divide(edge, fixed::abs(direction[axis]));
            if (distance < result) {
                found = true;
                result = distance;
            }
        }
    }
    return found ? result : 0;
}

struct SectorGrid {
    struct VehicleSectors {
        std::array<std::uint32_t, 4> ids{};
        std::uint8_t count = 0;

        std::span<const std::uint32_t> active() const {
            return std::span(ids).first(count);
        }
    };

    std::vector<std::array<std::int32_t, 2>> centers;
    std::uint32_t width = 0, height = 0;
    std::int32_t minimum_x = 0, maximum_z = 0;

    explicit SectorGrid(std::span<const std::array<std::int32_t, 2>> source) : centers(source.begin(), source.end()) {
        if (centers.empty()) {
            throw std::runtime_error("Props: original terrain sector grid is missing");
        }
        while (width < centers.size() && centers[width][1] == centers[0][1]) {
            ++width;
        }
        if (centers.size() % width != 0) {
            throw std::runtime_error("Props: original sector rows are incomplete");
        }
        height = std::uint32_t(centers.size() / width);
        minimum_x = centers.front()[0];
        maximum_z = centers.front()[1];
        for (const auto& center : centers) {
            minimum_x = std::min(minimum_x, center[0]);
            maximum_z = std::max(maximum_z, center[1]);
        }
    }

    std::optional<std::uint32_t> sector(const Vector& point) const {
        const auto dx = sub(add(32 * one, point[0]), minimum_x), dz = add(sub(32 * one, point[2]), maximum_z);
        const auto x = add(dx, dx < 0 ? 63 : 0) >> 22, z = add(dz, dz < 0 ? 63 : 0) >> 22;
        if (x < 0 || z < 0 || std::uint32_t(x) >= width || std::uint32_t(z) >= height) {
            return std::nullopt;
        }
        return std::uint32_t(z) * width + std::uint32_t(x);
    }

    VehicleSectors vehicle_sectors(const Vector& point) const {
        const auto primary = sector(point);
        if (!primary) {
            return {};
        }
        VehicleSectors result;
        result.ids[result.count++] = *primary;
        const auto& center = centers[*primary];
        std::int32_t horizontal = 0, vertical = 0;
        if (sub(point[0], 0x48000) < sub(center[0], 32 * one)) {
            horizontal = -1;
        } else if (add(point[0], 0x48000) > add(center[0], 32 * one)) {
            horizontal = 1;
        }
        if (add(point[2], 0x48000) > add(center[1], 32 * one)) {
            vertical = -std::int32_t(width);
        } else if (sub(point[2], 0x48000) < sub(center[1], 32 * one)) {
            vertical = std::int32_t(width);
        }
        for (const auto offset : {horizontal, vertical, horizontal && vertical ? horizontal + vertical : 0}) {
            const auto candidate = std::int64_t(*primary) + offset;
            if (offset && candidate >= 0 && candidate < std::int64_t(centers.size())) {
                result.ids[result.count++] = std::uint32_t(candidate);
            }
        }
        return result;
    }
};
} // namespace

struct PropContacts::State {
    struct Cone {
        std::uint32_t root;
        Basis axes = identity_basis;
        Vector position{}, saved{}, normal{0, one, 0}, velocity{}, angular{}, force{}, torque{};
        // PE cache0 is consumed by the first ground lookup; 45c4d0 leaves it
        // untouched when restoring an existing registered cone for a warm grid.
        std::optional<std::uint16_t> triangle = 0;
        std::uint8_t state = 4;
        bool grounded = false, settled = false, translating = false, rotating = false;
    };

    struct Post {
        std::uint32_t root;
        Transform saved;
        Basis axes{}, rotation{};
        Vector position{}, normal{}, turn_axis{}, endpoints[2]{};
        std::int32_t degrees = 0, wait = 0;
        std::uint8_t state = 0;
        bool touched = false, broken = false;
    };

    struct Sign {
        std::uint32_t root;
        Transform saved;
        Basis axes{};
        Vector position{}, normal{}, pivot{}, endpoints[2]{};
        std::int16_t increment = 0;
        std::uint8_t state = 0;
        std::vector<std::uint32_t> hidden_nodes;
    };

    scene::PropFrame frame, previous_frame;
    scene::HorizonModelFrame initial_cones, initial_posts, initial_signs;
    std::vector<Cone> cones;
    std::vector<Post> posts;
    std::vector<Sign> signs;
    std::vector<std::optional<std::uint32_t>> post_sectors, cone_sectors, sign_sectors;
    SectorGrid grid;
    PropSurfaceQuery surface;
    bool low_gravity;
    std::int32_t cone_radius = std::int32_t(integer_sqrt((std::uint64_t(1870 * 16 + 8)) << 16));
    std::int32_t cone_slope_a = 0, cone_slope_b = 0, cone_slope_c = 0, cone_intercept_b = 0, cone_intercept_c = 0;
    std::int32_t cone_angle_a = 0, cone_angle_b = 0, cone_tipped_height = 0;

    State(const scene::PropResources& c, const scene::PropResources& p, const scene::PropResources& s, std::span<const std::array<std::int32_t, 2>> centers, PropSurfaceQuery query,
        bool gravity)
        : initial_cones(model_frame(c.model)), initial_posts(model_frame(p.model)), initial_signs(model_frame(s.model)), grid(centers), surface(std::move(query)),
          low_gravity(gravity) {
        if (!surface || c.actors.size() > 30 || p.actors.size() > 30 || s.actors.size() > 50) {
            throw std::runtime_error("Props: invalid original controller registration");
        }
        // 458680 reads these entries from478b20's source lookup tables.
        cone_tipped_height = mul(sine(atan_lookup(194)), 0x75c2);
        cone_angle_a = 1024 - asin_lookup(divide(mul(cone_tipped_height, 0x20000), 0xeb85));
        const auto second_angle = (2048 - atan_lookup(389)) - cone_angle_a;
        cone_angle_b = second_angle + cone_angle_a;
        const auto peak = divide(cone_tipped_height, sine(second_angle + 1024));
        cone_slope_a = divide(sub(cone_tipped_height, 0x75c2), cone_angle_a);
        cone_slope_b = divide(sub(cone_tipped_height, peak), sub(cone_angle_a, cone_angle_b));
        cone_intercept_b = sub(cone_tipped_height, mul(cone_slope_b, cone_angle_a));
        cone_slope_c = divide(sub(0x75c2, peak), sub(2048, cone_angle_b));
        cone_intercept_c = sub(peak, mul(cone_slope_c, cone_angle_b));
        for (const auto& actor : c.actors) {
            Cone cone{};
            cone.root = actor.root_node;
            cone.saved = position(initial_cones.transforms.at(actor.root_node));
            cones.push_back(cone);
        }
        for (const auto& actor : p.actors) {
            Post post{};
            post.root = actor.root_node;
            post.saved = initial_posts.transforms.at(actor.root_node);
            posts.push_back(post);
        }
        for (const auto& actor : s.actors) {
            Sign sign{};
            sign.root = actor.root_node;
            sign.saved = initial_signs.transforms.at(actor.root_node);
            for (std::uint32_t index = 0; index < s.model.nodes.size(); ++index) {
                auto ancestor = std::int32_t(index);
                while (ancestor >= 0 && std::uint32_t(ancestor) != sign.root) {
                    ancestor = s.model.nodes[std::size_t(ancestor)].parent;
                }
                if (ancestor >= 0) {
                    sign.hidden_nodes.push_back(index);
                }
            }
            signs.push_back(sign);
        }
        // Registration counts stay fixed across reset and source restore.
        post_sectors.resize(posts.size());
        cone_sectors.resize(cones.size());
        sign_sectors.resize(signs.size());
        reset();
    }

    std::optional<HpcPropSample> ground(const Vector& point, std::optional<std::uint16_t> cached = std::nullopt) {
        const auto sample = surface(point[0], point[2], cached);
        if (!sample) {
            frame.missing_surface = true;
        }
        return sample;
    }

    Vector normal(const HpcPropSample& sample) const {
        return sample.normal;
    }

    void reset() {
        frame = {};
        frame.cones = initial_cones;
        frame.posts = initial_posts;
        frame.signs = initial_signs;
        for (auto& cone : cones) {
            const auto root = cone.root;
            const auto saved = cone.saved;
            const auto triangle = cone.triangle;
            cone = Cone{};
            cone.root = root;
            cone.saved = saved;
            cone.triangle = triangle;
            cone.position = saved;
            publish(frame.cones.transforms[root], cone.axes, cone.position);
        }
        for (auto& post : posts) {
            post.axes = basis(post.saved);
            post.position = position(post.saved);
            post.state = 0;
            post.touched = post.broken = false;
            post.wait = post.degrees = 0;
            post.endpoints[0] = post.position;
            post.endpoints[0][1] = add(post.endpoints[0][1], 0x8666);
            post.endpoints[1] = post.endpoints[0];
            if (const auto sample = ground(post.position)) {
                post.normal = normal(*sample);
            }
        }
        for (auto& sign : signs) {
            sign.axes = basis(sign.saved);
            sign.position = position(sign.saved);
            sign.state = 0;
            sign.increment = 0;
            auto center = sign.position;
            center[1] = add(center[1], one);
            sign.endpoints[0] = add(center, sign.axes[0]);
            sign.endpoints[1] = sub(center, sign.axes[0]);
            if (const auto sample = ground(sign.position)) {
                sign.normal = normal(*sample);
            }
        }
        publish_states();
        previous_frame = frame;
    }

    void restore_source_state(std::span<const std::uint8_t> cone_records, std::span<const std::uint8_t> post_records, std::span<const std::uint8_t> sign_records,
        std::span<const std::array<std::uint8_t, 68>> sign_models) {
        if (cone_records.size() != cones.size() * 0xcc || post_records.size() != posts.size() * 0x13c || sign_records.size() != signs.size() * 0x40
            || sign_models.size() != signs.size()) {
            throw std::runtime_error("Props: captured controller counts differ from canonical registration");
        }
        const auto vector = [](std::span<const std::uint8_t> raw, std::size_t offset) {
            return Vector{source_i32(raw, offset), source_i32(raw, offset + 4), source_i32(raw, offset + 8)};
        };
        const auto compact_basis = [&](std::span<const std::uint8_t> raw, std::size_t offset) {
            return Basis{vector(raw, offset), vector(raw, offset + 12), vector(raw, offset + 24)};
        };
        // Decode before committing: malformed diagnostic input must not partly
        // replace a live shared world. Static native root identities are kept.
        auto imported_cones = cones;
        auto imported_posts = posts;
        auto imported_signs = signs;
        auto imported_frame = frame;
        for (std::size_t index = 0; index < cones.size(); ++index) {
            const auto raw = cone_records.subspan(index * 0xcc, 0xcc);
            auto& cone = imported_cones[index];
            cone.axes = compact_basis(raw, 0x48);
            cone.position = vector(raw, 0x6c);
            cone.normal = vector(raw, 0x78);
            cone.velocity = vector(raw, 0x84);
            cone.angular = vector(raw, 0x90);
            cone.force = vector(raw, 0x9c);
            cone.torque = vector(raw, 0xa8);
            cone.triangle = source_u16(raw, 0xb4);
            cone.grounded = source_u32(raw, 0xb8) != 0;
            cone.settled = source_u32(raw, 0xbc) != 0;
            cone.state = raw[0xc0];
            cone.translating = source_u32(raw, 0xc4) != 0;
            cone.rotating = source_u32(raw, 0xc8) != 0;
            if (cone.state > 4 || cone.state == 3) {
                throw std::runtime_error("Props: unsupported captured cone state");
            }
            publish(imported_frame.cones.transforms[cone.root], cone.axes, cone.position);
        }
        for (std::size_t index = 0; index < posts.size(); ++index) {
            const auto raw = post_records.subspan(index * 0x13c, 0x13c);
            auto& post = imported_posts[index];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                post.rotation[axis] = vector(raw, 0x88 + axis * 16);
                for (std::size_t component = 0; component < 3; ++component) {
                    post.saved[axis * 4 + component] = source_i32(raw, 0x48 + axis * 16 + component * 4);
                }
                post.saved[12 + axis] = source_i32(raw, 0x78 + axis * 4);
            }
            post.axes = compact_basis(raw, 0xc8);
            post.position = vector(raw, 0xec);
            post.turn_axis = vector(raw, 0xf8);
            post.normal = vector(raw, 0x104);
            post.degrees = source_i32(raw, 0x110);
            post.endpoints[0] = vector(raw, 0x114);
            post.endpoints[1] = vector(raw, 0x120);
            post.wait = source_i32(raw, 0x12c);
            post.state = raw[0x130];
            post.touched = source_u32(raw, 0x134) != 0;
            post.broken = source_u32(raw, 0x138) != 0;
            if (post.state > 6) {
                throw std::runtime_error("Props: unsupported captured post state");
            }
            publish(imported_frame.posts.transforms[post.root], post.axes, post.position);
        }
        for (std::size_t index = 0; index < signs.size(); ++index) {
            const auto raw = sign_records.subspan(index * 0x40, 0x40);
            auto& sign = imported_signs[index];
            const auto& model = sign_models[index];
            auto& matrix = imported_frame.signs.transforms[sign.root];
            for (std::size_t component = 0; component < matrix.size(); ++component) {
                matrix[component] = rounded(double(std::bit_cast<float>(source_u32(model, component * 4))) * one);
            }
            sign.axes = basis(matrix);
            sign.position = position(matrix);
            sign.endpoints[0] = vector(raw, 8);
            sign.endpoints[1] = vector(raw, 0x14);
            sign.normal = vector(raw, 0x20);
            sign.increment = std::bit_cast<std::int16_t>(source_u16(raw, 0x2c));
            sign.pivot = vector(raw, 0x30);
            const auto state = source_u32(raw, 0x3c);
            if (state != 0 && state != 1 && state != 3) {
                throw std::runtime_error("Props: unsupported captured sign state");
            }
            sign.state = std::uint8_t(state);
            for (const auto node : sign.hidden_nodes) {
                imported_frame.signs.visible[node] = model[64] != 0;
            }
        }
        cones = std::move(imported_cones);
        posts = std::move(imported_posts);
        signs = std::move(imported_signs);
        frame = std::move(imported_frame);
        frame.sounds.clear();
        frame.breaks.clear();
        frame.contacts = 0;
        frame.missing_surface = false;
        publish_states();
        previous_frame = frame;
    }

    void publish_states() {
        frame.cone_states.clear();
        frame.post_states.clear();
        frame.sign_states.clear();
        for (const auto& cone : cones) {
            frame.cone_states.push_back(cone.state);
        }
        for (const auto& post : posts) {
            frame.post_states.push_back(post.state);
        }
        for (const auto& sign : signs) {
            frame.sign_states.push_back(sign.state);
        }
    }

    void sound(const Vector& point, std::uint32_t kind, std::int32_t strength, bool local) {
        frame.sounds.push_back({point, kind, strength, local, std::uint32_t(frame.sounds.size() + frame.breaks.size())});
    }

    void haptic(PropResponse& response, const PropVehicle& car, std::int32_t strength) {
        if (car.local_player) {
            response.impact_strength_q16 = std::max(response.impact_strength_q16, std::clamp(strength, 0, one));
        }
    }

    bool broad(const Vector& point, const PropVehicle& car, std::int32_t extra) const {
        const auto delta = sub(point, car.body.position);
        const auto half = scene::car_half_dimensions(car.car_id);
        return inside(delta, Vector{half[0] * 4, half[0] * 4, half[0] * 4}) && length(delta) < add(length(half), extra);
    }

    bool endpoint_hit(const Vector& first, const Vector& second, const PropVehicle& car) const {
        const auto half = add(scene::car_half_dimensions(car.car_id), Vector{0x3333, 0x3333, 0x3333});
        return inside(float_local(sub(first, car.body.position), car.body.basis), half) || inside(float_local(sub(second, car.body.position), car.body.basis), half);
    }

    void post_contact(Post& post, const PropVehicle& car, PropResponse& response) {
        if (post.state == 5 || !broad(post.position, car, 0x8666) || !endpoint_hit(post.endpoints[0], post.endpoints[1], car)) {
            return;
        }
        ++response.contacts;
        post.touched = true;
        if (post.state == 6) {
            post.wait = 10;
            return;
        }
        if (post.state != 0 && post.state != 2) {
            return;
        }
        const auto speed = length(car.velocity_q16);
        auto kind = 0x12u;
        auto strength = std::min(mul(speed, 0x30000), one);
        if (post.state == 0) {
            const Vector axis{neg(car.velocity_q16[2]), 0, car.velocity_q16[0]};
            const auto horizontal_speed = length(axis);
            post.turn_axis = horizontal_speed < 0x107 ? Vector{one, 0, 0} : scale(axis, reciprocal(horizontal_speed));
            post.degrees = 0x190000;
            const auto drag = scale(car.velocity_q16, -0x1999);
            response.force_q16 = add(response.force_q16, drag);
            const auto offset = float_local(sub(post.position, car.body.position), car.body.basis);
            response.torque_q16 = add(response.torque_q16, cross(offset, float_local(drag, car.body.basis)));
            post.broken = horizontal_speed >= 0x10001;
            if (post.broken) {
                kind = 0x14;
                strength = one;
            }
        }
        post.state = 4;
        haptic(response, car, strength);
        sound(post.endpoints[0], kind, strength, car.local_player);
    }

    void cone_contact(Cone& cone, const PropVehicle& car, PropResponse& response) {
        if (!broad(cone.position, car, cone_radius)) {
            return;
        }
        const auto half = add(scene::car_half_dimensions(car.car_id), Vector{cone_radius, cone_radius, cone_radius});
        const auto relative = sub(cone.position, car.body.position);
        const auto relative_local = float_local(relative, car.body.basis);
        if (!inside(relative_local, half)) {
            return;
        }
        ++response.contacts;
        auto direction = relative;
        if (cone.grounded) {
            const auto tangent = project(relative, cone.normal);
            if (length(tangent) >= 0x106) {
                direction = tangent;
            }
        }
        direction = normalized(direction);
        const auto local_direction = !cone.grounded || length(project(relative, cone.normal)) < 0x106 ? normalized(relative_local) : float_local(direction, car.body.basis);
        cone.position = add(cone.position, scale(direction, box_exit(relative_local, local_direction, half)));
        const auto approach = dot(sub(car.velocity_q16, cone.velocity), direction);
        const auto impulse = mul(mul(approach, low_gravity ? 0x60000 : 0x40000), 0xa0000);
        cone.force = scale(direction, impulse);
        cone.torque = {};
        if (cone.grounded && fixed::abs(approach) > (low_gravity ? 0x51e : 0x3333)) {
            cone.force = add(cone.force, scale(cone.normal, impulse));
            cone.torque = cross(float_local(Vector{0, neg(cone_radius), 0}, cone.axes), float_local(cone.force, cone.axes));
        }
        cone.state = 1;
        publish(frame.cones.transforms[cone.root], cone.axes, cone.position);
        const auto strength = std::min(mul(approach, 0x30000), one);
        haptic(response, car, strength);
        sound(cone.position, 6, strength, car.local_player);
    }

    void sign_contact(Sign& sign, const PropVehicle& car, PropResponse& response, bool& local_sound) {
        if (sign.state || !broad(sign.position, car, 0x30000) || !endpoint_hit(sign.endpoints[0], sign.endpoints[1], car)) {
            return;
        }
        ++response.contacts;
        const auto speed = length(car.velocity_q16);
        if (speed >= 0x8001) {
            // The existing fragment sound has full strength, but allocation
            // and its source audio local flag cannot identify the struck car.
            haptic(response, car, one);
            frame.breaks.push_back({sign.root, frame.signs.transforms[sign.root], car.velocity_q16, std::uint32_t(frame.sounds.size() + frame.breaks.size())});
            sign.state = 1;
            for (const auto node : sign.hidden_nodes) {
                frame.signs.visible[node] = 0;
            }
            return;
        }
        const auto strength = mul(speed, 0x20000);
        haptic(response, car, strength);
        sound(sign.position, 0x12, strength, local_sound); // source sets local flag only after this call.
        const auto degrees = add(mul(strength, 0x70000), 0x30000);
        const bool reverse = dot(car.velocity_q16, sign.axes[1]) < 0;
        sign.increment = std::int16_t((reverse ? -degrees : degrees) / 0x1680);
        sign.pivot = add(sign.position, scale(sign.axes[1], reverse ? -0xccc : 0xccc));
        if (car.local_player) {
            local_sound = true;
        }
        sign.state = 3;
    }

    std::vector<PropResponse> contact(std::span<const PropVehicle> cars) {
        std::vector<PropResponse> responses(cars.size());
        for (auto& post : posts) {
            post.touched = false;
        }
        // 45b900 reads the actor's stored+120 sector. A cone moved across a
        // sector edge by the current hit must not re-enter a later neighbor
        // list during this same dispatcher call.
        const auto snapshot_sectors = [&](const auto& actors, auto& sectors) {
            for (std::size_t index = 0; index < actors.size(); ++index) {
                sectors[index] = grid.sector(actors[index].position);
            }
        };
        snapshot_sectors(posts, post_sectors);
        snapshot_sectors(cones, cone_sectors);
        snapshot_sectors(signs, sign_sectors);
        for (std::size_t actor = 0; actor < cars.size(); ++actor) {
            const auto& car = cars[actor];
            auto& response = responses[actor];
            const auto sectors = grid.vehicle_sectors(car.body.position);
            for (const auto sector : sectors.active()) {
                for (std::size_t i = 0; i < posts.size(); ++i) {
                    if (post_sectors[i] == sector) {
                        post_contact(posts[i], car, response);
                    }
                }
                for (std::size_t i = 0; i < cones.size(); ++i) {
                    if (cone_sectors[i] == sector) {
                        cone_contact(cones[i], car, response);
                    }
                }
                bool local_sign_sound = false; // 45d4a0 local_8 survives its candidate loop.
                for (std::size_t i = 0; i < signs.size(); ++i) {
                    if (sign_sectors[i] == sector) {
                        sign_contact(signs[i], car, response, local_sign_sound);
                    }
                }
            }
            frame.contacts += response.contacts;
        }
        for (auto& post : posts) {
            if (post.state == 1 && !post.touched) {
                post.rotation = rotation(post.turn_axis, mul(post.degrees, 0x3333) / 0x1680);
                post.state = 6;
                post.wait = 10;
            }
        }
        publish_states();
        return responses;
    }

    void advance_post(Post& post) {
        if (post.state == 4) {
            post.rotation = rotation(post.turn_axis, -post.degrees / 0x1680);
            post.state = 3;
            return;
        }
        if (post.state == 6) {
            if (!post.wait) {
                post.state = 2;
                sound(post.endpoints[0], 0x13, one, false);
            } else {
                --post.wait;
            }
            return;
        }
        if (post.state == 2) {
            if (post.axes[1][1] < 0xcccc) {
                for (auto& axis : post.axes) {
                    axis = normalized(float_world(axis, post.rotation));
                }
            } else {
                post.state = 0;
                post.axes = basis(post.saved);
            }
            publish(frame.posts.transforms[post.root], post.axes, post.position);
        } else if (post.state == 3) {
            for (auto& axis : post.axes) {
                axis = normalized(float_world(axis, post.rotation));
            }
            publish(frame.posts.transforms[post.root], post.axes, post.position);
            const auto down = dot(post.normal, post.axes[1]);
            if (down < 1) {
                post.axes[1] = normalized(project(post.axes[1], post.normal));
                post.axes[0] = normalized(project(post.axes[0], post.axes[1]));
                post.axes[2] = normalized(cross(post.axes[0], post.axes[1]));
                publish(frame.posts.transforms[post.root], post.axes, post.position);
                post.state = post.broken ? 5 : 1;
            }
        }
        if (post.state == 0 || post.state == 1 || post.state == 2 || post.state == 3 || post.state == 5) {
            post.endpoints[1][0] = add(post.position[0], mul(post.axes[1][0], 0x8666));
            post.endpoints[1][2] = add(post.position[2], mul(post.axes[1][2], 0x8666));
        }
    }

    void advance_sign(Sign& sign) {
        if (sign.state != 3) {
            return;
        }
        const auto turn = rotation(sign.axes[0], sign.increment);
        sign.axes[1] = normalized(float_world(sign.axes[1], turn));
        sign.axes[2] = normalized(float_world(sign.axes[2], turn));
        if (dot(sign.axes[2], sign.normal) >= 0) {
            sign.axes[2] = normalized(project(sign.axes[2], sign.normal));
            sign.axes[1] = normalized(project(sign.axes[1], sign.axes[2]));
            sign.state = 1;
        }
        sign.axes[0] = normalized(cross(sign.axes[1], sign.axes[2]));
        // Source45b010 copies its local X vector into all three temporary
        // rotation rows before47b150; retain that observable aliasing.
        const Basis aliased{sign.axes[0], sign.axes[0], sign.axes[0]};
        sign.position = sub(sign.pivot, float_world(Vector{0, sign.increment > 0 ? 0xccc : -0xccc, 0}, aliased));
        publish(frame.signs.transforms[sign.root], sign.axes, sign.position);
    }

    std::int32_t cone_height(const Cone& cone) const {
        auto angle = 1024 - asin_lookup(neg(dot(cone.axes[1], cone.normal)));
        if (angle > 2048) {
            angle = 4096 - angle;
        }
        std::int32_t value = 0;
        if (angle < cone_angle_a) {
            value = add(mul(angle, cone_slope_a), 0x75c2);
        } else if (angle < cone_angle_b) {
            value = add(mul(angle, cone_slope_b), cone_intercept_b);
        } else {
            value = add(mul(angle, cone_slope_c), cone_intercept_c);
        }
        return divide(value, cone.normal[1]);
    }

    void settle_cone(Cone& cone) {
        // 4594f0: stable upright or tipped orientation, source table thresholds.
        const auto up = dot(cone.axes[1], cone.normal);
        const auto threshold = sine(1706); // DAT53b2e8.
        if (fixed::abs(up) > threshold && up >= 0) {
            cone.axes[1] = cone.normal;
        } else {
            auto tangent = project(cone.axes[1], cone.normal);
            if (!length(tangent)) {
                cone.axes[1] = cone.normal;
            } else {
                tangent = normalized(tangent);
                // DAT53ffcc = source cosine table[227].
                cone.axes[1] = normalized(sub(tangent, scale(cone.normal, sine(1024 + 227))));
            }
        }
        cone.axes[0] = normalized(project(cone.axes[0], cone.axes[1]));
        cone.axes[2] = normalized(cross(cone.axes[0], cone.axes[1]));
    }

    void advance_cone(Cone& cone) {
        if (cone.state == 2) {
            return;
        }
        if (!cone.grounded) {
            cone.velocity[1] = sub(cone.velocity[1], low_gravity ? 0x28f : 0x1999);
            cone.translating = true;
        } else {
            cone.velocity = scale(cone.velocity, 0xe666);
            if (length(cone.velocity) < 0x41) {
                cone.velocity = {};
                cone.translating = false;
            }
            cone.angular = scale(cone.angular, 0xe666);
            if (length(cone.angular) < 0x41) {
                cone.angular = {};
                cone.rotating = false;
            }
        }
        if (cone.state == 1) {
            cone.angular = sub(cone.angular, Vector{cone.torque[0], mul(cone.torque[1], 0x20000), cone.torque[2]});
            const auto angular_speed = length(cone.angular);
            cone.rotating = angular_speed != 0;
            if (angular_speed > 0x8000) {
                cone.angular = scale(cone.angular, divide(0x8000, angular_speed));
            }
            cone.velocity = add(cone.velocity, scale(scale(cone.force, 0x1999), 0x3333));
            const auto speed = length(cone.velocity);
            cone.translating = speed != 0;
            if (speed > 0x20000) {
                cone.velocity = scale(cone.velocity, divide(0x20000, speed));
            }
            cone.state = 0;
        }
        if (cone.translating) {
            cone.position = add(cone.position, cone.velocity);
        }
        if (cone.rotating) {
            std::array<std::int16_t, 3> angles{};
            for (std::size_t i = 0; i < 3; ++i) {
                angles[i] = std::bit_cast<std::int16_t>(std::uint16_t(mul(cone.angular[i], 0x28b)));
            }
            rotate(cone.axes, angles);
        }
        const auto sample = ground(cone.position, cone.triangle);
        if (!sample) {
            publish(frame.cones.transforms[cone.root], cone.axes, cone.position);
            return;
        }
        cone.triangle = sample->triangle;
        cone.normal = normal(*sample);
        if (cone.position[1] < add(add(sample->height, 0x6666), cone_radius)) {
            std::int32_t height = 0;
            if (!cone.rotating) {
                if (!cone.settled) {
                    settle_cone(cone);
                    cone.settled = true;
                }
                height = divide(fixed::abs(dot(cone.axes[1], cone.normal)) > sine(1706) ? 0x75c2 : cone_tipped_height, cone.normal[1]);
                if (!cone.translating) {
                    cone.state = 2;
                }
            } else {
                cone.settled = false;
                height = cone_height(cone);
            }
            cone.position[1] = add(sample->height, height);
            if (cone.translating) {
                cone.velocity = project(cone.velocity, cone.normal);
            }
            cone.grounded = true;
        } else {
            cone.grounded = false;
        }
        publish(frame.cones.transforms[cone.root], cone.axes, cone.position);
    }

    void advance() {
        previous_frame = frame;
        ++frame.updates;
        frame.sounds.clear();
        frame.breaks.clear();
        frame.missing_surface = false;
        for (auto cone = cones.rbegin(); cone != cones.rend(); ++cone) {
            advance_cone(*cone);
        }
        for (auto post = posts.rbegin(); post != posts.rend(); ++post) {
            advance_post(*post);
        }
        for (auto sign = signs.rbegin(); sign != signs.rend(); ++sign) {
            advance_sign(*sign);
        }
        publish_states();
    }
};

PropContacts::PropContacts(const scene::PropResources& cones, const scene::PropResources& posts, const scene::PropResources& signs,
    std::span<const std::array<std::int32_t, 2>> centers, PropSurfaceQuery surface, bool low_gravity)
    : state_(std::make_unique<State>(cones, posts, signs, centers, std::move(surface), low_gravity)) {}

PropContacts::PropContacts(const scene::PropResources& cones, const scene::PropResources& posts, const scene::PropResources& signs,
    std::span<const std::array<std::int32_t, 2>> centers, const HpcSurface& surface, bool low_gravity)
    : PropContacts(
          cones, posts, signs, centers,
          [&surface](auto x, auto z, auto cached) {
              return surface.prop_sample(x, z, cached);
          },
          low_gravity) {}

PropContacts::~PropContacts() = default;
PropContacts::PropContacts(PropContacts&&) noexcept = default;
PropContacts& PropContacts::operator=(PropContacts&&) noexcept = default;

void PropContacts::reset() {
    state_->reset();
}

void PropContacts::advance() {
    state_->advance();
}

PropResponse PropContacts::contact(const PropVehicle& vehicle) {
    return state_->contact(std::span(&vehicle, 1)).front();
}

std::vector<PropResponse> PropContacts::contact(std::span<const PropVehicle> vehicles) {
    if (vehicles.size() > 16) {
        throw std::runtime_error("Props: contact dispatcher accepts up to sixteen original actors");
    }
    return state_->contact(vehicles);
}

void PropContacts::restore_source_state(
    std::span<const std::uint8_t> cones, std::span<const std::uint8_t> posts, std::span<const std::uint8_t> signs, std::span<const std::array<std::uint8_t, 68>> sign_models) {
    state_->restore_source_state(cones, posts, signs, sign_models);
}

const scene::PropFrame& PropContacts::frame() const {
    return state_->frame;
}

PropContactSnapshot PropContacts::snapshot() const {
    PropContactSnapshot result;
    result.cones.reserve(state_->cones.size());
    result.posts.reserve(state_->posts.size());
    result.signs.reserve(state_->signs.size());
    for (const auto& cone : state_->cones) {
        result.cones.push_back({cone.axes, cone.position, cone.normal, cone.velocity, cone.angular, cone.force, cone.torque, cone.triangle, cone.state, cone.grounded, cone.settled,
            cone.translating, cone.rotating});
    }
    for (const auto& post : state_->posts) {
        result.posts.push_back({post.saved, post.axes, post.rotation, post.position, post.normal, post.turn_axis, {post.endpoints[0], post.endpoints[1]}, post.degrees, post.wait,
            post.state, post.touched, post.broken});
    }
    for (const auto& sign : state_->signs) {
        result.signs.push_back({state_->frame.signs.transforms.at(sign.root), {sign.endpoints[0], sign.endpoints[1]}, sign.normal, sign.pivot, sign.increment, sign.state,
            state_->frame.signs.visible.at(sign.root) != 0});
    }
    return result;
}

scene::PropFrame PropContacts::presentation_frame(float alpha) const {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Prop presentation requires a finite fraction");
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    auto result = state_->frame;
    if (alpha == 1) {
        return result;
    }
    const auto interpolate = [alpha](const scene::HorizonModelFrame& previous, scene::HorizonModelFrame& current) {
        for (std::size_t node = 0; node < current.transforms.size(); ++node) {
            current.transforms[node] = scene::interpolate_horizon_transform(previous.transforms[node], current.transforms[node], alpha);
        }
    };
    interpolate(state_->previous_frame.cones, result.cones);
    interpolate(state_->previous_frame.posts, result.posts);
    interpolate(state_->previous_frame.signs, result.signs);
    return result;
}
} // namespace toca::simulation
