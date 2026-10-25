#include "scene/visibility.hpp"
#include "simulation/contact_candidates.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace toca;
using namespace toca::scene;
using namespace toca::simulation;
constexpr auto absent = no_visibility_sector;
constexpr std::array<std::uint16_t, 3> none{absent, absent, absent};

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

Terrain grid() {
    Terrain result;
    for (std::int32_t row = 0; row < 3; ++row) {
        for (std::int32_t column = 0; column < 3; ++column) {
            result.sector_centers.push_back({column * 64 * 65536, (2 - row) * 64 * 65536});
        }
    }
    return result;
}

void registration() {
    Visibility visibility(grid());
    const auto& data = visibility.data();
    auto registration = initial_root_registration(data, {64, 0, 64});
    expect(registration.sector == 4 && registration.neighbors == none, "Initial47de00 registration must establish the local root sector without synthesizing neighbor slots");
    update_root_registration(data, registration, {91.5f, 0, 91.5f});
    expect(registration.sector == 4 && registration.neighbors == none, "Exact4.5-unit neighbor margin equality is excluded on both axes");
    constexpr auto raw_unit = 1.f / 65536;
    update_root_registration(data, registration, {91.5f + raw_unit, 0, 91.5f + raw_unit});
    expect(registration.neighbors == std::array<std::uint16_t, 3>{5, 1, 2}, "47e3c0 must register X, then Z, then diagonal across the strict positive margins");
    update_root_registration(data, registration, {36.5f, 0, 36.5f});
    expect(registration.neighbors == none, "Exact negative4.5-unit neighbor margin equality is excluded on both axes");
    update_root_registration(data, registration, {36.5f - raw_unit, 0, 36.5f - raw_unit});
    expect(registration.neighbors == std::array<std::uint16_t, 3>{3, 7, 6}, "47e3c0 must preserve the negative-margin and row-direction ordering");
    update_root_registration(data, registration, {64, 0, 91.5f + raw_unit});
    expect(registration.neighbors == std::array<std::uint16_t, 3>{1, absent, absent}, "A lone Z neighbor must occupy the first source slot");

    const auto fractional_boundary = std::nextafter(96.f, 0.f);
    registration = initial_root_registration(data, {fractional_boundary, 0, 64});
    expect(registration.sector == 4, "Initial root linking truncates local Q16 coordinates before sector lookup");
    update_root_registration(data, registration, {fractional_boundary, 0, 64});
    expect(registration.sector == 5 && registration.neighbors == std::array<std::uint16_t, 3>{4, absent, absent},
        "47e3c0 sector ROUND must remain distinct from neighbor negative-coordinate truncation at a half-raw-unit boundary");

    update_root_registration(data, registration, {-31, 0, 95});
    expect(registration.sector == 3 && registration.neighbors == std::array<std::uint16_t, 3>{2, 0, absent},
        "47e3c0 must retain raw edge-neighbor IDs rather than clamp a horizontal neighbor to its grid row");
    update_root_registration(data, registration, {159, 0, -31});
    expect(registration.sector == 8 && registration.neighbors == std::array<std::uint16_t, 3>{9, 11, 12},
        "Out-of-grid nonffff neighbor shorts remain valid grouping keys in the source contact pass");
    const auto retained = registration;
    update_root_registration(data, registration, {64, 0, 64}, 0);
    expect(registration.sector == retained.sector && registration.neighbors == retained.neighbors, "A hidden root must retain its previous source registration");
    update_root_registration(data, registration, {1000, 0, 1000});
    expect(registration.sector == absent && registration.neighbors == none, "Leaving the grid must unlink the root and clear all three source neighbors");
    update_root_registration(data, registration, {64, 0, 64});
    expect(registration.sector == absent, "47f500 must retain an outside root instead of introducing a new reentry registration");

    const auto root = visibility.register_root({64, 0, 64}, 0);
    visibility.initialize_roots(std::array<std::array<float, 3>, 1>{{{64, 0, 64}}});
    const auto snapshot = visibility.root_registration(root);
    visibility.update_root(root, {91.5f + raw_unit, 0, 91.5f + raw_unit});
    const auto moved = visibility.root_registration(root);
    expect(snapshot.sector == moved.sector && snapshot.neighbors == none && moved.neighbors == std::array<std::uint16_t, 3>{5, 1, 2},
        "Registered neighbors must update inside the same root sector, while earlier snapshots remain stale until the actual registration phase");
}

void grouping() {
    const std::array<ContactCandidateActor, 6> actors{
        {{{10, {20, 30, 40}}, true}, {{30, {20, absent, 999}}, true}, {{99, {10, 20, 30}}, false}, {{5, {10, 20, 30}}, true}, {{absent, none}, true}, {{absent, none}, true}}};
    const auto groups = contact_candidate_groups(actors);
    const auto ordered = groups.ordered();
    constexpr std::array<std::uint16_t, 6> expected{10, 20, 30, 40, 5, absent};
    expect(ordered.size() == expected.size(), "42bc70 must exclude mode4 and terminate at the firstffff neighbor while preserving the ownffff sector group");
    for (std::size_t group = 0; group < expected.size(); ++group) {
        expect(ordered[group].sector == expected[group], "42bc70 groups must preserve first-encounter order across ascending actor slots");
    }
    expect(ordered[1].count == 3 && ordered[1].actors[0] == 0 && ordered[1].actors[1] == 1 && ordered[1].actors[2] == 3,
        "Each source sector list must retain ascending actor-slot order independently of sector discovery order");
    using Pair = std::pair<std::uint8_t, std::uint8_t>;
    std::vector<Pair> attempts;
    for (const auto& group : ordered) {
        for (std::uint8_t first = 0; first < group.count; ++first) {
            for (std::uint8_t second = first + 1; second < group.count; ++second) {
                attempts.emplace_back(group.actors[first], group.actors[second]);
            }
        }
    }
    const std::vector<Pair> expected_attempts{{0, 3}, {0, 1}, {0, 3}, {1, 3}, {0, 1}, {0, 3}, {1, 3}, {4, 5}};
    expect(attempts == expected_attempts, "Sector candidates must preserve repeated misses and encountered pair chronology; only confirmed contacts may be suppressed by428820");

    std::array<ContactCandidateActor, 16> maximum;
    for (std::size_t actor = 0; actor < maximum.size(); ++actor) {
        const auto sector = std::uint16_t(actor * 4);
        maximum[actor].registration = {sector, {std::uint16_t(sector + 1), std::uint16_t(sector + 2), std::uint16_t(sector + 3)}};
    }
    const auto full = contact_candidate_groups(maximum);
    expect(full.ordered().size() == 64 && full.ordered().back().sector == 63, "Original16 actors and three neighbors must fit exactly64 bounded sector groups");
}
} // namespace

int main() {
    try {
        registration();
        grouping();
        std::cout << "PASS: source root-sector registration, strict neighbor arithmetic and ordered contact candidate groups\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
