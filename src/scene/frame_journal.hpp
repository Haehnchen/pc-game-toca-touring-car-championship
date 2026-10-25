#pragma once

#include "scene/fleet_scene.hpp"

#include <optional>

namespace toca::scene {
enum class SceneJournalRootRole : std::uint32_t {
    external,
    car_parent,
    follow_anchor,
    cockpit_anchor,
    authored_camera
};

struct SceneJournalRoot {
    SceneJournalRootRole role = SceneJournalRootRole::external;
    std::uint32_t actor = 0, flags = 0;
    std::array<float, 3> local_position{};
    // Arbitrary external controllers remain explicit. Authored actors have
    //their own typed source identity; car masks are generated.
    std::uint8_t external_manual_mask = 15;
};

struct SceneJournalModels {
    std::uint32_t local_actor = 0, graphics_detail = 3, view = 2;
    bool authored_camera = false;
    std::int32_t view_factor_q16 = 0;
    // Physical inputs at this scene boundary, not autonomous-physics evidence.
    std::array<std::array<std::int32_t, 3>, 16> body_positions{};
    std::array<std::uint8_t, 16> body_modes{};
    std::array<std::int32_t, 3> local_forward{65536, 0, 0};
};

struct SceneJournalCarMasks {
    std::uint8_t car = 0, high_medium = 0, full = 0;
};

// Executes actual47de00/47e3c0/47d3d0/4271d0 operations. Orders are source
// invocation order, independent of physical-update counts or native deadlines.
// Canonical terrain/static registrations are supplied by the resource owner.
class SceneFrameJournal {
    Visibility visibility_;
    FleetScene fleet_;
    std::vector<SceneJournalRoot> roots_;
    std::vector<std::array<float, 3>> root_positions_;
    std::array<std::uint32_t, 16> car_roots_{};
    std::uint32_t follow_root_ = no_visibility_entry;
    std::uint32_t cockpit_root_ = no_visibility_entry, selected_root_ = no_visibility_entry;
    VisibilityCamera selected_camera_;
    std::optional<std::uint64_t> last_order_;
    std::optional<SceneJournalModels> models_;
    FleetSceneContext context_;
    std::array<SceneJournalCarMasks, 16> masks_{};
    bool initialized_ = false, selected_ = false, published_ = false;
    void advance(std::uint64_t order);

public:
    explicit SceneFrameJournal(Visibility visibility);
    void initialize(std::uint64_t order, std::span<const SceneJournalRoot> roots);
    void migrate(std::uint64_t order, std::uint32_t root, const std::array<float, 3>& world_position);
    //443f60 hides all but the local actor quarter;460170 restores every
    //physical parent's manual bit. Neither writer reconstructs mesh LOD.
    void reset_parent_masks(std::uint64_t order, std::optional<std::uint32_t> qualifying_local = {});
    void external_mask(std::uint64_t order, std::uint32_t root, std::uint8_t mask);
    void select(std::uint64_t order, const VisibilityCamera&, std::span<const std::uint32_t> ancestor_roots, bool all_sectors = false);
    void models(std::uint64_t order, const SceneJournalModels&);
    void publish(std::uint64_t order);
    RootSectorRegistration root_registration(std::uint32_t root) const;
    std::uint8_t root_manual_mask(std::uint32_t root) const;
    std::uint8_t model_detail(std::uint32_t actor) const;
    const VisibilityFrame& visibility() const;
    const FleetSceneContext& context() const;
    const std::array<SceneJournalCarMasks, 16>& car_masks() const;
};
} // namespace toca::scene
