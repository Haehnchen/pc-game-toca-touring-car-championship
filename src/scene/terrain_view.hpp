#pragma once

#include "formats/terrain.hpp"
#include "render/gpu_renderer.hpp"
#include "scene/camera.hpp"
#include "scene/lighting.hpp"

#include <optional>

namespace toca::scene {
class ModelTextures;

// Bounded terrain pass. The owned Terrain must outlive this view.
// Prop/car visibility, scene ordering and the race lifecycle remain separate.
class TerrainView {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    TerrainView(
        gpu::Renderer& renderer, const Terrain& terrain, bool mipmaps = false, std::optional<std::uint32_t> mega_track_id = {}, std::shared_ptr<ModelTextures> textures = {});
    ~TerrainView();
    TerrainView(const TerrainView&) = delete;
    TerrainView& operator=(const TerrainView&) = delete;
    gpu::Frame frame(const CameraInputs& camera, const Lighting& lighting, std::optional<std::span<const std::uint32_t>> chunks = {}) const;
    // Preserve the target's existing draws and presentation metadata.
    void append(gpu::Frame& target, const CameraInputs& camera, const Lighting& lighting, std::optional<std::span<const std::uint32_t>> chunks = {}) const;
};

} // namespace toca::scene
