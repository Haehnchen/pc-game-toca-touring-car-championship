#include "audio/spatial.hpp"
#include "scene/finish_camera.hpp"
#include "scene/follow_camera.hpp"
#include "scene/weather.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void spatial_listener() {
    using namespace toca;
    scene::BodyPose body;
    body.position = {13 * 65536, 3 * 65536, -7 * 65536};
    for (const bool micro : {false, true}) {
        for (const std::int16_t heading : {0, 500, 1024, 1234, 2048, 3000, 4095}) {
            const scene::FollowCamera camera(heading, micro);
            const auto world = scene::weather_camera(camera.world_to_view(body));
            expect(world.position == camera.position(body), "Audio must use the existing actual camera position");
            const audio::SpatialListener listener{world.position, audio::source_listener_heading(world.forward)};
            auto left = world.position, right = world.position;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                left[axis] -= 10 * world.right[axis];
                right[axis] += 10 * world.right[axis];
            }
            const auto left_sound = audio::source_spatial(left, listener), right_sound = audio::source_spatial(right, listener);
            expect(left_sound.in_range && left_sound.pan < 16 && right_sound.in_range && right_sound.pan > 111,
                "Screen-left/right contacts must stay on their matching speakers through camera yaw and CMMICRO");
        }
    }
}

void cold_cockpit_anchor() {
    using namespace toca::scene;
    BodyPose body;
    body.position = {-10098926, 383958, -5220496};
    body.basis = {{{-64552, -140, 11336}, {-915, 65370, -4403}, {-11300, -4495, -64396}}};
    FollowCamera camera(0, false, 7);
    //Exterior ticks retain42f280's cold inactive latch and zero anchor.
    camera.update_cockpit(body, {}, {}, 0);
    camera.cycle_view();
    camera.cycle_view();
    camera.cycle_view();
    camera.update_cockpit(body, {-23939, -70, 4111}, {-26050, -58, 4519}, -97);
    const std::array<float, 16> expected{0.17243592441082f, 0.06854677945375443f, 0.9827479720115662f, 0, -0.005780222825706005f, 0.9976406097412109f, -0.0686090812087059f, 0,
        -0.984647274017334f, 0.006160310469567776f, 0.17233093082904816f, 0, -154.1678466796875f, 6.230336666107178f, -80.07913208007812f, 1};
    const auto first = camera.frame(body);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expect(std::bit_cast<std::uint32_t>(first.camera_to_world[index]) == std::bit_cast<std::uint32_t>(expected[index]),
            "First cockpit entry must retain the source cold zero anchor before its spring update");
    }
    camera.cycle_view();
    camera.update_cockpit(body, {}, {}, 0);
    camera.cycle_view();
    camera.cycle_view();
    camera.cycle_view();
    camera.update_cockpit(body, {}, {}, 0);
    expect(camera.frame(body).camera_to_world[1] < 0.02f, "Leaving the active cockpit must seed identity once, giving a different damped reentry from the cold zero anchor");
}

void source_frame() {
    using namespace toca::scene;
    BodyPose body;
    //First Donington NoQual publication: authored SGRID heading and physical
    //body output. Numerical camera expectations from the retained source journal.
    body.position = {-9642299, 380691, -5298024};
    FollowCamera camera(2161);
    const std::array<float, 16> expected_world{0.1724700927734375f, 0, 0.9850006103515625f, 0, -0.16541926562786102f, 0.9857940673828125f, 0.028964322060346603f, 0,
        -0.971007764339447f, -0.167938232421875f, 0.1700199991464615f, 0, -141.81021118164062f, 7.309112548828125f, -81.77287292480469f, 1};
    const std::array<float, 16> expected_view{0.1724749207496643f, -0.16542500257492065f, -0.9710414409637451f, 0, 1.7914945082964095e-9f, 0.9858008027076721f,
        -0.16793937981128693f, 0, 0.985028088092804f, 0.02896532602608204f, 0.17002590000629425f, 0, 105.00727844238281f, -28.295705795288086f, -122.57260131835938f, 1};
    const auto frame = camera.frame(body);
    for (std::size_t index = 0; index < expected_world.size(); ++index) {
        expect(std::bit_cast<std::uint32_t>(frame.camera_to_world[index]) == std::bit_cast<std::uint32_t>(expected_world[index]),
            "Generated View2 world matrix must retain every source float bit");
        expect(std::bit_cast<std::uint32_t>(frame.world_to_view[index]) == std::bit_cast<std::uint32_t>(expected_view[index]),
            "Source cofactor spills and reciprocal must reproduce the recorded inverse bitwise");
    }
    expect(frame.world_position == std::array<float, 3>{expected_world[12], expected_world[13], expected_world[14]},
        "Scene selection must consume published world translation directly");
    expect(camera.world_to_view(body) == frame.world_to_view, "Live View2 must consume the source frame inverse");
    expect(FollowCamera(47).frame(body).world_position[0] == -152.5163116455078f, "Camera composition must add the body before spilling the rotated source chase offset");

    BodyPose previous;
    previous.basis[0] = {65536, 0, 0};
    FollowCamera ordered(512);
    ordered.update(previous, false);
    body.basis[0] = {0, 0, 65536};
    const auto publication = ordered.frame(body);
    auto retained_basis = body;
    retained_basis.basis = previous.basis;
    expect(ordered.heading() == 448 && publication.world_to_view == ordered.frame(body).world_to_view,
        "Repeated scene publication must retain pre-physics yaw without advancing it from the latest body");
    expect(publication.camera_to_world == ordered.frame(retained_basis).camera_to_world, "Latest physical orientation must not replace the retained follow anchor yaw");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        body.position[axis] += 65536;
    }
    const auto translated = ordered.frame(body);
    expect(ordered.heading() == 448, "Publishing the latest physical translation must preserve the retained follow heading");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        expect(translated.world_position[axis] == publication.world_position[axis] + 1.f, "Scene refresh must use the latest physical translation");
    }
}

void published_world() {
    using namespace toca::scene;
    BodyPose body;
    body.position = {13 * 65536 + 17, 3 * 65536 + 31, -7 * 65536 - 23};
    body.basis = {{{65522, 0, 0}, {0, 65524, 0}, {0, 0, 65538}}};
    for (const bool micro : {false, true}) {
        FollowCamera camera(0, micro);
        for (unsigned step = 0; step < 4; ++step) {
            camera.update_cockpit(body, {}, {}, 0);
            std::array<float, 16> world;
            const auto before = camera.world_to_view(body);
            expect(camera.world_to_view(body, &world) == before, "Publishing chase/bonnet/cockpit world matrices must retain every existing view value");
            expect(
                world[15] == 1 && std::isfinite(world[12]) && std::isfinite(world[13]) && std::isfinite(world[14]), "Every ordinary camera must publish its composed affine node");
            camera.set_rear_view(true);
            const auto rear = camera.world_to_view(body);
            expect(camera.world_to_view(body, &world) == rear, "Publishing the rear camera must preserve its existing inverse");
            camera.set_rear_view(false);
            camera.cycle_view();
        }
    }
    FollowCamera chase(0);
    std::array<float, 16> world;
    chase.world_to_view(body, &world);
    expect(world[12] == float(body.position[0]) / 65536.f - float(0x5668e) / 65536.f && world[13] == float(body.position[1]) / 65536.f + float(0x1800f) / 65536.f
               && world[14] == float(body.position[2]) / 65536.f,
        "Published View2 translation must come directly from its original local node and follow anchor");
    SceneCamera authored;
    authored.transform[0] = authored.transform[5] = authored.transform[10] = authored.transform[15] = 65536;
    authored.transform[12] = 73 * 65536 + 17;
    authored.transform[13] = 9 * 65536 + 6553;
    authored.transform[14] = -55 * 65536 - 123;
    const std::array cameras{authored};
    const FinishCamera finish(cameras);
    const auto original = finish.world_to_view(body.position);
    expect(finish.world_to_view(body.position, nullptr, false, &world) == original, "Publishing an authored camera must retain its existing look-at inverse");
    for (std::size_t axis = 0; axis < 3; ++axis) {
        expect(world[12 + axis] == float(authored.transform[12 + axis]) / 65536.f, "Authored camera must publish its actual world origin before inversion");
    }
}
} // namespace

int main() {
    try {
        using namespace toca::scene;
        source_frame();
        cold_cockpit_anchor();
        spatial_listener();
        published_world();
        constexpr std::array<std::int16_t, 10> initial_headings{2218, 5006, -682, 1024, 1024, 1024, 3072, 512, 1024, 1024};
        for (std::uint32_t track = 0; track < initial_headings.size(); ++track) {
            expect(qualifying_camera_heading(track) == initial_headings[track], "Qualifying must use the original track-angle table and signed fixed conversion");
        }
        expect(follow_direction_angle({65536, 0, 0}) == 0 && follow_direction_angle({0, 0, 65536}) == 1024 && follow_direction_angle({-65536, 0, 0}) == 2048
                   && follow_direction_angle({0, 0, -65536}) == -1024,
            "Original follow atan axis/quadrant results changed");
        expect(follow_direction_angle({65536, 0, 65536}) == 0, "Equal-component lookup must preserve original cos[0] low-word overlap");
        BodyPose body;
        body.basis = {{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
        for (const bool vertical : {false, true}) {
            FollowCamera split(0, false, 0);
            split.set_split_screen(vertical);
            expect(
                split.position(body) == std::array<std::int32_t, 3>{vertical ? -0x6eb76 : -0x5335c, 0x1800f, 0}, "Split near chase must use4508ce orientation-specific distance");
            split.cycle_view();
            expect(split.position(body) == std::array<std::int32_t, 3>{vertical ? -0x79972 : -0x6ccec, vertical ? 0x1a8ff : 0x219a5, 0},
                "Split far chase must use original orientation-specific height and distance");
            split.cycle_view();
            expect(split.position(body) == std::array<std::int32_t, 3>{vertical ? 0xc18d : 0xdbda, vertical ? 0x75f8 : 0xb341, 0},
                "Split bonnet camera must use original orientation-specific node");
            split.set_rear_view(true);
            expect(split.position(body) == std::array<std::int32_t, 3>{vertical ? -78643 : -0x129b0, vertical ? 0xc000 : 0xe68b, 0},
                "Horizontal split rear view must select its distinct source node");
            split.set_rear_view(false);
            split.set_split_screen(std::nullopt);
            expect(split.position(body) == std::array<std::int32_t, 3>{19660, 0xb333, 0}, "Returning to one player must restore the original camera node");
        }
        FollowCamera selected(0, false, 0);
        expect(selected.selected_view() == 2, "Normal entry must use near chase2");
        selected.cycle_view();
        expect(selected.selected_view() == 3, "First C must select far chase3");
        const auto far_position = selected.position(body);
        expect(far_position == std::array<std::int32_t, 3>{-7 * 65536, 2 * 65536, 0}, "Far chase must use source seven-unit offset and two-unit height");
        selected.cycle_view();
        expect(selected.selected_view() == 4, "Second C must select bonnet4");
        expect(selected.position(body) == std::array<std::int32_t, 3>{19660, 0xb333, 0}, "Bonnet camera must use source per-car table");
        selected.set_rear_view(true);
        expect(selected.projection_view() == 4, "Rear view must retain the prior projection");
        expect(selected.selected_view() == 5 && selected.position(body) == std::array<std::int32_t, 3>{-78643, 0xc000, 0}, "Held Z must select source rear camera5");
        const auto rear_matrix = selected.world_to_view(body);
        expect(rear_matrix[2] < 0, "Rear camera must face opposite the body forward axis");
        selected.cycle_view();
        expect(selected.selected_view() == 5, "C must be ignored while rear view is held");
        selected.set_rear_view(false);
        expect(selected.selected_view() == 4, "Z release must restore the saved bonnet view");
        selected.cycle_view();
        selected.update_cockpit(body, {}, {}, 0);
        expect(selected.selected_view() == 1 && selected.position(body) == std::array<std::int32_t, 3>{0, 0x6666, -26208},
            "Bonnet wraps to source cockpit1 with original left/right offset");
        selected.toggle_trackside();
        expect(!selected.trackside(), "Ordinary F5 must require CMFOLLOW");
        FollowCamera right_hand(0, false, 1);
        right_hand.cycle_view();
        right_hand.cycle_view();
        right_hand.cycle_view();
        right_hand.update_cockpit(body, {}, {}, 0);
        expect(right_hand.position(body)[2] == 26208, "Cockpit must retain source driving-side table");
        FollowCamera tank(0, false, 8);
        tank.cycle_view();
        tank.cycle_view();
        tank.cycle_view();
        expect(tank.selected_view() == 2, "Original tank cycle must skip absent cockpit1");
        for (std::uint32_t detail = 0; detail < 5; ++detail) {
            FollowCamera tank_detail(0, false, 8, false, detail);
            tank_detail.cycle_view();
            tank_detail.cycle_view();
            expect(tank_detail.selected_view() == 4 && tank_detail.position(body)[0] == 19660 + (detail < 2 ? 0x3333 : 0),
                "Tank bonnet must add the original offset only at Low/Medium detail");
        }
        for (const auto car : {7u, 8u}) {
            for (const bool micro_mode : {false, true}) {
                FollowCamera very_high(512, micro_mode, car, false, 3), mega(512, micro_mode, car, false, 4);
                for (unsigned step = 0; step < 5; ++step) {
                    very_high.update(body, false);
                    mega.update(body, false);
                    very_high.update_cockpit(body, {0, 0, 0}, {1000, 20, -300}, 128);
                    mega.update_cockpit(body, {0, 0, 0}, {1000, 20, -300}, 128);
                    expect(mega.selected_view() == very_high.selected_view() && mega.projection_view() == very_high.projection_view() && mega.heading() == very_high.heading()
                               && mega.position(body) == very_high.position(body) && mega.world_to_view(body) == very_high.world_to_view(body),
                        "Mega must retain Very High view cycling, follow yaw and cockpit motion for ordinary and tank cameras");
                    very_high.set_rear_view(true);
                    mega.set_rear_view(true);
                    expect(mega.position(body) == very_high.position(body) && mega.world_to_view(body) == very_high.world_to_view(body),
                        "Mega's held rear camera must match Very High");
                    very_high.set_rear_view(false);
                    mega.set_rear_view(false);
                    very_high.cycle_view();
                    mega.cycle_view();
                }
            }
        }
        bool invalid_detail = false;
        try {
            (void)FollowCamera(0, false, 7, false, 5);
        } catch (const std::runtime_error&) {
            invalid_detail = true;
        }
        expect(invalid_detail, "Follow cameras must reject detail above Mega");
        FollowCamera authored(0, false, 0, true);
        expect(authored.selected_view() == 2 && !authored.trackside(), "CMFOLLOW must retain original initial chase view");
        authored.cycle_view();
        authored.cycle_view();
        authored.cycle_view();
        expect(authored.selected_view() == 0 && authored.trackside(), "CMFOLLOW wraps bonnet4 into authored camera0 override");
        authored.set_rear_view(true);
        expect(authored.trackside() && authored.selected_view() == 5 && authored.projection_view() == 0, "Rear hold must retain the source authored override and saved projection");
        authored.toggle_trackside();
        expect(authored.trackside(), "View0 must retain authored mode until the separate helicopter actor exists");
        authored.set_rear_view(false);
        expect(authored.selected_view() == 0 && authored.trackside(), "Rear release must restore authored camera0");
        authored.cycle_view();
        expect(authored.selected_view() == 1 && !authored.trackside(), "Next C leaves authored mode for cockpit1");
        authored.toggle_trackside();
        expect(authored.trackside() && authored.selected_view() == 1, "F5 toggles authored override without changing the stored camera");
        FollowCamera airborne(0, false, 0, true, 3, true, true);
        airborne.cycle_view();
        airborne.cycle_view();
        airborne.cycle_view();
        expect(airborne.selected_view() == 0 && !airborne.trackside(), "CMCOPTER must win over CMFOLLOW and choose actual actor camera0 after bonnet4");
        airborne.toggle_trackside();
        expect(airborne.trackside(), "F5 must switch actual actor camera0 to authored trackside mode");
        airborne.set_rear_view(true);
        airborne.toggle_trackside();
        expect(airborne.selected_view() == 5 && !airborne.trackside() && airborne.projection_view() == 0,
            "F5 while rear is held must permit restoring an existing source actor camera0");
        airborne.set_rear_view(false);
        expect(airborne.selected_view() == 0 && !airborne.trackside(), "Rear release must restore actual actor camera0");
        airborne.cycle_view();
        expect(airborne.selected_view() == 1 && !airborne.trackside(), "C must leave actual actor camera0 for cockpit1");
        FollowCamera follow_actor(0, false, 0, true, 3, false, true);
        follow_actor.cycle_view();
        follow_actor.cycle_view();
        follow_actor.cycle_view();
        expect(follow_actor.selected_view() == 0 && follow_actor.trackside(), "CMFOLLOW without CMCOPTER must still enter authored0 first");
        follow_actor.toggle_trackside();
        expect(!follow_actor.trackside() && follow_actor.selected_view() == 0, "F5 may enter an actual HOR camera0 without the CMCOPTER flag");
        FollowCamera wet_copter(0, false, 0, false, 3, true, false);
        wet_copter.cycle_view();
        wet_copter.cycle_view();
        wet_copter.cycle_view();
        expect(wet_copter.selected_view() == 1 && !wet_copter.trackside(), "Unavailable weather actors must preserve a usable ordinary camera cycle");
        FollowCamera wet_both(0, false, 0, true, 3, true, false);
        wet_both.cycle_view();
        wet_both.cycle_view();
        wet_both.cycle_view();
        expect(wet_both.selected_view() == 0 && wet_both.trackside(), "Missing HOR camera0 must still allow CMFOLLOW's authored override");
        FollowCamera head(0);
        head.cycle_view();
        head.cycle_view();
        head.cycle_view();
        head.update_cockpit(body, {}, {}, 0);
        const auto head_still = head.world_to_view(body);
        head.update_cockpit(body, {}, {}, 0);
        expect(head.world_to_view(body) == head_still, "Constant body and velocity must retain the initialized cockpit camera");
        head.update_cockpit(body, {}, {65536, 0, 0}, 0);
        const auto head_accelerated = head.world_to_view(body);
        expect(head_accelerated != head_still && head.position(body)[0] < -6000 && head.position(body)[0] > -7000,
            "Acceleration must displace the head through the original capped .1-unit spring velocity");
        for (int tick = 0; tick < 20; ++tick) {
            head.update_cockpit(body, {65536, 0, 0}, {65536, 0, 0}, 0);
        }
        const auto residual = head.position(body)[0];
        expect(std::abs(residual) < 0x1ca, "Damped head translation must return within the source fixed-square-root deadband of its immutable driver offset");
        for (int pulse = 0; pulse < 30; ++pulse) {
            head.update_cockpit(body, {}, {65536, 0, 0}, 0);
            head.update_cockpit(body, {65536, 0, 0}, {}, 0);
            for (int tick = 0; tick < 20; ++tick) {
                head.update_cockpit(body, {}, {}, 0);
            }
        }
        expect(std::abs(head.position(body)[0]) < 0x1ca, "Repeated acceleration/braking pulses must not drift the cockpit position");
        const auto damped = head.world_to_view(body);
        head.update_cockpit(body, {65536, 0, 0}, {65536, 0, 0}, 0);
        expect(head.world_to_view(body) == damped, "Original head damping must stop its velocity and preserve final translation");
        head.update_cockpit(body, {}, {}, 65536);
        const auto rolled = head.world_to_view(body);
        expect(rolled != damped, "Angular velocity must drive the original head roll slew");
        head.cycle_view();
        head.update_cockpit(body, {}, {}, 0);
        head.cycle_view();
        head.cycle_view();
        head.cycle_view();
        expect(head.selected_view() == 1, "Head spring check must return to cockpit");
        expect(head.world_to_view(body) != rolled, "Leaving cockpit must reset its orientation state");
        head.update_cockpit(body, {}, {}, 0);
        expect(std::abs(head.position(body)[0]) < 0x1ca, "Cockpit re-entry must retain its bounded original driver offset");
        FollowCamera micro(0, true);
        micro.update(body);
        expect(micro.heading() == 0 && micro.position(body) == std::array<std::int32_t, 3>{0, 40 * 65536, 0},
            "CMMICRO must use the original forty-unit camera height and retain its heading");
        const auto micro_view = micro.world_to_view(body);
        FollowCamera cycling_micro(512, true);
        cycling_micro.update(body, false);
        expect(cycling_micro.heading() == 512, "CMMICRO3 must freeze follow yaw");
        cycling_micro.cycle_view();
        cycling_micro.update(body, false);
        expect(cycling_micro.heading() == 448, "CMMICRO must allow normal slew in other selected views");
        expect(micro_view[6] < -.99f && std::abs(micro_view[14] - 40.f) < .1f, "CMMICRO camera must look down toward the car");
        FollowCamera slow(512), regular(512);
        slow.update(body, true);
        regular.update(body, false);
        expect(slow.heading() == 490 && regular.heading() == 448, "State4 threshold slew and normal error/8 must stay distinct");
        FollowCamera negative(-512);
        negative.update(body, true);
        expect(negative.heading() == -490, "State4 negative threshold has wrong sign");
        FollowCamera deadband(15);
        deadband.update(body, true);
        expect(deadband.heading() == 15, "Original integer heading deadband was replaced with floating convergence");
        FollowCamera wrap(4090);
        wrap.update(body, false);
        expect(wrap.heading() == 4090, "Wrapped negative error must divide toward zero");
        FollowCamera fixed(1234);
        const auto before = fixed.world_to_view(body);
        const auto listener_before = fixed.position(body);
        const auto initial = initial_follow_world_to_view(body, 1234);
        for (std::size_t index = 0; index < initial.size(); ++index) {
            expect(std::abs(before[index] - initial[index]) < 0.000002f, "Source camera inverse must preserve the existing View2 geometry");
        }
        body.position = {10 * 65536, 3 * 65536, -7 * 65536};
        const auto moved = fixed.world_to_view(body);
        const auto listener_after = fixed.position(body);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            expect(std::abs(std::int64_t(listener_after[axis]) - listener_before[axis] - body.position[axis]) <= 4,
                "Listener must follow actual affine camera translation in fixed coordinates");
        }
        expect(moved != before && fixed.heading() == 1234, "Presentation must follow body translation without advancing heading state");
        body.basis[1] = {0, 0, 65536};
        body.basis[2] = {0, -65536, 0};
        expect(fixed.world_to_view(body) == moved, "Follow anchor must not inherit body roll/pitch");
        for (const auto value : moved) {
            expect(std::isfinite(value), "Follow camera matrix is not finite");
        }
        std::cout << "PASS: source view cycle/rear/CMFOLLOW, cockpit spring, source follow slew, projection hold and spatial listener\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
