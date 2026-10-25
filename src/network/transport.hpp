#pragma once

#include "network/types.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace toca::network {
using Bytes = std::vector<std::uint8_t>;

// Transport time is independent of the simulation/focus/pause clock.
MonotonicMillis monotonic_millis();

//Actual UP IPv4 interface addresses, excluding wildcard/loopback. Enumeration
//does not probe a subnet or establish reachability/firewall/router behavior.
std::vector<std::string> local_ipv4_addresses();

struct TransportConfig {
    std::string bind_address = "0.0.0.0";
    std::uint16_t port = 0;
    std::size_t max_peers = max_players;
    MonotonicMillis timeout_ms = 3000;
    std::size_t max_events_per_pump = 64;
};

enum class SendResult {
    queued,
    replaced,
    queue_full,
    invalid_peer,
    invalid_message
};
enum class TransportEventType {
    connected,
    received,
    disconnected
};
enum class DisconnectCause {
    remote,
    requested,
    timeout
};

struct TransportEvent {
    TransportEventType type = TransportEventType::received;
    ConnectionId connection = ConnectionId::invalid;
    Channel channel = Channel::control;
    Bytes payload;
    DisconnectCause cause = DisconnectCause::remote;
    std::uint32_t reason = 0;
};

struct TransportStatistics {
    std::uint64_t sent_messages = 0;
    std::uint64_t received_messages = 0;
    std::uint64_t replaced_states = 0;
    std::uint64_t queue_full = 0;
    std::uint64_t rejected_messages = 0;
    std::uint64_t wire_bytes_sent = 0;
    std::uint64_t wire_bytes_received = 0;
    std::size_t reliable_bytes = 0;
    std::size_t peak_reliable_bytes = 0;
    std::size_t unreliable_bytes = 0;
    std::size_t peak_unreliable_bytes = 0;
    std::size_t pending_states = 0;
    std::size_t peak_pending_states = 0;
    MonotonicMillis max_service_gap_ms = 0;
};

// A single-threaded endpoint. Call pump even during loading, pause and focus loss.
// Every pump is nonblocking: one ENet service call (up to 256 UDP datagrams), then
// at most max_events_per_pump events. Returned events own their payloads.
// Each peer has <=64 KiB reliable payload (including ENet-owned packets), <=256
// reliable packets, <=64 KiB/64 transient unreliable packets, and eight latest
// state slots. pump flushes those slots; send replaces an unsent actor state.
// Input and state are unsequenced at ENet: consumers compare per-actor sequence.
// They must also fit the negotiated MTU minus12bytes (1024 fits native1400 MTU).
class Endpoint {
public:
    explicit Endpoint(const TransportConfig& config = {});
    ~Endpoint();
    Endpoint(Endpoint&&) noexcept;
    Endpoint& operator=(Endpoint&&) noexcept;
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;

    ConnectionId connect(const std::string& ipv4, std::uint16_t port);
    SendResult send(ConnectionId connection, Channel channel, std::span<const std::uint8_t> payload, std::optional<ActorId> actor = {});
    std::vector<TransportEvent> pump(MonotonicMillis now);
    void disconnect(ConnectionId connection, std::uint32_t reason = 0);
    std::uint16_t local_port() const;
    TransportStatistics statistics() const;
    std::optional<std::uint32_t> round_trip_time_ms(ConnectionId connection) const;
    // Sends queued outbound data without reading or dispatching events.
    void flush_outbound();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace toca::network
