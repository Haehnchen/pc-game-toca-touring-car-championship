#include "scene/skid_marks.hpp"

#include "audio/wheel_queue.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
namespace fixed = simulation::fixed;

namespace {
using namespace fixed;
// 478b20 generates DAT539840; DAT53a920 is its sin[1080] entry.
constexpr std::int32_t turn_limit = 65294;

Vector contact(const SurfaceEffectWheel& wheel) {
    return {wheel.position[0], add(wheel.ground_height, 0xa3d), wheel.position[2]};
}

void begin_quad(SkidMarkQuad& quad, const Vector& center, const Vector& side) {
    const auto offset = scale(side, 0x1999);
    quad.vertices[0] = sub(center, offset);
    quad.vertices[3] = add(center, offset);
    quad.vertices[1] = quad.vertices[0];
    quad.vertices[2] = quad.vertices[3];
}
} // namespace

SkidMarks::SkidMarks(const SceneSetup& setup, SkidSectorQuery sectors, std::optional<SkidMarkPersistentState> previous)
    : sectors_(std::move(sectors)), rain_(setup.weather == 3 || setup.weather == 5) {
    if (setup.selection.track_id >= 10 || setup.weather < 1 || setup.weather > 5 || !sectors_) {
        throw std::runtime_error("Skid marks require an original track, resolved weather and sector query");
    }
    if (previous) {
        if (previous->cursor > stored_.size()) {
            throw std::runtime_error("Skid marks require an original ring cursor0..100");
        }
        stored_ = previous->records;
        cursor_ = previous->cursor;
    }
    // 441c90 invalidates sectors but retains vertex/color bytes and f2808.
    // 44ab90 then clears fade flags/timers for the new scene's surface colors.
    for (auto& record : stored_) {
        record.quad.sector = 0xffff;
        record.delay = 0;
        record.fading = false;
    }
    const auto track = setup.selection.track_id;
    if (setup.weather <= 2) {
        constexpr std::array<std::uint32_t, 10> gravel{0xdc5a4229, 0xb4784229, 0xb45a4229, 0xb48a7355, 0xb4705d44, 0xdc5a4229, 0xb45a4229, 0xb45a4229, 0xb4705d44, 0xb45a4229};
        colors_ = {0xa0000000, 0xa0000000, 0x781e1405, gravel[track]};
    } else {
        // 44ab90's hardware branch: snow shares wet colors, but44c3a0's
        // road/curb fade is enabled only by the separate rain flag.
        colors_ = {0x14969696, 0x32646464, track == 6 ? 0x960f1408u : 0x9609140cu, track == 1 ? 0x963b1c11u : 0x96271c11u};
    }
}

SkidMarkQuad SkidMarks::descriptor(const Wheel& wheel, std::uint8_t visibility) const {
    auto result = wheel.quad;
    result.color = colors_[wheel.surface];
    result.sector = sectors_(result.vertices[0]);
    result.visibility = visibility;
    return result;
}

void SkidMarks::store(const Wheel& wheel, std::uint8_t visibility) {
    // 44b480's first branch. The physical ring order is also the draw order.
    if (cursor_ == stored_.size()) {
        cursor_ = 0;
    }
    auto& slot = stored_[cursor_];
    slot.quad = descriptor(wheel, visibility);
    slot.fading = rain_ && wheel.surface < 2;
    slot.delay = slot.fading ? 50 : 0;
    ++cursor_;
    count_ = std::min(count_ + 1, stored_.size());
    ++emitted_;
}

void SkidMarks::start(Wheel& state, const SurfaceEffectVehicle& vehicle, const SurfaceEffectWheel& wheel, const Vector& direction) {
    state.origin = state.current = contact(wheel);
    state.normal = vehicle.support_normal;
    state.direction = direction;
    state.side = cross(state.direction, state.normal);
    state.surface = wheel.surface;
    begin_quad(state.quad, state.origin, state.side);
}

void SkidMarks::wheel(Wheel& state, const SurfaceEffectVehicle& vehicle, const SurfaceEffectWheel& wheel) {
    const auto previous = state.slipping;
    // 410110 writes car+517 before later body stabilization; retain this
    // explicit flag rather than reconstructing it from corrected coordinates.
    state.slipping = audio::source_wheel_slip(wheel.longitudinal_slip, wheel.lateral_slip, std::uint8_t(wheel.airborne)).current != 0;
    if (!state.slipping) {
        if (previous) {
            store(state, vehicle.visibility);
        }
        return;
    }
    if (!previous) {
        start(state, vehicle, wheel, normalized(vehicle.velocity));
        return;
    }
    if (dot(state.normal, vehicle.support_normal) < turn_limit || wheel.surface != state.surface) {
        // This direction is the previous endpoint, before sampling this tick's
        // wheel. The source closes the old material/plane before starting anew.
        const auto direction = normalized(sub(state.current, state.origin));
        store(state, vehicle.visibility);
        start(state, vehicle, wheel, direction);
        return;
    }
    state.current = contact(wheel);
    const auto delta = sub(state.current, state.origin), direction = normalized(delta);
    state.side = cross(direction, state.normal);
    const auto offset = scale(state.side, 0x1999);
    state.quad.vertices[1] = sub(state.current, offset);
    state.quad.vertices[2] = add(state.current, offset);
    if (dot(state.direction, direction) < turn_limit || length(delta) > 0x30000) {
        store(state, vehicle.visibility);
        state.origin = state.current;
        state.normal = vehicle.support_normal;
        state.direction = direction;
        state.side = cross(state.direction, state.normal);
        state.quad.vertices[0] = state.quad.vertices[1];
        state.quad.vertices[3] = state.quad.vertices[2];
        state.surface = wheel.surface;
    } else if (active_.size() < 99) {
        active_.push_back(descriptor(state, vehicle.visibility));
        active_anchors_.push_back({vehicle.actor, state.normal});
    }
}

void SkidMarks::update(std::span<const SurfaceEffectVehicle> vehicles) {
    ++updates_;
    // 441d10 sets dcdc0 after consuming the transient queue; the next44b480
    // clears it once. Native drawing is read-only, so each25Hz step owns it.
    active_.clear();
    active_anchors_.clear();
    for (const auto& vehicle : vehicles) {
        if (vehicle.actor >= wheels_.size()) {
            throw std::runtime_error("Skid marks support the original sixteen car slots");
        }
        for (std::size_t index = 0; index < vehicle.wheels.size(); ++index) {
            if (vehicle.wheels[index].surface > 3) {
                throw std::runtime_error("Skid marks require an original wheel surface class0..3");
            }
            auto& state = wheels_[vehicle.actor][index];
            if (vehicle.visibility == 0) {
                // 446b50→44c430 closes an invisible nonhuman's last segment
                // and clears slip. Distant bodies retain old wheel heights;
                // they must not extend geometry until visible again.
                if (state.slipping) {
                    store(state, 0);
                    state.slipping = false;
                }
            } else {
                wheel(state, vehicle, vehicle.wheels[index]);
            }
        }
    }
    if (rain_) {
        // 446b50 calls44c3a0 after the car loop; the commit tick counts as the
        // first delay decrement. Expired alpha stays in its ring slot.
        for (std::size_t index = 0; index < count_; ++index) {
            auto& slot = stored_[index];
            if (slot.fading) {
                if (slot.delay > 0) {
                    --slot.delay;
                } else if ((slot.quad.color >> 24) != 0) {
                    slot.quad.color -= UINT32_C(0x01000000);
                }
            }
        }
    }
}

void SkidMarks::build_frame(SkidMarkFrame& result) const {
    result.persistent = count_;
    result.active = active_.size();
    result.updates = updates_;
    result.emitted = emitted_;
    result.quads.clear();
    result.quads.reserve(count_ + active_.size());
    for (std::size_t index = 0; index < count_; ++index) {
        result.quads.push_back(stored_[index].quad);
    }
    result.quads.insert(result.quads.end(), active_.begin(), active_.end());
}

const SkidMarkFrame& SkidMarks::frame() const {
    build_frame(frame_);
    return frame_;
}

SkidMarkFrame SkidMarks::presentation_frame(std::span<const EffectPose> poses) const {
    SkidMarkFrame result;
    presentation_frame_into(result, poses);
    return result;
}

void SkidMarks::presentation_frame_into(SkidMarkFrame& result, std::span<const EffectPose> poses) const {
    build_frame(result);
    for (std::size_t index = 0; index < active_.size(); ++index) {
        const auto& anchor = active_anchors_[index];
        auto& quad = result.quads[count_ + index];
        for (const auto corner : {1u, 2u}) {
            const auto original = quad.vertices[corner];
            auto point = effect_position(original, anchor.actor, poses);
            // Keep the live end on its existing support plane while following
            // the displayed wheel in XZ. Stored road marks never move.
            point[1] = original[1];
            if (anchor.normal[1] != 0) {
                const auto change = (double(point[0]) - original[0]) * anchor.normal[0] + (double(point[2]) - original[2]) * anchor.normal[2];
                point[1] -= std::int32_t(std::llround(change / anchor.normal[1]));
            }
            quad.vertices[corner] = point;
        }
    }
}

void SkidMarks::reset() {
    wheels_ = {};
    active_.clear();
    active_anchors_.clear();
    count_ = 0;
}

std::size_t SkidMarks::cursor() const {
    return cursor_;
}

SkidMarkPersistentState SkidMarks::persistent_state() const {
    return {stored_, cursor_};
}
} // namespace toca::scene
