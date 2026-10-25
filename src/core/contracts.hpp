#pragma once

#include "core/input_settings.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace toca {

struct VideoSettings {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool fullscreen = true;
    bool vsync = true;
    std::uint32_t fps_limit = 120;

    bool operator==(const VideoSettings&) const = default;
};

struct AudioSettings {
    float master_gain = 1.0f;
    bool muted = false;
    // Original independent 0..100 controls; game_music gates scene callers, not frontend CD02.
    std::uint32_t effects_volume = 100;
    std::uint32_t music_volume = 100;
    bool game_music = true;
    bool stereo_effects = true;

    bool operator==(const AudioSettings&) const = default;
};

inline constexpr std::uint32_t graphics_mega_detail = 4;
inline constexpr std::uint32_t graphics_detail_count = graphics_mega_detail + 1;

struct GraphicsSettings {
    // Original configuration +0x328: low, medium, high, very high; fresh settings use Mega.
    std::uint32_t detail = graphics_mega_detail;
    // Original configuration +0xd8 defaults to Horizontal; the native flag names Vertical explicitly.
    bool split_vertical = false;

    bool operator==(const GraphicsSettings&) const = default;
};

struct Settings {
    VideoSettings video;
    AudioSettings audio;
    GraphicsSettings graphics;
    InputSettings input;
    InputSettings input_player2 = default_player2_input();

    bool operator==(const Settings&) const = default;
};

// Native action slots, not the original control table's numeric bit assignments.
enum class Action : std::uint32_t {
    left = 1u << 0,
    right = 1u << 1,
    up = 1u << 2,
    down = 1u << 3,
    confirm = 1u << 4,
    back = 1u << 5,
    pointer_primary = 1u << 6,
    backspace = 1u << 7,
    gear_up = 1u << 8,
    gear_down = 1u << 9,
    camera_next = 1u << 10,
    camera_rear = 1u << 11,
    camera_trackside = 1u << 12,
    horn = 1u << 13,
};

constexpr std::uint32_t action_mask(Action action) {
    return static_cast<std::uint32_t>(action);
}

// The host adapter owns held state and accumulates edges until one consumer call.
// A quick press/release can set both edge masks; repeated presentation consumes neither.
struct InputSnapshot {
    std::uint64_t timestamp_ns = 0;
    std::uint32_t held = 0;
    std::uint32_t pressed = 0;
    std::uint32_t released = 0;
    bool any_key_pressed = false;
    std::string text_input;
    std::optional<PhysicalBinding> physical_binding = std::nullopt;
    std::optional<AnalogControls> analog = std::nullopt;
    float pointer_x = 0;
    float pointer_y = 0;
    bool pointer_inside = false;
    bool focused = true;
    bool quit_requested = false;
};

// Monotonic host/replay time. This contract imposes no simulation tick frequency.
struct ClockStep {
    std::uint64_t timestamp_ns = 0;
    std::uint64_t elapsed_ns = 0;
};

enum class GameEventKind : std::uint32_t {
    startup,
    focus_changed,
    play_sound,
    shutdown,
};

// Producer assigns strictly increasing sequence numbers; consumers preserve that order.
// value is a native sample identity for play_sound and 0/1 for focus_changed.
struct GameEvent {
    std::uint64_t sequence = 0;
    std::uint64_t timestamp_ns = 0;
    GameEventKind kind = GameEventKind::startup;
    std::uint32_t value = 0;
};

} // namespace toca
