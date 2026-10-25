#pragma once

#include "formats/terrain.hpp"

namespace toca {
struct StaticModel {
    Model model;
    Terrain baked;
};

// 00494a40 bit29 static branch, 00496980 raw fixed transforms and UV clamp.
// All variants survive; node_index/variant_index identify their source slots.
// Decode the original hierarchy and its static consumer together.
StaticModel parse_static_model(std::span<const std::uint8_t> bytes);
} // namespace toca
