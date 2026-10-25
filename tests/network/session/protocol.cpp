#include "support.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace toca::network::test::session {
void address_parsing() {
    require(parse_address("127.0.0.1:27015").port == 27015, "IPv4 endpoint parser failed");
    for (auto invalid : {"localhost:1", "1.2.3:1", "1.2.3.256:1", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4:"}) {
        bool failed = false;
        try {
            (void)parse_address(invalid);
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed, "Malformed endpoint was accepted");
    }
}

void malformed_and_unbound() {
    for (unsigned fault = 0; fault < 5; ++fault) {
        Session host(150);
        host.host(0, {42, "HOST", 0, false});
        RawClient raw(host);
        const auto pump = pump_endpoints(host, raw);
        until(
            pump,
            [&] {
                return raw.connected;
            },
            "Malformed fixture did not connect");
        auto bytes = encode({0, 0, 1, Hello{42, "RAW", 0, false}});
        if (fault == 0) {
            bytes.resize(5);
        } else if (fault == 1) {
            bytes[4] = 1;
        } else if (fault == 2) {
            bytes = encode({0, 0, 1, Ready{1, 1, true}});
        } else if (fault == 3) {
            bytes = encode({1, 1, 1, Hello{42, "RAW", 0, false}});
        }
        if (fault != 4) {
            require(raw.endpoint.send(raw.server, Channel::control, bytes) == SendResult::queued, "Malformed fixture could not send");
        }
        until(
            pump,
            [&] {
                return raw.rejected.has_value();
            },
            "Invalid/unbound peer did not receive rejection");
        const auto expected = fault == 1 ? RejectReason::protocol : fault == 4 ? RejectReason::timeout : RejectReason::invalid_message;
        require(raw.rejected == expected && host.view().roster.players.size() == 1, "Malformed/version/Hello-deadline rejection changed membership or reason");
    }
}

void sequence_and_readiness() {
    Session host(1000);
    host.host(0, {42, "HOST", 0, false});
    RawClient raw(host);
    const auto pump = pump_endpoints(host, raw);
    until(
        pump,
        [&] {
            return raw.connected;
        },
        "Sequence fixture did not connect");
    raw.send({0, 0, 0xffffffffu, Hello{42, "RAW", 0, false}});
    until(
        pump,
        [&] {
            return raw.welcome.has_value();
        },
        "Wrapped sequence Hello failed");
    const auto player = raw.welcome->player;
    raw.send({raw.generation, player, 1, SelectCar{9, true}});
    until(
        pump,
        [&] {
            return host.view().roster.players.back().car == 9;
        },
        "Sequence wrap fromffffffff to1 was rejected");
    const auto revision = host.view().roster.revision;
    const auto rejected_received = host.statistics().received_messages;
    raw.send({raw.generation, player, 0xffffffffu, SelectCar{1, false}});
    raw.send({raw.generation, player, 1, SelectCar{2, false}});
    raw.send({raw.generation, player, 2, Ready{host.view().settings.revision + 1, revision, true}});
    raw.send({raw.generation, player, 3, Ready{host.view().settings.revision, revision - 1, true}});
    raw.send({raw.generation, player, 4, Ready{host.view().settings.revision, revision + 1, true}});
    until(
        pump,
        [&] {
            return host.statistics().received_messages >= rejected_received + 5;
        },
        "Invalid readiness requests did not arrive");
    require(!host.view().roster.players.back().ready && host.view().roster.revision == revision, "Stale-choice, wrong-settings or future-roster Ready was accepted");
    raw.send({raw.generation, player, 5, Ready{host.view().settings.revision, revision, true}});
    until(
        pump,
        [&] {
            return host.view().roster.players.back().ready;
        },
        "Current revision-bound readiness was not accepted");
    require(host.view().roster.players.back().car == 9 && host.view().roster.revision == revision + 1, "Old/duplicate packets or stale readiness changed lobby state");
    raw.send({raw.generation + 1, player, 6, SelectCar{1, false}});
    until(
        pump,
        [&] {
            return raw.rejected.has_value() && host.view().roster.players.size() == 1;
        },
        "Wrong session generation was not removed");
    require(raw.rejected == RejectReason::invalid_message, "Wrong generation needs invalid-message rejection");
}

void client_snapshot_binding() {
    Endpoint host;
    Session client(1000);
    client.join({"127.0.0.1", host.local_port()}, {42, "CLIENT", 0, false});
    ConnectionId incoming = ConnectionId::invalid;
    bool hello = false;
    const auto pump = [&] {
        const auto now = monotonic_millis();
        client.pump(now);
        for (const auto& event : host.pump(now)) {
            if (event.type == TransportEventType::connected) {
                incoming = event.connection;
            } else if (event.type == TransportEventType::received) {
                const auto packet = decode(event.payload);
                require(bool(packet) && std::holds_alternative<Hello>(packet.packet->message), "Client sent unexpected initial message");
                hello = true;
            }
        }
    };
    until(
        pump,
        [&] {
            return hello;
        },
        "Snapshot fixture did not send Hello");
    std::uint32_t sequence = 0;
    const auto send = [&](Generation generation, PlayerId origin, Message message) {
        require(host.send(incoming, Channel::control, encode({generation, origin, ++sequence, std::move(message)})) == SendResult::queued, "Snapshot fixture could not send");
    };
    send(123, 1, Welcome{2, 1, 10, 20});
    send(123, 1, Roster{9, {{1, "HOST", 0, false}, {2, "CLIENT", 0, false}}});
    send(123, 1, LobbySettings{19, 1});
    until(
        pump,
        [&] {
            return client.statistics().received_messages >= 3;
        },
        "Initial stale snapshots were not received");
    require(client.view().phase == SessionPhase::connecting && client.view().roster.revision == 0 && client.view().settings.revision == 0,
        "Client exposed lobby before Welcome's advertised revisions arrived");
    send(123, 1, Roster{10, {{1, "HOST", 0, false}, {2, "CLIENT", 0, false}}});
    send(123, 1, LobbySettings{20, 2});
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::lobby;
        },
        "Current initial snapshots did not open lobby");
    send(123, 1, LobbySettings{19, 9});
    until(
        pump,
        [&] {
            return client.statistics().received_messages >= 6;
        },
        "Stale setting snapshot did not arrive");
    require(client.view().settings.track == 2, "Old snapshot replaced current host settings");
    send(124, 1, Reject{RejectReason::host_left});
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::failed;
        },
        "Wrong-generation host rejection was accepted");
    require(client.view().error == "Invalid session message" && client.view().roster.players.empty(), "Unbound host rejection reason bypassed generation validation");
}

void invalid_sender() {
    Session host(500);
    host.host(0, {42, "HOST", 0, false});
    Endpoint raw;
    const auto server = raw.connect("127.0.0.1", host.view().port);
    std::optional<Welcome> welcome;
    Generation generation = 0;
    bool rejected = false;
    const auto pump = [&] {
        const auto now = monotonic_millis();
        host.pump(now);
        for (const auto& event : raw.pump(now)) {
            if (event.type == TransportEventType::connected) {
                require(raw.send(server, Channel::control, encode({0, 0, 1, Hello{42, "RAW", 1, false}})) == SendResult::queued, "Hello send failed");
            } else if (event.type == TransportEventType::received) {
                const auto packet = decode(event.payload);
                require(bool(packet), "Host sent invalid packet");
                if (const auto* value = std::get_if<Welcome>(&packet.packet->message)) {
                    welcome = *value;
                    generation = packet.packet->generation;
                }
                rejected |= std::holds_alternative<Reject>(packet.packet->message);
            }
        }
    };
    until(
        pump,
        [&] {
            return welcome.has_value();
        },
        "Raw client did not join");
    require(raw.send(server, Channel::control, encode({generation, welcome->host, 2, SelectCar{5, false}})) == SendResult::queued, "Spoof send failed");
    until(
        pump,
        [&] {
            return rejected && host.view().roster.players.size() == 1;
        },
        "Spoofed host sender was not removed");
    require(host.view().roster.players.front().car == 0, "Spoof changed host car");
}

} // namespace toca::network::test::session
