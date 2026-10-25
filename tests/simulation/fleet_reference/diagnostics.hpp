#pragma once

#include "record.hpp"
#include "retained_state.hpp"
#include "simulation/motion.hpp"
#include "simulation/race_release.hpp"

#include <string>

namespace toca::test::fleet {
using namespace toca::simulation;

struct Column {
    std::string name;
    std::int64_t value;
};

bool valid_observed_ai_tire_offset(std::int32_t offset, std::int32_t previous, std::int32_t base);
void validate_configuration(const SourceSnapshot& initial, const SourceSnapshot& current, std::uint32_t id, std::uint32_t actor, std::uint32_t local);
std::vector<Column> columns(const Motion& motion);
void copy_physical_fields(SourceSnapshot& destination, const SourceSnapshot& source);
void reject_physical_fields(const SourceSnapshot& source);
void reject_physical_world(const Record& record, bool cold_world = false);
void reject_scene_inputs(const Record& record);
void physical_feedback_columns(std::ostream& output, const SourceSnapshot& source, bool header);

} // namespace toca::test::fleet
