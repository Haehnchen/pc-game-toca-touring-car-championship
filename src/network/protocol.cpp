#include "network/protocol.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <type_traits>

namespace toca::network {
namespace {
constexpr std::uint32_t wire_magic = 0x41434f54; // TOCA in wire order.

struct Invalid {};

void require(bool value) {
    if (!value) {
        throw Invalid{};
    }
}

class Writer {
public:
    std::vector<std::uint8_t> bytes;

    void u8(std::uint8_t value) {
        bytes.push_back(value);
    }

    void u16(std::uint16_t value) {
        u8(std::uint8_t(value));
        u8(std::uint8_t(value >> 8));
    }

    void u32(std::uint32_t value) {
        u16(std::uint16_t(value));
        u16(std::uint16_t(value >> 16));
    }

    void boolean(bool value) {
        u8(value ? 1 : 0);
    }

    void raw(std::span<const std::uint8_t> value) {
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    void name(const std::string& value) {
        require(!value.empty() && value.size() <= max_name_bytes);
        require(std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch >= 0x20 && ch <= 0x7e;
        }));
        u8(std::uint8_t(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
};

class Reader {
    std::span<const std::uint8_t> bytes_;
    std::size_t position_ = 0;

public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::uint8_t u8() {
        require(position_ < bytes_.size());
        return bytes_[position_++];
    }

    std::uint16_t u16() {
        const auto low = u8();
        return std::uint16_t(low | std::uint16_t(u8()) << 8);
    }

    std::uint32_t u32() {
        const auto low = u16();
        return std::uint32_t(low) | std::uint32_t(u16()) << 16;
    }

    bool boolean() {
        const auto value = u8();
        require(value <= 1);
        return value != 0;
    }

    std::string name() {
        const auto size = u8();
        require(size > 0 && size <= max_name_bytes);
        std::string result;
        for (std::size_t i = 0; i < size; ++i) {
            const auto ch = u8();
            require(ch >= 0x20 && ch <= 0x7e);
            result.push_back(char(ch));
        }
        return result;
    }

    void finish() const {
        require(position_ == bytes_.size());
    }
};

void validate_car(std::uint8_t car) {
    require(car <= 9);
}

void validate_settings(const LobbySettings& settings) {
    require(settings.track <= 9 && settings.weather <= 5);
    require(settings.laps == 1 || settings.laps == 2 || settings.laps == 3 || settings.laps == 5 || settings.laps == 10 || settings.laps == 15 || settings.laps == 20);
}

void validate_roster(const Roster& roster) {
    require(!roster.players.empty() && roster.players.size() <= max_players);
    for (std::size_t i = 0; i < roster.players.size(); ++i) {
        require(roster.players[i].player != 0);
        validate_car(roster.players[i].car);
        for (std::size_t j = 0; j < i; ++j) {
            require(roster.players[i].player != roster.players[j].player);
        }
    }
}

void validate_grid(std::span<const ActorId> grid) {
    require(!grid.empty() && grid.size() <= max_players);
    std::array<bool, max_players> seen{};
    for (const auto actor : grid) {
        require(actor < grid.size() && !seen[actor]);
        seen[actor] = true;
    }
}

void write_state(Writer& writer, const simulation::RemoteMotionState& state) {
    try {
        simulation::validate_remote_motion(state);
    } catch (const std::runtime_error&) {
        throw Invalid{};
    }
    for (const auto& vector : {state.position, state.velocity, state.angular_velocity}) {
        for (const auto value : vector) {
            writer.u32(std::bit_cast<std::uint32_t>(value));
        }
    }
    std::array<std::int32_t, 9> basis{};
    for (std::size_t row = 0; row < 3; ++row) {
        std::copy(state.basis[row].begin(), state.basis[row].end(), basis.begin() + row * 3);
    }
    auto orientation = pack_source_orientation(basis);
    const auto visual = (std::uint16_t(state.visual_angles[0]) & 0x3f) << 6 | (std::uint16_t(state.visual_angles[1]) & 0x3f);
    orientation[22] = std::uint8_t(visual);
    orientation[23] = std::uint8_t(visual >> 8);
    writer.raw(orientation);
    writer.u32(state.engine_speed_q16 | (state.throttle_q16 ? 1U << 20 : 0));
    writer.u32(state.steering_angle | (std::uint32_t(state.throttle_q16.value_or(0)) << 12));
    writer.raw(pack_source_axes(state.controls));
}

simulation::RemoteControls read_controls(Reader& reader) {
    std::array<std::uint8_t, 8> bytes{};
    for (auto& byte : bytes) {
        byte = reader.u8();
    }
    require((bytes[7] & 0xe0) == 0);
    return unpack_source_axes(bytes);
}

simulation::RemoteMotionState read_state(Reader& reader) {
    simulation::RemoteMotionState state;
    for (auto* vector : {&state.position, &state.velocity, &state.angular_velocity}) {
        for (auto& value : *vector) {
            value = std::bit_cast<std::int32_t>(reader.u32());
        }
    }
    std::array<std::uint8_t, 24> orientation{};
    for (auto& byte : orientation) {
        byte = reader.u8();
    }
    require((orientation[2] & 0xfc) == 0 && orientation[3] == 0 && (orientation[23] & 0xf0) == 0);
    const auto basis = unpack_source_orientation(orientation);
    for (std::size_t row = 0; row < 3; ++row) {
        std::copy_n(basis.begin() + row * 3, 3, state.basis[row].begin());
    }
    const auto visual = std::uint16_t(orientation[22]) | (std::uint16_t(orientation[23]) << 8);
    state.visual_angles = {std::int16_t((((visual >> 6) & 0x3f) ^ 0x20) - 0x20), std::int16_t(((visual & 0x3f) ^ 0x20) - 0x20)};
    const auto engine = reader.u32(), steering = reader.u32();
    require((engine & 0xffe00000U) == 0 && (steering & 0xf0000000U) == 0);
    state.engine_speed_q16 = engine & 0xfffff;
    state.steering_angle = std::uint16_t(steering & 0xfff);
    if ((engine & (1U << 20)) != 0) {
        state.throttle_q16 = std::uint16_t(steering >> 12);
    }
    state.controls = read_controls(reader);
    return state;
}
} // namespace

MessageKind message_kind(const Message& message) {
    return std::visit(
        []<class T>(const T&) {
            return T::wire_kind;
        },
        message);
}

std::vector<std::uint8_t> encode(const Packet& packet) {
    try {
        Writer payload;
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Hello>) {
                    validate_car(value.car);
                    require(value.sensitivity >= 1 && value.sensitivity <= 3);
                    payload.u32(value.content_crc32);
                    payload.name(value.name);
                    payload.u8(value.car);
                    payload.boolean(value.manual);
                    payload.u8(value.sensitivity);
                } else if constexpr (std::is_same_v<T, Welcome>) {
                    require(value.player != 0 && value.host != 0);
                    payload.u32(value.player);
                    payload.u32(value.host);
                    payload.u32(value.roster_revision);
                    payload.u32(value.settings_revision);
                } else if constexpr (std::is_same_v<T, Reject>) {
                    require(value.reason >= RejectReason::protocol && value.reason <= RejectReason::loading_timeout);
                    payload.u8(std::uint8_t(value.reason));
                } else if constexpr (std::is_same_v<T, Roster>) {
                    validate_roster(value);
                    payload.u32(value.revision);
                    payload.u8(std::uint8_t(value.players.size()));
                    for (const auto& player : value.players) {
                        payload.u32(player.player);
                        payload.name(player.name);
                        payload.u8(player.car);
                        payload.boolean(player.manual);
                        payload.boolean(player.ready);
                        require(player.sensitivity >= 1 && player.sensitivity <= 3);
                        payload.u8(player.sensitivity);
                    }
                } else if constexpr (std::is_same_v<T, LobbySettings>) {
                    validate_settings(value);
                    payload.u32(value.revision);
                    payload.u8(value.track);
                    payload.u8(value.weather);
                    payload.u8(value.laps);
                    payload.boolean(value.ai);
                    payload.boolean(value.car_boost);
                } else if constexpr (std::is_same_v<T, SelectCar>) {
                    validate_car(value.car);
                    payload.u8(value.car);
                    payload.boolean(value.manual);
                } else if constexpr (std::is_same_v<T, Ready>) {
                    payload.u32(value.settings_revision);
                    payload.u32(value.roster_revision);
                    payload.boolean(value.ready);
                } else if constexpr (std::is_same_v<T, Start>) {
                    payload.u32(value.roster_revision);
                    payload.u32(value.settings_revision);
                    payload.u32(value.seed);
                    validate_grid(value.grid);
                    payload.u8(std::uint8_t(value.grid.size()));
                    payload.raw(value.grid);
                } else if constexpr (std::is_same_v<T, ActorInput>) {
                    require(value.actor < max_players);
                    payload.u8(value.actor);
                    payload.raw(pack_source_axes(value.controls));
                } else if constexpr (std::is_same_v<T, ActorState>) {
                    require(value.actor < max_players);
                    payload.u8(value.actor);
                    write_state(payload, value.state);
                } else if constexpr (std::is_same_v<T, Gear>) {
                    require(value.actor < max_players && value.gear <= 7);
                    payload.u8(value.actor);
                    payload.u8(value.gear);
                    payload.boolean(value.reverse);
                } else if constexpr (std::is_same_v<T, Finish>) {
                    require(value.actor < max_players && value.time_centiseconds <= max_finish_centiseconds && value.ordinal <= max_players);
                    payload.u8(value.actor);
                    payload.u32(value.time_centiseconds);
                    payload.u32(value.ordinal);
                } else if constexpr (std::is_same_v<T, Pause>) {
                    payload.boolean(value.paused);
                } else if constexpr (std::is_same_v<T, ActorStatus>) {
                    require(value.actor < max_players);
                    payload.u8(value.actor);
                    payload.boolean(value.active_human);
                } else if constexpr (std::is_same_v<T, DamageVisual>) {
                    require(value.actor < max_players);
                    payload.u8(value.actor);
                    for (const auto variant : value.variants) {
                        require(variant <= 3);
                        payload.u8(variant);
                    }
                } else if constexpr (std::is_same_v<T, RaceStatus>) {
                    payload.u8(value.loaded_mask);
                    payload.u8(value.pause_mask);
                }
            },
            packet.message);
        require(payload.bytes.size() + envelope_bytes <= max_message_bytes);
        Writer writer;
        writer.u32(wire_magic);
        writer.u16(protocol_version);
        writer.u8(std::uint8_t(message_kind(packet.message)));
        writer.u8(0);
        writer.u32(packet.generation);
        writer.u32(packet.origin);
        writer.u32(packet.sequence);
        writer.u16(std::uint16_t(payload.bytes.size()));
        writer.u16(0);
        writer.bytes.insert(writer.bytes.end(), payload.bytes.begin(), payload.bytes.end());
        return writer.bytes;
    } catch (const Invalid&) {
        throw std::invalid_argument("Invalid network message field");
    }
}

DecodeResult decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < envelope_bytes || bytes.size() > max_message_bytes) {
        return {{}, DecodeError::length};
    }
    try {
        Reader header(bytes.first(envelope_bytes));
        if (header.u32() != wire_magic) {
            return {{}, DecodeError::magic};
        }
        if (header.u16() != protocol_version) {
            return {{}, DecodeError::version};
        }
        const auto kind = MessageKind(header.u8());
        if (kind < MessageKind::hello || kind > MessageKind::race_status) {
            return {{}, DecodeError::kind};
        }
        if (header.u8() != 0) {
            return {{}, DecodeError::reserved};
        }
        Packet packet;
        packet.generation = header.u32();
        packet.origin = header.u32();
        packet.sequence = header.u32();
        if (header.u16() != bytes.size() - envelope_bytes) {
            return {{}, DecodeError::length};
        }
        if (header.u16() != 0) {
            return {{}, DecodeError::reserved};
        }
        Reader reader(bytes.subspan(envelope_bytes));
        switch (kind) {
            case MessageKind::hello: {
                Hello value;
                value.content_crc32 = reader.u32();
                value.name = reader.name();
                value.car = reader.u8();
                value.manual = reader.boolean();
                value.sensitivity = reader.u8();
                require(value.sensitivity >= 1 && value.sensitivity <= 3);
                validate_car(value.car);
                packet.message = std::move(value);
                break;
            }
            case MessageKind::welcome: {
                Welcome value;
                value.player = reader.u32();
                value.host = reader.u32();
                value.roster_revision = reader.u32();
                value.settings_revision = reader.u32();
                require(value.player != 0 && value.host != 0);
                packet.message = value;
                break;
            }
            case MessageKind::reject: {
                const auto reason = RejectReason(reader.u8());
                require(reason >= RejectReason::protocol && reason <= RejectReason::loading_timeout);
                packet.message = Reject{reason};
                break;
            }
            case MessageKind::roster: {
                Roster value;
                value.revision = reader.u32();
                const auto count = reader.u8();
                require(count > 0 && count <= max_players);
                for (std::size_t i = 0; i < count; ++i) {
                    LobbyPlayer player;
                    player.player = reader.u32();
                    player.name = reader.name();
                    player.car = reader.u8();
                    player.manual = reader.boolean();
                    player.ready = reader.boolean();
                    player.sensitivity = reader.u8();
                    require(player.sensitivity >= 1 && player.sensitivity <= 3);
                    value.players.push_back(std::move(player));
                }
                validate_roster(value);
                packet.message = std::move(value);
                break;
            }
            case MessageKind::settings: {
                LobbySettings value;
                value.revision = reader.u32();
                value.track = reader.u8();
                value.weather = reader.u8();
                value.laps = reader.u8();
                value.ai = reader.boolean();
                value.car_boost = reader.boolean();
                validate_settings(value);
                packet.message = value;
                break;
            }
            case MessageKind::select_car: {
                SelectCar value;
                value.car = reader.u8();
                value.manual = reader.boolean();
                validate_car(value.car);
                packet.message = value;
                break;
            }
            case MessageKind::ready: {
                Ready value;
                value.settings_revision = reader.u32();
                value.roster_revision = reader.u32();
                value.ready = reader.boolean();
                packet.message = value;
                break;
            }
            case MessageKind::start: {
                Start value;
                value.roster_revision = reader.u32();
                value.settings_revision = reader.u32();
                value.seed = reader.u32();
                const auto count = reader.u8();
                require(count > 0 && count <= max_players);
                for (std::uint8_t i = 0; i < count; ++i) {
                    value.grid.push_back(reader.u8());
                }
                validate_grid(value.grid);
                packet.message = value;
                break;
            }
            case MessageKind::leave:
                packet.message = Leave{};
                break;
            case MessageKind::loaded:
                packet.message = Loaded{};
                break;
            case MessageKind::go:
                packet.message = Go{};
                break;
            case MessageKind::return_lobby:
                packet.message = ReturnLobby{};
                break;
            case MessageKind::actor_input: {
                ActorInput value;
                value.actor = reader.u8();
                require(value.actor < max_players);
                value.controls = read_controls(reader);
                packet.message = value;
                break;
            }
            case MessageKind::actor_state: {
                ActorState value;
                value.actor = reader.u8();
                require(value.actor < max_players);
                value.state = read_state(reader);
                packet.message = value;
                break;
            }
            case MessageKind::gear: {
                Gear value;
                value.actor = reader.u8();
                value.gear = reader.u8();
                value.reverse = reader.boolean();
                require(value.actor < max_players && value.gear <= 7);
                packet.message = value;
                break;
            }
            case MessageKind::finish: {
                Finish value;
                value.actor = reader.u8();
                value.time_centiseconds = reader.u32();
                value.ordinal = reader.u32();
                require(value.actor < max_players && value.time_centiseconds <= max_finish_centiseconds && value.ordinal <= max_players);
                packet.message = value;
                break;
            }
            case MessageKind::pause:
                packet.message = Pause{reader.boolean()};
                break;
            case MessageKind::actor_status: {
                ActorStatus value;
                value.actor = reader.u8();
                value.active_human = reader.boolean();
                require(value.actor < max_players);
                packet.message = value;
                break;
            }
            case MessageKind::damage_visual: {
                DamageVisual value;
                value.actor = reader.u8();
                require(value.actor < max_players);
                for (auto& variant : value.variants) {
                    variant = reader.u8();
                    require(variant <= 3);
                }
                packet.message = value;
                break;
            }
            case MessageKind::race_status:
                packet.message = RaceStatus{reader.u8(), reader.u8()};
                break;
        }
        reader.finish();
        return {std::move(packet), DecodeError::none};
    } catch (const Invalid&) {
        return {{}, DecodeError::field};
    }
}

std::array<std::uint8_t, 8> pack_source_axes(SourceAxes axes) {
    if (axes.left_q16 < -65536 || axes.left_q16 > 65536 || axes.right_q16 < -65536 || axes.right_q16 > 65536 || axes.throttle_q16 > 65536 || axes.brake_q16 > 65536
        || (axes.source_flags & ~0x1fd00000U) != 0) {
        throw std::invalid_argument("Invalid original network control axes");
    }
    std::uint32_t low = 0, high = axes.source_flags;
    const auto steering = axes.left_q16 != 0 ? axes.left_q16 : axes.right_q16;
    if (steering != 0) {
        high |= 1U << 18;
        if (axes.left_q16 == 0) {
            high |= 1U << 21;
        }
        if (steering < 0) {
            high |= 1U << 19;
        }
        low = std::uint32_t(steering < 0 ? -steering : steering) - 1;
    }
    if (axes.throttle_q16 != 0) {
        high |= 1U << 16;
        low |= (axes.throttle_q16 - 1) << 16;
    }
    if (axes.brake_q16 != 0) {
        high |= (1U << 17) | (axes.brake_q16 - 1);
    }
    std::array<std::uint8_t, 8> result{};
    for (unsigned byte = 0; byte < 4; ++byte) {
        result[byte] = std::uint8_t(low >> (8 * byte));
        result[4 + byte] = std::uint8_t(high >> (8 * byte));
    }
    return result;
}

SourceAxes unpack_source_axes(std::span<const std::uint8_t, 8> bytes) {
    Reader reader(bytes);
    const auto low = reader.u32(), high = reader.u32();
    SourceAxes result;
    if ((high & (1U << 18)) != 0) {
        auto steering = std::int32_t((low & 0xffff) + 1);
        if ((high & (1U << 19)) != 0) {
            steering = -steering;
        }
        ((high & (1U << 21)) != 0 ? result.right_q16 : result.left_q16) = steering;
    }
    result.throttle_q16 = (high & (1U << 16)) != 0 ? (low >> 16) + 1 : 0;
    result.brake_q16 = (high & (1U << 17)) != 0 ? (high & 0xffff) + 1 : 0;
    result.source_flags = high & 0x1fd00000U;
    return result;
}

bool source_axes_changed(std::span<const std::uint8_t, 8> before, std::span<const std::uint8_t, 8> after) {
    Reader first(before), second(after);
    const auto first_low = first.u32(), second_low = second.u32();
    return first_low != second_low || ((first.u32() ^ second.u32()) & 0x00f7ffffU) != 0;
}

std::array<std::uint8_t, 24> pack_source_orientation(std::array<std::int32_t, 9> components) {
    std::uint32_t mask = 0;
    for (std::size_t i = 0; i < components.size(); ++i) {
        components[i] = std::clamp(components[i], -65536, 65536);
        if (components[i] != 0) {
            mask |= 1U << i;
            if (components[i] < 0) {
                mask |= 1U << (i + 9);
            }
        }
    }
    std::array<std::uint8_t, 24> result{};
    for (unsigned byte = 0; byte < 4; ++byte) {
        result[byte] = std::uint8_t(mask >> (8 * byte));
    }
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto component = components[index];
        const auto magnitude = component == 0 ? 0 : std::uint16_t((component < 0 ? -component : component) - 1);
        result[4 + 2 * index] = std::uint8_t(magnitude);
        result[5 + 2 * index] = std::uint8_t(magnitude >> 8);
    }
    // The final reserved halfword remains zero.
    return result;
}

std::array<std::int32_t, 9> unpack_source_orientation(std::span<const std::uint8_t, 24> bytes) {
    Reader reader(bytes);
    const auto mask = reader.u32();
    std::array<std::int32_t, 9> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto magnitude = std::int32_t(reader.u16()) + 1;
        if ((mask & (1U << i)) != 0) {
            result[i] = (mask & (1U << (i + 9))) != 0 ? -magnitude : magnitude;
        }
    }
    return result;
}

std::array<std::int32_t, 3> correct_source_position(std::array<std::int32_t, 3> old, std::array<std::int32_t, 3> received) {
    return simulation::correct_remote_position(old, received);
}
} // namespace toca::network
