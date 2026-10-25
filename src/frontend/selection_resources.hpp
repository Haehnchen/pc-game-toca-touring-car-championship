#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace toca::frontend {
struct CarInformation {
    std::string name;
    // Drive: front/rear/four wheels; RPM is the speed at quoted power (0 = unavailable).
    std::uint32_t drive = 0, valves = 0, rpm = 0;
    std::string power; // British horsepower, or the original unavailable marker.
};

struct SelectionLabels {
    std::string title, transmission_help, unavailable_help, unknown, manual, automatic, drive, valves, power, rpm;
    std::string name_title, name_prompt, name_help;
    std::array<std::string, 3> drive_types;
};

struct SelectionResources {
    std::array<CarInformation, 10> cars;
    std::array<SelectionLabels, 5> labels;
};

SelectionResources load_selection_resources();
// 475dcb/475bb5 select a replacement garage for IDs7..9 according to
// the two independently earned/overridden bonus-car unlocks.
std::uint32_t garage_variant(std::uint32_t car, bool car_8_available, bool car_9_available);
} // namespace toca::frontend
