#include "frontend/selection_resources.hpp"

#include "frontend/builtin_data.hpp"

#include <stdexcept>

namespace toca::frontend {
SelectionResources load_selection_resources() {
    SelectionResources result;
    result.cars = builtin::cars();
    for (std::uint32_t language = 0; language < result.labels.size(); ++language) {
        auto& labels = result.labels[language];
        labels.title = builtin::translated(builtin::TextId::select_car, language) + " 1";
        labels.transmission_help = builtin::translated(builtin::TextId::press_up_to_toggle_transmission, language);
        labels.unavailable_help = builtin::translated(builtin::TextId::you_cannot_select_this_car, language);
        labels.unknown = builtin::translated(builtin::TextId::unknown_vehicle, language);
        labels.name_title = builtin::translated(builtin::TextId::enter_name, language);
        labels.name_prompt = labels.name_title + " " + builtin::translated(builtin::TextId::player, language) + " 1";
        labels.name_help = builtin::translated(builtin::TextId::enter_player_name, language);
        labels.manual = builtin::translated(builtin::TextId::manual, language);
        labels.automatic = builtin::translated(builtin::TextId::auto_label, language);
        labels.drive = builtin::translated(builtin::TextId::drive, language);
        labels.valves = builtin::translated(builtin::TextId::valves, language);
        labels.power = builtin::translated(builtin::TextId::bhp, language);
        labels.rpm = builtin::translated(builtin::TextId::rpm, language);
        for (std::uint32_t type = 0; type < labels.drive_types.size(); ++type) {
            labels.drive_types[type] = builtin::translated(builtin::drive_type_texts[type], language);
        }
    }
    return result;
}

std::uint32_t garage_variant(std::uint32_t car, bool car_8_available, bool car_9_available) {
    if (car < 7) {
        return 1;
    }
    if (car > 9) {
        throw std::runtime_error("Garage car ID is outside the original roster");
    }
    return car_8_available ? car_9_available ? 4 : 2 : car_9_available ? 3 : 1;
}

} // namespace toca::frontend
