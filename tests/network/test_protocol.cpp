#include "network/protocol.hpp"
#include "network/race_state.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace toca::network;
        // Synthetic native vector; no game bytes are reproduced.
        const Packet hello{0, 0, 0x12345678, Hello{0x8ce56d41, "AB", 7, true}};
        const std::vector<std::uint8_t> expected{
            0x54, 0x4f, 0x43, 0x41, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x78, 0x56, 0x34, 0x12, 10, 0, 0, 0, 0x41, 0x6d, 0xe5, 0x8c, 2, 'A', 'B', 7, 1, 2};
        expect(encode(hello) == expected, "Hello must use exact LE fields and a bounded length-prefixed name");
        const std::array<Message, 9> messages{Hello{0x12345678, "ABCDEFGHI", 9, false}, Welcome{2, 1, 3, 4}, Reject{RejectReason::content},
            Roster{2, {{1, "HOST", 0, false, true}, {2, "GUEST", 9, true, false}}}, LobbySettings{4, 9, 5, 20, true}, SelectCar{8, true}, Ready{4, 2, true},
            Start{2, 4, 0xfedcba98, {1, 0}}, Leave{}};
        // Literal protocol3 IDs are independent of enum and variant ordering.
        constexpr std::array<std::uint8_t, 9> lobby_wire_ids{1, 2, 3, 4, 5, 6, 7, 8, 9};
        for (std::size_t index = 0; index < messages.size(); ++index) {
            const auto& message = messages[index];
            const Packet packet{0xfedcba98, 0xabcdef01, 0xffffffff, message};
            const auto encoded = encode(packet);
            const auto decoded = decode(encoded);
            expect(encoded[6] == lobby_wire_ids[index] && std::uint8_t(message_kind(message)) == lobby_wire_ids[index], "Lobby wire IDs must retain protocol3 identities");
            expect(!race_message(message), "Lobby alternatives must stay outside race routing regardless of their variant index");
            expect(bool(decoded) && *decoded.packet == packet, "Every lobby kind must round-trip explicit fields");
            for (std::size_t size = 0; size < encoded.size(); ++size) {
                expect(!decode(std::span(encoded).first(size)), "Every truncated packet must be rejected");
            }
            auto extended = encoded;
            extended.push_back(0);
            expect(!decode(extended), "Trailing bytes must be rejected");
        }
        for (const auto& [offset, value, error] : std::array<std::tuple<std::size_t, std::uint8_t, DecodeError>, 7>{{{0, 0, DecodeError::magic}, {4, 1, DecodeError::version},
                 {6, 0xff, DecodeError::kind}, {7, 1, DecodeError::reserved}, {20, 8, DecodeError::length}, {22, 1, DecodeError::reserved}, {32, 2, DecodeError::field}}}) {
            auto altered = expected;
            altered[offset] = value;
            expect(decode(altered).error == error, "Malformed envelope and boolean errors must remain typed");
        }
        auto long_packet = expected;
        long_packet.resize(max_message_bytes + 1);
        expect(decode(long_packet).error == DecodeError::length, "Receive must enforce transport maximum before reading fields");
        for (const auto& message : std::array<Message, 7>{Hello{0, "", 0, false}, Hello{0, "0123456789", 0, false}, Hello{0, "A\n", 0, false}, SelectCar{10, false},
                 LobbySettings{0, 0, 0, 4, false}, Roster{0, {}}, Roster{0, {{1, "A"}, {1, "B"}}}}) {
            bool rejected = false;
            try {
                (void)encode({1, 1, 0, message});
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            expect(rejected, "Invalid outbound names/cars/laps/rosters must be rejected");
        }
        Roster full{1, {}};
        for (std::uint32_t player = 1; player <= max_players; ++player) {
            full.players.push_back({player, "ABCDEFGHI", 9, true, true});
        }
        expect(bool(decode(encode({1, 1, 0, full}))), "All eight maximum-length lobby members must fit the bounded envelope");
        for (const bool enabled : {false, true}) {
            const Packet settings{1, 1, 1, LobbySettings{2, 0, 1, 3, false, enabled}};
            const auto wire = encode(settings);
            expect(wire.size() == envelope_bytes + 9 && wire.back() == enabled && decode(wire).packet == settings,
                "Shared Car Boost must round-trip as a strict trailing settings boolean");
            auto malformed = wire;
            malformed.back() = 2;
            expect(decode(malformed).error == DecodeError::field, "Car Boost rejects non-boolean wire values");
        }
        expect(decode(encode({1, 1, 1, Reject{RejectReason::not_committed}})).packet->message == Message{Reject{RejectReason::not_committed}},
            "Uncommitted players need an explicit race-started-without-you rejection");
        for (const auto reason : {RejectReason::loading_cancelled, RejectReason::load_failed, RejectReason::loading_timeout}) {
            const Message rejection = Reject{reason};
            expect(decode(encode({1, 1, 1, rejection})).packet->message == rejection, "Loading failure reason did not survive reliable wire encoding");
        }
        const SourceAxes axes{0, -65536, 65536, 65536, 0};
        const std::array<std::uint8_t, 8> packed_axes{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x2f, 0};
        expect(pack_source_axes(axes) == packed_axes && unpack_source_axes(packed_axes) == axes,
            "43b2d0 controls must retain full-scale signed right steering, throttle and brake via magnitude-1");
        auto changed_axes = axes;
        changed_axes.right_q16 = 65536;
        expect(!source_axes_changed(packed_axes, pack_source_axes(changed_axes)), "43c4a0 excludes the steering sign bit19 from its changed-word mask");
        changed_axes.source_flags = 1U << 24;
        expect(!source_axes_changed(packed_axes, pack_source_axes(changed_axes)), "43c4a0 excludes high mode bits from changed-input sending");
        changed_axes.source_flags |= 1U << 20;
        expect(source_axes_changed(packed_axes, pack_source_axes(changed_axes)), "43c4a0 includes held-start bit20 in the changed-input mask");
        for (const auto boundary : {-65536, -1, 0, 1, 65536}) {
            const SourceAxes input{boundary, 0, 0, 0, 0};
            expect(unpack_source_axes(pack_source_axes(input)) == input, "Control pack must distinguish absent0 from magnitude1 and full-scale65536");
        }
        const std::array<std::int32_t, 9> components{0, 1, -1, 65536, -65536, 65537, -65537, 32768, -32768};
        const std::array<std::uint8_t, 24> packed_orientation{0xfe, 0xa9, 2, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0xff, 0x7f, 0, 0};
        expect(pack_source_orientation(components) == packed_orientation, "43c6e0 must pack nine presence/sign bits and clamped magnitude-1 halfwords");
        expect(unpack_source_orientation(packed_orientation) == std::array<std::int32_t, 9>{0, 1, -1, 65536, -65536, 65536, -65536, 32768, -32768},
            "43b9a0 must reconstruct signed Q16 boundaries from separate presence and sign masks");
        expect(correct_source_position({0, 0, 0}, {0x40000, -7, 0x40000}) == std::array<std::int32_t, 3>{87381, -2, 87381},
            "43b7d0 smooths the inclusive40000 X/Z threshold and divides negative Y toward zero");
        expect(correct_source_position({0, 0, 0}, {0x40001, -7, 0}) == std::array<std::int32_t, 3>{0x40001, -7, 0}, "43b7d0 snaps every axis beyond the X/Z threshold");
        expect(correct_source_position({0, 0, 0}, {1, 0x70000000, -1}) == std::array<std::int32_t, 3>{0, 626349397, 0}, "43b7d0 Y distance does not choose snapping");
        toca::simulation::RemoteMotionState source_state;
        source_state.position = {-123456, 456789, -987654};
        source_state.velocity = {1, -2, 3};
        source_state.angular_velocity = {-4, 5, -6};
        source_state.basis = {{{65536, 0, -1}, {0, 65536, 1}, {1, -1, 65536}}};
        source_state.visual_angles = {-32, 31};
        source_state.steering_angle = 4095;
        source_state.engine_speed_q16 = 0xfffff;
        source_state.throttle_q16 = 65535;
        source_state.controls = axes;
        const std::array<Message, 11> race_messages{Loaded{}, Go{}, ActorInput{7, axes}, ActorState{7, source_state}, Gear{7, 7, true}, Finish{7, 12345, 8}, Pause{true},
            ActorStatus{7, false}, DamageVisual{7, {0, 1, 2, 3}}, ReturnLobby{}, RaceStatus{0xff, 0x81}};
        constexpr std::array<std::uint8_t, 11> race_wire_ids{10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
        static_assert(messages.size() + race_messages.size() == std::variant_size_v<Message>, "Every message needs a stable wire-ID/routing fixture");
        for (std::size_t index = 0; index < race_messages.size(); ++index) {
            const auto& message = race_messages[index];
            const Packet packet{33, 8, 0xffffffff, message};
            const auto bytes = encode(packet);
            expect(bytes[6] == race_wire_ids[index] && std::uint8_t(message_kind(message)) == race_wire_ids[index], "Race wire IDs must retain protocol3 identities");
            expect(race_message(message), "Race alternatives must remain routable regardless of their variant index");
            expect(bool(decode(bytes)) && *decode(bytes).packet == packet, "Race message must round-trip only typed recovered fields");
            for (std::size_t length = envelope_bytes; length < bytes.size(); ++length) {
                expect(!decode(std::span(bytes).first(length)), "Every race payload truncation must be rejected");
            }
        }
        const auto state_bytes = encode({33, 8, 1, ActorState{7, source_state}});
        expect(bool(decode(encode({33, 8, 3, Finish{7, max_finish_centiseconds, 1}}))), "Finish time must fit the native source32-bit active millisecond clock");
        auto large_finish = encode({33, 8, 3, Finish{7, max_finish_centiseconds, 1}});
        for (unsigned byte = 0; byte < 4; ++byte) {
            large_finish[envelope_bytes + 1 + byte] = std::uint8_t((max_finish_centiseconds + 1) >> (8 * byte));
        }
        expect(decode(large_finish).error == DecodeError::field, "Oversized finish time must reject before the simulation applies an accepted event");
        expect(state_bytes.size() == envelope_bytes + 77, "Actor state must retain the original76-byte packed body");
        auto absent_throttle = source_state;
        absent_throttle.throttle_q16.reset();
        expect(decode(encode({33, 8, 2, ActorState{7, absent_throttle}})).packet->message == Message(ActorState{7, absent_throttle}),
            "Absent throttle must remain distinct from a present zero field");
        std::cout << "Network lobby and race protocol vectors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
