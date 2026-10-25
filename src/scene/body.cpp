#include "scene/body.hpp"

#include "core/contracts.hpp"
#include "core/source_fixed.hpp"
#include "scene/matrix.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace toca::scene {
namespace {
using Vector = std::array<std::int32_t, 3>;
using matrix::camera_multiply;
using matrix::Matrix;

using source_fixed::add;
using source_fixed::bits;
using source_fixed::mul;
using source_fixed::sub;

Vector normalized(Vector vector) {
    const auto square = std::uint32_t(add(add(mul(vector[0], vector[0]), mul(vector[1], vector[1])), mul(vector[2], vector[2])));
    if (square == 0) {
        return {};
    }
    const auto length = source_fixed::sqrt(square);
    const auto inverse = bits(std::uint32_t(INT64_C(0x100000000) / length));
    for (auto& component : vector) {
        component = mul(component, inverse);
    }
    return vector;
}

Vector cross(const Vector& a, const Vector& b) {
    Vector result{};
    for (std::size_t index = 0; index < 3; ++index) {
        result[index] = sub(mul(a[(index + 1) % 3], b[(index + 2) % 3]), mul(a[(index + 2) % 3], b[(index + 1) % 3]));
    }
    return result;
}

std::int32_t dot(const Vector& a, const Vector& b) {
    return add(add(mul(a[0], b[0]), mul(a[1], b[1])), mul(a[2], b[2]));
}

std::int32_t sine(std::int32_t angle) {
    // 478b20 and literal double 004ac6d8. Lookup identities matter at quadrants.
    return simulation::fixed::sine(angle);
}

std::array<Vector, 3> yaw(std::int16_t heading) {
    const auto cosine = sine(0x400 - heading);
    const auto negative_sine = sine(-std::int32_t(heading));
    return {Vector{cosine, 0, negative_sine}, Vector{0, 65536, 0}, Vector{sub(0, negative_sine), 0, cosine}};
}

Vector half_dimensions(std::uint32_t car_id) {
    // 40a770's car switch, then arithmetic >>1 at 40afxx.
    constexpr std::array<Vector, 10> dimensions{
        {{0x4af5c, 0x1428f, 0x1d9db}, {0x48000, 0x1428f, 0x203d7}, {0x49062, 0x1428f, 0x1dd2f}, {0x4926e, 0x1428f, 0x1f9db}, {0x4c49b, 0x1428f, 0x1eb43},
            {0x48831, 0x1428f, 0x1ca7e}, {0x48ac0, 0x1428f, 0x1f9db}, {0x49126, 0x1428f, 0x1ec08}, {0x42872, 0x22ed9, 0x23c28}, {0x5c6a7, 0x183d7, 0x2553f}}};
    if (car_id >= dimensions.size()) {
        throw std::runtime_error("Initial body pose requires original car 0..9");
    }
    auto half = dimensions[car_id];
    for (auto& component : half) {
        component >>= 1;
    }
    return half;
}

void contacts(BodyPose& body, const Vector& half) {
    // 40c280 shifts each positive half-extent product before combining signs.
    // Multiplying signed corners instead changes the negative-product rounding.
    for (std::size_t component = 0; component < 3; ++component) {
        const auto x = mul(body.basis[0][component], half[0]);
        const auto y = mul(body.basis[1][component], half[1]);
        const auto z = mul(body.basis[2][component], half[2]);
        const std::array<std::int32_t, 4> top{add(add(z, y), x), add(sub(y, z), x), add(sub(z, x), y), sub(sub(y, x), z)};
        for (std::size_t index = 0; index < 4; ++index) {
            body.contacts[index + 4][component] = add(body.position[component], top[index]);
            body.contacts[index][component] = sub(body.position[component], top[3 - index]);
        }
    }
}

void sample_contacts(BodyPose& body, const HpcSurface& surface, bool first_pass) {
    for (std::size_t index = 0; index < body.contacts.size(); ++index) {
        const auto& point = body.contacts[index];
        const auto sample = surface.sample(point[0], point[2], first_pass ? std::uint16_t(0) : body.surfaces[index].triangle);
        if (!sample) {
            throw std::runtime_error("Initial body contact " + std::to_string(index) + " lies outside the HPC surface");
        }
        body.surfaces[index] = *sample;
    }
}

Vector initial_support_normal(const BodyPose& body) {
    //40bbc0's yaw basis has Y as the dominant vertical axis: corners0/1/2.
    Vector first{}, second{};
    for (std::size_t component = 0; component < 3; ++component) {
        const auto a = component == 1 ? body.surfaces[0].height : body.contacts[0][component];
        const auto b = component == 1 ? body.surfaces[1].height : body.contacts[1][component];
        const auto c = component == 1 ? body.surfaces[2].height : body.contacts[2][component];
        first[component] = sub(b, a);
        second[component] = sub(c, a);
    }
    auto up = cross(second, first);
    if (up[1] < 0) {
        for (auto& component : up) {
            component = sub(0, component);
        }
    }
    return normalized(up);
}

void publish_matrix(BodyPose& body) {
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t component = 0; component < 3; ++component) {
            body.object_to_world[row * 4 + component] = float(body.basis[row][component]) / 65536.0f;
        }
        body.object_to_world[12 + row] = float(body.position[row]) / 65536.0f;
    }
    body.object_to_world[15] = 1;
}

Matrix inverse(const Matrix& value, Matrix* published_world) {
    if (published_world) {
        *published_world = value;
    }
    return matrix::camera_inverse(value, "Initial follow camera has no invertible basis");
}

} // namespace

InitialBodySupport initial_body_support(const HpcSurface& surface, const HorizontalPlacement& start, std::uint32_t car_id) {
    BodyPose body;
    body.position = {start.xz[0], 0, start.xz[1]};
    body.basis = yaw(start.heading);
    contacts(body, half_dimensions(car_id));
    sample_contacts(body, surface, true);
    return {body.surfaces, initial_support_normal(body)};
}

BodyPose initial_body_pose(const HpcSurface& surface, const HorizontalPlacement& start, std::uint32_t car_id, const InitialBodySupport* retained_support, bool current_body) {
    if (!current_body && !retained_support) {
        throw std::runtime_error("Noncurrent initial body requires its retained loader support");
    }
    const auto half = half_dimensions(car_id);
    BodyPose body;
    body.position = {start.xz[0], bits(0x83000000), start.xz[1]};
    body.basis = yaw(start.heading);
    contacts(body, half);
    if (retained_support) {
        body.surfaces = retained_support->surfaces;
    }
    if (current_body) {
        sample_contacts(body, surface, retained_support == nullptr);
    }
    const auto up = current_body ? initial_support_normal(body) : retained_support->normal;
    const auto projection = dot(body.basis[0], up);
    for (std::size_t component = 0; component < 3; ++component) {
        body.basis[0][component] = sub(body.basis[0][component], mul(up[component], projection));
    }
    body.basis[0] = normalized(body.basis[0]);
    body.basis[1] = up;
    body.ground_normal = up;
    body.basis[2] = normalized(cross(body.basis[0], up));
    if (up == Vector{} || body.basis[0] == Vector{} || body.basis[2] == Vector{}) {
        throw std::runtime_error("Initial body contacts cannot define a nondegenerate basis");
    }
    if (!current_body) {
        publish_matrix(body);
        return body;
    }
    contacts(body, half);
    sample_contacts(body, surface, false);

    // 410190: highest positive contact correction, otherwise nearest negative
    // correction within the source's -500-unit search bound.
    std::int32_t positive = 0, negative = -0x1f40000;
    bool has_positive = false, has_negative = false;
    for (std::size_t index = body.contacts.size(); index-- > 0;) {
        const auto correction = sub(body.surfaces[index].height, body.contacts[index][1]);
        if (correction > positive) {
            positive = correction;
            has_positive = true;
        } else if (correction <= 0 && correction > negative) {
            negative = correction;
            has_negative = true;
        }
    }
    const auto correction = has_positive ? positive : has_negative ? negative : 0;
    body.position[1] = add(body.position[1], correction);
    for (auto& point : body.contacts) {
        point[1] = add(point[1], correction);
    }
    publish_matrix(body);
    return body;
}

void refresh_body_contacts(BodyPose& body, std::uint32_t car_id) {
    contacts(body, half_dimensions(car_id));
    publish_matrix(body);
}

std::array<std::int32_t, 3> car_half_dimensions(std::uint32_t car_id) {
    return half_dimensions(car_id);
}

std::array<float, 16> initial_follow_world_to_view(const BodyPose& body, std::int16_t anchor_heading, bool micro, std::optional<bool> split_vertical, Matrix* published_world) {
    const auto anchor_basis = yaw(anchor_heading);
    Matrix anchor{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t component = 0; component < 3; ++component) {
            anchor[row * 4 + component] = float(anchor_basis[row][component]) / 65536.0f;
        }
        anchor[12 + row] = float(body.position[row]) / 65536.0f;
    }
    anchor[15] = 1;
    if (micro) {
        // 4508ce's CMMICRO View3: height40, pitch03dd, yawfc23.
        // 47fe20 rotates local Y then local X (roll is zero here).
        const auto sp = -float(sine(-0x3dd)) / 65536.0f, cp = float(sine(0x400 - 0x3dd)) / 65536.0f;
        const auto sy = -float(sine(0x3dd)) / 65536.0f, cy = float(sine(0x400 + 0x3dd)) / 65536.0f;
        const Matrix local{cy, 0, -sy, 0, sp * sy, cp, sp * cy, 0, cp * sy, -sp, cp * cy, 0, 0, 40, 0, 1};
        return inverse(camera_multiply(local, anchor), published_world);
    }
    const auto sine_pitch = -float(sine(-0x6e)) / 65536.0f;
    const auto cosine_pitch = float(sine(0x400 - 0x6e)) / 65536.0f;
    // 4508ce's normal single-player View2 (table base 4c3478, entry 4c3480).
    const auto distance = split_vertical ? (*split_vertical ? 0x6eb76 : 0x5335c) : 0x5668e;
    const Matrix local{0, 0, -1, 0, sine_pitch, cosine_pitch, 0, 0, cosine_pitch, -sine_pitch, 0, 0, -float(distance) / 65536.0f, float(0x1800f) / 65536.0f, 0, 1};
    return inverse(camera_multiply(local, anchor), published_world);
}

std::array<float, 16> race_world_to_view(const BodyPose& body, std::int16_t anchor_heading, std::uint16_t view, std::uint32_t car_id, bool micro, std::uint32_t detail,
    std::optional<bool> split_vertical, Matrix* published_world) {
    if (car_id > 9 || detail >= graphics_detail_count || view < 2 || view > 5) {
        throw std::runtime_error("Race camera requires source view2..5, car0..9 and detail0..4");
    }
    if (view == 2 || (view == 3 && micro)) {
        return initial_follow_world_to_view(body, anchor_heading, view == 3 && micro, split_vertical, published_world);
    }
    Matrix anchor{};
    if (view == 3) {
        const auto basis = yaw(anchor_heading);
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t component = 0; component < 3; ++component) {
                anchor[row * 4 + component] = float(basis[row][component]) / 65536.f;
            }
        }
    } else {
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t component = 0; component < 3; ++component) {
                anchor[row * 4 + component] = float(body.basis[row][component]) / 65536.f;
            }
        }
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        anchor[12 + axis] = float(body.position[axis]) / 65536.f;
    }
    anchor[15] = 1;
    std::int32_t offset_x = 0, offset_y = 0, offset_z = 0, pitch = 0;
    bool rear = false;
    switch (view) {
        case 3:
            offset_x = -0x70000;
            offset_y = 0x20000;
            pitch = 0x38;
            if (split_vertical) {
                offset_x = *split_vertical ? -0x79972 : -0x6ccec;
                offset_y = *split_vertical ? 0x1a8ff : 0x219a5;
                pitch = 0x6e;
            }
            break;
        case 4: {
            constexpr std::array<std::int32_t, 10> bonnet{19660, 20971, 21626, 19660, 22937, 19660, 26214, 20971, 19660, 62259};
            offset_x = bonnet[car_id] + (car_id == 8 && detail < 2 ? 0x3333 : 0);
            offset_y = 0xb333;
            if (split_vertical) {
                if (*split_vertical) {
                    offset_x = car_id == 8 ? 0x4ccc : car_id == 9 ? 0xf333 : 0xc18d;
                    offset_y = car_id == 8 || car_id == 9 ? 0xb333 : 0x75f8;
                    pitch = 0x3c;
                } else {
                    offset_x = car_id == 8 ? 0x6666 : car_id == 9 ? 0x10000 : 0xdbda;
                    offset_y = car_id == 8 ? 0xd999 : car_id == 9 ? 0xe666 : 0xb341;
                    pitch = 0x6e;
                }
                if (car_id == 8 && detail < 2) {
                    offset_x += 0x3333;
                }
            }
            break;
        }
        case 5: {
            constexpr std::array<std::int32_t, 10> back{-78643, -72089, -65536, -65536, -85196, -65536, -65536, -78643, -52428, -45875};
            offset_x = back[car_id];
            offset_y = 0xc000;
            if (split_vertical && !*split_vertical) {
                offset_x = -0x129b0;
                offset_y = 0xe68b;
                pitch = 0x6e;
            }
            rear = true;
            break;
        }
    }
    const auto sp = -float(sine(-pitch)) / 65536.f;
    const auto cp = float(sine(0x400 - pitch)) / 65536.f;
    const auto direction = rear ? -1.f : 1.f;
    const Matrix local{
        0, 0, -direction, 0, sp * direction, cp, 0, 0, cp * direction, -sp, 0, 0, float(offset_x) / 65536.f, float(offset_y) / 65536.f, float(offset_z) / 65536.f, 1};
    return inverse(camera_multiply(local, anchor), published_world);
}

std::array<float, 16> cockpit_world_to_view(
    const BodyPose& body, const std::array<Vector, 3>& anchor_basis, const std::array<Vector, 3>& camera_basis, const Vector& offset, Matrix* published_world) {
    Matrix anchor{}, local{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            anchor[row * 4 + axis] = float(anchor_basis[row][axis]) / 65536.f;
            local[row * 4 + axis] = float(camera_basis[row][axis]) / 65536.f;
        }
        anchor[12 + row] = float(body.position[row]) / 65536.f;
        local[12 + row] = float(offset[row]) / 65536.f;
    }
    anchor[15] = local[15] = 1;
    return inverse(camera_multiply(local, anchor), published_world);
}
} // namespace toca::scene
