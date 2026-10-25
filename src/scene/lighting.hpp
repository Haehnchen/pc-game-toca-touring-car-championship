#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace toca {
struct TerrainChunk;
}

namespace toca::scene {
struct SceneSetup;

struct Lighting {
    std::array<float, 3> direction;
    float diffuse = 0;
    std::array<std::uint8_t, 3> ambient{};

    bool operator==(const Lighting&) const = default;
};

Lighting scene_lighting(const SceneSetup& setup);
std::uint32_t model_vertex_color(const std::array<float, 3>& normal, const std::array<std::uint8_t, 4>& material, const Lighting& lighting);
// 487afe consumes the local normal and rounds basis/light products before it.
std::uint32_t model_vertex_color(
    const std::array<float, 3>& local_normal, const std::array<float, 16>& object_to_world, const std::array<std::uint8_t, 4>& material, const Lighting& lighting);
std::uint32_t ghost_vertex_color(const std::array<float, 3>& normal, const Lighting& lighting);
std::uint32_t ghost_vertex_color(const std::array<float, 3>& local_normal, const std::array<float, 16>& object_to_world, const Lighting& lighting);
std::span<const std::int16_t> model_lighting_angles();

// Independent CPU consumers of 00496c50/0047e730; no GPU context needed.
std::vector<std::uint32_t> prepare_terrain_colors(const TerrainChunk& chunk, const Lighting& lighting);
void prepare_terrain_colors_into(
    const TerrainChunk& chunk, const Lighting& lighting, std::vector<std::uint32_t>& result, std::span<const std::optional<std::array<float, 3>>> normals = {});
} // namespace toca::scene
