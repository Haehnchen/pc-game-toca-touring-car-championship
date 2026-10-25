#include "scene/championship_finale.hpp"

#include "scene/finish_camera.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"
#include "simulation/session.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace toca::scene {
namespace {
using namespace simulation;
constexpr std::uint64_t tick_ns = LiveSchedule::interval_ns;

std::uint32_t field(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("Finale requires complete POINTS records");
    }
    return bytes[offset] | std::uint32_t(bytes[offset + 1]) << 8 | std::uint32_t(bytes[offset + 2]) << 16 | std::uint32_t(bytes[offset + 3]) << 24;
}

void put(std::span<std::uint8_t> bytes, std::size_t offset, std::int32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = std::uint8_t(std::uint32_t(value) >> (8 * i));
    }
}

ChampionshipFinaleCarPose model_pose(const Motion& actor, const std::array<std::int16_t, 4>& wheels, std::int32_t root_y) {
    const auto& state = actor.state();
    auto matrix = state.body.object_to_world;
    //449939/4499f9: CHAMPFIN root is a child of the original physical actor.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        matrix[12 + axis] += float(root_y) / 65536.f * matrix[4 + axis];
    }
    return {matrix, actor.controls().steering_angle, wheels, state.car_visual_angles};
}

std::array<std::int32_t, 3> camera_origin(const std::array<float, 16>& matrix) {
    std::array<std::int32_t, 3> result{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        float value = 0;
        for (std::size_t component = 0; component < 3; ++component) {
            value -= matrix[12 + component] * matrix[axis * 4 + component];
        }
        result[axis] = std::int32_t(value * 65536.f);
    }
    return result;
}
} // namespace

std::vector<std::uint8_t> championship_finale_route(std::span<const std::uint8_t> route) {
    const auto count = field(route, 0);
    if (count < 18 || count > 256 || count > (route.size() - 4) / 64) {
        throw std::runtime_error("Finale requires18..256 original POINTS records");
    }
    std::vector<std::uint8_t> result(route.begin(), route.end());
    //44982c starts at the first record, not the tuning loop's point1.
    for (std::uint32_t point = 0; point < count; ++point) {
        const auto base = 4 + point * 64;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto first = fixed::bits(field(route, base + axis * 4));
            const auto second = fixed::bits(field(route, base + 36 + axis * 4));
            const auto delta = fixed::sub(first, second);
            //CDQ; SUB eax,edx; SAR eax,1: signed division toward zero.
            const auto middle = fixed::add(second, delta / 2);
            put(result, base + 12 + axis * 4, middle);
            put(result, base + 24 + axis * 4, middle);
        }
    }
    return result;
}

std::array<HorizontalPlacement, 2> championship_finale_starts(const SceneResources& resources) {
    const auto start = [&](std::size_t slot) {
        return HorizontalPlacement{resources.grid[slot].first_xz, resources.grid[slot].heading, std::uint32_t(slot)};
    };
    return {start(4), start(1)}; //Source actor0 transporter, actor1 Pace Car.
}

ChampionshipFinaleSequence::ChampionshipFinaleSequence(std::span<const std::uint8_t> credits) : source_(credits.begin(), credits.end()) {
    //Bounded native validation; do not normalize away source marker pages.
    if (source_.empty() || !source_.ends_with("END\r\n")) {
        throw std::runtime_error("Finale credits require the original terminated CRLF stream");
    }
    std::size_t at = 0;
    while (at < source_.size()) {
        const auto end = source_.find("\r\n", at);
        if (end == std::string::npos || end - at > 39) {
            throw std::runtime_error("Finale credit line exceeds the original forty-byte buffer");
        }
        at = end + 2;
    }
}

std::string ChampionshipFinaleSequence::read_line() {
    const auto end = source_.find("\r\n", cursor_);
    if (end == std::string::npos) {
        throw std::runtime_error("Finale credit page has no PAGE delimiter");
    }
    auto result = source_.substr(cursor_, end - cursor_);
    cursor_ = end + 2;
    return result;
}

void ChampionshipFinaleSequence::next_page() {
    ++page_;
    lines_.clear();
    auto text = read_line();
    if (text == "END") {
        cursor_ = 0;
        return;
    }
    while (text != "PAGE") {
        //4198f0's count saturates at7. Later lines overwrite an undrawn slot.
        if (lines_.size() < 7) {
            const auto heading = lines_.empty() || text.starts_with('.');
            lines_.push_back({text, heading});
        }
        text = read_line();
    }
}

void ChampionshipFinaleSequence::advance(std::uint64_t active_delta_ns) {
    if (active_delta_ns > std::numeric_limits<std::uint64_t>::max() - active_ns_) {
        throw std::runtime_error("Finale active clock overflow");
    }
    active_ns_ += active_delta_ns;
    while (ticks_ < active_ns_ / tick_ns) {
        ++ticks_;
        const auto now = ticks_ * 40;
        const auto timer = session_timer_q16(now - cycle_start_ms_);
        if (timer > 4 * 65536 && timer < 8 * 65536 && phase_ == 0) {
            phase_ = 1;
            next_page();
        }
        if (timer > 8 * 65536 && phase_ == 1) {
            phase_ = 2;
            next_page();
        }
        if (timer > 12 * 65536) {
            cycle_start_ms_ = now;
            phase_ = 0;
            next_page();
        }
    }
}

bool ChampionshipFinaleSequence::can_exit() const {
    return session_timer_q16(ticks_ * 40 - cycle_start_ms_) >= 4 * 65536;
}

std::uint64_t ChampionshipFinaleSequence::active_time_ms() const {
    return ticks_ * 40;
}

std::uint64_t ChampionshipFinaleSequence::ticks() const {
    return ticks_;
}

std::uint32_t ChampionshipFinaleSequence::camera_phase() const {
    return phase_;
}

std::uint64_t ChampionshipFinaleSequence::credit_page() const {
    return page_;
}

const std::vector<ChampionshipFinaleCredit>& ChampionshipFinaleSequence::credits() const {
    return lines_;
}

struct ChampionshipFinale::Impl {
    const SceneResources& resources;
    HpcSurface surface;
    BarrierData barriers;
    DamageContext damage;
    FinaleTraffic traffic;
    std::vector<std::uint8_t> route;
    std::array<Session, 2> routes;
    std::vector<fixed::Vector> targets;
    std::vector<std::int32_t> distances;
    std::array<std::unique_ptr<Motion>, 2> actors;
    std::array<std::array<std::int16_t, 4>, 2> wheels{};
    ChampionshipFinaleSequence sequence;
    FinishCamera camera;
    std::array<std::uint32_t, 3> teams;
    std::int16_t orbit_heading;
    std::uint64_t pending_ns = 0;

    Impl(const SceneResources& input, const QualifyingData& data, std::span<const std::uint8_t> credits, std::array<std::uint32_t, 3> podium, SourceRandom& random,
        SurfaceRoughness& roughness, std::int16_t heading)
        : resources(input), surface(input.collision_hpc), barriers(input.route_points, input.ground.sector_centers), damage(surface, &barriers),
          route(championship_finale_route(input.route_points)), routes{Session(route, false), Session(route, false)}, sequence(credits), camera(input.cameras), teams(podium),
          orbit_heading(heading) {
        if (input.setup.selection.track_id != 1 || std::any_of(teams.begin(), teams.end(), [](auto team) {
                return team >= 8;
            })) {
            throw std::runtime_error("Championship finale requires Silverstone and original team indices0..7");
        }
        const auto count = field(route, 0);
        targets.resize(count);
        distances.resize(count);
        for (std::uint32_t point = 0; point < count; ++point) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                targets[point][axis] = fixed::bits(field(route, 4 + point * 64 + 12 + axis * 4));
            }
            targets[point][1] = 0;
            if (point != 0) {
                distances[point] = fixed::add(distances[point - 1], fixed::length(fixed::sub(targets[point], targets[point - 1])));
            }
        }
        distances.push_back(fixed::add(distances.back(), fixed::length(fixed::sub(targets.front(), targets.back()))));
        if (distances.back() <= 0) {
            throw std::runtime_error("Finale requires a positive original route length");
        }
        const auto starts = championship_finale_starts(input);
        for (std::uint32_t actor = 0; actor < 2; ++actor) {
            routes[actor].freeze_race_laps();
            //449790 replaces geometry, retaining actor0/1 Honda dynamics.
            actors[actor] = std::make_unique<Motion>(surface, initial_body_pose(surface, starts[actor], 0), 0, false, input.setup.weather, false, damage, roughness, &barriers);
            actors[actor]->set_actor_identity(actor, false);
            AiDriver driver(route, data.drivers[actor].raw, data.distributions, data.weather_speed_penalty_q16, data.tire_offset_q16, random);
            driver.set_finale(actor, traffic);
            actors[actor]->prepare_ai(std::move(driver));
            actors[actor]->handoff_to_ai();
        }
    }

    std::int32_t route_distance(std::uint32_t point, const fixed::Vector& position) const {
        //403dd0, shared with ordinary race classification's distance formula.
        const auto previous = (point + targets.size() - 1) % targets.size();
        auto delta = fixed::sub(targets[point], position);
        delta[1] = 0;
        const auto direction = fixed::normalized(fixed::sub(targets[point], targets[previous]));
        auto result = fixed::sub(point == 0 ? distances.back() : distances[point], fixed::abs(fixed::dot(delta, direction)));
        if (result < 0) {
            result = fixed::add(result, distances.back());
        }
        return std::clamp(result, 0, distances.back());
    }

    void update_routes() {
        std::array<std::int32_t, 2> positions{};
        for (std::uint32_t actor = 0; actor < 2; ++actor) {
            const auto& body = actors[actor]->state().body;
            routes[actor].update(sequence.active_time_ms(), body.position);
            const auto point = routes[actor].next_point();
            actors[actor]->set_ai_route_point(point);
            positions[actor] = route_distance(point, body.position);
        }
        traffic.truck_forward_gap_q16 = fixed::sub(positions[1], positions[0]);
        if (traffic.truck_forward_gap_q16 < 0) {
            traffic.truck_forward_gap_q16 = fixed::add(traffic.truck_forward_gap_q16, distances.back());
        }
    }

    void tick() {
        update_routes();
        //403dd0 observes ascending;404ea0 controls descend;415090 moves ascending.
        for (auto& actor : actors) {
            actor->begin_tick();
            actor->observe_rival_ai();
        }
        for (std::uint32_t actor = 0; actor < 2; ++actor) {
            actors[actor]->update({}, TimeStep{});
            if (actors[actor]->state().boundary != MotionBoundary::none) {
                throw std::runtime_error("Finale convoy left the original drivable surface");
            }
            for (std::size_t wheel = 0; wheel < 4; ++wheel) {
                const auto delta = (std::int64_t(actors[actor]->state().wheel_speed_q16[wheel]) * 0x28b) >> 16;
                wheels[actor][wheel] = std::bit_cast<std::int16_t>(std::uint16_t(std::int64_t(wheels[actor][wheel]) - delta));
            }
        }
        sequence.advance(tick_ns);
        //45eb30 rotates Camera3's separate yaw anchor even in authored phases.
        orbit_heading = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(orbit_heading) + 15));
        const auto phase = sequence.camera_phase();
        if (phase != 0) {
            camera.update(actors[phase == 1 ? 0 : 1]->state().body.position);
        }
        damage.advance();
    }
};

ChampionshipFinale::ChampionshipFinale(const SceneResources& resources, const QualifyingData& data, std::span<const std::uint8_t> credits,
    std::array<std::uint32_t, 3> podium_teams, SourceRandom& random, SurfaceRoughness& roughness, std::int16_t initial_camera_heading)
    : impl_(std::make_unique<Impl>(resources, data, credits, podium_teams, random, roughness, initial_camera_heading)) {}

ChampionshipFinale::~ChampionshipFinale() = default;

void ChampionshipFinale::advance(std::uint64_t active_delta_ns) {
    if (active_delta_ns > std::numeric_limits<std::uint64_t>::max() - impl_->pending_ns) {
        throw std::runtime_error("Finale active clock overflow");
    }
    impl_->pending_ns += active_delta_ns;
    while (impl_->pending_ns >= tick_ns) {
        impl_->tick();
        impl_->pending_ns -= tick_ns;
    }
}

ChampionshipFinaleFrame ChampionshipFinale::frame() const {
    const auto& self = *impl_;
    ChampionshipFinaleFrame result;
    result.transporter = model_pose(*self.actors[0], self.wheels[0], 0xb333);
    result.pace = model_pose(*self.actors[1], self.wheels[1], 0x1999);
    result.teams = self.teams;
    result.camera_phase = self.sequence.camera_phase();
    result.camera_cut = std::uint64_t(result.camera_phase) << 32 | (result.camera_phase == 0 ? 0 : self.camera.selected());
    result.credit_page = self.sequence.credit_page();
    result.credits = self.sequence.credits();
    if (result.camera_phase == 0) {
        result.camera_world_to_view = race_world_to_view(self.actors[0]->state().body, self.orbit_heading, 3, 0, false);
        result.camera_position_q16 = camera_origin(result.camera_world_to_view);
    } else {
        const auto& target = self.actors[result.camera_phase == 1 ? 0 : 1]->state().body.position;
        result.camera_world_to_view = self.camera.world_to_view(target, &self.surface, true);
        result.camera_position_q16 = self.camera.position(target, &self.surface, true);
    }
    return result;
}

bool ChampionshipFinale::can_exit() const {
    return impl_->sequence.can_exit();
}

std::uint64_t ChampionshipFinale::active_time_ms() const {
    return impl_->sequence.active_time_ms();
}

const MotionState& ChampionshipFinale::actor(std::uint32_t source_actor) const {
    if (source_actor > 1) {
        throw std::runtime_error("Finale source actor must be0 or1");
    }
    return impl_->actors[source_actor]->state();
}

const VehicleState& ChampionshipFinale::controls(std::uint32_t source_actor) const {
    if (source_actor > 1) {
        throw std::runtime_error("Finale source actor must be0 or1");
    }
    return impl_->actors[source_actor]->controls();
}
} // namespace toca::scene
