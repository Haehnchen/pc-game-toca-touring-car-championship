#include "assets/store.hpp"
#include "scene/catalog.hpp"
#include "scene/qualifying.hpp"
#include "scene/resources.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/session.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::simulation;

void run(const std::filesystem::path& root, const std::filesystem::path& output) {
    const AssetStore store(root);
    const auto setup = scene::resolve_setup({.track_id = 0, .car_id = 0, .weather_choice = 1});
    const auto resources = scene::load_scene_resources(store, setup);
    const auto data = scene::load_qualifying_data(store, setup, 1, "PLAYER1");
    const auto route = std::span<const std::uint8_t>(resources.route_points);
    const HpcSurface surface(resources.collision_hpc);
    const scene::BarrierData barriers(route, resources.ground.sector_centers);
    PropContacts props(resources.cones, resources.posts, resources.signs, resources.ground.sector_centers, surface);
    const auto body = scene::initial_body_pose(surface, scene::single_qualifying_start(route), 0);
    SourceRandom random(7);
    SurfaceRoughness roughness;
    DamageContext damage(surface, &barriers);
    Motion motion(surface, body, 0, false, setup.weather, true, damage, roughness, &barriers);
    motion.prepare_ai(AiDriver(route, data.drivers.at(data.local_actor).raw, data.distributions, data.weather_speed_penalty_q16, data.tire_offset_q16, random));
    // Explicit CPU test driver: preserve the source unfinished-rollout driver
    // after the normal player handover. It supplies axes, never body state.
    motion.handoff_to_ai(false);
    Session session(route, true);
    std::ofstream csv;
    if (!output.empty()) {
        csv.open(output);
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        csv << "tick,point,released,lap,completed,x,y,z,speed,gear,left,right,gas,brake,boundary,barriers,contacts,surface0,surface1,surface2,surface3,elapsed_ms,release_tick,lap_"
               "ms,last_ms,best_ms,lap_complete,terminal,session_x,session_y,session_z\n";
    }
    std::set<std::uint32_t> measured_points;
    auto previous_position = body.position;
    auto previous_point = session.next_point();
    std::uint32_t release_tick = 0, route_advances = 0, lap_events = 0;
    double distance = 0;
    const auto write_state = [&](std::uint32_t tick, bool lap_complete, bool terminal) {
        const auto& state = motion.state();
        const auto& control = motion.controls();
        if (csv.is_open()) {
            csv << tick << ',' << session.next_point() << ',' << session.released() << ',' << session.lap() << ',' << session.completed_laps();
            for (const auto coordinate : state.body.position) {
                csv << ',' << coordinate;
            }
            csv << ',' << state.speed_q16 << ',' << int(control.gear) << ',' << control.raw_left_q16 << ',' << control.raw_right_q16 << ',' << control.raw_throttle_q16 << ','
                << control.raw_brake_q16 << ',' << int(state.boundary) << ',' << state.barrier_contact_count << ',' << state.contact_count;
            for (const auto material : state.surface_ids) {
                csv << ',' << int(material);
            }
            csv << ',' << std::uint64_t(tick) * 40 << ',' << release_tick << ',' << session.lap_time_ms() << ',' << session.last_lap_ms() << ',' << session.best_lap_ms() << ','
                << lap_complete << ',' << terminal;
            // Session examines this pre-Motion position. The terminal row has
            // no additional physical update and records the actual crossing.
            for (const auto coordinate : previous_position) {
                csv << ',' << coordinate;
            }
            csv << '\n';
        }
    };
    constexpr std::uint32_t maximum_ticks = 32000;
    for (std::uint32_t tick = 1; tick <= maximum_ticks; ++tick) {
        const auto events = session.update(std::uint64_t(tick) * 40, motion.state().body.position);
        for (const auto event : events) {
            lap_events += event == SessionEvent::lap_complete ? 1u : 0u;
        }
        if (session.released() && release_tick == 0) {
            release_tick = tick;
            std::cout << "Player handover tick " << tick << ", route point " << session.next_point() << '\n';
        }
        if (release_tick != 0) {
            measured_points.insert(session.next_point());
        }
        if (previous_point != session.next_point() && session.next_point() != (previous_point + 1) % session.point_count()) {
            throw std::runtime_error("Full-lap driver did not cross the next ordered POINTS boundary");
        }
        route_advances += previous_point != session.next_point() ? 1u : 0u;
        previous_point = session.next_point();
        motion.set_ai_route_point(session.next_point());
        if (session.completed_laps() != 0) {
            write_state(tick, lap_events == 1, true);
            if (lap_events != 1 || session.completed_laps() != 1 || session.lap() != 2 || measured_points.size() != session.point_count() || session.last_lap_ms() == 0
                || session.last_lap_ms() != session.best_lap_ms() || session.last_lap_ms() != (tick - release_tick) * 40 || session.lap_time_ms() != 0) {
                throw std::runtime_error("Completed-lap evidence disagrees with the continuous ordered-route/session state");
            }
            std::cout << "PASS: one native car completed Donington through real force/body/contact updates; CPU source-AI test driver, " << tick << " ticks, measured "
                      << tick - release_tick << " ticks, " << measured_points.size() << " route points, lap " << session.last_lap_ms() << " ms, distance " << distance << " m\n";
            return;
        }
        motion.update({}, {}, true);
        props.advance();
        motion.apply_prop_response(props.contact({motion.state().body, motion.state().velocity_q16, 0, true}));
        motion.finish_ground();
        damage.advance();
        const auto& state = motion.state();
        write_state(tick, false, false);
        double squared = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto delta = double(std::int64_t(state.body.position[axis]) - previous_position[axis]) / fixed::one;
            squared += delta * delta;
        }
        distance += std::sqrt(squared);
        previous_position = state.body.position;
        if (state.boundary != MotionBoundary::none) {
            throw std::runtime_error("Full-lap driver hit motion boundary " + std::to_string(int(state.boundary)) + " at tick " + std::to_string(tick) + ", route point "
                                     + std::to_string(session.next_point()) + ", position " + std::to_string(state.body.position[0]) + ',' + std::to_string(state.body.position[1])
                                     + ',' + std::to_string(state.body.position[2]));
        }
        if (tick % 1000 == 0) {
            std::cout << "Progress " << tick << ": point " << session.next_point() << '/' << session.point_count() << ", advances " << route_advances << ", distance " << distance
                      << ", speed " << state.speed_q16 << '\n';
        }
    }
    throw std::runtime_error("Source-AI test driver did not complete the lap within the bounded continuous run");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3) {
            throw std::runtime_error("Usage: toca-full-lap-tests CANONICAL_ROOT [OUTPUT_CSV]");
        }
        run(argv[1], argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::path{});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
