#pragma once

#include "formats/model.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace toca {
class AssetStore;
}

namespace toca::scene {
class CockpitRoleCache;

struct CockpitResources {
    // 42de80: gloves left/right, dashboard, wiper, steering wheel.
    std::array<Model, 5> models;
    // Session snapshot of compatible parsed wiper/wheel templates only.
    std::shared_ptr<CockpitRoleCache> shared_roles;
};

std::string_view cockpit_archive(std::uint32_t car_id);
// Pass the Mega directory only for detail4; absent/incompatible roles retain
// their complete original models. Both local players use this loader.
CockpitResources load_cockpit_resources(
    const AssetStore& store, std::uint32_t car_id, const std::filesystem::path& mega_models = {}, std::shared_ptr<CockpitRoleCache> shared_roles = {});
// Original side flag1 is physical left-hand drive (+body Z).
bool cockpit_left_hand_drive(std::uint32_t car_id);
bool local_car_visible(std::uint16_t view);

struct CockpitFrame {
    std::array<std::array<float, 16>, 5> transforms;
    std::array<bool, 5> visible{};
};

struct CockpitRoleSnapshot {
    std::uint8_t actual_mask = 0, manual_mask = 0;
    std::array<float, 16> local{}, parent_local{};
};

// Published controller poses use the canonical role models and the caller's
// rendered body. They contain no mesh data or substituted body/world matrices.
struct CockpitSnapshot {
    std::uint32_t car_id = 0;
    std::int16_t wheel_angle = 0, wiper_angle = 0;
    bool duplicate_wiper = false;
    std::array<CockpitRoleSnapshot, 5> roles;
};

// 42c280/42db50 advance only on active cockpit simulation ticks (25 Hz),
// independently from rendering. SceneView owns one with its scene resources.
class Cockpit {
    struct Hand {
        std::array<std::array<std::int32_t, 3>, 3> basis{{{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}}};
        std::array<std::int32_t, 3> position{};
        bool transformed = false;
    };

    std::uint32_t car_id_, weather_;
    std::int16_t wheel_angle_ = 10;
    std::int16_t previous_wheel_angle_ = 10;
    std::int32_t wiper_angle_ = 0x71, wiper_speed_ = 0;
    std::int32_t previous_wiper_angle_ = 0x71;
    std::int32_t wiper_phase_ = 0, wiper_hold_ = 0;
    std::array<Hand, 2> hands_;
    std::array<Hand, 2> previous_hands_;
    std::array<std::int32_t, 3> shift_position_{}, shift_step_{};
    std::uint8_t shift_phase_ = 0;
    std::int32_t shift_count_ = 0, shift_duration_ = 0;

public:
    Cockpit(std::uint32_t car_id, std::uint32_t weather);
    void update(std::int16_t steering_angle);
    // 418cd0 calls42d850 on the local pending-gear commit, in every view.
    void shift();
    std::uint8_t shift_phase() const;
    std::int16_t wheel_angle() const;
    std::int32_t wiper_angle() const;
    // Interpolate local animated values before attaching them to the rendered
    // body. The caller uses alpha1 for pauses, camera cuts and fresh scenes.
    CockpitFrame frame(const std::array<float, 16>& body, bool chun, bool handy, float alpha = 1) const;
    CockpitFrame frame(const std::array<float, 16>& body, const CockpitSnapshot& published) const;
};
} // namespace toca::scene
