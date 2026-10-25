#include "options.hpp"

#include <array>
#include <stdexcept>
#include <string_view>

namespace toca::test::fleet {
void validate_combined_replay_options(const CombinedReplayPaths& paths, const CombinedReplayOptions& options) {
    if (paths.startup && paths.physical && !options.startup_physical_body) {
        throw std::runtime_error("Startup join requires the retained-driver-only interval");
    }
    if (options.startup_selection && !paths.startup) {
        throw std::runtime_error("First startup selection requires generated startup context");
    }
    if (options.startup_physical_body && (!paths.startup || !options.startup_selection || !paths.physical || !options.dispatch_feedback)) {
        throw std::runtime_error("Startup BODY restore requires startup, first selection and retained physical/dispatch chronology");
    }
    if (options.startup_control_calibration && !options.startup_physical_body) {
        throw std::runtime_error("Startup control calibration requires the startup BODY restore boundary");
    }
    if (options.startup_contact_cache && !options.startup_control_calibration) {
        throw std::runtime_error("Startup contact cache requires the startup calibration restore boundary");
    }
    if (options.startup_world_reset && !options.startup_contact_cache) {
        throw std::runtime_error("Startup world reset requires the startup contact cache restore boundary");
    }
    if (options.scene_owned && !options.startup_world_reset) {
        throw std::runtime_error("Generated scene replay requires the complete cold physical world and clock constructors");
    }
    if (options.scene_lifecycle && !options.scene_owned) {
        throw std::runtime_error("Lifecycle scene replay requires generated scene ownership");
    }
    if ((paths.clock != nullptr) != options.startup_world_reset) {
        throw std::runtime_error("Startup world reset requires its source clock transaction input");
    }
}

std::optional<CombinedReplayRequest> combined_replay_arguments(std::span<const char* const> arguments) {
    enum class Feedback {
        none,
        drivetrain,
        rpm,
        limiter,
        pedal,
        control,
        physical,
        dispatch
    };
    enum class Startup {
        none,
        join,
        selection,
        body,
        calibration,
        cache,
        world
    };

    struct Profile {
        std::string_view flag;
        std::size_t argument_count;
        Feedback feedback;
        Startup startup;
        bool physical;
        bool scene_owned = false;
        bool scene_lifecycle = false;
    };

    constexpr std::array profiles{
        Profile{"--compare-startup-join", 8, Feedback::none, Startup::join, false},
        Profile{"--compare-startup-first-selection", 8, Feedback::none, Startup::selection, false},
        Profile{"--compare-startup-physical-body", 9, Feedback::dispatch, Startup::body, true},
        Profile{"--compare-startup-control-calibration", 9, Feedback::dispatch, Startup::calibration, true},
        Profile{"--compare-startup-contact-cache", 9, Feedback::dispatch, Startup::cache, true},
        Profile{"--compare-startup-world-reset", 10, Feedback::dispatch, Startup::world, true},
        Profile{"--compare-startup-scene", 10, Feedback::dispatch, Startup::world, true, true},
        Profile{"--compare-startup-scene-lifecycle", 10, Feedback::dispatch, Startup::world, true, true, true},
        Profile{"--compare-dispatch-feedback", 8, Feedback::dispatch, Startup::none, true},
        Profile{"--compare-physical-feedback", 8, Feedback::physical, Startup::none, true},
        Profile{"--compare-control-feedback", 8, Feedback::control, Startup::none, true},
        Profile{"--compare-pedal-feedback", 8, Feedback::pedal, Startup::none, true},
        Profile{"--compare-limiter-feedback", 8, Feedback::limiter, Startup::none, true},
        Profile{"--compare-rpm-feedback", 8, Feedback::rpm, Startup::none, true},
        Profile{"--compare-drivetrain-feedback", 8, Feedback::drivetrain, Startup::none, true},
        Profile{"--compare-driver-physics", 8, Feedback::none, Startup::none, true},
        Profile{"--compare-combined-rng", 7, Feedback::none, Startup::none, false},
    };
    if (arguments.size() < 2) {
        return std::nullopt;
    }
    for (const auto& profile : profiles) {
        if (arguments.size() != profile.argument_count || arguments[1] != profile.flag) {
            continue;
        }
        CombinedReplayRequest request;
        request.paths = {.canonical_root = arguments[2],
            .track_id_text = arguments[3],
            .fleet = arguments[4],
            .journal = arguments[5],
            .output_prefix = arguments[profile.physical ? 7 : 6],
            .physical = profile.physical ? arguments[6] : nullptr,
            .startup = profile.startup != Startup::none ? arguments[profile.physical ? 8 : 7] : nullptr,
            .clock = profile.startup == Startup::world ? arguments[9] : nullptr};
        request.options = {.drivetrain_feedback = profile.feedback >= Feedback::drivetrain,
            .rpm_feedback = profile.feedback >= Feedback::rpm,
            .limiter_feedback = profile.feedback >= Feedback::limiter,
            .pedal_feedback = profile.feedback >= Feedback::pedal,
            .control_feedback = profile.feedback >= Feedback::control,
            .physical_feedback = profile.feedback >= Feedback::physical,
            .dispatch_feedback = profile.feedback >= Feedback::dispatch,
            .startup_selection = profile.startup >= Startup::selection,
            .startup_physical_body = profile.startup >= Startup::body,
            .startup_control_calibration = profile.startup >= Startup::calibration,
            .startup_contact_cache = profile.startup >= Startup::cache,
            .startup_world_reset = profile.startup >= Startup::world,
            .scene_owned = profile.scene_owned,
            .scene_lifecycle = profile.scene_lifecycle};
        return request;
    }
    return std::nullopt;
}
} // namespace toca::test::fleet
