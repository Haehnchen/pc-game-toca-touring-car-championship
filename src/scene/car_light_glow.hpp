#pragma once

#include "scene/car_lights.hpp"
#include "scene/surface_effects.hpp"

namespace toca::scene {
struct CarLightGroundInput {
    // 4608b0 publishes physical contacts/plane heights into body+464/+42c;
    // 436790 separately selects road surface0 from dynamics+328.
    std::array<SurfaceEffectVector, 4> wheel_position{};
    std::array<std::int32_t, 4> ground_height{};
    std::array<std::uint8_t, 4> surface{};
    // Body+24 is its authored half-height. Body+4f0==1 selects the distant
    // placement branch; it is independent of actual airborne wheel contacts.
    std::int32_t body_height = 0;
    bool distant_body = false;
};

struct CarLightGlowFrame {
    std::array<SurfaceEffectSprite, 2> sprites{};
    std::size_t count = 0;
    std::array<SurfaceEffectSprite, 2> ground_sprites{};
    std::size_t ground_count = 0;
    bool front = false;
    std::int32_t angle_fade_q16 = 0;
};

// 4367f0 ->436c80 ->437640: original HGLOW/BGLOW billboards. The
// physical root supplies camera facing; the selected body/root carries lamps.
// Submit sprites through497070 Flags6, front HGLOW or rear BGLOW. Optional
// ground input adds436790/436c80's displaced, camera-facing wet reflections
// for the separate442210 Flags1 queue, using the same end's texture.
CarLightGlowFrame car_light_glow_frame(std::uint32_t car, std::uint32_t weather, const CarLights& lights, const std::array<float, 16>& physical_root,
    const std::array<float, 16>& lamp_transform, const std::array<std::int32_t, 3>& camera_position, const CarLightGroundInput* ground = nullptr);
} // namespace toca::scene
