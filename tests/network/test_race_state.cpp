#include "network/race_state.hpp"
#include "network/session.hpp"

#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace toca::network;

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

const Roster roster{7, {{1, "HOST", 0, false}, {2, "FIRST", 1, true}, {3, "SECOND", 2, false}}};
const LobbySettings settings{9, 0, 0, 3, true};
const Start start{7, 9, 0x12345678, {7, 6, 5, 4, 3, 2, 1, 0}};

void recovered_cadence() {
    for (const auto [hint, level, interval] : std::array<std::array<unsigned, 3>, 12>{{{0, 0, 1}, {50, 0, 1}, {51, 1, 66}, {100, 1, 66}, {101, 2, 99}, {150, 2, 99}, {151, 4, 165},
             {200, 4, 165}, {201, 16, 561}, {500, 16, 561}, {501, 30, 1023}, {1000, 30, 1023}}}) {
        SourceSendCadence cadence(hint);
        require(cadence.level() == level && cadence.interval_ms() == interval, "Original initial cadence hint branch changed");
        cadence.sent(100);
        require(!cadence.due(100 + interval) && cadence.due(101 + interval), "State cadence lost original strict elapsed comparison");
    }
    SourceSendCadence cadence(0);
    require(!cadence.record_rtt(620) && cadence.level() == 0, "Four-sample155ms equality incorrectly raised level");
    require(cadence.record_rtt(4) && cadence.level() == 1, "Four-sample high threshold did not raise cadence level");
    for (unsigned i = 0; i < 64; ++i) {
        cadence.record_rtt(1000, false);
    }
    require(cadence.level() == 32 && cadence.interval_ms() == 3000, "Mismatched probe did not add two levels and clamp32");
    for (unsigned i = 0; i < 64; ++i) {
        cadence.record_rtt(0);
    }
    require(cadence.level() == 6 && cadence.interval_ms() == 231, "Recovered low-RTT downshift floor must remain level6");
    SourceSendCadence normal(200), alternate(200);
    require(normal.record_rtt(200) && !alternate.record_rtt(200, true, true), "Alternate210/170 thresholds were replaced by normal155/90");
}

void drain(RaceState& race) {
    while (race.take_event()) {
    }
}

void load(RaceState& host, RaceState& first, RaceState& second) {
    require(host.publish(Loaded{}).result == RouteResult::accepted && host.view().phase == RacePhase::loading, "Host loaded before its remote peers");
    auto request = first.publish(Loaded{});
    require(request.transmissions.size() == 1, "Client loading acknowledgement was not routed to host");
    require(host.receive(request.transmissions[0].packet, 2, Channel::control).result == RouteResult::accepted && host.view().phase == RacePhase::loading,
        "One loaded client released an incomplete barrier");
    request = second.publish(Loaded{});
    auto go = host.receive(request.transmissions[0].packet, 3, Channel::control);
    require(host.view().phase == RacePhase::grid && go.transmissions.size() == 2 && std::holds_alternative<Go>(go.transmissions.back().packet.message),
        "All loaded peers did not release Go");
    for (const auto& transmission : go.transmissions) {
        require(first.receive(transmission.packet, 1, Channel::control).result == RouteResult::accepted
                    && second.receive(transmission.packet, 1, Channel::control).result == RouteResult::accepted,
            "Clients rejected authoritative progress followed by host Go");
    }
    require(host.begin_racing() && first.begin_racing() && second.begin_racing(), "Shared grid did not enter racing");
    drain(host);
    drain(first);
    drain(second);
}

void ownership_and_freshness() {
    RaceState host(42, 1, 1, roster, settings, start), first(42, 2, 1, roster, settings, start), second(42, 3, 1, roster, settings, start);
    require(!host.publish(ActorState{0, {}}).transmissions.size(), "Car state was sent before loading completed");
    require(first.publish(Go{}).result == RouteResult::invalid, "Client bypassed loading barrier");
    load(host, first, second);
    require(host.owns(0) && host.owns(7) && !host.owns(1) && first.owns(1) && !first.owns(7), "Distributed human and host AI ownership changed");
    ActorState human{1, {}};
    human.state.position[0] = 123;
    ActorState ai{7, {}};
    ai.state.position[0] = 456;
    require(host.receive({42, 2, 10, ActorState{0, {}}}, 2, Channel::latest_state).result == RouteResult::invalid, "Client wrote host actor state");
    require(host.receive({42, 1, 10, human}, 2, Channel::latest_state).result == RouteResult::invalid, "Packet origin bypassed source connection binding");
    auto relay = host.receive({42, 2, 10, human}, 2, Channel::latest_state);
    require(relay.result == RouteResult::accepted && relay.transmissions.size() == 1 && relay.transmissions[0].except == 2, "Owner state was not relayed with sender exclusion");
    require(host.take_states().size() == 1, "Host did not expose remote state at the application boundary");
    require(second.receive({42, 1, 20, ai}, 1, Channel::latest_state).result == RouteResult::accepted
                && second.receive(relay.transmissions[0].packet, 1, Channel::latest_state).result == RouteResult::accepted,
        "A newer different actor suppressed a fresh state");
    require(second.receive({42, 2, 9, human}, 1, Channel::latest_state).result == RouteResult::ignored, "Old same-actor state was applied");
    require(second.receive({41, 2, 100, human}, 1, Channel::latest_state).result == RouteResult::ignored, "Old race generation mutated current race");
    require(second.receive({42, 2, 11, human}, 2, Channel::latest_state).result == RouteResult::invalid, "Client accepted state outside host routing");
    const auto states = second.take_states();
    require(states.size() == 2 && states[0].actor == 1 && states[0].state.position[0] == 123 && states[1].actor == 7 && states[1].state.position[0] == 456,
        "Independent actor snapshots lost typed fields");
    require(second.take_states().empty(), "State snapshots were delivered twice");
    require(host.receive({42, 2, 11, ActorInput{1, {}}}, 2, Channel::latest_state).result == RouteResult::invalid, "Actor controls bypassed channel contract");
    require(host.receive({42, 2, 11, ActorInput{1, {}}}, 2, Channel::input).result == RouteResult::accepted && host.take_inputs().size() == 1,
        "Changed controls did not route independently");
    require(host.publish(Gear{7, 2, false}).result == RouteResult::invalid && host.publish(DamageVisual{7, {}}).result == RouteResult::invalid,
        "Host AI invented local-human-only gear/damage publications");
    auto old = human;
    old.actor = 2;
    require(first.receive({42, 3, 0xfffffffeu, old}, 1, Channel::latest_state).result == RouteResult::accepted
                && first.receive({42, 3, 1, old}, 1, Channel::latest_state).result == RouteResult::accepted
                && first.receive({42, 3, 0xfffffffeu, old}, 1, Channel::latest_state).result == RouteResult::ignored,
        "Actor freshness did not handle sequence wrap");
}

void loading_progress_authority() {
    RaceState host(42, 1, 1, roster, settings, start), first(42, 2, 1, roster, settings, start), second(42, 3, 1, roster, settings, start);
    require(host.receive({42, 2, 1, RaceStatus{7, 0}}, 2, Channel::control).result == RouteResult::invalid && first.publish(RaceStatus{7, 0}).result == RouteResult::invalid,
        "A client manufactured authoritative loading progress");
    require(first.receive({42, 1, 1, RaceStatus{7, 0}}, 2, Channel::control).result == RouteResult::invalid
                && first.receive({42, 2, 1, RaceStatus{7, 0}}, 1, Channel::control).result == RouteResult::invalid,
        "Loading status bypassed host transport and origin authority");
    require(first.receive({41, 1, 1, RaceStatus{7, 0}}, 1, Channel::control).result == RouteResult::ignored && !first.view().actors[0].loaded,
        "Old generation status mutated the next loading barrier");
    auto status = host.publish(Loaded{});
    require(host.view().actors[0].loaded && !host.view().actors[1].loaded && status.transmissions.size() == 1, "Host loading progress was not published while waiting for clients");
    for (auto* peer : {&first, &second}) {
        require(
            peer->receive(status.transmissions[0].packet, 1, Channel::control).result == RouteResult::accepted && peer->view().actors[0].loaded && !peer->view().actors[1].loaded,
            "Clients did not receive the same partial loading progress");
    }
    require(first.receive({42, 1, 100, RaceStatus{0x87, 0}}, 1, Channel::control).result == RouteResult::invalid
                && first.receive({42, 1, 100, RaceStatus{1, 0x80}}, 1, Channel::control).result == RouteResult::invalid
                && first.receive({42, 1, 100, RaceStatus{0, 0}}, 1, Channel::control).result == RouteResult::invalid,
        "Status accepted pure-AI bits or rolled back an acknowledged loaded human");
    require(first.receive({42, 1, 100, Go{}}, 1, Channel::control).result == RouteResult::invalid, "Host Go skipped the public all-human loading barrier");
    const auto loaded = first.publish(Loaded{});
    require(!first.view().actors[1].loaded, "Local Loaded request was exposed as an unacknowledged authoritative status");
    status = host.receive(loaded.transmissions[0].packet, 2, Channel::control);
    for (auto* peer : {&first, &second}) {
        require(peer->receive(status.transmissions[0].packet, 1, Channel::control).result == RouteResult::accepted && peer->view().actors[0].loaded && peer->view().actors[1].loaded
                    && !peer->view().actors[2].loaded,
            "Other clients' loaded identities were hidden from public progress");
    }
    const auto departed = host.peer_left(3);
    for (const auto& transmission : departed.transmissions) {
        require(first.receive(transmission.packet, 1, transmission.channel).result == RouteResult::accepted, "Loading departure status did not reach the remaining human");
    }
    require(host.view().phase == RacePhase::grid && first.view().phase == RacePhase::grid && !host.view().actors[2].active_human && !host.view().actors[2].loaded
                && !first.view().actors[2].loaded,
        "Departed failed loader was marked Loaded instead of releasing its active-human barrier slot");
}

void events_pause_and_takeover() {
    RaceState host(42, 1, 1, roster, settings, start), first(42, 2, 1, roster, settings, start), second(42, 3, 1, roster, settings, start);
    load(host, first, second);
    auto pause = first.publish(Pause{true});
    auto aggregate = host.receive(pause.transmissions[0].packet, 2, Channel::control);
    require(host.view().paused && aggregate.transmissions.size() == 2, "Human pause did not publish its owner and aggregate at host");
    for (const auto& transmission : aggregate.transmissions) {
        require(second.receive(transmission.packet, 1, Channel::control).result == RouteResult::accepted, "Host pause status did not reach peer");
    }
    require(second.view().paused && second.view().actors[1].pause_requested && !second.view().actors[2].pause_requested, "Peer did not receive the actual pausing identity");
    pause = second.publish(Pause{true});
    aggregate = host.receive(pause.transmissions[0].packet, 3, Channel::control);
    require(aggregate.transmissions.size() == 1 && std::holds_alternative<RaceStatus>(aggregate.transmissions[0].packet.message),
        "Second pausing identity was hidden by unchanged aggregate");
    require(first.receive(aggregate.transmissions[0].packet, 1, Channel::control).result == RouteResult::accepted && first.view().actors[1].pause_requested
                && first.view().actors[2].pause_requested,
        "Not every pausing identity reached clients");
    pause = first.publish(Pause{false});
    aggregate = host.receive(pause.transmissions[0].packet, 2, Channel::control);
    require(aggregate.transmissions.size() == 1 && host.view().paused, "One human resumed while another was paused");
    require(second.receive(aggregate.transmissions[0].packet, 1, Channel::control).result == RouteResult::accepted && !second.view().actors[1].pause_requested
                && second.view().actors[2].pause_requested,
        "Resumed player remained in the public pause-owner set");
    const Packet gear{42, 2, 50, Gear{1, 3, false}};
    require(host.receive(gear, 2, Channel::control).result == RouteResult::accepted && host.receive(gear, 2, Channel::control).result == RouteResult::ignored,
        "Discrete gear event repeated");
    require(host.receive({42, 2, 51, Finish{1, max_finish_centiseconds + 1, 0}}, 2, Channel::control).result == RouteResult::invalid && host.view().finish_order.empty(),
        "Typed finish overflow mutated host classification");
    require(first.publish(Finish{1, max_finish_centiseconds + 1, 0}).result == RouteResult::invalid && !first.view().actors[1].finished,
        "Typed local finish overflow bypassed native millisecond bound");
    auto finish = first.publish(Finish{1, 1200, 0});
    // Use a fresh owner sequence after the deliberate synthetic gear packet.
    finish.transmissions[0].packet.sequence = 51;
    auto ordered = host.receive(finish.transmissions[0].packet, 2, Channel::control);
    require(ordered.transmissions.size() == 1 && host.view().finish_order[0] == Finish{1, 1200, 1}, "Host changed owner-authored finish time or missed ordinal");
    require(first.publish(Finish{1, 1, 0}).result == RouteResult::ignored, "Local finish was proposed repeatedly before host echo");
    require(first.receive(ordered.transmissions[0].packet, 1, Channel::control).result == RouteResult::accepted
                && second.receive(ordered.transmissions[0].packet, 1, Channel::control).result == RouteResult::accepted,
        "Finish ordinal did not agree on peers");
    ordered = host.publish(Finish{0, 1000, 0});
    require(host.view().finish_order.size() == 2 && host.view().finish_order[1] == Finish{0, 1000, 2}, "Results sorted source timestamps instead of append-on-event order");
    require(second.receive(ordered.transmissions[0].packet, 1, Channel::control).result == RouteResult::accepted, "Host local finish did not relay");
    require(first.receive(ordered.transmissions[0].packet, 1, Channel::control).result == RouteResult::accepted, "Host local finish did not reach finished owner");
    require(host.receive({42, 3, 40, Finish{2, 1300, 8}}, 3, Channel::control).result == RouteResult::invalid, "Owner selected final result ordinal");
    const auto status = host.peer_left(3);
    require(host.view().phase == RacePhase::results && !host.view().paused && host.owns(2), "Departure did not clear pause, transfer ownership or finish active-human barrier");
    for (const auto& transmission : status.transmissions) {
        require(first.receive(transmission.packet, 1, transmission.channel).result == RouteResult::accepted, "Host departure status failed to route");
    }
    require(!first.view().actors[2].active_human, "Inactive human was not recorded");
    require(first.receive({42, 3, 1000, ActorState{2, {}}}, 1, Channel::latest_state).result == RouteResult::ignored, "In-flight old owner state undid takeover");
    require(first.receive({42, 1, 1, ActorState{2, {}}}, 1, Channel::latest_state).result == RouteResult::accepted, "Host takeover could not reset per-actor freshness");
    const auto frozen_order = host.view().finish_order;
    require(host.publish(Finish{2, 1400, 0}).result == RouteResult::ignored && host.publish(Finish{7, 1400, 0}).result == RouteResult::ignored
                && host.view().finish_order == frozen_order && !host.view().actors[2].finished && !host.view().actors[7].finished,
        "Late inactive/AI finish changed the classified result order");
    require(first.view().phase == RacePhase::results && first.receive({42, 1, 1000, Finish{7, 1400, 3}}, 1, Channel::control).result == RouteResult::ignored
                && first.view().finish_order == frozen_order && !first.view().actors[7].finished,
        "Late relayed AI finish changed classified client results");
    require(first.publish(Finish{1, 1200, 0}).result == RouteResult::ignored, "Duplicate classified local finish was republished");
    std::size_t finish_events = 0;
    while (const auto event = host.take_event()) {
        finish_events += std::holds_alternative<Finish>(*event);
    }
    require(finish_events == 2, "Duplicate or late finish changed classified events or result ordering");
}

void bounded_events() {
    RaceState host(42, 1, 1, roster, settings, start), first(42, 2, 1, roster, settings, start), second(42, 3, 1, roster, settings, start);
    load(host, first, second);
    RouteResult result = RouteResult::accepted;
    for (std::uint32_t sequence = 2; sequence < 1000 && result != RouteResult::queue_full; ++sequence) {
        result = host.receive({42, 2, sequence, Gear{1, std::uint8_t(sequence % 6), false}}, 2, Channel::control).result;
    }
    require(result == RouteResult::queue_full, "Discrete race event queue grew without a bound");
    require(host.receive({42, 2, 1000, ActorState{1, {}}}, 2, Channel::latest_state).result == RouteResult::accepted && host.take_states().size() == 1,
        "Reliable event pressure blocked fresh state channel");
    std::size_t count = 0;
    while (host.take_event()) {
        ++count;
    }
    require(count <= 128, "Bounded race event consumer exceeded its declared queue");
}

void until(const std::function<void()>& pump, const std::function<bool()>& done, const char* message) {
    const auto deadline = monotonic_millis() + 3000;
    do {
        pump();
        if (done()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (monotonic_millis() < deadline);
    throw std::runtime_error(message);
}

void actual_session_race() {
    Session host(1000), client(1000);
    host.host(0, {42, "HOST", 0, false});
    client.join({"127.0.0.1", host.view().port}, {42, "CLIENT", 1, true});
    const auto pump = [&] {
        const auto now = monotonic_millis();
        host.pump(now);
        client.pump(now);
    };
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::lobby;
        },
        "Race session fixture did not connect");
    require(!host.start_race(123, {1, 0}) && !client.start_race(123, {1, 0}), "Race bypassed readiness or host-only start");
    require(client.ready(true), "Client readiness failed");
    until(
        pump,
        [&] {
            return host.view().roster.players.back().ready;
        },
        "Client readiness did not arrive");
    require(host.ready(true) && host.can_start_race(), "Host could not commit ready lobby");
    const auto old_generation = host.view().generation;
    require(host.start_race(123, {1, 0}), "Ready host failed to start shared race");
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::loading;
        },
        "Shared Start did not reach client");
    require(host.view().generation != old_generation && client.view().generation == host.view().generation && client.race_view()->grid == std::vector<ActorId>{1, 0},
        "Race generation/grid differed");
    require(!host.select_car(3, false) && !client.ready(false), "Lobby mutation remained active during race loading");
    require(host.loaded() && host.view().phase == SessionPhase::loading && client.loaded(), "Load acknowledgement failed");
    until(
        pump,
        [&] {
            return host.view().phase == SessionPhase::grid && client.view().phase == SessionPhase::grid;
        },
        "Loading barrier did not release common grid");
    require(host.begin_racing() && client.begin_racing(), "Grid failed to become live race");
    require(!client.publish_race(Finish{1, 0xffffffffu, 0}) && !client.race_view()->actors[1].finished, "Invalid local finish field mutated race before codec validation");
    ActorState state{1, {}};
    state.state.position[0] = 0x123456;
    require(client.publish_race(state), "Owner could not publish actual state");
    bool received = false;
    until(
        pump,
        [&] {
            const auto states = host.take_race_states();
            received |= states.size() == 1 && states[0].state.position[0] == state.state.position[0];
            return received;
        },
        "Actual remote car state did not reach application queue");
    const auto before_sent = client.race_statistics().state_link_sent;
    const auto burst_time = monotonic_millis();
    for (unsigned index = 0; index < 1000; ++index) {
        state.state.position[0] = static_cast<std::int32_t>(index);
        require(client.publish_state_due(1, state.state, burst_time), "Tick-facing state publication failed");
    }
    require(client.race_statistics().state_link_sent - before_sent <= 1 && client.race_statistics().state_replaced >= 998 && client.race_statistics().peak_pending_states == 1,
        "Presentation-frequency state publications bypassed cadence or grew the latest queue");
    until(
        pump,
        [&] {
            for (const auto& latest : host.take_race_states()) {
                if (latest.state.position[0] == 999) {
                    return true;
                }
            }
            return false;
        },
        "Cadence gate did not eventually send the freshest actor state");
    toca::simulation::RemoteControls controls;
    require(client.publish_controls_changed(1, controls) && client.publish_controls_changed(1, controls), "Changed control publication failed");
    controls.source_flags = 1u << 24;
    require(client.publish_controls_changed(1, controls), "Source-excluded filter flag update failed");
    controls.throttle_q16 = 1000;
    require(client.publish_controls_changed(1, controls), "Changed packed throttle failed");
    require(client.race_statistics().controls_changed == 2 && client.race_statistics().controls_unchanged == 2, "Controls stopped using original packed on-change mask");
    until(
        pump,
        [&] {
            for (const auto& input : host.take_race_inputs()) {
                if (input.controls.throttle_q16 == 1000) {
                    return true;
                }
            }
            return false;
        },
        "Changed controls did not reach actual owner receiver");
    require(client.publish_race(Finish{1, 1200, 0}), "Owner finish failed");
    until(
        pump,
        [&] {
            return client.race_view()->finish_order.size() == 1 && host.race_view()->finish_order.size() == 1;
        },
        "Owner finish did not receive common ordinal");
    require(host.publish_race(Finish{0, 1000, 0}), "Host finish failed");
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::results;
        },
        "Shared result barrier did not complete");
    require(host.race_view()->finish_order == client.race_view()->finish_order && host.race_view()->finish_order[0].actor == 1,
        "Two actual endpoints disagreed on append result order");
    const auto finished_generation = host.view().generation;
    require(!client.return_to_lobby() && host.return_to_lobby(), "Only host may coordinate a shared results-to-lobby return");
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::lobby && client.view().roster == host.view().roster;
        },
        "Shared result return did not restore synchronized lobby");
    require(host.view().generation != finished_generation && host.view().generation == client.view().generation && !host.race_view() && !client.race_view(),
        "Lobby return retained old race generation/state");
    for (const auto& player : client.view().roster.players) {
        require(!player.ready, "Lobby return retained previous ready commitment");
    }
    host.leave();
    until(
        pump,
        [&] {
            return client.view().phase == SessionPhase::failed;
        },
        "Host loss during results did not terminate session");
    require(!client.race_view() && client.take_race_states().empty() && !client.take_race_event(), "Old race state/events survived host loss");
}

void loss_during_load_and_pause() {
    auto host = std::make_unique<Session>(300);
    Session leaving(300), remaining(300);
    host->host(0, {42, "HOST", 0, false});
    leaving.join({"127.0.0.1", host->view().port}, {42, "LEAVING", 1, false});
    remaining.join({"127.0.0.1", host->view().port}, {42, "STAYING", 2, false});
    const auto pump = [&] {
        const auto now = monotonic_millis();
        if (host) {
            host->pump(now);
        }
        leaving.pump(now);
        remaining.pump(now);
    };
    until(
        pump,
        [&] {
            return leaving.view().phase == SessionPhase::lobby && remaining.view().phase == SessionPhase::lobby && host->view().roster.players.size() == 3;
        },
        "Loading-loss fixture did not connect");
    require(host->ready(true), "Host readiness failed");
    until(
        pump,
        [&] {
            return leaving.view().roster == host->view().roster;
        },
        "Host commitment did not synchronize");
    require(leaving.ready(true), "First client readiness failed");
    until(
        pump,
        [&] {
            return remaining.view().roster == host->view().roster && host->view().roster.players[1].ready;
        },
        "First client commitment did not synchronize");
    require(remaining.ready(true), "Second client readiness failed");
    until(
        pump,
        [&] {
            return std::all_of(host->view().roster.players.begin(), host->view().roster.players.end(), [](const auto& player) {
                return player.ready;
            });
        },
        "Three-player readiness did not complete");
    require(host->start_race(123, {2, 1, 0}), "Three-player race did not start");
    until(
        pump,
        [&] {
            return leaving.race_view() && remaining.race_view();
        },
        "Shared loading did not arrive");
    require(host->loaded() && remaining.loaded(), "Surviving peers could not acknowledge loading");
    leaving.leave();
    until(
        pump,
        [&] {
            return host->view().phase == SessionPhase::grid && remaining.view().phase == SessionPhase::grid;
        },
        "Departed loading player blocked barrier");
    require(host->owns_actor(1) && !remaining.race_view()->actors[1].active_human, "Loading departure did not transfer its actor to host");
    require(host->begin_racing() && remaining.begin_racing(), "Surviving grid did not start");
    require(remaining.publish_race(Pause{true}), "Surviving player could not pause");
    until(
        pump,
        [&] {
            return host->race_view()->paused && remaining.race_view()->paused;
        },
        "Pause aggregation did not reach all peers");
    host.reset();
    until(
        pump,
        [&] {
            return remaining.view().phase == SessionPhase::failed;
        },
        "Hard host loss froze while race was paused");
    require(remaining.view().error == "Connection timed out" && !remaining.race_view() && remaining.take_race_inputs().empty(),
        "Paused host loss retained remote inputs or stale race");
}
} // namespace

int main() {
    try {
        recovered_cadence();
        ownership_and_freshness();
        loading_progress_authority();
        events_pause_and_takeover();
        bounded_events();
        actual_session_race();
        loss_during_load_and_pause();
        std::cout << "PASS: race ownership, actor freshness, load barrier, ordered events/results, pause, takeover and bounded queues\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
