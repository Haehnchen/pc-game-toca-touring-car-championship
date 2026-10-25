#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace toca {
struct HpcSample {
    std::int32_t height = 0;
    // Original 42e6f0 plane ratios (nx/ny, 1, nz/ny), not a unit normal.
    std::array<std::int32_t, 3> normal_ratio{};
    std::uint16_t triangle = 0, flags = 0;
};

struct HpcPropSample {
    std::int32_t height = 0;
    // 42e9f0 normalizes both edges before their cross product and its normal.
    std::array<std::int32_t, 3> normal{};
    std::uint16_t triangle = 0, flags = 0;
};

// Owns decoded data; no span into the input archive survives construction.
class HpcSurface {
    struct Cell {
        std::int16_t count;
        std::uint16_t offset;
    };

    struct Plane {
        std::array<std::int32_t, 3> normal{};
        std::int32_t offset = 0;
    };

    struct Face {
        std::array<std::uint16_t, 3> vertices;
        std::uint16_t flags;
        // The two recovered queries use different fixed-point normalization.
        // A zero Y keeps an invalid plane deferred until that query selects it.
        Plane ground{}, prop{};
    };

    std::array<std::int32_t, 2> origin_{};
    std::array<std::vector<Cell>, 5> levels_;
    std::vector<std::array<std::int32_t, 3>> vertices_;
    std::vector<Face> faces_;
    std::vector<std::uint16_t> indices_;
    std::uint16_t width_ = 0, height_ = 0;

    bool contains(std::uint16_t triangle, std::int32_t x, std::int32_t z) const;
    std::optional<std::uint16_t> find(std::int32_t x, std::int32_t z) const;
    bool in_domain(std::int32_t x, std::int32_t z) const;
    Plane ground_plane(const Face& face) const;
    Plane prop_plane(const Face& face) const;
    HpcSample sample_triangle(std::int32_t x, std::int32_t z, std::uint16_t triangle) const;
    HpcPropSample prop_sample_triangle(std::int32_t x, std::int32_t z, std::uint16_t triangle) const;

public:
    explicit HpcSurface(std::span<const std::uint8_t> bytes);
    // 42e6f0/42eed0: cached containing face wins, otherwise first tree match.
    // Outside/empty cells return nullopt; a selected vertical plane is invalid.
    std::optional<HpcSample> sample(std::int32_t x, std::int32_t z, std::optional<std::uint16_t> cached_triangle = std::nullopt) const;
    std::optional<HpcPropSample> prop_sample(std::int32_t x, std::int32_t z, std::optional<std::uint16_t> cached_triangle = std::nullopt) const;
    // 42e6f0/42e9f0 ignore42eed0's search failure and retain the caller's
    // own cached face. Only these actor queries extrapolate that valid plane;
    // outside the bounded HPC root rectangle they still return nullopt.
    std::optional<HpcSample> sample_retaining_cached(std::int32_t x, std::int32_t z, std::optional<std::uint16_t> cached_triangle) const;
    std::optional<HpcPropSample> prop_sample_retaining_cached(std::int32_t x, std::int32_t z, std::optional<std::uint16_t> cached_triangle) const;
    std::size_t triangle_count() const;
};
} // namespace toca
