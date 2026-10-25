#include "assets/store.hpp"
#include "scene/catalog.hpp"
#include "scene/qualifying.hpp"
#include "scene/resources.hpp"
#include "scene/surface_effects.hpp"
#include "simulation/ai_driver.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"
#include "simulation/prop_contacts.hpp"
#include "simulation/session.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace toca;
using namespace toca::simulation;

constexpr std::array<std::string_view, 10> names{
    "Donington GP", "Silverstone", "Thruxton", "Brands Hatch", "Oulton Park", "Donington Short", "Croft", "Knockhill", "Snetterton", "Lavaland"};
constexpr std::uint32_t tick_ms = 40, maximum_ticks = 32000;

struct Result {
    std::uint32_t track = 0, tick = 0, release_tick = 0, points = 0, visited = 0, advances = 0, lap_ms = 0, approach_reversals = 0;
    std::uint32_t weather_choice = 1, resolved_weather = 1;
    std::uint64_t barrier_contacts = 0, barrier_ticks = 0, wheel_contacts = 0, airborne_ticks = 0, boundary_ticks = 0;
    std::uint64_t slip_ticks = 0;
    double distance = 0, measured_distance = 0;
    std::string failure;
};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Result traverse(const AssetStore& store, std::uint32_t track, std::uint32_t weather, const std::filesystem::path& output) {
    Result result;
    result.track = track;
    result.weather_choice = weather;
    try {
        const auto setup = scene::resolve_setup({.track_id = track, .car_id = 0, .weather_choice = weather});
        result.resolved_weather = setup.weather;
        expect(setup.weather == (track == 9 ? 1u : weather), "Explicit weather must reach Motion; Lavaland must retain its original dry override");
        const auto resources = scene::load_scene_resources(store, setup);
        const auto drivers = scene::load_qualifying_data(store, setup, 1, "PLAYER1");
        const auto route = std::span<const std::uint8_t>(resources.route_points);
        const HpcSurface surface(resources.collision_hpc);
        const scene::BarrierData barriers(route, resources.ground.sector_centers);
        PropContacts props(resources.cones, resources.posts, resources.signs, resources.ground.sector_centers, surface);
        const auto body = scene::initial_body_pose(surface, scene::single_qualifying_start(route), 0);
        SourceRandom random(7);
        SurfaceRoughness roughness;
        DamageContext damage(surface, &barriers);
        Motion motion(surface, body, 0, false, setup.weather, true, damage, roughness, &barriers);
        motion.prepare_ai(AiDriver(route, drivers.drivers.at(drivers.local_actor).raw, drivers.distributions, drivers.weather_speed_penalty_q16, drivers.tire_offset_q16, random));
        // The existing unfinished-rollout source AI is only this CPU test's
        // input driver. It retains ownership past the normal start crossing.
        // Motion, props, wheels, barriers, RNG and Session persist throughout;
        // no snapshot import, reset, teleport or physics adjustment is used.
        motion.handoff_to_ai(false);
        Session session(route, true);
        result.points = session.point_count();
        std::ofstream csv;
        if (!output.empty()) {
            csv.exceptions(std::ios::failbit | std::ios::badbit);
            csv.open(output / ("track-" + std::to_string(track) + (weather == 1 ? "" : "-weather-" + std::to_string(weather)) + ".csv"));
            csv << "tick,elapsed_ms,next_point,released,lap,completed_laps,session_x,session_y,session_z,x,y,z,speed_q16,gear,left,right,gas,brake,boundary,barrier_contacts,"
                   "wheel_contacts,surface0,surface1,surface2,surface3,lap_ms,last_ms,best_ms,release_tick,measured_advances,approach_reversals,terminal,weather_choice,"
                   "resolved_weather,slip_ticks\n";
        }
        std::set<std::uint32_t> measured_points;
        auto previous_point = session.next_point();
        for (result.tick = 1; result.tick <= maximum_ticks; ++result.tick) {
            const auto session_position = motion.state().body.position;
            const auto events = session.update(std::uint64_t(result.tick) * tick_ms, session_position);
            std::uint32_t releases = 0, completions = 0;
            for (const auto event : events) {
                releases += event == SessionEvent::released ? 1u : 0u;
                completions += event == SessionEvent::lap_complete ? 1u : 0u;
            }
            const bool newly_released = session.released() && result.release_tick == 0;
            if (newly_released) {
                result.release_tick = result.tick;
            }
            if (result.release_tick != 0) {
                measured_points.insert(session.next_point());
            }
            result.visited = std::uint32_t(measured_points.size());
            const auto next_point = session.next_point();
            const bool advanced = next_point != previous_point;
            const bool ordered = !advanced || next_point == (previous_point + 1) % session.point_count();
            // Authored qualifying Best can begin behind its own gate plane.
            // Session's original reverse branch then unwinds the initial
            // cursor during the discarded approach. Retain that real update;
            // every measured-lap transition still must be forward and ordered.
            if (!ordered && !session.released()) {
                ++result.approach_reversals;
            }
            if (advanced && result.release_tick != 0 && !newly_released) {
                ++result.advances;
            }
            previous_point = next_point;
            motion.set_ai_route_point(next_point);
            const bool terminal = session.completed_laps() != 0;
            if (!terminal && (ordered || !session.released())) {
                motion.update({}, {}, true);
                props.advance();
                motion.apply_prop_response(props.contact({motion.state().body, motion.state().velocity_q16, 0, true}));
                motion.finish_ground();
                damage.advance();
                const auto& state = motion.state();
                double squared = 0;
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const auto delta = double(std::int64_t(state.body.position[axis]) - session_position[axis]) / fixed::one;
                    squared += delta * delta;
                }
                const auto distance = std::sqrt(squared);
                result.distance += distance;
                result.measured_distance += result.release_tick != 0 ? distance : 0;
                result.barrier_contacts += state.barrier_contact_count;
                result.barrier_ticks += state.barrier_contact_count != 0 ? 1u : 0u;
                result.wheel_contacts += state.contact_count;
                result.airborne_ticks += state.contact_count == 0 ? 1u : 0u;
                result.boundary_ticks += state.boundary != MotionBoundary::none ? 1u : 0u;
                const auto slipping = std::ranges::any_of(motion.surface_effect_vehicle().wheels, [](const auto& wheel) {
                    return std::abs(std::int64_t(wheel.longitudinal_slip)) + std::abs(std::int64_t(wheel.lateral_slip)) > 0x4ccc;
                });
                result.slip_ticks += slipping ? 1u : 0u;
            }
            const auto& state = motion.state();
            if (csv.is_open()) {
                csv << result.tick << ',' << std::uint64_t(result.tick) * tick_ms << ',' << next_point << ',' << session.released() << ',' << session.lap() << ','
                    << session.completed_laps();
                for (const auto coordinate : session_position) {
                    csv << ',' << coordinate;
                }
                for (const auto coordinate : state.body.position) {
                    csv << ',' << coordinate;
                }
                const auto& controls = motion.controls();
                csv << ',' << state.speed_q16 << ',' << int(controls.gear) << ',' << controls.raw_left_q16 << ',' << controls.raw_right_q16 << ',' << controls.raw_throttle_q16
                    << ',' << controls.raw_brake_q16 << ',' << int(state.boundary) << ',' << state.barrier_contact_count << ',' << state.contact_count;
                for (const auto material : state.surface_ids) {
                    csv << ',' << int(material);
                }
                csv << ',' << session.lap_time_ms() << ',' << session.last_lap_ms() << ',' << session.best_lap_ms() << ',' << result.release_tick << ',' << result.advances << ','
                    << result.approach_reversals << ',' << terminal << ',' << result.weather_choice << ',' << result.resolved_weather << ',' << result.slip_ticks << '\n';
            }
            expect(ordered || !session.released(),
                "driver-route failure: unfinished-rollout source AI reversed across a measured-lap boundary; inspect trajectory before attributing a runtime defect");
            expect(releases == (newly_released ? 1u : 0u), "Session release event disagrees with first measured-lap handover");
            expect(motion.ai_controlled(), "CPU source-AI test driver unexpectedly lost control ownership");
            if (state.boundary != MotionBoundary::none) {
                throw std::runtime_error("motion-boundary failure: boundary " + std::to_string(int(state.boundary)) + " at POINTS " + std::to_string(next_point) + " position "
                                         + std::to_string(state.body.position[0]) + ' ' + std::to_string(state.body.position[1]) + ' ' + std::to_string(state.body.position[2]));
            }
            if (terminal) {
                result.lap_ms = session.last_lap_ms();
                expect(result.release_tick != 0 && completions == 1 && session.completed_laps() == 1 && session.lap() == 2 && result.visited == result.points
                           && result.advances == result.points && result.lap_ms != 0 && result.lap_ms == session.best_lap_ms()
                           && result.lap_ms == (result.tick - result.release_tick) * tick_ms && session.lap_time_ms() == 0 && result.measured_distance > 0,
                    "Completed lap must contain every ordered POINTS transition and match exact uninterrupted 40ms timing");
                return result;
            }
            expect(completions == 0, "Session emitted a lap event without a completed lap");
        }
        result.tick = maximum_ticks;
        throw std::runtime_error("driver-timeout: unfinished-rollout source AI did not complete an ordered lap; this alone does not establish a map defect");
    } catch (const std::exception& error) {
        result.failure = error.what();
    }
    return result;
}

void report(const Result& result, std::ostream& output) {
    output << result.track << ',' << names.at(result.track) << ',' << (result.failure.empty() ? "PASS" : "FAIL") << ',' << result.tick << ',' << result.release_tick << ','
           << (result.release_tick == 0 ? 0 : result.tick - result.release_tick) << ',' << result.visited << ',' << result.points << ',' << result.advances << ',' << result.lap_ms
           << ',' << std::fixed << std::setprecision(2) << result.distance << ',' << result.measured_distance << ',' << result.boundary_ticks << ',' << result.barrier_ticks << ','
           << result.barrier_contacts << ',' << result.wheel_contacts << ',' << result.airborne_ticks << ',' << result.approach_reversals << ',' << std::quoted(result.failure)
           << ',' << result.weather_choice << ',' << result.resolved_weather << ',' << result.slip_ticks << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc >= 2, "Usage: toca-track-lap-tests CANONICAL_ROOT [OUTPUT_DIRECTORY [TRACK_ID]] [--weather sunny|rain|snow]");
        const AssetStore store(argv[1]);
        std::filesystem::path output;
        std::uint32_t first = 0, last = std::uint32_t(scene::track_catalog.size());
        std::uint32_t weather = 1, positional = 0;
        bool explicit_weather = false;
        for (int index = 2; index < argc; ++index) {
            const std::string_view value(argv[index]);
            if (value == "--weather") {
                expect(!explicit_weather && ++index < argc, "--weather requires one sunny, rain or snow value");
                const std::string_view choice(argv[index]);
                expect(choice == "sunny" || choice == "rain" || choice == "snow", "--weather must be sunny, rain or snow");
                weather = choice == "rain" ? 3u : choice == "snow" ? 4u : 1u;
                explicit_weather = true;
            } else if (positional == 0) {
                expect(!value.starts_with("--"), "Unknown track-lap option");
                output = value;
                ++positional;
            } else {
                expect(positional == 1, "Expected only OUTPUT_DIRECTORY and optional TRACK_ID positional arguments");
                const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), first);
                expect(error == std::errc{} && end == value.data() + value.size() && scene::track_supported(first), "TRACK_ID must be 0..9");
                last = first + 1;
                ++positional;
            }
        }
        std::ofstream summary;
        if (!output.empty()) {
            std::filesystem::create_directories(output);
            summary.exceptions(std::ios::failbit | std::ios::badbit);
            const auto stem = positional == 2 ? "summary-track-" + std::to_string(first) : "summary";
            summary.open(output / (stem + (explicit_weather ? "-weather-" + std::to_string(weather) : "") + ".csv"));
        }
        constexpr auto header =
            "track,name,result,ticks,release_tick,measured_ticks,visited_points,route_points,ordered_advances,lap_ms,total_distance_m,measured_distance_m,"
            "boundary_ticks,barrier_ticks,barrier_contacts,wheel_contacts,airborne_ticks,approach_reversals,failure,weather_choice,resolved_weather,slip_ticks\n";
        std::cout << header;
        if (summary.is_open()) {
            summary << header;
        }
        std::uint32_t failures = 0;
        for (auto track = first; track < last; ++track) {
            const auto result = traverse(store, track, weather, output);
            report(result, std::cout);
            std::cout.flush();
            if (summary.is_open()) {
                report(result, summary);
                summary.flush();
            }
            failures += !result.failure.empty() ? 1u : 0u;
        }
        expect(failures == 0, "Canonical track lap acceptance failed; inspect per-track summary/trace, distinguishing test-driver failure from Motion coverage");
        std::cout << "PASS: continuous original Honda Motion/HPC/POINTS/barrier laps; unfinished-rollout source AI used explicitly as CPU test input\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
