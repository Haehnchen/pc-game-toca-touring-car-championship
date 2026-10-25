#pragma once

#include "formats/hpc.hpp"
#include "scene/body.hpp"
#include "simulation/damage.hpp"
#include "simulation/fixed.hpp"

#include <functional>
#include <span>

namespace toca::scene {
class BarrierData;
}

namespace toca::simulation {
struct DetachedPart {
    static constexpr std::uint32_t unknown_owner = UINT32_MAX;
    std::uint8_t source_slot = 0, type = 0;
    std::uint32_t car_id = 0;
    std::uint32_t actor_id = 0;
    fixed::Basis basis{}, rotation{};
    // Source centre of rotation, not the mesh origin. Rendering subtracts the
    // type's transformed pivot; the reverse face uses its own mirrored basis.
    fixed::Vector position{}, velocity{};
    std::int32_t initial_y = 0, remaining_ticks = 0;
    std::uint32_t sector = 0;
    std::uint16_t cached_triangle = 0;
    bool active = false, resting = false;
};

struct DamagePartsVehicle {
    scene::BodyPose body;
    fixed::Vector velocity{};
    std::uint32_t car_id = 0;
    bool sector_visible = true;
    std::uint8_t model_detail = 0;
    std::uint32_t actor_id = 0;
    std::uint16_t source_sector = 0;
};

using DamageSurfaceQuery = std::function<std::optional<HpcPropSample>(std::int32_t, std::int32_t, std::optional<std::uint16_t>)>;
using DamageSectorQuery = std::function<std::optional<std::uint32_t>(const fixed::Vector&)>;
using DamageSectorVisible = std::function<bool(std::uint32_t)>;

// 416320/416870/417830/417890: original ten-record pool, flight, landing and
// resting lifetime. Sector visibility is supplied by the scene's existing grid.
class DetachedParts {
    std::array<DetachedPart, 10> parts_{};
    std::array<DetachedPart, 10> previous_parts_{};
    std::array<bool, 10> free_{true, true, true, true, true, true, true, true, true, true};
    std::uint32_t count_ = 0;
    DamageSurfaceQuery surface_;
    DamageSectorQuery sector_;
    DamageSectorVisible visible_;
    std::int32_t gravity_;
    void retire(std::size_t index);
    bool ground(std::size_t index);

public:
    DetachedParts(DamageSurfaceQuery surface, DamageSectorQuery sector, DamageSectorVisible visible, bool low_gravity = false);
    void reset();
    //Diagnostic physics import of416320's entire noncompacting pool. Original
    //records contain no render owner; imported car_id/actor_id use unknown_owner.
    //The source type has a unique inverse source_slot; no mesh identity is guessed.
    void restore_source_state(std::span<const std::uint8_t, 10 * 0x11c> pool, std::span<const std::uint8_t, 40> free_flags, std::uint32_t count);
    void detach(std::uint8_t source_slot, const DamagePartsVehicle& vehicle);
    void advance();
    DamageInput::DebrisStatus status() const;
    std::span<const DetachedPart> parts() const;
    // Interpolate the source rotation centre and basis before mesh pivots.
    // Allocation/reset/import initializes each slot's own presentation history.
    std::array<DetachedPart, 10> presentation_parts(float alpha) const;
    bool free(std::size_t index) const;
};

//419ad0's process globals survive both warm restarts and resource reloads.
//Detached storage and scene callbacks have separate ownership.
struct DamagePersistentState {
    std::uint8_t cycle = 0;
    std::array<DamageSelection, 5> selection{};
};

//419ad0 shares its selection scratch/cycle and ten detached records across
//all actors. A car reset restores only that actor's visual damage variants.
class DamageContext {
    DamageSectorVisible visible_;
    DetachedParts parts_;
    std::uint8_t cycle_ = 0;
    std::array<DamageSelection, 5> selection_{};

public:
    DamageContext(const HpcSurface& surface, const scene::BarrierData* barriers = nullptr, bool low_gravity = false, DamagePersistentState prior = {});
    DamageContext(const DamageContext&) = delete;
    DamageContext& operator=(const DamageContext&) = delete;
    void set_visibility(DamageSectorVisible visible);
    DamageResult apply(DamageState& state, DamageInput input);
    DetachedParts& parts();
    const DetachedParts& parts() const;
    void advance();
    void reset_parts();
    std::uint8_t cycle() const;
    DamagePersistentState persistent_state() const;
    //Diagnostic import; detached records require their own supported state.
    void restore_source_state(std::uint32_t cycle, std::span<const std::uint8_t, 15> selection);
    void restore_source_state(std::uint32_t cycle, std::span<const std::uint8_t, 15> selection, std::span<const std::uint8_t, 10 * 0x11c> pool,
        std::span<const std::uint8_t, 40> free_flags, std::uint32_t count);
};
} // namespace toca::simulation
