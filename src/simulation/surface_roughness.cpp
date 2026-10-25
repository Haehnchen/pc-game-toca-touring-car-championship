#include "simulation/surface_roughness.hpp"

#include <array>
#include <stdexcept>

namespace toca::simulation {
namespace {
//Original signed Q16 table at004ae988.
constexpr std::array<std::int32_t, 15> offsets{655, -1310, -1966, 655, -1310, 0, -1310, -1966, 655, -1310, 655, 1966, -1310, -655, 1966};
} // namespace

std::int32_t SurfaceRoughness::next() {
    const auto offset = offsets[cursor_];
    if (++cursor_ == offsets.size()) {
        cursor_ = 0;
    }
    return offset;
}

std::uint16_t SurfaceRoughness::current() const {
    return cursor_;
}

void SurfaceRoughness::import_cursor(std::uint32_t cursor) {
    if (cursor >= offsets.size()) {
        throw std::runtime_error("Original roughness cursor is outside its15-entry table");
    }
    cursor_ = std::uint16_t(cursor);
}
} // namespace toca::simulation
