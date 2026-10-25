#include "simulation/ai_driver.hpp"

#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace toca::simulation {
namespace {
using namespace fixed;

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        throw std::runtime_error("AI driver: truncated POINTS/AIBIN field");
    }
    return bytes[offset] | std::uint32_t(bytes[offset + 1]) << 8 | std::uint32_t(bytes[offset + 2]) << 16 | std::uint32_t(bytes[offset + 3]) << 24;
}

// 4265b0 and478b20: normalized target, signed dot, quantized asin table,
// followed by the XZ cross-product sign. Preserve the separate fixed products.
std::int16_t target_vector_angle(const scene::BodyPose& body, Vector delta) {
    const auto direction = normalized(delta);
    auto cosine = dot(body.basis[0], direction);
    if (cosine > one) {
        cosine = sub(2 * one, cosine);
    }
    if (cosine < -one) {
        cosine = sub(neg(cosine), 2 * one);
    }
    const auto index = std::uint32_t(std::min(fixed::abs(cosine), 0xffff)) >> 4;
    const auto radians = std::int32_t(std::asin(double(float(index) / 4095.0f)) * one);
    auto arc = mul(radians, 0x28b);
    if (cosine < 0) {
        arc = neg(arc);
    }
    auto angle = 1024 - arc;
    if (add(mul(direction[0], body.basis[0][2]), mul(neg(direction[2]), body.basis[0][0])) > 0) {
        angle = -angle;
    }
    return std::int16_t(angle);
}

std::int16_t target_angle(const scene::BodyPose& body, Vector target) {
    auto delta = sub(target, body.position);
    delta[1] = 0;
    return target_vector_angle(body, delta);
}

const std::array<std::int32_t, 237>& braking_table() {
    //4050e0 initializes exactly237 entries;4051a0 signed indexing can also
    //read the adjacent initialized nearest-map/notice/bound words before it.
    static const auto table = [] {
        constexpr std::array<std::int32_t, 6> anchors{0, 0xf6b85, 0x35547a, 0x75a666, 0xcbee14, 0xfa0000};
        std::array<std::int32_t, 237> values{};
        for (std::size_t index = 0; index < values.size(); ++index) {
            const auto section = index / 50;
            const auto increment = (anchors[section + 1] - anchors[section]) / 50;
            values[index] = anchors[section] + increment * std::int32_t(index % 50);
        }
        return values;
    }();
    return table;
}

} // namespace

std::vector<std::uint8_t> initialize_ai_route_heights(std::span<const std::uint8_t> authored, const HpcSurface& surface) {
    const auto count = u32(authored, 0);
    if (count < 3 || count > 256 || count > (authored.size() - 4) / 64) {
        throw std::runtime_error("AI route height setup requires3..256 complete POINTS records");
    }
    std::vector<std::uint8_t> result(authored.begin(), authored.end());
    std::uint16_t triangle = 0;
    for (std::uint32_t step = 1; step <= count; ++step) {
        const auto point = step % count;
        for (std::size_t vector = 0; vector < 4; ++vector) {
            const auto offset = 4 + point * 64 + vector * 12;
            const auto sample = surface.prop_sample_retaining_cached(bits(u32(result, offset)), bits(u32(result, offset + 8)), triangle);
            if (!sample) {
                throw std::runtime_error("AI route height setup lies outside the HPC root");
            }
            triangle = sample->triangle;
            const auto height = std::uint32_t(sample->height);
            for (unsigned byte = 0; byte < 4; ++byte) {
                result[offset + 4 + byte] = std::uint8_t(height >> (byte * 8));
            }
        }
    }
    return result;
}

AiDriver::AiDriver(std::span<const std::uint8_t> route, std::span<const std::uint8_t, 40> driver, const std::array<std::vector<std::int16_t>, 3>& distributions,
    std::int32_t weather_speed_penalty_q16, std::int32_t tire_offset_q16, SourceRandom& random, bool impossible, AiRouteEncoding encoding)
    : route_encoding_(encoding), distributions_(distributions), random_(&random), throttle_q16_(bits(u32(driver, 28))), weather_penalty_q16_(weather_speed_penalty_q16),
      tire_offset_q16_(tire_offset_q16) {
    if (encoding == AiRouteEncoding::source_initialized && impossible) {
        throw std::runtime_error("Initialized source POINTS must not receive authored difficulty tuning twice");
    }
    route_refresh_period_ = driver[19];
    distant_variation_q16_ = {bits(u32(driver, 36)), bits(u32(driver, 32))};
    const auto count = u32(route, 0);
    if (count < 3 || count > 256 || count > (route.size() - 4) / 64) {
        throw std::runtime_error("AI driver requires3..256 complete POINTS records");
    }
    for (std::size_t i = 0; i < variation_counts_.size(); ++i) {
        variation_counts_[i] = driver[21 + i];
        if (variation_counts_[i] > 127) {
            throw std::runtime_error("AI driver variation count exceeds signed source byte");
        }
    }
    if (throttle_q16_ < 0 || throttle_q16_ > 2 * one) {
        throw std::runtime_error("AI driver has invalid source throttle");
    }
    points_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto base = 4 + i * 64;
        auto& point = points_[i];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            point.first[axis] = bits(u32(route, base + axis * 4));
            point.target[axis] = bits(u32(route, base + 12 + axis * 4));
            point.alternate[axis] = bits(u32(route, base + 24 + axis * 4));
            point.second[axis] = bits(u32(route, base + 36 + axis * 4));
        }
        if (encoding == AiRouteEncoding::source_initialized) {
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const auto packed = u32(route, base + (axis == 0 ? 48 : 60)) >> 16;
                point.packed_lateral[axis] = packed > 0x8000 ? std::int32_t(packed) - 65536 : std::int32_t(packed);
            }
        } else {
            const auto lateral = normalized(sub(point.first, point.second));
            for (std::size_t axis = 0; axis < 2; ++axis) {
                const auto packed = std::uint16_t(lateral[axis == 0 ? 0 : 2] / 2);
                point.packed_lateral[axis] = packed > 0x8000 ? std::int32_t(packed) - 65536 : std::int32_t(packed);
            }
        }
        auto speed = u32(route, base + 52);
        if (speed > 1000) {
            throw std::runtime_error("AI driver route speed is outside the source speed table");
        }
        // 4019f0 tunes integer POINTS speed fields, before their Q16 use.
        // Point0 is excluded by the original loop starting at index1.
        if (impossible && i != 0) {
            speed += std::uint32_t((std::uint64_t(speed) * 0xccc) >> 16);
        }
        point.speed_q16 = bits(speed << 16);
        auto alternate_speed = u32(route, base + 56);
        if (impossible && i != 0) {
            alternate_speed += std::uint32_t((std::uint64_t(alternate_speed) * 0xccc) >> 16);
        }
        point.alternate_speed_q16 = bits(alternate_speed << 16);
    }
    if (encoding == AiRouteEncoding::authored && count >= 33) {
        auto& lines = native_start_lines_.emplace();
        lines.end_bound = count - 8;
        for (std::uint32_t point = 0; point < count; ++point) {
            if (point >= lines.start_bound && point < lines.end_bound) {
                continue;
            }
            const auto index = point >= lines.end_bound ? point + lines.start_bound - lines.end_bound : point;
            //4018f0 interpolates from the second edge with two separately
            //shifted products, using the original low byte of48/60.
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto delta = mul(sub(points_[point].first[axis], points_[point].second[axis]), 0x100);
                lines.start_primary[index][axis] = add(points_[point].second[axis], mul(delta, std::int32_t(route[4 + point * 64 + 60]) * one));
                lines.start_alternate[index][axis] = add(points_[point].second[axis], mul(delta, std::int32_t(route[4 + point * 64 + 48]) * one));
            }
        }
    }
    std::vector<Vector> targets;
    targets.reserve(points_.size());
    for (const auto& point : points_) {
        targets.push_back(point.target);
    }
    nearest_route_ = NearestRouteMap(targets);
    //Native4019f0 setup places its byte nearest-map before the braking table.
    //Pit notice starts0 (401370); the source start-line bound is25. Diagnostic
    //restore replaces this WHOLE window with observed PRE bytes, including the
    //live notice, without importing any later decision or mapped control.
    for (std::size_t index = 0; index < nearest_route_.candidates().size(); ++index) {
        braking_memory_[index] = std::uint8_t(nearest_route_.candidates()[index]);
    }
    braking_memory_[260] = 25;
    for (std::size_t index = 0; index < braking_table().size(); ++index) {
        const auto value = std::uint32_t(braking_table()[index]);
        for (unsigned byte = 0; byte < 4; ++byte) {
            braking_memory_[264 + index * 4 + byte] = std::uint8_t(value >> (byte * 8));
        }
    }
    alternate_target_ = points_[0].alternate;
    target_ = points_[0].target; // 402160 initializes +108 from route0.
}

void AiDriver::initialize_race_grid() {
    cached_point_ = previous_route_point_ = 0;
    route_ticks_ = 0;
    target_ = points_.front().target;
    alternate_target_ = points_.front().alternate;
    desired_speed_q16_ = points_.front().speed_q16;
    alternate_speed_q16_ = points_.front().alternate_speed_q16;
    speed_variation_q16_ = route_distance_q16_ = distance_q16_ = 0;
    angle_ = alternate_angle_ = route_angle_ = primary_boundary_angle_ = alternate_boundary_angle_ = 0;
    AiTrafficDecisionState traffic;
    traffic.passed_actor = traffic.blocker_actor = 0;
    traffic.corners = {2, 3};
    restore_source_traffic(traffic);
    AiMappedControlState mapped;
    mapped.gear = 0;
    restore_source_controls(mapped);
    braking_memory_[256] = 0;
}

std::int32_t AiDriver::sample(std::size_t distribution, std::uint8_t count) {
    const auto& values = distributions_[distribution];
    std::int32_t sum = 0;
    if (!values.empty()) {
        for (std::uint32_t i = 0; i < count; ++i) {
            sum = add(sum, values[random_->next() % values.size()]);
        }
    }
    return sum;
}

void AiDriver::select_target(std::uint32_t point, const AiRouteContext* source) {
    // 4024c0, full-body branch. Finished local player retains its route speed;
    // the160/170 cap is explicitly only for actors whose player byte is-1.
    cached_point_ = point;
    target_ = points_[point].target;
    alternate_target_ = points_[point].alternate;
    if (source && !source->qualifying && source->release_counter > 0 && (point >= source->end_bound || point < source->start_bound)) {
        const auto index = point >= source->end_bound ? point + source->start_bound - source->end_bound : point;
        if (index >= source->start_primary.size()) {
            throw std::runtime_error("Source start-line target index exceeds initialized table");
        }
        target_ = source->swap_start_lines ? source->start_alternate[index] : source->start_primary[index];
        alternate_target_ = source->swap_start_lines ? source->start_primary[index] : source->start_alternate[index];
    }
    desired_speed_q16_ = points_[point].speed_q16;
    alternate_speed_q16_ = points_[point].alternate_speed_q16;
    if ((source ? !source->player && (source->qualifying || source->finished) : rival_ && finished_) && desired_speed_q16_ > 160 * one) {
        desired_speed_q16_ = 160 * one;
        alternate_speed_q16_ = 170 * one;
    }
    const bool full = source ? source->full_body : !distant_;
    const auto positive = full ? sample(1, variation_counts_[0]) : 0;
    const auto negative = full ? sample(0, variation_counts_[1]) : 0;
    speed_variation_q16_ = mul(bits(std::uint32_t(sub(positive, negative)) << 16), 0x28f);
    const auto offset = mul(bits(std::uint32_t(sample(2, variation_counts_[2])) << 16), 0x28f);
    const auto lateral = points_[point].packed_lateral;
    target_[0] = add(target_[0], mul(lateral[0], offset));
    target_[2] = add(target_[2], mul(lateral[1], offset));
    // 4024c0 consumes the second target's RNG draws even when mode1 uses
    // only the first target. Preserve chronology within this actor stream.
    const auto alternate_offset = mul(bits(std::uint32_t(sample(2, variation_counts_[3])) << 16), 0x28f);
    alternate_target_[0] = add(alternate_target_[0], mul(lateral[0], alternate_offset));
    alternate_target_[2] = add(alternate_target_[2], mul(lateral[1], alternate_offset));
}

std::array<bool, 2> AiDriver::source_route_corridor(const scene::BodyPose& body, std::uint32_t next, std::uint32_t previous) const {
    const auto& current = points_.at(next);
    const auto& rear = points_.at(previous);
    return {!route_boundary_passed(body.position, current.first, rear.first), route_boundary_passed(body.position, current.second, rear.second)};
}

std::int16_t AiDriver::source_route_angle(const scene::BodyPose& body, std::uint32_t point) const {
    const auto& boundary = points_.at(point);
    const Vector forward{sub(boundary.first[2], boundary.second[2]), 0, sub(boundary.second[0], boundary.first[0])};
    return target_angle(body, add(body.position, forward));
}

void AiDriver::observe_route(const scene::BodyPose& body, std::int32_t game_speed_q16, std::uint32_t next_point) {
    if (next_point >= points_.size() || game_speed_q16 < -1000 * one || game_speed_q16 > 1000 * one) {
        throw std::runtime_error("AI driver received an invalid route point or source speed");
    }
    traffic_steering_cached_ = false; //403dd0 clears game+34 before geometry.
    //4218e0 precedes route advancement. Preserve its previous boundary normal.
    const auto cadence = advance_source_route_cadence();
    advance_source_braking_notice();
    const auto angle = cadence.refresh ? source_route_angle(body, previous_route_point_ == UINT32_MAX ? next_point : previous_route_point_) : route_angle_;
    previous_route_point_ = next_point;
    if (traffic_context_.qualifying || (traffic_context_.release_counter > 0 && native_start_lines_)) {
        auto context = native_start_lines_.value_or(AiRouteContext{});
        context.release_counter = traffic_context_.release_counter;
        context.qualifying = traffic_context_.qualifying;
        context.player = !rival_;
        context.full_body = !distant_;
        context.finished = finished_;
        context.swap_start_lines = traffic_self_.swap_start_lines;
        observe_route_core(body, game_speed_q16, next_point, angle, &context);
    } else {
        observe_route_core(body, game_speed_q16, next_point, angle, nullptr);
    }
}

void AiDriver::observe_route_core(const scene::BodyPose& body, std::int32_t game_speed_q16, std::uint32_t next_point, std::int16_t route_angle, const AiRouteContext* source) {
    if (next_point >= points_.size() || game_speed_q16 < -1000 * one || game_speed_q16 > 1000 * one) {
        throw std::runtime_error("AI driver received an invalid route point or source speed");
    }
    auto delta = sub(body.position, traffic_mode_ == 2 ? points_[next_point].alternate : points_[next_point].target);
    delta[1] = 0;
    route_distance_q16_ = length(delta);
    angle_ = target_angle(body, pit_target_.value_or(target_));
    alternate_angle_ = target_angle(body, alternate_target_);
    route_angle_ = route_angle;
    distance_q16_ = fixed::abs(mul(sine(std::int32_t(traffic_mode_ == 2 ? alternate_angle_ : angle_) - route_angle + 1024), route_distance_q16_));
    const bool one_ahead = game_speed_q16 < 70 * one || distance_q16_ > 20 * one;
    const auto lookahead = (next_point + (one_ahead ? 1u : 2u)) % std::uint32_t(points_.size());
    if (lookahead != cached_point_) {
        select_target(lookahead, source);
    }
    //In the one-ahead branch403dd0 refreshes only the other line's angle.
    if (!one_ahead || traffic_mode_ == 2) {
        angle_ = target_angle(body, pit_target_.value_or(target_));
    }
    if (!one_ahead || traffic_mode_ != 2) {
        alternate_angle_ = target_angle(body, alternate_target_);
    }
}

void AiDriver::restore_source_route(const AiRouteState& state) {
    if (route_encoding_ != AiRouteEncoding::source_initialized || state.cached_point > 255 || state.traffic_mode > 6) {
        throw std::runtime_error("Source AI route cache requires initialized POINTS and valid cache/mode");
    }
    cached_point_ = state.cached_point;
    traffic_mode_ = state.traffic_mode;
    target_ = state.target;
    alternate_target_ = state.alternate_target;
    desired_speed_q16_ = state.desired_speed_q16;
    alternate_speed_q16_ = state.alternate_speed_q16;
    speed_variation_q16_ = state.speed_variation_q16;
    route_distance_q16_ = state.distance_q16;
    distance_q16_ = state.projected_distance_q16;
    angle_ = state.angle;
    alternate_angle_ = state.alternate_angle;
    route_angle_ = state.route_angle;
    primary_boundary_angle_ = state.primary_boundary_angle;
    alternate_boundary_angle_ = state.alternate_boundary_angle;
}

AiRouteCadence AiDriver::advance_source_route_cadence(std::uint8_t ticks) const {
    if (ticks > 127 || route_refresh_period_ > 127) {
        throw std::runtime_error("AI route cadence requires nonnegative signed source bytes");
    }
    return ticks < route_refresh_period_ ? AiRouteCadence{std::uint8_t(ticks + 1), false} : AiRouteCadence{0, true};
}

void AiDriver::initialize_source_route_cadence(std::uint8_t ticks) {
    if (ticks > 127) {
        throw std::runtime_error("Source route cadence must be a nonnegative signed byte");
    }
    route_ticks_ = ticks;
}

AiRouteCadence AiDriver::advance_source_route_cadence() {
    const auto result = advance_source_route_cadence(route_ticks_);
    route_ticks_ = result.ticks;
    traffic_steering_cached_ = false;
    return result;
}

AiRouteState AiDriver::route_state() const {
    return {cached_point_, traffic_mode_, target_, alternate_target_, desired_speed_q16_, alternate_speed_q16_, speed_variation_q16_, route_distance_q16_, distance_q16_, angle_,
        alternate_angle_, route_angle_, primary_boundary_angle_, alternate_boundary_angle_};
}

AiRouteProgress AiDriver::advance_source_route_progress(const scene::BodyPose& body, const AiRouteProgressInput& input, bool refresh) const {
    if (input.cursor.next >= points_.size() || input.cursor.previous >= points_.size()) {
        throw std::runtime_error("Source route cursor exceeds initialized POINTS");
    }
    if (input.checkpoint >= 6 || input.checkpoint_marker >= 6 || (input.checkpoint_marker & 1) != 0) {
        throw std::runtime_error("Source route checkpoint/marker exceeds original six checkpoints");
    }
    const auto& next = points_.at(input.cursor.next);
    const auto& previous = points_.at(input.cursor.previous);
    const bool passed_next = route_boundary_passed(body.position, next.first, next.second);
    const bool passed_previous = !input.test_previous || route_boundary_passed(body.position, previous.first, previous.second);
    const auto step = advance_route_cursor(input.cursor, std::uint32_t(points_.size()), passed_next, passed_previous);
    AiRouteProgress result;
    result.cursor = result.target_cursor = step.cursor;
    result.crossing = step.crossing;
    result.current_gate = passed_next ? 2 : 1;
    result.previous_gate = passed_previous ? 2 : 1;
    result.backwards = input.backwards;
    result.checkpoint = input.checkpoint;
    result.checkpoint_marker = input.checkpoint_marker;
    if (step.crossing == RouteCrossing::forward) {
        if (input.backwards && input.cursor.next == 0) {
            result.backwards = false;
        } else {
            const auto checkpoint_point = ((std::uint32_t(points_.size()) * 0x2b85u) >> 16) * input.checkpoint;
            if (input.cursor.next == checkpoint_point) {
                //421390/4212d0 need race timers/lap side effects outside this slice.
                result.lap_event = input.cursor.next == 0 && !input.finished;
                result.checkpoint_side_effect = result.lap_event;
                if (input.checkpoint_marker == input.checkpoint) {
                    result.checkpoint_side_effect = true;
                    result.split_event = true;
                    result.split_sector = std::uint8_t(input.checkpoint_marker / 2);
                    result.checkpoint_marker = std::uint8_t((input.checkpoint_marker + 2) % 6);
                }
                result.checkpoint = std::uint8_t((input.checkpoint + 1) % 6);
            } else if (input.cursor.next == 0) {
                result.checkpoint = 1;
                result.checkpoint_marker = 2;
            }
        }
    } else if (step.crossing == RouteCrossing::backward && input.cursor.previous == 0) {
        result.backwards = true;
    }
    //Source distance/lookahead arrays were already computed from target_cursor.
    //Nearest recovery updates only the published cursor before target refresh.
    if (refresh && input.human) {
        const auto recovery = nearest_route_.recover(result.cursor, body.position);
        result.cursor = recovery.cursor;
        result.recovered = recovery.recovered;
    }
    return result;
}

void AiDriver::observe_source_route(const scene::BodyPose& body, std::int32_t speed, std::uint32_t next, std::int16_t route_angle, const AiRouteContext& context) {
    if (route_encoding_ != AiRouteEncoding::source_initialized || context.start_bound != 25 || points_.size() < 33 || context.end_bound != points_.size() - 8) {
        throw std::runtime_error("Source route observation requires the initialized33-point start-line table");
    }
    observe_route_core(body, speed, next, route_angle, &context);
}

void AiDriver::observe(const scene::BodyPose& body, std::int32_t game_speed_q16, std::uint32_t next_point) {
    observe_route(body, game_speed_q16, next_point);
    // The lone local consumer has no traffic ring. Rivals use the separate
    // ascending source passes below, then consume their already observed axes.
    passing_ = blocked_ = force_traffic_brake_ = traffic_brake_ = false;
    blocker_actor_ = passed_actor_ = UINT32_MAX;
    traffic_mode_ = 1;
}

namespace {
const AiTraffic* traffic_actor(std::span<const AiTraffic> fleet, std::uint32_t actor) {
    const auto found = std::find_if(fleet.begin(), fleet.end(), [actor](const auto& value) {
        return value.actor_id == actor;
    });
    return found == fleet.end() ? nullptr : &*found;
}

std::array<std::uint8_t, 2> traffic_corners(std::int16_t angle) {
    //4033d0 reads the RETAINED game+100, not the current route boundary.
    if (angle < -227) {
        if (angle > -1764) {
            return angle < -1024 ? std::array<std::uint8_t, 2>{1, 2} : std::array<std::uint8_t, 2>{0, 3};
        }
    } else if (angle < 228) {
        return {2, 3};
    } else if (angle < 1764) {
        return angle > 1024 ? std::array<std::uint8_t, 2>{3, 0} : std::array<std::uint8_t, 2>{2, 1};
    }
    return {1, 0};
}

std::int16_t corner_angle(const scene::BodyPose& body, std::size_t origin, Vector target) {
    auto ray = body;
    ray.position = body.contacts[origin];
    return target_vector_angle(ray, sub(target, ray.position));
}
} // namespace

void AiDriver::restore_source_braking_memory(std::span<const std::uint8_t, 1212> bytes) {
    std::copy(bytes.begin(), bytes.end(), braking_memory_.begin());
}

void AiDriver::advance_source_braking_notice() {
    if (braking_memory_[256] != 0) {
        --braking_memory_[256]; //403dd0 decrements4bcde0 before the braking lookup window is read.
    }
}

std::int32_t AiDriver::braking_distance(std::int32_t speed, std::int32_t target) const {
    const auto actual = std::min((speed >> 16) + 20, 236);
    const auto desired = target >> 16;
    if (actual < -66 || desired < -66 || desired > 236) {
        throw std::runtime_error("AI stopping lookup exceeds the recovered source memory window: speed=" + std::to_string(speed) + " target=" + std::to_string(target)
                                 + " actual_index=" + std::to_string(actual) + " target_index=" + std::to_string(desired));
    }
    const auto first = bits(u32(braking_memory_, std::size_t(actual + 66) * 4));
    const auto second = bits(u32(braking_memory_, std::size_t(desired + 66) * 4));
    return fixed::abs(sub(second, first));
}

void AiDriver::restore_source_traffic(const AiTrafficDecisionState& state) {
    if (state.mode > 6 || state.corners[0] >= 4 || state.corners[1] >= 4) {
        throw std::runtime_error("Source traffic state exceeds original mode/corner bounds");
    }
    traffic_mode_ = state.mode;
    passing_ = state.passing;
    blocked_ = state.blocked;
    force_traffic_brake_ = state.forced_brake;
    traffic_brake_ = state.brake;
    traffic_steering_cached_ = state.steering_cached;
    passed_actor_ = state.passed_actor;
    blocker_actor_ = state.blocker_actor;
    corridor_blocked_ = state.corridor;
    traffic_corners_ = state.corners;
    obstacle_angles_ = state.obstacle_angles;
    headlight_ticks_ = state.headlight_ticks;
    headlights_ = state.headlights;
}

AiTrafficDecisionState AiDriver::traffic_decision_state() const {
    return {traffic_mode_, passing_, blocked_, force_traffic_brake_, traffic_brake_, traffic_steering_cached_, passed_actor_, blocker_actor_, corridor_blocked_, traffic_corners_,
        obstacle_angles_, headlight_ticks_, headlights_};
}

void AiDriver::set_release_counter(std::uint32_t value) {
    if (value > 360) {
        throw std::runtime_error("AI release counter exceeds source360 initialization");
    }
    traffic_context_.release_counter = value;
}

void AiDriver::release_start() {
    //Ordinary4482d0 calls401260(0): reaction ticks are cleared without RNG.
    grid_wait_ = reaction_wait_ = false;
    mapped_controls_.reaction_ticks = 0;
}

void AiDriver::set_traffic_context(const AiTrafficContext& context) {
    traffic_context_ = context;
}

void AiDriver::initialize_headlights(bool active) {
    headlights_ = active;
}

bool AiDriver::headlights_requested() const {
    return headlights_;
}

void AiDriver::advance_headlights() {
    auto& ticks = headlight_ticks_[0];
    ++ticks;
    if (ticks == 0) {
        headlights_ = true;
        headlight_ticks_[1] = std::uint8_t(random_->next() % 15 + 5);
        headlight_ticks_[2] = std::uint8_t(random_->next() % 30 + 5);
    } else if (ticks == headlight_ticks_[1] || ticks == headlight_ticks_[2] + 2 * headlight_ticks_[1]) {
        headlights_ = false;
    } else if (ticks == headlight_ticks_[1] + headlight_ticks_[2]) {
        headlights_ = true;
    } else if (ticks == 2 * (headlight_ticks_[1] + headlight_ticks_[2])) {
        if (traffic_context_.headlight_gates) {
            headlights_ = true;
        }
        ticks = 0;
    }
}

void AiDriver::select_traffic_angle(const scene::BodyPose& body, const AiTraffic& obstacle, std::int32_t gap) {
    auto left = corner_angle(body, 1, obstacle.body.contacts[traffic_corners_[0]]);
    auto right = corner_angle(body, 0, obstacle.body.contacts[traffic_corners_[1]]);
    if (gap < 30 * one) {
        left = std::int16_t(left + 0x155);
        right = std::int16_t(right + 0x155);
    }
    obstacle_angles_ = {left, right};
    traffic_steering_cached_ = true;
}

std::int32_t AiDriver::traffic_stopping_distance(std::int32_t speed, const AiTraffic& ahead) const {
    //403dd0/4036f0 test game+10 here, not the+28 reverse-recovery flag.
    return ahead.opposing_direction ? braking_distance(0, add(ahead.speed_q16, speed)) : add(braking_distance(speed, ahead.speed_q16), 8 * one);
}

void AiDriver::decide_traffic(const scene::BodyPose& body, std::int32_t speed, const AiTraffic& self, std::span<const AiTraffic> fleet, std::int32_t route_length) {
    force_traffic_brake_ = blocked_ = false;
    const auto* ahead = traffic_actor(fleet, self.ahead_id);
    traffic_ahead_ = ahead ? std::optional<AiTraffic>(*ahead) : std::nullopt;
    const auto* behind = traffic_actor(fleet, self.behind_id);
    traffic_behind_ = behind ? std::optional<AiTraffic>(*behind) : std::nullopt;
    traffic_self_ = self;
    if (!rival_ || !ahead || pit_target_) {
        passing_ = false;
        traffic_mode_ = 1;
        return;
    }
    traffic_stopping_q16_ = traffic_stopping_distance(speed, *ahead);
    if (!traffic_context_.headlight_gates) {
        headlights_ = false;
    }
    if (ahead->route_unavailable) {
        passing_ = false;
        traffic_mode_ = 1;
        return;
    }
    const auto gap = self.forward_gap_q16;
    const bool faster = add(ahead->speed_q16, 4 * one) < speed;
    if (gap < 200 * one) {
        if (ahead->speed_q16 < 50 * one && speed > 50 * one) {
            advance_headlights();
            blocked_ = true;
            blocker_actor_ = ahead->actor_id;
        } else {
            headlights_ = traffic_context_.headlight_gates;
        }
        if (ahead->lap < self.lap) {
            headlights_ = true;
        }
    }
    const auto corridor = [&] {
        traffic_corners_ = traffic_corners(ahead->route_angle);
        if (!self.refresh && traffic_stopping_q16_ <= gap && gap > 10 * one) {
            return;
        }
        const auto& boundary = points_.at(ahead->next_point);
        auto primary = boundary.target, alternate = boundary.alternate;
        if (traffic_context_.release_counter > 0) {
            const auto* lines = traffic_context_.start_lines ? traffic_context_.start_lines : native_start_lines_ ? &*native_start_lines_ : nullptr;
            if (!lines) {
                throw std::runtime_error("Traffic during source release requires initialized start-line targets");
            }
            const auto point = ahead->next_point;
            if (point >= lines->end_bound || point < lines->start_bound) {
                const auto index = point >= lines->end_bound ? point + lines->start_bound - lines->end_bound : point;
                if (index >= 33) {
                    throw std::runtime_error("Traffic start-line target exceeds its original allocation");
                }
                primary = self.swap_start_lines ? lines->start_alternate[index] : lines->start_primary[index];
                alternate = self.swap_start_lines ? lines->start_primary[index] : lines->start_alternate[index];
            }
        }
        const auto side = [](Vector first, Vector target, Vector point) {
            return add(mul(neg(sub(first[2], point[2])), sub(first[0], target[0])), mul(sub(first[0], point[0]), sub(first[2], target[2]))) >= 0;
        };
        corridor_blocked_[0] = !side(body.contacts[0], primary, ahead->body.contacts[traffic_corners_[1]]);
        corridor_blocked_[1] = !side(body.contacts[0], alternate, ahead->body.contacts[traffic_corners_[1]]);
        corridor_blocked_[2] = side(body.contacts[1], primary, ahead->body.contacts[traffic_corners_[0]]);
        corridor_blocked_[3] = side(body.contacts[1], alternate, ahead->body.contacts[traffic_corners_[0]]);
    };
    const auto select_mode = [&] {
        if (traffic_mode_ == 4 || traffic_mode_ == 5 || traffic_context_.release_counter > 0) {
            return;
        }
        const auto left = obstacle_angles_[0], right = obstacle_angles_[1];
        traffic_mode_ = left > 0 && right > 0 ? 4 : left < 0 && right < 0 ? 5 : (std::int32_t(left) + right > 0 ? 5 : 4);
    };
    if (passing_) {
        const auto* ahead_ahead = traffic_actor(fleet, ahead->ahead_id);
        if ((ahead_ahead && ahead_ahead->human) || ((finished_ || traffic_context_.qualifying) && speed > 60 * one)) {
            passing_ = false;
            traffic_mode_ = 1;
            return;
        }
        if (!ahead->blocked) {
            corridor();
            const auto* passed = traffic_actor(fleet, passed_actor_);
            if (!passed || passed_actor_ == self.behind_id || passed->blocked || passed_actor_ != ahead->actor_id || !faster
                || (traffic_mode_ == 2 && !corridor_blocked_[1] && !corridor_blocked_[3]) || (traffic_mode_ == 1 && !corridor_blocked_[0] && !corridor_blocked_[2])) {
                passing_ = false;
                traffic_mode_ = 1;
            }
            return;
        }
        passing_ = false;
    }
    if (traffic_stopping_q16_ <= gap && (gap >= 30 * one || traffic_context_.release_counter != 0)) {
        return;
    }
    if (!ahead->blocked) {
        if (faster) {
            corridor();
            if (!corridor_blocked_[0] && !corridor_blocked_[2]) {
                if (corridor_blocked_[1] || corridor_blocked_[3]) {
                    passing_ = true;
                    passed_actor_ = ahead->actor_id;
                    traffic_mode_ = 2;
                    return;
                }
                if (ahead->speed_q16 >= 70 * one) {
                    force_traffic_brake_ = true;
                    return;
                }
                select_traffic_angle(body, *ahead, gap);
                passing_ = true;
                passed_actor_ = ahead->actor_id;
                select_mode();
                return;
            }
            passing_ = true;
            passed_actor_ = ahead->actor_id;
        }
        traffic_mode_ = 1;
        return;
    }
    blocked_ = true;
    blocker_actor_ = ahead->blocker_id;
    if (gap <= 6 * one) {
        return;
    }
    const auto* blocker = traffic_actor(fleet, blocker_actor_);
    if (!blocker) {
        throw std::runtime_error("Traffic blocker chain requires an allocated actor: self=" + std::to_string(self.actor_id) + ", ahead=" + std::to_string(ahead->actor_id)
                                 + ", blocker=" + std::to_string(blocker_actor_));
    }
    auto blocker_gap = sub(blocker->route_distance_q16, self.route_distance_q16);
    if (blocker_gap < 0) {
        blocker_gap = add(blocker_gap, route_length);
    }
    const auto stop = traffic_stopping_distance(speed, *blocker);
    if (stop <= blocker_gap && blocker_gap >= 20 * one) {
        return;
    }
    traffic_corners_ = traffic_corners(blocker->route_angle);
    select_traffic_angle(body, *blocker, gap);
    select_mode();
}

void AiDriver::decide_traffic_brake(std::int32_t speed, const AiTraffic& self, std::span<const AiTraffic> fleet) {
    traffic_brake_ = false;
    const auto* ahead = traffic_actor(fleet, self.ahead_id);
    if (!rival_ || !ahead || pit_target_) {
        return;
    }
    const auto gap = self.forward_gap_q16;
    const auto ahead_speed = ahead->speed_q16;
    const auto margin = std::max(4 * one, mul(ahead_speed, 0xccc));
    const auto* ahead_ahead = traffic_actor(fleet, ahead->ahead_id);
    if (ahead_ahead && ahead_ahead->player && ahead->forward_gap_q16 < 30 * one && !ahead->route_unavailable) {
        passing_ = false;
        if (gap < 17 * one && speed > 50 * one) {
            traffic_brake_ = true;
            return;
        }
    }
    if (traffic_context_.release_counter == 0) {
        if (ahead->route_unavailable) {
            return;
        }
        if (self.obstacle && speed > 80 * one) {
            traffic_brake_ = true;
            return;
        }
        if ((finished_ || traffic_context_.qualifying) && !self.player && gap < 15 * one && !passing_ && speed > 20 * one) {
            traffic_brake_ = true;
            return;
        }
        if (speed > 80 * one && ahead_speed > 80 * one) {
            if (!passing_ && gap < 0x68000) {
                if (ahead->blocked) {
                    traffic_brake_ = true;
                    return;
                }
                if (ahead->mode == 1 && traffic_mode_ != 4 && traffic_mode_ != 5) {
                    traffic_mode_ = 2;
                }
                if (speed > add(ahead_speed, margin)) {
                    traffic_brake_ = true;
                    return;
                }
            }
            if (ahead->blocked && gap < 15 * one) {
                traffic_brake_ = true;
                return;
            }
        }
    }
    if (speed <= add(ahead_speed, margin)) {
        return;
    }
    if (force_traffic_brake_) {
        traffic_brake_ = true;
        return;
    }
    if (gap >= traffic_stopping_q16_ && gap >= 5 * one) {
        return;
    }
    switch (traffic_mode_) {
        case 0:
        case 3:
            traffic_brake_ = speed >= 40 * one;
            break;
        case 1:
        case 2:
            traffic_brake_ = !passing_;
            break;
        case 4:
        case 5:
            traffic_brake_ = speed >= 30 * one;
            break;
        case 6:
            traffic_brake_ = speed >= ahead_speed;
            break;
    }
}

AiTraffic AiDriver::traffic_state(const scene::BodyPose& body, std::int32_t speed, std::uint32_t next_point) const {
    AiTraffic result{body, speed, next_point};
    result.reversing = reversing_;
    result.blocked = blocked_;
    result.passing = passing_;
    result.blocker_id = blocker_actor_;
    result.mode = traffic_mode_;
    result.route_angle = route_angle_;
    return result;
}

void AiDriver::set_pit_target(std::optional<std::array<std::int32_t, 3>> target) {
    pit_target_ = target;
}

void AiDriver::set_start_wait(bool grid, bool reaction) {
    grid_wait_ = grid;
    reaction_wait_ = reaction;
}

std::uint8_t AiDriver::traffic_mode() const {
    return traffic_mode_;
}

void AiDriver::set_rival(bool rival, bool distant) {
    rival_ = rival;
    distant_ = distant;
}

void AiDriver::handoff(std::int8_t gear, bool finished) {
    //402be0 sets game+28 only for reverse; a second handoff does not
    //clear an existing reverse-recovery flag merely because gear changed.
    if (gear == 7) {
        reversing_ = true;
    }
    finished_ = finished;
}

void AiDriver::synchronize_source_finish(bool finished) {
    finished_ = finished;
}

void AiDriver::set_finale(std::uint32_t actor, const FinaleTraffic& traffic) {
    if (actor > 1) {
        throw std::runtime_error("Finale AI requires source actor0 or1");
    }
    finale_ = &traffic;
    finale_actor_ = actor;
}

bool AiDriver::brake_lights_requested() const {
    return mapped_controls_.brake_lights;
}

void AiDriver::restore_source_controls(const AiMappedControlState& state) {
    if (state.gear < 0 || state.gear > 7) {
        throw std::runtime_error("Source AI gear exceeds original bounds");
    }
    mapped_controls_ = state;
    reversing_ = state.reversing;
}

AiMappedControlState AiDriver::mapped_control_state() const {
    auto state = mapped_controls_;
    state.reversing = reversing_;
    return state;
}

void AiDriver::synchronize_source_physical_controls(std::int8_t gear, std::int32_t reverse_pedal, const DriverAxes* axes) {
    if (gear < 0 || gear > 7) {
        throw std::runtime_error("Source AI physical gear exceeds original bounds");
    }
    mapped_controls_.gear = gear;
    mapped_controls_.reverse_pedal = reverse_pedal;
    if (axes) {
        mapped_controls_.axes = *axes;
    }
}

void AiDriver::apply_source_motion_steering_gate(std::int32_t signed_speed_q16, bool full_body) {
    if (full_body && fixed::abs(signed_speed_q16) < 0xccc) {
        mapped_controls_.axes.left_q16 = mapped_controls_.axes.right_q16 = 0; //40b360 before418cd0.
    }
}

void AiDriver::set_control_context(const AiControlContext& context) {
    control_context_ = context;
}

void AiDriver::retain_control_axes(const DriverAxes& axes) {
    mapped_controls_.axes = axes;
}

void AiDriver::advance_source_reverse_recovery(std::int32_t speed, bool barrier) {
    auto& state = mapped_controls_;
    state.axes.forced_gear = -1;
    state.axes.reset_reverse_pedal = false;
    if (state.reverse_ticks >= 3) {
        if (++state.reverse_ticks == 50) {
            state.reverse_ticks = 0;
        }
    } else if (barrier) {
        if (state.reverse_ticks == 2) {
            reversing_ = !reversing_;
            state.axes.left_q16 = state.axes.right_q16 = 0;
            state.gear = reversing_ ? 7 : 1;
            state.axes.forced_gear = state.gear;
            if (!reversing_) {
                state.reverse_pedal = 0;
                state.axes.reset_reverse_pedal = true;
            }
        }
        ++state.reverse_ticks;
    } else if (!reversing_ && speed < 10 * one && fixed::abs(angle_) > 1024) {
        reversing_ = true;
        state.axes.left_q16 = state.axes.right_q16 = 0;
        state.gear = state.axes.forced_gear = 7;
    } else if (reversing_ && fixed::abs(angle_) < 512) {
        reversing_ = false;
        state.gear = state.axes.forced_gear = 1;
        state.reverse_pedal = 0;
        state.axes.reset_reverse_pedal = true;
    }
    state.reversing = reversing_;
}

DriverAxes AiDriver::map_source_controls(const scene::BodyPose& body, std::int32_t game_speed_q16) {
    DriverAxes result;
    result.forced_gear = mapped_controls_.axes.forced_gear;
    result.reset_reverse_pedal = mapped_controls_.axes.reset_reverse_pedal;
    ++mapped_controls_.brake_ticks;
    const auto finish = [&] {
        mapped_controls_.axes = result;
        return result;
    };
    if (mapped_controls_.reaction_ticks != 0) {
        --mapped_controls_.reaction_ticks;
        result.brake_q16 = one;
        mapped_controls_.brake_lights = false;
        return finish();
    }
    if (!control_context_.started) {
        result.throttle_q16 = random_->next() <= 0x7ff ? one : 0;
        return finish();
    }
    if (control_context_.suppressed) {
        mapped_controls_.brake_lights = false;
        return finish();
    }
    //405530 returns its low-speed obstacle steering before4032c0; reverse
    //steering bypasses the mode switch altogether. Retain game+34/+f8/+fa.
    const bool fixed_obstacle_steering = !reversing_ && (traffic_mode_ == 4 || traffic_mode_ == 5) && control_context_.obstacle && game_speed_q16 < 65 * one;
    auto steering_angle = angle_;
    if (!reversing_ && !fixed_obstacle_steering) {
        if (traffic_mode_ == 0 || traffic_mode_ == 3) {
            const auto& point = points_.at(cached_point_);
            steering_angle = target_angle(body, traffic_mode_ == 0 ? point.first : point.second);
            (traffic_mode_ == 0 ? primary_boundary_angle_ : alternate_boundary_angle_) = steering_angle;
        } else if (traffic_mode_ == 2) {
            steering_angle = alternate_angle_;
        } else if (traffic_mode_ == 6 && traffic_ahead_) {
            steering_angle = std::int16_t(target_angle(body, traffic_ahead_->body.position) / 2);
        } else if (traffic_mode_ == 4 || traffic_mode_ == 5) {
            if (!traffic_steering_cached_ && traffic_ahead_) {
                select_traffic_angle(body, *traffic_ahead_, traffic_self_.forward_gap_q16);
            }
            steering_angle = obstacle_angles_[traffic_mode_ == 4 ? 0 : 1];
        }
    }
    auto desired = passing_ ? alternate_speed_q16_ : desired_speed_q16_;
    if (desired < 230 * one) {
        const auto variation = distant_ ? distant_variation_q16_[passing_ ? 1u : 0u] : speed_variation_q16_;
        desired = add(desired, mul(desired, sub(variation, weather_penalty_q16_)));
    }
    if (blocked_) {
        desired = 60 * one;
    }
    result.throttle_q16 = add(throttle_q16_, traffic_context_.release_counter > 0 ? 0x9999 : 0);
    if (finale_) {
        //4051e0 State18 precedes pits, sampled route speeds and finish coasting.
        const bool truck_wait = finale_actor_ == 0 && finale_->truck_forward_gap_q16 <= 0xeffff && finale_->truck_ahead_actor == 1;
        const bool pace_wait = finale_actor_ == 1 && finale_->truck_forward_gap_q16 >= 0x2d0001;
        if (truck_wait || pace_wait || game_speed_q16 > 0x5a0000) {
            result.throttle_q16 = 0;
            result.brake_q16 = one;
        } else if (game_speed_q16 > 0x460000) {
            result.throttle_q16 = 0xccc;
        } else {
            result.throttle_q16 = std::clamp(mul(fixed::abs(sub(0x460000, game_speed_q16)), 0xccc), 0xccc, one);
        }
    } else if (traffic_brake_) {
        result.throttle_q16 = 0;
        result.brake_q16 = one;
    } else if (pit_target_) {
        // 4051e0's pit branch precedes route speed and the finished coasting
        // branch. It does not stop for a pit service or consult PITS speeds.
        if (game_speed_q16 > 120 * one) {
            result.throttle_q16 = 0;
            result.brake_q16 = one;
        } else if (game_speed_q16 > 100 * one) {
            result.throttle_q16 = 0xccc;
        } else {
            result.throttle_q16 = std::clamp(mul(fixed::abs(sub(100 * one, game_speed_q16)), 0xccc), 0xccc, one);
        }
    } else {
        bool route_pedals = true;
        if (traffic_self_.route_unavailable && traffic_behind_) {
            const auto speed = traffic_behind_->speed_q16;
            const auto gap = fixed::abs(sub(traffic_self_.route_distance_q16, traffic_behind_->route_distance_q16));
            if (control_context_.obstacle || (!(speed / 2 < gap || speed > 30 * one || (!reversing_ && game_speed_q16 > 40 * one)) && !traffic_behind_->route_unavailable)) {
                route_pedals = false;
                result.throttle_q16 = 0;
                result.brake_q16 = one;
            } else if (reversing_) {
                route_pedals = false;
                result.throttle_q16 = throttle_q16_;
            }
        }
        if (route_pedals && game_speed_q16 > desired) {
            if (finished_ || control_context_.brake_bypass) {
                result.throttle_q16 = 0;
            } else if (braking_distance(game_speed_q16, desired) > distance_q16_) {
                result.throttle_q16 = 0;
                result.brake_q16 = one;
            }
        }
    }
    if (reversing_) {
        const auto& previous = mapped_controls_.axes;
        if (previous.left_q16 == one || previous.right_q16 == one) {
            result.left_q16 = previous.left_q16;
            result.right_q16 = previous.right_q16;
        } else {
            (angle_ > 0 && angle_ < 0x7c7 ? result.right_q16 : result.left_q16) = one;
        }
    } else if (fixed_obstacle_steering) {
        (traffic_mode_ == 4 ? result.left_q16 : result.right_q16) = 0xe666;
    } else {
        const auto threshold = distance_q16_ < 20 * one ? (game_speed_q16 < 60 * one       ? 4
                                                              : game_speed_q16 < 100 * one ? 22
                                                              : game_speed_q16 < 150 * one ? 28
                                                              : traffic_brake_             ? 22
                                                                                           : 45)
                                                        : 45;
        const auto& gains = control_context_.steering_gains;
        const auto gain = fixed::abs(steering_angle) < threshold ? gains[distant_ ? 0 : 1] : gains[distant_ ? 3 : 2];
        const auto value = mul(std::int32_t(steering_angle) * 0x1680, gain);
        const auto amount = std::min(one, fixed::abs(value));
        if (steering_angle > 0 && value > 0) {
            result.left_q16 = amount;
        } else if (steering_angle < 0 && value < 0) {
            result.right_q16 = amount;
        }
    }
    if ((result.left_q16 > 0x3333 || result.right_q16 > 0x3333) && result.brake_q16 == one && game_speed_q16 > 70 * one) {
        result.brake_q16 = 0;
    }
    if (control_context_.force_acceleration) {
        result.throttle_q16 = one;
        result.brake_q16 = 0;
    }
    if (control_context_.tyre_brake_disabled) {
        result.brake_q16 = 0;
    }
    if (mapped_controls_.brake_ticks == 16) {
        mapped_controls_.brake_lights = result.brake_q16 == one && (!traffic_context_.qualifying || control_context_.player || game_speed_q16 < 10 * one);
        mapped_controls_.brake_ticks = 0;
    }
    return finish();
}

DriverAxes AiDriver::axes(const scene::BodyPose& body, std::int32_t speed, std::int8_t gear, bool advance_recovery) {
    if (advance_recovery) {
        mapped_controls_.gear = gear;
    }
    if (reaction_wait_) {
        mapped_controls_.reaction_ticks = 1;
    }
    control_context_.started = !grid_wait_;
    if (advance_recovery) {
        advance_source_reverse_recovery(speed, control_context_.barrier_hit);
    }
    return map_source_controls(body, speed);
}

std::int32_t AiDriver::tire_offset_q16() const {
    return tire_offset_q16_;
}

std::uint32_t AiDriver::target_point() const {
    return cached_point_;
}

std::int16_t AiDriver::steering_error() const {
    return angle_;
}
} // namespace toca::simulation
