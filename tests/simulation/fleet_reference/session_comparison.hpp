#pragma once

#include "record.hpp"
#include "retained_state.hpp"
#include "simulation/race_release.hpp"

#include <string>

namespace toca::test::fleet {
using namespace toca::simulation;

void write_handoff_header(std::ostream& output);
void compare_local_handoffs(const Record& record, std::ostream& output);
void write_session_timing(const Record& record, const std::array<LapTimingState, 16>& timing_results, const std::array<ActorFinishState, 16>& actor_finishes,
    const CheckpointTimes& checkpoint_times, std::uint32_t timing_flags, const RaceFinishState& finish, const SplitDisplays& displays, std::ostream* actor_timing,
    std::ostream* shared_timing, std::ostream* finish_output, std::ostream* display_output);
void write_retained_local_session(const Record& record, const RetainedRaceTiming& timing, std::uint32_t release_counter, std::ostream& session, std::ostream& controller,
    std::ostream& classification, std::ostream& handoffs);
void prepare_retained_race_timing(Record& record, RetainedRaceTiming& timing);
void compare_session_boundaries(const AiInitialState& initial, const Record& record, std::uint32_t local, std::ostream& actor_output, std::ostream& checkpoint_output,
    std::ostream& finish_output, std::ostream& display_output, std::ostream& session_output, std::ostream& controller_output, std::ostream& classification_output);

} // namespace toca::test::fleet
