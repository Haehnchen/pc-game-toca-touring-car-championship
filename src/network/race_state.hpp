#pragma once

#include "network/protocol.hpp"

#include <array>
#include <deque>

namespace toca::network {
// Original43a740/43bc70/43cf20 interval arithmetic. Sampling the replacement
// transport's RTT is a caller decision; repeated cached RTTs are not samples.
class SourceSendCadence {
    std::array<std::uint32_t, 4> rtts_{};
    std::uint8_t level_ = 0;
    MonotonicMillis last_sent_ = 0;
    bool sent_ = false;

public:
    explicit SourceSendCadence(std::uint32_t initial_hint_ms = 0);
    bool record_rtt(std::uint32_t rtt_ms, bool matched_probe = true, bool alternate_thresholds = false);
    MonotonicMillis interval_ms() const;
    std::uint8_t level() const;
    bool due(MonotonicMillis now) const;
    void sent(MonotonicMillis now);
};

enum class RacePhase {
    loading,
    grid,
    racing,
    results
};

struct RaceActor {
    ActorId actor = 0;
    PlayerId player = 0; // zero for an AI slot; retained after human departure
    bool active_human = false;
    bool finished = false;
    bool loaded = false, pause_requested = false;
};

struct RaceView {
    RacePhase phase = RacePhase::loading;
    Generation generation = 0;
    std::uint32_t seed = 0;
    std::vector<ActorId> grid;
    std::vector<RaceActor> actors;
    std::vector<Finish> finish_order;
    bool paused = false;
};

enum class RouteResult {
    accepted,
    ignored,
    invalid,
    queue_full
};

// Session sends each transmission immediately through its one Endpoint. A zero
// destination broadcasts to admitted players, optionally excluding its origin.
struct RaceTransmission {
    Packet packet;
    Channel channel = Channel::control;
    PlayerId destination = 0, except = 0;
    std::optional<ActorId> actor = {};
};

struct RaceDispatch {
    RouteResult result = RouteResult::ignored;
    std::vector<RaceTransmission> transmissions;
};

// Original distributed car ownership with host routing and one agreed append
// order for owner-authored finish times. No simulation runs from this module.
class RaceState {
    RaceView view_;
    PlayerId local_, host_;
    std::uint32_t sequence_ = 0;
    std::array<std::uint32_t, max_players> state_sequences_{}, input_sequences_{};
    std::array<std::optional<ActorState>, max_players> pending_states_;
    std::array<std::optional<ActorInput>, max_players> pending_inputs_;
    std::array<bool, max_players> loaded_{}, pause_requests_{};
    std::array<bool, max_players> finish_proposed_{};
    std::array<std::uint32_t, max_players> control_sequences_{};
    std::deque<Message> events_;
    bool go_sent_ = false;

    std::optional<ActorId> player_actor(PlayerId player) const;
    PlayerId owner(ActorId actor) const;
    Packet packet(Message message, PlayerId origin = 0);
    bool event(Message message);
    RaceDispatch apply(const Packet& packet, Channel channel, bool local);
    void start_if_loaded(RaceDispatch& dispatch);
    void aggregate_pause(RaceDispatch& dispatch);
    void broadcast_status(RaceDispatch& dispatch);

public:
    RaceState(Generation generation, PlayerId local, PlayerId host, const Roster& roster, const LobbySettings& settings, const Start& start);
    const RaceView& view() const;
    bool owns(ActorId actor) const;
    RaceDispatch publish(Message message);
    RaceDispatch receive(const Packet& packet, PlayerId transport_sender, Channel channel);
    RaceDispatch peer_left(PlayerId player);
    bool begin_racing();
    std::vector<ActorState> take_states();
    std::vector<ActorInput> take_inputs();
    std::optional<Message> take_event();
};

bool race_message(const Message& message);
} // namespace toca::network
