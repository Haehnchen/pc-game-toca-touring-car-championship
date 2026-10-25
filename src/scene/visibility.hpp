#pragma once

#include "formats/terrain.hpp"
#include "scene/camera.hpp"

#include <memory>

namespace toca::scene {
constexpr std::uint16_t no_visibility_sector = 0xffff;
constexpr std::uint32_t no_visibility_entry = UINT32_MAX;

struct VisibilityBounds {
    float minimum_x = 0, minimum_z = 0, maximum_x = 0, maximum_z = 0;
};

struct VisibilitySector {
    std::array<std::int32_t, 2> center{};
    VisibilityBounds bounds;
    std::uint32_t ground_chunk = no_visibility_entry;
    // 47de00 prepends static registrations; these are already in draw order.
    std::vector<std::uint32_t> static_entries;
};

struct VisibilityData {
    std::vector<VisibilitySector> sectors;
    std::int32_t minimum_x = 0, maximum_z = 0, half_size = 0;
    std::uint32_t width = 0, height = 0;

    std::uint16_t sector(const std::array<std::int32_t, 3>& position) const;
};

// Registered object+120/+122/+124/+126 consumed by42bc70. Neighbor IDs
// are raw source shorts; edge neighbors are not clamped to the terrain grid.
struct RootSectorRegistration {
    std::uint16_t sector = no_visibility_sector;
    std::array<std::uint16_t, 3> neighbors{no_visibility_sector, no_visibility_sector, no_visibility_sector};
};

// CPU source47de00/47f500/47e3c0 registration, independently of selection or
// rendering. Initialization truncates LOCAL translation; updates ROUND WORLD
// coordinates for the root sector and separately truncate negative coordinates
// for the strict4.5-unit neighbor thresholds. Hidden/outside roots retain state.
RootSectorRegistration initial_root_registration(const VisibilityData&, const std::array<float, 3>& local_position);
void update_root_registration(const VisibilityData&, RootSectorRegistration&, const std::array<float, 3>& world_position, std::uint8_t manual_mask = 15);

struct VisibilityCamera {
    std::array<float, 16> world_to_view{};
    std::array<float, 3> world_position{};
    Projection projection{};
    std::uint8_t view_mask = 1;
};

// Full affine inversion, retaining float world translation for the different
// ROUND (grid) and __ftol (detail) consumers in47d3d0.
VisibilityCamera visibility_camera(const std::array<float, 16>& world_to_view, const Projection& projection);
// 47cf40: horizontal clip walls only, including its edge-crossing test. The
// actual projection.x supplies 4:3/16:9/16:10 coverage. No Y/near/far AABB test.
bool visibility_bounds_in_view(const VisibilityBounds& bounds, const VisibilityCamera& camera);
std::uint8_t visibility_root_limit(std::uint32_t id_flags);

struct VisibilityDrawEntry {
    std::uint32_t id = 0;
    std::uint16_t sector = no_visibility_sector;
    bool dynamic = false;
};

struct VisibilityFrame {
    std::vector<std::uint16_t> ordered_sectors, terrain_sectors;
    std::vector<std::uint8_t> detail, selected, root_masks;
    // After the terrain /442500 skid /498430 shadow passes: each selected
    // sector's static entries, then its dynamic roots passing the strict LOD.
    std::vector<VisibilityDrawEntry> draws;
    bool all_sectors = false;

    // 47db00: an empty selection admits effects; invalid sectors are bounded.
    bool sector_visible(std::uint16_t sector) const;
    // 47f440/47f4d0 camera mask, before489069's separate root LOD gate.
    std::uint8_t root_mask(std::uint32_t id) const;
};

// Own one instance per scene. Selection retains detail bytes for sectors that
// 47dd90 forces without reclassifying, and moving roots retain linked-list order.
// Explicit simulation-tick selection can feed effects independently of UI FPS;
// this class consumes neither RNG nor simulation time.
class Visibility {
    struct Root {
        std::uint16_t sector = no_visibility_sector;
        std::uint8_t manual_mask = 15;
        std::array<std::uint16_t, 3> neighbors{no_visibility_sector, no_visibility_sector, no_visibility_sector};
    };

    struct Source {
        VisibilityData data;
        std::vector<std::uint8_t> root_limits;
        std::uint32_t static_count = 0;
    };

    // Selection and migration never modify source data. Copies share it until
    // a later allocation explicitly changes the registered scene contents.
    std::shared_ptr<Source> source_ = std::make_shared<Source>();
    std::vector<Root> roots_;
    std::vector<std::vector<std::uint32_t>> sector_roots_;
    VisibilityFrame frame_;
    Source& writable_source();

public:
    explicit Visibility(const Terrain& ground);
    const VisibilityData& data() const;
    // Call in494a40 allocation order (TREES,FENCE,OBJECTS,props,...). One
    // registration per source node; its last valid variant owns the entry.
    // Return one ID per input chunk, UINT32_MAX for overwritten variants.
    std::vector<std::uint32_t> register_static(const Terrain& baked);
    // 47de00 initial LOCAL root translation truncates to Q16. Register direct
    // children of the scene root in source sibling order, excluding ID999 and
    // baked roots. Empty camera anchors are registrations too.
    std::uint32_t register_root(const std::array<float, 3>& local_position, std::uint32_t id_flags, std::uint8_t manual_mask = 15);
    // Initial47de00 link construction, after the caller has all initial body
    // and follow-anchor translations. Positions are in root-ID order.
    void initialize_roots(std::span<const std::array<float, 3>> local_positions);
    // 47f500/47e3c0: ROUND world position; leave old list and append to new.
    // Hidden roots do not update. Once outside the grid,47f500 does not invoke
    // migration again; retaining ffff is deliberate source behavior.
    void update_root(std::uint32_t id, const std::array<float, 3>& world_position, std::uint8_t manual_mask = 15);
    // Execute an observed47e3c0 invocation without repeating47f440's manual gate.
    void migrate_root(std::uint32_t id, const std::array<float, 3>& world_position);
    void set_root_mask(std::uint32_t id, std::uint8_t manual_mask);
    // Reuse the current PVS after source-ordered model changes/migrations.
    void refresh_roots(std::uint8_t view_mask = 1);
    std::uint16_t root_sector(std::uint32_t id) const;
    RootSectorRegistration root_registration(std::uint32_t id) const;
    std::uint8_t root_manual_mask(std::uint32_t id) const;
    const VisibilityFrame& select(const VisibilityCamera& camera, std::span<const std::uint16_t> camera_ancestry_sectors = {}, bool all_sectors = false);
    const VisibilityFrame& frame() const;
};
} // namespace toca::scene
