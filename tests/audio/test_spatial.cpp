#include "audio/spatial.hpp"

#include <iostream>
#include <stdexcept>

namespace {
using namespace toca::audio;
constexpr std::int32_t one = 65536;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void pan_curve() {
    expect(source_pan(0) == 68 && source_pan(1024) == 122 && source_pan(2048) == 68 && source_pan(3072) == 5,
        "424ac0 must preserve original asymmetric front/back centres and side gains");
    expect(source_pan(2049) == 59 && source_pan(4095) == 59, "The original second-half pan centre must remain59 rather than a symmetric native interpolation");
    for (std::uint16_t angle = 0; angle < 4096; ++angle) {
        expect(source_pan(angle) <= 127, "Every source12-bit relative angle must produce a valid original pan");
    }
}

void distance_and_direction() {
    SpatialListener listener;
    const auto centre = source_spatial({}, listener);
    expect(centre.in_range && centre.distance_q16 == 0 && centre.fade_q16 == 100 * one && centre.angle == 0 && centre.pan == 68,
        "Coincident stereo source must retain424070's early angle-zero result and424ac0's pan68");
    listener.stereo = false;
    expect(source_spatial({10 * one, 0, -8 * one}, listener).pan == 64, "Mono must bypass the original angle-pan curve and retain exact centre64");
    listener.stereo = true;
    const auto positive = source_spatial({one / 2, 100 * one, 0}, listener);
    const auto negative = source_spatial({-one / 2, -100 * one, 0}, listener);
    expect(positive.distance_q16 == 0 && negative.distance_q16 == one, "423fb0 must arithmetic-shift signed XZ before distance, independently of source height");
    expect(positive.angle != negative.angle && positive.pan == 68 && negative.pan == 68, "Sub-unit source direction must remain unquantized even when distance quantizes to zero");
    const auto side_a = source_spatial({0, 0, -one}, listener);
    const auto side_b = source_spatial({0, 0, one}, listener);
    expect(side_a.angle == 1024 && side_a.pan == 122 && side_b.angle == 3071 && side_b.pan == 5, "Original positive-Z quadrant uses4095-angle and retains its one-angle asymmetry");
    const auto edge = source_spatial({40 * one, 0, 0}, listener);
    const auto far = source_spatial({41 * one, 0, 0}, listener);
    expect(edge.in_range && edge.distance_q16 == 40 * one && edge.fade_q16 == 1600 && !far.in_range && far.fade_q16 == 0,
        "424070 must accept the40-unit distance boundary and424c70 must retain fractional fade before gain");
    expect(source_spatial({40 * one, 0, one}, listener).in_range, "Truncated horizontal square root must retain source diagonal points whose integer distance is40");
    listener.position_q16 = {17 * one, -30 * one, 9 * one};
    expect(source_spatial({17 * one, 90 * one, 8 * one}, listener).angle == side_a.angle, "Translation and vertical listener position must not alter the horizontal source angle");
    listener.position_q16 = {};
    listener.inverse_heading = 1024;
    expect(source_spatial({0, 0, -one}, listener).angle == 0 && source_spatial({0, 0, -one}, listener).pan == 68,
        "Source angle must subtract the actual camera inverse heading before the original pan curve");
    listener.inverse_heading = -1;
    expect(source_spatial({0, 0, -one}, listener).angle == side_a.angle, "Negative signed listener heading must clamp to zero");
    listener.inverse_heading = 4096;
    expect(source_spatial({0, 0, -one}, listener).angle == 1025, "Out-of-range signed listener heading must clamp to4095");
}

void crc_word(std::uint32_t& crc, std::uint32_t word) {
    // IEEE/zlib CRC32 over explicit little-endian32-bit words.
    for (unsigned byte = 0; byte < 4; ++byte) {
        crc ^= word & 0xff;
        word >>= 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320u : 0u);
        }
    }
}

void source_reference_corpus() {
    // Goldens from independently extracted424070/424ac0/424c70/426190,
    // with478b20's initialized tables and423fb0's recovered x87 distance.
    std::uint32_t pan_crc = 0xffffffff;
    for (std::uint16_t angle = 0; angle < 4096; ++angle) {
        crc_word(pan_crc, source_pan(angle));
    }
    expect(~pan_crc == 0x8b790799, "All4096 source pan angles must retain the independently extracted424ac0 golden");

    auto draw = [state = std::uint32_t(0x424070)]() mutable {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    constexpr std::array<std::int16_t, 8> headings{-1, 0, 511, 1024, 2048, 3072, 4095, 4096};
    std::uint32_t crc = 0xffffffff;
    for (unsigned index = 0; index < 1024; ++index) {
        SpatialVector delta{}, camera_axis{};
        for (auto& component : delta) {
            component = std::int32_t(draw() % (82 * one)) - 41 * one;
        }
        for (auto& component : camera_axis) {
            component = std::int32_t(draw() % (2 * one + 1)) - one;
        }
        const SpatialListener listener{{-7 * one, 3 * one, 11 * one}, headings[index % headings.size()], index % 7 != 0};
        SpatialVector source{};
        for (unsigned axis = 0; axis < 3; ++axis) {
            source[axis] = listener.position_q16[axis] + delta[axis];
        }
        const auto result = source_spatial(source, listener);
        crc_word(crc, std::uint32_t(result.distance_q16));
        crc_word(crc, std::uint32_t(result.fade_q16));
        crc_word(crc, result.angle);
        crc_word(crc, result.pan);
        crc_word(crc, result.in_range ? 1 : 0);
        crc_word(crc, std::uint32_t(source_listener_heading(camera_axis)));
    }
    expect(~crc == 0x132d051a, "1024 deterministic camera/source spatial records must match the independent original-function golden");
}

void camera_heading() {
    expect(source_listener_heading({0, 0, 0}) == 3072 && source_listener_heading({0, 0, one}) == 3072 && source_listener_heading({0, 0, -one}) == 1024,
        "426190 must retain its zero-axis result and signed-Z camera heading quadrants");
    expect(
        source_listener_heading({one, 100 * one, one}) == source_listener_heading({one, -100 * one, one}), "Camera heading must normalize only the horizontal inverse-camera axis");
}
} // namespace

int main() {
    try {
        pan_curve();
        distance_and_direction();
        camera_heading();
        source_reference_corpus();
        std::cout << "PASS: original source distance, camera-relative angle and stereo pan\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
