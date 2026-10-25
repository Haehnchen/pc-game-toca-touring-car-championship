#include "support.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace toca::network::test::session {
void membership() {
    Session host(500), client(500), other(500);
    host.host(0, {0x12345678, "HOST", 0, false});
    const Address address{"127.0.0.1", host.view().port};
    client.join(address, {0x12345678, "CLIENT", 1, true});
    other.join(address, {0x12345678, "OTHER", 2, false});
    const auto pump = pump_endpoints(host, client, other);
    const auto synchronized = [&] {
        return same_roster(host, client, other);
    };
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 3 && synchronized();
        },
        "Three players did not receive the same roster");
    require(client.view().local_player != other.view().local_player && client.view().local_player != host.view().local_player, "Player identities must be unique");
    require(client.select_car(3, false), "Client car request rejected");
    until(
        pump,
        [&] {
            return synchronized() && host.view().roster.players[1].car == 3;
        },
        "Car choice did not reach all peers");
    require(client.ready(true), "Client ready request rejected");
    until(
        pump,
        [&] {
            return synchronized() && host.view().roster.players[1].ready;
        },
        "Ready did not reach peers");
    auto settings = host.view().settings;
    settings.track = 1;
    require(host.set_settings(settings) && !client.set_settings(settings), "Only host can set shared settings");
    until(
        pump,
        [&] {
            return synchronized() && client.view().settings.track == 1 && other.view().settings.track == 1;
        },
        "Settings did not reach peers");
    require(client.view().roster.players[1].ready, "Host settings must not remove a committed waiting-room player");
    require(host.select_car(9, true) && client.select_car(8, true), "Unlocked Linkup bonus car IDs8/9 were rejected");
    until(
        pump,
        [&] {
            return synchronized() && host.view().roster.players[0].car == 9 && host.view().roster.players[1].car == 8;
        },
        "Bonus selections did not synchronize");
    require(!client.select_car(10, false), "Out-of-range car was accepted");
    require(host.ready(true), "Host ready request rejected");
    until(
        pump,
        [&] {
            return synchronized() && client.view().roster.players.front().ready;
        },
        "Host readiness did not synchronize");
    const auto ready_revision = host.view().roster.revision;
    require(host.select_car(9, true) && host.set_settings(host.view().settings) && host.ready(true), "Unchanged lobby actions should succeed");
    require(host.view().roster.revision == ready_revision && host.view().roster.players.front().ready, "Unchanged choices invalidated readiness");
    require(other.select_car(9, false), "Duplicate car model was rejected");
    until(
        pump,
        [&] {
            return synchronized() && host.view().roster.players[2].car == 9;
        },
        "Duplicate car model did not synchronize");
    require(host.view().roster.players.front().ready, "Another member's changed car must preserve waiting-room commitment");
    const auto old_id = client.view().local_player;
    client.leave();
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 2 && host.view().roster == other.view().roster;
        },
        "Leave did not remove a player");
    client.join(address, {0x12345678, "CLIENT", 1, false});
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 3 && synchronized();
        },
        "Rejoin failed");
    require(client.view().local_player != old_id, "Rejoin must not reuse stale actor identity");
    host.leave();
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::failed && other.view().phase == SessionPhase::failed;
        },
        "Host departure did not end client sessions");
    require(client.view().roster.players.empty() && client.view().error == "Host left the session", "Host loss must clear roster and explain departure");
}

void concurrent_readiness() {
    Session host(1000), first(1000), second(1000);
    host.host(0, {42, "HOST", 0, false});
    const Address address{"127.0.0.1", host.view().port};
    first.join(address, {42, "FIRST", 0, false});
    second.join(address, {42, "SECOND", 0, false});
    const auto pump = pump_endpoints(host, first, second);
    const auto synchronized = [&] {
        return same_roster(host, first, second);
    };
    const auto all_ready = [&] {
        return std::all_of(host.view().roster.players.begin(), host.view().roster.players.end(), [](const auto& player) {
            return player.ready;
        });
    };
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 3 && synchronized();
        },
        "Concurrent commitment fixture did not synchronize");
    const auto revision = host.view().roster.revision;
    require(host.ready(true) && first.ready(true) && second.ready(true), "Simultaneous waiting-room entry could not queue");
    until(
        pump,
        [&] {
            return all_ready() && synchronized();
        },
        "Concurrent waiting-room commitments were silently lost");
    require(host.view().roster.revision == revision + 3, "Concurrent commitment did not publish each accepted status change");
    const auto received = host.statistics().received_messages;
    require(first.select_car(1, true) && first.ready(true), "Own-choice stale commitment fixture could not queue");
    until(
        pump,
        [&] {
            return host.statistics().received_messages >= received + 2 && synchronized();
        },
        "Own-choice stale commitment did not arrive");
    require(host.view().roster.players[0].ready && !host.view().roster.players[1].ready && host.view().roster.players[2].ready,
        "Own car edit must reject its stale confirmation without ejecting other waiting-room players");
    auto settings = host.view().settings;
    settings.track = 1;
    settings.car_boost = false;
    require(host.set_settings(settings) && first.ready(true), "Concurrent host options and waiting-room entry could not queue");
    until(
        pump,
        [&] {
            return all_ready() && synchronized() && first.view().settings == host.view().settings && second.view().settings == host.view().settings;
        },
        "A host options change invalidated another player's committed car");
    require(host.select_car(2, false), "Host own car edit was rejected");
    require(!host.can_start_race() && !host.view().roster.players[0].ready && host.view().roster.players[1].ready && host.view().roster.players[2].ready,
        "Host own car edit must uncommit only the host");
    require(host.ready(true), "Host waiting-room return was rejected");
    until(
        pump,
        [&] {
            return all_ready() && synchronized();
        },
        "Host recommitment did not synchronize");
    second.leave();
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 2 && host.view().roster == first.view().roster;
        },
        "Waiting-room departure did not remove membership");
    require(host.view().roster.players[0].ready && host.view().roster.players[1].ready, "Other departure cleared committed waiting-room choices");
}

void committed_subset_start() {
    Session host(1000), committed(1000), garage(1000);
    host.host(0, {42, "HOST", 0, false});
    const Address address{"127.0.0.1", host.view().port};
    committed.join(address, {42, "COMMITTED", 1, false});
    const auto pump = pump_endpoints(host, committed, garage);
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 2 && host.view().roster == committed.view().roster;
        },
        "Committed subset fixture did not synchronize");
    require(host.ready(true) && committed.ready(true), "Waiting-room commitments could not queue");
    until(
        pump,
        [&] {
            return host.view().roster == committed.view().roster && host.view().roster.players.back().ready;
        },
        "Committed player did not reach waiting room");
    garage.join(address, {42, "GARAGE", 2, false});
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 3 && same_roster(host, committed, garage);
        },
        "Garage player did not join existing waiting room");
    require(host.view().roster.players[0].ready && host.view().roster.players[1].ready && !host.view().roster.players[2].ready && host.can_start_race(),
        "Uncommitted newcomer blocked or cleared the source waiting room");
    const auto original_generation = host.view().generation;
    require(!host.start_race(123, {0, 1, 2}) && host.view().roster.players.size() == 3 && garage.view().phase == SessionPhase::lobby,
        "Invalid subset grid changed membership before validation");
    require(host.start_race(123, {1, 0}), "Host could not start its committed subset");
    until(
        pump,
        [&] {
            return committed.race_view() && garage.view().phase == SessionPhase::failed && !garage.draining();
        },
        "Committed subset Start or uncommitted rejection failed");
    require(host.view().generation != original_generation && host.view().generation == committed.view().generation && host.view().roster == committed.view().roster
                && host.view().roster.players.size() == 2 && committed.race_view()->actors.size() == 2,
        "Shared race did not commit its trimmed actor mapping before advancing generation");
    require(garage.view().error == "Race started without you" && garage.view().roster.players.empty(), "Uncommitted garage player needs the source exclusion reason");
    const auto settled = monotonic_millis() + 100;
    until(
        pump,
        [&] {
            return monotonic_millis() >= settled;
        },
        "Excluded peer close did not settle");
    require(host.view().phase == SessionPhase::loading && host.view().roster.players.size() == 2, "Excluded peer disconnect terminated the admitted race");
}

void capacity() {
    Session host(1000);
    host.host(0, {42, "HOST", 0, false});
    std::vector<std::unique_ptr<Session>> clients;
    const auto pump = [&] {
        const auto now = monotonic_millis();
        host.pump(now);
        for (auto& client : clients) {
            client->pump(now);
        }
    };
    for (unsigned index = 0; index < 8; ++index) {
        auto client = std::make_unique<Session>(1000);
        client->join({"127.0.0.1", host.view().port}, {42, "PLAYER" + std::to_string(index), 0, false});
        clients.push_back(std::move(client));
        until(
            pump,
            [&] {
                return clients.back()->view().phase != SessionPhase::connecting;
            },
            "Capacity connection did not resolve");
    }
    require(host.view().roster.players.size() == 8 && clients.back()->view().error == "Session full", "Eight humans including host must fit and ninth must receive Full");
    for (unsigned index = 0; index < 7; ++index) {
        until(
            pump,
            [&] {
                return clients[index]->view().roster == host.view().roster;
            },
            "Capacity roster differs");
    }
    clients.back()->leave();
    clients.back()->join({"127.0.0.1", host.view().port}, {43, "MISMATCH", 0, false});
    until(
        pump,
        [&] {
            return clients.back()->view().phase == SessionPhase::failed;
        },
        "Incompatible content was accepted");
    require(clients.back()->view().error == "Game data differs from the host", "Content mismatch needs visible reason");
    clients.front()->leave();
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 7;
        },
        "A departing member did not release capacity");
    clients.back()->join({"127.0.0.1", host.view().port}, {42, "REPLACED", 9, true, 3});
    until(
        pump,
        [&] {
            return host.view().roster.players.size() == 8 && clients.back()->view().phase == SessionPhase::lobby;
        },
        "Full rejection did not leave a reusable session slot");
    require(clients.back()->view().roster.players.back().sensitivity == 3, "Hello sensitivity was not retained in the authoritative roster");
}

} // namespace toca::network::test::session
