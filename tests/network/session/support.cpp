#include "support.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace toca::network::test::session {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void until(const std::function<void()>& pump, const std::function<bool()>& done, const char* message, unsigned timeout) {
    const auto end = monotonic_millis() + timeout;
    do {
        pump();
        if (done()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (monotonic_millis() < end);
    throw std::runtime_error(message);
}

RawClient::RawClient(const Session& host) : server(endpoint.connect("127.0.0.1", host.view().port)) {}

void RawClient::pump(MonotonicMillis now) {
    for (const auto& event : endpoint.pump(now)) {
        if (event.type == TransportEventType::connected) {
            connected = true;
        }
        if (event.type != TransportEventType::received) {
            continue;
        }
        const auto packet = decode(event.payload);
        require(bool(packet), "Host sent an invalid native packet");
        if (const auto* value = std::get_if<Welcome>(&packet.packet->message)) {
            welcome = *value;
            generation = packet.packet->generation;
        }
        if (const auto* value = std::get_if<Reject>(&packet.packet->message)) {
            rejected = value->reason;
        }
    }
}

void RawClient::send(Packet packet) {
    require(endpoint.send(server, Channel::control, encode(packet)) == SendResult::queued, "Raw peer failed to queue packet");
}
} // namespace toca::network::test::session
