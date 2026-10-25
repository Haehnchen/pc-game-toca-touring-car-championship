#include "scene/lighting.hpp"

#include "formats/terrain.hpp"
#include "scene/catalog.hpp"
#include "scene/setup.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
namespace {
const std::array<std::int16_t, 4096>& angle_table() {
    static const auto result = [] {
        std::array<std::int16_t, 4096> table;
        for (std::size_t index = 0; index < table.size(); ++index) {
            // 00478b20: asin(index/4095), truncate radians*65536, then *651>>16.
            const auto radians = std::int32_t(std::asin(double(float(index)) / 4095.0) * 65536.0);
            table[index] = std::int16_t((std::int64_t(radians) * 0x28b) >> 16);
        }
        return table;
    }();
    return result;
}

int illuminated_angle(float dot) {
    if (!std::isfinite(dot)) {
        throw std::runtime_error("Terrain lighting requires finite normal/direction inputs");
    }
    // 00496c50 converts to fixed, retains sign, clamps magnitude and indexes >>4.
    const auto fixed = std::int32_t(std::clamp(dot, -1.f, 1.f) * 65536.f);
    const auto magnitude = std::min(std::uint32_t(fixed < 0 ? -fixed : fixed), 65535u);
    const auto angle = angle_table()[magnitude >> 4];
    return fixed < 0 ? 0 : angle;
}

} // namespace

void prepare_terrain_colors_into(
    const TerrainChunk& chunk, const Lighting& lighting, std::vector<std::uint32_t>& result, std::span<const std::optional<std::array<float, 3>>> normals) {
    if (!std::isfinite(lighting.diffuse) || lighting.diffuse < 0
        || !std::ranges::all_of(
            lighting.direction,
            [](float value) {
                return std::isfinite(value);
            })) {
        throw std::runtime_error("Terrain lighting requires finite original light parameters");
    }
    if (chunk.face_flags.size() != chunk.mesh.faces.size()) {
        throw std::runtime_error("Terrain face flags do not match the original face records");
    }
    if (!normals.empty() && normals.size() != chunk.mesh.vertices.size()) {
        throw std::runtime_error("Terrain lighting normals must match source vertices");
    }
    result.clear();
    result.reserve(chunk.mesh.vertices.size());
    for (std::size_t index = 0; index < chunk.mesh.vertices.size(); ++index) {
        const auto& vertex = chunk.mesh.vertices[index];
        const auto normal = normals.empty() ? vertex.normal : normals[index].value_or(vertex.normal);
        const auto dot = normal[0] * lighting.direction[0] + normal[1] * lighting.direction[1] + normal[2] * lighting.direction[2];
        // Assembly 00496d43..91: angle*(light.red*250/1024)+ambient.red,
        // truncated, saturated, then copied to ALL three color channels.
        const auto value = std::clamp(float(illuminated_angle(dot)) * (lighting.diffuse * .244140625f) + lighting.ambient[0], 0.f, 255.f);
        const auto channel = std::uint32_t(value);
        result.push_back(0xff000000 | (channel << 16) | (channel << 8) | channel);
    }
    for (std::size_t index = 0; index < chunk.mesh.faces.size(); ++index) {
        if ((chunk.face_flags[index] & 0x80) != 0) {
            for (const auto vertex : chunk.mesh.faces[index].indices) {
                result.at(vertex) = 0xff828282;
            }
        }
    }
}

std::vector<std::uint32_t> prepare_terrain_colors(const TerrainChunk& chunk, const Lighting& lighting) {
    std::vector<std::uint32_t> result;
    prepare_terrain_colors_into(chunk, lighting, result);
    return result;
}

std::span<const std::int16_t> model_lighting_angles() {
    return angle_table();
}

Lighting scene_lighting(const SceneSetup& setup) {
    const auto& content = track_content(setup.selection.track_id);
    if (setup.weather < 1 || setup.weather > 5) {
        throw std::runtime_error("Scene lighting requires resolved weather");
    }
    // 0044eda9/0049da90; catalog directions retain the midpoint-sqrt normalization.
    // The first light direction is negated again into DAT_00634508..10.
    std::array<float, 3> direction;
    for (std::size_t axis = 0; axis < direction.size(); ++axis) {
        direction[axis] = float(content.light_direction_q16[axis]) / 65536.f;
    }
    constexpr std::array<std::int32_t, 6> diffuse{0, 65536, 58982, 52428, 45875, 32768};
    constexpr std::array<std::int32_t, 6> ambient{0, 39321, 32768, 32768, 32768, 26214};
    const auto gain = std::int32_t((std::int64_t(diffuse[setup.weather]) * 0xcccc) >> 16);
    const auto channel = std::uint8_t((ambient[setup.weather] * 255) >> 16);
    return {direction, float(gain) / 65536.f, {channel, channel, channel}};
}

std::uint32_t model_vertex_color(const std::array<float, 3>& normal, const std::array<std::uint8_t, 4>& material, const Lighting& lighting) {
    float dot = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        dot += normal[axis] * lighting.direction[axis];
    }
    // 00487afe uses x87 nearest rounding, unlike 00496c50's truncation.
    if (!std::isfinite(dot) || !std::isfinite(lighting.diffuse) || lighting.diffuse < 0) {
        throw std::runtime_error("Invalid model lighting");
    }
    const auto fixed = std::int32_t(std::nearbyint(std::clamp(dot, -1.f, 1.f) * 65536.f));
    const auto index = std::min(std::uint32_t(fixed < 0 ? -fixed : fixed), 65535u) >> 4;
    const auto fraction = fixed < 0 ? 0.f : float(angle_table()[index]) / 1024.f;
    std::uint32_t result = std::uint32_t(material[3]) << 24;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto channel = std::uint32_t(
            std::clamp(std::nearbyint(float(material[axis]) * lighting.diffuse * fraction + (float(lighting.ambient[axis]) / 255.f) * float(material[axis])), 0.f, 255.f));
        result |= channel << ((2 - axis) * 8);
    }
    return result;
}

std::uint32_t model_vertex_color(const std::array<float, 3>& normal, const std::array<float, 16>& matrix, const std::array<std::uint8_t, 4>& material, const Lighting& lighting) {
    std::array<float, 3> components;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        // 487c78..fc: nine individual FSTP float coefficients. The local
        // normal then contributes to each light axis, not to a world normal.
        const float x = float(double(matrix[axis]) * lighting.direction[axis]);
        const float y = float(double(matrix[4 + axis]) * lighting.direction[axis]);
        const float z = float(double(matrix[8 + axis]) * lighting.direction[axis]);
        const double a = double(normal[0]) * x, b = double(normal[1]) * y, c = double(normal[2]) * z;
        components[axis] = float(axis == 0 ? c + (a + b) : axis == 1 ? (b + a) + c : (a + b) + c);
    }
    // 487d66/7d/98 store three components, then 487da4 stores their sum.
    const float dot = float((double(components[0]) + components[1]) + components[2]);
    if (!std::isfinite(dot) || !std::isfinite(lighting.diffuse) || lighting.diffuse < 0) {
        throw std::runtime_error("Invalid model lighting");
    }
    const auto fixed = std::int32_t(std::nearbyint(double(dot) * 65536.0));
    const auto index = std::min(std::uint32_t(fixed < 0 ? -fixed : fixed), 65535u) >> 4;
    const float fraction = fixed < 0 ? 0.f : float(angle_table()[index]) / 1024.f;
    std::uint32_t result = std::uint32_t(material[3]) << 24;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        // 487b54/ba3/bf2 and 487c1d/49/75 are separate float stores.
        const float ambient = float((double(lighting.ambient[axis]) / 255.0) * material[axis]);
        const float diffuse = float(double(material[axis]) * lighting.diffuse);
        const auto channel = std::uint32_t(std::clamp(std::nearbyint(double(diffuse) * fraction + ambient), 0.0, 255.0));
        result |= channel << ((2 - axis) * 8);
    }
    return result;
}

std::uint32_t ghost_vertex_color(const std::array<float, 3>& local_normal, const std::array<float, 16>& object_to_world, const Lighting& lighting) {
    // Race startup488f49(1) routes ghosts through487afe, whose ambient
    // multiplier is the face diffuse, rather than D3DMATERIAL.ambient.
    return model_vertex_color(local_normal, object_to_world, {150, 150, 255, 150}, lighting);
}

std::uint32_t ghost_vertex_color(const std::array<float, 3>& normal, const Lighting& lighting) {
    constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return ghost_vertex_color(normal, identity, lighting);
}

} // namespace toca::scene
