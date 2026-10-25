#include "app/network.hpp"

#include "app/paths.hpp"
#include "network/content.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace toca {
NetworkRuntime::NetworkRuntime(const AssetStore& assets, const Options& options) : assets_(assets) {
    if (!options.trace.empty()) {
        const auto path = std::filesystem::path(options.trace.string() + ".network.csv");
        require_writable_outside_data(path, assets.root(), assets.bundled_root());
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path());
        }
        trace_.open(path);
        if (!trace_) {
            throw std::runtime_error("Cannot create network trace");
        }
        trace_
            << "time_ms,phase,hosting,port,generation,local_player,players,roster_revision,settings_revision,track,weather,laps,ai,roster,error,sent,received,wire_sent,wire_"
               "received,peak_reliable,max_service_gap_ms,"
               "race_generation,race_phase,actors,active_humans,local_actor,paused,finish_count,state_publications,state_link_sent,state_received,state_consumed,state_discarded,"
               "state_replaced,cadence_suppressed,controls_changed,controls_unchanged,controls_received,controls_consumed,events_consumed,latest_pending_peak,"
               "rtt_samples,last_rtt_ms,cadence_level,cadence_interval_ms,actor_packets,finish_order,load_service_calls,load_service_gap_ms,car_boost,loaded_players,pause_players,"
               "host_endpoints\n";
    }
    if (options.network_host) {
        host(*options.network_host, options.network_name);
    } else if (!options.network_join.empty()) {
        join(options.network_join, options.network_name);
    }
}

network::Hello NetworkRuntime::identity(std::string name, std::uint8_t car, bool manual, std::uint8_t sensitivity) {
    if (!content_) {
        content_ = network::simulation_content_crc32(assets_);
    }
    network::Hello result{*content_, std::move(name), car, manual};
    result.sensitivity = sensitivity;
    return result;
}

void NetworkRuntime::host(std::uint16_t port, std::string name, std::uint8_t car, bool manual, std::uint8_t sensitivity) {
    session_.host(port, identity(std::move(name), car, manual, sensitivity));
    std::cout << "Network host port=" << session_.view().port << '\n';
    std::cout.flush();
}

void NetworkRuntime::join(std::string_view address, std::string name, std::uint8_t car, bool manual, std::uint8_t sensitivity) {
    const auto endpoint = network::parse_address(address);
    session_.join(endpoint, identity(std::move(name), car, manual, sensitivity));
}

void NetworkRuntime::pump() {
    const auto now = network::monotonic_millis();
    session_.pump(now);
    if (session_.view().phase != network::SessionPhase::loading) {
        last_load_service_ = 0;
    }
    if (!trace_.is_open() || now < next_trace_) {
        return;
    }
    next_trace_ = now + 50;
    const auto& view = session_.view();
    const auto stats = session_.statistics();
    const auto& race = session_.race_statistics();
    trace_ << now << ',' << network::phase_name(view.phase) << ',' << view.hosting << ',' << view.port << ',' << view.generation << ',' << view.local_player << ','
           << view.roster.players.size() << ',' << view.roster.revision << ',' << view.settings.revision << ',' << unsigned(view.settings.track) << ','
           << unsigned(view.settings.weather) << ',' << unsigned(view.settings.laps) << ',' << view.settings.ai << ',';
    for (const auto& player : view.roster.players) {
        trace_ << player.player << ':' << player.name << ':' << unsigned(player.car) << ':' << player.manual << ':' << unsigned(player.sensitivity) << ':' << player.ready << '/';
    }
    trace_ << ',' << view.error << ',' << stats.sent_messages << ',' << stats.received_messages << ',' << stats.wire_bytes_sent << ',' << stats.wire_bytes_received << ','
           << stats.peak_reliable_bytes << ',' << stats.max_service_gap_ms << ',' << race.generation << ',';
    if (race.generation == 0) {
        trace_ << "none";
    } else {
        switch (race.phase) {
            case network::RacePhase::loading:
                trace_ << "loading";
                break;
            case network::RacePhase::grid:
                trace_ << "grid";
                break;
            case network::RacePhase::racing:
                trace_ << "racing";
                break;
            case network::RacePhase::results:
                trace_ << "results";
                break;
        }
    }
    trace_ << ',' << race.actors << ',' << race.active_humans << ',' << unsigned(race.local_actor) << ',' << race.paused << ',' << race.finish_count << ','
           << race.state_publications << ',' << race.state_link_sent << ',' << race.state_received << ',' << race.state_consumed << ',' << race.state_discarded << ','
           << race.state_replaced << ',' << race.cadence_suppressed << ',' << race.controls_changed << ',' << race.controls_unchanged << ',' << race.controls_received << ','
           << race.controls_consumed << ',' << race.events_consumed << ',' << race.peak_pending_states << ',' << race.rtt_samples << ',' << race.last_rtt_ms << ','
           << unsigned(race.max_cadence_level) << ',' << race.max_cadence_interval_ms << ',';
    for (std::size_t actor = 0; actor < race.actor_state_received.size(); ++actor) {
        trace_ << actor << ':' << race.actor_state_received[actor] << '/';
    }
    trace_ << ',';
    for (const auto& finish : race.finish_order) {
        trace_ << unsigned(finish.actor) << ':' << finish.time_centiseconds << ':' << finish.ordinal << '/';
    }
    trace_ << ',' << load_service_calls_ << ',' << max_load_service_gap_ << ',' << view.settings.car_boost << ',';
    if (const auto* race_view = session_.race_view()) {
        for (const auto& actor : race_view->actors) {
            if (actor.active_human && actor.loaded) {
                trace_ << actor.player << '/';
            }
        }
    }
    trace_ << ',';
    if (const auto* race_view = session_.race_view()) {
        for (const auto& actor : race_view->actors) {
            if (actor.active_human && actor.pause_requested) {
                trace_ << actor.player << '/';
            }
        }
    }
    trace_ << ',';
    for (const auto& endpoint : view.host_endpoints) {
        trace_ << endpoint << '/';
    }
    trace_ << '\n';
    trace_.flush();
}

void NetworkRuntime::pump_load() {
    const auto now = network::monotonic_millis();
    if (last_load_service_ != 0) {
        max_load_service_gap_ = std::max(max_load_service_gap_, now - last_load_service_);
    }
    last_load_service_ = now;
    ++load_service_calls_;
    pump();
}

void NetworkRuntime::shutdown() {
    const bool active = session_.view().phase != network::SessionPhase::idle || session_.draining();
    session_.leave();
    if (active) {
        const auto until = network::monotonic_millis() + 2000;
        do {
            pump();
            SDL_Delay(2);
        } while (session_.draining() && network::monotonic_millis() < until);
    }
    next_trace_ = 0;
    pump();
}

void NetworkRuntime::wait_until(std::uint64_t deadline_ns) {
    auto now = SDL_GetTicksNS();
    while (now < deadline_ns) {
        pump();
        SDL_DelayNS(std::min<std::uint64_t>(deadline_ns - now, 5'000'000));
        now = SDL_GetTicksNS();
    }
}

network::Session& NetworkRuntime::session() {
    return session_;
}
} // namespace toca
