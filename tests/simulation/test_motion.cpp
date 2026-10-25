#include "motion_tests/body.hpp"
#include "motion_tests/fixtures.hpp"
#include "motion_tests/snapshot.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace toca::test::motion;

void all_body_checks() {
    BodyFixture fixture;
    fixture.coverage();
    fixture.flight_landing();
    fixture.drivetrain();
    fixture.barriers_materials();
}

void all_snapshot_checks() {
    SnapshotFixture fixture;
    fixture.damage_feedback();
    fixture.handoff_launch();
    fixture.barrier_moment();
    fixture.support_publication();
    fixture.continued_inputs();
}

void synthetic() {
    sine_values();
    cold_distant_effect_gate();
    qualifying_restart();
    fixed_arithmetic();
    all_body_checks();
    handling();
    all_snapshot_checks();
    engine_effect_adapter();
    automatic_reverse();
    brake_lights();
}

struct Case {
    std::string_view name, suite;
    void (*run)();
};

const std::array cases{
    Case{"sine", "numeric", sine_values},
    Case{"fixed-arithmetic", "numeric", fixed_arithmetic},
    Case{"cold-distant-effects", "effects", cold_distant_effect_gate},
    Case{"brake-lights", "effects", brake_lights},
    Case{"engine-effects", "effects", engine_effect_adapter},
    Case{"automatic-reverse", "handling", automatic_reverse},
    Case{"surface-handling", "handling", handling},
    Case{"qualifying-restart", "lifecycle", qualifying_restart},
    Case{"floor-coverage", "body",
        +[] {
            BodyFixture fixture;
            fixture.coverage();
        }},
    Case{"flight-landing", "body",
        +[] {
            BodyFixture fixture;
            fixture.flight_landing();
        }},
    Case{"drivetrain", "body",
        +[] {
            BodyFixture fixture;
            fixture.drivetrain();
        }},
    Case{"barriers-materials", "body",
        +[] {
            BodyFixture fixture;
            fixture.barriers_materials();
        }},
    Case{"snapshot-feedback", "snapshot",
        +[] {
            SnapshotFixture fixture;
            fixture.damage_feedback();
        }},
    Case{"snapshot-handoff-launch", "snapshot",
        +[] {
            SnapshotFixture fixture;
            fixture.handoff_launch();
        }},
    Case{"snapshot-barrier-moment", "snapshot",
        +[] {
            SnapshotFixture fixture;
            fixture.barrier_moment();
        }},
    Case{"snapshot-support-publication", "snapshot",
        +[] {
            SnapshotFixture fixture;
            fixture.support_publication();
        }},
    Case{"snapshot-continuation", "snapshot",
        +[] {
            SnapshotFixture fixture;
            fixture.prepare_continuation();
            fixture.continued_inputs();
        }},
};
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2 && std::string_view(argv[1]) == "--list") {
            for (const auto& test : cases) {
                std::cout << test.suite << '/' << test.name << '\n';
            }
            return 0;
        } else if (argc == 2) {
            canonical(argv[1]);
        } else if (argc == 3 && (std::string_view(argv[1]) == "--suite" || std::string_view(argv[1]) == "--case")) {
            bool matched = false;
            for (const auto& test : cases) {
                if ((std::string_view(argv[1]) == "--suite" ? test.suite : test.name) == argv[2]) {
                    test.run();
                    matched = true;
                }
            }
            if (!matched) {
                throw std::runtime_error("Unknown Motion test selection; use --list");
            }
        } else {
            throw std::runtime_error("Usage: simulation-motion-tests [canonical-root | --suite NAME | --case NAME | --list]");
        }
        if (argc == 3) {
            std::cout << "PASS: Motion " << (std::string_view(argv[1]) == "--case" ? "case " : "suite ") << argv[2] << '\n';
        } else {
            std::cout << "PASS: source wheel/contact forces, body integration, braking and steering\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
