#include "race/linkup_results.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void rejects(const toca::race::RaceResults& results, const std::array<bool, 8>& active) {
    try {
        (void)toca::race::linkup_results(results, active);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid Linkup classification must reject before rendering");
}
} // namespace

int main() {
    try {
        using namespace toca::race;
        RaceResults classification;
        classification.count = 8;
        classification.local_actor = 5;
        classification.local_rank = 3;
        const std::array<std::uint32_t, 8> order{6, 2, 3, 5, 0, 7, 4, 1};
        for (std::uint32_t rank = 0; rank < order.size(); ++rank) {
            const auto actor = order[rank];
            classification.rows[rank] = {actor, actor, actor * 1000 + 123, 1, "HUMAN", true, false};
        }
        classification.rows[0].time_ms = 0; //Unfinished AI time is unavailable.
        classification.rows[1].time_ms = 1111; //Departed human had a valid finish.
        const std::array<bool, 8> active{true, true, false, true, false, true, false, false};
        const auto shown = linkup_results(classification, active);
        expect(shown.count == 4 && shown.local_actor == 5 && shown.local_rank == 1, "Visible rank must count active humans only");
        const std::array<std::uint32_t, 4> visible{3, 5, 0, 1};
        for (std::size_t rank = 0; rank < visible.size(); ++rank) {
            const auto actor = visible[rank];
            const auto& row = shown.rows[rank];
            expect(row.actor_id == actor && row.time_ms == actor * 1000 + 123 && row.car_id == actor && row.driver == "HUMAN" && row.finished,
                "Filtering must preserve source append order and every owner's authored record");
        }
        expect(shown.rows[0].time_ms == 3123 && shown.rows[2].time_ms == 123, "Visible times must never be sorted or extrapolated");
        const auto unchanged = linkup_results(classification, {true, true, true, true, true, true, true, true});
        expect(unchanged.count == 8 && unchanged.rows[0].actor_id == 6 && unchanged.local_rank == 3, "Eight active humans preserve the complete host classification");
        auto invalid = classification;
        invalid.count = 9;
        rejects(invalid, active);
        invalid = classification;
        invalid.rows[0].actor_id = 8;
        rejects(invalid, active);
        invalid = classification;
        invalid.rows[1].actor_id = invalid.rows[0].actor_id;
        rejects(invalid, active);
        invalid = classification;
        invalid.count = 4;
        rejects(invalid, active);
        auto inactive_local = active;
        inactive_local[5] = false;
        rejects(classification, inactive_local);
        std::cout << "Source Linkup active-human result rows passed\n";
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
