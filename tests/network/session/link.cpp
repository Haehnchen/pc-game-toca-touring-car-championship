#include "../udp_impairment.hpp"
#include "support.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace toca::network::test::session {
void reliable_host_departure() {
    using namespace toca::network::test;
    Session host(3000), client(3000);
    host.host(0, {42, "HOST", 0, false});
    UdpProxy proxy(host.view().port, {50, 0, 0, 0, 0, 0x4c454156});
    client.join({"127.0.0.1", proxy.port()}, {42, "CLIENT", 0, false});
    const auto pump = pump_endpoints(proxy, host, client);
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::lobby;
        },
        "Impaired host-departure fixture did not connect");
    // Finish the lobby's handshake ACKs before targeting the departure message,
    // rather than inheriting the initial ENet handshake retransmission clock.
    until(
        pump,
        [&] {
            return host.statistics().reliable_bytes == 0 && client.statistics().reliable_bytes == 0;
        },
        "Departure handshake did not settle");
    proxy.drop_next_reliable_control(true, 3);
    const auto start = monotonic_millis();
    host.leave();
    require(host.view().phase == SessionPhase::idle && host.draining(), "Visible host departure must retain bounded reliable drain");
    try {
        until(
            pump,
            [&] {
                return client.view().phase == SessionPhase::failed && !host.draining() && !client.draining();
            },
            "Reliable host departure did not finish its retry/close", 4000);
    } catch (...) {
        std::cerr << "Host departure elapsed_ms=" << monotonic_millis() - start << " host_draining=" << host.draining() << " client_draining=" << client.draining()
                  << " client_phase=" << phase_name(client.view().phase) << " client_error=" << client.view().error << " forced_drops=" << proxy.forced_reliable_drops << '\n';
        throw;
    }
    require(proxy.forced_reliable_drops == 3 && monotonic_millis() - start >= 500, "Host departure fixture did not force reliable retransmission");
    require(client.view().error == "Host left the session" && client.view().roster.players.empty(), "Dropped departure became timeout or retained stale players");
    require(host.statistics().sent_messages > 0 && client.statistics().received_messages > 0, "Final session statistics vanished after disconnect");
}

void saturated_peer() {
    Session host(1000), client(1000);
    host.host(0, {42, "HOST", 0, false});
    client.join({"127.0.0.1", host.view().port}, {42, "CLIENT", 0, false});
    const auto pump = pump_endpoints(host, client);
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::lobby;
        },
        "Saturation fixture did not connect");
    for (unsigned index = 0; index < 600; ++index) {
        auto settings = host.view().settings;
        settings.weather = std::uint8_t((index + 1) % 6);
        require(host.set_settings(settings), "Host lost local settings while retiring a saturated peer");
    }
    require(host.statistics().queue_full > 0 && host.statistics().reliable_bytes <= max_reliable_bytes, "Session traffic escaped reliable transport bounds");
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 1 && client.view().phase == SessionPhase::failed;
        },
        "Saturated peer remained a stale roster member");
    require(host.view().phase == SessionPhase::lobby && client.view().error == "Connection timed out", "Queue failure did not preserve host lobby and explain client loss");
}

void adaptive_state_link() {
    using namespace toca::network::test;
    Session host(3000), client(3000);
    host.host(0, {42, "HOST", 0, false});
    UdpProxy proxy(host.view().port, {100, 0, 0, 0, 0, 0x43414445});
    client.join({"127.0.0.1", proxy.port()}, {42, "CLIENT", 1, false});
    const auto pump = pump_endpoints(proxy, host, client);
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::lobby;
        },
        "Adaptive link did not connect");
    require(host.ready(true), "Adaptive host ready failed");
    until(
        pump,
        [&] {
            return client.view().roster == host.view().roster;
        },
        "Adaptive host ready did not arrive");
    require(client.ready(true), "Adaptive client ready failed");
    until(
        pump,
        [&] {
            return std::all_of(host.view().roster.players.begin(), host.view().roster.players.end(), [](const auto& player) {
                return player.ready;
            });
        },
        "Adaptive readiness failed");
    require(host.start_race(123, {0, 1}), "Adaptive race start failed");
    until(
        pump,
        [&] {
            return client.race_view() != nullptr;
        },
        "Adaptive Start did not arrive");
    require(host.loaded() && client.loaded(), "Adaptive load acknowledgement failed");
    until(
        pump,
        [&] {
            return host.view().phase == SessionPhase::grid && client.view().phase == SessionPhase::grid;
        },
        "Adaptive Go did not arrive");
    require(host.begin_racing() && client.begin_racing(), "Adaptive grid did not release");
    const auto finish = monotonic_millis() + 1250;
    std::uint64_t publications = 0;
    while (monotonic_millis() < finish) {
        toca::simulation::RemoteMotionState state;
        state.position[0] = static_cast<std::int32_t>(++publications);
        require(client.publish_state_due(1, state, monotonic_millis()), "Adaptive tick publication failed");
        pump();
        (void)host.take_race_states();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto& stats = client.race_statistics();
    require(publications > 100 && stats.state_link_sent > 1 && stats.state_link_sent < 25 && stats.state_replaced > 0 && stats.peak_pending_states == 1
                && stats.max_cadence_interval_ms >= 66 && stats.rtt_samples > 0 && stats.cadence_suppressed > 0,
        "Actual delayed ENet link bypassed recovered cadence or sampled RTT at presentation rate");
    std::cout << "adaptive publications=" << publications << " sent=" << stats.state_link_sent << " interval_ms=" << stats.max_cadence_interval_ms
              << " rtt_samples=" << stats.rtt_samples << " pending_peak=" << stats.peak_pending_states << '\n';
}

void timeout_and_cancel() {
    auto host = std::make_unique<Session>(300);
    host->host(0, {7, "HOST", 0, false});
    Session client(300);
    client.join({"127.0.0.1", host->view().port}, {7, "CLIENT", 0, false});
    until(
        [&] {
            const auto now = monotonic_millis();
            host->pump(now);
            client.pump(now);
        },
        [&] {
            return client.view().phase == SessionPhase::lobby;
        },
        "Timeout fixture did not connect");
    host.reset();
    until(
        [&] {
            client.pump(monotonic_millis());
        },
        [&] {
            return client.view().phase == SessionPhase::failed;
        },
        "Hard loss did not terminate session", 1500);
    require(client.view().roster.players.empty(), "Hard loss retained players");
    require(client.view().error == "Connection timed out" && !client.draining(), "Hard host loss needs timeout cleanup without a pending drain");
    client.join({"127.0.0.1", 9}, {7, "CLIENT", 0, false});
    client.leave();
    require(client.view().phase == SessionPhase::idle && client.view().error.empty(), "Cancellation must restore usable idle state");
    require(!client.draining(), "Cancelling an unbound connection must release transport immediately");
}

} // namespace toca::network::test::session
