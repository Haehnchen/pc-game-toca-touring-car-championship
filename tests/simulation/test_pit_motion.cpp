#include "assets/store.hpp"
#include "formats/terrain.hpp"
#include "scene/catalog.hpp"
#include "scene/placement.hpp"
#include "scene/qualifying.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"
#include "simulation/pit_lane.hpp"
#include "simulation/session.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::simulation;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

scene::HorizontalPlacement approach(const scene::PitRoute& route) {
    const auto& gate = route.points.front();
    const auto forward = fixed::normalized(fixed::Vector{fixed::sub(gate.left[2], gate.right[2]), 0, fixed::sub(gate.right[0], gate.left[0])});
    const auto inside = [](const fixed::Vector& endpoint, const fixed::Vector& position, const fixed::Vector& other) {
        return fixed::add(fixed::mul(fixed::sub(endpoint[0], other[0]), fixed::sub(endpoint[0], position[0])),
                   fixed::mul(fixed::sub(endpoint[2], other[2]), fixed::sub(endpoint[2], position[2])))
               >= 0;
    };
    // 4017b0/403dd0 also require crossing BETWEEN Left and Right. Knockhill's
    // authored first Best lies beyond Right, so a line through Best cannot
    // enter. Use the actual gate midpoint there, retaining Best when valid.
    const auto crossing = inside(gate.left, gate.best, gate.right) && inside(gate.right, gate.best, gate.left)
                              ? gate.best
                              : fixed::add(gate.left, fixed::scale(fixed::sub(gate.right, gate.left), fixed::one / 2));
    const auto before = fixed::sub(crossing, fixed::scale(forward, 5 * fixed::one));
    // Fixture placement only: quantize a heading facing across the authored
    // entry plane, then use the ordinary source ground/body initialization.
    const auto heading = std::int16_t(std::lround(std::atan2(-double(forward[2]), double(forward[0])) * 4096 / 0x1.921fb54442d18p+2));
    return {{before[0], before[2]}, heading, 0};
}

void traverse(const AssetStore& store, std::uint32_t track, bool manual = false) {
    const auto setup = scene::resolve_setup({.track_id = track, .car_id = 0, .weather_choice = 1});
    const auto prefix = std::string(scene::track_content(track).archive_prefix);
    const auto keep = store.open_archive(prefix + "keep.bfp");
    const auto free = store.open_archive(prefix + "free.bfp");
    const auto points = keep.member(1).bytes();
    const auto route = scene::decode_pit_route(keep.member(2).bytes());
    const HpcSurface surface(keep.member(0).bytes());
    const auto terrain = parse_terrain(free.member(1).bytes());
    const scene::BarrierData barriers(points, terrain.sector_centers);
    const auto initial = scene::initial_body_pose(surface, approach(route), 0);
    const auto drivers = scene::load_qualifying_data(store, setup, 1, "PLAYER1");
    SourceRandom random(7);
    // The Honda's source qualifying initialization supplies its grounded
    // basis, wheel speeds and forward velocity; all later positions integrate.
    SurfaceRoughness roughness;
    DamageContext damage(surface, &barriers);
    Motion motion(surface, initial, 0, manual, setup.weather, true, damage, roughness, &barriers);
    const auto prepare = [&] {
        motion.prepare_ai(AiDriver(points, drivers.drivers[drivers.local_actor].raw, drivers.distributions, drivers.weather_speed_penalty_q16, drivers.tire_offset_q16, random));
    };
    prepare();
    PitLane pit(route, track);
    for (unsigned cycle = 0; cycle < (manual ? 2u : 1u); ++cycle) {
        if (cycle != 0) {
            // A new race resets the actual controller, then installs its fresh
            // route/AIBIN driver before the first tick, as app begin_race does.
            motion.reset_for_race(initial);
            prepare();
            pit.reset();
            expect(!motion.ai_controlled() && motion.start_held() && motion.controls().gear == 0 && motion.state().velocity_q16 == fixed::Vector{} && !pit.active()
                       && pit.point() == 0 && pit.collision_enabled() && !pit.target(),
                "Race reset must clear pit ownership/cursors/target and restore stationary source grid controls");
            motion.set_pit_target(pit.target());
            motion.update({.gear_up = true}, {});
            damage.advance();
            motion.update({}, {});
            damage.advance();
            expect(motion.controls().gear == 1, "Reset manual car must retain driver gear selection before clutch release");
            motion.set_start_hold(false);
        }
        Session session(points, false);
        std::uint32_t entries = 0, advances = 0, ticks = 0;
        bool exited = false, suppression = false, exit_hint = false, autonomous_throttle = false;
        auto last_point = pit.point();
        for (; ticks < 6000 && !exited; ++ticks) {
            const auto transition = pit.update(motion.state().body.position, !motion.ai_controlled());
            if (transition.crossed_entry) {
                session.enter_pit();
                expect(session.next_point() == 0, "Pit entry must reset ordinary POINTS progress to point zero");
            }
            if (transition.entered) {
                ++entries;
                const auto before = motion.state();
                motion.handoff_to_ai(false);
                expect(motion.ai_controlled() && motion.state().body.position == before.body.position && motion.state().velocity_q16 == before.velocity_q16,
                    "Pit entry must hand the integrated car to the source AI without replacing its pose or velocity");
            }
            if (transition.exited) {
                const auto before = motion.state();
                const auto gear = motion.controls().gear;
                const auto rpm = motion.controls().engine_speed_q16;
                motion.restore_player_control();
                expect(!motion.ai_controlled() && motion.state().body.position == before.body.position && motion.state().velocity_q16 == before.velocity_q16
                           && motion.controls().gear == gear && motion.controls().engine_speed_q16 == rpm
                           && motion.controls().tire_offsets_q16 == std::array<std::int32_t, 8>{0, 0, 0, 0, 0xccc, 0, 0, 0},
                    "Pit exit must return the moving car with its gear/RPM intact and restore the player's tire fields");
                exited = true;
            }
            if (pit.point() != last_point && pit.active() && !transition.entered) {
                expect(pit.point() == last_point + 1, "Actual pit traversal must cross authored gates in order");
                ++advances;
            }
            last_point = pit.point();
            suppression = suppression || !pit.collision_enabled();
            exit_hint = exit_hint || pit.exiting_hint();
            (void)session.update(std::uint64_t(ticks + 1) * 40, motion.state().body.position);
            motion.set_ai_route_point(session.next_point());
            motion.set_pit_target(pit.target());
            motion.set_barrier_contacts_enabled(pit.collision_enabled());
            // Conflicting human controls during AI ownership must not steer or
            // stop the car. Human throttle only supplies the approach to gate zero.
            motion.update(motion.ai_controlled() ? ControlsInput{.brake = true, .right = true, .gear_down = true} : ControlsInput{.throttle = true}, {});
            damage.advance();
            autonomous_throttle = autonomous_throttle || (pit.active() && motion.controls().raw_throttle_q16 > 0);
            if (motion.state().boundary != MotionBoundary::none) {
                std::ostringstream message;
                message << "Track " << track << " pit Motion boundary " << int(motion.state().boundary) << " at tick " << ticks << " PITS point " << pit.point() << " position ";
                for (const auto value : motion.state().body.position) {
                    message << value << ' ';
                }
                throw std::runtime_error(message.str());
            }
        }
        std::cout << "Pit Motion track " << track << ": ticks " << ticks << " entries " << entries << " ordered advances " << advances << " exited " << exited << " position "
                  << motion.state().body.position[0] << ' ' << motion.state().body.position[2] << " speed " << motion.state().speed_q16 << '\n';
        if (!exited) {
            throw std::runtime_error("Track " + std::to_string(track) + " failed to complete actual PITS driving; stopped at point " + std::to_string(pit.point()));
        }
        expect(entries == 1 && advances == route.points.size() - 4 && autonomous_throttle && suppression && exit_hint && pit.collision_enabled() && !motion.ai_controlled(),
            "Canonical full-physics pit traversal must drive every ordered gate, suppress and restore barriers, show the exit hint and return controls");
        expect(motion.state().body.position != initial.position && motion.state().speed_q16 > 0, "Pit traversal must complete with an actually integrated moving car");
        if (manual) {
            // Complete any automatic shift already pending at the handback, then
            // exercise the source manual edge/next-tick commit on the real car.
            motion.update({}, {});
            damage.advance();
            const auto gear = motion.controls().gear;
            const bool up = gear < 6 || gear == 7;
            const auto wanted = up ? (gear == 7 ? 1 : gear + 1) : gear - 1;
            motion.update({.gear_up = up, .gear_down = !up}, {});
            damage.advance();
            expect(motion.controls().gear == gear && motion.controls().gear_pending, "Pit handback must restore manual gear-edge handling");
            motion.update({}, {});
            damage.advance();
            expect(motion.controls().gear == wanted && !motion.controls().gear_pending && motion.state().boundary == MotionBoundary::none,
                "Manual gear request after pit exit must commit on the next actual Motion tick");
        }
    }
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t byte = 0; byte < 4; ++byte) {
        bytes.at(offset + byte) = std::uint8_t(value >> (byte * 8));
    }
}

fixed::Vector circle(double point, double radius = 100) {
    const auto angle = point * 0x1.921fb54442d18p+2 / 24;
    return {std::int32_t(std::cos(angle) * radius * fixed::one), 0, std::int32_t(std::sin(angle) * radius * fixed::one)};
}

void pit_checkpoint_timing() {
    std::vector<std::uint8_t> points(4 + 24 * 64);
    put32(points, 0, 24);
    for (unsigned point = 0; point < 24; ++point) {
        const auto left = circle(point, 90), right = circle(point, 110);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put32(points, 4 + point * 64 + axis * 4, std::uint32_t(left[axis]));
            put32(points, 4 + point * 64 + 36 + axis * 4, std::uint32_t(right[axis]));
        }
    }
    for (const auto earned : {false, true}) {
        Session session(points, true);
        std::uint64_t now = 0;
        for (unsigned point = 7; point <= 24; ++point) {
            (void)session.update(now += 100, circle(point % 24 + .1));
        }
        expect(session.released() && session.completed_laps() == 0, "Pit timing fixture must begin a measured qualifying lap");
        for (unsigned point = 1; point <= (earned ? 20u : 12u); ++point) {
            (void)session.update(now += 100, circle(point + .1));
        }
        const auto elapsed = session.lap_time_ms();
        session.enter_pit();
        expect(session.next_point() == 0 && session.completed_laps() == 0 && session.lap() == 1 && session.lap_time_ms() == elapsed && session.best_lap_ms() == 0,
            "Pit entry must reset only ordinary route cursors, preserving lap and elapsed time");
        const auto events = session.update(now += 200, circle(.1));
        if (earned) {
            expect(events == std::vector{SessionEvent::lap_complete} && session.completed_laps() == 1 && session.last_lap_ms() == elapsed + 200
                       && session.best_lap_ms() == elapsed + 200,
                "Pit entry must preserve earned checkpoints and the original lap-start timestamp");
        } else {
            expect(events.empty() && session.completed_laps() == 0 && session.best_lap_ms() == 0 && session.lap_time_ms() == elapsed + 200,
                "Pit entry must not grant missing checkpoints or a free lap at the reset start gate");
        }
    }
}

void finish_during_pit(const AssetStore& store) {
    const auto setup = scene::resolve_setup({.track_id = 0, .car_id = 0, .weather_choice = 1});
    const auto keep = store.open_archive("data/donlkeep.bfp");
    const HpcSurface surface(keep.member(0).bytes());
    const auto body = scene::initial_body_pose(surface, scene::single_qualifying_start(keep.member(1).bytes()), 0);
    const auto drivers = scene::load_qualifying_data(store, setup, 1, "PLAYER1");
    // Isolate finished coasting from pit speed control: three straight source-
    // format POINTS gates with zero desired speed. Ground/body remain canonical.
    std::vector<std::uint8_t> points(4 + 3 * 64);
    put32(points, 0, 3);
    const auto target = fixed::add(body.position, fixed::scale(body.basis[0], 100 * fixed::one));
    const auto left = fixed::add(target, fixed::scale(body.basis[2], 5 * fixed::one));
    const auto right = fixed::sub(target, fixed::scale(body.basis[2], 5 * fixed::one));
    for (unsigned point = 0; point < 3; ++point) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            put32(points, 4 + point * 64 + axis * 4, std::uint32_t(left[axis]));
            put32(points, 4 + point * 64 + 12 + axis * 4, std::uint32_t(target[axis]));
            put32(points, 4 + point * 64 + 24 + axis * 4, std::uint32_t(target[axis]));
            put32(points, 4 + point * 64 + 36 + axis * 4, std::uint32_t(right[axis]));
        }
    }
    SourceRandom random(7);
    SurfaceRoughness roughness;
    DamageContext damage(surface);
    Motion motion(surface, body, 0, true, setup.weather, true, damage, roughness);
    motion.prepare_ai(AiDriver(points, drivers.drivers[drivers.local_actor].raw, drivers.distributions, 0, drivers.tire_offset_q16, random));
    motion.handoff_to_ai(false);
    motion.set_pit_target(target);
    motion.update({}, {});
    damage.advance();
    expect(motion.controls().raw_throttle_q16 > 0 && motion.state().speed_q16 > 0, "Unfinished pit-owned car must consume the source pit governor");
    const auto before = motion.state();
    const auto controls = motion.controls();
    motion.handoff_to_ai(true); // Qualifying finishes while pit AI already owns it.
    expect(motion.ai_controlled() && motion.state().body.position == before.body.position && motion.state().velocity_q16 == before.velocity_q16
               && motion.controls().gear == controls.gear && motion.controls().engine_speed_q16 == controls.engine_speed_q16,
        "Finishing an already AI-owned pit car must update its regime without replacing the moving body or controls");
    motion.set_pit_target(std::nullopt); // Pit exit retains finished ownership.
    motion.update({}, {});
    damage.advance();
    expect(motion.ai_controlled() && motion.controls().raw_throttle_q16 == 0 && motion.controls().raw_brake_q16 == 0 && motion.state().boundary == MotionBoundary::none,
        "Finished pit exit must use source finished coasting, rather than unfinished route acceleration/braking");
}

void canonical(const std::filesystem::path& root) {
    const AssetStore store(root);
    pit_checkpoint_timing();
    finish_during_pit(store);
    std::string failures;
    // 403dd0 excludes Lavaland (track 9) from pit entry despite its PITS data.
    for (std::uint32_t track = 0; track < 9; ++track) {
        try {
            traverse(store, track);
        } catch (const std::exception& error) {
            std::cerr << "Pit Motion track " << track << ": " << error.what() << '\n';
            failures += " track " + std::to_string(track) + ": " + error.what() + '\n';
        }
    }
    if (!failures.empty()) {
        throw std::runtime_error("Canonical pit traversal failures:\n" + failures);
    }
    traverse(store, 0, true); // Manual handback, then reset/re-entry on same car.
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Usage: pit-motion-tests canonical-root");
        }
        canonical(argv[1]);
        std::cout << "PASS: canonical authored pit traversal through real Honda wheel/body/HPC/barrier integration\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
