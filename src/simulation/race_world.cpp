#include "simulation/race_world.hpp"

#include "simulation/car_contacts.hpp"
#include "simulation/prop_contacts.hpp"

namespace toca::simulation {
RaceWorldProcessContext fresh_race_world_process() {
    return {};
}

void reset_grid_world(CarContacts& contacts, DamageContext& damage, PropContacts& props) {
    contacts.reset();
    damage.reset_parts();
    props.reset();
}
} // namespace toca::simulation
