#pragma once

#include "network/session.hpp"

#include <functional>

namespace toca::network::test::session {
void require(bool value, const char* message);
void until(const std::function<void()>& pump, const std::function<bool()>& done, const char* message, unsigned timeout = 4000);

// One source clock sample per poll, with the caller's explicit endpoint order.
// This also supports the impairment proxy without moving it after either peer.
template <class... Endpoints> auto pump_endpoints(Endpoints&... endpoints) {
    return [&... endpoints = endpoints] {
        const auto now = monotonic_millis();
        (endpoints.pump(now), ...);
    };
}

template <class... Peers> bool same_roster(const Session& host, const Peers&... peers) {
    return ((host.view().roster == peers.view().roster) && ...);
}

struct RawClient {
    Endpoint endpoint;
    ConnectionId server;
    bool connected = false;
    std::optional<Welcome> welcome;
    std::optional<RejectReason> rejected;
    Generation generation = 0;

    explicit RawClient(const Session& host);
    void pump(MonotonicMillis now);
    void send(Packet packet);
};

void address_parsing();
void membership();
void concurrent_readiness();
void committed_subset_start();
void capacity();
void malformed_and_unbound();
void sequence_and_readiness();
void client_snapshot_binding();
void reliable_host_departure();
void saturated_peer();
void adaptive_state_link();
void invalid_sender();
void loading_recovery();
void loading_grid_unwind();
void timeout_and_cancel();
} // namespace toca::network::test::session
