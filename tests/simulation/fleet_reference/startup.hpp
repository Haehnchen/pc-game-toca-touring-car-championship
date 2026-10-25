#pragma once

#include "assets/store.hpp"
#include "record.hpp"
#include "simulation/cold_grid.hpp"

#include <string_view>

namespace toca::test::fleet {
using namespace toca::simulation;

ColdGridInitInputs read_cold_grid_inputs(const char* input_path);
void emit_cold_grid(std::ostream& output, const ColdGridInitInputs& request, const ColdGridInitResult& result, bool sequence, std::string_view phase, bool qualifying = false);
int replay_qualifying_startup(const char* root, const char* input_path, const char* output_path);
void restore_startup_record(Record& record, const ColdGridInitResult& startup, bool first_selection = false);
void prepare_startup_selection_context(Record& record, const ColdGridInitResult& startup);
void emit_startup_selection(std::ostream& output, std::span<const BodySelectionActor, 16> inputs, const BodySelectionResult& result, std::uint32_t before, std::uint32_t after);
void select_startup_bodies(Record& record, const ColdGridInitResult& startup, const SourceRandom& random, std::ostream& output);
void restore_startup_physical_body(Record& physical, const Record& driver, const ColdGridInitResult& startup);
void emit_startup_physical_body(std::ostream& output, const std::array<SourceSnapshot, 16>& restored);
void restore_startup_control_calibration(SourceSnapshot& target, const ColdGridActor& generated);
void emit_startup_control_calibration(std::ostream& output, std::string_view phase, const std::array<SourceSnapshot, 16>& snapshots);
void restore_startup_contact_cache(SourceSnapshot& target, const ColdGridActor& generated);
void emit_startup_contact_cache(std::ostream& output, std::string_view phase, const std::array<SourceSnapshot, 16>& snapshots);
void restore_startup_cold_actor_context(Record& record, const ColdGridInitResult& startup, bool driver_aliases);
void emit_startup_world_reset(std::ostream& output, const std::vector<InitialWorldValue>& initialized, const std::vector<InitialWorldValue>& restored);
WarmGridInitInputs warm_grid_context(
    const AssetStore& assets, const AiInitialState& initial, const Record& first, std::uint32_t track, std::uint32_t local, std::uint32_t weather, std::uint32_t steering);
int replay_cold_grid_initialization(const char* root, const char* input_path, const char* output_path, bool sequence = false);

} // namespace toca::test::fleet
