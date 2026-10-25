#include "network/race_state.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace toca::network {
namespace {
bool newer_sequence(std::uint32_t value, std::uint32_t previous) {
    return value != 0 && (previous == 0 || (value != previous && value - previous < 0x80000000u));
}

std::optional<ActorId> message_actor(const Message& message) {
    return std::visit(
        [](const auto& value) -> std::optional<ActorId> {
            if constexpr (requires { value.actor; }) {
                return value.actor;
            }
            return {};
        },
        message);
}

Channel message_channel(const Message& message) {
    if (std::holds_alternative<ActorState>(message)) {
        return Channel::latest_state;
    }
    if (std::holds_alternative<ActorInput>(message)) {
        return Channel::input;
    }
    return Channel::control;
}
} // namespace

SourceSendCadence::SourceSendCadence(std::uint32_t hint) {
    rtts_.fill(hint);
    level_ = hint < 51 ? 0 : hint < 101 ? 1 : hint < 151 ? 2 : hint < 201 ? 4 : hint <= 500 ? 16 : 30;
}

bool SourceSendCadence::record_rtt(std::uint32_t rtt, bool matched, bool alternate) {
    const auto previous = level_;
    if (!matched) {
        level_ = static_cast<std::uint8_t>(std::min<unsigned>(32, level_ + 2));
    } else {
        std::move(rtts_.begin() + 1, rtts_.end(), rtts_.begin());
        rtts_.back() = rtt;
        // Source adds unsigned32-bit slots before shifting; retain that order.
        const std::uint32_t mean = (rtts_[0] + rtts_[1] + rtts_[2] + rtts_[3]) >> 2;
        if (mean > (alternate ? 210u : 155u) && level_ < 32) {
            ++level_;
        } else if (mean < (alternate ? 170u : 90u) && level_ > 6) {
            --level_;
        }
    }
    return level_ != previous;
}

MonotonicMillis SourceSendCadence::interval_ms() const {
    return level_ == 0 ? 1 : level_ == 32 ? 3000 : (level_ + 1u) * 33u;
}

std::uint8_t SourceSendCadence::level() const {
    return level_;
}

bool SourceSendCadence::due(MonotonicMillis now) const {
    return !sent_ || (now >= last_sent_ && now - last_sent_ > interval_ms());
}

void SourceSendCadence::sent(MonotonicMillis now) {
    last_sent_ = now;
    sent_ = true;
}

bool race_message(const Message& message) {
    return std::visit(
        []<class T>(const T&) {
            return std::is_same_v<T, Loaded> || std::is_same_v<T, Go> || std::is_same_v<T, ActorInput> || std::is_same_v<T, ActorState> || std::is_same_v<T, Gear>
                   || std::is_same_v<T, Finish> || std::is_same_v<T, Pause> || std::is_same_v<T, ActorStatus> || std::is_same_v<T, DamageVisual> || std::is_same_v<T, ReturnLobby>
                   || std::is_same_v<T, RaceStatus>;
        },
        message);
}

RaceState::RaceState(Generation generation, PlayerId local, PlayerId host, const Roster& roster, const LobbySettings& settings, const Start& start) : local_(local), host_(host) {
    if (generation == 0 || local == 0 || host == 0 || roster.players.empty() || roster.players.size() > max_players || start.roster_revision != roster.revision
        || start.settings_revision != settings.revision) {
        throw std::invalid_argument("Invalid race membership or lobby revisions");
    }
    view_.generation = generation;
    view_.seed = start.seed;
    view_.grid = start.grid;
    for (const auto& player : roster.players) {
        if (player.player == 0 || player_actor(player.player)) {
            throw std::invalid_argument("Duplicate race player identity");
        }
        view_.actors.push_back({static_cast<ActorId>(view_.actors.size()), player.player, true, false});
    }
    if (!player_actor(local) || !player_actor(host)) {
        throw std::invalid_argument("Race lacks the local player or host");
    }
    if (settings.ai) {
        while (view_.actors.size() < max_players) {
            view_.actors.push_back({static_cast<ActorId>(view_.actors.size()), 0, false, false});
        }
    }
    if (view_.grid.size() != view_.actors.size()) {
        throw std::invalid_argument("Race grid does not cover its actor set");
    }
    std::array<bool, max_players> seen{};
    for (const auto actor : view_.grid) {
        if (actor >= view_.actors.size() || seen[actor]) {
            throw std::invalid_argument("Race grid must be an actor permutation");
        }
        seen[actor] = true;
    }
}

std::optional<ActorId> RaceState::player_actor(PlayerId player) const {
    for (const auto& actor : view_.actors) {
        if (actor.player == player && player != 0) {
            return actor.actor;
        }
    }
    return {};
}

PlayerId RaceState::owner(ActorId actor) const {
    if (actor >= view_.actors.size()) {
        return 0;
    }
    return view_.actors[actor].active_human ? view_.actors[actor].player : host_;
}

bool RaceState::owns(ActorId actor) const {
    return owner(actor) == local_;
}

const RaceView& RaceState::view() const {
    return view_;
}

Packet RaceState::packet(Message message, PlayerId origin) {
    if (++sequence_ == 0) {
        ++sequence_;
    }
    return {view_.generation, origin ? origin : local_, sequence_, std::move(message)};
}

bool RaceState::event(Message message) {
    // Message is a bounded value variant;128 entries stay below64KiB even for
    // its largest alternative. State/input use eight independent slots instead.
    static_assert(sizeof(Message) * 128 <= max_reliable_bytes);
    if (events_.size() >= 128) {
        return false;
    }
    events_.push_back(std::move(message));
    return true;
}

void RaceState::start_if_loaded(RaceDispatch& dispatch) {
    if (local_ != host_ || go_sent_ || view_.phase != RacePhase::loading) {
        return;
    }
    for (const auto& actor : view_.actors) {
        if (actor.active_human && !loaded_[actor.actor]) {
            return;
        }
    }
    go_sent_ = true;
    view_.phase = RacePhase::grid;
    (void)event(Go{});
    dispatch.transmissions.push_back({packet(Go{}), Channel::control});
}

void RaceState::aggregate_pause(RaceDispatch& dispatch) {
    bool paused = false;
    for (const auto& actor : view_.actors) {
        paused |= actor.active_human && pause_requests_[actor.actor];
    }
    if (paused == view_.paused) {
        return;
    }
    view_.paused = paused;
    (void)event(Pause{paused});
    dispatch.transmissions.push_back({packet(Pause{paused}), Channel::control});
}

void RaceState::broadcast_status(RaceDispatch& dispatch) {
    RaceStatus status;
    for (auto& actor : view_.actors) {
        actor.loaded = actor.active_human && loaded_[actor.actor];
        actor.pause_requested = actor.active_human && pause_requests_[actor.actor];
        if (actor.loaded) {
            status.loaded_mask |= std::uint8_t(1u << actor.actor);
        }
        if (actor.pause_requested) {
            status.pause_mask |= std::uint8_t(1u << actor.actor);
        }
    }
    dispatch.transmissions.push_back({packet(status), Channel::control});
}

RaceDispatch RaceState::apply(const Packet& incoming, Channel channel, bool local) {
    RaceDispatch result;
    if (const auto* finish = std::get_if<Finish>(&incoming.message); finish && finish->time_centiseconds > max_finish_centiseconds) {
        result.result = RouteResult::invalid;
        return result;
    }
    if (!race_message(incoming.message) || channel != message_channel(incoming.message)) {
        result.result = RouteResult::invalid;
        return result;
    }
    if (channel == Channel::control && events_.size() >= 126) {
        result.result = RouteResult::queue_full;
        return result;
    }
    const auto actor = message_actor(incoming.message);
    if (actor && *actor >= view_.actors.size()) {
        result.result = RouteResult::invalid;
        return result;
    }
    if (actor && !std::holds_alternative<ActorStatus>(incoming.message) && owner(*actor) != incoming.origin) {
        // State and the reliable takeover announcement can cross in flight.
        // Neither an old human packet nor early host takeover state can change
        // authority before the host's reliable ActorStatus is applied.
        result.result = local_ != host_ || (!view_.actors[*actor].active_human && view_.actors[*actor].player == incoming.origin) ? RouteResult::ignored : RouteResult::invalid;
        return result;
    }
    result.result = RouteResult::accepted;
    if (const auto* status = std::get_if<RaceStatus>(&incoming.message)) {
        if (local_ == host_ || incoming.origin != host_) {
            return {RouteResult::invalid, {}};
        }
        std::uint8_t active_mask = 0;
        for (const auto& entry : view_.actors) {
            if (entry.active_human) {
                active_mask |= std::uint8_t(1u << entry.actor);
                const bool loaded = (status->loaded_mask & (1u << entry.actor)) != 0;
                if ((entry.loaded && !loaded) || (view_.phase != RacePhase::loading && loaded != entry.loaded)) {
                    return {RouteResult::invalid, {}};
                }
            }
        }
        if (((status->loaded_mask | status->pause_mask) & ~active_mask) != 0) {
            return {RouteResult::invalid, {}};
        }
        for (auto& entry : view_.actors) {
            entry.loaded = (status->loaded_mask & (1u << entry.actor)) != 0;
            entry.pause_requested = (status->pause_mask & (1u << entry.actor)) != 0;
        }
        return result;
    }
    if (std::holds_alternative<Loaded>(incoming.message)) {
        const auto member = player_actor(incoming.origin);
        if (local_ != host_ || !member || !view_.actors[*member].active_human) {
            result.result = RouteResult::invalid;
            return result;
        }
        if (loaded_[*member]) {
            result.result = RouteResult::ignored;
            return result;
        }
        if (view_.phase != RacePhase::loading) {
            result.result = RouteResult::invalid;
            return result;
        }
        loaded_[*member] = true;
        broadcast_status(result);
        start_if_loaded(result);
        return result;
    }
    if (std::holds_alternative<Go>(incoming.message)) {
        if (incoming.origin != host_ || view_.phase != RacePhase::loading || local_ == host_ || std::any_of(view_.actors.begin(), view_.actors.end(), [](const auto& actor) {
                return actor.active_human && !actor.loaded;
            })) {
            result.result = RouteResult::invalid;
            return result;
        }
        view_.phase = RacePhase::grid;
        (void)event(Go{});
        return result;
    }
    if (const auto* pause = std::get_if<Pause>(&incoming.message)) {
        if (local_ == host_) {
            const auto member = player_actor(incoming.origin);
            if (!member || !view_.actors[*member].active_human) {
                result.result = RouteResult::invalid;
                return result;
            }
            pause_requests_[*member] = pause->paused;
            broadcast_status(result);
            aggregate_pause(result);
        } else {
            if (incoming.origin != host_) {
                result.result = RouteResult::invalid;
                return result;
            }
            if (view_.paused != pause->paused) {
                view_.paused = pause->paused;
                (void)event(*pause);
            }
        }
        return result;
    }
    if (const auto* status = std::get_if<ActorStatus>(&incoming.message)) {
        // Authority changes are accepted only from the host; reconnect is not
        // part of this baseline, so an inactive human cannot reactivate.
        if (incoming.origin != host_ || local_ == host_ || status->actor >= view_.actors.size() || status->active_human || !view_.actors[status->actor].active_human) {
            result.result = RouteResult::invalid;
            return result;
        }
        view_.actors[status->actor].active_human = false;
        view_.actors[status->actor].loaded = false;
        view_.actors[status->actor].pause_requested = false;
        state_sequences_[status->actor] = input_sequences_[status->actor] = 0;
        pending_states_[status->actor].reset();
        pending_inputs_[status->actor].reset();
        (void)event(*status);
        if (view_.phase == RacePhase::racing && std::all_of(view_.actors.begin(), view_.actors.end(), [](const auto& entry) {
                return !entry.active_human || entry.finished;
            })) {
            view_.phase = RacePhase::results;
        }
        return result;
    }
    if (view_.phase == RacePhase::loading || !actor) {
        result.result = RouteResult::invalid;
        return result;
    }
    if (const auto* state = std::get_if<ActorState>(&incoming.message)) {
        if (!newer_sequence(incoming.sequence, state_sequences_[*actor])) {
            result.result = RouteResult::ignored;
            return result;
        }
        state_sequences_[*actor] = incoming.sequence;
        if (!local && !owns(*actor)) {
            pending_states_[*actor] = *state;
        }
    } else if (const auto* controls = std::get_if<ActorInput>(&incoming.message)) {
        if (!newer_sequence(incoming.sequence, input_sequences_[*actor])) {
            result.result = RouteResult::ignored;
            return result;
        }
        input_sequences_[*actor] = incoming.sequence;
        if (!local && !owns(*actor)) {
            pending_inputs_[*actor] = *controls;
        }
    } else if (const auto* finish = std::get_if<Finish>(&incoming.message)) {
        if (view_.phase != RacePhase::racing && view_.phase != RacePhase::results) {
            result.result = RouteResult::invalid;
            return result;
        }
        if (view_.actors[*actor].finished) {
            result.result = RouteResult::ignored;
            return result;
        }
        // The active-human completion barrier classifies every remaining
        // inactive/AI actor locally. Its append order is frozen at that point;
        // a delayed physical finish must not mutate an already rendered table.
        if (view_.phase == RacePhase::results) {
            result.result = RouteResult::ignored;
            return result;
        }
        auto accepted = *finish;
        if (local_ == host_) {
            if (finish->ordinal != 0) {
                result.result = RouteResult::invalid;
                return result;
            }
            accepted.ordinal = static_cast<std::uint32_t>(view_.finish_order.size() + 1);
        } else if (finish->ordinal != view_.finish_order.size() + 1) {
            result.result = RouteResult::invalid;
            return result;
        }
        view_.actors[*actor].finished = true;
        view_.finish_order.push_back(accepted);
        (void)event(accepted);
        const bool complete = std::all_of(view_.actors.begin(), view_.actors.end(), [](const auto& entry) {
            return !entry.active_human || entry.finished;
        });
        if (complete) {
            view_.phase = RacePhase::results;
        }
        if (local_ == host_) {
            auto relay = incoming;
            relay.message = accepted;
            result.transmissions.push_back({std::move(relay), Channel::control});
        }
        return result;
    } else if (std::holds_alternative<DamageVisual>(incoming.message)) {
        if (!view_.actors[*actor].active_human) {
            result.result = RouteResult::invalid;
            return result;
        }
        if (!local) {
            (void)event(incoming.message);
        }
    } else if (std::holds_alternative<Gear>(incoming.message)) {
        if (!view_.actors[*actor].active_human) {
            result.result = RouteResult::invalid;
            return result;
        }
        if (!local) {
            (void)event(incoming.message);
        }
    } else {
        result.result = RouteResult::invalid;
        return result;
    }
    if (local_ == host_) {
        result.transmissions.push_back({incoming, channel, 0, incoming.origin, actor});
    }
    return result;
}

RaceDispatch RaceState::publish(Message message) {
    if (const auto* finish = std::get_if<Finish>(&message); finish && finish->time_centiseconds > max_finish_centiseconds) {
        return {RouteResult::invalid, {}};
    }
    const auto actor = message_actor(message);
    if (actor && (!owns(*actor) || std::holds_alternative<ActorStatus>(message))) {
        return {RouteResult::invalid, {}};
    }
    if (!race_message(message) || std::holds_alternative<Go>(message) || std::holds_alternative<RaceStatus>(message)) {
        return {RouteResult::invalid, {}};
    }
    if (local_ == host_) {
        auto incoming = packet(std::move(message));
        return apply(incoming, message_channel(incoming.message), true);
    }
    const auto member = player_actor(local_);
    if (!member || !view_.actors[*member].active_human) {
        return {RouteResult::invalid, {}};
    }
    if (std::holds_alternative<Loaded>(message)) {
        if (loaded_[*member]) {
            return {RouteResult::ignored, {}};
        }
        if (view_.phase != RacePhase::loading) {
            return {RouteResult::invalid, {}};
        }
        loaded_[*member] = true;
    } else if (const auto* pause = std::get_if<Pause>(&message)) {
        if (pause_requests_[*member] == pause->paused) {
            return {RouteResult::ignored, {}};
        }
        pause_requests_[*member] = pause->paused;
    } else {
        if (view_.phase == RacePhase::loading || !actor) {
            return {RouteResult::invalid, {}};
        }
        if (const auto* finish = std::get_if<Finish>(&message)) {
            if (finish->ordinal != 0 || (view_.phase != RacePhase::racing && view_.phase != RacePhase::results)) {
                return {RouteResult::invalid, {}};
            }
            if (finish_proposed_[*actor] || view_.actors[*actor].finished) {
                return {RouteResult::ignored, {}};
            }
            if (view_.phase == RacePhase::results) {
                return {RouteResult::ignored, {}};
            }
            finish_proposed_[*actor] = true;
        }
    }
    const auto channel = message_channel(message);
    return {RouteResult::accepted, {{packet(std::move(message)), channel, host_, 0, actor}}};
}

RaceDispatch RaceState::receive(const Packet& incoming, PlayerId transport_sender, Channel channel) {
    if (incoming.generation != view_.generation) {
        return {RouteResult::ignored, {}};
    }
    if (incoming.sequence == 0 || (local_ == host_ ? incoming.origin != transport_sender || transport_sender == host_ : transport_sender != host_)) {
        return {RouteResult::invalid, {}};
    }
    const auto member = player_actor(incoming.origin);
    if (!member) {
        return {RouteResult::invalid, {}};
    }
    if (channel == Channel::control && !newer_sequence(incoming.sequence, control_sequences_[*member])) {
        return {RouteResult::ignored, {}};
    }
    auto result = apply(incoming, channel, false);
    if (channel == Channel::control && (result.result == RouteResult::accepted || result.result == RouteResult::ignored)) {
        control_sequences_[*member] = incoming.sequence;
    }
    return result;
}

RaceDispatch RaceState::peer_left(PlayerId player) {
    const auto actor = player_actor(player);
    if (local_ != host_ || !actor || player == host_ || !view_.actors[*actor].active_human) {
        return {RouteResult::invalid, {}};
    }
    if (events_.size() >= 125) {
        return {RouteResult::queue_full, {}};
    }
    view_.actors[*actor].active_human = false;
    loaded_[*actor] = false;
    pause_requests_[*actor] = false;
    state_sequences_[*actor] = input_sequences_[*actor] = 0;
    pending_states_[*actor].reset();
    pending_inputs_[*actor].reset();
    const ActorStatus status{*actor, false};
    (void)event(status);
    RaceDispatch dispatch{RouteResult::accepted, {{packet(status), Channel::control}}};
    broadcast_status(dispatch);
    aggregate_pause(dispatch);
    start_if_loaded(dispatch);
    if (view_.phase == RacePhase::racing && std::all_of(view_.actors.begin(), view_.actors.end(), [](const auto& entry) {
            return !entry.active_human || entry.finished;
        })) {
        view_.phase = RacePhase::results;
    }
    return dispatch;
}

bool RaceState::begin_racing() {
    if (view_.phase != RacePhase::grid) {
        return false;
    }
    view_.phase = RacePhase::racing;
    return true;
}

std::vector<ActorState> RaceState::take_states() {
    std::vector<ActorState> result;
    for (auto& state : pending_states_) {
        if (state) {
            result.push_back(std::move(*state));
            state.reset();
        }
    }
    return result;
}

std::vector<ActorInput> RaceState::take_inputs() {
    std::vector<ActorInput> result;
    for (auto& input : pending_inputs_) {
        if (input) {
            result.push_back(std::move(*input));
            input.reset();
        }
    }
    return result;
}

std::optional<Message> RaceState::take_event() {
    if (events_.empty()) {
        return {};
    }
    auto message = std::move(events_.front());
    events_.pop_front();
    return message;
}
} // namespace toca::network
