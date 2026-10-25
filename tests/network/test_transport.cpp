#include "network/transport.hpp"
#include "udp_impairment.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
using namespace toca::network;
using namespace toca::network::test;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

TransportConfig loopback(MonotonicMillis timeout = 3000, std::size_t events = 64) {
    TransportConfig config;
    config.bind_address = "127.0.0.1";
    config.timeout_ms = timeout;
    config.max_events_per_pump = events;
    return config;
}

void pause() {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void put(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (index * 8));
    }
}

std::uint64_t get(const Bytes& bytes, std::size_t offset, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value |= static_cast<std::uint64_t>(bytes.at(offset + index)) << (index * 8);
    }
    return value;
}

ConnectionId await_connection(Endpoint& server, Endpoint& client, UdpProxy* proxy = nullptr) {
    const auto deadline = monotonic_millis() + 5000;
    ConnectionId accepted = ConnectionId::invalid;
    bool joined = false;
    while (monotonic_millis() < deadline) {
        const auto now = monotonic_millis();
        if (proxy) {
            proxy->pump(now);
        }
        for (const auto& event : server.pump(now)) {
            if (event.type == TransportEventType::connected) {
                accepted = event.connection;
            }
        }
        for (const auto& event : client.pump(now)) {
            if (event.type == TransportEventType::connected) {
                joined = true;
            }
        }
        if (accepted != ConnectionId::invalid && joined) {
            return accepted;
        }
        pause();
    }
    throw std::runtime_error("ENet connection did not complete");
}

void bounded_queues_and_leave() {
    Endpoint host(loopback(3000, 4)), client(loopback());
    const auto outgoing = client.connect("127.0.0.1", host.local_port());
    const auto incoming = await_connection(host, client);
    require(host.local_port() != 0 && outgoing != ConnectionId::invalid, "Endpoint did not bind an ephemeral port");
    require(client.send(outgoing, Channel::control, Bytes(max_message_bytes + 1)) == SendResult::invalid_message, "Oversize message accepted");
    require(client.send(outgoing, Channel::control, {}) == SendResult::invalid_message, "Empty message bypassed packet-count bound");
    require(client.send(outgoing, Channel::latest_state, Bytes(84)) == SendResult::invalid_message, "State without actor accepted");
    require(client.send(outgoing, Channel::latest_state, Bytes(84), ActorId{8}) == SendResult::invalid_message, "Invalid actor accepted");
    require(client.send(ConnectionId::invalid, Channel::control, Bytes(12)) == SendResult::invalid_peer, "Unknown connection accepted");
    for (unsigned index = 0; index < max_reliable_bytes / max_message_bytes; ++index) {
        Bytes bytes(max_message_bytes, 0x55);
        put(bytes, 0, index, 4);
        require(client.send(outgoing, Channel::control, bytes) == SendResult::queued, "Reliable queue filled before64KiB");
    }
    require(client.send(outgoing, Channel::control, Bytes(1)) == SendResult::queue_full, "Reliable ENet-owned payload escaped64KiB bound");
    require(client.statistics().reliable_bytes == max_reliable_bytes, "Reliable budget excludes ENet-owned packets");
    std::size_t reliable = 0, states = 0;
    const auto deadline = monotonic_millis() + 4000;
    while (monotonic_millis() < deadline && (reliable != 64 || client.statistics().reliable_bytes != 0)) {
        const auto now = monotonic_millis();
        client.pump(now);
        const auto events = host.pump(now);
        require(events.size() <= 4, "Receive pump exceeded its configured event budget");
        for (const auto& event : events) {
            require(event.type == TransportEventType::received && event.connection == incoming, "Transport sender binding changed");
            if (event.channel == Channel::control) {
                require(get(event.payload, 0, 4) == reliable++, "Reliable channel changed ordering");
            } else {
                require(event.channel == Channel::latest_state && get(event.payload, 1, 4) == 99, "Stale unsent state reached receiver");
                ++states;
            }
        }
        pause();
    }
    require(reliable == 64 && client.statistics().reliable_bytes == 0, "Reliable queue did not drain");
    for (unsigned sequence = 0; sequence < 100; ++sequence) {
        for (ActorId actor = 0; actor < max_players; ++actor) {
            Bytes bytes(84, actor);
            put(bytes, 1, sequence, 4);
            const auto result = client.send(outgoing, Channel::latest_state, bytes, actor);
            require(result == (sequence == 0 ? SendResult::queued : SendResult::replaced), "Latest actor slot was not replaced");
        }
    }
    require(client.statistics().pending_states == max_players && client.statistics().replaced_states == 99 * max_players, "Latest state queue grew beyond actor slots");
    const auto state_deadline = monotonic_millis() + 1000;
    auto next_state_retry = monotonic_millis() + 40;
    std::array<bool, max_players> seen_states{};
    while (states != max_players && monotonic_millis() < state_deadline) {
        const auto now = monotonic_millis();
        // ENet may throttle unreliable packets after the preceding saturated
        // reliable burst. Keep fresh actor streams alive instead of asserting
        // guaranteed delivery for a single unreliable snapshot.
        if (now >= next_state_retry) {
            for (ActorId actor = 0; actor < max_players; ++actor) {
                if (!seen_states[actor]) {
                    Bytes bytes(84, actor);
                    put(bytes, 1, 99, 4);
                    require(client.send(outgoing, Channel::latest_state, bytes, actor) == SendResult::queued, "Fresh state retry could not queue");
                }
            }
            next_state_retry = now + 40;
        }
        client.pump(now);
        const auto events = host.pump(now);
        require(events.size() <= 4, "State pump exceeded its event budget");
        for (const auto& event : events) {
            require(event.type == TransportEventType::received && event.channel == Channel::latest_state && get(event.payload, 1, 4) == 99, "Stale unsent state reached receiver");
            require(event.payload[0] < max_players, "Latest state actor changed");
            if (!seen_states[event.payload[0]]) {
                seen_states[event.payload[0]] = true;
                ++states;
            }
        }
        pause();
    }
    require(states == max_players, "Latest actor states did not arrive");
    for (unsigned index = 0; index < 256; ++index) {
        require(client.send(outgoing, Channel::control, Bytes(1)) == SendResult::queued, "Reliable packet cap filled early");
    }
    require(client.send(outgoing, Channel::control, Bytes(1)) == SendResult::queue_full, "Small reliable packets bypassed count bound");
    for (unsigned index = 0; index < 64; ++index) {
        require(client.send(outgoing, Channel::input, Bytes(8)) == SendResult::queued, "Input packet cap filled early");
    }
    require(client.send(outgoing, Channel::input, Bytes(8)) == SendResult::queue_full, "Input packet count grew without bound");

    const Bytes rejection{0xde, 0xad, 0xbe, 0xef};
    require(host.send(incoming, Channel::control, rejection) == SendResult::queued, "Leave rejection could not queue");
    host.disconnect(incoming, 17);
    bool received_rejection = false, disconnected = false;
    const auto leave_deadline = monotonic_millis() + 4000;
    while (!disconnected && monotonic_millis() < leave_deadline) {
        const auto now = monotonic_millis();
        host.pump(now);
        for (const auto& event : client.pump(now)) {
            if (event.type == TransportEventType::received) {
                require(event.channel == Channel::control && event.payload == rejection, "Leave payload changed");
                received_rejection = true;
            } else if (event.type == TransportEventType::disconnected) {
                require(received_rejection && event.reason == 17, "Disconnect discarded preceding reliable rejection");
                disconnected = true;
            }
        }
        pause();
    }
    require(disconnected, "Graceful disconnect never completed");
    std::cout << "queues reliable_peak=" << client.statistics().peak_reliable_bytes << " state_slots_peak=" << client.statistics().peak_pending_states << " leave=ordered\n";
}

struct Control {
    MonotonicMillis generated = 0;
    bool applied = false;
    bool acknowledged = false;
};

void workload(std::string_view profile, MonotonicMillis duration, const Impairment& impairment) {
    Endpoint host(loopback()), client(loopback());
    UdpProxy proxy(host.local_port(), impairment);
    const auto outgoing = client.connect("127.0.0.1", proxy.port());
    const auto incoming = await_connection(host, client, &proxy);
    const auto start = monotonic_millis();
    const auto finish = start + duration;
    auto next_state = start, next_control = start;
    std::array<std::uint32_t, max_players> generated_sequences{};
    std::array<std::uint32_t, max_players> delivered_sequences{};
    std::array<MonotonicMillis, max_players> fresh_at{};
    fresh_at.fill(start);
    std::vector<Control> controls;
    std::vector<MonotonicMillis> ages;
    MonotonicMillis max_control_apply = 0, max_control_ack = 0, max_fresh_gap = 0;
    std::uint64_t generated = 0, delivered = 0, stale = 0;
    constexpr std::array<std::size_t, 10> sizes{8, 11, 12, 13, 16, 19, 20, 36, 68, 84};
    const auto deadline = finish + 2200;
    bool forced_control_loss = false;
    for (;;) {
        const auto now = monotonic_millis();
        require(now < deadline, "Transport workload/control acknowledgement exceeded deadline");
        while (next_state < finish && next_state <= now) {
            for (ActorId actor = 0; actor < max_players; ++actor) {
                Bytes bytes(84, 0x5a);
                bytes[0] = 0;
                bytes[1] = actor;
                put(bytes, 2, ++generated_sequences[actor], 4);
                put(bytes, 6, next_state, 8);
                const auto result = client.send(outgoing, Channel::latest_state, bytes, actor);
                require(result == SendResult::queued || result == SendResult::replaced, "Fresh state was refused during control retries");
                ++generated;
            }
            next_state += 40;
        }
        while (next_control < finish && next_control <= now) {
            Bytes bytes(sizes[controls.size() % sizes.size()], 0xa5);
            bytes[0] = 1;
            put(bytes, 1, controls.size(), 4);
            controls.push_back({next_control, false, false});
            require(client.send(outgoing, Channel::control, bytes) == SendResult::queued, "Control operation queue filled");
            if (impairment.loss_per_10000 != 0 && !forced_control_loss) {
                // The first control datagram shares the first fresh-state burst;
                // drop below ENet to guarantee its retransmission is exercised.
                proxy.drop_next_reliable_control();
                forced_control_loss = true;
            }
            next_control += 1000;
        }
        proxy.pump(now);
        proxy.pump(now);
        for (const auto& event : host.pump(now)) {
            require(event.type == TransportEventType::received && event.connection == incoming, "Workload connection disappeared or changed source");
            if (event.channel == Channel::latest_state) {
                require(event.payload.size() == 84 && event.payload[0] == 0 && event.payload[1] < max_players, "State size class or actor changed");
                const auto actor = event.payload[1];
                const auto sequence = static_cast<std::uint32_t>(get(event.payload, 2, 4));
                if (sequence <= delivered_sequences[actor]) {
                    ++stale;
                    continue;
                }
                delivered_sequences[actor] = sequence;
                ages.push_back(now - get(event.payload, 6, 8));
                max_fresh_gap = std::max(max_fresh_gap, now - fresh_at[actor]);
                fresh_at[actor] = now;
                ++delivered;
            } else {
                require(event.channel == Channel::control && event.payload[0] == 1, "Unexpected workload input");
                const auto index = static_cast<std::size_t>(get(event.payload, 1, 4));
                require(index < controls.size() && !controls[index].applied, "Reliable operation repeated or unknown");
                controls[index].applied = true;
                max_control_apply = std::max(max_control_apply, now - controls[index].generated);
                auto echo = event.payload;
                echo[0] = 2;
                require(host.send(incoming, Channel::control, echo) == SendResult::queued, "Control acknowledgement queue filled");
            }
        }
        proxy.pump(now);
        for (const auto& event : client.pump(now)) {
            require(event.type == TransportEventType::received && event.channel == Channel::control && event.payload[0] == 2, "Unexpected control acknowledgement");
            const auto index = static_cast<std::size_t>(get(event.payload, 1, 4));
            require(index < controls.size() && controls[index].applied && !controls[index].acknowledged, "Control acknowledgement repeated or unknown");
            controls[index].acknowledged = true;
            max_control_ack = std::max(max_control_ack, now - controls[index].generated);
        }
        if (now < finish) {
            for (const auto last : fresh_at) {
                require(now - last < 1000, "An active state stream had no fresh delivery for1s");
            }
        }
        const bool all_controls = std::all_of(controls.begin(), controls.end(), [](const auto& control) {
            return control.acknowledged;
        });
        if (now >= finish + 200 && all_controls) {
            break;
        }
        pause();
    }
    require(!ages.empty() && generated == ((duration + 39) / 40) * max_players, "State generation did not run at25Hz for all eight actors");
    std::sort(ages.begin(), ages.end());
    const auto p95 = ages.at((ages.size() - 1) * 95 / 100);
    require(p95 < 250 && max_control_apply < 2000 && max_control_ack < 2000 && max_fresh_gap < 1000, "ENet native latency/freshness targets failed");
    require(controls.size() == (duration + 999) / 1000
                && std::all_of(controls.begin(), controls.end(),
                    [](const auto& control) {
                        return control.applied && control.acknowledged;
                    }),
        "Reliable operation did not apply and acknowledge once");
    const auto stats = client.statistics();
    require(stats.peak_reliable_bytes <= max_reliable_bytes && stats.peak_pending_states <= max_players && stats.peak_unreliable_bytes <= max_reliable_bytes,
        "Workload exceeded declared queue caps");
    if (impairment.loss_per_10000) {
        require(proxy.dropped_datagrams > 0 && proxy.forced_reliable_drops == 1, "Loss profile failed to force a reliable UDP retransmission");
    }
    if (impairment.duplication_per_10000) {
        require(proxy.duplicated_datagrams > 0 && proxy.reordered_datagrams > 0, "Stress profile did not exercise duplicates and reordering");
    }
    const auto elapsed = monotonic_millis() - start;
    std::cout << "{\"profile\":\"" << profile << "\",\"seed\":" << impairment.seed << ",\"duration_ms\":" << duration << ",\"actors\":8,\"hz\":25,\"generated\":" << generated
              << ",\"delivered\":" << delivered << ",\"undelivered\":" << generated - delivered << ",\"stale_dropped\":" << stale + stats.replaced_states
              << ",\"state_p95_ms\":" << p95 << ",\"state_max_ms\":" << ages.back() << ",\"fresh_gap_max_ms\":" << max_fresh_gap << ",\"control_operations\":" << controls.size()
              << ",\"control_apply_max_ms\":" << max_control_apply << ",\"control_ack_max_ms\":" << max_control_ack << ",\"service_gap_max_ms\":" << stats.max_service_gap_ms
              << ",\"reliable_peak_bytes\":" << stats.peak_reliable_bytes << ",\"unreliable_peak_bytes\":" << stats.peak_unreliable_bytes
              << ",\"state_slots_peak\":" << stats.peak_pending_states << ",\"wire_bytes_sent\":" << stats.wire_bytes_sent
              << ",\"wire_bytes_per_sec\":" << stats.wire_bytes_sent * 1000 / elapsed << ",\"udp_dropped\":" << proxy.dropped_datagrams
              << ",\"udp_duplicated\":" << proxy.duplicated_datagrams << ",\"forced_reliable_drops\":" << proxy.forced_reliable_drops
              << ",\"udp_reordered\":" << proxy.reordered_datagrams << ",\"proxy_queue_peak\":" << proxy.peak_pending << "}\n";
}

void three_endpoint_relay() {
    Endpoint host(loopback()), first(loopback()), second(loopback());
    const Impairment impairment{50, 0, 0, 0, 0, 0x52454c41};
    UdpProxy first_proxy(host.local_port(), impairment), second_proxy(host.local_port(), impairment);
    const auto first_outgoing = first.connect("127.0.0.1", first_proxy.port());
    const auto first_incoming = await_connection(host, first, &first_proxy);
    const auto second_outgoing = second.connect("127.0.0.1", second_proxy.port());
    ConnectionId second_incoming = ConnectionId::invalid;
    bool second_connected = false;
    const auto connection_deadline = monotonic_millis() + 3000;
    while (monotonic_millis() < connection_deadline && (!second_connected || second_incoming == ConnectionId::invalid)) {
        const auto now = monotonic_millis();
        first_proxy.pump(now);
        second_proxy.pump(now);
        first.pump(now);
        for (const auto& event : host.pump(now)) {
            if (event.type == TransportEventType::connected) {
                second_incoming = event.connection;
            }
        }
        for (const auto& event : second.pump(now)) {
            second_connected |= event.type == TransportEventType::connected;
        }
        pause();
    }
    require(second_connected && second_incoming != ConnectionId::invalid && first_incoming != second_incoming, "Host did not bind two distinct source connections");
    Bytes first_message(84, 0x11), second_message(84, 0x22);
    // Deliberately identical claimed origin bytes: host routing must use the
    // transport source, which cannot be forged by a packet's claimed identity.
    first_message[0] = second_message[0] = 99;
    require(first.send(first_outgoing, Channel::latest_state, first_message, ActorId{0}) == SendResult::queued, "First relay sender refused state");
    require(second.send(second_outgoing, Channel::latest_state, second_message, ActorId{0}) == SendResult::queued, "Second relay sender refused state");
    const auto start = monotonic_millis();
    bool first_received = false, second_received = false;
    MonotonicMillis end_to_end = 0;
    while (monotonic_millis() < start + 2000 && (!first_received || !second_received)) {
        const auto now = monotonic_millis();
        first_proxy.pump(now);
        second_proxy.pump(now);
        for (const auto& event : host.pump(now)) {
            require(event.type == TransportEventType::received, "Relay peer disconnected");
            if (event.connection == first_incoming) {
                require(event.payload == first_message, "First relay source binding changed");
                require(host.send(second_incoming, Channel::latest_state, event.payload, ActorId{0}) == SendResult::queued, "First host relay failed");
            } else {
                require(event.connection == second_incoming && event.payload == second_message, "Second relay source binding changed");
                require(host.send(first_incoming, Channel::latest_state, event.payload, ActorId{0}) == SendResult::queued, "Second host relay failed");
            }
        }
        for (const auto& event : first.pump(now)) {
            require(event.type == TransportEventType::received && event.connection == first_outgoing && event.payload == second_message, "Host routed first peer its own state");
            first_received = true;
            end_to_end = now - start;
        }
        for (const auto& event : second.pump(now)) {
            require(event.type == TransportEventType::received && event.connection == second_outgoing && event.payload == first_message, "Host routed second peer its own state");
            second_received = true;
            end_to_end = now - start;
        }
        pause();
    }
    require(first_received && second_received && end_to_end >= 100 && end_to_end < 250, "Two-link relay delay/routing failed");
    std::cout << "relay peers=3 per_link_rtt_ms=100 per_link_one_way_ms=50 end_to_end_state_ms=" << end_to_end << " source_binding=distinct\n";
}

void hard_loss_timeout() {
    constexpr MonotonicMillis timeout = 400;
    Endpoint host(loopback(timeout)), client(loopback(timeout));
    UdpProxy proxy(host.local_port(), {});
    client.connect("127.0.0.1", proxy.port());
    await_connection(host, client, &proxy);
    const auto start = monotonic_millis();
    proxy.blackhole();
    bool expired = false;
    MonotonicMillis elapsed = 0;
    while (monotonic_millis() < start + timeout + 1000 && !expired) {
        const auto now = monotonic_millis();
        proxy.pump(now);
        client.pump(now);
        for (const auto& event : host.pump(now)) {
            require(event.type == TransportEventType::disconnected, "Unexpected hard-loss event");
            require(event.cause == DisconnectCause::timeout, "Hard peer loss did not report timeout");
            expired = true;
            elapsed = now - start;
        }
        pause();
    }
    require(expired && elapsed <= timeout + host.statistics().max_service_gap_ms, "Hard loss exceeded configured timeout plus one service interval");
    std::cout << "timeout configured_ms=" << timeout << " observed_ms=" << elapsed << " service_gap_ms=" << host.statistics().max_service_gap_ms << '\n';
}

void independently_reordered_actors() {
    for (const auto channel : {Channel::latest_state, Channel::input}) {
        Endpoint host(loopback()), client(loopback());
        UdpProxy proxy(host.local_port(), {});
        const auto outgoing = client.connect("127.0.0.1", proxy.port());
        await_connection(host, client, &proxy);
        const Bytes first(84, 0x11), second(84, 0x22);
        const auto start = monotonic_millis();
        proxy.pump(start);
        require(
            client.send(outgoing, channel, first, channel == Channel::latest_state ? std::optional<ActorId>{0} : std::nullopt) == SendResult::queued, "ActorA state queue failed");
        client.pump(start);
        proxy.delay_next_client_datagram(80);
        proxy.pump(start);
        require(
            client.send(outgoing, channel, second, channel == Channel::latest_state ? std::optional<ActorId>{1} : std::nullopt) == SendResult::queued, "ActorB state queue failed");
        client.pump(start);
        std::vector<Bytes> received;
        while (monotonic_millis() < start + 1000 && received.size() < 2) {
            const auto now = monotonic_millis();
            proxy.pump(now);
            for (const auto& event : host.pump(now)) {
                require(event.type == TransportEventType::received && event.channel == channel, "Actor reordering received unexpected event");
                received.push_back(event.payload);
            }
            client.pump(now);
            pause();
        }
        require(received.size() == 2 && received[0] == second && received[1] == first, "ENet global sequencing suppressed another actor's fresh reordered state");
        std::cout << "actor_reordering=independent channel=" << unsigned(channel) << " delayed_actor_ms=80 delivered=2\n";
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--probe"), "Usage: test_transport [--probe]");
        const bool full = argc == 2;
        bounded_queues_and_leave();
        three_endpoint_relay();
        hard_loss_timeout();
        independently_reordered_actors();
        workload("loss_free", full ? 50000 : 1200, {});
        workload("rtt100_loss2", full ? 50000 : 2400, {50, 200, 0, 0, 0, 0x544f4341});
        workload("jitter_duplicate_reorder", full ? 10000 : 4000, {50, 200, 20, 100, 100, 0x544f4342});
        std::cout << "PASS: actual ENet endpoints, bounded queues, UDP impairments, three-peer relay and timeout" << (full ? " (full N02 probe)\n" : " (short profile)\n");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
