#include "scene/barriers.hpp"

#include "simulation/fixed.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace toca::scene {
namespace {
std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("Barriers: truncated POINTS field");
    }
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}

std::int32_t i32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<std::int32_t>(u32(bytes, offset));
}

std::int32_t whole(std::int32_t value) {
    return value / 65536;
}

std::uint8_t outcode(std::int32_t x, std::int32_t z, std::int32_t left, std::int32_t bottom, std::int32_t right, std::int32_t top) {
    std::uint8_t result = x < left ? 1 : x > right ? 2 : 0;
    if (z < bottom) {
        result |= 4;
    } else if (z > top) {
        result |= 8;
    }
    return result;
}

// 47db40/47dd30: preserve the original single-edge clipping priority and
// fixed division/product sequence, rather than replacing it with float AABBs.
bool touches_sector(const BarrierSegment& segment, const std::array<std::int32_t, 2>& center, std::int32_t half_size) {
    using namespace simulation::fixed;
    const auto extent = add(half_size, 0x28000);
    const auto left = sub(center[0], extent), right = add(center[0], extent), bottom = sub(center[1], extent), top = add(center[1], extent);
    const auto x = segment.first[0], z = segment.first[2];
    const auto first = outcode(x, z, left, bottom, right, top), second = outcode(segment.second[0], segment.second[2], left, bottom, right, top);
    if (first == 0 || second == 0) {
        return true;
    }
    if (first == second || (first & second) != 0) {
        return false;
    }
    const auto dx = sub(segment.second[0], x), dz = sub(segment.second[2], z);
    const auto divide = [](std::int32_t value, std::int32_t divisor) {
        if (divisor == 0) {
            throw std::runtime_error("Barriers: zero sector clipping divisor");
        }
        return bits(std::uint32_t(std::int64_t(value) * one / divisor));
    };
    if (first & 8) {
        const auto clipped = add(x, mul(dx, divide(sub(top, z), dz)));
        return left <= clipped && clipped <= right;
    }
    if (first & 4) {
        const auto clipped = add(x, mul(dx, divide(sub(bottom, z), dz)));
        return left <= clipped && clipped <= right;
    }
    if (first & 2) {
        const auto clipped = add(z, mul(dz, divide(sub(right, x), dx)));
        return bottom <= clipped && clipped <= top;
    }
    if (first & 1) {
        const auto clipped = add(z, mul(dz, divide(sub(left, x), dx)));
        return bottom <= clipped && clipped <= top;
    }
    return false;
}
} // namespace

BarrierData::BarrierData(std::span<const std::uint8_t> bytes, std::span<const std::array<std::int32_t, 2>> centers) {
    using namespace simulation::fixed;
    const auto route_count = u32(bytes, 0);
    const auto count_offset = UINT64_C(4) + std::uint64_t(route_count) * 64;
    if (count_offset > bytes.size()) {
        throw std::runtime_error("Barriers: route count exceeds POINTS bytes");
    }
    const auto count = u32(bytes, std::size_t(count_offset));
    const auto begin = count_offset + 4;
    if (begin > bytes.size() || std::uint64_t(count) * 88 > bytes.size() - begin) {
        throw std::runtime_error("Barriers: segment count exceeds POINTS bytes");
    }
    segments_.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto offset = std::size_t(begin + std::uint64_t(index) * 88);
        BarrierSegment segment;
        segment.source_index = index;
        segment.kind = bytes[offset + 0x55];
        if (segment.kind > 3) {
            throw std::runtime_error("Barriers: unknown original segment kind");
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            segment.first[axis] = i32(bytes, offset + axis * 4);
            segment.second[axis] = i32(bytes, offset + 12 + axis * 4);
        }
        // Preserve the source's whole-unit axis classification, not an epsilon.
        if (whole(segment.first[2]) == whole(segment.second[2])) {
            segment.axis = 3;
            segment.line[0] = segment.first[2];
            segment.orientation = segment.first[0] >= segment.second[0];
            segment.normal = {0, 0, one};
        } else if (whole(segment.first[0]) == whole(segment.second[0])) {
            segment.axis = 2;
            segment.line[0] = segment.first[0];
            segment.orientation = segment.first[2] < segment.second[2];
            segment.normal = {one, 0, 0};
        } else {
            const auto dx = sub(segment.first[0], segment.second[0]), dz = sub(segment.second[2], segment.first[2]);
            if (dz == 0) {
                throw std::runtime_error("Barriers: zero normal divisor");
            }
            segment.axis = 1;
            segment.orientation = segment.first[0] >= segment.second[0];
            const auto divide = [](std::int32_t value, std::int32_t divisor) {
                if (divisor == 0) {
                    throw std::runtime_error("Barriers: zero prepared-line divisor");
                }
                return bits(std::uint32_t(std::int64_t(value) * one / divisor));
            };
            auto& line = segment.line;
            line[0] = divide(dz, neg(dx));
            line[1] = sub(segment.first[2], mul(segment.first[0], line[0]));
            const auto perpendicular = divide(dx, dz);
            line[2] = perpendicular;
            const auto difference = sub(line[0], perpendicular);
            line[4] = divide(perpendicular, difference);
            line[5] = divide(line[1], difference);
            line[6] = divide(line[0], difference);
            line[7] = mul(line[0], line[4]);
            line[8] = sub(sub(segment.first[2], mul(line[0], segment.first[0])), mul(line[0], line[5]));
            line[3] = reciprocal(difference);
            const auto magnitude = sqrt(std::uint32_t(add(mul(perpendicular, perpendicular), one)));
            if (!magnitude) {
                throw std::runtime_error("Barriers: degenerate original normal");
            }
            const auto inverse = reciprocal(magnitude);
            segment.normal = {mul(one, inverse), 0, mul(perpendicular, inverse)};
        }
        if (segment.kind == 3) {
            segment.orientation = !segment.orientation;
        }
        segments_.push_back(segment);
    }
    // 479182 closes each contiguous endpoint-linked run, including a run
    // ending at EOF. A discontinuity closes the preceding group first.
    std::optional<std::uint32_t> head, previous;
    for (auto& segment : segments_) {
        if (segment.kind == 2 || segment.kind == 3) {
            if (head && segments_[*previous].second == segment.first) {
                segments_[*previous].next_index = segment.source_index;
            } else {
                if (head) {
                    segments_[*previous].next_index = *head;
                }
                head = segment.source_index;
            }
            previous = segment.source_index;
        } else if (head) {
            segments_[*previous].next_index = *head;
            head.reset();
        }
    }
    if (head) {
        segments_[*previous].next_index = *head;
    }
    // 47de00: width is the first row with equal Z; the minimum adjacent
    // same-row X spacing supplies the rectangle half size.
    if (centers.size() < 2 || centers.size() > 0x800) {
        throw std::runtime_error("Barriers: original sector grid requires 2..2048 centers");
    }
    width_ = 1;
    while (width_ < centers.size() && centers[width_][1] == centers.front()[1]) {
        ++width_;
    }
    if (centers.size() % width_ != 0) {
        throw std::runtime_error("Barriers: incomplete sector rows");
    }
    height_ = std::uint32_t(centers.size() / width_);
    minimum_x_ = centers.front()[0];
    maximum_z_ = centers.front()[1];
    auto spacing = std::int32_t(0x7fff0000);
    for (std::size_t i = 0; i < centers.size(); ++i) {
        minimum_x_ = std::min(minimum_x_, centers[i][0]);
        maximum_z_ = std::max(maximum_z_, centers[i][1]);
        if (i + 1 < centers.size() && centers[i][1] == centers[i + 1][1]) {
            const auto distance = simulation::fixed::abs(sub(centers[i + 1][0], centers[i][0]));
            if (distance > 0) {
                spacing = std::min(spacing, distance);
            }
        }
    }
    if (spacing != 64 * one) {
        throw std::runtime_error("Barriers: original shift22 lookup requires 64-unit sector spacing");
    }
    half_size_ = spacing / 2;
    for (std::size_t i = 0; i < centers.size(); ++i) {
        // 47de00/47db40 clip against each stored center in file order.
        // Lavaland's rows2..9 retain a one-raw-unit Z offset; do not rebuild
        // these rectangles from the separate 47dab0 arithmetic lookup grid.
        auto& candidates = sector_candidates_.emplace_back();
        for (const auto& segment : segments_) {
            if (touches_sector(segment, centers[i], half_size_)) {
                candidates.push_back(segment.source_index);
            }
        }
    }
}

std::span<const BarrierSegment> BarrierData::segments() const {
    return segments_;
}

std::optional<std::uint32_t> BarrierData::sector(const std::array<std::int32_t, 3>& position) const {
    using namespace simulation::fixed;
    const auto dx = sub(add(half_size_, position[0]), minimum_x_), dz = add(sub(half_size_, position[2]), maximum_z_);
    // 47dab0 uses a 63-RAW-unit signed adjustment before shifting22, not
    // floating-point division or a full fixed-point truncation correction.
    const auto column = add(dx, dx < 0 ? 63 : 0) >> 22, row = add(dz, dz < 0 ? 63 : 0) >> 22;
    if (column < 0 || row < 0 || std::uint32_t(column) >= width_ || std::uint32_t(row) >= height_) {
        return std::nullopt;
    }
    return std::uint32_t(row) * width_ + std::uint32_t(column);
}

std::span<const std::uint32_t> BarrierData::candidates(const std::array<std::int32_t, 3>& position) const {
    const auto index = sector(position);
    return index ? std::span<const std::uint32_t>(sector_candidates_[*index]) : std::span<const std::uint32_t>{};
}

std::size_t BarrierData::sector_count() const {
    return sector_candidates_.size();
}
} // namespace toca::scene
