#include "scene/sun.hpp"

#include "assets/store.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace toca::scene {
SunResources load_sun_resources(const AssetStore& store, const SceneSetup& setup) {
    SunResources result;
    if (setup.weather != 1) {
        return result;
    }
    const auto archive = store.open_archive("data/image640.bfp");
    for (std::size_t index = 0; index < lens_dimensions.size(); ++index) {
        const auto data = archive.member(40 + index).bytes();
        result.textures.push_back({"LENS" + std::to_string(index + 1) + "320", {data.begin(), data.end()}});
    }
    return result;
}

std::array<float, 3> sun_world_position(std::uint32_t track_id) {
    constexpr std::array<std::array<float, 3>, 10> source{
        {{50, 100, 75}, {100, 200, -40}, {75, 100, 75}, {-60, 100, -45}, {-75, 100, -125}, {50, 100, 75}, {100, 100, 55}, {-175, 200, 110}, {-75, 100, 100}, {1, 1, 1}}};
    if (track_id >= source.size()) {
        throw std::runtime_error("Sun requires original track0..9");
    }
    auto result = source[track_id];
    result[0] *= 32;
    result[1] *= 10;
    result[2] *= 32;
    return result;
}

std::vector<LensQuad> lens_quads(const SunCamera& camera, std::uint32_t track_id) {
    const auto& projection = camera.projection;
    if (!projection.width || !projection.height || !std::isfinite(projection.x) || !std::isfinite(projection.y) || projection.x <= 0 || projection.y <= 0) {
        throw std::runtime_error("Lens projection requires a finite nonempty camera");
    }
    const auto point = sun_world_position(track_id);
    std::array<double, 3> view{};
    for (std::size_t row = 0; row < view.size(); ++row) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            view[row] += double(camera.world_to_view[row + axis * 4]) * point[axis];
        }
        view[row] += camera.world_to_view[12 + row];
        if (!std::isfinite(view[row])) {
            throw std::runtime_error("Lens projection requires finite view coordinates");
        }
    }
    if (view[2] <= 1) {
        return {};
    }
    // Keep the original integer pixel layout in480-line logical coordinates;
    // physical height scales sprites uniformly at widescreen resolutions.
    const auto scale = double(projection.height) / 480.;
    const auto width = double(projection.width) / scale;
    constexpr double height = 480;
    const auto center_x = width / 2, center_y = height / 2;
    const auto projected_x = (double(projection.x) * view[0] / view[2] + 1) * double(projection.width) / 2;
    const auto projected_y = (1 - double(projection.y) * view[1] / view[2]) * double(projection.height) / 2;
    if (std::abs(projected_x) > std::numeric_limits<int>::max() / 8 || std::abs(projected_y) > std::numeric_limits<int>::max() / 8) {
        return {};
    }
    const auto sun_x = double(int(projected_x)) / scale, sun_y = double(int(projected_y)) / scale;
    // 434b30 reports rectangle intersection even when490b90 suppresses
    // the sun itself. There is no scene-depth/occlusion query here.
    if (sun_x + 32 <= 0 || sun_x - 32 >= width || sun_y + 32 <= 0 || sun_y - 32 >= height) {
        return {};
    }
    const auto step_x = int(std::floor((sun_x - center_x) * 0x6666 / 65536.));
    const auto step_y = int(std::floor((sun_y - center_y) * 0x6666 / 65536.));
    std::vector<LensQuad> result;
    for (std::uint32_t index = 0; index < lens_dimensions.size(); ++index) {
        const auto x = index < 2 ? sun_x + step_x * int(index + 1) : center_x - step_x * int(index - 1);
        const auto y = index < 2 ? sun_y + step_y * int(index + 1) : center_y - step_y * int(index - 1);
        const auto side = double(lens_dimensions[index]);
        const auto left = x - side / 2, top = y - side / 2;
        const auto clipped_left = std::max(left, 0.), clipped_top = std::max(top, 0.);
        const auto right = std::min(left + side, width), bottom = std::min(top + side, height);
        if (right <= clipped_left || bottom <= clipped_top) {
            continue;
        }
        // 490cb0 samples a tenth of an original pixel inside each UV boundary.
        result.push_back({index, float(clipped_left * scale), float(clipped_top * scale), float((right - clipped_left) * scale), float((bottom - clipped_top) * scale),
            float((clipped_left - left + .1) / side), float((clipped_top - top + .1) / side), float((right - left + .1) / side), float((bottom - top + .1) / side)});
    }
    return result;
}
} // namespace toca::scene
