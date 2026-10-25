#pragma once

#include "core/game_mode.hpp"
#include "scene/surface_effects.hpp"

namespace toca {
class AssetStore;
}

namespace toca::scene {
struct EngineEffectResources {
    // 4463e0: IMAGE640 member16 BANG2, independent of track/weather.
    std::vector<std::uint8_t> bang_bmp;
};

EngineEffectResources load_engine_effect_resources(const AssetStore& store);

struct EngineEffectVehicle {
    SurfaceEffectVector position{};
    SurfaceEffectBasis basis{{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
    // Original controls+124/+128, not throttle. Previous is captured before
    // 40d3b0/40d5f0 in40b360 and survives the later pose integration.
    std::int32_t engine_speed_q16 = 0, previous_engine_speed_q16 = 0;
    std::uint8_t actor = 0, car_type = 0, visibility = 1;
    std::int8_t gear = 0;
    bool local_player = true;
};

struct EngineEffectSound {
    SurfaceEffectVector position{};
    std::uint32_t kind = 17;
    std::int32_t strength_q16 = 65536;
    bool local_player = true;
    // Permanent source actor identity, independent of visible-list order or
    // a human actor's temporary AI ownership after finishing.
    std::uint8_t actor = 0;
};

struct EngineEffectFrame {
    std::vector<SurfaceEffectSprite> sprites;
    // Original separate c4560 mask array, in lockstep with sprites.
    std::vector<std::uint8_t> visibility;
    // 446f20: hardware flags6, after wet spray and before wheel sprite pools.
    std::uint8_t flags = 6;
    std::vector<EngineEffectSound> sounds;
    std::uint64_t updates = 0, emitted = 0;
    std::uint32_t cooldown = 900;
    std::uint8_t phase = 2;
};

// 4209d0/420bb0/420be0 exhaust flames. There is no damage trigger in this
// consumer; the generic branch follows engine-speed increase and gear state.
class EngineEffects {
    PropFragmentTables& tables_;
    GameMode mode_;
    std::array<bool, 16> active_{};
    std::array<std::uint8_t, 16> age_{};
    EngineEffectFrame frame_;
    std::vector<std::uint8_t> sprite_actors_;
    void next();
    void emit_vehicle(const EngineEffectVehicle&);

public:
    EngineEffects(PropFragmentTables& tables, GameMode mode = GameMode::single_race);
    // 446a80's first420bb0 call, before tyre particles/fragments/wind/HOR.
    void advance();
    // 4608b0 after car transforms and432090/4347d0. Native scheduling calls
    // this once per active25Hz tick before446d30's weather view update.
    void emit(std::span<const EngineEffectVehicle> vehicles);
    const EngineEffectFrame& frame() const;
    EngineEffectFrame presentation_frame(std::span<const EffectPose> poses) const;
    // 4469a0→420b80: clear visible sprites and pending bursts. Cooldown and
    // three-tick phase survive grid reset; new scene setup restarts900/2.
    void reset();
};
} // namespace toca::scene
