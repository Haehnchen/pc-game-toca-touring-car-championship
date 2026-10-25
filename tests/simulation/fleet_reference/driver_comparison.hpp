#pragma once

#include "record.hpp"
#include "retained_state.hpp"
#include "simulation/race_release.hpp"

#include <string>

namespace toca::test::fleet {
using namespace toca::simulation;

void compare_ai_route(const AiInitialState& initial, const Record& record, std::ostream& output, std::ostream* actor_timing, std::ostream* shared_timing,
    std::ostream* finish_output, std::ostream* display_output, AiDrivers* retained_drivers = nullptr, SourceRandom* shared_random = nullptr,
    AiRouteStepResults* retained_results = nullptr, bool retain_driver_state = false, RetainedRaceTiming* retained_timing = nullptr, Record* generated = nullptr);
void compare_traffic_controls(const AiInitialState& initial, const Record& record, std::ostream& traffic_output, std::ostream& traffic_world, std::ostream& control_output,
    std::ostream& control_world, AiDrivers* retained_drivers = nullptr, SourceRandom* shared_random = nullptr, bool retain_driver_state = false, Record* generated = nullptr,
    const RetainedRaceTiming* timing = nullptr);
void compare_joined_ai(const AiInitialState& initial, const Record& record, std::uint32_t local, RaceReleaseCounter& release, std::ostream& route_output,
    std::ostream& progress_output, std::ostream& joined_world, std::ostream& traffic_output, std::ostream& traffic_world, std::ostream& control_output, std::ostream& control_world,
    SourceRandom* shared_random = nullptr, bool warm_first = false, RetainedDrivers* retained = nullptr, std::int32_t* retained_launch = nullptr,
    RetainedRaceTiming* timing = nullptr, std::ostream* actor_timing = nullptr, std::ostream* checkpoint_output = nullptr, std::ostream* finish_output = nullptr,
    std::ostream* display_output = nullptr);

} // namespace toca::test::fleet
