#include "simulation/damage.hpp"

#include "simulation/fixed.hpp"
#include "simulation/source_snapshot.hpp"

#include <algorithm>
#include <stdexcept>

namespace toca::simulation {
namespace {
void advance_cycle(DamageState& state) {
    state.cycle = state.cycle == 5 ? 0 : std::uint8_t(state.cycle + 1);
}
} // namespace

DamageResult apply_damage(DamageState& state, const DamageInput& input) {
    using namespace fixed;
    if (input.car_id > 9) {
        throw std::runtime_error("Damage requires original car index0..9");
    }
    advance_cycle(state);
    DamageResult result;
    result.strength_q16 = std::min(length(input.contact.impulse_q16), one);
    result.event_kind = result.strength_q16 > 0x106 ? 1 : 0;
    if (result.strength_q16 < 0x4ccc) {
        return result;
    }
    result.severity = result.strength_q16 < 0xb333 ? 1 : result.strength_q16 > 0xf332 ? 3 : 2;
    if (input.car_id >= 8) {
        return result;
    }
    Vector position;
    for (unsigned axis = 0; axis < 3; ++axis) {
        position[axis] = mul(input.contact.local_contact_q16[axis], input.inverse_half_dimensions_q16[axis]);
    }
    position[0] = add(position[0], 0xccc);
    const auto x = abs(position[0]), y = abs(position[1]), z = abs(position[2]);
    std::uint8_t side;
    if (x >= y && z <= x) {
        side = position[0] > 0 ? 5 : 4;
    } else if (z <= y) {
        side = position[1] > 0 ? 2 : 3;
    } else {
        side = position[2] > 0 ? 0 : 1;
    }
    unsigned count = 0;
    auto& selection = state.selection;
    switch (side) {
        case 0:
            selection[0] = {5, 0, 0};
            selection[1] = {22, 4, 9};
            selection[2] = {16, 3, 6};
            count = 3;
            break;
        case 1:
            selection[0] = {6, 0, 1};
            selection[1] = {23, 4, 10};
            selection[2] = {15, 3, 5};
            count = 3;
            break;
        case 2:
            selection[0] = {20, 2, 12};
            if (position[0] < 0) {
                selection[1] = {13, 2, 11};
                selection[2] = {21, 3, 8};
            } else {
                selection[1] = {17, 3, 7};
                selection[2] = {9, 1, 3};
            }
            count = 3;
            break;
        case 3:
            // Original419d2c changes only this byte: retain the prior slot/ID.
            selection[0].rule = 5;
            count = 1;
            break;
        case 4:
            selection = {{{13, 2, 11}, {21, 3, 8}, {12, 1, 4}, {}, {}}};
            if (position[2] <= 0) {
                selection[3] = {15, 3, 5};
                selection[4] = {11, 2, 16};
            } else {
                selection[3] = {10, 2, 15};
                selection[4] = {16, 3, 6};
            }
            count = 5;
            break;
        case 5:
            selection = {{{9, 1, 3}, {17, 3, 7}, {8, 1, 2}, {}, {}}};
            if (position[2] <= 0) {
                selection[3] = {6, 0, 1};
                selection[4] = {25, 2, 14};
            } else {
                selection[3] = {24, 2, 13};
                selection[4] = {5, 0, 0};
            }
            count = 5;
            break;
    }
    if (input.feedback_enabled) {
        state.feedback_strength = result.strength_q16 < 0x999a ? std::uint16_t(result.strength_q16 / 8) : 10000;
        state.feedback_side = std::uint8_t(side + 1);
    }
    for (unsigned index = count; index > 0; --index) {
        advance_cycle(state);
        const auto& selected = selection[index - 1];
        auto& old = state.levels.at(selected.slot);
        if (old >= result.severity) {
            continue;
        }
        auto level = result.severity;
        switch (selected.rule) {
            case 1:
                if (level == 1) {
                    level = 0;
                }
                break;
            case 2:
                if (level == 1) {
                    level = 0;
                } else if (level == 3) {
                    level = 2;
                }
                if (selected.slot >= 13 && selected.slot <= 16 && level == 2) {
                    result.event_kind = 3;
                }
                break;
            case 3:
                if (level == 3) {
                    level = 2;
                }
                if ((selected.slot == 7 || selected.slot == 8) && level == 2) {
                    result.event_kind = 4;
                }
                break;
            case 4:
                if (level == 1) {
                    level = 0;
                } else if (level == 2) {
                    level = 3;
                }
                if ((selected.slot == 9 || selected.slot == 10) && level == 3) {
                    result.event_kind = 2;
                }
                break;
        }
        if (level == 3) {
            const auto pool = input.debris_status ? input.debris_status() : DamageInput::DebrisStatus{input.debris_count, input.debris_blocked_by_recent_part};
            if (pool.count < 10 && !pool.blocked_by_recent_part && state.cycle == 2) {
                if (input.detach) {
                    input.detach(selected.slot);
                }
            } else {
                level = 2;
            }
        }
        if (level == 2) {
            if (selected.node_id == 24) {
                state.broken_features[0] = true;
            } else if (selected.node_id == 25) {
                state.broken_features[1] = true;
            } else if (selected.node_id == 10) {
                state.broken_features[2] = true;
            } else if (selected.node_id == 11) {
                state.broken_features[3] = true;
            }
        }
        old = level;
    }
    return result;
}

void restore_source_damage(DamageState& state, std::span<const std::uint8_t, 17> levels, std::span<const std::uint8_t, 16> broken_features) {
    auto restored = state;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (levels[i] > 3) {
            throw std::runtime_error("Original actor damage level exceeds variants0..3");
        }
        restored.levels[i] = levels[i];
    }
    for (std::size_t i = 0; i < restored.broken_features.size(); ++i) {
        const auto flag = source_u32(broken_features, i * 4);
        if (flag > 1) {
            throw std::runtime_error("Original broken-feature flag must be0or1");
        }
        restored.broken_features[i] = flag != 0;
    }
    state = restored;
}

void reset_damage(DamageState& state) {
    state = {};
}
} // namespace toca::simulation
