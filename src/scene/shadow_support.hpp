#pragma once

#include "scene/body.hpp"
#include "scene/car_shadow.hpp"
#include "scene/setup.hpp"

namespace toca::scene {
// 4608b0's published actor fields consumed by437770, with434f00 sunlight.
CarShadowInput car_shadow_input(const BodyPose& body, const SceneSetup& setup);
} // namespace toca::scene
