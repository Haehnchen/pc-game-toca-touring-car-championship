#pragma once

#include "simulation/fixed.hpp"

#include <algorithm>
#include <array>
#include <numeric>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace toca::simulation {
inline constexpr std::array<std::uint32_t, 10> source_race_awards{15, 12, 10, 8, 6, 5, 4, 3, 2, 1};

//422720: source scores are signed shorts; the ordinary season uses bounded
//unsigned scores. Keep the same awards, local penalty floor and podium order.
template <class Score, class Podium, class Order>
void award_race_classification(std::array<Score, 16>& scores, std::array<Podium, 3>& podiums, const Order& order, std::uint32_t local, std::uint32_t penalty) {
    if (local >= 16 || std::ranges::any_of(order, [](auto actor) {
            return actor >= 16;
        })) {
        throw std::runtime_error("Classification awards require original actor bounds");
    }
    const auto before = scores[local];
    for (std::size_t rank = 0; rank < source_race_awards.size(); ++rank) {
        scores[order[rank]] = Score(std::uint32_t(scores[order[rank]]) + source_race_awards[rank]);
    }
    for (std::size_t rank = 0; rank < podiums.size(); ++rank) {
        if (order[rank] == local) {
            ++podiums[rank];
        }
    }
    //Unsigned callers avoid underflow; signed-short callers preserve the
    //source subtract/wrap followed by its signed comparison to prior points.
    if constexpr (std::is_unsigned_v<Score>) {
        scores[local] -= std::min(std::uint32_t(scores[local] - before), penalty);
    } else {
        scores[local] = std::bit_cast<std::int16_t>(std::uint16_t(std::uint32_t(scores[local]) - penalty));
        scores[local] = std::max(scores[local], before);
    }
}

struct FinishClassificationActor {
    std::int32_t total = 0, distance_q16 = 0;
    std::uint32_t best_ms = 0;
    std::int16_t lap = 1;
    std::int8_t rank = 1;
};

//4220c0's deterministic branch only. The caller excludes local retirement
//state+84>=6, whose source fallback invents times with additional RNG draws.
inline void classify_finish_totals(std::array<FinishClassificationActor, 16>& actors, std::array<std::uint8_t, 16>& order, std::uint32_t count, std::uint32_t local,
    std::uint32_t finished, std::int32_t route_length_q16) {
    if (count < 2 || count > 16 || local >= count || finished > count || route_length_q16 <= 0) {
        throw std::runtime_error("Finish classification requires bounded actors and positive route length");
    }
    for (auto rank = finished; rank < count; ++rank) {
        for (std::uint32_t actor = 0; actor < count; ++actor) {
            if (actors[actor].rank - 1 == std::int32_t(rank)) {
                order[rank] = std::uint8_t(actor);
            }
        }
    }
    std::array<unsigned, 16> occurrences{};
    for (const auto actor : std::span(order).first(count)) {
        if (actor >= count) {
            throw std::runtime_error("Finish classification order exceeds active actors");
        }
        ++occurrences[actor];
    }
    //422ac0 repairs the LAST duplicate, preserving the first occurrence.
    for (std::uint32_t actor = 0; actor < count; ++actor) {
        for (std::uint32_t missing = 0; occurrences[actor] > 1 && missing < count; ++missing) {
            if (occurrences[missing] == 0) {
                const auto slot = std::find(std::make_reverse_iterator(order.begin() + count), order.rend(), actor);
                *slot = std::uint8_t(missing);
                --occurrences[actor];
                ++occurrences[missing];
            }
        }
    }
    const auto winner = order[0];
    if (actors[winner].total == 0) {
        actors[winner].total = 60000;
        actors[winner].best_ms = 60000;
    }
    for (std::uint32_t actor = 0; actor < count; ++actor) {
        if (actors[actor].total == 0) {
            const auto laps = std::max(std::int32_t(actors[local].lap) - actors[actor].lap, 0);
            const auto seconds = actors[local].best_ms / 1000;
            const auto fraction = fixed::sub(0x10000, fixed::bits(std::uint32_t((std::int64_t(actors[actor].distance_q16) * 65536) / route_length_q16)));
            const auto partial = fixed::mul(fixed::mul(fraction, fixed::bits(seconds << 16)), 0x640000);
            const auto whole = fixed::mul(fixed::bits(seconds * std::uint32_t(laps) * 65536), 0x640000);
            actors[actor].total = fixed::add(actors[local].total, fixed::add(partial, whole) >> 16);
        }
    }
    for (std::uint32_t actor = 0; actor < count; ++actor) {
        const auto winner_time = actors[winner].total;
        if (actor == winner || actors[actor].total >= winner_time) {
            continue;
        }
        const auto rank = std::find(order.begin(), order.begin() + count, actor) - order.begin();
        std::int32_t before = -1, after = -1, before_steps = 0, after_steps = 0;
        for (auto index = rank - 1; index >= 0; --index) {
            ++before_steps;
            if (actors[order[std::size_t(index)]].total >= winner_time) {
                before = order[std::size_t(index)];
                break;
            }
        }
        for (auto index = rank + 1; index < std::ptrdiff_t(count); ++index) {
            ++after_steps;
            if (actors[order[std::size_t(index)]].total >= winner_time) {
                after = order[std::size_t(index)];
                break;
            }
        }
        if (before >= 0 && after >= 0) {
            actors[actor].total = fixed::add(actors[std::size_t(before)].total, actors[std::size_t(after)].total) / 2;
        } else if (before >= 0 || after >= 0) {
            actors[actor].total =
                before >= 0 ? fixed::add(actors[std::size_t(before)].total, before_steps * 3000) : fixed::add(actors[std::size_t(after)].total, after_steps * 3000);
        }
    }
    //The source swaps total VALUES along the retained finish order.
    for (std::uint32_t pass = 0; pass < count; ++pass) {
        for (std::uint32_t rank = 1; rank < count; ++rank) {
            if (actors[order[rank]].total < actors[order[rank - 1]].total) {
                std::swap(actors[order[rank]].total, actors[order[rank - 1]].total);
            }
        }
    }
}

template <class Score> std::array<std::uint8_t, 16> classification_standings(const std::array<Score, 16>& scores) {
    std::array<std::uint8_t, 16> order;
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto first, auto second) {
        return scores[first] > scores[second];
    });
    return order;
}
} // namespace toca::simulation
