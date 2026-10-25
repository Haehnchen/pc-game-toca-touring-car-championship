#include "session/support.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace toca::network::test::session;

struct Case {
    std::string_view suite, name;
    void (*run)();
};

// Preserve the complete run's original scenario order.
constexpr std::array cases{Case{"protocol", "address", address_parsing}, Case{"lobby", "membership", membership}, Case{"lobby", "concurrent-readiness", concurrent_readiness},
    Case{"lobby", "committed-subset", committed_subset_start}, Case{"lobby", "capacity", capacity}, Case{"protocol", "invalid-sender", invalid_sender},
    Case{"protocol", "malformed", malformed_and_unbound}, Case{"protocol", "sequence-readiness", sequence_and_readiness},
    Case{"protocol", "snapshot-binding", client_snapshot_binding}, Case{"link", "host-departure", reliable_host_departure}, Case{"link", "saturation", saturated_peer},
    Case{"link", "adaptive-cadence", adaptive_state_link}, Case{"link", "timeout-cancel", timeout_and_cancel}, Case{"loading", "recovery", loading_recovery},
    Case{"loading", "grid-unwind", loading_grid_unwind}};
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--list") {
            for (const auto& entry : cases) {
                std::cout << entry.suite << ':' << entry.name << '\n';
            }
            return 0;
        }
        std::string_view suite, name;
        for (int index = 1; index < argc; index += 2) {
            const std::string_view option(argv[index]);
            if (index + 1 >= argc || (option != "--suite" && option != "--case")) {
                throw std::runtime_error("Usage: toca-network-session-tests [--list | --suite lobby|protocol|loading|link] [--case NAME]");
            }
            auto& selected = option == "--suite" ? suite : name;
            if (!selected.empty()) {
                throw std::runtime_error("Duplicate network test selector");
            }
            selected = argv[index + 1];
        }
        const auto matches = [&](const Case& entry) {
            return (suite.empty() || entry.suite == suite) && (name.empty() || entry.name == name);
        };
        if (!std::ranges::any_of(cases, matches)) {
            throw std::runtime_error("No network session tests match the requested suite/case");
        }
        for (const auto& entry : cases) {
            if (matches(entry)) {
                entry.run();
                std::cout << "PASS: " << entry.suite << ':' << entry.name << '\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
