#include "assets/store.hpp"
#include "scene/catalog.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/weather.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace toca::scene;
using Vector = std::array<std::int32_t, 3>;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

Vector position(const HorizonModelFrame& frame, std::uint32_t node) {
    const auto& value = frame.transforms.at(node);
    return {value[12], value[13], value[14]};
}

toca::ModelNode node(std::int16_t parent = -1) {
    toca::ModelNode result{parent, {}, 0, {}};
    result.transform[0] = result.transform[5] = result.transform[10] = result.transform[15] = 1;
    return result;
}

HorizonActorResources moving(std::uint32_t track, std::uint32_t count, bool animated = false) {
    HorizonActorResources result;
    result.setup = resolve_setup({track, 0, 1, 3});
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto root = std::uint32_t(result.moving.nodes.size());
        result.moving_roots.push_back(root);
        result.moving.nodes.push_back(node());
        if (animated) {
            for (auto j = 0; j < 5; ++j) {
                result.moving.nodes.push_back(node(std::int16_t(root)));
            }
        }
    }
    return result;
}

HorizonActorResources rotorcraft(std::uint32_t track) {
    HorizonActorResources result;
    result.setup = resolve_setup({track, 0, 1, 3});
    result.rotorcraft_root = 0;
    result.rotorcraft_child = 1;
    result.rotorcraft.nodes = {node(), node(0), node(1), node(1), node(1), node(1), node(1)};
    return result;
}

void check_camera(const HorizonFrame& frame, const HorizonTransform& anchor, bool rotorcraft) {
    expect(frame.camera_zero.has_value() && frame.camera_zero->rotorcraft == rotorcraft, "Loaded source actor must publish its camera0 kind");
    const auto& camera = *frame.camera_zero;
    const auto pose = weather_camera(camera.world_to_view);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto expected_position = std::int64_t(anchor[12 + axis]) + 10 * std::int64_t(anchor[axis]) + anchor[4 + axis] - 4 * std::int64_t(anchor[8 + axis]);
        expect(camera.anchor_position_q16[axis] == anchor[12 + axis], "Camera0 visibility must retain its actual actor parent position");
        expect(std::nearbyint(double(camera.published_world_position[axis]) * 65536) == camera.position_q16[axis],
            "Actor camera must retain the original composed float translation alongside its unchanged rounded listener");
        expect(std::abs(std::int64_t(camera.position_q16[axis]) - expected_position) <= 4, "Camera0 must compose the original (10,1,-4) local offset with its actual actor root");
        expect(std::abs(std::int64_t(pose.position[axis]) - camera.position_q16[axis]) <= 8, "Actor-camera inverse must recover the same actual audio/weather listener");
    }
}

void presentation() {
    // Sample the real constant-speed route at two render rates. Only update()
    // advances its25Hz source controller; every intervening image must move.
    for (const auto hz : {60, 144}) {
        HorizonActors actors(moving(0, 2));
        const auto initial = position(actors.frame().moving, 0);
        actors.update();
        const auto first = position(actors.frame().moving, 0);
        auto last = initial;
        for (int image = 1; image <= hz; ++image) {
            const auto milliseconds = double(image) * 1000 / hz;
            const auto tick = std::uint64_t(std::floor(milliseconds / 40));
            while (actors.frame().updates <= tick) {
                actors.update();
            }
            const auto authoritative = actors.frame();
            const auto alpha = float((milliseconds - double(tick) * 40) / 40);
            const auto rendered = actors.presentation_frame(alpha);
            const auto current = position(rendered.moving, 0);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto velocity = std::int64_t(first[axis]) - initial[axis];
                const auto expected = double(initial[axis]) + double(velocity) * milliseconds / 40;
                expect(std::abs(double(current[axis]) - expected) <= 2, "Aircraft render positions must advance linearly between25Hz source ticks at60/144Hz");
                expect(std::abs(double(std::int64_t(current[axis]) - last[axis]) - double(velocity) * 25 / hz) <= 2,
                    "Aircraft render displacement must not repeat or jump at source-tick boundaries");
            }
            expect(current != last, "Every60/144Hz aircraft image must move between fixed simulation ticks");
            expect(actors.frame().updates == authoritative.updates && actors.frame().moving.transforms == authoritative.moving.transforms
                       && actors.frame().moving.visible == authoritative.moving.visible && actors.frame().camera_zero->world_to_view == authoritative.camera_zero->world_to_view,
                "Rendering aircraft must not mutate source counters, positions or animation masks");
            expect(rendered.updates == authoritative.updates && rendered.moving_count == authoritative.moving_count,
                "Actor rendering must preserve authoritative controller metadata");
            check_camera(rendered, rendered.moving.transforms[0], false);
            last = current;
        }
    }

    HorizonActors planes(moving(2, 3, true));
    const auto initial = planes.frame();
    planes.update();
    const auto current = planes.frame();
    expect(planes.presentation_frame(0).moving.transforms == initial.moving.transforms && planes.presentation_frame(1).moving.transforms == current.moving.transforms,
        "Presentation endpoints must retain exact original fixed matrices");
    for (const auto alpha : {.25f, .75f}) {
        const auto rendered = planes.presentation_frame(alpha);
        expect(rendered.moving.visible == current.moving.visible && rendered.moving.transforms[1] == current.moving.transforms[1],
            "Propeller sibling selection and unchanged child transforms must retain the current source frame");
    }

    HorizonActors balloon(moving(8, 1));
    balloon.update();
    const auto half_spin = balloon.presentation_frame(.5f).moving.transforms[0];
    expect(std::abs(half_spin[0] - 50) <= 2 && std::abs(half_spin[6] - 50) <= 2 && std::abs(half_spin[2] - 65536) <= 2 && std::abs(half_spin[4] + 65536) <= 2
               && half_spin[9] == -65536,
        "Rotating actors must present the intermediate orientation while preserving source basis scale and handedness");

    HorizonActors heli(rotorcraft(3));
    for (int i = 0; i < 101; ++i) {
        heli.update();
    }
    const auto previous_heli = heli.frame();
    heli.update();
    const auto current_heli = heli.frame();
    const auto half_heli = heli.presentation_frame(.5f);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const auto expected = (std::int64_t(previous_heli.rotorcraft.transforms[0][12 + axis]) + current_heli.rotorcraft.transforms[0][12 + axis]) / 2;
        expect(std::abs(std::int64_t(half_heli.rotorcraft.transforms[0][12 + axis]) - expected) <= 1,
            "Helicopter root motion must present intermediate fixed positions during acceleration");
    }
    expect(half_heli.rotorcraft.visible == current_heli.rotorcraft.visible && half_heli.rotorcraft_position == current_heli.rotorcraft_position
               && half_heli.rotorcraft_speed == current_heli.rotorcraft_speed && half_heli.rotorcraft_state == current_heli.rotorcraft_state,
        "Helicopter render poses must retain current rotor masks and source motion metadata");
    check_camera(half_heli, half_heli.rotorcraft.transforms[0], true);

    HorizonActors repeat(moving(0, 2));
    const auto start = repeat.frame().moving.transforms;
    for (int i = 0; i < 681; ++i) {
        repeat.update();
    }
    expect(repeat.frame().moving.transforms[0] == start[0], "Repeat-route fixture must reach its exact authored start");
    for (const auto alpha : {0.f, .5f, 1.f}) {
        expect(repeat.presentation_frame(alpha).moving.transforms[0] == start[0], "Route repeats must cut directly to start without sweeping across the track");
    }
    repeat.update();
    repeat.reset();
    for (const auto alpha : {0.f, .5f, 1.f}) {
        expect(
            repeat.presentation_frame(alpha).updates == 0 && repeat.presentation_frame(alpha).moving.transforms == start, "Scene reset must discard aircraft presentation history");
    }
    std::cout << "PASS: aircraft60/144Hz intermediate frames, exact source endpoints, discrete animation and route/reset cuts\n";
}

void synthetic() {
    // Source43cf60/e2a0 skip both unused buffers on this track/weather.
    const auto disabled = decode_horizon_actors({}, {}, resolve_setup({9, 0, 1, 3}));
    HorizonActors empty(disabled);
    empty.update();
    expect(empty.frame().updates == 1 && empty.frame().moving_count == 0 && empty.frame().moving.transforms.empty() && empty.frame().rotorcraft.transforms.empty(),
        "Disabled source groups must skip unused geometry and retain explicit tick state");
    expect(!empty.frame().camera_zero, "An absent HOR group must not manufacture a camera0");

    HorizonActors airship(moving(0, 2));
    const auto initial = airship.frame().moving.transforms;
    check_camera(airship.frame(), initial[0], false);
    const auto initial_air_camera = airship.frame().camera_zero->world_to_view;
    expect(position(airship.frame().moving, 0) == Vector{0x3e80000, 0x5c0000, -0x640000}, "Donington first source start position changed");
    airship.update();
    check_camera(airship.frame(), airship.frame().moving.transforms[0], false);
    expect(airship.frame().camera_zero->world_to_view != initial_air_camera, "Camera0 must move with the actual first HOR actor");
    expect(position(airship.frame().moving, 0) == Vector{0x3e503d8, 0x5c21c9, -0x63b7d0}, "Donington source per-tick fixed displacement changed");
    for (auto i = 1; i < 671; ++i) {
        airship.update();
    }
    expect(position(airship.frame().moving, 0) == Vector{-0x3e80000, 0xb40000, 0x5a0000}, "Original distance threshold must snap to authored end position");
    for (auto i = 0; i < 9; ++i) {
        airship.update();
    }
    expect(position(airship.frame().moving, 0) == Vector{-0x3e80000, 0xb40000, 0x5a0000}, "Source wait10 must retain the end until the wait1 reset");
    airship.update();
    expect(position(airship.frame().moving, 0) == Vector{0x3e80000, 0x5c0000, -0x640000}, "Source wait1 must restore start without advancing on that tick");
    airship.reset();
    expect(airship.frame().updates == 0 && airship.frame().moving.transforms == initial, "Scene reset must deterministically rebuild original controller poses");

    HorizonActors balloon(moving(8, 1));
    expect(!balloon.frame().camera_zero, "A Snetterton moving balloon cannot replace the separately loaded helicopter camera0");
    balloon.update();
    expect(position(balloon.frame().moving, 0) == Vector{-0x24b0000, 0x370000, -0x1bff334} && balloon.frame().moving.transforms[0][0] == 100,
        "Snetterton balloon must use its source displacement and one lookup-angle increment");
    for (auto i = 1; i < 17205; ++i) {
        balloon.update();
    }
    expect(balloon.frame().moving_count == 0, "A balloon reaching its path end must remove its controller");
    const auto retained = balloon.frame().moving.transforms;
    for (auto i = 0; i < 20; ++i) {
        balloon.update();
    }
    expect(balloon.frame().moving.transforms == retained, "Controller removal must leave its last displayed actor pose intact");

    HorizonActors planes(moving(2, 3, true));
    expect(planes.frame().moving.visible == std::vector<std::uint8_t>{1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1},
        "Source planes must retain the body plus only the first animated sibling");
    planes.update();
    expect(planes.frame().moving.visible[1] == 0 && planes.frame().moving.visible[2] == 1 && planes.frame().moving.visible[5] == 1,
        "Animated siblings must advance once per simulation tick while retaining the plane body");
    for (auto i = 0; i < 3; ++i) {
        planes.update();
    }
    expect(planes.frame().moving.visible[1] == 1 && planes.frame().moving.visible[2] == 0, "Source four-sibling plane animation must cycle every four ticks");

    struct Flight {
        std::uint32_t track, takeoff_tick, turn_ticks;
        Vector first_corner, first_straight;
    };

    for (const auto flight : {
             Flight{1, 939, 342, {-0xf20000, 0x4b0000, -0x1310000}, {-0x1420000, 0x4b0000, -0xe10000}},
             Flight{3, 469, 171, {-0x1120000, 0x1e0000, -0xb20000}, {-0x13a0000, 0x1e0000, -0x8a0000}},
             Flight{4, 692, 205, {-0x3c0000, 0x260000, 0xfa0000}, {-0xa0000, 0x260000, 0x12c0000}},
             Flight{7, 616, 512, {0x10e0000, 0xa0000, 0x640000}, {0x1720000, 0xa0000, 0}},
             Flight{8, 318, 128, {0x6e0000, 0x1f0000, 0x960000}, {0x8c0000, 0x1f0000, 0xb40000}},
         }) {
        const auto track = flight.track;
        HorizonActors heli(rotorcraft(track));
        const auto initial_position = heli.frame().rotorcraft_position;
        const auto initial_frame = heli.frame().rotorcraft.transforms;
        check_camera(heli.frame(), initial_frame[0], true);
        if (track == 1) {
            const auto camera_pose = weather_camera(heli.frame().camera_zero->world_to_view);
            expect(camera_pose.right[0] > 65000 && std::abs(camera_pose.right[1]) < 20 && std::abs(camera_pose.right[2]) < 20 && camera_pose.forward[1] < -32000
                       && camera_pose.forward[1] > -33500 && camera_pose.forward[2] > 56000,
                "Camera0 must retain source local Y/Z/X rotation order, including its quarter-turn roll");
        }
        for (auto i = 0; i < 100; ++i) {
            heli.update();
        }
        expect(heli.frame().rotorcraft_position == initial_position && heli.frame().rotorcraft_speed == 0 && heli.frame().rotorcraft.visible[2] == 1,
            "Original helicopter must wait100 ticks while its four rotor siblings animate");
        heli.update();
        check_camera(heli.frame(), heli.frame().rotorcraft.transforms[0], true);
        expect(heli.frame().rotorcraft_speed == 0x147 && heli.frame().rotorcraft_position[1] > initial_position[1], "Helicopter must accelerate/climb on tick101");
        if (track == 3) {
            expect(heli.frame().rotorcraft_position == Vector{-9437508, 609535, -11665408}, "Brands Hatch tick101 must retain exact shifted velocity products");
        }
        while (heli.frame().updates + 1 < flight.takeoff_tick) {
            heli.update();
        }
        expect(heli.frame().rotorcraft_state == 0, "Helicopter takeoff must use the original strict phase-distance threshold");
        heli.update();
        expect(heli.frame().updates == flight.takeoff_tick && heli.frame().rotorcraft_state == 1 && heli.frame().rotorcraft_position == flight.first_corner,
            "Helicopter must snap to its authored first corner on the exact source takeoff tick");
        for (std::uint32_t i = 0; i < flight.turn_ticks; ++i) {
            heli.update();
        }
        expect(heli.frame().rotorcraft_state == 2 && heli.frame().rotorcraft_position == flight.first_straight,
            "Original corner speed, angular quantization and90-degree cutoff must reach the authored first straight");
        std::array<bool, 5> visited{true, true, true};
        bool landed = false;
        for (auto i = 0; i < 25000; ++i) {
            const auto before = heli.frame().rotorcraft_state;
            heli.update();
            check_camera(heli.frame(), heli.frame().rotorcraft.transforms[0], true);
            const auto after = heli.frame().rotorcraft_state;
            expect(after < visited.size(), "Original helicopter phase escaped its five-state controller");
            visited[after] = true;
            if (before == 3 && after == 0) {
                landed = true;
                break;
            }
        }
        expect(landed
                   && std::ranges::all_of(visited,
                       [](bool value) {
                           return value;
                       })
                   && heli.frame().rotorcraft_position == initial_position,
            "Every source helicopter route must complete takeoff, turns, approach and landing at its exact start");
        for (auto i = 0; i < 100; ++i) {
            heli.update();
        }
        expect(heli.frame().rotorcraft_position == initial_position && heli.frame().rotorcraft_speed == 0, "Landing must restore the100-tick ground wait");
        heli.reset();
        expect(heli.frame().updates == 0 && heli.frame().rotorcraft.transforms == initial_frame && heli.frame().rotorcraft.visible[2] == 1,
            "Scene reset must restore helicopter child identity, root pose and first rotor sibling");
    }
    std::cout << "PASS: original HOR fixed movement, end/reset/removal, sibling animation and complete helicopter routes\n";
}

void reference(const std::filesystem::path& root) {
    const toca::AssetStore store(root);
    constexpr std::array<std::uint32_t, 10> counts{2, 2, 3, 0, 0, 2, 3, 0, 1, 0};
    for (std::uint32_t track = 0; track < 10; ++track) {
        const auto archive = store.open_archive(std::string(track_content(track).archive_prefix) + "hor.bfp");
        for (const auto weather : {1u, 2u, 3u, 4u, 5u}) {
            const auto setup = resolve_setup({track, 0, weather, 3}, 1);
            const auto resources = decode_horizon_actors(archive.member(0).bytes(), archive.member(1).bytes(), setup);
            const bool dry = setup.weather == 1 || setup.weather == 2;
            const auto expected = track == 0 || track == 5 || dry ? counts[track] : 0;
            const bool heli = dry && (track == 1 || track == 3 || track == 4 || track == 7 || track == 8);
            expect(resources.moving_roots.size() == expected && (resources.rotorcraft_root != UINT32_MAX) == heli,
                "Canonical HOR members must obey original per-track and wet-weather loader gates");
            HorizonActors actors(resources);
            expect(actors.frame().moving_count == expected, "Canonical moving-controller registration changed");
            const bool camera_zero = heli || ((track == 0 || track == 2 || track == 5 || track == 6) && expected != 0);
            expect(actors.frame().camera_zero.has_value() == camera_zero, "Canonical camera0 availability must follow actual source track/weather actor loading");
            if (camera_zero) {
                check_camera(actors.frame(),
                    heli ? actors.frame().rotorcraft.transforms[resources.rotorcraft_root] : actors.frame().moving.transforms[resources.moving_roots.front()], heli);
            }
            if (heli) {
                expect(resources.rotorcraft.nodes.size() == 7 && resources.rotorcraft.nodes[1].parent == 0 && resources.rotorcraft.nodes[2].parent == 1
                           && resources.rotorcraft.meshes[0].name == "HELI_1" && resources.rotorcraft.meshes[4].name == "HELI_5",
                    "Rotorcraft must retain its synthetic parent, imported child hierarchy and original rotor/body geometry");
            }
            for (auto i = 0; i < 121; ++i) {
                actors.update();
            }
            expect(actors.frame().updates == 121 && (!heli || actors.frame().rotorcraft_speed > 0), "Canonical horizon controllers must advance through the actual source path");
            if (track == 0) {
                expect(resources.moving.meshes[0].name == "AIRMARK_1" && resources.moving.meshes[1].name == "PSP1_1", "Donington must retain its original named background models");
            }
            if (track == 2 && dry) {
                expect(resources.moving.nodes.size() == 18 && resources.moving.meshes[0].name == "PLANE1R_1" && resources.moving.meshes[5].name == "PLANE2R_1",
                    "Thruxton must retain all three animated plane actors and both original mesh groups");
            }
        }
    }
    std::cout << "PASS: canonical HOR registration, original actor identities, complete hierarchy and all track/weather gates\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            presentation();
        } else if (argc == 2) {
            reference(argv[1]);
        } else {
            throw std::runtime_error("Expected optional canonical data root");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
