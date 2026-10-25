#pragma once

#include "scene/prop_frame.hpp"

#include <optional>

namespace toca {
class SourceRandom;
}

namespace toca::scene {
struct PropFragmentTables {
    std::array<std::int32_t, 37> positive{}, signed_values{};
    std::array<std::uint8_t, 37> color_indices{};
    std::array<std::uint8_t, 37> counts{};
    std::uint32_t cursor = 0;
};

// 431820's111 draws precede4153c0's74 draws at4467a0, after resource/flag
// initialization. Runtime fragmentation consumes this shared cyclic table only.
PropFragmentTables make_prop_fragment_tables(SourceRandom& random);

struct PropFragmentTriangle {
    std::array<std::array<std::int32_t, 3>, 3> vertices{};
    std::uint32_t color = 0;
};

// 409fa0/15e70's impact request; car IDs select the two source palettes.
struct ImpactFragmentRequest {
    std::array<std::int32_t, 3> position{}, velocity{};
    std::int32_t intensity_q16 = 0;
    std::array<std::uint8_t, 2> car_ids{};
};

class PropFragments {
    struct Fragment {
        PropFragmentTriangle triangle, previous_triangle;
        std::array<std::int32_t, 3> velocity{};
        std::int32_t initial_y = 0;
        std::uint32_t remaining = 200;
        bool impact = false;
    };

    PropFragmentTables& tables_;
    std::uint32_t weather_;
    std::int32_t gravity_;
    std::int32_t impact_gravity_;
    std::vector<Fragment> fragments_;
    mutable std::vector<PropFragmentTriangle> triangles_;

public:
    PropFragments(PropFragmentTables& tables, std::uint32_t weather, bool low_gravity = false);
    // 417cd0 returns event21 only when the63-fragment pool has room.
    std::optional<PropSoundEvent> break_sign(const HorizonTransform& root, const std::array<std::int32_t, 3>& vehicle_velocity);
    void impact(const ImpactFragmentRequest& request); // 15e70, same63-slot pool.
    void update(); // One explicit416150 tick; rendering never advances state.
    void clear(); // 415d50 clears the pool, preserving the shared table cursor.
    std::span<const PropFragmentTriangle> triangles() const;
    // Record-local geometry history survives pool compaction; births cut.
    std::vector<PropFragmentTriangle> presentation_triangles(float alpha) const;
    std::size_t size() const;
};
} // namespace toca::scene
