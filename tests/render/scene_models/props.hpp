#pragma once

#include "scene/view.hpp"

namespace toca::test::scene_models {
void mega_prop_pixels(gpu::Renderer& renderer, const scene::SceneResources& original, const scene::SceneResources& mega, scene::PropResources scene::SceneResources::* family,
    std::string_view name, int width = 640, int height = 480, bool batch = false);
void mega_static_pixels(gpu::Renderer& renderer, const scene::SceneResources& original, const scene::SceneResources& mega, bool tyres, std::string_view mesh_name = {},
    int width = 640, int height = 480);
} // namespace toca::test::scene_models
