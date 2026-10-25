#pragma once

#include "formats/model.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace toca::scene {
struct MegaGroundTransitionVertex {
    std::array<float, 3> position;
    // Interpolate the existing source-corner lighting without changing normals
    // or the original terrain's face flags.
    std::array<float, 3> source_weights;
    std::uint8_t opacity = 255;
};

using MegaGroundTransitionTriangle = std::array<MegaGroundTransitionVertex, 3>;

struct MegaGroundTransition {
    MegaGroundTransitionTriangle grass;
    std::vector<MegaGroundTransitionTriangle> gravel;
};

struct MegaGroundTransitionProfile {
    std::string_view mixed, grass, gravel;
    float feather_half_width;
    float cap_width = 0;
};

struct MegaGroundDirectSegment {
    std::array<std::array<float, 3>, 2> endpoints;
    float width;
    std::array<float, 3> normal{0, 1, 0};
    std::array<bool, 2> taper{};
};

struct MegaGroundTransitionCap {
    MegaGroundDirectSegment edge;
    bool gravel;
};

// A single, nonoverlapping gravel layer inside this existing grass triangle.
// Segment distance and endpoint caps are affine in world space. Taking their
// maximum alpha preserves continuity across neighboring grass source faces.
// Tapered ends fade longitudinally to zero at their endpoint.
std::vector<MegaGroundTransitionTriangle> prepare_mega_ground_direct_band(const std::array<ModelVertex, 3>& source, std::span<const MegaGroundDirectSegment> segments);

// Exact canonical material names and the accepted source-UV feather width.
std::optional<MegaGroundTransitionProfile> mega_ground_transition_profile(std::uint32_t track_id) noexcept;

// Only accepted Mega mixed-material slots. Original/BMP comparisons draw their
// authored images; other tracks/profiles stay outside.
bool is_mega_ground_transition(bool mega, std::uint32_t track_id, const ModelTexture& texture) noexcept;

// Cache per original source face. Both layers remain coplanar inside that face;
// callers map their positions with the corresponding GRASS/GRAVEL world UVs.
// Draw grass opaque with depth writes, then gravel blended without depth writes.
// The source shoreline stays in original UV space, independent of grain UVs.
// The source-UV feather half-width must be finite and in (0, .2]. The default
// preserves the accepted 3.2-source-pixel full band; selected patches may widen
// that band without moving the shoreline's half-coverage contour.
std::optional<MegaGroundTransition> prepare_mega_ground_transition(
    std::uint32_t track_id, const std::array<ModelVertex, 3>& source, float feather_half_width = .05f, std::span<const MegaGroundTransitionCap> caps = {});

std::uint32_t mega_ground_transition_color(const MegaGroundTransitionVertex& vertex, const std::array<std::uint32_t, 3>& source_colors) noexcept;
} // namespace toca::scene
