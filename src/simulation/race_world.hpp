#pragma once

#include "simulation/damage_parts.hpp"
#include "simulation/surface_roughness.hpp"

namespace toca::simulation {
class CarContacts;
class PropContacts;

// PE process defaults, shared by every actor and retained across scene reloads.
// A grid reset is deliberately independent of this process-lifetime context.
struct RaceWorldProcessContext {
    SurfaceRoughness roughness;
    DamagePersistentState damage;
};

RaceWorldProcessContext fresh_race_world_process();

// 44ffe1: 4287f0 pair state, 415d50 detached pool, then 45c4c0 registered
// props. Actor repair and its RNG chronology belong to the grid actor factory.
void reset_grid_world(CarContacts& contacts, DamageContext& damage, PropContacts& props);
} // namespace toca::simulation
