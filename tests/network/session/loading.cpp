#include "support.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace toca::network::test::session {
//Three real, continuously serviced endpoints distinguish an absolute resource
//deadline from a transport timeout, including aggregate pause during loading.
void loading_recovery() {
    for (const auto reason : {LoadingAbortReason::cancelled, LoadingAbortReason::load_failed, LoadingAbortReason::loading_timeout}) {
        Session host(1000, reason == LoadingAbortReason::loading_timeout ? 150 : 2000), first(1000, 2000), waiting(1000, 2000);
        host.host(0, {42, "HOST", 0, false});
        const Address address{"127.0.0.1", host.view().port};
        first.join(address, {42, "FIRST", 0, false});
        waiting.join(address, {42, "WAITING", 0, false});
        const auto pump = pump_endpoints(host, first, waiting);
        until(
            pump,
            [&] {
                return host.view().roster.players.size() == 3 && same_roster(host, first, waiting);
            },
            "Loading-recovery peers did not synchronize");
        require(!host.abort_loading(reason), "Loading abort changed an ordinary lobby");
        require(host.view().host_endpoints.size() <= 16 && first.view().host_endpoints.empty() && waiting.view().host_endpoints.empty(),
            "LAN endpoint advertisement must be bounded and host-local");
        for (const auto& endpoint : host.view().host_endpoints) {
            const auto candidate = parse_address(endpoint);
            require(candidate.port == host.view().port && !candidate.ipv4.starts_with("127.") && !candidate.ipv4.starts_with("0."),
                "Advertised LAN candidate has an unusable IP or wrong bound port");
        }
        const auto first_actor = static_cast<ActorId>(std::find_if(host.view().roster.players.begin(), host.view().roster.players.end(), [&](const auto& player) {
            return player.player == first.view().local_player;
        }) - host.view().roster.players.begin());
        const auto waiting_actor = static_cast<ActorId>(std::find_if(host.view().roster.players.begin(), host.view().roster.players.end(), [&](const auto& player) {
            return player.player == waiting.view().local_player;
        }) - host.view().roster.players.begin());
        require(host.ready(true) && first.ready(true) && waiting.ready(true), "Loading-recovery commitments failed");
        until(
            pump,
            [&] {
                return std::all_of(host.view().roster.players.begin(), host.view().roster.players.end(), [](const auto& player) {
                    return player.ready;
                });
            },
            "Loading-recovery commitments did not arrive");
        const auto started = monotonic_millis();
        require(host.start_race(123, {0, 1, 2}), "Loading-recovery start failed");
        until(
            pump,
            [&] {
                return first.view().phase == SessionPhase::loading && waiting.view().phase == SessionPhase::loading;
            },
            "Loading-recovery Start did not arrive");
        require(!host.abort_loading(static_cast<LoadingAbortReason>(99)), "Invalid loading abort reason mutated the session");
        require(host.loaded() && first.loaded() && host.publish_race(Pause{true}), "Loading progress or loading pause failed");
        until(
            pump,
            [&] {
                const auto* race = waiting.race_view();
                return race && race->actors[0].loaded && race->actors[first_actor].loaded && !race->actors[waiting_actor].loaded && race->actors[0].pause_requested && race->paused;
            },
            "Responsive waiting peer did not receive loading and pause identities");
        require(host.view().phase == SessionPhase::loading && waiting.race_view()->actors[waiting_actor].active_human, "An incomplete active player was skipped by the barrier");
        if (reason != LoadingAbortReason::loading_timeout) {
            require(host.abort_loading(reason), "Host could not explicitly abort loading");
        }
        until(
            pump,
            [&] {
                return host.view().phase == SessionPhase::failed && first.view().phase == SessionPhase::failed && waiting.view().phase == SessionPhase::failed;
            },
            "Loading abort did not terminate every responsive peer");
        const auto expected = reason == LoadingAbortReason::cancelled     ? "Race loading cancelled"
                              : reason == LoadingAbortReason::load_failed ? "Unable to load the race"
                                                                          : "Race loading timed out";
        for (const auto* peer : {&host, &first, &waiting}) {
            require(
                peer->view().error == expected && peer->view().roster.players.empty() && !peer->race_view(), "Loading recovery lost the explicit reason or retained a stale race");
        }
        if (reason == LoadingAbortReason::loading_timeout) {
            require(monotonic_millis() - started >= 150 && monotonic_millis() - started < 1500, "Paused, responsive loading did not respect the absolute deadline");
        }
        until(
            pump,
            [&] {
                return !host.draining() && !first.draining() && !waiting.draining();
            },
            "Loading abort retained an endpoint after reliable close");
    }
    for (const auto reason : {LoadingAbortReason::cancelled, LoadingAbortReason::load_failed}) {
        Session host(1000, 2000), survivor(1000, 2000), broken(1000, 2000);
        host.host(0, {42, "HOST", 0, false});
        const Address address{"127.0.0.1", host.view().port};
        survivor.join(address, {42, "SURVIVOR", 0, false});
        broken.join(address, {42, "BROKEN", 0, false});
        const auto pump = pump_endpoints(host, survivor, broken);
        until(
            pump,
            [&] {
                return host.view().roster.players.size() == 3 && same_roster(host, survivor, broken);
            },
            "Client-failure peers did not synchronize");
        require(host.ready(true) && survivor.ready(true) && broken.ready(true), "Client-failure commitments failed");
        until(
            pump,
            [&] {
                return std::all_of(host.view().roster.players.begin(), host.view().roster.players.end(), [](const auto& player) {
                    return player.ready;
                });
            },
            "Client-failure commitments did not arrive");
        const auto departing = broken.view().local_player;
        const auto departing_actor = static_cast<ActorId>(std::find_if(host.view().roster.players.begin(), host.view().roster.players.end(), [&](const auto& player) {
            return player.player == departing;
        }) - host.view().roster.players.begin());
        require(host.start_race(321, {0, 1, 2}), "Client-failure race did not start");
        until(
            pump,
            [&] {
                return survivor.view().phase == SessionPhase::loading && broken.view().phase == SessionPhase::loading;
            },
            "Client-failure Start did not arrive");
        require(host.loaded() && survivor.loaded() && broken.abort_loading(reason), "Client could not cancel failed resources");
        until(
            pump,
            [&] {
                return host.view().phase == SessionPhase::grid && survivor.view().phase == SessionPhase::grid && !broken.draining();
            },
            "Client failure left the surviving barrier blocked");
        require(
            broken.view().phase == SessionPhase::failed && broken.view().error == (reason == LoadingAbortReason::cancelled ? "Race loading cancelled" : "Unable to load the race"),
            "Client loading failure lost its useful reason");
        const auto& actor = survivor.race_view()->actors[departing_actor];
        require(!actor.active_human && !actor.loaded && host.owns_actor(departing_actor), "Client failure fabricated Loaded or failed host takeover");
    }
}

void loading_grid_unwind() {
    //Cases0/1 abort the host,2/3 remove a client;4 verifies the racing boundary.
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        Session host(1000, 2000), client(1000, 2000);
        host.host(0, {42, "HOST", 0, false});
        client.join({"127.0.0.1", host.view().port}, {42, "CLIENT", 0, false});
        const auto pump = pump_endpoints(host, client);
        until(
            pump,
            [&] {
                return host.view().roster.players.size() == 2 && host.view().roster == client.view().roster;
            },
            "Grid-unwind fixture did not synchronize");
        require(host.ready(true) && client.ready(true), "Grid-unwind commitments failed");
        until(
            pump,
            [&] {
                return host.view().roster.players[1].ready;
            },
            "Grid-unwind commitment did not arrive");
        require(host.start_race(123, {0, 1}), "Grid-unwind start failed");
        until(
            pump,
            [&] {
                return client.view().phase == SessionPhase::loading;
            },
            "Grid-unwind Start did not arrive");
        require(host.loaded() && client.loaded(), "Grid-unwind loaded acknowledgement failed");
        until(
            pump,
            [&] {
                return host.view().phase == SessionPhase::grid && client.view().phase == SessionPhase::grid;
            },
            "Grid-unwind Go did not arrive");
        require(!host.abort_loading(LoadingAbortReason::loading_timeout) && !client.abort_loading(LoadingAbortReason::loading_timeout),
            "Loading timeout incorrectly aborted a released grid");
        if (scenario == 4) {
            require(host.begin_racing() && client.begin_racing(), "Grid-unwind fixture could not begin racing");
            for (const auto reason : {LoadingAbortReason::cancelled, LoadingAbortReason::load_failed, LoadingAbortReason::loading_timeout}) {
                require(!host.abort_loading(reason) && !client.abort_loading(reason), "Loader abort incorrectly terminated an active race");
            }
            require(host.view().phase == SessionPhase::racing && client.view().phase == SessionPhase::racing, "Rejected loader abort mutated racing phase");
            host.leave();
            until(
                pump,
                [&] {
                    return client.view().phase == SessionPhase::failed && !host.draining() && !client.draining();
                },
                "Grid-unwind racing fixture retained its connections");
            continue;
        }
        const auto reason = scenario % 2 == 0 ? LoadingAbortReason::cancelled : LoadingAbortReason::load_failed;
        const auto expected = reason == LoadingAbortReason::cancelled ? "Race loading cancelled" : "Unable to load the race";
        auto& aborting = scenario < 2 ? host : client;
        require(aborting.abort_loading(reason), "Explicit loader unwind was rejected after Go");
        until(
            pump,
            [&] {
                return client.view().phase == SessionPhase::failed && !host.draining() && !client.draining()
                       && (scenario < 2 ? host.view().phase == SessionPhase::failed : host.race_view() && !host.race_view()->actors[1].active_human);
            },
            "Grid loader unwind retained a connected or active failed player");
        require(aborting.view().error == expected && !aborting.race_view(), "Grid loader unwind lost its explicit reason or retained race resources");
        if (scenario < 2) {
            require(client.view().error == expected && !client.race_view(), "Host grid unwind did not propagate its reason to the client");
        } else {
            require(host.view().phase == SessionPhase::grid && host.owns_actor(1) && !host.race_view()->actors[1].loaded,
                "Client grid unwind damaged the remaining session or missed takeover");
        }
    }
}

} // namespace toca::network::test::session
