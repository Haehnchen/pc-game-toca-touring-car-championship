#include "simulation/damage_parts.hpp"

#include "scene/barriers.hpp"
#include "scene/horizon_actors.hpp"
#include "simulation/source_snapshot.hpp"
#include "simulation/source_transform.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace toca::simulation {
namespace {
using namespace fixed;
constexpr std::array<Vector, 5> pivots{{{0x19999, -0x3333, 0xb333}, {0x19999, -0x3333, -0xb333}, {0x19999, 0x2666, 0}, {0x23333, -0x4ccc, 0}, {-0x23333, -0x4ccc, 0}}};
constexpr std::array<std::uint8_t, 5> types{0, 1, 3, 2, 4}, rotation_axes{1, 1, 2, 2, 2}, launch_axes{2, 2, 1, 0, 0};
constexpr std::array<std::int32_t, 5> angles{-227, 227, 113, 113, 113}, launch{0x9999, -0x9999, 0x9999, 0x9999, -0x9999};

Basis rotation(const Vector& axis, std::int32_t angle) {
    // 47fc00, as used by416320. Keep the original per-product Q16 shifts.
    const auto s = sine(-angle), c = sine(1024 - angle), t = sub(one, c);
    Basis result{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto square = mul(axis[i], axis[i]);
        result[i][i] = add(mul(sub(one, square), c), square);
    }
    const auto xy = mul(mul(axis[0], axis[1]), t), xz = mul(mul(axis[0], axis[2]), t), yz = mul(mul(axis[1], axis[2]), t);
    result[0][1] = sub(xy, mul(axis[2], s));
    result[0][2] = add(xz, mul(axis[1], s));
    result[1][0] = add(xy, mul(axis[2], s));
    result[1][2] = sub(yz, mul(axis[0], s));
    result[2][0] = sub(xz, mul(axis[1], s));
    result[2][1] = add(yz, mul(axis[0], s));
    return result;
}
} // namespace

DetachedParts::DetachedParts(DamageSurfaceQuery surface, DamageSectorQuery sector, DamageSectorVisible visible, bool low_gravity)
    : surface_(std::move(surface)), sector_(std::move(sector)), visible_(std::move(visible)), gravity_(low_gravity ? 0xc4 : 0x7ae) {}

void DetachedParts::reset() {
    //415d50 releases every slot without erasing retained flight/landing
    //fields.416320 overwrites only its launch inputs on reuse.
    for (auto& part : parts_) {
        part.active = false;
    }
    free_.fill(true);
    count_ = 0;
    previous_parts_ = parts_;
}

void DetachedParts::restore_source_state(std::span<const std::uint8_t, 10 * 0x11c> pool, std::span<const std::uint8_t, 40> free_flags, std::uint32_t count) {
    if (count > parts_.size()) {
        throw std::runtime_error("Original detached count exceeds ten records");
    }
    std::array<DetachedPart, 10> restored;
    std::array<bool, 10> restored_free;
    for (std::size_t slot = 0; slot < restored.size(); ++slot) {
        const auto raw = pool.subspan(slot * 0x11c, 0x11c);
        auto& part = restored[slot];
        const auto free = source_u32(free_flags, slot * 4), active = source_u32(raw, 0x108), resting = source_u32(raw, 0x118);
        if (free > 1 || active > 1 || resting > 1 || (free != 0) == (active != 0) || raw[0x10c] >= types.size()) {
            throw std::runtime_error("Original detached record has invalid allocation, state or type");
        }
        //416320 points the two matrices into this exact pool record. Free
        //records retain these pointers after417830; untouched records are zero.
        const auto first = source_u32(raw, 0), second = source_u32(raw, 4);
        const auto source_base = std::uint32_t(0x4bf8a8 + slot * 0x11c);
        if ((active || first || second) && (first != source_base + 8 || second != source_base + 0x48)) {
            throw std::runtime_error("Original detached matrix pointers do not belong to their pool slot");
        }
        part.active = active != 0;
        restored_free[slot] = free != 0;
        part.resting = resting != 0;
        part.type = raw[0x10c];
        part.source_slot = std::uint8_t(std::find(types.begin(), types.end(), part.type) - types.begin());
        part.car_id = part.actor_id = DetachedPart::unknown_owner;
        const auto vector = [&](std::size_t offset) {
            return Vector{source_i32(raw, offset), source_i32(raw, offset + 4), source_i32(raw, offset + 8)};
        };
        for (std::size_t row = 0; row < 3; ++row) {
            part.rotation[row] = vector(0x88 + row * 16);
            part.basis[row] = vector(0xc8 + row * 12);
        }
        part.position = vector(0xec);
        part.velocity = vector(0xf8);
        part.initial_y = source_i32(raw, 0x104);
        part.cached_triangle = source_u16(raw, 0x10e);
        part.remaining_ticks = source_i32(raw, 0x110);
        part.sector = source_u32(raw, 0x114);
    }
    //Count is not a high-water mark or a compacted active-record prefix.
    //417890's changing-count traversal also visits retained free records.
    parts_ = restored;
    free_ = restored_free;
    count_ = count;
    previous_parts_ = parts_;
}

void DetachedParts::detach(std::uint8_t source_slot, const DamagePartsVehicle& vehicle) {
    if (!vehicle.sector_visible || vehicle.model_detail >= 2 || count_ >= parts_.size() || source_slot >= types.size()) {
        return;
    }
    //416320 tests the owner's retained physical-root+120 registration, before
    //constructing the new part. Its already-updated position can be elsewhere.
    if (visible_ && !visible_(vehicle.source_sector)) {
        return;
    }
    const auto available = std::find(free_.begin(), free_.end(), true);
    if (available == free_.end()) {
        return;
    }
    const auto index = std::size_t(available - free_.begin());
    auto& part = parts_[index];
    free_[index] = false;
    //416320 retains timer/sector until416870 lands the reused record.
    part.resting = false;
    part.cached_triangle = 0;
    part.active = true;
    part.source_slot = source_slot;
    part.type = types[source_slot];
    part.car_id = vehicle.car_id;
    part.actor_id = vehicle.actor_id;
    part.basis = vehicle.body.basis;
    //416320 calls47b150: round the complete transform once, before translation.
    part.position = add(vehicle.body.position, source_transform::world(pivots[part.type], part.basis));
    part.initial_y = part.position[1];
    part.rotation = rotation(part.basis[rotation_axes[part.type]], angles[part.type]);
    part.velocity = add(scale(part.basis[launch_axes[part.type]], launch[part.type]), scale(vehicle.velocity, 0x8000));
    part.velocity[1] = std::max(part.velocity[1], 0);
    previous_parts_[index] = part;
    ++count_;
}

DamageContext::DamageContext(const HpcSurface& surface, const scene::BarrierData* barriers, bool low_gravity, DamagePersistentState prior)
    : parts_(
          [&surface](auto x, auto z, auto triangle) {
              //416870→42e9f0 keeps the caller's cached plane when the
              //containing-face search fails inside the bounded HPC root.
              return surface.prop_sample_retaining_cached(x, z, triangle);
          },
          [barriers](const auto& point) {
              return barriers ? barriers->sector(point) : std::optional<std::uint32_t>{0};
          },
          [this](auto sector) {
              return !visible_ || visible_(sector);
          },
          low_gravity),
      cycle_(prior.cycle), selection_(prior.selection) {
    if (prior.cycle > 5) {
        throw std::runtime_error("Persistent damage cycle must be0..5");
    }
}

void DamageContext::set_visibility(DamageSectorVisible visible) {
    visible_ = std::move(visible);
}

DamageResult DamageContext::apply(DamageState& state, DamageInput input) {
    state.cycle = cycle_;
    state.selection = selection_;
    input.debris_status = [this] {
        return parts_.status();
    };
    const auto result = apply_damage(state, input);
    cycle_ = state.cycle;
    selection_ = state.selection;
    return result;
}

DetachedParts& DamageContext::parts() {
    return parts_;
}

const DetachedParts& DamageContext::parts() const {
    return parts_;
}

void DamageContext::advance() {
    parts_.advance();
}

void DamageContext::reset_parts() {
    parts_.reset();
}

std::uint8_t DamageContext::cycle() const {
    return cycle_;
}

DamagePersistentState DamageContext::persistent_state() const {
    return {cycle_, selection_};
}

void DetachedParts::retire(std::size_t index) {
    // 417830 changes this record and the count; it does not compact the array.
    if (count_) {
        parts_[index].active = false;
        free_[index] = true;
        --count_;
    }
}

bool DetachedParts::ground(std::size_t index) {
    auto& part = parts_[index];
    const auto sample = surface_(part.position[0], part.position[2], part.cached_triangle);
    if (!sample) {
        return false;
    }
    part.cached_triangle = sample->triangle;
    if (sample->height <= part.position[1]) {
        return false;
    }
    const auto& normal = sample->normal;
    std::size_t axis = 1;
    if (part.type <= 1) {
        axis = 2;
    } else if (part.type >= 3) {
        const auto x = dot(normal, part.basis[0]), y = dot(normal, part.basis[1]);
        // 416870's asymmetric side panels may settle on X only on their
        // outward face, and only when X dominates Y. Ties settle on Y.
        if ((part.type == 3 ? x < 0 : x >= 0) && abs(x) > abs(y)) {
            axis = 0;
        }
    }
    const auto sign = dot(normal, part.basis[axis]);
    if (!sign) {
        retire(index);
        return true;
    }
    part.basis[axis] = sign < 0 ? scale(normal, -one) : normal;
    std::int32_t lift = 0;
    if (axis == 2) {
        lift = sign < 0 ? 0x1999 : 0x3333;
        part.basis[0] = normalized(project(part.basis[0], part.basis[2]));
        part.basis[1] = normalized(cross(part.basis[2], part.basis[0]));
    } else if (axis == 1) {
        lift = part.type == 2 ? 0x1999 : 0x3333;
        part.basis[0] = normalized(project(part.basis[0], part.basis[1]));
        part.basis[2] = normalized(cross(part.basis[0], part.basis[1]));
    } else {
        lift = 0x2666;
        part.basis[1] = normalized(project(part.basis[1], part.basis[0]));
        part.basis[2] = normalized(cross(part.basis[0], part.basis[1]));
    }
    part.position[1] = add(sample->height, lift);
    part.sector = sector_(part.position).value_or(UINT32_MAX);
    part.remaining_ticks = 700;
    part.resting = true;
    return true;
}

void DetachedParts::advance() {
    previous_parts_ = parts_;
    // Preserve the original changing-count loop and noncompacting pool. A
    // removed early record can leave a higher active record unadvanced.
    for (std::size_t index = 0; index < count_; ++index) {
        auto& part = parts_[index];
        if (part.resting) {
            if (!visible_(part.sector) || part.remaining_ticks < 1) {
                retire(index);
            } else {
                --part.remaining_ticks;
            }
        } else {
            part.position = add(part.position, part.velocity);
            if (!ground(index)) {
                if (sub(part.initial_y, part.position[1]) < 0x50001) {
                    part.velocity[1] = sub(part.velocity[1], gravity_);
                    part.velocity[0] = sub(part.velocity[0], mul(part.velocity[0], 0xa3d));
                    part.velocity[2] = sub(part.velocity[2], mul(part.velocity[2], 0xa3d));
                    for (auto& row : part.basis) {
                        //417890 uses47b150 before the separate Q16 normalization.
                        row = normalized(source_transform::world(row, part.rotation));
                    }
                } else {
                    retire(index);
                }
            }
        }
    }
}

DamageInput::DebrisStatus DetachedParts::status() const {
    bool blocked = false;
    // 419ad0's original pool gate scans only the current count, too.
    for (std::size_t index = 0; index < count_; ++index) {
        blocked |= !parts_[index].resting || parts_[index].remaining_ticks > 500;
    }
    return {count_, blocked};
}

std::span<const DetachedPart> DetachedParts::parts() const {
    return parts_;
}

std::array<DetachedPart, 10> DetachedParts::presentation_parts(float alpha) const {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Detached-part presentation requires a finite fraction");
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    auto result = parts_;
    if (alpha == 1) {
        return result;
    }
    const auto transform = [](const DetachedPart& part) {
        scene::HorizonTransform matrix{};
        for (std::size_t row = 0; row < 3; ++row) {
            std::copy(part.basis[row].begin(), part.basis[row].end(), matrix.begin() + std::ptrdiff_t(row * 4));
            matrix[12 + row] = part.position[row];
        }
        matrix[15] = one;
        return matrix;
    };
    for (std::size_t slot = 0; slot < result.size(); ++slot) {
        auto& part = result[slot];
        if (!part.active) {
            continue;
        }
        const auto matrix = scene::interpolate_horizon_transform(transform(previous_parts_[slot]), transform(part), alpha);
        for (std::size_t row = 0; row < 3; ++row) {
            std::copy_n(matrix.begin() + std::ptrdiff_t(row * 4), 3, part.basis[row].begin());
            part.position[row] = matrix[12 + row];
        }
    }
    return result;
}

bool DetachedParts::free(std::size_t index) const {
    return free_.at(index);
}

void DamageContext::restore_source_state(std::uint32_t cycle, std::span<const std::uint8_t, 15> selection) {
    if (cycle > 5) {
        throw std::runtime_error("Original damage cycle must be0..5");
    }
    cycle_ = std::uint8_t(cycle);
    for (std::size_t i = 0; i < selection_.size(); ++i) {
        selection_[i] = {selection[i * 3], selection[i * 3 + 1], selection[i * 3 + 2]};
    }
}

void DamageContext::restore_source_state(std::uint32_t cycle, std::span<const std::uint8_t, 15> selection, std::span<const std::uint8_t, 10 * 0x11c> pool,
    std::span<const std::uint8_t, 40> free_flags, std::uint32_t count) {
    if (cycle > 5) {
        throw std::runtime_error("Original damage cycle must be0..5");
    }
    parts_.restore_source_state(pool, free_flags, count);
    restore_source_state(cycle, selection);
}
} // namespace toca::simulation
