#pragma once

#include "network/transport.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <enet/enet.h>
#include <random>
#include <stdexcept>
#include <vector>

namespace toca::network::test {
struct Impairment {
    MonotonicMillis one_way_delay_ms = 0;
    unsigned loss_per_10000 = 0;
    unsigned jitter_ms = 0;
    unsigned duplication_per_10000 = 0;
    unsigned reordering_per_10000 = 0;
    std::uint32_t seed = 0x544f4341;
};

// Actual UDP datagrams are delayed/dropped before ENet parses them. One proxy
// represents one client<->host link; relay tests use two independent links.
// The fixture uses ENet's portable socket API, never adapter event callbacks.
class UdpProxy {
    struct Datagram {
        MonotonicMillis due;
        ENetAddress destination;
        Bytes bytes;
    };

    ENetSocket socket_ = ENET_SOCKET_NULL;
    ENetAddress server_{};
    ENetAddress client_{};
    ENetAddress address_{};
    Impairment config_;
    std::mt19937 random_;
    std::vector<Datagram> pending_;
    bool blackhole_ = false;
    std::size_t forced_drops_ = 0;
    std::size_t reliable_drops_left_ = 0;
    bool drop_reliable_from_server_ = false;
    MonotonicMillis extra_delay_next_client_ = 0;

    static bool has_reliable_control(const std::uint8_t* bytes, std::size_t length) {
        if (length < 2) {
            return false;
        }
        enet_uint16 peer_id;
        std::memcpy(&peer_id, bytes, sizeof(peer_id));
        const auto flags = ENET_NET_TO_HOST_16(peer_id);
        std::size_t offset = (flags & ENET_PROTOCOL_HEADER_FLAG_SENT_TIME) ? sizeof(ENetProtocolHeader) : 2;
        constexpr std::array<std::size_t, 13> command_sizes{0, sizeof(ENetProtocolAcknowledge), sizeof(ENetProtocolConnect), sizeof(ENetProtocolVerifyConnect),
            sizeof(ENetProtocolDisconnect), sizeof(ENetProtocolPing), sizeof(ENetProtocolSendReliable), sizeof(ENetProtocolSendUnreliable), sizeof(ENetProtocolSendFragment),
            sizeof(ENetProtocolSendUnsequenced), sizeof(ENetProtocolBandwidthLimit), sizeof(ENetProtocolThrottleConfigure), sizeof(ENetProtocolSendFragment)};
        while (offset + sizeof(ENetProtocolCommandHeader) <= length) {
            const auto command = static_cast<std::size_t>(bytes[offset] & ENET_PROTOCOL_COMMAND_MASK);
            if (command == 0 || command >= command_sizes.size() || offset + command_sizes[command] > length) {
                return false;
            }
            if (command == ENET_PROTOCOL_COMMAND_SEND_RELIABLE && bytes[offset + 1] == static_cast<std::uint8_t>(Channel::control)) {
                return true;
            }
            auto size = command_sizes[command];
            if (command == ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE || command == ENET_PROTOCOL_COMMAND_SEND_FRAGMENT || command == ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED
                || command == ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT) {
                enet_uint16 payload_length;
                std::memcpy(&payload_length, bytes + offset + 6, sizeof(payload_length));
                size += ENET_NET_TO_HOST_16(payload_length);
            }
            offset += size;
        }
        return false;
    }

    bool probability(unsigned rate) {
        return random_() % 10000 < rate;
    }

    void schedule(const ENetAddress& destination, Bytes bytes, MonotonicMillis due) {
        if (pending_.size() >= 2048) {
            throw std::runtime_error("UDP impairment fixture exceeded its bounded datagram queue");
        }
        pending_.push_back({due, destination, std::move(bytes)});
        peak_pending = std::max(peak_pending, pending_.size());
    }

public:
    std::uint64_t received_datagrams = 0;
    std::uint64_t forwarded_datagrams = 0;
    std::uint64_t dropped_datagrams = 0;
    std::uint64_t duplicated_datagrams = 0;
    std::uint64_t reordered_datagrams = 0;
    std::uint64_t forced_reliable_drops = 0;
    std::size_t peak_pending = 0;

    UdpProxy(std::uint16_t server_port, const Impairment& config) : config_(config), random_(config.seed) {
        if (enet_address_set_host_ip(&server_, "127.0.0.1") != 0 || enet_address_set_host_ip(&address_, "127.0.0.1") != 0) {
            throw std::runtime_error("UDP proxy IPv4 setup failed");
        }
        server_.port = server_port;
        socket_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if (socket_ == ENET_SOCKET_NULL) {
            throw std::runtime_error("UDP proxy socket creation failed");
        }
        if (enet_socket_bind(socket_, &address_) != 0 || enet_socket_get_address(socket_, &address_) != 0 || enet_socket_set_option(socket_, ENET_SOCKOPT_NONBLOCK, 1) != 0) {
            enet_socket_destroy(socket_);
            socket_ = ENET_SOCKET_NULL;
            throw std::runtime_error("UDP proxy socket configuration failed");
        }
    }

    ~UdpProxy() {
        if (socket_ != ENET_SOCKET_NULL) {
            enet_socket_destroy(socket_);
        }
    }

    UdpProxy(const UdpProxy&) = delete;
    UdpProxy& operator=(const UdpProxy&) = delete;

    std::uint16_t port() const {
        return address_.port;
    }

    void blackhole() {
        blackhole_ = true;
        pending_.clear();
    }

    void drop_next(std::size_t count = 1) {
        forced_drops_ = count;
    }

    void drop_next_reliable_control(bool from_server = false, std::size_t count = 1) {
        reliable_drops_left_ = count;
        drop_reliable_from_server_ = from_server;
    }

    void delay_next_client_datagram(MonotonicMillis extra_ms) {
        extra_delay_next_client_ = extra_ms;
    }

    void pump(MonotonicMillis now) {
        std::array<std::uint8_t, 4096> storage{};
        for (unsigned count = 0; count < 256; ++count) {
            ENetAddress source{};
            ENetBuffer buffer{};
            buffer.data = storage.data();
            buffer.dataLength = storage.size();
            const int length = enet_socket_receive(socket_, &source, &buffer, 1);
            if (length == 0) {
                break;
            }
            if (length == -2) {
                continue;
            }
            if (length < 0) {
                throw std::runtime_error("UDP proxy receive failed");
            }
            ++received_datagrams;
            const bool from_server = source.host == server_.host && source.port == server_.port;
            if (!from_server) {
                if (client_.port != 0 && (source.host != client_.host || source.port != client_.port)) {
                    throw std::runtime_error("UDP link proxy received an unexpected third endpoint");
                }
                client_ = source;
            }
            const bool reliable_drop =
                reliable_drops_left_ > 0 && from_server == drop_reliable_from_server_ && has_reliable_control(storage.data(), static_cast<std::size_t>(length));
            if (blackhole_ || forced_drops_ > 0 || reliable_drop || probability(config_.loss_per_10000)) {
                if (reliable_drop) {
                    --reliable_drops_left_;
                    ++forced_reliable_drops;
                }
                if (forced_drops_ > 0) {
                    --forced_drops_;
                }
                ++dropped_datagrams;
                continue;
            }
            const auto destination = from_server ? client_ : server_;
            if (destination.port == 0) {
                throw std::runtime_error("UDP proxy has no client destination");
            }
            const auto jitter_range = 2 * config_.jitter_ms + 1;
            const auto jitter = static_cast<std::int64_t>(random_() % jitter_range) - config_.jitter_ms;
            auto delay = static_cast<std::int64_t>(config_.one_way_delay_ms) + jitter;
            if (!from_server && extra_delay_next_client_ != 0) {
                delay += static_cast<std::int64_t>(extra_delay_next_client_);
                extra_delay_next_client_ = 0;
                ++reordered_datagrams;
            }
            if (probability(config_.reordering_per_10000)) {
                delay += 80;
                ++reordered_datagrams;
            }
            const auto due = now + static_cast<MonotonicMillis>(std::max<std::int64_t>(0, delay));
            Bytes bytes(storage.begin(), storage.begin() + length);
            if (probability(config_.duplication_per_10000)) {
                schedule(destination, bytes, due + 7);
                ++duplicated_datagrams;
            }
            schedule(destination, std::move(bytes), due);
        }
        for (auto iterator = pending_.begin(); iterator != pending_.end();) {
            if (iterator->due > now) {
                ++iterator;
                continue;
            }
            ENetBuffer buffer{};
            buffer.data = iterator->bytes.data();
            buffer.dataLength = iterator->bytes.size();
            const auto sent = enet_socket_send(socket_, &iterator->destination, &buffer, 1);
            if (sent != static_cast<int>(iterator->bytes.size())) {
                throw std::runtime_error("UDP proxy send failed");
            }
            ++forwarded_datagrams;
            iterator = pending_.erase(iterator);
        }
    }
};
} // namespace toca::network::test
