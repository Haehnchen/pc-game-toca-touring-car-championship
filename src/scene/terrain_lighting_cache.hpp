#pragma once

#include "formats/terrain.hpp"
#include "scene/lighting.hpp"

#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>

namespace toca::scene {
using TerrainLightingNormals = std::span<const std::optional<std::array<float, 3>>>;

// Source chunks and optional Mega normals are immutable and keep stable
// addresses for the cache's lifetime, just as they do for their owning view.
// Changing the actual light value invalidates colors without changing any
// quantized lighting arithmetic or the source-face override order.
class TerrainLightingCache {
public:
    using Prepare = std::function<void(const TerrainChunk&, const Lighting&, std::vector<std::uint32_t>&, TerrainLightingNormals)>;

    explicit TerrainLightingCache(Prepare prepare) : prepare_(std::move(prepare)) {}

    // The result remains valid until another call changes this chunk's normals
    // or the lighting. A light change keeps vector storage for later reuse.
    const std::vector<std::uint32_t>& colors(const TerrainChunk& chunk, const Lighting& lighting, TerrainLightingNormals normals = {}) {
        if (!lighting_ || *lighting_ != lighting) {
            for (auto& [source, entry] : entries_) {
                entry.valid = false;
            }
            lighting_ = lighting;
        }
        auto& entry = entries_[&chunk];
        if (!entry.valid || entry.normals != normals.data() || entry.normal_count != normals.size()) {
            entry.valid = false;
            prepare_(chunk, lighting, entry.colors, normals);
            entry.normals = normals.data();
            entry.normal_count = normals.size();
            entry.valid = true;
        }
        return entry.colors;
    }

private:
    struct Entry {
        std::vector<std::uint32_t> colors;
        const std::optional<std::array<float, 3>>* normals = nullptr;
        std::size_t normal_count = 0;
        bool valid = false;
    };

    Prepare prepare_;
    std::optional<Lighting> lighting_;
    std::unordered_map<const TerrainChunk*, Entry> entries_;
};
} // namespace toca::scene
