#pragma once

#include <optional>
#include <span>

namespace toca::test::fleet {
struct CombinedReplayPaths {
    const char* canonical_root = nullptr;
    const char* track_id_text = nullptr;
    const char* fleet = nullptr;
    const char* journal = nullptr;
    const char* output_prefix = nullptr;
    const char* physical = nullptr;
    const char* startup = nullptr;
    const char* clock = nullptr;
};

struct CombinedReplayOptions {
    bool drivetrain_feedback = false;
    bool rpm_feedback = false;
    bool limiter_feedback = false;
    bool pedal_feedback = false;
    bool control_feedback = false;
    bool physical_feedback = false;
    bool dispatch_feedback = false;
    bool startup_selection = false;
    bool startup_physical_body = false;
    bool startup_control_calibration = false;
    bool startup_contact_cache = false;
    bool startup_world_reset = false;
    bool scene_owned = false;
    bool scene_lifecycle = false;
};

struct CombinedReplayRequest {
    CombinedReplayPaths paths;
    CombinedReplayOptions options;
};

// These are the existing startup boundary requirements. Feedback flags may be
// selected independently; only the CLI's named profiles form cumulative stages.
void validate_combined_replay_options(const CombinedReplayPaths& paths, const CombinedReplayOptions& options);

// An unrecognized flag or argument count remains available to the other replay
// commands. All path pointers retain the caller's argument-storage lifetime.
std::optional<CombinedReplayRequest> combined_replay_arguments(std::span<const char* const> arguments);
} // namespace toca::test::fleet
