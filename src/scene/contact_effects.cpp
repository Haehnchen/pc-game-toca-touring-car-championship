#include "scene/contact_effects.hpp"

#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
namespace {
using namespace simulation::fixed;

void advance(PropFragmentTables& tables) {
    tables.cursor = (tables.cursor + 1) % 37;
}
} // namespace

ContactSparks::ContactSparks(PropFragmentTables& tables) : tables_(tables) {
    if (tables.cursor >= 37) {
        throw std::runtime_error("Contact sparks require the original cyclic table cursor");
    }
}

void ContactSparks::emit(const ScrapeSparkRequest& request) {
    if (sparks_.size() == 64) {
        return;
    }
    const auto count = std::min(std::size_t(tables_.counts[tables_.cursor]), std::size_t(64) - sparks_.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto speed = mul(request.intensity_q16, tables_.positive[tables_.cursor]);
        auto direction = request.direction;
        if (!request.ground) {
            direction[1] = add(direction[1], mul(tables_.signed_values[tables_.cursor], 0x4ccc));
        } else {
            advance(tables_);
            direction[1] = add(direction[1], mul(tables_.positive[tables_.cursor], 0x8000));
            const auto sideways = mul(tables_.signed_values[tables_.cursor], 0x4ccc);
            direction[0] = add(request.direction[0], mul(neg(request.direction[2]), sideways));
            direction[2] = add(request.direction[2], mul(request.direction[0], sideways));
        }
        direction = normalized(direction);
        Spark spark;
        spark.line.from = request.position;
        spark.line.to = add(request.position, scale(direction, 0x3333));
        spark.previous_line = spark.line;
        spark.velocity = add(scale(direction, speed), request.velocity);
        sparks_.push_back(spark);
        advance(tables_);
    }
}

void ContactSparks::update() {
    std::erase_if(sparks_, [](Spark& spark) {
        if (spark.remaining == 0) {
            return true;
        }
        spark.previous_line = spark.line;
        spark.line.from = add(spark.line.from, spark.velocity);
        spark.line.to = add(spark.line.to, spark.velocity);
        --spark.remaining;
        return false;
    });
}

void ContactSparks::clear() {
    sparks_.clear();
}

std::span<const ContactSparkLine> ContactSparks::lines() const {
    lines_.clear();
    // 418920 exports only the ring interval between head and tail. At all64
    // occupied records those indices coincide, so the source exports no lines.
    if (sparks_.size() == 64) {
        return lines_;
    }
    for (const auto& spark : sparks_) {
        lines_.push_back(spark.line);
    }
    return lines_;
}

std::vector<ContactSparkLine> ContactSparks::presentation_lines(float alpha) const {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Spark presentation requires a finite fraction");
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    std::vector<ContactSparkLine> result;
    // Preserve418920's full-ring export behavior in presentation too.
    if (sparks_.size() == 64) {
        return result;
    }
    result.reserve(sparks_.size());
    for (const auto& spark : sparks_) {
        auto line = spark.line;
        if (alpha != 1) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                line.from[axis] = std::int32_t(std::llround(std::lerp(double(spark.previous_line.from[axis]), double(line.from[axis]), double(alpha))));
                line.to[axis] = std::int32_t(std::llround(std::lerp(double(spark.previous_line.to[axis]), double(line.to[axis]), double(alpha))));
            }
        }
        result.push_back(line);
    }
    return result;
}

std::size_t ContactSparks::size() const {
    return sparks_.size();
}
} // namespace toca::scene
