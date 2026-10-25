#include "scene/camera.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

bool close(float a, float b) {
    return std::abs(a - b) < 0.00001f;
}

bool same_coverage(const toca::scene::Projection& a, const toca::scene::Projection& b) {
    return a.x == b.x && a.y == b.y && a.near_plane == b.near_plane && a.width == b.width && a.height == b.height;
}

void mega_depth(const toca::scene::Projection& p) {
    require(p.far_plane == 500 && p.depth_scale == 65568.f / 65536 && p.depth_offset == -16392.f / 65536, "Mega must double scene distance with matching fixed-point depth");
}
} // namespace

int main() {
    try {
        using toca::scene::default_projection;
        const auto original = default_projection(3, 640, 480);
        require(original.x == 83886.0f / 65536 && original.y == 104857.0f / 65536, "4:3 projection must retain both original fixed-point scale factors");
        for (std::uint16_t view = 0; view < 6; ++view) {
            const auto p = toca::scene::selected_view_projection(3, 640, 480, view);
            const bool chase = view == 2 || view == 3;
            require(p.x == float(chase ? 83886 : 91750) / 65536 && p.y == float(chase ? 104857 : 114687) / 65536,
                "Each source camera must select its exact0xa3d7/0xb333 fixed projection");
            const auto wide = toca::scene::selected_view_projection(3, 1280, 720, view);
            require(wide.y == p.y && close(wide.x * 1280 / 720, p.x * 640 / 480), "Every camera must preserve vertical widescreen coverage");
            const auto mega = toca::scene::selected_view_projection(4, 640, 480, view), mega_wide = toca::scene::selected_view_projection(4, 1280, 720, view);
            require(same_coverage(mega, p) && same_coverage(mega_wide, wide), "Mega must retain Very High field of view for every selected camera");
            mega_depth(mega);
            mega_depth(mega_wide);
        }
        for (const bool vertical : {false, true}) {
            const auto pane = toca::scene::split_view_projection(3, vertical ? 320 : 640, vertical ? 480 : 240, vertical);
            const auto factor = vertical ? 0x1383a : 0x10000;
            require(pane.y == float(std::int64_t(factor) * 0x28000 / 65536) / 65536, "Split projection lost its original orientation factor");
            require(pane.depth_scale == original.depth_scale && pane.depth_offset == original.depth_offset, "Split screen must retain source scene depth");
            for (const auto size : {std::array<std::uint32_t, 2>{640, 240}, {640, 720}, {640, 800}, {641, 801}}) {
                const auto p = toca::scene::split_view_projection(3, size[0], size[1], vertical);
                require(p.y == pane.y && close(p.x * float(p.width) / float(p.height), pane.x * float(pane.width) / float(pane.height)),
                    "Split panes must correct their own aspect without distorting world scale");
                const auto mega = toca::scene::split_view_projection(4, size[0], size[1], vertical);
                require(same_coverage(mega, p), "Mega must preserve split-screen coverage for both orientations");
                mega_depth(mega);
            }
        }
        constexpr std::array<float, 5> far{150, 150, 200, 250, 500};
        constexpr std::array<int, 5> zscale{65645, 65645, 65618, 65601, 65568};
        constexpr std::array<int, 5> zoffset{-16411, -16411, -16404, -16400, -16392};
        for (std::uint32_t detail = 0; detail < far.size(); ++detail) {
            const auto p = default_projection(detail, 640, 480);
            require(p.near_plane == 0.25f && p.far_plane == far[detail] && p.depth_scale == static_cast<float>(zscale[detail]) / 65536
                        && p.depth_offset == static_cast<float>(zoffset[detail]) / 65536,
                "Detail-specific depth coefficients changed");
            require(std::abs(p.depth_scale + p.depth_offset / p.near_plane) < 4.0f / 65536 && std::abs(p.depth_scale + p.depth_offset / p.far_plane - 1.0f) < 1.0f / 65536,
                "Near/far depth must retain original fixed-point rounding");
        }
        for (const std::array<std::uint32_t, 2> size : {std::array<std::uint32_t, 2>{1280, 720}, {1280, 800}, {1024, 768}}) {
            const auto p = default_projection(3, size[0], size[1]);
            const auto mega = default_projection(4, size[0], size[1]);
            require(same_coverage(mega, p), "Mega must retain Very High coverage at every supported aspect");
            mega_depth(mega);
            require(p.width == size[0] && p.height == size[1] && p.y == original.y && p.depth_scale == original.depth_scale && p.depth_offset == original.depth_offset,
                "Drawable aspect must not change vertical projection or scene depth");
            require(close(p.x * static_cast<float>(p.width) / static_cast<float>(p.height), original.x * 640 / 480),
                "Changing aspect must preserve horizontal pixel scale relative to height");
            if (size[0] == 1280) {
                require(p.x < original.x, "Widescreen must reveal extra horizontal scenery");
            }
        }
        for (const std::array<std::uint32_t, 3> invalid : {std::array<std::uint32_t, 3>{5, 640, 480}, {0, 0, 480}, {0, 640, 0}}) {
            bool rejected = false;
            try {
                (void)default_projection(invalid[0], invalid[1], invalid[2]);
            } catch (const std::runtime_error&) {
                rejected = true;
            }
            require(rejected, "Invalid scene projection inputs must be rejected");
        }
        std::cout << "PASS: original projection/depth and 4:3, 16:9, 16:10 horizontal coverage\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
