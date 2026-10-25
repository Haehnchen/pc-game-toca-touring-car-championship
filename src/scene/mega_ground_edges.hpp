#pragma once

#include "formats/terrain.hpp"
#include "scene/mega_ground_transitions.hpp"

#include <optional>

namespace toca::scene {
struct MegaGroundGapBridge {
    std::size_t source_chunk, source_face;
    std::int16_t texture;
    // Positions derive from unchanged source edges and endpoint cuts; color
    // weights refer to corresponding corners of the lighting/UV source face.
    std::vector<MegaGroundTransitionTriangle> triangles;
};

struct MegaGroundEdges {
    std::vector<std::vector<bool>> faces;
    std::vector<std::vector<std::optional<std::array<float, 3>>>> normals;
    std::vector<std::vector<std::vector<MegaGroundTransitionCap>>> caps;
    std::vector<MegaGroundGapBridge> bridges;
    // Cached ownership: no full bridge scan during per-face rendering. Empty
    // outer storage when no source gaps need bridging.
    std::vector<std::vector<std::vector<std::size_t>>> bridge_indices;
};

struct MegaGroundDirectProfile {
    std::string_view grass, gravel;
    float width;
};

struct MegaGroundDirectEdges {
    std::vector<MegaGroundDirectSegment> edges;
    std::vector<std::vector<std::vector<MegaGroundTransitionTriangle>>> gravel;
    std::vector<std::vector<std::optional<std::array<float, 3>>>> normals;
};

std::optional<MegaGroundDirectProfile> mega_ground_direct_profile(std::uint32_t track_id) noexcept;

// Direct plain-material boundaries only. Draw these cached gravel triangles
// after their ordinary grass source faces; merge optional normals at load time.
// Seeds and propagation accept only paired eligible edges, including canonical
// copies within .002 world units with compatible planes. Segments stay on the
// authored grass edge; lighting still uses exact positions. Never crosses marks.
MegaGroundDirectEdges prepare_mega_ground_direct_edges(const Terrain& terrain, std::uint32_t track_id);

// Accepted Mega mixed-material faces, including disconnected
// patches. Cache at load time; original terrain and contact data stay intact.
// Boundary normals change only where all incident faces are eligible neutral
// ground and every source normal lies within 15 degrees of their shared average.
// Lavaland independently caches opaque source-gap bridges for its audited
// road/wall and same-material pairs; faces/caps/normals remain empty there.
MegaGroundEdges prepare_mega_ground_edges(const Terrain& terrain, std::uint32_t track_id);
} // namespace toca::scene
