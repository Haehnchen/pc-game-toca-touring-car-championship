#pragma once

#include "scene/qualifying.hpp"
#include "simulation/controls.hpp"
#include "simulation/rivals.hpp"
#include "simulation/source_snapshot.hpp"

namespace toca::simulation {
// Ordinary sixteen-actor, one-player NoQual460170. Values describe caller
// configuration, never a captured initialized actor or later setup table.
// The fresh cold context has zero inherited raw axes/throttle output and
// local saved controller modes{0,0,0,manual?1:2}. A process-boundary comparator
// must validate those prerequisites;460170 does not reset those entry fields.
// Initial gameplay+8d bindings are cold-zero until401370 runs after sorting.
struct ColdGridInitInputs {
    scene::SceneSetup setup;
    std::uint32_t difficulty = 1, steering_profile = 2, seed = 1;
    bool manual = false;
    std::string player_name = "PLAYER 1";
    std::array<std::int16_t, 16> entry_points{};
};

struct ColdGridActor {
    scene::BodyPose body;
    VehicleState controls;
    std::uint32_t rank = 0, ahead = 0, behind = 0;
    std::uint8_t driver_index = 0; //401370 publishes gameplay+8d after sorting.
    BodyMode mode = BodyMode::distant; //Previous mode before414e20 selection.
    std::int32_t player = -1;
    bool human = false;
    std::uint8_t initialized_support_indices = 0; //410110 writes only the active descending prefix before412040 replaces its first four.
    // Generated publication only. Pointer slots and inherited loader/render
    // bytes have no generated-state claim; compare named initializer writers.
    SourceSnapshot snapshot;
};

struct ColdGridInitResult {
    scene::QualifyingData data;
    RaceGrid grid;
    std::array<ColdGridActor, 16> actors;
    std::array<std::int16_t, 16> points{};
    std::uint32_t rng_final = 0, pole_actor = 0;
    std::uint32_t full_body_budget = 4, dispatch_reset = 0, release_counter = 0;
    std::array<std::int8_t, 16> ownership{};
    //Only934/938/93c/940 are generated here; steering scratch/count tails
    //are inherited process state, not new zero-initialization claims.
    std::array<std::uint32_t, 4> status{0, 1, 0, 0};
    std::array<std::int32_t, 256> pair_handles{};
    std::array<std::uint32_t, 256> pair_touching{};
    std::array<std::uint32_t, 2> ai_setup_resets{}; //4c3b74/78.
    bool clock_first_sample = true;
    //412a80's QPC divisor is not generated without an independently observed
    //QueryPerformanceFrequency. Actual frame/render clocks remain external.
};

struct StartGridInitResult {
    ColdGridInitResult first;
    ColdGridInitResult repeated;
};

//44f0a3 and the first State4 wrapper each call443f60. Unlike460170,
//neither transaction classifies/awards points or consumes classification RNG.
struct QualifyingStartInitResult {
    ColdGridInitResult first;
    ColdGridInitResult repeated;
    std::int16_t camera_heading = 0; //460070 binds gameplay+bc to actor ID before45ea50 reads SGRID.
};

//449f50 ->448790/447980 repeats one460170 using the current process world.
//Actors are the last GENERATED publications, never a new observed grid.
struct WarmGridInitInputs {
    ColdGridInitInputs configuration;
    scene::QualifyingData data;
    std::array<SourceSnapshot, 16> actors;
    std::array<std::int16_t, 16> points{};
};

// Shared401190→412040 placement used by the live rival constructor as well
// as the bounded cold-grid factory. No RNG or observed initialized pose.
scene::BodyPose cold_grid_body(const HpcSurface&, const std::array<scene::GridSlot, 32>&, const RaceGrid&, std::uint32_t actor, std::uint32_t car,
    const scene::InitialBodySupport* retained_support = nullptr, bool current_body = true);
scene::BodyPose cold_qualifying_body(const HpcSurface&, std::span<const std::uint8_t> route, std::uint32_t local_actor, std::uint32_t actor, std::uint32_t car,
    const scene::InitialBodySupport* retained_support = nullptr, bool current_body = true);
QualifyingStartInitResult initialize_qualifying_start(const AssetStore&, const ColdGridInitInputs&);
ColdGridInitResult initialize_cold_grid(const AssetStore&, const ColdGridInitInputs&);
//44f0a3→447980 constructs the first grid. State9's448790→447980 repeats
//460170 before the first421aa0 call. The second transaction uses only the
//generated first identity/RNG/score state; subsequent frame clocks stay outside.
StartGridInitResult initialize_start_grid(const AssetStore&, const ColdGridInitInputs&);
//Shares the cold initializer's authored placement/Controls/AI writers while
//retaining fields outside40a770/401370/412040 and the controller handoffs.
//Only the caller's live stream is advanced; configuration.seed is unused.
ColdGridInitResult initialize_warm_grid(const AssetStore&, const WarmGridInitInputs&, SourceRandom&);
} // namespace toca::simulation
