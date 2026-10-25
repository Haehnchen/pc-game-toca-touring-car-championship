#include "formats/hpc.hpp"

#include "core/source_fixed.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca {
namespace {
using source_fixed::add;
using source_fixed::bits;
using source_fixed::mul;
using source_fixed::sub;

std::array<std::int32_t, 3> normalized(const std::array<std::int32_t, 3>& value) {
    const auto square = std::uint32_t(add(add(mul(value[2], value[2]), mul(value[0], value[0])), mul(value[1], value[1])));
    if (square == 0) {
        return {};
    }
    // 478b20's midpoint lookup, also used by the original 42e9f0 query.
    const auto length = source_fixed::sqrt(square);
    if (length == 0) {
        return {};
    }
    const auto inverse = bits(std::uint32_t(INT64_C(0x100000000) / length));
    return {mul(value[0], inverse), mul(value[1], inverse), mul(value[2], inverse)};
}

class Source {
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0;

public:
    explicit Source(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint16_t u16() {
        if (cursor_ > bytes_.size() || bytes_.size() - cursor_ < 2) {
            throw std::runtime_error("HPC: truncated field");
        }
        const auto value = std::uint16_t(bytes_[cursor_]) | (std::uint16_t(bytes_[cursor_ + 1]) << 8);
        cursor_ += 2;
        return std::uint16_t(value);
    }

    std::int32_t fixed() {
        const auto low = u16();
        return bits(low | (std::uint32_t(u16()) << 16));
    }

    bool eof() const {
        return cursor_ == bytes_.size();
    }
};
} // namespace

HpcSurface::HpcSurface(std::span<const std::uint8_t> bytes) {
    Source source(bytes);
    origin_ = {source.fixed(), source.fixed()};
    std::array<std::uint16_t, 5> counts{};
    for (auto& count : counts) {
        count = source.u16();
        if (count > 0x7fff) {
            throw std::runtime_error("HPC: negative cell count");
        }
    }
    (void)source.u16(); // Original header alignment.
    for (std::size_t level = 0; level < levels_.size(); ++level) {
        auto& cells = levels_[level];
        cells.reserve(counts[level]);
        for (std::uint16_t index = 0; index < counts[level]; ++index) {
            cells.push_back({std::bit_cast<std::int16_t>(source.u16()), source.u16()});
        }
    }
    const auto vertex_count = source.u16();
    (void)source.u16(); // Vertex and face count headers are four bytes each.
    vertices_.reserve(vertex_count);
    for (std::uint32_t index = 0; index < vertex_count; ++index) {
        vertices_.push_back({source.fixed(), source.fixed(), source.fixed()});
    }
    const auto face_count = source.u16();
    if (face_count > 0x8000) {
        throw std::runtime_error("HPC: face identity exceeds signed query index");
    }
    (void)source.u16();
    faces_.reserve(face_count);
    for (std::uint32_t index = 0; index < face_count; ++index) {
        Face face{{source.u16(), source.u16(), source.u16()}, source.u16()};
        for (const auto vertex : face.vertices) {
            if (vertex >= vertices_.size()) {
                throw std::runtime_error("HPC: face vertex outside vertex table");
            }
        }
        faces_.push_back(face);
    }
    const auto index_count = source.u16();
    indices_.reserve(index_count);
    for (std::uint32_t index = 0; index < index_count; ++index) {
        const auto triangle = source.u16();
        if (triangle >= faces_.size()) {
            throw std::runtime_error("HPC: leaf triangle outside face table");
        }
        indices_.push_back(triangle);
    }
    width_ = source.u16();
    height_ = source.u16();
    if (!source.eof() || width_ == 0 || height_ == 0 || std::uint32_t(width_) * height_ != levels_[0].size()) {
        throw std::runtime_error("HPC: invalid root dimensions or trailing bytes");
    }
    for (std::size_t level = 0; level < levels_.size(); ++level) {
        for (const auto& cell : levels_[level]) {
            if (cell.count == -1) {
                if (level + 1 == levels_.size() || std::size_t(cell.offset) + 16 > levels_[level + 1].size()) {
                    throw std::runtime_error("HPC: branch outside next cell level");
                }
            } else if (cell.count < 0 || std::size_t(cell.offset) + std::uint16_t(cell.count) > indices_.size()) {
                throw std::runtime_error("HPC: leaf outside triangle index list");
            }
        }
    }
    // Vertices never change after parsing. Preserve both original algorithms
    // independently, including cases where only one produces a height plane.
    for (auto& face : faces_) {
        face.ground = ground_plane(face);
        face.prop = prop_plane(face);
    }
}

bool HpcSurface::contains(std::uint16_t triangle, std::int32_t x, std::int32_t z) const {
    const auto& face = faces_[triangle];
    const auto& a = vertices_[face.vertices[0]];
    const auto& b = vertices_[face.vertices[1]];
    const auto& c = vertices_[face.vertices[2]];
    if (x < std::min({a[0], b[0], c[0]}) || x > std::max({a[0], b[0], c[0]}) || z < std::min({a[2], b[2], c[2]}) || z > std::max({a[2], b[2], c[2]})) {
        return false;
    }
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& first = vertices_[face.vertices[index]];
        const auto& second = vertices_[face.vertices[(index + 1) % 3]];
        const auto side = add(mul(sub(x, first[0]), sub(second[2], first[2])), mul(sub(z, first[2]), sub(first[0], second[0])));
        if (side < 0) {
            return false;
        }
    }
    return true;
}

bool HpcSurface::in_domain(std::int32_t x, std::int32_t z) const {
    const auto dx = std::int64_t(x) - origin_[0], dz = std::int64_t(z) - origin_[1];
    return dx >= 0 && dz >= 0 && (std::uint64_t(dx) >> 24) < width_ && (std::uint64_t(dz) >> 24) < height_;
}

std::optional<std::uint16_t> HpcSurface::find(std::int32_t x, std::int32_t z) const {
    const auto dx = sub(x, origin_[0]);
    const auto dz = sub(z, origin_[1]);
    // The source uses signed high bytes and unchecked linear indexing. Reject
    // points outside the actual rectangle before indexing the native vectors.
    if (dx < 0 || dz < 0 || (std::uint32_t(dx) >> 24) >= width_ || (std::uint32_t(dz) >> 24) >= height_) {
        return std::nullopt;
    }
    auto index = std::size_t(std::uint32_t(dx) >> 24) + std::size_t(width_) * (std::uint32_t(dz) >> 24);
    for (std::size_t level = 0; level < levels_.size(); ++level) {
        const auto& cell = levels_[level][index];
        if (cell.count == -1) {
            const auto shift = unsigned(22 - 2 * level);
            index = cell.offset + ((std::uint32_t(dx) >> shift) & 3) + 4 * ((std::uint32_t(dz) >> shift) & 3);
        } else {
            for (std::uint16_t offset = 0; offset < cell.count; ++offset) {
                const auto triangle = indices_[std::size_t(cell.offset) + offset];
                if (contains(triangle, x, z)) {
                    return triangle;
                }
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<HpcSample> HpcSurface::sample(std::int32_t x, std::int32_t z, std::optional<std::uint16_t> cached_triangle) const {
    if (cached_triangle && *cached_triangle >= faces_.size()) {
        throw std::runtime_error("HPC: cached triangle outside face table");
    }
    const auto triangle = cached_triangle && contains(*cached_triangle, x, z) ? cached_triangle : find(x, z);
    if (!triangle) {
        return std::nullopt;
    }
    return sample_triangle(x, z, *triangle);
}

HpcSurface::Plane HpcSurface::ground_plane(const Face& face) const {
    const auto& a = vertices_[face.vertices[0]];
    const auto& b = vertices_[face.vertices[1]];
    const auto& c = vertices_[face.vertices[2]];
    std::array<std::int32_t, 3> ab{}, ac{}, normal{};
    for (std::size_t index = 0; index < 3; ++index) {
        ab[index] = sub(b[index], a[index]);
        ac[index] = sub(c[index], a[index]);
    }
    for (std::size_t index = 0; index < 3; ++index) {
        normal[index] = sub(mul(ab[(index + 1) % 3], ac[(index + 2) % 3]), mul(ab[(index + 2) % 3], ac[(index + 1) % 3]));
    }
    if (normal[1] == 0) {
        return {};
    }
    const auto inverse_y = bits(std::uint32_t(INT64_C(0x100000000) / normal[1]));
    normal = {mul(normal[0], inverse_y), 0x10000, mul(normal[2], inverse_y)};
    const auto plane = add(add(mul(normal[2], a[2]), mul(normal[0], a[0])), a[1]);
    return {normal, plane};
}

HpcSample HpcSurface::sample_triangle(std::int32_t x, std::int32_t z, std::uint16_t triangle) const {
    const auto& face = faces_[triangle];
    const auto& plane = face.ground;
    if (plane.normal[1] == 0) {
        throw std::runtime_error("HPC: selected triangle has no height plane");
    }
    const auto height = sub(sub(plane.offset, mul(x, plane.normal[0])), mul(z, plane.normal[2]));
    return HpcSample{height, plane.normal, triangle, face.flags};
}

std::optional<HpcPropSample> HpcSurface::prop_sample(std::int32_t x, std::int32_t z, std::optional<std::uint16_t> cached_triangle) const {
    if (cached_triangle && *cached_triangle >= faces_.size()) {
        throw std::runtime_error("HPC: cached prop triangle outside face table");
    }
    const auto triangle = cached_triangle && contains(*cached_triangle, x, z) ? cached_triangle : find(x, z);
    if (!triangle) {
        return std::nullopt;
    }
    return prop_sample_triangle(x, z, *triangle);
}

HpcSurface::Plane HpcSurface::prop_plane(const Face& face) const {
    const auto& a = vertices_[face.vertices[0]];
    const auto& b = vertices_[face.vertices[1]];
    const auto& c = vertices_[face.vertices[2]];
    std::array<std::int32_t, 3> ab{}, ac{}, normal{};
    for (std::size_t index = 0; index < 3; ++index) {
        ab[index] = sub(b[index], a[index]);
        ac[index] = sub(c[index], a[index]);
    }
    ab = normalized(ab);
    ac = normalized(ac);
    for (std::size_t index = 0; index < 3; ++index) {
        normal[index] = sub(mul(ab[(index + 1) % 3], ac[(index + 2) % 3]), mul(ab[(index + 2) % 3], ac[(index + 1) % 3]));
    }
    normal = normalized(normal);
    if (normal[1] == 0) {
        return {};
    }
    const auto plane = add(add(mul(normal[2], a[2]), mul(normal[0], a[0])), mul(normal[1], a[1]));
    return {normal, plane};
}

HpcPropSample HpcSurface::prop_sample_triangle(std::int32_t x, std::int32_t z, std::uint16_t triangle) const {
    const auto& face = faces_[triangle];
    const auto& plane = face.prop;
    if (plane.normal[1] == 0) {
        throw std::runtime_error("HPC: selected prop triangle has no height plane");
    }
    const auto distance = sub(sub(plane.offset, mul(z, plane.normal[2])), mul(x, plane.normal[0]));
    const auto height = bits(std::uint32_t(std::int64_t(distance) * 65536 / plane.normal[1]));
    return HpcPropSample{height, plane.normal, triangle, face.flags};
}

std::optional<HpcSample> HpcSurface::sample_retaining_cached(std::int32_t x, std::int32_t z, std::optional<std::uint16_t> cached_triangle) const {
    if (cached_triangle && *cached_triangle >= faces_.size()) {
        throw std::runtime_error("HPC: cached triangle outside face table");
    }
    if (!in_domain(x, z)) {
        return std::nullopt;
    }
    if (const auto containing = sample(x, z, cached_triangle)) {
        return containing;
    }
    return cached_triangle ? std::optional(sample_triangle(x, z, *cached_triangle)) : std::nullopt;
}

std::optional<HpcPropSample> HpcSurface::prop_sample_retaining_cached(std::int32_t x, std::int32_t z, std::optional<std::uint16_t> cached_triangle) const {
    if (cached_triangle && *cached_triangle >= faces_.size()) {
        throw std::runtime_error("HPC: cached prop triangle outside face table");
    }
    if (!in_domain(x, z)) {
        return std::nullopt;
    }
    if (const auto containing = prop_sample(x, z, cached_triangle)) {
        return containing;
    }
    return cached_triangle ? std::optional(prop_sample_triangle(x, z, *cached_triangle)) : std::nullopt;
}

std::size_t HpcSurface::triangle_count() const {
    return faces_.size();
}
} // namespace toca
