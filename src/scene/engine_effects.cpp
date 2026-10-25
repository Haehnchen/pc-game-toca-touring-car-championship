#include "scene/engine_effects.hpp"

#include "assets/store.hpp"
#include "simulation/fixed.hpp"
#include "simulation/source_transform.hpp"

#include <stdexcept>

namespace toca::scene {
namespace fixed = simulation::fixed;

namespace {
using namespace fixed;

const std::array<Vector, 10> exhaust_positions{{{bits(0xfffdbcad), bits(0xffff93f8), 0x5ced}, {bits(0xfffdc7f0), bits(0xffff9605), bits(0xffffb99a)},
    {bits(0xfffdcd0f), bits(0xffffa9fc), bits(0xffffb99a)}, {bits(0xfffdda1d), bits(0xffff849c), 0x94bc}, {bits(0xfffdb6c9), bits(0xffff9be8), bits(0xffffc3d8)},
    {bits(0xfffdd1aa), bits(0xffff926f), bits(0xffffc20d)}, {bits(0xfffdd47b), bits(0xffff9eb9), bits(0xffff6313)}, {bits(0xfffdc9bb), bits(0xffff8ac1), bits(0xffffc107)},
    {bits(0xfffdb894), 0x1999, 0}, {bits(0xfffd4ccd), bits(0xffff9063), bits(0xffffb334)}}};

SurfaceEffectSprite sprite(const Vector& center, std::int32_t radius, std::uint8_t shade) {
    return {center, {neg(radius), neg(radius), 0}, {radius, radius, 0}, shade, 0};
}
} // namespace

EngineEffectResources load_engine_effect_resources(const AssetStore& store) {
    const auto images = store.open_archive("data/image640.bfp");
    const auto bytes = images.member(16).bytes();
    return {{bytes.begin(), bytes.end()}};
}

EngineEffects::EngineEffects(PropFragmentTables& tables, GameMode mode) : tables_(tables), mode_(mode) {
    if (tables.cursor >= tables.positive.size()) {
        throw std::runtime_error("Engine effects require the original shared random-table cursor0..36");
    }
}

void EngineEffects::next() {
    tables_.cursor = (tables_.cursor + 1) % 37;
}

void EngineEffects::advance() {
    ++frame_.updates;
    if (frame_.cooldown > 0) {
        --frame_.cooldown;
    }
    frame_.phase = frame_.phase > 0 ? frame_.phase - 1 : 2;
}

void EngineEffects::emit_vehicle(const EngineEffectVehicle& vehicle) {
    const auto position = add(vehicle.position, simulation::source_transform::world(exhaust_positions[vehicle.car_type], vehicle.basis));
    const auto append = [&](const Vector& center, std::int32_t radius, std::uint8_t shade) {
        frame_.sprites.push_back(sprite(center, radius, shade));
        sprite_actors_.push_back(vehicle.actor);
        frame_.visibility.push_back(vehicle.visibility);
        ++frame_.emitted;
    };
    if (vehicle.car_type == 8 && mode_ == GameMode::time_trial) {
        active_[vehicle.actor] = true;
        age_[vehicle.actor] = 0;
        for (int index = 0; index < 2; ++index) {
            const auto sample = tables_.positive[tables_.cursor];
            const auto radius = sub(index == 0 ? 0x3333 : 0x3d70, mul(sample, 0xccc));
            // 420cf9/420ea0 deliberately subtract the fixed product from the
            // integer200, then divide toward zero and keep its low byte.
            const auto shade = std::uint8_t(sub(200, mul(sample, 0x960000)) / one);
            append(index == 0 ? position : add(position, scale(vehicle.basis[0], -0x1999)), radius, shade);
            next();
        }
        return;
    }
    if (!active_[vehicle.actor]) {
        const auto increase = sub(vehicle.engine_speed_q16, vehicle.previous_engine_speed_q16);
        if (increase > 0x9999 && vehicle.gear != 0 && tables_.cursor == 0 && frame_.phase == 0 && frame_.cooldown == 0) {
            append(position, 0x1999, 200);
            frame_.sounds.push_back({position, 17, one, vehicle.local_player, vehicle.actor});
            frame_.cooldown = 900;
            active_[vehicle.actor] = true;
            age_[vehicle.actor] = 0;
        }
        return;
    }
    if (age_[vehicle.actor] == 2) {
        active_[vehicle.actor] = false;
        return;
    }
    const auto age = ++age_[vehicle.actor];
    const auto center = add(position, scale(vehicle.basis[0], mul(std::int32_t(age) * one, -0x3333)));
    const auto radius = add(0x1999, mul(std::int32_t(age) * one, 0xccc));
    append(center, radius, std::uint8_t(200 - age * 40));
}

void EngineEffects::emit(std::span<const EngineEffectVehicle> vehicles) {
    frame_.sprites.clear();
    sprite_actors_.clear();
    frame_.visibility.clear();
    frame_.sounds.clear();
    for (const auto& vehicle : vehicles) {
        if (vehicle.actor >= active_.size() || vehicle.car_type >= exhaust_positions.size()) {
            throw std::runtime_error("Engine effects require original car slots0..15 and car types0..9");
        }
        // 4608b0 tests ed968 before420be0, preserving a hidden burst's age.
        if (vehicle.visibility != 0) {
            emit_vehicle(vehicle);
        }
    }
}

const EngineEffectFrame& EngineEffects::frame() const {
    return frame_;
}

EngineEffectFrame EngineEffects::presentation_frame(std::span<const EffectPose> poses) const {
    auto result = frame_;
    for (std::size_t index = 0; index < result.sprites.size(); ++index) {
        result.sprites[index].center = effect_position(result.sprites[index].center, sprite_actors_[index], poses);
    }
    return result;
}

void EngineEffects::reset() {
    active_.fill(false);
    frame_.sprites.clear();
    sprite_actors_.clear();
    frame_.visibility.clear();
    frame_.sounds.clear();
}
} // namespace toca::scene
