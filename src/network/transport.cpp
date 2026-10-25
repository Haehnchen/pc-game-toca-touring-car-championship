#include "network/transport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <enet/enet.h>
#include <mutex>
#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#include <iphlpapi.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

namespace toca::network {
namespace {
constexpr std::size_t channel_count = 3;
constexpr std::size_t max_reliable_packets = 256;
constexpr std::size_t max_unreliable_packets = 64;

struct Runtime {
    Runtime() {
        if (enet_initialize() != 0) {
            throw std::runtime_error("ENet initialization failed");
        }
    }

    ~Runtime() {
        enet_deinitialize();
    }
};

std::shared_ptr<Runtime> acquire_runtime() {
    static std::mutex mutex;
    static std::weak_ptr<Runtime> instance;
    const std::lock_guard lock(mutex);
    auto runtime = instance.lock();
    if (!runtime) {
        runtime = std::make_shared<Runtime>();
        instance = runtime;
    }
    return runtime;
}

struct Budget {
    std::size_t reliable_bytes = 0;
    std::size_t reliable_packets = 0;
    std::size_t unreliable_bytes = 0;
    std::size_t unreliable_packets = 0;
};

struct PacketBudget {
    std::shared_ptr<Budget> budget;
    bool reliable;
};

void release_packet(ENetPacket* packet) {
    const std::unique_ptr<PacketBudget> token(static_cast<PacketBudget*>(packet->userData));
    if (token->reliable) {
        token->budget->reliable_bytes -= packet->dataLength;
        --token->budget->reliable_packets;
    } else {
        token->budget->unreliable_bytes -= packet->dataLength;
        --token->budget->unreliable_packets;
    }
}
} // namespace

MonotonicMillis monotonic_millis() {
    return static_cast<MonotonicMillis>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::vector<std::string> local_ipv4_addresses() {
    std::vector<std::string> result;
    const auto append = [&](const void* raw) {
        const auto* octets = static_cast<const unsigned char*>(raw);
        if (octets[0] == 0 || octets[0] == 127 || octets[0] >= 224 || result.size() >= 16) {
            return;
        }
        auto address = std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." + std::to_string(octets[2]) + "." + std::to_string(octets[3]);
        if (std::find(result.begin(), result.end(), address) == result.end()) {
            result.push_back(std::move(address));
        }
    };
#if defined(_WIN32)
    //IP Helper is a Windows system library. No DNS lookup, adapter activation,
    //subnet query or remote discovery is performed. Bound the OS buffer to64KiB.
    ULONG size = 16384;
    std::vector<unsigned char> storage(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    ULONG status = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &size);
    if (status == ERROR_BUFFER_OVERFLOW && size <= 65536) {
        storage.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        status = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &size);
    }
    if (status == NO_ERROR) {
        std::size_t count = 0;
        for (auto* adapter = adapters; adapter && count++ < 4096; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }
            for (auto* entry = adapter->FirstUnicastAddress; entry && count++ < 4096; entry = entry->Next) {
                if (entry->Address.lpSockaddr && entry->Address.lpSockaddr->sa_family == AF_INET && entry->Address.iSockaddrLength >= static_cast<int>(sizeof(sockaddr_in))) {
                    append(&reinterpret_cast<const sockaddr_in*>(entry->Address.lpSockaddr)->sin_addr);
                }
            }
        }
    }
#elif defined(__unix__) || defined(__APPLE__)
    ifaddrs* addresses = nullptr;
    if (getifaddrs(&addresses) == 0) {
        const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> owned(addresses, freeifaddrs);
        std::size_t count = 0;
        for (const auto* entry = addresses; entry && count++ < 4096; entry = entry->ifa_next) {
            if (entry->ifa_addr && entry->ifa_addr->sa_family == AF_INET && (entry->ifa_flags & IFF_UP) != 0 && (entry->ifa_flags & IFF_LOOPBACK) == 0) {
                append(&reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr);
            }
        }
    }
#endif
    std::sort(result.begin(), result.end());
    return result;
}

struct Endpoint::Impl {
    struct Peer {
        ENetPeer* handle = nullptr;
        ConnectionId connection = ConnectionId::invalid;
        std::shared_ptr<Budget> budget = std::make_shared<Budget>();
        std::array<std::optional<Bytes>, max_players> states;
        MonotonicMillis last_activity = 0;
        enet_uint32 last_receive = 0;
        bool clock_started = false;
        bool connected = false;
        bool disconnecting = false;
    };

    std::shared_ptr<Runtime> runtime = acquire_runtime();
    TransportConfig config;
    ENetHost* host = nullptr;
    std::vector<Peer> peers;
    std::uint32_t next_connection = 1;
    TransportStatistics stats;
    std::optional<MonotonicMillis> last_pump;

    explicit Impl(const TransportConfig& value) : config(value) {
        if (config.max_peers < 1 || config.max_peers > 16 || config.max_events_per_pump < 1 || config.max_events_per_pump > 256 || config.timeout_ms < 100
            || config.timeout_ms > 60000) {
            throw std::invalid_argument("Invalid bounded network transport configuration");
        }
        peers.resize(config.max_peers);
        ENetAddress address{};
        address.port = config.port;
        if (enet_address_set_host_ip(&address, config.bind_address.c_str()) != 0) {
            throw std::invalid_argument("Network bind address must be IPv4");
        }
        host = enet_host_create(&address, config.max_peers, channel_count, 0, 0);
        if (!host) {
            throw std::runtime_error("Unable to create ENet endpoint");
        }
        host->maximumPacketSize = max_message_bytes;
        // Upstream checks the aggregate before adding a packet. Reserve one
        // maximum packet so even that addition cannot exceed 64 KiB.
        host->maximumWaitingData = max_reliable_bytes - max_message_bytes;
    }

    ~Impl() {
        if (host) {
            // Packet callbacks run before peer budgets and Runtime are released.
            enet_host_destroy(host);
        }
    }

    Peer* find(ConnectionId connection) {
        const auto found = std::find_if(peers.begin(), peers.end(), [connection](const Peer& peer) {
            return peer.handle && peer.connection == connection;
        });
        return found == peers.end() ? nullptr : &*found;
    }

    Peer& bind(ENetPeer* handle) {
        auto& peer = peers.at(static_cast<std::size_t>(handle - host->peers));
        if (peer.handle != handle) {
            if (next_connection == 0) {
                throw std::runtime_error("Network connection ID space exhausted");
            }
            peer = Peer{};
            peer.handle = handle;
            peer.connection = static_cast<ConnectionId>(next_connection++);
            peer.last_receive = handle->lastReceiveTime;
            enet_peer_timeout(handle, 2, static_cast<enet_uint32>(config.timeout_ms), static_cast<enet_uint32>(config.timeout_ms));
            enet_peer_ping_interval(handle, static_cast<enet_uint32>(std::min<MonotonicMillis>(500, config.timeout_ms / 4)));
        }
        return peer;
    }

    void update_peaks() {
        std::size_t reliable = 0, unreliable = 0, pending = 0;
        for (const auto& peer : peers) {
            reliable += peer.budget->reliable_bytes;
            unreliable += peer.budget->unreliable_bytes;
            pending += static_cast<std::size_t>(std::count_if(peer.states.begin(), peer.states.end(), [](const auto& state) {
                return state.has_value();
            }));
        }
        stats.peak_reliable_bytes = std::max(stats.peak_reliable_bytes, reliable);
        stats.peak_unreliable_bytes = std::max(stats.peak_unreliable_bytes, unreliable);
        stats.peak_pending_states = std::max(stats.peak_pending_states, pending);
    }

    SendResult send_packet(Peer& peer, Channel channel, std::span<const std::uint8_t> bytes) {
        const bool reliable = channel == Channel::control;
        auto& budget = *peer.budget;
        const auto current_bytes = reliable ? budget.reliable_bytes : budget.unreliable_bytes;
        const auto current_packets = reliable ? budget.reliable_packets : budget.unreliable_packets;
        if (bytes.size() > max_reliable_bytes - current_bytes || current_packets >= (reliable ? max_reliable_packets : max_unreliable_packets)) {
            ++stats.queue_full;
            return SendResult::queue_full;
        }
        auto token = std::make_unique<PacketBudget>(PacketBudget{peer.budget, reliable});
        const auto flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
        ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(), flags);
        if (!packet) {
            throw std::bad_alloc();
        }
        if (enet_peer_send(peer.handle, static_cast<enet_uint8>(channel), packet) != 0) {
            enet_packet_destroy(packet);
            ++stats.queue_full;
            return SendResult::queue_full;
        }
        if (reliable) {
            budget.reliable_bytes += bytes.size();
            ++budget.reliable_packets;
        } else {
            budget.unreliable_bytes += bytes.size();
            ++budget.unreliable_packets;
        }
        packet->freeCallback = release_packet;
        packet->userData = token.release();
        ++stats.sent_messages;
        update_peaks();
        return SendResult::queued;
    }

    void flush_states() {
        for (auto& peer : peers) {
            if (!peer.handle || !peer.connected || peer.disconnecting) {
                continue;
            }
            for (auto& state : peer.states) {
                if (state && send_packet(peer, Channel::latest_state, *state) == SendResult::queued) {
                    state.reset();
                }
            }
        }
    }

    TransportEvent receive_event(const ENetEvent& event, MonotonicMillis now) {
        auto& peer = bind(event.peer);
        const bool silent = peer.clock_started && now - peer.last_activity >= config.timeout_ms;
        // Dispatching an old queued packet is not fresh wire activity.
        if (!peer.clock_started || peer.last_receive != event.peer->lastReceiveTime) {
            peer.last_activity = now;
            peer.clock_started = true;
            peer.last_receive = event.peer->lastReceiveTime;
        }
        TransportEvent result;
        result.connection = peer.connection;
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                peer.connected = true;
                result.type = TransportEventType::connected;
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                result.type = TransportEventType::disconnected;
                result.cause = peer.disconnecting ? DisconnectCause::requested : (silent ? DisconnectCause::timeout : DisconnectCause::remote);
                result.reason = event.data;
                peer.handle = nullptr;
                peer.connected = false;
                peer.states = {};
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                result.type = TransportEventType::received;
                result.channel = static_cast<Channel>(event.channelID);
                {
                    const std::unique_ptr<ENetPacket, decltype(&enet_packet_destroy)> packet(event.packet, enet_packet_destroy);
                    result.payload.assign(packet->data, packet->data + packet->dataLength);
                }
                ++stats.received_messages;
                break;
            default:
                throw std::runtime_error("Unexpected ENet event");
        }
        return result;
    }
};

Endpoint::Endpoint(const TransportConfig& config) : impl_(std::make_unique<Impl>(config)) {}

Endpoint::~Endpoint() = default;
Endpoint::Endpoint(Endpoint&&) noexcept = default;
Endpoint& Endpoint::operator=(Endpoint&&) noexcept = default;

ConnectionId Endpoint::connect(const std::string& ipv4, std::uint16_t port) {
    ENetAddress address{};
    address.port = port;
    if (port == 0 || enet_address_set_host_ip(&address, ipv4.c_str()) != 0) {
        throw std::invalid_argument("Network destination must be IPv4 with a nonzero port");
    }
    auto* peer = enet_host_connect(impl_->host, &address, channel_count, 0);
    if (!peer) {
        return ConnectionId::invalid;
    }
    return impl_->bind(peer).connection;
}

SendResult Endpoint::send(ConnectionId connection, Channel channel, std::span<const std::uint8_t> payload, std::optional<ActorId> actor) {
    auto* peer = impl_->find(connection);
    if (!peer || !peer->connected || peer->disconnecting) {
        return SendResult::invalid_peer;
    }
    if (payload.empty() || payload.size() > max_message_bytes || static_cast<std::uint8_t>(channel) >= channel_count
        || (channel == Channel::latest_state && (!actor || *actor >= max_players)) || (channel != Channel::latest_state && actor)) {
        ++impl_->stats.rejected_messages;
        return SendResult::invalid_message;
    }
    // ENet does not support unsequenced fragments. Reject instead of silently
    // promoting a fragmented state to reliable/global sequencing. Native peers
    // negotiate the default1400-byte MTU, so all capped1024-byte states fit.
    if (channel != Channel::control && payload.size() + sizeof(ENetProtocolHeader) + sizeof(ENetProtocolSendUnsequenced) > peer->handle->mtu) {
        ++impl_->stats.rejected_messages;
        return SendResult::invalid_message;
    }
    if (channel != Channel::latest_state) {
        return impl_->send_packet(*peer, channel, payload);
    }
    auto& state = peer->states[*actor];
    const bool replacing = state.has_value();
    if (replacing) {
        ++impl_->stats.replaced_states;
    }
    state = Bytes(payload.begin(), payload.end());
    impl_->update_peaks();
    return replacing ? SendResult::replaced : SendResult::queued;
}

std::vector<TransportEvent> Endpoint::pump(MonotonicMillis now) {
    auto& impl = *impl_;
    if (impl.last_pump) {
        if (now < *impl.last_pump) {
            throw std::invalid_argument("Network clock must be monotonic");
        }
        impl.stats.max_service_gap_ms = std::max(impl.stats.max_service_gap_ms, now - *impl.last_pump);
    }
    impl.last_pump = now;
    impl.flush_states();
    std::vector<TransportEvent> events;
    events.reserve(impl.config.max_events_per_pump);
    ENetEvent event{};
    int result = enet_host_service(impl.host, &event, 0);
    std::size_t handled = 0;
    // Reserve lifecycle capacity so busy receive channels cannot starve a
    // different peer's timeout. With the default limits all peer slots fit.
    const auto receive_budget = impl.config.max_events_per_pump - std::min(impl.config.max_peers, impl.config.max_events_per_pump - 1);
    while (result > 0) {
        ++handled;
        if (event.type == ENET_EVENT_TYPE_RECEIVE
            && (event.channelID >= channel_count || event.packet->dataLength == 0 || event.packet->dataLength > max_message_bytes
                || ((event.packet->flags & ENET_PACKET_FLAG_RELIABLE) != 0) != (event.channelID == static_cast<enet_uint8>(Channel::control)))) {
            enet_packet_destroy(event.packet);
            ++impl.stats.rejected_messages;
        } else {
            events.push_back(impl.receive_event(event, now));
        }
        // Include rejected packets in the bound: no malformed flood can extend
        // this loop indefinitely through enet_host_check_events().
        if (handled >= receive_budget) {
            break;
        }
        result = enet_host_check_events(impl.host, &event);
    }
    if (result < 0) {
        throw std::runtime_error("ENet endpoint service failed");
    }
    for (auto& peer : impl.peers) {
        if (!peer.handle) {
            continue;
        }
        if (!peer.clock_started || peer.last_receive != peer.handle->lastReceiveTime) {
            peer.clock_started = true;
            peer.last_activity = now;
            peer.last_receive = peer.handle->lastReceiveTime;
        }
        if (now - peer.last_activity >= impl.config.timeout_ms && events.size() < impl.config.max_events_per_pump) {
            TransportEvent timeout;
            timeout.type = TransportEventType::disconnected;
            timeout.connection = peer.connection;
            timeout.cause = DisconnectCause::timeout;
            events.push_back(std::move(timeout));
            enet_peer_reset(peer.handle);
            peer.handle = nullptr;
            peer.connected = false;
            peer.states = {};
        }
    }
    enet_host_flush(impl.host);
    impl.stats.wire_bytes_sent += impl.host->totalSentData;
    impl.stats.wire_bytes_received += impl.host->totalReceivedData;
    impl.host->totalSentData = 0;
    impl.host->totalReceivedData = 0;
    return events;
}

void Endpoint::disconnect(ConnectionId connection, std::uint32_t reason) {
    if (auto* peer = impl_->find(connection)) {
        peer->disconnecting = true;
        peer->states = {};
        enet_peer_disconnect_later(peer->handle, reason);
    }
}

std::uint16_t Endpoint::local_port() const {
    return impl_->host->address.port;
}

TransportStatistics Endpoint::statistics() const {
    auto stats = impl_->stats;
    for (const auto& peer : impl_->peers) {
        stats.reliable_bytes += peer.budget->reliable_bytes;
        stats.unreliable_bytes += peer.budget->unreliable_bytes;
        stats.pending_states += static_cast<std::size_t>(std::count_if(peer.states.begin(), peer.states.end(), [](const auto& state) {
            return state.has_value();
        }));
    }
    return stats;
}

std::optional<std::uint32_t> Endpoint::round_trip_time_ms(ConnectionId connection) const {
    const auto* peer = impl_->find(connection);
    if (!peer || !peer->connected || peer->disconnecting) {
        return {};
    }
    return peer->handle->roundTripTime;
}

void Endpoint::flush_outbound() {
    impl_->flush_states();
    enet_host_flush(impl_->host);
    impl_->stats.wire_bytes_sent += impl_->host->totalSentData;
    impl_->host->totalSentData = 0;
}
} // namespace toca::network
