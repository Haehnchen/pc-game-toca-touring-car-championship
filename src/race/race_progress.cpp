#include "race/race_progress.hpp"

#include "simulation/cold_grid.hpp"
#include "simulation/finish_classification.hpp"
#include "simulation/fixed.hpp"
#include "simulation/source_race_progress.hpp"

#include <algorithm>
#include <bit>
#include <numeric>
#include <utility>

namespace toca::race {
namespace {
std::int32_t field(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("Race progression requires complete POINTS records");
    }
    return std::bit_cast<std::int32_t>(
        std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24));
}
} // namespace

RaceProgress::RaceProgress(std::span<const std::uint8_t> route, std::uint32_t lap_limit, std::uint32_t local_actor, const std::array<std::uint32_t, 16>& grid_order,
    std::uint32_t actor_count, GameMode mode)
    : RaceProgress(route, lap_limit, local_actor, grid_order, actor_count, mode, false) {}

RaceProgress::RaceProgress(std::span<const std::uint8_t> route, std::uint32_t lap_limit, const simulation::QualifyingStartInitResult& startup)
    : RaceProgress(route, lap_limit, startup.repeated.data.local_actor, startup.repeated.grid.rank_to_actor, 16, GameMode::single_race, true) {
    for (std::uint32_t actor = 0; actor < 16; ++actor) {
        if (startup.repeated.actors[actor].mode == simulation::BodyMode::removed) {
            remove(actor);
        }
    }
}

RaceProgress::RaceProgress(std::span<const std::uint8_t> route, std::uint32_t lap_limit, std::uint32_t local_actor, const std::array<std::uint32_t, 16>& grid_order,
    std::uint32_t actor_count, GameMode mode, bool qualifying)
    : order_(grid_order), local_actor_(local_actor), actor_count_(actor_count), mode_(mode), qualifying_(qualifying) {
    const auto valid_count = mode == GameMode::linkup ? actor_count >= 1 && actor_count <= 8 : actor_count == 2 || actor_count == 16;
    if (!valid_count || local_actor >= actor_count || lap_limit == 0 || lap_limit > 99) {
        throw std::runtime_error("Invalid race actor count or finite lap limit");
    }
    // A one-actor Linkup has no rival Session to validate the route for us.
    // Check the complete bounded table before constructing actors or vectors.
    const auto count = std::uint32_t(field(route, 0));
    if (count < 18 || count > 256 || count > (route.size() - 4) / 64) {
        throw std::runtime_error("Race progression requires18..256 complete POINTS records");
    }
    std::array<bool, 16> seen{};
    for (const auto actor : std::span(grid_order).first(actor_count_)) {
        if (actor >= actor_count_ || seen[actor]) {
            throw std::runtime_error("Race grid must contain each original actor exactly once");
        }
        seen[actor] = true;
        if (actor != local_actor) {
            rivals_[actor].emplace(route, qualifying_, mode, lap_limit);
            if (qualifying_) {
                rivals_[actor]->configure_qualifying_actor(local_actor, actor);
            }
            if (mode == GameMode::single_race && actor_count == 2) {
                rivals_[actor]->configure_local_players(2);
            }
            if (mode == GameMode::linkup) {
                rivals_[actor]->set_network_finish_authority(false);
            }
        }
    }
    for (std::uint32_t rank = 0; rank < actor_count_; ++rank) {
        const auto actor = grid_order[rank];
        ranks_[actor] = rank + 1;
        spatial_ranks_[actor] = rank;
        ahead_[actor] = grid_order[(rank + actor_count_ - 1) % actor_count_];
        behind_[actor] = grid_order[(rank + 1) % actor_count_];
        distance_[actor] = -1; //401370 initializes cc5f4 toffffffff.
    }
    targets_.resize(count);
    distances_.resize(count);
    for (std::uint32_t point = 0; point < count; ++point) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            targets_[point][axis] = field(route, 4 + point * 64 + 12 + axis * 4);
        }
        targets_[point][1] = 0;
        if (point != 0) {
            distances_[point] = simulation::fixed::add(distances_[point - 1], simulation::fixed::length(simulation::fixed::sub(targets_[point], targets_[point - 1])));
        }
    }
    distances_.push_back(simulation::fixed::add(distances_.back(), simulation::fixed::length(simulation::fixed::sub(targets_.front(), targets_.back()))));
    if (distances_.back() <= 0) {
        throw std::runtime_error("Race progression requires a positive original route length");
    }
}

void RaceProgress::update_positions() {
    std::array<simulation::SourceRaceOrderActor, 16> source;
    for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
        source[actor] = {!removed_[actor], finished_[actor], !crossed_start_[actor], std::int16_t(completed_[actor]), ranks_[actor], spatial_ranks_[actor], ahead_[actor],
            behind_[actor], distance_[actor], distance_delta_[actor]};
    }
    for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
        simulation::advance_source_race_order(std::span(source).first(actor_count_), actor, distances_.back() / 2);
    }
    for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
        ranks_[actor] = source[actor].rank;
        spatial_ranks_[actor] = source[actor].spatial_rank;
        ahead_[actor] = source[actor].ahead;
        behind_[actor] = source[actor].behind;
    }
    if (!classified_) {
        for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
            order_[ranks_[actor] - 1] = actor;
        }
    }
}

std::int32_t RaceProgress::route_distance(std::uint32_t point, const std::array<std::int32_t, 3>& position) const {
    using namespace simulation;
    const auto previous = (point + targets_.size() - 1) % targets_.size();
    auto delta = fixed::sub(targets_[point], position);
    delta[1] = 0;
    const auto direction = fixed::normalized(fixed::sub(targets_[point], targets_[previous]));
    //403dd0 computes cumulative target distance minus the absolute horizontal
    // projection onto the route direction, then wraps at the finish line.
    return simulation::source_route_distance(point == 0 ? distances_.back() : distances_[point], fixed::abs(fixed::dot(delta, direction)), distances_.back());
}

void RaceProgress::update(std::uint64_t active_ms, const simulation::Session& local, const RacePositions& positions) {
    stop_horns_ = false;
    for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
        if (removed_[actor]) {
            continue;
        }
        if (rivals_[actor]) {
            if (qualifying_ && (local.state() == 7 || local.state() == 8)) {
                rivals_[actor]->freeze_qualifying_laps();
            }
            rivals_[actor]->update(active_ms, positions[actor]);
        }
        const auto& session = actor == local_actor_ ? local : *rivals_[actor];
        if (const auto sector = session.split_sector()) {
            const auto split = simulation::record_checkpoint_time(checkpoint_times_, actor, *sector, ranks_[actor], std::int16_t(session.lap()), std::uint32_t(active_ms));
            if (actor == local_actor_ || actor_count_ == 2) {
                simulation::consume_checkpoint_display(split_displays_, actor == local_actor_ ? 0 : 1, *sector, split, session.split_components());
            }
        }
        points_[actor] = session.next_point();
        if (!classified_) {
            completed_[actor] = session.completed_laps();
        }
        const auto distance = route_distance(session.next_point(), positions[actor]);
        distance_delta_[actor] = simulation::fixed::abs(simulation::fixed::sub(distance, distance_[actor]));
        distance_[actor] = distance;
        crossed_start_[actor] = !session.start_line_pending();
        if (mode_ != GameMode::linkup && !classified_ && !finished_[actor] && session.state() == 10) {
            finished_[actor] = true;
            stop_horns_ = true; //421390 ->425950 stops the existing horn voices.
            finish_order_[finish_count_++] = actor;
            times_[actor] = std::uint32_t(simulation::finish_time_hundredths(session.race_time_ms(), 0)) * 10;
        }
    }
    //41ab70 follows the race update;44a6f0 then publishes the displayed lap
    //used by the next41a840 call. Keep those two phases in the same order.
    for (std::uint32_t player = 0; player < (actor_count_ == 2 ? 2u : 1u); ++player) {
        const auto actor = player == 0 ? local_actor_ : 1 - local_actor_;
        const auto& session = player == 0 ? local : *rivals_[actor];
        auto& display = split_displays_.players[player];
        simulation::advance_split_display(display, false, finished_[actor]);
        display.mode = std::int32_t(session.lap());
    }
    update_positions();
    if (mode_ == GameMode::linkup || classified_ || !finished_[local_actor_] || (actor_count_ == 2 && !finished_[1 - local_actor_])) {
        return;
    }
    const auto running_order = order_;
    std::copy_n(finish_order_.begin(), finish_count_, order_.begin());
    auto next = finish_count_;
    for (const auto actor : std::span(running_order).first(actor_count_)) {
        if (!finished_[actor]) {
            order_[next++] = actor;
        }
    }
    std::array<simulation::FinishClassificationActor, 16> classification;
    std::array<std::uint8_t, 16> source_order{};
    for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
        auto& row = classification[actor];
        row.total = std::bit_cast<std::int32_t>(times_[actor] / 10);
        row.distance_q16 = distance_[actor];
        row.best_ms = actor == local_actor_ ? local.best_lap_ms() : rivals_[actor]->best_lap_ms();
        row.lap = std::int16_t(retired_[actor] ? 0 : completed_[actor] + (finished_[actor] ? 0 : 1));
        row.rank = std::int8_t(ranks_[actor]);
        source_order[actor] = std::uint8_t(order_[actor]);
    }
    simulation::classify_finish_totals(classification, source_order, actor_count_, local_actor_, finish_count_, distances_.back());
    for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
        times_[actor] = std::uint32_t(classification[actor].total) * 10;
        order_[actor] = source_order[actor];
        if (rivals_[actor]) {
            rivals_[actor]->freeze_race_laps();
        }
    }
    classified_ = true;
}

void RaceProgress::apply_network_finish(std::uint32_t actor, std::uint32_t centiseconds, std::uint32_t ordinal, simulation::Session& local) {
    if (mode_ != GameMode::linkup || actor >= actor_count_ || centiseconds > UINT32_MAX / 10) {
        throw std::runtime_error("Invalid Linkup finish record");
    }
    if (finished_[actor]) {
        if (ordinal == 0 || ordinal > finish_count_ || finish_order_[ordinal - 1] != actor || times_[actor] != centiseconds * 10) {
            throw std::runtime_error("Conflicting Linkup finish record");
        }
        return;
    }
    if (classified_ || ordinal != finish_count_ + 1) {
        throw std::runtime_error("Linkup finishes require consecutive host delivery order");
    }
    auto& session = actor == local_actor_ ? local : *rivals_[actor];
    session.apply_network_finish(centiseconds);
    finished_[actor] = true;
    completed_[actor] = session.completed_laps();
    times_[actor] = centiseconds * 10;
    finish_order_[finish_count_++] = actor;
}

void RaceProgress::finish_network_classification(const simulation::Session& local) {
    if (mode_ != GameMode::linkup || !finished_[local_actor_]) {
        throw std::runtime_error("Linkup classification requires the local owner's finish");
    }
    if (classified_) {
        return;
    }
    std::copy_n(finish_order_.begin(), finish_count_, order_.begin());
    auto next = finish_count_;
    //43ccb0 appends nonactive identities in ascending actor order once every
    //active human has finished, independently of their physical running order.
    for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
        if (!finished_[actor]) {
            order_[next++] = actor;
        }
    }
    // Linkup's owner-authored times must stay attached to their actors. The
    // single-player extrapolation/sorting pass does not apply to these records.
    for (std::uint32_t rank = 0; rank < actor_count_; ++rank) {
        const auto actor = order_[rank];
        ranks_[actor] = rank + 1;
        if (actor == local_actor_) {
            completed_[actor] = local.completed_laps();
        } else {
            rivals_[actor]->freeze_race_laps();
        }
    }
    classified_ = true;
}

std::array<std::uint32_t, 16> RaceProgress::spatial_order() const {
    std::array<std::uint32_t, 16> result{};
    std::uint32_t actor = local_actor_;
    for (std::uint32_t id = 0; id < actor_count_; ++id) {
        if (!removed_[id] && spatial_ranks_[id] < spatial_ranks_[actor]) {
            actor = id;
        }
    }
    const auto active_count = std::uint32_t(std::count(removed_.begin(), removed_.begin() + actor_count_, false));
    for (std::uint32_t rank = 0; rank < active_count; ++rank) {
        result[rank] = actor;
        actor = behind_[actor];
    }
    // Keep stable identities for callers; only the leading active entries form
    // the ring. Rivals filters body4 before selecting neighbors.
    auto next = active_count;
    for (std::uint32_t id = 0; id < actor_count_; ++id) {
        if (removed_[id]) {
            result[next++] = id;
        }
    }
    return result;
}

const std::array<std::int32_t, 16>& RaceProgress::route_distances() const {
    return distance_;
}

std::int32_t RaceProgress::total_route_distance() const {
    return distances_.back();
}

std::array<std::uint32_t, 16> RaceProgress::next_points(const simulation::Session& local) const {
    auto result = points_;
    for (std::uint32_t actor = 0; actor < actor_count_; ++actor) {
        result[actor] = actor == local_actor_ ? local.next_point() : rivals_[actor]->next_point();
    }
    return result;
}

const simulation::CheckpointTimes& RaceProgress::checkpoint_times() const {
    return checkpoint_times_;
}

bool RaceProgress::stop_horns_requested() const {
    return stop_horns_;
}

const simulation::SplitDisplayState& RaceProgress::checkpoint_display(std::uint32_t player) const {
    if (player >= (actor_count_ == 2 ? 2u : 1u)) {
        throw std::runtime_error("Checkpoint display requires an active local player");
    }
    return split_displays_.players[player];
}

std::uint32_t RaceProgress::position(std::uint32_t actor) const {
    if (actor >= 16) {
        throw std::runtime_error("Race position requires original actor0..15");
    }
    return std::uint32_t(std::find(order_.begin(), order_.end(), actor) - order_.begin()) + 1;
}

bool RaceProgress::finished(std::uint32_t actor) const {
    return finished_.at(actor);
}

void RaceProgress::remove(std::uint32_t actor) {
    if (actor == local_actor_ || actor >= 16) {
        throw std::runtime_error("Only nonplayer race actors can be removed");
    }
    if (removed_[actor]) {
        return;
    }
    removed_[actor] = true;
    behind_[ahead_[actor]] = behind_[actor];
    ahead_[behind_[actor]] = ahead_[actor];
    for (std::uint32_t id = 0; id < actor_count_; ++id) {
        if (!removed_[id] && spatial_ranks_[id] > spatial_ranks_[actor]) {
            --spatial_ranks_[id];
        }
    }
    spatial_ranks_[actor] = 16;
    if (!finished_[actor] && !classified_) {
        retired_[actor] = true;
        completed_[actor] = 0;
        crossed_start_[actor] = false;
        const auto old_rank = ranks_[actor];
        for (std::uint32_t id = 0; id < actor_count_; ++id) {
            if (id != actor && !finished_[id] && ranks_[id] > old_rank) {
                --ranks_[id];
                ++ranks_[actor];
            }
        }
        for (std::uint32_t id = 0; id < actor_count_; ++id) {
            order_[ranks_[id] - 1] = id;
        }
    }
}

bool RaceProgress::active(std::uint32_t actor) const {
    return !removed_.at(actor);
}

bool RaceProgress::retired(std::uint32_t actor) const {
    return retired_.at(actor);
}

std::uint32_t RaceProgress::completed_laps(std::uint32_t actor) const {
    return completed_.at(actor);
}

bool RaceProgress::classified() const {
    return classified_;
}

RaceResults RaceProgress::results(const scene::QualifyingData& drivers) const {
    if (!classified_ || drivers.local_actor != local_actor_) {
        throw std::runtime_error("Race results require the actual local finish classification");
    }
    RaceResults result;
    result.count = actor_count_;
    result.local_actor = local_actor_;
    result.local_rank = position(local_actor_) - 1;
    for (std::uint32_t rank = 0; rank < actor_count_; ++rank) {
        const auto actor = order_[rank];
        result.rows[rank] = {actor, drivers.drivers[actor].car_id, times_[actor], completed_[actor], drivers.drivers[actor].name, finished_[actor], retired_[actor]};
    }
    return result;
}

simulation::Session& RaceProgress::participant(std::uint32_t actor) {
    return const_cast<simulation::Session&>(std::as_const(*this).participant(actor));
}

const simulation::Session& RaceProgress::participant(std::uint32_t actor) const {
    if (actor >= actor_count_ || actor == local_actor_) {
        throw std::runtime_error("Participant must select a nonlocal active race actor");
    }
    return *rivals_[actor];
}
} // namespace toca::race
