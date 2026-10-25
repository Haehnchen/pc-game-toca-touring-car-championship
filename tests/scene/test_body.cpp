#include "assets/store.hpp"
#include "scene/body.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::int32_t unit = 65536;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, message);
}

void put16(Bytes& bytes, std::size_t position, std::uint16_t value) {
    bytes.at(position) = std::uint8_t(value);
    bytes.at(position + 1) = std::uint8_t(value >> 8);
}

void put32(Bytes& bytes, std::size_t position, std::int32_t value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    put16(bytes, position, std::uint16_t(bits));
    put16(bytes, position + 2, std::uint16_t(bits >> 16));
}

Bytes plane(bool sloped) {
    Bytes bytes(84);
    put32(bytes, 0, -128 * unit);
    put32(bytes, 4, -128 * unit);
    put16(bytes, 8, 1);
    put16(bytes, 20, 1);
    put16(bytes, 24, 3);
    const std::array<std::array<std::int32_t, 2>, 3> xz{{{-16 * unit, -16 * unit}, {-16 * unit, 16 * unit}, {16 * unit, -16 * unit}}};
    for (std::size_t vertex = 0; vertex < xz.size(); ++vertex) {
        put32(bytes, 28 + vertex * 12, xz[vertex][0]);
        put32(bytes, 32 + vertex * 12, 2 * unit + (sloped ? xz[vertex][0] / 16 + xz[vertex][1] / 32 : 0));
        put32(bytes, 36 + vertex * 12, xz[vertex][1]);
    }
    put16(bytes, 64, 1);
    put16(bytes, 70, 1);
    put16(bytes, 72, 2);
    put16(bytes, 76, 1);
    put16(bytes, 80, 1);
    put16(bytes, 82, 1);
    return bytes;
}

std::array<float, 3> transform(const std::array<float, 16>& matrix, const std::array<float, 3>& point) {
    std::array<float, 3> result{};
    for (std::size_t component = 0; component < 3; ++component) {
        result[component] = point[0] * matrix[component] + point[1] * matrix[4 + component] + point[2] * matrix[8 + component] + matrix[12 + component];
    }
    return result;
}

void supported(const toca::scene::BodyPose& body) {
    bool touching = false;
    for (std::size_t index = 0; index < body.contacts.size(); ++index) {
        const auto difference = std::int64_t(body.contacts[index][1]) - body.surfaces[index].height;
        expect(difference >= 0, "Initial body correction must leave every queried contact at or above its surface");
        touching |= difference == 0;
    }
    expect(touching, "The initial body must retain at least one actual supporting contact");
    for (std::size_t component = 0; component < 3; ++component) {
        expect(body.object_to_world[12 + component] == float(body.position[component]) / unit, "Render translation must be the final contact-corrected body origin");
    }
    for (const auto value : body.object_to_world) {
        expect(std::isfinite(value), "Initial body render matrix must remain finite");
    }
}

void synthetic() {
    using namespace toca::scene;
    const toca::HpcSurface flat(plane(false)), slope(plane(true));
    const HorizontalPlacement start{{-3 * unit, -3 * unit}, 0, 0};
    const auto body = initial_body_pose(flat, start, 0);
    expect(body.position == std::array<std::int32_t, 3>{-3 * unit, 172351, -3 * unit}, "Flat Honda body origin must include source half-height and lookup rounding");
    expect(body.basis == std::array<std::array<std::int32_t, 3>, 3>{{{65522, 0, 0}, {0, 65524, 0}, {0, 0, 65538}}},
        "Initial body basis must retain original non-unit lookup normalization");
    supported(body);
    const auto tilted = initial_body_pose(slope, start, 0);
    expect(tilted.position[0] == start.xz[0] && tilted.position[2] == start.xz[1] && tilted.basis[1][0] < 0 && tilted.basis[1][1] > 65000 && tilted.basis[1][2] < 0,
        "Surface contacts must tilt the body while preserving its horizontal start");
    supported(tilted);
    for (std::uint32_t car = 0; car < 10; ++car) {
        supported(initial_body_pose(slope, {{-3 * unit, -3 * unit}, -376, 0}, car));
    }
    expect(car_half_dimensions(8) == std::array<std::int32_t, 3>{0x21439, 0x1176c, 0x11e14} && car_half_dimensions(9) == std::array<std::int32_t, 3>{0x2e353, 0xc1eb, 0x12a9f},
        "Bonus cars must use their separate original dimensions");
    rejects(
        [&] {
            (void)initial_body_pose(flat, {{100 * unit, 100 * unit}, 0, 0}, 0);
        },
        "An off-surface car must not invent its body height");

    const auto camera = initial_follow_world_to_view(body, 0);
    const std::array<float, 3> origin{float(body.position[0]) / unit, float(body.position[1]) / unit, float(body.position[2]) / unit};
    const auto camera_origin = transform(camera, {origin[0] - float(0x5668e) / unit, origin[1] + float(0x1800f) / unit, origin[2]});
    expect(std::abs(camera_origin[0]) < .0001f && std::abs(camera_origin[1]) < .0001f && std::abs(camera_origin[2]) < .0001f,
        "Follow view must invert the original View2 camera-node position");
    const auto target = transform(camera, origin);
    expect(std::abs(target[0]) < .0001f && target[1] > -.7f && target[1] < -.4f && target[2] > 5.5f && target[2] < 5.7f,
        "Camera2's source pitch must place the body below center and in front of the camera");
    auto other_basis = body;
    other_basis.basis = tilted.basis;
    other_basis.object_to_world = tilted.object_to_world;
    expect(initial_follow_world_to_view(other_basis, 0) == camera, "The separate initial follow anchor must not inherit body tilt");
    expect(initial_follow_world_to_view(body, 1024) != camera, "Explicit initial anchor heading must affect the view");
    for (const auto car : {0u, 7u, 8u, 9u}) {
        for (std::uint16_t view = 2; view <= 5; ++view) {
            for (const bool micro : {false, true}) {
                expect(race_world_to_view(tilted, 512, view, car, micro, 4) == race_world_to_view(tilted, 512, view, car, micro, 3),
                    "Mega must preserve Very High body-relative chase, bonnet, rear and micro camera transforms");
            }
        }
    }
    rejects(
        [&] {
            (void)race_world_to_view(body, 0, 2, 7, false, 5);
        },
        "Race cameras must reject detail above Mega");
}

void canonical(const std::filesystem::path& root) {
    using namespace toca::scene;
    const toca::AssetStore store(root);
    for (const auto path : {"DATA/DONLKEEP.BFP", "DATA/SILVKEEP.BFP", "DATA/THRUKEEP.BFP", "DATA/BRANKEEP.BFP", "DATA/OULTKEEP.BFP", "DATA/DONSKEEP.BFP", "DATA/CROFKEEP.BFP",
             "DATA/KNOCKEEP.BFP", "DATA/SNETKEEP.BFP", "DATA/LANDKEEP.BFP"}) {
        const auto keep = store.open_archive(path);
        const toca::HpcSurface surface(keep.member(0).bytes());
        const auto grid = decode_start_grid(keep.member(3).bytes());
        const auto qualifying = single_qualifying_start(keep.member(1).bytes());
        const auto race = single_race_start(grid);
        for (std::uint32_t car = 0; car < 8; ++car) {
            for (const auto start : {qualifying, race}) {
                const auto body = initial_body_pose(surface, start, car);
                supported(body);
                expect(body.position[0] == start.xz[0] && body.position[2] == start.xz[1] && body.basis[1][1] > 0,
                    "Track starts must preserve source XZ and obtain a finite supported body pose");
                const auto heading = start.source_index == 15 ? grid[15].heading : grid[car * 2].heading;
                const auto view = initial_follow_world_to_view(body, heading);
                const auto target = transform(view, {float(body.position[0]) / unit, float(body.position[1]) / unit, float(body.position[2]) / unit});
                expect(std::abs(target[0]) < .001f && target[2] > 5.5f && target[2] < 5.7f, "Canonical follow-node inverse must retain its original body-relative target");
                if (car == 0) {
                    std::cout << path << " Honda " << (start.source_index == 15 ? "race" : "qualifying") << " position " << body.position[0] << ' ' << body.position[1] << ' '
                              << body.position[2] << '\n';
                }
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else {
            throw std::runtime_error("Usage: scene-body-tests [canonical-root]");
        }
        std::cout << "PASS: source contact body pose, fixed normalization and initial follow-camera node\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
