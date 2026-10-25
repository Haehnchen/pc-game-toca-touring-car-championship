#include "scene/car_visual.hpp"
#include "scene/matrix.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca::scene;
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void producer() {
    CarVisualAngles angles;
    advance_car_visual_angles(angles, 4, 0x640001, -0x320001);
    expect(angles.local_x == -4 && angles.absolute_z == 2, "Source contact torque must use separate opposite-sign4/2 angle steps");
    advance_car_visual_angles(angles, 4, -0x640001, 0x320001);
    expect(angles.local_x == 0 && angles.absolute_z == 0, "Reversing contact moments must reverse both visual feedback steps");
    advance_car_visual_angles(angles, 4, 0x640000, -0x320000);
    expect(angles.local_x == 0 && angles.absolute_z == 0, "Exact source torque thresholds must remain in the neutral branch");
    angles = {12, -12};
    advance_car_visual_angles(angles, 1, 0, 0);
    expect(angles.local_x == 8 && angles.absolute_z == -10, "Interior angles must recenter by the original per-axis steps");
    angles = {20, -20};
    advance_car_visual_angles(angles, 1, 0, 0);
    expect(angles.local_x == 20 && angles.absolute_z == -20, "Source neutral branch must retain saturated endpoints");
    angles = {};
    for (int tick = 0; tick < 20; ++tick) {
        advance_car_visual_angles(angles, 4, 0x640001, 0x320001);
    }
    expect(angles.local_x == -20 && angles.absolute_z == -20, "Continuous contact moments must stop at source-minus20 endpoints");
    advance_car_visual_angles(angles, 0, 0x640001, 0x320001);
    expect(angles.local_x == 0 && angles.absolute_z == 0, "No active support contacts must clear both visual angles immediately");
}

void transforms() {
    auto authored = identity;
    authored[0] = 3;
    authored[5] = 2;
    authored[12] = 7;
    authored[13] = 8;
    authored[14] = 9;
    const auto reset = car_body_visual_transform(authored, {});
    expect(reset[0] == 1 && reset[5] == 1 && reset[10] == 1 && reset[12] == 7 && reset[13] == 8 && reset[14] == 9,
        "4807f0 replaces node300 basis even at zero angles while preserving its authored pivot");
    const auto z = car_body_visual_transform(identity, {0, 1024});
    expect(z[0] == 0 && z[1] == 1 && z[4] == -1 && z[5] == 0 && z[10] == 1, "Absolute positive Z must use the original table orientation");
    const auto x = car_body_visual_transform(identity, {1024, 0});
    expect(x[0] == 1 && x[5] == 0 && x[6] == 1 && x[9] == -1 && x[10] == 0, "Positive local X must rotate Y/Z without rotating X");
    const auto combined = car_body_visual_transform(identity, {1024, 1024});
    expect(combined[0] == 0 && combined[1] == 1 && combined[6] == 1 && combined[8] == 1 && combined[9] == 0,
        "Body feedback must apply absolute Z before rotating about the resulting local X");
    // Original table values for dynamics+278=8,+27a=0 (4807f0/47fe20).
    const auto original = car_body_visual_transform(identity, {8, 0});
    expect(original[5] == 65531.f / 65536.f && original[6] == 804.f / 65536.f && original[9] == -804.f / 65536.f,
        "Small original body tilt must retain the independently truncated sine/cosine table values");
}

void source_rotation_stores() {
    // Original TT4a64d6db: steering=-51, wheel angles=-6812/-3064.
    // 480750 yaw and47fe20's individual float spills produce these local
    // coefficients before47f500 composes any parent/world matrix.
    auto yaw = identity;
    yaw[0] = yaw[10] = 65335.f / 65536.f;
    yaw[8] = -5121.f / 65536.f;
    yaw[2] = -yaw[8];
    yaw[12] = 7;
    yaw[13] = 8;
    yaw[14] = 9;
    constexpr std::array<std::array<std::int32_t, 2>, 2> spin{{{-56004, -34036}, {-65531, -804}}};
    constexpr std::array<std::array<std::uint32_t, 9>, 2> expected{{{0xbf048b9c, 0x3f5ac2f3, 0xbd26394f, 0xbf5a183c, 0xbf04f400, 0xbd88c156, 0xbda00800, 0, 0x3f7f3700},
        {0xbc48622f, 0x3f7ff9c6, 0xba7b4c85, 0xbf7f3204, 0xbc490000, 0xbda004e0, 0xbda00800, 0, 0x3f7f3700}}};
    for (std::size_t wheel = 0; wheel < spin.size(); ++wheel) {
        const auto rotated = matrix::source_axis_rotation(yaw, 2, float(spin[wheel][0]) / 65536.f, float(spin[wheel][1]) / 65536.f);
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                expect(std::bit_cast<std::uint32_t>(rotated[row * 4 + column]) == expected[wheel][row * 3 + column],
                    "Frontwheel local rotation must match original47fe20 binary32 stores exactly");
            }
        }
        for (const auto index : {3u, 7u, 11u, 12u, 13u, 14u, 15u}) {
            expect(std::bit_cast<std::uint32_t>(rotated[index]) == std::bit_cast<std::uint32_t>(yaw[index]),
                "Source local rotation must preserve the authored pivot and homogeneous coefficients");
        }
    }
    // Original NoQual87946708 actor14: localX=8, absoluteZ=2. This
    // exercises the same47fe20 stores around a nonunit local X row.
    const auto body = car_body_visual_transform(identity, {8, 2});
    constexpr std::array<std::uint32_t, 9> body_expected{0x3f7fff00, 0x3b490000, 0, 0xbb48fc13, 0x3f7ffa00, 0x3c48feea, 0x381dd100, 0xbc48ff37, 0x3f7ffb00};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            expect(std::bit_cast<std::uint32_t>(body[row * 4 + column]) == body_expected[row * 3 + column],
                "Body local rotation must share original47fe20 binary32 spills and53-bit row sums");
        }
    }
}
} // namespace

int main() {
    try {
        producer();
        transforms();
        source_rotation_stores();
        std::cout << "PASS: source contact-torque body feedback and absolute-Z/local-X node300 transforms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
