#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace toca {
class AssetStore;
}

namespace toca::frontend {
enum class MenuRegion : std::size_t {
    content,
    title,
    help
};

struct MenuPoint {
    float x = 0, y = 0;
};

struct MenuQuad {
    // Canonical order: top left, bottom left, bottom right, top right.
    std::array<MenuPoint, 4> corners;
};

struct MenuTransitionRegion {
    float x, y, width, height;
};

// FUN_00471ca3's source scan windows, translated from its 598x448 buffer
// into the stable 640x480 canvas by FUN_0046ee06's (33,15) origin.
inline constexpr std::array<MenuTransitionRegion, 3> menu_transition_regions{{{33, 61, 574, 360}, {49, 15, 550, 39}, {57, 441, 534, 12}}};

struct MenuTransitionResources {
    std::array<std::array<MenuQuad, 3>, 14> frames;
};

struct MenuTransitionPhase {
    std::uint32_t frame = 0, next_frame = 0;
    float fraction = 0;
    bool active = false;
};

MenuTransitionResources load_menu_transition_resources();
// Normalized coordinates in one of the three source scan windows. The
// original interpolates scanline edges linearly, rather than projecting 3D.
MenuPoint menu_transition_point(const MenuQuad& quad, float u, float v);
// At elapsed 0 retain the initial frame (0, or 5 during startup). Each active
// 30Hz tick advances one frame; a normal roll has 14 ticks, startup has 9.
MenuTransitionPhase menu_transition_phase(std::uint64_t elapsed_ns, int direction, bool startup = false);
} // namespace toca::frontend
