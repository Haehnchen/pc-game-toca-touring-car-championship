#include "assets/store.hpp"
#include "scene/cockpit.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

float basis_length(const std::array<float, 16>& matrix) {
    return std::sqrt(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2]);
}

void synthetic() {
    using namespace toca::scene;
    expect(cockpit_archive(0) == "data/honddasx.bfp" && cockpit_archive(9) == "data/flexdasx.bfp" && cockpit_archive(8).empty() && cockpit_archive(10).empty(),
        "42de80 cockpit archive selection changed");
    expect(cockpit_left_hand_drive(1) && cockpit_left_hand_drive(2) && cockpit_left_hand_drive(4) && cockpit_left_hand_drive(7) && !cockpit_left_hand_drive(0)
               && !cockpit_left_hand_drive(9),
        "42c0e0 driver-side table changed");
    expect(!local_car_visible(1) && local_car_visible(0) && local_car_visible(2) && local_car_visible(3) && local_car_visible(4) && local_car_visible(5),
        "4271d0 local body mask must retain bonnet hood and rear-view car");
    Cockpit dry(0, 1);
    dry.update(0);
    expect(dry.wheel_angle() == 5 && dry.wiper_angle() == 0x71, "Dry cockpit tick must halve steering error and park the wiper");
    const auto initial = dry.frame(identity, false, false);
    expect(initial.transforms[3][8] == 46340.f / 65536.f && initial.transforms[3][9] == 46340.f / 65536.f && initial.transforms[3][10] == 0,
        "42e440 wiper basis must retain its local X normal through the separate 512-angle parent");
    expect(std::ranges::all_of(initial.visible,
               [](bool value) {
                   return value;
               }),
        "Normal interior must include the original gloves/dashboard/wiper/wheel");
    expect(initial.transforms[2][5] == 0 && initial.transforms[2][6] == 1 && initial.transforms[2][9] == -1 && initial.transforms[2][12] == -float(0x1999) / 65536.f,
        "42de80 dashboard must use its source X quarter turn and translation");
    expect(initial.transforms[4][12] == float(0xcccc) / 65536.f && initial.transforms[4][13] == -float(0xccc) / 65536.f && initial.transforms[4][14] == -float(0x6666) / 65536.f,
        "42e1d0 wheel pivot must retain its driver-side position");
    const auto chun = dry.frame(identity, true, false);
    expect(chun.visible == std::array<bool, 5>{true, true, false, false, true}, "CMCHUN must retain gloves/wheel while hiding dashboard/wiper");
    const auto handy = dry.frame(identity, false, true);
    expect(std::abs(basis_length(handy.transforms[0]) / basis_length(initial.transforms[0]) - float(0x1b333) / 65536.f) < .0001f && handy.transforms[4] == initial.transforms[4]
               && handy.transforms[2] == initial.transforms[2],
        "CMHANDY must apply its original extra glove scale while retaining dashboard/wheel");
    dry.update(398);
    expect(dry.wheel_angle() == 514, "42c220 steering ratio must map road-wheel angle to original smoothed cockpit angle");
    auto body = identity;
    body[12] = 20;
    body[13] = -3;
    body[14] = 17;
    const auto translated = dry.frame(body, false, false), local = dry.frame(identity, false, false);
    for (std::size_t index = 0; index < translated.transforms.size(); ++index) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            expect(std::abs(translated.transforms[index][12 + axis] - local.transforms[index][12 + axis] - body[12 + axis]) < .00001f,
                "Every cockpit resource must follow the selected body transform");
        }
    }
    CockpitSnapshot published;
    published.car_id = 0;
    for (std::size_t role = 0; role < published.roles.size(); ++role) {
        published.roles[role] = {1, 1, local.transforms[role], identity};
    }
    expect(dry.frame(body, published).transforms == translated.transforms, "Published cockpit locals must use the same canonical roles and native body attachment");
    published.roles[0].actual_mask = 0;
    const auto hidden_published = dry.frame(body, published);
    expect(!hidden_published.visible[0] && hidden_published.visible[1], "Published cockpit visibility must retain each role's actual viewport mask");
    {
        // Actual47f500 role3 world: the separate wiper parent is stored before
        // attaching its child. Inputs are Q16; expected words are source stores.
        const auto matrix_q16 = [](std::array<std::int32_t, 16> fixed) {
            std::array<float, 16> result;
            for (std::size_t i = 0; i < result.size(); ++i) {
                result[i] = float(fixed[i]) / 65536.f;
            }
            return result;
        };
        const auto posed_body = matrix_q16({-64558, -149, 11301, 0, -644, 65474, -2819, 0, -11281, -2885, -64482, 0, -9642299, 376764, -5298024, 65536});
        CockpitSnapshot wiper_snapshot;
        wiper_snapshot.car_id = 7;
        wiper_snapshot.roles[3] = {1, 1, matrix_q16({0, -11303, 64553, 0, 0, 64553, 11303, 0, 65536, 0, 0, 0, 0, 0, 0, 65536}),
            matrix_q16({46340, 46340, 0, 0, -46340, 46340, 0, 0, 0, 0, 65536, 0, 78643, 19660, 0, 65536})};
        constexpr std::array<std::uint32_t, 16> world{0xbe93b480u, 0xbe297267u, 0xbf7160d8u, 0, 0x3f264998u, 0x3f3097f2u, 0xbea3b735u, 0, 0xbf3417d5u, 0x3f346eceu, 0x3dbb6c74u, 0,
            0xc314509au, 0x40c17853u, 0xc2a14b79u, 0x3f800000u};
        const Cockpit wiper(7, 1);
        const auto frozen_wiper = wiper.frame(posed_body, wiper_snapshot).transforms[3], runtime_wiper = wiper.frame(posed_body, false, false).transforms[3];
        for (std::size_t i = 0; i < world.size(); ++i) {
            expect(std::bit_cast<std::uint32_t>(frozen_wiper[i]) == world[i] && std::bit_cast<std::uint32_t>(runtime_wiper[i]) == world[i],
                "Frozen and runtime wipers must preserve47f500's parent-world binary32 store before composing the child");
        }
    }
    Cockpit rain(0, 3), snow(0, 5);
    rain.update(0);
    snow.update(0);
    expect(rain.wiper_angle() == 0x71 + 0x2d && snow.wiper_angle() == 0x71 + 0x22, "42e440 wet weather must retain distinct rain/snow sweep speeds");
    bool top = false, returned = false;
    for (int tick = 0; tick < 80; ++tick) {
        rain.update(0);
        top |= rain.wiper_angle() == 0x800;
        returned |= top && rain.wiper_angle() == 0x71;
    }
    expect(top && returned, "42db50 rain wiper must reach both original sweep limits");
    for (const auto id : {8u, 10u}) {
        const auto absent = Cockpit(id, 1).frame(identity, false, false);
        expect(std::ranges::none_of(absent.visible,
                   [](bool value) {
                       return value;
                   }),
            "Original absent interiors must not synthesize another car's cockpit");
    }
    std::cout << "PASS: original cockpit masks, driver sides, local transforms, steering, CMCHUN/CMHANDY and weather wipers\n";
}

std::array<std::int32_t, 3> hand_position(const toca::scene::CockpitFrame& frame, std::size_t hand) {
    const auto& pose = frame.transforms[hand];
    return {std::int32_t(std::lround(pose[12] * 65536)), std::int32_t(std::lround(pose[13] * 65536)), std::int32_t(std::lround(pose[14] * 65536))};
}

void shift_states() {
    using namespace toca::scene;
    constexpr std::array<std::int32_t, 3> lever{0x6666, -0x6666, 0};
    for (const auto car : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
        Cockpit state(car, 1);
        state.update(0);
        const auto shift_hand = cockpit_left_hand_drive(car) ? 1u : 0u;
        const auto wheel_hand = 1u - shift_hand;
        const auto before = state.frame(identity, false, false);
        state.shift();
        expect(state.shift_phase() == 1 && state.frame(identity, false, false).transforms == before.transforms,
            "418cd0 commit must begin reaching without inventing a presentation-frame step");
        // Rendering and source ticks spent in other views do not call update.
        for (unsigned frame = 0; frame < 20; ++frame) {
            expect(
                state.frame(identity, false, false).transforms == before.transforms && state.shift_phase() == 1, "A pending shift must remain frozen outside active cockpit ticks");
        }
        unsigned reach_ticks = 0;
        while (state.shift_phase() == 1 && reach_ticks < 30) {
            state.update(0);
            const auto pose = state.frame(identity, false, false);
            for (std::size_t index = 0; index < 12; ++index) {
                expect(pose.transforms[shift_hand][index] == before.transforms[shift_hand][index], "Reach must preserve the last wheel-grip basis");
            }
            ++reach_ticks;
        }
        expect(state.shift_phase() == 3 && reach_ticks > 1 && reach_ticks < 30 && hand_position(state.frame(identity, false, false), shift_hand) == lever,
            "Original reach must snap to the authored lever and enter hold3");
        const auto held = state.frame(identity, false, false);
        state.shift(); // A second committed shift at the lever restarts hold5.
        for (unsigned tick = 0; tick < 5; ++tick) {
            state.update(180);
            expect(state.shift_phase() == 3 && state.frame(identity, false, false).transforms[shift_hand] == held.transforms[shift_hand],
                "Held hand must retain its exact transform during the original five-tick countdown");
        }
        expect(state.frame(identity, false, false).transforms[wheel_hand] != held.transforms[wheel_hand], "The other hand must continue steering during a gear change");
        state.update(180);
        expect(state.shift_phase() == 2 && hand_position(state.frame(identity, false, false), shift_hand) == lever,
            "42c280 hold expiry must enter return2 without an extra translation step");
        state.update(180);
        expect(state.shift_phase() == 2 && hand_position(state.frame(identity, false, false), shift_hand) != lever, "Returning hand must move toward the current steering grip");
        unsigned return_ticks = 0;
        while (state.shift_phase() == 2 && return_ticks < 30) {
            state.update(180);
            ++return_ticks;
        }
        expect(state.shift_phase() == 0 && return_ticks < 30, "Original returning hand must regrip and restore wheel state0");
    }
    Cockpit outside(0, 1);
    outside.shift();
    for (unsigned tick = 0; tick < 5; ++tick) {
        outside.update(0);
    }
    expect(outside.shift_phase() == 3 && hand_position(outside.frame(identity, false, false), 0) == lever,
        "A commit before the first cockpit tick must use42e1d0's original zero cached hand position");
    Cockpit flex(9, 1), tank(8, 1);
    const auto parked = flex.frame(identity, false, true).transforms[0];
    expect(
        hand_position(flex.frame(identity, false, false), 0) == lever && basis_length(parked) == 1, "42e1d0 must park Flex's left glove at the lever with its authored unit basis");
    for (unsigned tick = 0; tick < 20; ++tick) {
        flex.shift();
        tank.shift();
        flex.update(200);
        expect(flex.shift_phase() == 0 && tank.shift_phase() == 0 && flex.frame(identity, false, true).transforms[0] == parked,
            "Tank/Flex must bypass42d850, and Flex's lever hand must remain fixed even with CMHANDY");
    }
    std::cout << "PASS: all touring-car shift hands, reach/hold/return states, repeated commits, steering during shifts and original Flex lever pose\n";
}

void presentation() {
    using namespace toca::scene;
    for (const auto car : {0u, 1u}) {
        Cockpit state(car, 3);
        for (int tick = 0; tick < 8; ++tick) {
            state.update(0);
        }
        const auto previous = state.frame(identity, false, true);
        state.update(180);
        const auto current = state.frame(identity, false, true);
        const auto half = state.frame(identity, false, true, .5f);
        expect(state.frame(identity, false, true, 0).transforms == previous.transforms && state.frame(identity, false, true, 1).transforms == current.transforms,
            "Cockpit interpolation endpoints must retain the exact original local poses");
        for (const auto hand : {0u, 1u}) {
            const auto first = hand_position(previous, hand), last = hand_position(current, hand), middle = hand_position(half, hand);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                expect(std::abs(2 * middle[axis] - first[axis] - last[axis]) <= 2, "Cockpit hands must interpolate their local position before body attachment");
            }
            expect(std::abs(basis_length(half.transforms[hand]) - 2.55f) < .001f, "Cockpit hand interpolation must preserve the original CMHANDY glove scale");
        }
        for (const auto part : {3u, 4u}) {
            expect(half.transforms[part] != previous.transforms[part] && half.transforms[part] != current.transforms[part],
                "Wipers and steering wheels must present intermediate local angles");
        }
        const auto& wiper = half.transforms[3];
        const auto determinant = wiper[0] * (wiper[5] * wiper[10] - wiper[6] * wiper[9]) - wiper[4] * (wiper[1] * wiper[10] - wiper[2] * wiper[9])
                                 + wiper[8] * (wiper[1] * wiper[6] - wiper[2] * wiper[5]);
        expect(wiper[8] == 46340.f / 65536.f && wiper[9] == 46340.f / 65536.f && wiper[10] == 0 && determinant < 0,
            "The interpolated wiper must retain42e440's independent local X normal and a nondegenerate basis");
        auto body = identity;
        body[12] = 100;
        const auto attached = state.frame(body, true, true, .5f);
        expect(!attached.visible[2] && !attached.visible[3] && attached.visible[0] && attached.visible[1] && attached.visible[4],
            "Cockpit presentation must keep current CMCHUN visibility discrete");
        for (std::size_t part = 0; part < half.transforms.size(); ++part) {
            expect(std::abs(attached.transforms[part][12] - half.transforms[part][12] - 100) < .00002f,
                "Local cockpit interpolation must attach to the already rendered body exactly once");
        }
        state.shift();
        const auto reaching = state.frame(identity, false, false);
        state.update(180);
        const auto reached = state.frame(identity, false, false);
        const auto shift_hand = cockpit_left_hand_drive(car) ? 1u : 0u;
        const auto middle = hand_position(state.frame(identity, false, false, .5f), shift_hand);
        const auto first = hand_position(reaching, shift_hand), last = hand_position(reached, shift_hand);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            expect(std::abs(2 * middle[axis] - first[axis] - last[axis]) <= 2, "The shifting hand must reach the lever smoothly between source ticks");
        }
    }

    for (const auto hz : {60, 144}) {
        Cockpit state(0, 3);
        for (int tick = 0; tick < 8; ++tick) {
            state.update(180);
        }
        std::uint32_t updates = 0, intermediate_pairs = 0;
        auto previous = state.frame(identity, false, false), current = previous, last = previous;
        std::uint32_t last_tick = UINT32_MAX;
        for (int image = 1; image <= hz; ++image) {
            const auto milliseconds = double(image) * 1000 / hz;
            const auto tick = std::uint32_t(std::floor(milliseconds / 40));
            while (updates <= tick) {
                previous = state.frame(identity, false, false);
                state.update(180);
                current = state.frame(identity, false, false);
                ++updates;
            }
            const auto angle = state.wiper_angle();
            const auto wheel = state.wheel_angle();
            const auto phase = state.shift_phase();
            const auto alpha = float((milliseconds - tick * 40) / 40);
            const auto rendered = state.frame(identity, false, false, alpha);
            if (tick == last_tick && previous.transforms[3] != current.transforms[3]) {
                expect(rendered.transforms[3] != last.transforms[3], "Rain wipers must move in successive60/144Hz images within unchanged simulation ticks");
                ++intermediate_pairs;
            }
            expect(state.wiper_angle() == angle && state.wheel_angle() == wheel && state.shift_phase() == phase
                       && state.frame(identity, false, false).transforms == current.transforms,
                "Repeated cockpit presentation must leave all source animation states untouched");
            last = rendered;
            last_tick = tick;
        }
        expect(intermediate_pairs >= std::uint32_t(hz / 3), "Cockpit cadence fixture must exercise enough actual wiper frames between25Hz source ticks");
    }
    Cockpit fresh(9, 5);
    expect(fresh.frame(identity, false, true, 0).transforms == fresh.frame(identity, false, true, 1).transforms,
        "Fresh cockpit history must retain the exact initialized Flex lever hand and wiper pose");
    std::cout << "PASS: local cockpit60/144Hz motion, hand scale, planar wipers, source endpoints and read-only presentation\n";
}

void reference(const char* root) {
    const toca::AssetStore store(root);
    constexpr std::array<std::size_t, 5> normal_meshes{6, 6, 4, 1, 1};
    for (std::uint32_t car = 0; car < 11; ++car) {
        const auto resources = toca::scene::load_cockpit_resources(store, car);
        for (std::size_t index = 0; index < resources.models.size(); ++index) {
            const auto& model = resources.models[index];
            if (car == 8 || car == 10) {
                expect(model.nodes.empty() && model.meshes.empty(), "No original cockpit should be allocated for absent archive branches");
                continue;
            }
            expect(!model.nodes.empty() && !model.meshes.empty() && !model.textures.empty(), "Original cockpit archive must decode all five CM3D members and textures");
            if (car != 9) {
                expect(model.meshes.size() == normal_meshes[index], "Original touring-car cockpit mesh structure changed");
            }
        }
        expect(resources.models[3].meshes.empty() || resources.models[3].meshes[0].name == "wiper031", "Cockpit member3 must remain the source wiper");
        expect(resources.models[4].meshes.empty() || resources.models[4].meshes[0].name == "WHEEL1", "Cockpit member4 must remain the source steering wheel");
    }
    std::cout << "PASS: every original cockpit archive, touring/bonus resource selection and absent interiors\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            shift_states();
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
