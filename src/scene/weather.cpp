#include "scene/weather.hpp"

#include "assets/store.hpp"
#include "core/random.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace toca::scene {
namespace {
using namespace simulation::fixed;

std::int32_t coordinate(double value, bool round = true) {
    value = round ? std::nearbyint(value * one) : std::trunc(value * one);
    if (!std::isfinite(value) || value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("Weather camera is outside original fixed coordinates");
    }
    return std::int32_t(value);
}

Vector center(const WeatherCamera& camera) {
    return add(camera.position, scale(camera.forward, 10 * one));
}

std::int32_t wrap(std::int32_t value) {
    // 4623b0/462d20 keep both endpoints of the twelve-unit cube inclusive.
    constexpr std::int64_t half = 6 * one, width = 12 * one;
    auto wide = std::int64_t(value);
    if (wide > half) {
        wide -= ((wide + half - 1) / width) * width;
    } else if (wide < -half) {
        wide += ((half - 1 - wide) / width) * width;
    }
    return std::int32_t(wide);
}
} // namespace

WeatherResources load_weather_resources(const AssetStore& store) {
    const auto images = store.open_archive("data/image640.bfp");
    WeatherResources result;
    for (std::size_t texture = 0; texture < result.textures.size(); ++texture) {
        const auto bytes = images.member(13 + texture).bytes();
        result.textures[texture].assign(bytes.begin(), bytes.end());
    }
    return result;
}

WeatherCamera weather_camera(const std::array<float, 16>& matrix) {
    // The native camera stores an inverse view. As with FollowCamera's
    // listener, invert its entire affine basis; lookup bases are not unit.
    const std::array<float, 9> inverse{matrix[10] * matrix[5] - matrix[9] * matrix[6], matrix[9] * matrix[2] - matrix[10] * matrix[1],
        matrix[6] * matrix[1] - matrix[5] * matrix[2], matrix[8] * matrix[6] - matrix[4] * matrix[10], matrix[0] * matrix[10] - matrix[8] * matrix[2],
        matrix[4] * matrix[2] - matrix[0] * matrix[6], matrix[4] * matrix[9] - matrix[8] * matrix[5], matrix[8] * matrix[1] - matrix[0] * matrix[9],
        matrix[0] * matrix[5] - matrix[4] * matrix[1]};
    const auto determinant = matrix[0] * inverse[0] + matrix[4] * inverse[1] + matrix[8] * inverse[2];
    if (!std::isfinite(determinant) || determinant == 0) {
        throw std::runtime_error("Weather requires an invertible camera basis");
    }
    WeatherCamera result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.position[axis] = coordinate((-double(matrix[12]) * inverse[axis] - double(matrix[13]) * inverse[3 + axis] - double(matrix[14]) * inverse[6 + axis]) / determinant);
        result.forward[axis] = coordinate(double(inverse[6 + axis]) / determinant);
        result.right[axis] = coordinate(double(inverse[axis]) / determinant, false);
        result.up[axis] = coordinate(double(inverse[3 + axis]) / determinant, false);
    }
    return result;
}

std::int32_t Weather::sample() {
    return tables_.positive[tables_.cursor];
}

void Weather::next() {
    tables_.cursor = (tables_.cursor + 1) % 37;
}

Weather::Weather(
    PropFragmentTables& tables, const SceneSetup& setup, SourceRandom& random, const WeatherCamera& camera, bool low_gravity, std::optional<WeatherPersistentState> previous)
    : tables_(tables), center_(center(camera)) {
    if (setup.weather < 1 || setup.weather > 5 || tables.cursor >= 37) {
        throw std::runtime_error("Weather requires resolved original weather and a valid shared random-table cursor");
    }
    rain_ = setup.weather == 3 || setup.weather == 5;
    snow_ = setup.weather == 4;
    storm_ = setup.weather == 5;
    sprites_ = snow_ || (rain_ && setup.selection.cat_dog);
    const auto count = sprites_ ? 25u : rain_ ? (storm_ ? 10u : 90u) : 0u;
    frame_.particles.resize(count);
    alpha_.resize(count);
    variants_.resize(count);
    for (std::size_t particle = 0; particle < count; ++particle) {
        // Source RNG order is X,Z,Y, not memory coordinate order.
        const auto fraction = [&] {
            return std::int32_t(std::int64_t(random.next()) * one / 32767);
        };
        const auto x = fraction(), z = fraction(), y = fraction();
        frame_.particles[particle] = add(center_, Vector{sub(mul(x, 12 * one), 6 * one), sub(mul(y, 12 * one), 6 * one), sub(mul(z, 12 * one), 6 * one)});
        if (sprites_) {
            variants_[particle] = std::uint8_t(sample() > 0x8000);
            next();
        }
    }
    if (count != 0) {
        fall_[1] = sprites_ ? (low_gravity ? 0xa3d : 0x6666) : (low_gravity ? 0xf5c : 0x9999);
        if (!setup.selection.rain_up) {
            fall_[1] = neg(fall_[1]);
        }
        duration_ = std::max(mul(sample(), 100 * one) / one, 50);
        next();
        interval_ = std::max(mul(sample(), 100 * one) / one, 50); // Source deliberately does not advance this second read.
        calm_ = true;
        hold_ = 80;
    } else if (previous) {
        const auto& wind = previous->wind;
        frame_.wind = wind.velocity;
        strength_ = wind.strength;
        target_strength_ = wind.target_strength;
        angle_ = wind.angle;
        target_angle_ = wind.target_angle;
        elapsed_ = wind.elapsed;
        duration_ = wind.duration;
        interval_ = wind.interval;
        hold_ = wind.hold;
        calm_ = wind.calm;
    }
    if (previous) {
        thunder_remaining_ = previous->thunder_remaining;
        thunder_strength_ = previous->strength;
        pending_thunder_ = previous->pending;
    }
    geometry();
    previous_frame_ = frame_;
    particle_cuts_.resize(count);
}

std::optional<std::int32_t> Weather::advance() {
    const auto thunder = pending_thunder_;
    pending_thunder_.reset();
    if (elapsed_ < duration_ || calm_) {
        if (elapsed_ < interval_ || hold_ != 0) {
            ++elapsed_;
            if (hold_ > 0) {
                --hold_;
            }
        } else {
            target_angle_ = std::int16_t(mul(sample(), 360 * one) / 0x1680);
            next();
            target_strength_ = mul(sample(), one / 2);
            if (snow_ || storm_) {
                target_strength_ = mul(target_strength_, one / 2);
            } else if (rain_) {
                target_strength_ = mul(target_strength_, 2 * one);
            }
            next();
            duration_ = add(mul(sample(), 50 * one), 50 * one) / one;
            next();
            interval_ = add(mul(sample(), 50 * one), 50 * one) / one;
            next();
            calm_ = false;
            elapsed_ = 0;
        }
    } else {
        target_strength_ = 0;
        interval_ = add(mul(sample(), 50 * one), 50 * one) / one;
        next();
        duration_ = add(mul(sample(), 50 * one), 50 * one) / one;
        next();
        calm_ = true;
        hold_ = 80;
        elapsed_ = 0;
    }
    const auto difference = sub(target_strength_, strength_);
    strength_ = simulation::fixed::abs(difference) > 0x146 ? add(strength_, difference < 1 ? -0x147 : 0x147) : target_strength_;
    // The original angle difference is a signed16 subtraction with no shortest-
    // turn wrapping. Only the final lookup index is masked to twelve bits.
    const auto angle_difference = std::int16_t(std::uint16_t(target_angle_) - std::uint16_t(angle_));
    angle_ = simulation::fixed::abs(angle_difference) > 10 ? std::int16_t(angle_ + (angle_difference < 1 ? -11 : 11)) : target_angle_;
    frame_.wind = {mul(sine(std::int32_t(angle_) + 1024), strength_), 0, mul(sine(angle_), strength_)};
    ++frame_.wind_updates;
    return thunder;
}

void Weather::lightning() {
    if (!storm_) {
        return;
    }
    if (!flashing_) {
        if (--flash_remaining_ < 0) {
            flashing_ = true;
            flash_variant_ = std::uint32_t(sample() > 0x7fff);
            next();
            flash_remaining_ = 5;
        }
    } else if (--flash_remaining_ < 0) {
        flashing_ = false;
        flash_remaining_ = add(mul(sample(), 150 * one), 600 * one) / one;
        thunder_remaining_ = add(mul(sample(), 30 * one), 60 * one) / one + 1;
        thunder_strength_ = sub(one, sample());
        next();
    }
    if (thunder_remaining_ == 1) {
        pending_thunder_ = thunder_strength_;
        thunder_remaining_ = 0;
    } else if (thunder_remaining_ > 0) {
        --thunder_remaining_;
    }
    constexpr std::array<std::array<std::uint8_t, 6>, 2> opacity{{{60, 100, 140, 180, 0, 180}, {60, 100, 140, 180, 0, 0}}};
    frame_.lightning_alpha = flashing_ ? opacity[flash_variant_][std::size_t(flash_remaining_)] : 0;
}

void Weather::update_view(const WeatherCamera& camera) {
    previous_frame_ = frame_;
    const auto current_center = center(camera);
    const bool camera_cut = length(sub(center_, current_center)) > 5 * one;
    const auto movement = add(frame_.wind, fall_);
    auto camera_motion = sub(center_, current_center);
    camera_motion[1] = 0;
    if (!sprites_ && length(camera_motion) > 5 * one) {
        camera_motion = {};
    }
    frame_.streak = add(camera_motion, movement);
    if (!frame_.particles.empty()) {
        center_ = current_center;
    }
    for (std::size_t index = 0; index < frame_.particles.size(); ++index) {
        auto relative = sub(add(frame_.particles[index], movement), current_center);
        bool wrapped = false;
        for (auto& component : relative) {
            const auto before = component;
            component = wrap(component);
            wrapped |= before != component;
        }
        frame_.particles[index] = add(relative, current_center);
        particle_cuts_[index] = wrapped || camera_cut;
        if (sprites_) {
            if (wrapped) {
                variants_[index] = std::uint8_t(sample() > 0x8000);
            }
            // 462d20 advances even when this sprite did not wrap.
            next();
        } else {
            const auto facing = std::clamp(mul(neg(dot(camera.forward, relative)), 0x2ac0), -one, one);
            alpha_[index] = std::uint8_t(add(mul(facing, 50 * one), 158 * one) / one);
        }
    }
    lightning();
    geometry();
    ++frame_.updates;
}

void Weather::geometry() {
    frame_.lines.clear();
    frame_.sprites.clear();
    for (std::size_t index = 0; index < frame_.particles.size(); ++index) {
        const auto position = frame_.particles[index];
        if (sprites_) {
            const auto half = rain_ ? 0x4000 : 0xccc;
            frame_.sprites.push_back({position, {-half, -half, 0}, {half, half, 0}, snow_ ? WeatherTexture::snow : variants_[index] ? WeatherTexture::dogs : WeatherTexture::cats});
        } else {
            frame_.lines.push_back({add(position, frame_.streak), position, (std::uint32_t(alpha_[index]) << 24) | 0x969696u});
        }
    }
}

const WeatherFrame& Weather::frame() const {
    return frame_;
}

WeatherFrame Weather::presentation_frame(float alpha) const {
    WeatherFrame result;
    presentation_frame_into(result, alpha);
    return result;
}

void Weather::presentation_frame_into(WeatherFrame& result, float alpha) const {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Weather presentation requires a finite fraction");
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    result = frame_;
    if (alpha == 1) {
        return;
    }
    const auto interpolate = [alpha](const WeatherVector& previous, WeatherVector& current) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            current[axis] = std::int32_t(std::llround(std::lerp(double(previous[axis]), double(current[axis]), double(alpha))));
        }
    };
    for (std::size_t particle = 0; particle < result.particles.size(); ++particle) {
        if (particle_cuts_[particle]) {
            continue;
        }
        interpolate(previous_frame_.particles[particle], result.particles[particle]);
        if (!result.lines.empty()) {
            interpolate(previous_frame_.lines[particle].from, result.lines[particle].from);
            result.lines[particle].to = result.particles[particle];
        } else {
            result.sprites[particle].center = result.particles[particle];
        }
    }
}

WeatherWindState Weather::wind_state() const {
    return {frame_.wind, strength_, target_strength_, angle_, target_angle_, elapsed_, duration_, interval_, hold_, calm_};
}

WeatherPersistentState Weather::persistent_state() const {
    return {wind_state(), thunder_remaining_, thunder_strength_, pending_thunder_};
}
} // namespace toca::scene
