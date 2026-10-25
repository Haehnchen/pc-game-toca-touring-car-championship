#include "fleet_reference/options.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace toca::test::fleet;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint16_t option_bits(const CombinedReplayOptions& options) {
    const std::array flags{options.drivetrain_feedback, options.rpm_feedback, options.limiter_feedback, options.pedal_feedback, options.control_feedback, options.physical_feedback,
        options.dispatch_feedback, options.startup_selection, options.startup_physical_body, options.startup_control_calibration, options.startup_contact_cache,
        options.startup_world_reset};
    std::uint16_t result = 0;
    for (std::size_t bit = 0; bit < flags.size(); ++bit) {
        result |= std::uint16_t(unsigned(flags[bit]) << bit);
    }
    return result;
}

void profiles() {
    struct Expected {
        const char* flag;
        std::size_t argc;
        std::uint16_t bits;
        unsigned output, physical, startup, clock; // Zero denotes no optional input.
        bool scene_owned = false, scene_lifecycle = false;
    };

    // Frozen vectors from the original CLI call sites: seven feedback flags,
    // then selection/BODY/calibration/cache/world, followed by original argv slots.
    constexpr std::array expected{
        Expected{"--compare-startup-join", 8, 0x000, 6, 0, 7, 0},
        Expected{"--compare-startup-first-selection", 8, 0x080, 6, 0, 7, 0},
        Expected{"--compare-startup-physical-body", 9, 0x1ff, 7, 6, 8, 0},
        Expected{"--compare-startup-control-calibration", 9, 0x3ff, 7, 6, 8, 0},
        Expected{"--compare-startup-contact-cache", 9, 0x7ff, 7, 6, 8, 0},
        Expected{"--compare-startup-world-reset", 10, 0xfff, 7, 6, 8, 9},
        Expected{"--compare-startup-scene", 10, 0xfff, 7, 6, 8, 9, true, false},
        Expected{"--compare-startup-scene-lifecycle", 10, 0xfff, 7, 6, 8, 9, true, true},
        Expected{"--compare-dispatch-feedback", 8, 0x07f, 7, 6, 0, 0},
        Expected{"--compare-physical-feedback", 8, 0x03f, 7, 6, 0, 0},
        Expected{"--compare-control-feedback", 8, 0x01f, 7, 6, 0, 0},
        Expected{"--compare-pedal-feedback", 8, 0x00f, 7, 6, 0, 0},
        Expected{"--compare-limiter-feedback", 8, 0x007, 7, 6, 0, 0},
        Expected{"--compare-rpm-feedback", 8, 0x003, 7, 6, 0, 0},
        Expected{"--compare-drivetrain-feedback", 8, 0x001, 7, 6, 0, 0},
        Expected{"--compare-driver-physics", 8, 0x000, 7, 6, 0, 0},
        Expected{"--compare-combined-rng", 7, 0x000, 6, 0, 0, 0},
    };
    for (const auto& profile : expected) {
        const std::array arguments{"replay", profile.flag, "root-2", "track-3", "fleet-4", "journal-5", "slot-6", "slot-7", "slot-8", "slot-9"};
        const auto request = combined_replay_arguments(std::span(arguments).first(profile.argc));
        expect(request.has_value(), "An existing combined replay CLI profile was lost");
        expect(option_bits(request->options) == profile.bits, "A CLI profile changed its exact feedback/startup flags");
        expect(request->options.scene_owned == profile.scene_owned && request->options.scene_lifecycle == profile.scene_lifecycle,
            "A scene replay CLI profile changed ownership or lifecycle scope");
        const auto& paths = request->paths;
        expect(paths.canonical_root == arguments[2] && paths.track_id_text == arguments[3] && paths.fleet == arguments[4] && paths.journal == arguments[5],
            "A combined CLI profile changed its canonical/track/fleet/journal argument binding");
        expect(paths.output_prefix == arguments[profile.output] && paths.physical == (profile.physical ? arguments[profile.physical] : nullptr)
                   && paths.startup == (profile.startup ? arguments[profile.startup] : nullptr) && paths.clock == (profile.clock ? arguments[profile.clock] : nullptr),
            "A combined CLI profile changed its output or optional physical/startup/clock argument binding");
        validate_combined_replay_options(paths, request->options);
        // Incorrect counts must retain the old dispatcher fallthrough, rather
        // than reinterpret an output or another command's arguments as input.
        for (std::size_t count = 0; count <= arguments.size(); ++count) {
            if (count != profile.argc) {
                expect(!combined_replay_arguments(std::span(arguments).first(count)), "A combined CLI profile accepted a previously unsupported argument count");
            }
        }
    }
    const std::array other{"replay", "--compare-skid-dispatch", "root", "track", "fleet", "journal", "output"};
    expect(!combined_replay_arguments(other), "Non-combined replay commands must remain available to their existing dispatcher");
}

void rejected(const CombinedReplayPaths& paths, const CombinedReplayOptions& options, std::string_view message) {
    try {
        validate_combined_replay_options(paths, options);
    } catch (const std::runtime_error& error) {
        expect(error.what() == message, "A startup dependency changed its original error message or precedence");
        return;
    }
    throw std::runtime_error("An invalid original startup boundary combination was accepted");
}

void startup_boundaries() {
    rejected({.physical = "physical", .startup = "startup"}, {}, "Startup join requires the retained-driver-only interval");
    rejected({}, {.startup_selection = true}, "First startup selection requires generated startup context");
    constexpr auto body_error = "Startup BODY restore requires startup, first selection and retained physical/dispatch chronology";
    rejected({}, {.startup_physical_body = true}, body_error);
    rejected({.physical = "physical", .startup = "startup"}, {.dispatch_feedback = true, .startup_physical_body = true}, body_error);
    rejected({.startup = "startup"}, {.dispatch_feedback = true, .startup_selection = true, .startup_physical_body = true}, body_error);
    rejected({.physical = "physical", .startup = "startup"}, {.startup_selection = true, .startup_physical_body = true}, body_error);
    rejected({}, {.startup_control_calibration = true}, "Startup control calibration requires the startup BODY restore boundary");
    rejected({}, {.startup_contact_cache = true}, "Startup contact cache requires the startup calibration restore boundary");
    rejected({}, {.startup_world_reset = true}, "Startup world reset requires the startup contact cache restore boundary");
    rejected({}, {.scene_lifecycle = true}, "Lifecycle scene replay requires generated scene ownership");
    constexpr auto clock_error = "Startup world reset requires its source clock transaction input";
    rejected({.clock = "clock"}, {}, clock_error);
    CombinedReplayOptions world{.dispatch_feedback = true,
        .startup_selection = true,
        .startup_physical_body = true,
        .startup_control_calibration = true,
        .startup_contact_cache = true,
        .startup_world_reset = true};
    rejected({.physical = "physical", .startup = "startup"}, world, clock_error);
    // Multiple invalid boundaries preserve the first original diagnostic.
    rejected({.physical = "physical", .startup = "startup", .clock = "clock"}, {.startup_selection = true, .startup_world_reset = true},
        "Startup join requires the retained-driver-only interval");
    rejected({.physical = "physical"}, {.startup_selection = true, .startup_physical_body = true}, "First startup selection requires generated startup context");
    // Presence is the existing non-null pointer contract, including empty names;
    // filename validity belongs to the corresponding stream reader.
    validate_combined_replay_options({.physical = "", .startup = "", .clock = ""}, world);
}

void independent_feedback() {
    for (unsigned bits = 0; bits < 128; ++bits) {
        const CombinedReplayOptions options{.drivetrain_feedback = bool(bits & 1),
            .rpm_feedback = bool(bits & 2),
            .limiter_feedback = bool(bits & 4),
            .pedal_feedback = bool(bits & 8),
            .control_feedback = bool(bits & 16),
            .physical_feedback = bool(bits & 32),
            .dispatch_feedback = bool(bits & 64)};
        validate_combined_replay_options({}, options);
        validate_combined_replay_options({.physical = "physical"}, options);
    }
    // Existing startup checks require dispatch for BODY, but do not require
    // any of the CLI profile's earlier feedback layers.
    validate_combined_replay_options({.physical = "physical", .startup = "startup"}, {.dispatch_feedback = true, .startup_selection = true, .startup_physical_body = true});
}
} // namespace

int main() {
    try {
        profiles();
        startup_boundaries();
        independent_feedback();
        std::cout << "Combined fleet replay profiles preserve paths, feedback stages and startup dependency diagnostics\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
