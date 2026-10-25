#pragma once

#include "network/types.hpp"
#include "simulation/remote_state.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace toca::network {
inline constexpr std::uint32_t max_finish_centiseconds = 0xffffffffU / 10;
inline constexpr std::uint16_t protocol_version = 3;
inline constexpr std::size_t envelope_bytes = 24;
inline constexpr std::size_t max_name_bytes = 9;

// Protocol3 wire identities remain stable when Message alternatives move.
enum class MessageKind : std::uint8_t {
    hello = 1,
    welcome = 2,
    reject = 3,
    roster = 4,
    settings = 5,
    select_car = 6,
    ready = 7,
    start = 8,
    leave = 9,
    loaded = 10,
    go = 11,
    actor_input = 12,
    actor_state = 13,
    gear = 14,
    finish = 15,
    pause = 16,
    actor_status = 17,
    damage_visual = 18,
    return_lobby = 19,
    race_status = 20
};
enum class RejectReason : std::uint8_t {
    protocol = 1,
    content,
    full,
    started,
    invalid_message,
    host_left,
    timeout,
    not_committed,
    loading_cancelled,
    load_failed,
    loading_timeout
};

struct Hello {
    static constexpr MessageKind wire_kind = MessageKind::hello;
    std::uint32_t content_crc32 = 0;
    std::string name;
    std::uint8_t car = 0;
    bool manual = false;
    std::uint8_t sensitivity = 2;
    bool operator==(const Hello&) const = default;
};

struct Welcome {
    static constexpr MessageKind wire_kind = MessageKind::welcome;
    PlayerId player = 0, host = 0;
    std::uint32_t roster_revision = 0, settings_revision = 0;
    bool operator==(const Welcome&) const = default;
};

struct Reject {
    static constexpr MessageKind wire_kind = MessageKind::reject;
    RejectReason reason = RejectReason::invalid_message;
    bool operator==(const Reject&) const = default;
};

struct LobbyPlayer {
    PlayerId player = 0;
    std::string name;
    std::uint8_t car = 0;
    bool manual = false, ready = false;
    std::uint8_t sensitivity = 2;
    bool operator==(const LobbyPlayer&) const = default;
};

struct Roster {
    static constexpr MessageKind wire_kind = MessageKind::roster;
    std::uint32_t revision = 0;
    std::vector<LobbyPlayer> players;
    bool operator==(const Roster&) const = default;
};

struct LobbySettings {
    static constexpr MessageKind wire_kind = MessageKind::settings;
    std::uint32_t revision = 0;
    std::uint8_t track = 0, weather = 0, laps = 3;
    bool ai = false;
    bool car_boost = true;
    bool operator==(const LobbySettings&) const = default;
};

struct SelectCar {
    static constexpr MessageKind wire_kind = MessageKind::select_car;
    std::uint8_t car = 0;
    bool manual = false;
    bool operator==(const SelectCar&) const = default;
};

struct Ready {
    static constexpr MessageKind wire_kind = MessageKind::ready;
    std::uint32_t settings_revision = 0, roster_revision = 0;
    bool ready = false;
    bool operator==(const Ready&) const = default;
};

// Host chooses the shared actor grid; envelope generation identifies this race.
struct Start {
    static constexpr MessageKind wire_kind = MessageKind::start;
    std::uint32_t roster_revision = 0, settings_revision = 0, seed = 0;
    std::vector<ActorId> grid{};
    bool operator==(const Start&) const = default;
};

struct Leave {
    static constexpr MessageKind wire_kind = MessageKind::leave;
    bool operator==(const Leave&) const = default;
};

struct Loaded {
    static constexpr MessageKind wire_kind = MessageKind::loaded;
    bool operator==(const Loaded&) const = default;
};

struct Go {
    static constexpr MessageKind wire_kind = MessageKind::go;
    bool operator==(const Go&) const = default;
};

struct ActorInput {
    static constexpr MessageKind wire_kind = MessageKind::actor_input;
    ActorId actor = 0;
    simulation::RemoteControls controls;
    bool operator==(const ActorInput&) const = default;
};

struct ActorState {
    static constexpr MessageKind wire_kind = MessageKind::actor_state;
    ActorId actor = 0;
    simulation::RemoteMotionState state;
    bool operator==(const ActorState&) const = default;
};

struct Gear {
    static constexpr MessageKind wire_kind = MessageKind::gear;
    ActorId actor = 0;
    std::uint8_t gear = 0;
    bool reverse = false;
    bool operator==(const Gear&) const = default;
};

struct Finish {
    static constexpr MessageKind wire_kind = MessageKind::finish;
    ActorId actor = 0;
    std::uint32_t time_centiseconds = 0, ordinal = 0;
    bool operator==(const Finish&) const = default;
};

struct Pause {
    static constexpr MessageKind wire_kind = MessageKind::pause;
    bool paused = false;
    bool operator==(const Pause&) const = default;
};

struct ActorStatus {
    static constexpr MessageKind wire_kind = MessageKind::actor_status;
    ActorId actor = 0;
    bool active_human = false;
    bool operator==(const ActorStatus&) const = default;
};

struct DamageVisual {
    static constexpr MessageKind wire_kind = MessageKind::damage_visual;
    ActorId actor = 0;
    std::array<std::uint8_t, 17> variants{};
    bool operator==(const DamageVisual&) const = default;
};

struct ReturnLobby {
    static constexpr MessageKind wire_kind = MessageKind::return_lobby;
    bool operator==(const ReturnLobby&) const = default;
};

//Host-authored public progress for every active human, in shared actor order.
struct RaceStatus {
    static constexpr MessageKind wire_kind = MessageKind::race_status;
    std::uint8_t loaded_mask = 0, pause_mask = 0;
    bool operator==(const RaceStatus&) const = default;
};

using Message = std::variant<Hello, Welcome, Reject, Roster, LobbySettings, SelectCar, Ready, Start, Leave, Loaded, Go, ActorInput, ActorState, Gear, Finish, Pause, ActorStatus,
    DamageVisual, ReturnLobby, RaceStatus>;

struct Packet {
    Generation generation = 0;
    PlayerId origin = 0;
    std::uint32_t sequence = 0;
    Message message;
    bool operator==(const Packet&) const = default;
};
enum class DecodeError {
    none,
    length,
    magic,
    version,
    reserved,
    kind,
    field
};

struct DecodeResult {
    std::optional<Packet> packet;
    DecodeError error = DecodeError::none;

    explicit operator bool() const {
        return packet.has_value();
    }
};

MessageKind message_kind(const Message& message);
std::vector<std::uint8_t> encode(const Packet& packet);
DecodeResult decode(std::span<const std::uint8_t> bytes);

// Original 43b2d0/43b1e0 arithmetic. source_flags retains only the source
// word's bits20 and22..28; their simulation consumers remain separately typed.
using SourceAxes = simulation::RemoteControls;

std::array<std::uint8_t, 8> pack_source_axes(SourceAxes axes);
SourceAxes unpack_source_axes(std::span<const std::uint8_t, 8> bytes);
bool source_axes_changed(std::span<const std::uint8_t, 8> before, std::span<const std::uint8_t, 8> after);

// Original43c6e0/43b9a0 region: mask word plus nine magnitude-1 halfwords
// and a zero reserved halfword. Packing clamps each component to signed Q16 one.
std::array<std::uint8_t, 24> pack_source_orientation(std::array<std::int32_t, 9> components);
std::array<std::int32_t, 9> unpack_source_orientation(std::span<const std::uint8_t, 24> bytes);
std::array<std::int32_t, 3> correct_source_position(std::array<std::int32_t, 3> old, std::array<std::int32_t, 3> received);

} // namespace toca::network
