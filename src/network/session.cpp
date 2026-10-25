#include "network/session.hpp"

#include <algorithm>
#include <charconv>
#include <random>
#include <stdexcept>

namespace toca::network {
namespace {
bool newer(std::uint32_t value, std::uint32_t previous) {
    return value != 0 && (previous == 0 || (value != previous && value - previous < 0x80000000u));
}

bool at_least(std::uint32_t value, std::uint32_t minimum) {
    return value != 0 && (value == minimum || newer(value, minimum));
}

std::uint32_t advance(std::uint32_t& value) {
    if (++value == 0) {
        ++value;
    }
    return value;
}
} // namespace

std::string_view phase_name(SessionPhase phase) {
    switch (phase) {
        case SessionPhase::idle:
            return "idle";
        case SessionPhase::connecting:
            return "connecting";
        case SessionPhase::lobby:
            return "lobby";
        case SessionPhase::loading:
            return "loading";
        case SessionPhase::grid:
            return "grid";
        case SessionPhase::racing:
            return "racing";
        case SessionPhase::results:
            return "results";
        case SessionPhase::failed:
            return "failed";
    }
    throw std::logic_error("Invalid network phase");
}

std::string_view rejection_text(RejectReason reason) {
    switch (reason) {
        case RejectReason::protocol:
            return "Incompatible game protocol";
        case RejectReason::content:
            return "Game data differs from the host";
        case RejectReason::full:
            return "Session full";
        case RejectReason::started:
            return "Race already started";
        case RejectReason::invalid_message:
            return "Invalid session message";
        case RejectReason::host_left:
            return "Host left the session";
        case RejectReason::timeout:
            return "Connection timed out";
        case RejectReason::not_committed:
            return "Race started without you";
        case RejectReason::loading_cancelled:
            return "Race loading cancelled";
        case RejectReason::load_failed:
            return "Unable to load the race";
        case RejectReason::loading_timeout:
            return "Race loading timed out";
    }
    return "Connection ended";
}

Address parse_address(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || text.find(':', colon + 1) != std::string_view::npos) {
        throw std::runtime_error("Enter an IPv4 address and port, for example 192.168.1.20:27015");
    }
    Address address{std::string(text.substr(0, colon)), 0};
    const auto port = text.substr(colon + 1);
    const auto parsed = std::from_chars(port.data(), port.data() + port.size(), address.port);
    if (port.empty() || parsed.ec != std::errc{} || parsed.ptr != port.data() + port.size() || address.port == 0) {
        throw std::runtime_error("Port must be between 1 and 65535");
    }
    auto remaining = text.substr(0, colon);
    for (unsigned octet = 0; octet < 4; ++octet) {
        const auto dot = remaining.find('.');
        const auto piece = remaining.substr(0, dot);
        unsigned value = 256;
        const auto result = std::from_chars(piece.data(), piece.data() + piece.size(), value);
        if (piece.empty() || piece.size() > 3 || result.ec != std::errc{} || result.ptr != piece.data() + piece.size() || value > 255
            || (octet == 3) != (dot == std::string_view::npos)) {
            throw std::runtime_error("Enter four IPv4 numbers from 0 to 255");
        }
        remaining.remove_prefix(piece.size() + (dot == std::string_view::npos ? 0 : 1));
    }
    return address;
}

Session::Session(std::uint32_t timeout_ms, std::uint32_t loading_timeout_ms) : timeout_ms_(timeout_ms), loading_timeout_ms_(loading_timeout_ms) {
    if (timeout_ms < 100 || timeout_ms > 60000) {
        throw std::invalid_argument("Session timeout must be between100 and60000ms");
    }
    if (loading_timeout_ms < 100 || loading_timeout_ms > 600000) {
        throw std::invalid_argument("Loading timeout must be between100 and600000ms");
    }
}

void Session::host(std::uint16_t port, Hello identity) {
    (void)encode({0, 0, 1, identity});
    leave();
    endpoint_ = std::make_unique<Endpoint>(TransportConfig{"0.0.0.0", port, 16, timeout_ms_});
    identity_ = std::move(identity);
    view_ = {};
    view_.phase = SessionPhase::lobby;
    view_.hosting = true;
    view_.generation = std::random_device{}();
    if (view_.generation == 0) {
        view_.generation = 1;
    }
    view_.local_player = view_.host_player = 1;
    view_.port = endpoint_->local_port();
    for (const auto& address : local_ipv4_addresses()) {
        view_.host_endpoints.push_back(address + ":" + std::to_string(view_.port));
    }
    view_.settings.revision = 1;
    view_.roster = {1, {{1, identity_.name, identity_.car, identity_.manual, false}}};
    view_.roster.players.front().sensitivity = identity_.sensitivity;
    view_.revision = 1;
    next_player_ = 2;
    sequence_ = 0;
    final_statistics_ = {};
    race_statistics_ = {};
    last_controls_ = {};
}

void Session::join(Address address, Hello identity) {
    (void)parse_address(address.ipv4 + ":" + std::to_string(address.port));
    (void)encode({0, 0, 1, identity});
    leave();
    endpoint_ = std::make_unique<Endpoint>(TransportConfig{"0.0.0.0", 0, 1, timeout_ms_});
    identity_ = std::move(identity);
    view_ = {};
    view_.phase = SessionPhase::connecting;
    view_.revision = 1;
    server_ = endpoint_->connect(address.ipv4, address.port);
    peers_[server_] = {};
    connect_deadline_ = monotonic_millis() + timeout_ms_;
    sequence_ = 0;
    initial_roster_revision_ = initial_settings_revision_ = 0;
    final_statistics_ = {};
    race_statistics_ = {};
    last_controls_ = {};
}

bool Session::send(ConnectionId peer, Message message) {
    const auto bytes = encode({view_.generation, view_.local_player, advance(sequence_), std::move(message)});
    return endpoint_->send(peer, Channel::control, bytes) == SendResult::queued;
}

void Session::broadcast(Message message) {
    for (auto& [connection, peer] : peers_) {
        if (peer.player && !peer.closing && !send(connection, message)) {
            peer.closing = true;
            endpoint_->disconnect(connection, static_cast<std::uint32_t>(RejectReason::timeout));
        }
    }
}

void Session::changed_roster() {
    advance(view_.roster.revision);
    ++view_.revision;
    broadcast(view_.roster);
}

void Session::reject(ConnectionId connection, RejectReason reason) {
    const auto found = peers_.find(connection);
    if (found == peers_.end()) {
        return;
    }
    (void)send(connection, Reject{reason});
    found->second.closing = true;
    endpoint_->disconnect(connection, static_cast<std::uint32_t>(reason));
}

void Session::fail(RejectReason reason, bool await_remote_close) {
    view_.phase = SessionPhase::failed;
    view_.error = rejection_text(reason);
    view_.roster = {};
    view_.local_player = view_.host_player = 0;
    ++view_.revision;
    if (endpoint_) {
        final_statistics_ = endpoint_->statistics();
        // Keep servicing the disconnect handshake after publishing the error.
        // In particular, the host may still be awaiting the Reject ACK before
        // its disconnect command can be sent.
        if ((reason != RejectReason::timeout || await_remote_close) && server_ != ConnectionId::invalid) {
            // A Reject sender already schedules disconnect_later. Keep ACKing
            // that reliable message and let its one-sided close finish; starting
            // a competing ENet disconnect can leave one side waiting for timeout.
            if (!await_remote_close) {
                endpoint_->disconnect(server_, static_cast<std::uint32_t>(reason));
            }
            retiring_peers_ = {server_};
            retiring_ = std::move(endpoint_);
            retire_deadline_ = monotonic_millis() + timeout_ms_;
        }
    }
    peers_.clear();
    race_.reset();
    loading_deadline_ = 0;
    endpoint_.reset();
    server_ = ConnectionId::invalid;
}

void Session::remove_peer(ConnectionId connection) {
    const auto found = peers_.find(connection);
    if (found == peers_.end()) {
        return;
    }
    const auto player = found->second.player;
    peers_.erase(found);
    if (player) {
        if (race_) {
            const bool admitted = std::any_of(race_->view().actors.begin(), race_->view().actors.end(), [&](const auto& actor) {
                return actor.player == player;
            });
            if (admitted && !dispatch_race(race_->peer_left(player))) {
                fail(RejectReason::invalid_message);
            }
            ++view_.revision;
            return;
        }
        std::erase_if(view_.roster.players, [player](const auto& entry) {
            return entry.player == player;
        });
        changed_roster();
    }
}

void Session::receive(ConnectionId connection, const Packet& packet, Channel channel) {
    auto found = peers_.find(connection);
    if (found == peers_.end() || found->second.closing) {
        return;
    }
    auto& peer = found->second;
    if (std::holds_alternative<ReturnLobby>(packet.message)) {
        if (view_.hosting) {
            reject(connection, RejectReason::invalid_message);
            return;
        }
        if (!newer(packet.sequence, peer.sequence)) {
            return;
        }
        if (channel != Channel::control || view_.phase != SessionPhase::results || packet.origin != view_.host_player || !newer(packet.generation, view_.generation)) {
            fail(RejectReason::invalid_message);
            return;
        }
        peer.sequence = packet.sequence;
        for (auto& entry : peers_) {
            entry.second.pending_states = {};
        }
        race_.reset();
        view_.generation = packet.generation;
        view_.phase = SessionPhase::lobby;
        for (auto& player : view_.roster.players) {
            player.ready = false;
        }
        ++view_.revision;
        return;
    }
    if (race_message(packet.message)) {
        if (!peer.player || !race_) {
            if (peer.player && !race_ && view_.phase == SessionPhase::lobby && packet.generation != view_.generation) {
                return;
            }
            if (view_.hosting) {
                reject(connection, RejectReason::invalid_message);
            } else {
                fail(RejectReason::invalid_message);
            }
            return;
        }
        const auto result = race_->receive(packet, peer.player, channel);
        if (const auto* state = std::get_if<ActorState>(&packet.message)) {
            if (result.result == RouteResult::accepted) {
                ++race_statistics_.state_received;
                ++race_statistics_.actor_state_received[state->actor];
            } else if (result.result == RouteResult::ignored) {
                ++race_statistics_.state_discarded;
            }
        } else if (std::holds_alternative<ActorInput>(packet.message) && result.result == RouteResult::accepted) {
            ++race_statistics_.controls_received;
        }
        if (result.result == RouteResult::invalid || result.result == RouteResult::queue_full) {
            if (view_.hosting) {
                reject(connection, RejectReason::invalid_message);
            } else {
                fail(RejectReason::invalid_message);
            }
        } else {
            (void)dispatch_race(result);
        }
        return;
    }
    if (channel != Channel::control) {
        if (view_.hosting) {
            reject(connection, RejectReason::invalid_message);
        } else {
            fail(RejectReason::invalid_message);
        }
        return;
    }
    if (!newer(packet.sequence, peer.sequence)) {
        return;
    }
    peer.sequence = packet.sequence;
    if (view_.hosting) {
        if (peer.player == 0) {
            const auto* hello = std::get_if<Hello>(&packet.message);
            if (!hello || packet.generation != 0 || packet.origin != 0) {
                reject(connection, RejectReason::invalid_message);
                return;
            }
            if (hello->content_crc32 != identity_.content_crc32) {
                reject(connection, RejectReason::content);
                return;
            }
            if (view_.phase != SessionPhase::lobby) {
                reject(connection, RejectReason::started);
                return;
            }
            if (view_.roster.players.size() >= max_players) {
                reject(connection, RejectReason::full);
                return;
            }
            if (next_player_ == 0) {
                reject(connection, RejectReason::full);
                return;
            }
            auto roster_revision = view_.roster.revision;
            if (!send(connection, Welcome{next_player_, view_.host_player, advance(roster_revision), view_.settings.revision}) || !send(connection, view_.settings)) {
                reject(connection, RejectReason::timeout);
                return;
            }
            peer.player = next_player_++;
            view_.roster.players.push_back({peer.player, hello->name, hello->car, hello->manual, false});
            view_.roster.players.back().sensitivity = hello->sensitivity;
            changed_roster();
            peer.ready_roster_floor = view_.roster.revision;
            peer.ready_settings_floor = view_.settings.revision;
            return;
        }
        if (packet.generation != view_.generation || packet.origin != peer.player) {
            reject(connection, RejectReason::invalid_message);
            return;
        }
        if (view_.phase != SessionPhase::lobby && !std::holds_alternative<Leave>(packet.message)) {
            reject(connection, RejectReason::started);
            return;
        }
        const auto player = std::ranges::find(view_.roster.players, peer.player, &LobbyPlayer::player);
        if (std::holds_alternative<Leave>(packet.message)) {
            endpoint_->disconnect(connection);
            remove_peer(connection);
        } else if (const auto* choice = std::get_if<SelectCar>(&packet.message); choice && player != view_.roster.players.end()) {
            if (player->car == choice->car && player->manual == choice->manual) {
                return;
            }
            player->car = choice->car;
            player->manual = choice->manual;
            player->ready = false;
            changed_roster();
            peer.ready_roster_floor = view_.roster.revision;
        } else if (const auto* request = std::get_if<Ready>(&packet.message); request && player != view_.roster.players.end()) {
            // Waiting-room entry commits this player's car, not other players'
            // choices or the host's current options. Only this player's own car
            // change advances its confirmation floor. Never accept unseen future
            // revisions, pre-admission settings, or pre-choice confirmations.
            if (at_least(request->settings_revision, peer.ready_settings_floor) && at_least(view_.settings.revision, request->settings_revision)
                && at_least(request->roster_revision, peer.ready_roster_floor) && at_least(view_.roster.revision, request->roster_revision)) {
                if (player->ready != request->ready) {
                    player->ready = request->ready;
                    changed_roster();
                }
            }
        } else {
            reject(connection, RejectReason::invalid_message);
        }
        return;
    }
    if (connection != server_) {
        return;
    }
    if (const auto* rejection = std::get_if<Reject>(&packet.message)) {
        if (view_.local_player ? packet.generation != view_.generation || packet.origin != view_.host_player
                               : view_.phase != SessionPhase::connecting || packet.generation == 0 || packet.origin == 0) {
            fail(RejectReason::invalid_message);
        } else {
            fail(rejection->reason, true);
        }
        return;
    }
    if (const auto* welcome = std::get_if<Welcome>(&packet.message)) {
        if (view_.phase != SessionPhase::connecting || view_.local_player || packet.generation == 0 || packet.origin != welcome->host || welcome->host == welcome->player
            || welcome->roster_revision == 0 || welcome->settings_revision == 0) {
            fail(RejectReason::invalid_message);
            return;
        }
        view_.generation = packet.generation;
        view_.local_player = welcome->player;
        view_.host_player = welcome->host;
        peer.player = welcome->host;
        initial_roster_revision_ = welcome->roster_revision;
        initial_settings_revision_ = welcome->settings_revision;
        ++view_.revision;
        return;
    }
    if (const auto* start = std::get_if<Start>(&packet.message)) {
        if (view_.phase != SessionPhase::lobby || !view_.local_player || packet.origin != view_.host_player || !newer(packet.generation, view_.generation)) {
            fail(RejectReason::invalid_message);
            return;
        }
        if (!std::all_of(view_.roster.players.begin(), view_.roster.players.end(), [](const auto& player) {
                return player.ready;
            })) {
            fail(RejectReason::invalid_message);
            return;
        }
        try {
            race_ = std::make_unique<RaceState>(packet.generation, view_.local_player, view_.host_player, view_.roster, view_.settings, *start);
        } catch (const std::invalid_argument&) {
            fail(RejectReason::invalid_message);
            return;
        }
        view_.generation = packet.generation;
        loading_deadline_ = monotonic_millis() + loading_timeout_ms_;
        reset_race_streams();
        update_race_phase();
        return;
    }
    if (view_.local_player == 0 || packet.generation != view_.generation || packet.origin != view_.host_player
        || (view_.phase != SessionPhase::connecting && view_.phase != SessionPhase::lobby)) {
        fail(RejectReason::invalid_message);
        return;
    }
    if (const auto* roster = std::get_if<Roster>(&packet.message)) {
        if (!newer(roster->revision, view_.roster.revision) || !at_least(roster->revision, initial_roster_revision_)) {
            return;
        }
        const auto contains = [&](PlayerId id) {
            return std::ranges::any_of(roster->players, [id](const auto& p) {
                return p.player == id;
            });
        };
        if (!contains(view_.local_player) || !contains(view_.host_player)) {
            fail(RejectReason::invalid_message);
            return;
        }
        view_.roster = *roster;
    } else if (const auto* settings = std::get_if<LobbySettings>(&packet.message)) {
        if (!newer(settings->revision, view_.settings.revision) || !at_least(settings->revision, initial_settings_revision_)) {
            return;
        }
        view_.settings = *settings;
    } else {
        fail(RejectReason::invalid_message);
        return;
    }
    if (view_.roster.revision && view_.settings.revision) {
        view_.phase = SessionPhase::lobby;
    }
    ++view_.revision;
}

void Session::pump(MonotonicMillis now) {
    if (retiring_) {
        std::vector<TransportEvent> retired_events;
        try {
            retired_events = retiring_->pump(now);
        } catch (const std::runtime_error&) {
            retiring_peers_.clear();
        }
        for (const auto& event : retired_events) {
            if (event.type == TransportEventType::disconnected) {
                std::erase(retiring_peers_, event.connection);
            }
        }
        if (now >= retire_deadline_ || retiring_peers_.empty()) {
            if (!endpoint_) {
                final_statistics_ = retiring_->statistics();
            }
            retiring_.reset();
            retiring_peers_.clear();
        }
    }
    if (!endpoint_) {
        return;
    }
    //Native safety policy: a responsive peer may keep ENet alive forever
    //without completing resources. This absolute wall deadline is independent
    //of all heartbeat activity, focus, rendering and simulation pause clocks.
    if (view_.phase == SessionPhase::loading && loading_deadline_ != 0 && now >= loading_deadline_) {
        (void)abort_loading(LoadingAbortReason::loading_timeout);
        return;
    }
    std::vector<TransportEvent> events;
    try {
        events = endpoint_->pump(now);
    } catch (const std::runtime_error&) {
        fail(RejectReason::timeout);
        return;
    }
    for (const auto& event : events) {
        if (!endpoint_) {
            break;
        }
        if (event.type == TransportEventType::connected) {
            if (view_.hosting) {
                peers_[event.connection] = {0, 0, now + timeout_ms_, false};
            } else if (event.connection == server_) {
                if (!send(server_, identity_)) {
                    fail(RejectReason::timeout);
                }
            }
        } else if (event.type == TransportEventType::disconnected) {
            if (view_.hosting) {
                remove_peer(event.connection);
            } else {
                fail(
                    event.cause == DisconnectCause::timeout || event.reason == static_cast<std::uint32_t>(RejectReason::timeout) ? RejectReason::timeout : RejectReason::host_left);
            }
        } else {
            const auto packet = decode(event.payload);
            if (packet) {
                receive(event.connection, *packet.packet, event.channel);
            } else {
                const auto reason = packet.error == DecodeError::version ? RejectReason::protocol : RejectReason::invalid_message;
                if (view_.hosting) {
                    reject(event.connection, reason);
                } else {
                    fail(reason);
                }
            }
        }
    }
    if (!endpoint_) {
        return;
    }
    if (view_.phase == SessionPhase::connecting && now >= connect_deadline_) {
        fail(RejectReason::timeout);
        return;
    }
    if (view_.hosting) {
        for (const auto& [connection, peer] : peers_) {
            if (!peer.player && !peer.closing && now >= peer.hello_deadline) {
                reject(connection, RejectReason::timeout);
            }
        }
    }
    (void)flush_states(now);
}

void Session::leave() {
    if (endpoint_) {
        retiring_peers_.clear();
        for (const auto& [connection, peer] : peers_) {
            if (!view_.hosting && !view_.local_player) {
                continue;
            }
            if (!peer.closing) {
                if (view_.hosting) {
                    (void)send(connection, Reject{RejectReason::host_left});
                } else {
                    (void)send(connection, Leave{});
                }
            }
            endpoint_->disconnect(connection, static_cast<std::uint32_t>(RejectReason::host_left));
            retiring_peers_.push_back(connection);
        }
        (void)endpoint_->pump(monotonic_millis());
        final_statistics_ = endpoint_->statistics();
        if (!retiring_peers_.empty()) {
            retiring_ = std::move(endpoint_);
            retire_deadline_ = monotonic_millis() + timeout_ms_;
        }
    }
    endpoint_.reset();
    peers_.clear();
    race_.reset();
    loading_deadline_ = 0;
    server_ = ConnectionId::invalid;
    const auto revision = view_.revision + 1;
    view_ = {};
    view_.revision = revision;
}

bool Session::draining() const {
    return retiring_ != nullptr;
}

bool Session::select_car(std::uint8_t car, bool manual) {
    if (view_.phase != SessionPhase::lobby || car > 9) {
        return false;
    }
    if (!view_.hosting) {
        return send(server_, SelectCar{car, manual});
    }
    auto& player = view_.roster.players.front();
    if (player.car == car && player.manual == manual) {
        return true;
    }
    player.car = car;
    player.manual = manual;
    player.ready = false;
    changed_roster();
    return true;
}

bool Session::ready(bool value) {
    if (view_.phase != SessionPhase::lobby) {
        return false;
    }
    if (!view_.hosting) {
        return send(server_, Ready{view_.settings.revision, view_.roster.revision, value});
    }
    if (view_.roster.players.front().ready == value) {
        return true;
    }
    view_.roster.players.front().ready = value;
    changed_roster();
    return true;
}

bool Session::set_settings(LobbySettings settings) {
    if (!view_.hosting || view_.phase != SessionPhase::lobby) {
        return false;
    }
    settings.revision = view_.settings.revision;
    if (settings == view_.settings) {
        return true;
    }
    advance(settings.revision);
    (void)encode({view_.generation, view_.local_player, 1, settings});
    view_.settings = settings;
    broadcast(settings);
    ++view_.revision;
    return true;
}

bool Session::can_start_race() const {
    return view_.hosting && view_.phase == SessionPhase::lobby && !view_.roster.players.empty() && view_.roster.players.front().player == view_.host_player
           && view_.roster.players.front().ready && std::none_of(peers_.begin(), peers_.end(), [](const auto& entry) {
                  return entry.second.player && entry.second.closing;
              });
}

bool Session::start_race(std::uint32_t seed, std::vector<ActorId> grid) {
    if (!can_start_race()) {
        return false;
    }
    auto generation = view_.generation;
    advance(generation);
    auto committed = view_.roster;
    std::erase_if(committed.players, [](const auto& player) {
        return !player.ready;
    });
    const bool trimmed = committed.players.size() != view_.roster.players.size();
    if (trimmed) {
        advance(committed.revision);
    }
    Start start{committed.revision, view_.settings.revision, seed, std::move(grid)};
    std::unique_ptr<RaceState> next_race;
    try {
        next_race = std::make_unique<RaceState>(generation, view_.local_player, view_.host_player, committed, view_.settings, start);
    } catch (const std::invalid_argument&) {
        return false;
    }
    for (const auto& [connection, peer] : peers_) {
        if (peer.player && std::none_of(committed.players.begin(), committed.players.end(), [&](const auto& player) {
                return player.player == peer.player;
            })) {
            reject(connection, RejectReason::not_committed);
        }
    }
    if (trimmed) {
        view_.roster = std::move(committed);
        ++view_.revision;
        // Commit actor membership on the old generation before Start advances
        // it. Reliable control ordering makes every admitted peer use this map.
        broadcast(view_.roster);
    }
    view_.generation = generation;
    loading_deadline_ = monotonic_millis() + loading_timeout_ms_;
    race_ = std::move(next_race);
    reset_race_streams();
    broadcast(start);
    update_race_phase();
    return true;
}

void Session::update_race_phase() {
    if (!race_) {
        return;
    }
    race_statistics_.generation = race_->view().generation;
    race_statistics_.actors = race_->view().actors.size();
    race_statistics_.active_humans = static_cast<std::size_t>(std::count_if(race_->view().actors.begin(), race_->view().actors.end(), [](const auto& actor) {
        return actor.active_human;
    }));
    race_statistics_.finish_count = race_->view().finish_order.size();
    race_statistics_.phase = race_->view().phase;
    race_statistics_.paused = race_->view().paused;
    race_statistics_.finish_order = race_->view().finish_order;
    for (const auto& actor : race_->view().actors) {
        if (actor.player == view_.local_player) {
            race_statistics_.local_actor = actor.actor;
        }
    }
    SessionPhase phase = SessionPhase::loading;
    switch (race_->view().phase) {
        case RacePhase::loading:
            phase = SessionPhase::loading;
            break;
        case RacePhase::grid:
            phase = SessionPhase::grid;
            break;
        case RacePhase::racing:
            phase = SessionPhase::racing;
            break;
        case RacePhase::results:
            phase = SessionPhase::results;
            break;
    }
    if (view_.phase != phase) {
        view_.phase = phase;
        ++view_.revision;
    }
}

bool Session::dispatch_race(RaceDispatch dispatch) {
    if (dispatch.result == RouteResult::invalid || dispatch.result == RouteResult::queue_full) {
        return false;
    }
    for (const auto& transmission : dispatch.transmissions) {
        // Latest states remain typed until the link's cadence permits sending.
        const auto bytes = transmission.channel == Channel::latest_state ? std::vector<std::uint8_t>{} : encode(transmission.packet);
        for (auto& [connection, peer] : peers_) {
            if (!peer.player || peer.closing || peer.player == transmission.except || (transmission.destination && transmission.destination != peer.player)) {
                continue;
            }
            if (transmission.channel == Channel::latest_state) {
                if (!transmission.actor || *transmission.actor >= max_players) {
                    return false;
                }
                auto& pending = peer.pending_states[*transmission.actor];
                if (pending) {
                    ++race_statistics_.state_replaced;
                }
                pending = transmission.packet;
                std::size_t count = 0;
                for (const auto& [other_connection, other] : peers_) {
                    (void)other_connection;
                    count += static_cast<std::size_t>(std::count_if(other.pending_states.begin(), other.pending_states.end(), [](const auto& state) {
                        return state.has_value();
                    }));
                }
                race_statistics_.peak_pending_states = std::max(race_statistics_.peak_pending_states, count);
                continue;
            }
            const auto result = endpoint_->send(connection, transmission.channel, bytes, transmission.channel == Channel::latest_state ? transmission.actor : std::nullopt);
            if (result != SendResult::queued && result != SendResult::replaced) {
                if (!view_.hosting) {
                    fail(RejectReason::timeout);
                    return false;
                }
                peer.closing = true;
                endpoint_->disconnect(connection, static_cast<std::uint32_t>(RejectReason::timeout));
            }
        }
    }
    update_race_phase();
    return true;
}

bool Session::publish_race(Message message) {
    if (!race_) {
        return false;
    }
    try {
        (void)encode({view_.generation, view_.local_player, 1, message});
    } catch (const std::invalid_argument&) {
        return false;
    }
    const bool state = std::holds_alternative<ActorState>(message);
    const auto result = dispatch_race(race_->publish(std::move(message)));
    if (result && state) {
        ++race_statistics_.state_publications;
    }
    return result;
}

bool Session::publish_state_due(ActorId actor, const simulation::RemoteMotionState& state, MonotonicMillis now) {
    // Tick-facing publication replaces the latest value. The independently
    // serviced pump applies per-link deadlines and flushes it onto UDP.
    (void)now;
    return publish_race(ActorState{actor, state});
}

void Session::reset_race_streams() {
    race_statistics_ = {};
    last_controls_ = {};
    for (auto& [connection, peer] : peers_) {
        (void)connection;
        peer.pending_states = {};
        peer.last_state_sent = {};
        peer.cadence_initialized = false;
        peer.last_rtt_sample = 0;
    }
}

bool Session::publish_controls_changed(ActorId actor, const simulation::RemoteControls& controls) {
    if (!race_ || !owns_actor(actor)) {
        return false;
    }
    std::array<std::uint8_t, 8> packed;
    try {
        packed = pack_source_axes(controls);
    } catch (const std::invalid_argument&) {
        return false;
    }
    if (last_controls_[actor] && !source_axes_changed(*last_controls_[actor], packed)) {
        ++race_statistics_.controls_unchanged;
        return true;
    }
    if (!publish_race(ActorInput{actor, controls})) {
        return false;
    }
    last_controls_[actor] = packed;
    ++race_statistics_.controls_changed;
    return true;
}

bool Session::flush_states(MonotonicMillis now) {
    if (!race_ || !endpoint_) {
        return true;
    }
    for (auto& [connection, peer] : peers_) {
        if (!peer.player || peer.closing) {
            continue;
        }
        const auto rtt = endpoint_->round_trip_time_ms(connection);
        if (!rtt) {
            continue;
        }
        if (!peer.cadence_initialized) {
            peer.cadence = SourceSendCadence(*rtt);
            peer.cadence_initialized = true;
            peer.last_rtt_sample = now;
        } else if (now >= peer.last_rtt_sample && now - peer.last_rtt_sample > 1000) {
            // Native host-star adaptation: ENet measures each UDP link. Apply
            // original four-sample/level arithmetic once per source probe period,
            // rather than reusing a cached RTT on each render/physics update.
            (void)peer.cadence.record_rtt(*rtt);
            peer.last_rtt_sample = now;
            ++race_statistics_.rtt_samples;
        }
        race_statistics_.last_rtt_ms = *rtt;
        race_statistics_.max_cadence_level = std::max(race_statistics_.max_cadence_level, peer.cadence.level());
        race_statistics_.max_cadence_interval_ms = std::max(race_statistics_.max_cadence_interval_ms, peer.cadence.interval_ms());
        for (ActorId actor = 0; actor < max_players; ++actor) {
            auto& pending = peer.pending_states[actor];
            if (!pending) {
                continue;
            }
            if (peer.last_state_sent[actor] && (now < *peer.last_state_sent[actor] || now - *peer.last_state_sent[actor] <= peer.cadence.interval_ms())) {
                ++race_statistics_.cadence_suppressed;
                continue;
            }
            const auto result = endpoint_->send(connection, Channel::latest_state, encode(*pending), actor);
            if (result == SendResult::queued || result == SendResult::replaced) {
                peer.last_state_sent[actor] = now;
                pending.reset();
                ++race_statistics_.state_link_sent;
            } else {
                if (!view_.hosting) {
                    fail(RejectReason::timeout);
                    return false;
                }
                peer.closing = true;
                peer.pending_states = {};
                endpoint_->disconnect(connection, static_cast<std::uint32_t>(RejectReason::timeout));
                break;
            }
        }
    }
    endpoint_->flush_outbound();
    return true;
}

bool Session::return_to_lobby() {
    if (!view_.hosting || !race_ || view_.phase != SessionPhase::results) {
        return false;
    }
    advance(view_.generation);
    broadcast(ReturnLobby{});
    std::erase_if(view_.roster.players, [&](const auto& player) {
        if (player.player == view_.local_player) {
            return false;
        }
        return std::none_of(peers_.begin(), peers_.end(), [&](const auto& entry) {
            return entry.second.player == player.player && !entry.second.closing;
        });
    });
    for (auto& entry : peers_) {
        entry.second.pending_states = {};
    }
    race_.reset();
    view_.phase = SessionPhase::lobby;
    for (auto& player : view_.roster.players) {
        player.ready = false;
    }
    changed_roster();
    for (auto& [connection, peer] : peers_) {
        (void)connection;
        peer.ready_roster_floor = view_.roster.revision;
        peer.ready_settings_floor = view_.settings.revision;
    }
    return true;
}

bool Session::owns_actor(ActorId actor) const {
    return race_ && race_->owns(actor);
}

bool Session::loaded() {
    if (view_.phase == SessionPhase::loading && loading_deadline_ != 0 && monotonic_millis() >= loading_deadline_) {
        (void)abort_loading(LoadingAbortReason::loading_timeout);
        return false;
    }
    return publish_race(Loaded{});
}

bool Session::abort_loading(LoadingAbortReason reason) {
    //The native loader may still be unwinding final audio/resources after its
    //Loaded acknowledgement releases Go. Keep explicit failure/cancel available
    //on that grid boundary, before simulation has begun; deadlines stay loading-only.
    const bool loading_phase = view_.phase == SessionPhase::loading || (view_.phase == SessionPhase::grid && reason != LoadingAbortReason::loading_timeout);
    if (!loading_phase || !race_ || (reason != LoadingAbortReason::cancelled && reason != LoadingAbortReason::load_failed && reason != LoadingAbortReason::loading_timeout)) {
        return false;
    }
    const auto rejection = reason == LoadingAbortReason::cancelled     ? RejectReason::loading_cancelled
                           : reason == LoadingAbortReason::load_failed ? RejectReason::load_failed
                                                                       : RejectReason::loading_timeout;
    if (view_.hosting) {
        //Send each explicit reason before Leave retires the endpoint. Marking
        //the peer closing prevents replacement by a generic host-left reason.
        for (auto& [connection, peer] : peers_) {
            if (peer.player && !peer.closing) {
                reject(connection, rejection);
            }
        }
    }
    leave();
    view_.phase = SessionPhase::failed;
    view_.error = rejection_text(rejection);
    ++view_.revision;
    return true;
}

bool Session::begin_racing() {
    if (!race_ || !race_->begin_racing()) {
        return false;
    }
    update_race_phase();
    return true;
}

const RaceView* Session::race_view() const {
    return race_ ? &race_->view() : nullptr;
}

std::vector<ActorState> Session::take_race_states() {
    auto states = race_ ? race_->take_states() : std::vector<ActorState>{};
    race_statistics_.state_consumed += states.size();
    return states;
}

std::vector<ActorInput> Session::take_race_inputs() {
    auto inputs = race_ ? race_->take_inputs() : std::vector<ActorInput>{};
    race_statistics_.controls_consumed += inputs.size();
    return inputs;
}

std::optional<Message> Session::take_race_event() {
    auto event = race_ ? race_->take_event() : std::optional<Message>{};
    if (event) {
        ++race_statistics_.events_consumed;
    }
    return event;
}

const SessionView& Session::view() const {
    return view_;
}

TransportStatistics Session::statistics() const {
    return endpoint_ ? endpoint_->statistics() : (retiring_ ? retiring_->statistics() : final_statistics_);
}

const RaceStatistics& Session::race_statistics() const {
    return race_statistics_;
}
} // namespace toca::network
