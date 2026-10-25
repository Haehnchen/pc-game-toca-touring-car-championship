#pragma once

#include "network/protocol.hpp"
#include "network/race_state.hpp"
#include "network/transport.hpp"

#include <map>
#include <memory>
#include <string_view>

namespace toca::network {
enum class SessionPhase {
    idle,
    connecting,
    lobby,
    loading,
    grid,
    racing,
    results,
    failed
};
std::string_view phase_name(SessionPhase phase);
std::string_view rejection_text(RejectReason reason);

struct Address {
    std::string ipv4;
    std::uint16_t port = 0;
};

Address parse_address(std::string_view text);

enum class LoadingAbortReason {
    cancelled,
    load_failed,
    loading_timeout
};

struct SessionView {
    SessionPhase phase = SessionPhase::idle;
    Generation generation = 0;
    PlayerId local_player = 0, host_player = 0;
    Roster roster;
    LobbySettings settings;
    std::string error;
    std::uint16_t port = 0;
    bool hosting = false;
    std::uint64_t revision = 0;
    //Host-local OS interface candidates, never a claim of remote reachability.
    std::vector<std::string> host_endpoints;
};

struct RaceStatistics {
    Generation generation = 0;
    std::size_t actors = 0, active_humans = 0, finish_count = 0;
    std::uint64_t state_publications = 0, state_link_sent = 0, state_received = 0, state_consumed = 0, state_discarded = 0;
    std::uint64_t state_replaced = 0, cadence_suppressed = 0;
    std::uint64_t controls_changed = 0, controls_unchanged = 0, controls_received = 0, controls_consumed = 0, events_consumed = 0;
    std::array<std::uint64_t, max_players> actor_state_received{};
    std::size_t peak_pending_states = 0;
    std::uint64_t rtt_samples = 0;
    std::uint32_t last_rtt_ms = 0;
    std::uint8_t max_cadence_level = 0;
    MonotonicMillis max_cadence_interval_ms = 0;
    RacePhase phase = RacePhase::loading;
    bool paused = false;
    ActorId local_actor = 0xff;
    std::vector<Finish> finish_order;
};

// Membership and reliable lobby state. The caller owns rendering and simulation.
// Every process contributes exactly one player; transport IDs never index cars.
class Session {
    struct Peer {
        PlayerId player = 0;
        std::uint32_t sequence = 0;
        MonotonicMillis hello_deadline = 0;
        bool closing = false;
        std::uint32_t ready_roster_floor = 0, ready_settings_floor = 0;
        SourceSendCadence cadence{};
        bool cadence_initialized = false;
        MonotonicMillis last_rtt_sample = 0;
        std::array<std::optional<Packet>, max_players> pending_states{};
        std::array<std::optional<MonotonicMillis>, max_players> last_state_sent{};
    };

    SessionView view_;
    std::unique_ptr<Endpoint> endpoint_;
    std::unique_ptr<Endpoint> retiring_;
    std::vector<ConnectionId> retiring_peers_;
    MonotonicMillis retire_deadline_ = 0;
    TransportStatistics final_statistics_;
    std::map<ConnectionId, Peer> peers_;
    ConnectionId server_ = ConnectionId::invalid;
    Hello identity_;
    PlayerId next_player_ = 2;
    std::uint32_t sequence_ = 0;
    MonotonicMillis connect_deadline_ = 0;
    std::uint32_t initial_roster_revision_ = 0, initial_settings_revision_ = 0;
    std::uint32_t timeout_ms_ = 12000;
    std::uint32_t loading_timeout_ms_ = 120000;
    MonotonicMillis loading_deadline_ = 0;
    std::unique_ptr<RaceState> race_;
    RaceStatistics race_statistics_;
    std::array<std::optional<std::array<std::uint8_t, 8>>, max_players> last_controls_;

    bool send(ConnectionId peer, Message message);
    void broadcast(Message message);
    void changed_roster();
    void reject(ConnectionId peer, RejectReason reason);
    void fail(RejectReason reason, bool await_remote_close = false);
    void receive(ConnectionId peer, const Packet& packet, Channel channel);
    void remove_peer(ConnectionId peer);
    bool dispatch_race(RaceDispatch dispatch);
    void update_race_phase();
    bool flush_states(MonotonicMillis now);
    void reset_race_streams();

public:
    //Original12s liveness and an independent native120s absolute resource limit.
    explicit Session(std::uint32_t timeout_ms = 12000, std::uint32_t loading_timeout_ms = 120000);
    void host(std::uint16_t port, Hello identity);
    void join(Address address, Hello identity);
    void pump(MonotonicMillis now);
    void leave();
    bool draining() const;
    bool select_car(std::uint8_t car, bool manual);
    bool ready(bool value);
    bool set_settings(LobbySettings settings);
    bool can_start_race() const;
    bool start_race(std::uint32_t seed, std::vector<ActorId> grid);
    bool loaded();
    //Host terminates the shared load; a client leaves. Explicit cancel/failure also
    //covers the grid while local final resources unwind after Loaded/Go.
    bool abort_loading(LoadingAbortReason reason);
    bool begin_racing();
    bool publish_race(Message message);
    bool publish_state_due(ActorId actor, const simulation::RemoteMotionState& state, MonotonicMillis now);
    bool publish_controls_changed(ActorId actor, const simulation::RemoteControls& controls);
    bool return_to_lobby();
    bool owns_actor(ActorId actor) const;
    const RaceView* race_view() const;
    std::vector<ActorState> take_race_states();
    std::vector<ActorInput> take_race_inputs();
    std::optional<Message> take_race_event();
    const SessionView& view() const;
    TransportStatistics statistics() const;
    const RaceStatistics& race_statistics() const;
};
} // namespace toca::network
