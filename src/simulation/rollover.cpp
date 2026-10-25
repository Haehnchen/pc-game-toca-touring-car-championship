#include "simulation/rollover.hpp"

#include "simulation/fixed.hpp"

#include <stdexcept>

namespace toca::simulation {
using namespace fixed;

void RolloverState::observe(const std::array<std::int32_t, 3>& ground_dot) {
    const std::array<bool, 3> tilted{ground_dot[1] < -0xf333, ground_dot[2] < -0xf333 || ground_dot[2] > 0xf333, ground_dot[0] < -0xe666 || ground_dot[0] > 0xe666};
    const std::array<std::uint8_t, 3> limits{60, 10, 25};
    const std::array<bool*, 3> requests{&upside_down, &sideways, &pitched};
    for (std::size_t axis = 0; axis < requests.size(); ++axis) {
        if (!*requests[axis]) {
            ticks[axis] = tilted[axis] ? std::uint8_t(ticks[axis] + 1) : 0;
            if (ticks[axis] > limits[axis]) {
                *requests[axis] = true;
            }
        }
    }
}

BodyMode select_body_mode(BodyMode previous, const RolloverState& rollover, bool force_full, std::uint32_t contacts, std::int32_t normal_up) {
    if (previous == BodyMode::removed) {
        return previous;
    }
    if (force_full) {
        return BodyMode::full;
    }
    if (rollover.upside_down || rollover.sideways) {
        return previous == BodyMode::crashed ? previous : BodyMode::crashing;
    }
    if (previous == BodyMode::full && (contacts > 4 || (contacts == 4 && normal_up >= 0xf333))) {
        return BodyMode::distant;
    }
    return previous;
}

BodySelectionResult select_fleet_body_modes(std::span<const BodySelectionActor, 16> actors, const BodySelectionContext& context) {
    if (context.local_actor >= actors.size() || context.local_players < 1 || context.local_players > 4 || context.full_budget != 4) {
        throw std::runtime_error("Body selection requires original local actor/player count and four-body budget");
    }
    BodySelectionResult result;
    for (std::size_t actor = 0; actor < actors.size(); ++actor) {
        const auto& input = actors[actor];
        if (input.previous > BodyMode::removed || input.support_count > 8 || input.player_identity < -1 || input.player_identity > 3) {
            throw std::runtime_error("Body selection actor has an invalid mode, support count or player identity");
        }
        auto mode = input.previous;
        if (context.local_players < 2) {
            mode = select_body_mode(
                mode, input.rollover, input.human_control || input.player_identity >= 0 || input.first_tire_q16 < context.tire_base_q16, input.support_count, input.normal_up_q16);
        }
        result.modes[actor] = mode;
        result.full_count += mode == BodyMode::full ? 1u : 0u;
    }
    if (context.local_players >= 2 || result.full_count >= context.full_budget) {
        return result;
    }
    const auto neighbor = [&](std::uint32_t actor, bool ahead) {
        const auto next = ahead ? actors[actor].ahead : actors[actor].behind;
        if (next >= actors.size()) {
            throw std::runtime_error("Body selection spatial ring contains an invalid actor index");
        }
        return next;
    };
    const auto promote = [&](std::uint32_t actor) {
        if (result.modes[actor] == BodyMode::distant) {
            result.modes[actor] = BodyMode::full;
            ++result.full_count;
        }
    };
    auto actor = context.local_actor;
    const auto behind = neighbor(actor, false);
    if (!context.alternate_direction) {
        if (actors[behind].forward_gap_q16 < actors[actor].forward_gap_q16) {
            promote(behind);
        }
    } else if (actors[actor].forward_gap_q16 < actors[behind].forward_gap_q16) {
        promote(neighbor(actor, true));
    }
    for (std::size_t step = 1; step < actors.size() && result.full_count < context.full_budget; ++step) {
        actor = neighbor(actor, !context.alternate_direction);
        promote(actor);
    }
    return result;
}

bool prepare_rollover(RolloverState& rollover, bool player, const scene::BodyPose& body, std::array<std::int32_t, 3>& angular) {
    if (player && rollover.upside_down) {
        rollover.player_righting = true;
        rollover.upside_down = false;
        rollover.ticks[0] = 0;
        return true;
    }
    if (player && rollover.sideways) {
        rollover.player_righting = true;
        rollover.sideways = false;
        rollover.ticks[1] = 0;
    } else if (rollover.pitched) {
        angular[2] = body.basis[2][0] < 0 ? 0x2666 : -0x2666;
        rollover.pitched = false;
        rollover.ticks[2] = 0;
    }
    return false;
}

void advance_player_righting(RolloverState& rollover, std::int32_t normal_up, std::int32_t side_y, std::array<std::int32_t, 3>& angular) {
    if (rollover.player_righting) {
        if (normal_up < 0xb334) {
            angular[0] = side_y < 0 ? -0xccc : 0xccc;
        } else {
            rollover.player_righting = false;
        }
    }
}

void settle_rollover_pose(scene::BodyPose& body, std::uint32_t car_id, std::int32_t surface_height, const RolloverState& rollover) {
    auto& basis = body.basis;
    const auto half = scene::car_half_dimensions(car_id);
    const auto normal = body.ground_normal;
    if (rollover.upside_down) {
        body.position[1] = add(surface_height, half[1]);
        basis[1] = scale(normal, -one);
        basis[0] = normalized(project(basis[0], normal));
        basis[2] = normalized(cross(basis[0], basis[1]));
    } else if (rollover.sideways) {
        body.position[1] = add(surface_height, half[2]);
        basis[2] = basis[2][1] < 1 ? scale(normal, -one) : normal;
        basis[0] = normalized(project(basis[0], basis[2]));
        basis[1] = normalized(cross(basis[2], basis[0]));
    }
    scene::refresh_body_contacts(body, car_id);
    if (rollover.sideways) {
        const auto shift = scale(normal, 0xcccc);
        const std::size_t parity = basis[2][1] > 0 ? 0 : 1;
        for (std::size_t corner = parity; corner < body.contacts.size(); corner += 2) {
            body.contacts[corner] = sub(body.contacts[corner], shift);
        }
    }
}
} // namespace toca::simulation
