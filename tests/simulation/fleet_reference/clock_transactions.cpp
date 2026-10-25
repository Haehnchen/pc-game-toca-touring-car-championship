#include "clock_transactions.hpp"

#include "binary_io.hpp"

#include <algorithm>
#include <bit>
#include <fstream>

namespace toca::test::fleet {
using namespace toca::simulation;

ClockTransactions::ClockTransactions(const char* path, std::uint32_t updates, bool scene_owned) : updates_(updates), scene_owned_(scene_owned) {
    std::ifstream input(path, std::ios::binary);
    std::array<std::uint8_t, 8> magic;
    read(input, magic);
    const auto version = read_u32(input);
    if (magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'C', 'L', 'O', 'K'} || (version != 2 && version != 3) || read_u32(input) != updates || updates == 0) {
        throw std::runtime_error("Unsupported source clock transaction stream");
    }
    const auto divisor = std::bit_cast<std::int32_t>(read_u32(input));
    retained_.delta_q16 = std::bit_cast<std::int32_t>(read_u32(input));
    retained_.reciprocal_q16 = std::bit_cast<std::int32_t>(read_u32(input));
    const auto extra = read_u32(input), samples = read_u32(input);
    if (retained_.delta_q16 <= 0 || retained_.delta_q16 > 0x30000 || retained_.reciprocal_q16 <= 0 || extra > 1 || (version == 2 && (samples == 0 || samples >= updates))) {
        throw std::runtime_error("Invalid source clock initial retained step or sample count");
    }
    retained_.extra_step = extra != 0;
    initial_ = retained_;
    divisor_ = divisor;
    extra_pending_ = retained_.extra_step;
    //The entry contract validates412a80's pending first sample. Its stored
    //divisor and retained extra flag are explicit inputs, not a fake QPF.
    clock_.emplace(SourceClock::from_stored_divisor(divisor, retained_.extra_step));
    for (std::uint32_t sample = 0; sample < samples; ++sample) {
        if (version == 3) {
            const Operation operation{read_u32(input), read_u32(input), read_u32(input), read_u32(input)};
            const bool terminal_sample = scene_owned && operation.id == updates && operation.kind == 1;
            if (operation.id == 0 || (operation.id >= updates && !terminal_sample) || operation.order == 0 || operation.kind > 2
                || (operation.kind == 0 && operation.value != 0) || (operation.kind == 2 && std::bit_cast<std::int32_t>(operation.value) <= 0)
                || (!operations_.empty() && (operation.id < operations_.back().id || (operation.id == operations_.back().id && operation.order <= operations_.back().order)))) {
                throw std::runtime_error("Source clock operations must follow actual primary/sample/grid-reset order");
            }
            operations_.push_back(operation);
        } else {
            const auto id = read_u32(input), counter = read_u32(input);
            if (id == 0 || id >= updates || (!samples_.empty() && id <= samples_.back().first)) {
                throw std::runtime_error("Source clock sample IDs must follow the initial update in order");
            }
            samples_.emplace_back(id, counter);
        }
    }
    created_origins_ = version == 3;
    if (created_origins_) {
        start_.timers.create(read_u32(input));
    } else {
        //Existing v2 recordings supply observed initial context; they
        //cannot establish the earlier447a70 creation transaction.
        for (auto& origin : start_.timers.origins) {
            origin = read_u32(input);
        }
    }
    for (std::uint32_t update = 0; update < updates; ++update) {
        caller_clocks_.push_back(read_u32(input));
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Source clock transactions have trailing bytes");
    }
}

TimeStep ClockTransactions::step(std::uint32_t id) {
    if (id != next_update_++) {
        throw std::runtime_error("Source clock update chronology changed");
    }
    if (created_origins_) {
        while (next_operation_ < operations_.size() && operations_[next_operation_].id == id) {
            const auto& operation = operations_[next_operation_++];
            if (operation.kind == 0) {
                frame_physical_ = 0;
                sampled_frame_ = false;
            } else if (operation.kind == 1) {
                if (sampled_frame_) {
                    throw std::runtime_error("Source primary frame duplicated412ab0");
                }
                retained_ = clock_->sample(operation.value);
                sampled_frame_ = true;
            } else {
                divisor_ = std::bit_cast<std::int32_t>(operation.value);
                clock_->reset(divisor_);
            }
        }
        primary_ = frame_physical_ == 0;
        if ((frame_physical_ == 1 && !retained_.extra_step) || frame_physical_ >= 2) {
            throw std::runtime_error("Source primary frame has an extra physical update without its generated extra latch");
        }
        ++frame_physical_;
    } else {
        primary_ = id == 0 || !extra_pending_;
        if (id == 0) {
            //The first primary precedes the first412ab0 call.
        } else if (extra_pending_) {
            if (next_sample_ < samples_.size() && samples_[next_sample_].first <= id) {
                throw std::runtime_error("A source extra update cannot import another clock sample");
            }
            extra_pending_ = false;
        } else {
            if (next_sample_ >= samples_.size() || samples_[next_sample_].first != id) {
                throw std::runtime_error("A source primary update requires its explicit clock sample");
            }
            retained_ = clock_->sample(samples_[next_sample_++].second);
            extra_pending_ = retained_.extra_step;
        }
    }
    if (shared_timers_) {
        return retained_; //The retained local wrapper owns every lifecycle transaction.
    }
    start_.timers.clock = caller_clocks_.at(id);
    if (id == 0 && created_origins_) {
        start_.timers.reset(3); //First state9's447980, before460170.
        start_.timers.reset(2); //460170→44ffe1→421290.
    }
    if (primary_ && pending_active_) {
        session_state_ = 6;
        pending_active_ = false;
    }
    if (session_state_ == 6 && active_entry_) {
        start_.timers.reset(0);
        active_entry_ = false;
    }
    release_due_ = session_state_ == 9 && id != 0 && start_.advance_countdown() == SourceCountdownEvent::green;
    if (release_due_) {
        release_epoch_ = start_.timers.clock;
        pending_active_ = true;
    }
    return retained_;
}

std::uint32_t ClockTransactions::caller_clock() const {
    return caller_clocks_.at(next_update_ - 1);
}

std::uint32_t ClockTransactions::session_state() const {
    return session_state_;
}

std::uint32_t ClockTransactions::update_caller() const {
    return session_state_ == 6 ? 0x448434u : next_update_ == 1 ? 0x4488a1u : 0x4488d0u;
}

bool ClockTransactions::release_due() const {
    return shared_release_due_ ? *shared_release_due_ : release_due_;
}

void ClockTransactions::share_lifecycle(SourceTimerBank& timers, const std::uint16_t& phase, const std::int32_t& state, const std::uint32_t& release, const bool& release_due) {
    if (next_update_ != 0 || std::ranges::any_of(timers.origins, [](auto origin) {
            return origin != 0;
        })) {
        throw std::runtime_error("Shared startup lifecycle rejects duplicate timer origins or a late handoff");
    }
    timers.origins = start_.timers.origins;
    shared_timers_ = &timers;
    shared_phase_ = &phase;
    shared_state_ = &state;
    shared_release_ = &release;
    shared_release_due_ = &release_due;
}

bool ClockTransactions::primary() const {
    return primary_;
}

bool ClockTransactions::created_origins() const {
    return created_origins_;
}

const TimeStep& ClockTransactions::initial_step() const {
    return initial_;
}

std::int32_t ClockTransactions::divisor() const {
    return divisor_;
}

void ClockTransactions::observe_operation(std::uint32_t id, std::uint32_t kind, std::optional<std::uint32_t> value) {
    if (!created_origins_) {
        return;
    }
    if (scene_owned_ && observed_operation_ == next_operation_ && next_operation_ < operations_.size() && operations_[next_operation_].id == updates_ && id + 1 == updates_
        && kind == 1) {
        //The terminal frame's external sample follows the final physical
        //POST. It must not change the timestep of that last update.
        retained_ = clock_->sample(operations_[next_operation_++].value);
        sampled_frame_ = true;
        terminal_sampled_ = true;
    }
    if (observed_operation_ >= next_operation_) {
        throw std::runtime_error("Source journal contains an unrecorded clock operation");
    }
    const auto& operation = operations_[observed_operation_++];
    if ((operation.id != id && !(scene_owned_ && operation.id == updates_ && id + 1 == updates_)) || operation.kind != kind || (value && operation.value != *value)) {
        throw std::runtime_error("Source journal disagrees with its ordered clock operation");
    }
}

std::array<std::uint32_t, 9> ClockTransactions::lifecycle_values() const {
    if (shared_timers_) {
        const auto& origins = shared_timers_->origins;
        return {origins[0], origins[1], origins[2], origins[3], origins[4], *shared_phase_, std::uint32_t(*shared_state_), *shared_release_, shared_timers_->clock};
    }
    const auto& origins = start_.timers.origins;
    return {origins[0], origins[1], origins[2], origins[3], origins[4], start_.phase, session_state_, release_epoch_, start_.timers.clock};
}

void ClockTransactions::finish_update() {
    if (created_origins_ && observed_operation_ != next_operation_) {
        throw std::runtime_error("Source update omitted an actual clock operation from its journal");
    }
    if (primary_ && !shared_timers_) {
        start_.expire_green();
    }
}

void ClockTransactions::finish() const {
    if (scene_owned_ && !terminal_sampled_) {
        throw std::runtime_error("Generated scene replay requires the final primary frame's external QPC sample");
    }
    if (created_origins_
            ? next_operation_ != operations_.size() || observed_operation_ != next_operation_ || (!terminal_sampled_ && frame_physical_ == 1 && retained_.extra_step)
            : extra_pending_ || next_sample_ != samples_.size()) {
        throw std::runtime_error("Source clock interval ends inside a transaction or leaves unused samples");
    }
}

} // namespace toca::test::fleet
