#include "audio_replay.hpp"

#include "audio/engine_state.hpp"
#include "audio/skid.hpp"
#include "audio/wheel_queue.hpp"
#include "binary_io.hpp"
#include "simulation/source_snapshot.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace toca::test::fleet {
using namespace toca::simulation;

int replay_skid_dispatch(const char* input_path, const char* output_prefix) {
    std::ifstream input(input_path, std::ios::binary);
    std::array<std::uint8_t, 8> magic;
    read(input, magic);
    if (magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'S', 'K', 'I', 'D'} || read_u32(input) != 1) {
        throw std::runtime_error("Unsupported original skid dispatch stream");
    }
    const auto count = read_u32(input);
    if (count == 0) {
        throw std::runtime_error("Original skid dispatch stream has no updates");
    }
    std::ofstream states(std::string(output_prefix) + ".skid-dispatch.csv"), events(std::string(output_prefix) + ".skid-events.csv");
    if (!states || !events) {
        throw std::runtime_error("Cannot open original skid replay outputs");
    }
    states << "id,actor,mask,reset,rng\n";
    events << "id,actor,wheel,kind,sample,dispatch,restart,fade,rng,mask,reset\n";
    std::size_t dispatched = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto id = read_u32(input), seed = read_u32(input), enabled = read_u32(input), actor_count = read_u32(input);
        if (enabled > 1 || actor_count != 16) {
            throw std::runtime_error("Original skid replay requires sixteen source actors and a boolean audio gate");
        }
        SourceRandom random(seed);
        std::array<audio::SourceSkidActor, 16> actors;
        std::array<std::uint8_t, 16> raw;
        read(input, raw);
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            actors[actor].human = raw[actor] != 0xff;
        }
        read(input, raw);
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            actors[actor].identity = raw[actor];
        }
        for (auto& actor : actors) {
            actor.state.reset = std::bit_cast<std::int32_t>(read_u32(input));
        }
        read(input, raw);
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            actors[actor].state.surface = raw[actor];
        }
        read(input, raw);
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            actors[actor].state.mask = raw[actor];
        }
        for (auto& actor : actors) {
            for (auto& event : actor.events) {
                std::array<std::uint8_t, 2> bytes;
                read(input, bytes);
                event = std::bit_cast<std::int16_t>(std::uint16_t(bytes[0] | std::uint16_t(bytes[1]) << 8));
            }
        }
        for (auto& actor : actors) {
            for (auto& strength : actor.strengths) {
                strength = std::bit_cast<std::int32_t>(read_u32(input));
            }
        }
        for (auto& actor : actors) {
            read(input, actor.wheel_kinds);
        }
        const auto decisions = audio::dispatch_source_skids(actors, enabled != 0, random);
        dispatched += decisions.size();
        for (const auto& event : decisions) {
            const auto& decision = event.decision;
            events << id << ',' << event.actor << ',' << event.wheel << ',' << event.kind << ',' << decision.sample << ',' << unsigned(decision.action) << ',' << decision.restart
                   << ',' << decision.fade << ',' << event.rng << ',' << unsigned(event.state.mask) << ',' << event.state.reset << '\n';
        }
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            states << id << ',' << actor << ',' << unsigned(actors[actor].state.mask) << ',' << actors[actor].state.reset << ',' << random.state() << '\n';
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Original skid replay stream has trailing bytes");
    }
    std::cout << "Replayed " << count << " queued fleet skid dispatches and " << dispatched << " source wheel events from their sole entry states\n";
    return 0;
}

int replay_wheel_queue(const char* input_path, const char* output_prefix) {
    std::ifstream input(input_path, std::ios::binary);
    std::array<std::uint8_t, 8> magic;
    read(input, magic);
    if (magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'W', 'Q', 'U', 'E'} || read_u32(input) != 1) {
        throw std::runtime_error("Unsupported original wheel queue stream");
    }
    const auto count = read_u32(input);
    if (count == 0) {
        throw std::runtime_error("Original wheel queue stream has no updates");
    }
    const auto integer = [&] {
        return std::bit_cast<std::int32_t>(read_u32(input));
    };
    const auto short_integer = [&] {
        std::array<std::uint8_t, 2> bytes;
        read(input, bytes);
        return std::bit_cast<std::int16_t>(std::uint16_t(bytes[0] | std::uint16_t(bytes[1]) << 8));
    };
    std::array<audio::SourceWheelQueueActor, 16> retained;
    for (auto& actor : retained) {
        for (auto& event : actor.queue.events) {
            event = short_integer();
        }
    }
    for (auto& actor : retained) {
        for (auto& strength : actor.queue.strengths) {
            strength = integer();
        }
    }
    for (auto& actor : retained) {
        for (auto& flag : actor.state.current) {
            flag = integer();
        }
    }
    for (auto& actor : retained) {
        for (auto& flag : actor.state.previous) {
            flag = integer();
        }
    }
    const auto path = [&](const char* suffix) {
        return std::string(output_prefix) + suffix;
    };
    std::ofstream queues(path(".wheel-queue.csv")), dispatch_queues(path(".wheel-dispatch-queue.csv")), producers(path(".wheel-producers.csv")),
        order_output(path(".wheel-call-order.csv")), mutes(path(".wheel-mute.csv")), states(path(".skid-dispatch.csv")), events(path(".skid-events.csv"));
    if (!queues || !dispatch_queues || !producers || !order_output || !mutes || !states || !events) {
        throw std::runtime_error("Cannot open original wheel queue replay outputs");
    }
    queues << "id,callback,actor,wheel,event,strength,current,previous\n";
    dispatch_queues << "id,actor,wheel,event,strength,current,previous\n";
    producers << "id,callback,ordinal,actor,wheel,event,strength,current,previous\n";
    order_output << "id,callback,ordinal,actor\n";
    mutes << "id,callback,actor,pending\n";
    states << "id,actor,mask,reset,rng\n";
    events << "id,actor,wheel,kind,sample,dispatch,restart,fade,rng,mask,reset\n";
    std::size_t callback_total = 0, producer_total = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto id = read_u32(input), callback_count = read_u32(input);
        if (callback_count > 10000) {
            throw std::runtime_error("Original wheel queue callback count exceeds bounded replay capacity");
        }
        for (std::uint32_t callback = 0; callback < callback_count; ++callback) {
            audio::SourceWheelQueueContext context;
            context.view = read_u32(input);
            context.local_players = read_u32(input);
            const auto disabled = read_u32(input), actor_count = read_u32(input);
            if (disabled > 1 || actor_count != retained.size()) {
                throw std::runtime_error("Original wheel queue callback has invalid fleet/gate metadata");
            }
            context.disabled = disabled != 0;
            std::array<std::uint8_t, 16> raw;
            read(input, raw);
            for (std::size_t actor = 0; actor < retained.size(); ++actor) {
                retained[actor].human = raw[actor] != 0xff;
            }
            read(input, raw);
            for (std::size_t actor = 0; actor < retained.size(); ++actor) {
                retained[actor].visibility = raw[actor];
            }
            read(input, raw);
            for (std::size_t actor = 0; actor < retained.size(); ++actor) {
                retained[actor].identity = raw[actor];
            }
            for (auto& actor : retained) {
                read(input, actor.input.airborne);
            }
            for (auto& actor : retained) {
                for (auto& slip : actor.input.longitudinal_slip) {
                    slip = integer();
                }
            }
            for (auto& actor : retained) {
                for (auto& slip : actor.input.lateral_slip) {
                    slip = integer();
                }
            }
            //25e90 audio mute scheduling is an explicit separate boundary.
            //Queue events, strengths and both producer flag arrays stay retained.
            for (auto& actor : retained) {
                actor.pending_mute = integer();
            }
            const auto order = audio::prepare_source_wheel_queues(retained, context);
            callback_total += 1;
            producer_total += order.size();
            for (std::size_t ordinal = 0; ordinal < order.size(); ++ordinal) {
                const auto actor = order[ordinal];
                order_output << id << ',' << callback << ',' << ordinal << ',' << actor << '\n';
                const auto& source = retained[actor];
                for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                    producers << id << ',' << callback << ',' << ordinal << ',' << actor << ',' << wheel << ',' << source.queue.events[wheel] << ','
                              << source.queue.strengths[wheel] << ',' << source.state.current[wheel] << ',' << source.state.previous[wheel] << '\n';
                }
            }
            for (std::size_t actor = 0; actor < retained.size(); ++actor) {
                const auto& source = retained[actor];
                mutes << id << ',' << callback << ',' << actor << ',' << source.pending_mute << '\n';
                for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                    queues << id << ',' << callback << ',' << actor << ',' << wheel << ',' << source.queue.events[wheel] << ',' << source.queue.strengths[wheel] << ','
                           << source.state.current[wheel] << ',' << source.state.previous[wheel] << '\n';
                }
            }
        }
        for (std::size_t actor = 0; actor < retained.size(); ++actor) {
            const auto& source = retained[actor];
            for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                dispatch_queues << id << ',' << actor << ',' << wheel << ',' << source.queue.events[wheel] << ',' << source.queue.strengths[wheel] << ','
                                << source.state.current[wheel] << ',' << source.state.previous[wheel] << '\n';
            }
        }
        const auto seed = read_u32(input), enabled = read_u32(input);
        if (enabled > 1) {
            throw std::runtime_error("Original wheel queue dispatch has an invalid audio gate");
        }
        SourceRandom random(seed);
        std::array<audio::SourceSkidActor, 16> actors;
        std::array<std::uint8_t, 16> raw;
        read(input, raw);
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            actors[actor].human = raw[actor] != 0xff;
            actors[actor].events = retained[actor].queue.events;
            actors[actor].strengths = retained[actor].queue.strengths;
        }
        read(input, raw);
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            actors[actor].identity = raw[actor];
        }
        for (auto& actor : actors) {
            actor.state.reset = integer();
        }
        read(input, raw);
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            actors[actor].state.surface = raw[actor];
        }
        read(input, raw);
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            actors[actor].state.mask = raw[actor];
        }
        for (auto& actor : actors) {
            read(input, actor.wheel_kinds);
        }
        for (const auto& event : audio::dispatch_source_skids(actors, enabled != 0, random)) {
            const auto& decision = event.decision;
            events << id << ',' << event.actor << ',' << event.wheel << ',' << event.kind << ',' << decision.sample << ',' << unsigned(decision.action) << ',' << decision.restart
                   << ',' << decision.fade << ',' << event.rng << ',' << unsigned(event.state.mask) << ',' << event.state.reset << '\n';
        }
        for (std::size_t actor = 0; actor < actors.size(); ++actor) {
            states << id << ',' << actor << ',' << unsigned(actors[actor].state.mask) << ',' << actors[actor].state.reset << ',' << random.state() << '\n';
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Original wheel queue replay stream has trailing bytes");
    }
    std::cout << "Replayed " << callback_total << " actual view callbacks, " << producer_total << " wheel producers and " << count
              << " following skid dispatches with retained queues\n";
    return 0;
}

int replay_whine_rng(const char* input_path, const char* output_prefix) {
    std::ifstream input(input_path, std::ios::binary);
    std::array<std::uint8_t, 8> magic;
    read(input, magic);
    if (magic != std::array<std::uint8_t, 8>{'T', 'O', 'C', 'A', 'W', 'H', 'I', 'N'} || read_u32(input) != 1) {
        throw std::runtime_error("Unsupported original WHINE stream");
    }
    const auto count = read_u32(input);
    if (count == 0) {
        throw std::runtime_error("Original WHINE stream has no updates");
    }
    const auto integer = [&] {
        return std::bit_cast<std::int32_t>(read_u32(input));
    };
    audio::SourceEngineState state;
    state.previous_gear = integer();
    state.phase = read_u32(input);
    state.shift = integer();
    state.whine_throttle = integer();
    state.throttle = integer();
    state.throttle_mirror = integer();
    state.whine_volume = integer();
    const auto intervals = integer(), minimum = integer(), maximum = integer();
    if (intervals < 1 || intervals > 1024 || maximum <= minimum) {
        throw std::runtime_error("Original WHINE stream has invalid initialized curve metadata");
    }
    std::vector<std::uint16_t> rates(std::size_t(intervals) + 1);
    std::vector<std::uint8_t> gains(rates.size());
    for (auto& rate : rates) {
        std::array<std::uint8_t, 2> bytes;
        read(input, bytes);
        rate = std::uint16_t(bytes[0] | std::uint16_t(bytes[1]) << 8);
    }
    read(input, gains);
    std::array<std::int32_t, 24> offsets;
    for (auto& offset : offsets) {
        offset = integer();
    }
    const audio::SourceWhineCurve curve{minimum, maximum, rates, gains, offsets};
    const auto path = [&](const char* suffix) {
        return std::string(output_prefix) + suffix;
    };
    std::ofstream values_output(path(".whine-inputs.csv")), state_output(path(".whine-state.csv")), offset_output(path(".whine-offset.csv")),
        commands_output(path(".whine-commands.csv")), updates_output(path(".whine-updates.csv"));
    if (!values_output || !state_output || !offset_output || !commands_output || !updates_output) {
        throw std::runtime_error("Cannot open original WHINE replay outputs");
    }
    values_output << "id,callback,player,stage,rpm,speed,gear,throttle,flag0,flag1\n";
    state_output << "id,callback,player,stage,previous_gear,phase,shift_ticks,ramp,throttle,throttle_mirror,last_volume,rng\n";
    offset_output << "id,callback,player,offset\n";
    commands_output << "id,callback,player,ordinal,kind,value\n";
    updates_output << "id,previous_gear,phase,shift_ticks,ramp,throttle,throttle_mirror,last_volume,rng\n";
    const auto state_columns = [&](std::ostream& output, const SourceRandom& random) {
        output << state.previous_gear << ',' << state.phase << ',' << state.shift << ',' << state.whine_throttle << ',' << state.throttle << ',' << state.throttle_mirror << ','
               << state.whine_volume << ',' << random.state() << '\n';
    };
    std::size_t callback_total = 0, engine_total = 0, whine_total = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto id = read_u32(input), seed = read_u32(input), callback_count = read_u32(input);
        SourceRandom random(seed); //Only the preceding update-return seed enters this frame interval.
        if (callback_count > 10000) {
            throw std::runtime_error("Original WHINE callback count exceeds bounded replay capacity");
        }
        for (std::uint32_t callback = 0; callback < callback_count; ++callback) {
            const auto enabled = read_u32(input), players = read_u32(input);
            const auto session_state = integer();
            if (enabled > 1 || players < 1 || players > 4) {
                throw std::runtime_error("Original WHINE callback has invalid gate/player metadata");
            }
            std::array<std::uint8_t, 4> actors;
            read(input, actors);
            for (std::uint32_t player = 0; player < players; ++player) {
                const auto engine_speed = integer(), rpm_scale = integer(), throttle = integer(), gear = integer(), speed_q16 = integer();
                audio::SourceEngineCall call;
                call.values = audio::source_engine_caller(engine_speed, rpm_scale, throttle, gear, speed_q16, session_state);
                call.mix = integer();
                const auto playing = read_u32(input);
                if (playing > 1) {
                    throw std::runtime_error("Original WHINE callback has an unstable voice gate");
                }
                call.playing = playing != 0;
                call.enabled = enabled != 0;
                call.local_players = players;
                const auto pair = std::bit_cast<std::int8_t>(actors[0]) >> 1;
                call.bonus = pair == 8 || pair == 9;
                const auto write_stage = [&](std::uint32_t stage) {
                    const auto& values = call.values;
                    values_output << id << ',' << callback << ',' << player << ',' << stage << ',' << values.rpm << ',' << values.speed << ',' << values.gear << ','
                                  << values.throttle << ',' << values.flag0 << ',' << values.flag1 << '\n';
                    state_output << id << ',' << callback << ',' << player << ',' << stage << ',';
                    state_columns(state_output, random);
                };
                write_stage(0);
                if (audio::prepare_source_engine_update(state, call)) {
                    write_stage(1);
                    const auto decision = audio::advance_source_whine(state, call, curve, random);
                    write_stage(2);
                    ++whine_total;
                    if (!call.bonus) {
                        offset_output << id << ',' << callback << ',' << player << ',' << decision.offset << '\n';
                    }
                    std::uint32_t ordinal = 0;
                    if (decision.rate_written) {
                        commands_output << id << ',' << callback << ',' << player << ',' << ordinal++ << ",0," << decision.rate << '\n';
                    }
                    if (decision.volume_written) {
                        commands_output << id << ',' << callback << ',' << player << ',' << ordinal << ",1," << decision.volume << '\n';
                    }
                }
                write_stage(3);
                ++engine_total;
            }
            ++callback_total;
        }
        updates_output << id << ',';
        state_columns(updates_output, random);
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Original WHINE replay stream has trailing bytes");
    }
    std::cout << "Replayed " << callback_total << " actual frame callers, " << engine_total << " engine calls and " << whine_total
              << " WHINE calls with one retained numeric state\n";
    return 0;
}

} // namespace toca::test::fleet
