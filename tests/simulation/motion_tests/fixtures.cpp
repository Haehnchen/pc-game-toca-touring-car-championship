#include "fixtures.hpp"

#include "simulation/fixed.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace toca::test::motion {
using namespace toca::simulation;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = std::uint8_t(value);
    bytes.at(offset + 1) = std::uint8_t(value >> 8);
}

void put32(Bytes& bytes, std::size_t offset, std::int32_t value) {
    put16(bytes, offset, std::uint16_t(value));
    put16(bytes, offset + 2, std::uint16_t(std::uint32_t(value) >> 16));
}

Bytes plane(std::uint16_t flags) {
    // A complete 128x128 square, split into two clockwise XZ triangles.
    // Each plane cross product is 16384 Q16, within the source signed range;
    // using one enlarged triangle would unnecessarily exclude the upper half.
    Bytes bytes(106);
    put32(bytes, 0, -128 * unit);
    put32(bytes, 4, -128 * unit);
    put16(bytes, 8, 1);
    put16(bytes, 20, 2);
    put16(bytes, 24, 4);
    const std::array<std::array<std::int32_t, 2>, 4> vertices{{{-64 * unit, -64 * unit}, {-64 * unit, 64 * unit}, {64 * unit, -64 * unit}, {64 * unit, 64 * unit}}};
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        put32(bytes, 28 + i * 12, vertices[i][0]);
        put32(bytes, 32 + i * 12, 2 * unit);
        put32(bytes, 36 + i * 12, vertices[i][1]);
    }
    put16(bytes, 76, 2);
    const std::array<std::array<std::uint16_t, 3>, 2> faces{{{0, 1, 2}, {2, 1, 3}}};
    for (std::size_t i = 0; i < faces.size(); ++i) {
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            put16(bytes, 80 + i * 8 + vertex * 2, faces[i][vertex]);
        }
        put16(bytes, 86 + i * 8, flags);
    }
    put16(bytes, 96, 2);
    put16(bytes, 100, 1);
    put16(bytes, 102, 1);
    put16(bytes, 104, 1);
    return bytes;
}

Bytes driving_plane(std::uint16_t flags, bool split) {
    // 256x256 coverage with small triangles keeps every source Q16 plane
    // product in range. The split boundary at X=0 separates road and grass.
    constexpr std::size_t vertex_count = 25, face_count = 32;
    constexpr auto face_header = 28 + vertex_count * 12;
    constexpr auto face_start = face_header + 4;
    constexpr auto index_header = face_start + face_count * 8;
    Bytes bytes(index_header + 2 + face_count * 2 + 4);
    put32(bytes, 0, -128 * unit);
    put32(bytes, 4, -128 * unit);
    put16(bytes, 8, 1);
    put16(bytes, 20, face_count);
    put16(bytes, 24, vertex_count);
    for (std::size_t z = 0; z < 5; ++z) {
        for (std::size_t x = 0; x < 5; ++x) {
            const auto offset = 28 + (z * 5 + x) * 12;
            put32(bytes, offset, (std::int32_t(x) * 64 - 128) * unit);
            put32(bytes, offset + 4, 2 * unit);
            put32(bytes, offset + 8, (std::int32_t(z) * 64 - 128) * unit);
        }
    }
    put16(bytes, face_header, face_count);
    std::uint16_t face = 0;
    for (std::uint16_t z = 0; z < 4; ++z) {
        for (std::uint16_t x = 0; x < 4; ++x) {
            const auto a = std::uint16_t(z * 5 + x);
            for (const auto triangle : {std::array<std::uint16_t, 3>{a, std::uint16_t(a + 5), std::uint16_t(a + 1)},
                     std::array<std::uint16_t, 3>{std::uint16_t(a + 1), std::uint16_t(a + 5), std::uint16_t(a + 6)}}) {
                const auto offset = face_start + face * 8;
                for (std::size_t i = 0; i < 3; ++i) {
                    put16(bytes, offset + i * 2, triangle[i]);
                }
                put16(bytes, offset + 6, split && x < 2 ? 0 : flags);
                put16(bytes, index_header + 2 + face * 2, face);
                ++face;
            }
        }
    }
    put16(bytes, index_header, face_count);
    put16(bytes, bytes.size() - 4, 1);
    put16(bytes, bytes.size() - 2, 1);
    return bytes;
}

void finite(const MotionState& state) {
    expect(state.boundary == MotionBoundary::none, "Covered road motion must not stop at a fabricated conservative boundary");
    expect(state.speed_q16 >= 0 && state.speed_q16 < 20 * unit, "Short source-force trace produced an unbounded speed");
    for (auto component : state.body.object_to_world) {
        expect(std::isfinite(component), "Motion must publish a finite body render transform");
    }
    for (std::size_t i = 0; i < 3; ++i) {
        expect(state.body.object_to_world[12 + i] == float(state.body.position[i]) / unit, "Render translation must follow the integrated body position");
    }
    for (auto strength : state.rumble_strength_q16) {
        expect(strength >= 0 && strength <= unit, "Original per-wheel sound strength must remain bounded");
    }
}

void ticks(Motion& motion, std::uint32_t count, const ControlsInput& input) {
    for (std::uint32_t i = 0; i < count; ++i) {
        motion.update(input, {});
        if (motion.state().boundary != MotionBoundary::none) {
            const auto& state = motion.state();
            std::ostringstream message;
            message << "Motion boundary " << int(state.boundary) << " at tick " << i + 1 << '/' << count << " throttle/brake/left/right " << input.throttle << input.brake
                    << input.left << input.right << " last position " << state.body.position[0] << ' ' << state.body.position[1] << ' ' << state.body.position[2] << " velocity "
                    << state.velocity_q16[0] << ' ' << state.velocity_q16[1] << ' ' << state.velocity_q16[2];
            for (std::size_t contact = 0; contact < state.body.contacts.size(); ++contact) {
                const auto& point = state.body.contacts[contact];
                message << " contact" << contact << " x/z " << point[0] << '/' << point[2] << " sum " << std::int64_t(point[0]) + point[2];
            }
            throw std::runtime_error(message.str());
        }
        finite(motion.state());
    }
}

} // namespace toca::test::motion
