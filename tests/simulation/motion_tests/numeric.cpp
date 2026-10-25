#include "fixtures.hpp"
#include "simulation/source_transform.hpp"

namespace toca::test::motion {
using namespace toca::simulation;

void sine_values() {
    // CRC32 of all original scalar results, encoded as little-endian int32.
    std::uint32_t crc = UINT32_MAX;
    for (std::uint32_t angle = 0; angle < 4096; ++angle) {
        const auto value = fixed::sine(std::int32_t(angle));
        for (const std::uint32_t upper : {0u, 0x12345000u, 0x7ffff000u, 0x80000000u, 0xfffff000u}) {
            expect(fixed::sine(std::bit_cast<std::int32_t>(upper | angle)) == value, "Original sine must wrap every signed angle, including INT_MIN and INT_MAX, to 12 bits");
        }
        auto word = std::uint32_t(value);
        for (unsigned byte = 0; byte < 4; ++byte) {
            crc ^= word & 255;
            word >>= 8;
            for (unsigned bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
            }
        }
    }
    expect(~crc == 0x4bb480fdu, "All 4096 original sine values and their integer truncation must remain exact");
}

void fixed_arithmetic() {
    expect(fixed::mul(-1, 32768) == -1 && fixed::add(INT32_MAX, 1) == INT32_MIN, "Original fixed multiplication must shift before truncating and addition must wrap");
    expect(fixed::atan_ratio(unit, unit) == 0, "Original atan equality must retain the adjacent-table spill, not a native clamp");
    for (const auto [numerator, expected] : std::array<std::array<std::int32_t, 2>, 6>{{{1, 0}, {3, 2}, {5, 2}, {-1, 0}, {-3, -2}, {-5, -2}}}) {
        expect(source_transform::dot({numerator, 0, 0}, {unit / 2, 0, 0}) == expected, "47b150/47b250 must round positive and negative half-integer ties to even");
    }
    expect(source_transform::dot({1, 1, 0}, {unit / 2, unit / 2, 0}) == 1, "The source transform must add unrounded products before its single integer conversion");
    expect(source_transform::dot({INT32_MAX, INT32_MAX, 0}, {unit, unit, 0}) == INT32_MIN && source_transform::dot({INT32_MIN, INT32_MIN, 0}, {unit, unit, 0}) == INT32_MIN,
        "An overflowing source fistp must produce integer-indefinite instead of wrapping");
    auto basis = fixed::Basis{{{unit, 0, 0}, {0, unit, 0}, {0, 0, unit}}};
    fixed::rotate(basis, {0, 1024, 0});
    expect(basis[0][2] < -65000 && basis[2][0] > 65000, "Original local-axis rotation order/sign or lookup renormalization changed");
}

} // namespace toca::test::motion
