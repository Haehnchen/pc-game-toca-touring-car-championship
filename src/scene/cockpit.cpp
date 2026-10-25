#include "scene/cockpit.hpp"

#include "assets/store.hpp"
#include "formats/cm3d.hpp"
#include "formats/glb.hpp"
#include "formats/model_validation.hpp"
#include "image/decode.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/matrix.hpp"
#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace toca::scene {
class CockpitRoleCache {
    struct Entry {
        std::size_t role;
        std::vector<std::uint8_t> source;
        std::filesystem::path replacement;
        Model model;
    };

    std::vector<Entry> entries_;

public:
    const Model* find(std::size_t role, std::span<const std::uint8_t> source, const std::filesystem::path& replacement) const {
        const auto entry = std::ranges::find_if(entries_, [&](const Entry& entry) {
            return entry.role == role && entry.replacement == replacement && std::ranges::equal(entry.source, source);
        });
        return entry == entries_.end() ? nullptr : &entry->model;
    }

    void remember(std::size_t role, std::span<const std::uint8_t> source, const std::filesystem::path& replacement, const Model& model) {
        entries_.push_back({role, {source.begin(), source.end()}, replacement, model});
    }
};

namespace {
using namespace simulation::fixed;
using Matrix = std::array<float, 16>;
constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

Matrix multiply(const Matrix& a, const Matrix& b) {
    return matrix::source_product(a, b);
}

Matrix fixed_pose_matrix(const Basis& basis, const Vector& position) {
    auto result = identity;
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            result[row * 4 + col] = float(basis[row][col]) / 65536.f;
        }
        result[12 + row] = float(position[row]) / 65536.f;
    }
    return result;
}

Basis glove_basis(const Vector& grip, const Vector& shoulder, std::int32_t sine_value, std::int32_t cosine) {
    // 42c280 builds the forearm axis from shoulder to grip, projects its
    // wheel-plane tangent, then takes their cross product. Scale each Q16
    // component in the source order, including CMHANDY's additional1b333.
    Basis basis;
    basis[0] = normalized(sub(grip, shoulder));
    basis[1] = normalized(project({0, cosine, sine_value}, basis[0]));
    basis[2] = normalized(cross(basis[0], basis[1]));
    return basis;
}

struct Grip {
    Vector position, shoulder;
    std::int32_t tangent_sine, tangent_cosine;
};

std::array<Grip, 2> grips(std::uint32_t car, std::int32_t angle) {
    const bool left_drive = cockpit_left_hand_drive(car);
    const Vector pivot{0xcccc, -0xccc, left_drive ? 0x6666 : -0x6666};
    const auto spread = car == 9 ? 0x200 : 0xe3;
    const auto first = angle + (left_drive ? spread : -spread), second = angle + (left_drive ? -spread : spread);
    const auto radius = left_drive ? 0x48f5 : -0x48f5;
    return {Grip{add(pivot, Vector{0, mul(sine(first), radius), mul(sine(first + 0x400), radius)}), {0x1999, -0xa666, 0x1999}, -sine(first), sine(first + 0x400)},
        Grip{add(pivot, Vector{0, mul(-sine(second), radius), mul(-sine(second + 0x400), radius)}), {left_drive ? 0xb333 : 0x1999, -0xa666, left_drive ? 0x1999 : -0xb333},
            -sine(second), sine(second + 0x400)}};
}

std::int16_t smoothed_angle(std::int16_t current, std::int16_t steering) {
    const auto target = std::bit_cast<std::int16_t>(std::uint16_t(mul(steering, 0x4000000 / 0x18e)));
    const auto delta = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(target) - std::uint16_t(current)));
    return std::bit_cast<std::int16_t>(std::uint16_t(std::int32_t(current) + (std::int32_t(delta) >> 1)));
}

constexpr Vector gear_lever{0x6666, -0x6666, 0};

void validate_cockpit_model(const Model& source, const Model& replacement) {
    const auto require = [](bool valid) {
        if (!valid) {
            throw std::runtime_error("Mega cockpit must retain original nodes, pivots, mesh names and texture bindings");
        }
    };
    require(model_structure_matches(source, replacement) && model_node_transforms_match(source, replacement));
    for (const auto& mesh : replacement.meshes) {
        require(!mesh.faces.empty());
    }
    for (const auto& texture : replacement.textures) {
        (void)decode_model_texture(texture);
    }
}
} // namespace

std::string_view cockpit_archive(std::uint32_t car_id) {
    // 42de80 has no tank branch, and no separate Vectra2 cockpit resource.
    constexpr std::array<std::string_view, 11> archives{"data/honddasx.bfp", "data/audidasx.bfp", "data/vectdasx.bfp", "data/volvdasx.bfp", "data/forddasx.bfp",
        "data/nissdasx.bfp", "data/p406dasx.bfp", "data/rendasx.bfp", "", "data/flexdasx.bfp", ""};
    if (car_id >= archives.size()) {
        throw std::runtime_error("Cockpit requires original car0..10");
    }
    return archives[car_id];
}

CockpitResources load_cockpit_resources(const AssetStore& store, std::uint32_t car_id, const std::filesystem::path& mega_models, std::shared_ptr<CockpitRoleCache> shared_roles) {
    CockpitResources result;
    const auto path = cockpit_archive(car_id);
    result.shared_roles = shared_roles ? std::move(shared_roles) : std::make_shared<CockpitRoleCache>();
    if (!path.empty()) {
        const auto archive = store.open_archive(path);
        std::array<std::filesystem::path, 2> shared_paths;
        if (!mega_models.empty()) {
            const auto shared = mega_models / "cockpit" / "shared";
            shared_paths = {shared / "wiper.glb", shared / "wheel.glb"};
        }
        std::array<bool, 2> reused{};
        for (std::size_t index = 0; index < result.models.size(); ++index) {
            const auto bytes = archive.member(index).bytes();
            const auto* cached = index >= 3 ? result.shared_roles->find(index, bytes, shared_paths[index - 3]) : nullptr;
            if (cached) {
                result.models[index] = *cached;
                reused[index - 3] = true;
            } else {
                result.models[index] = parse_cm3d(bytes);
            }
        }
        if (!mega_models.empty()) {
            constexpr std::array<std::string_view, 11> keys{"honda", "audi", "vectra", "volvo", "ford", "nissan", "peugeot", "renault", "", "flexmobile", ""};
            const auto vehicle = mega_models / "cockpit" / keys[car_id];
            for (const auto role : {0u, 1u, 2u, 3u, 4u}) {
                if (role >= 3 && reused[role - 3]) {
                    continue;
                }
                const auto replacement_path = role < 3 ? vehicle / (role == 0 ? "left-hand.glb" : role == 1 ? "right-hand.glb" : "dashboard.glb") : shared_paths[role - 3];
                try {
                    if (!std::filesystem::exists(replacement_path)) {
                        continue;
                    }
                    auto replacement = load_glb(replacement_path);
                    validate_cockpit_model(result.models[role], replacement);
                    result.models[role] = std::move(replacement);
                    std::clog << "Mega cockpit: car" << car_id << " role" << role << ' ' << replacement_path.string() << '\n';
                } catch (const std::runtime_error& error) {
                    std::clog << "Mega cockpit unavailable: " << replacement_path.string() << ": " << error.what() << "; using original role\n";
                }
            }
        }
        // Cache the final role independently, including missing/invalid Mega
        // fallbacks. Exact source bytes retain every original pivot/material;
        // the replacement path isolates Mega directories and original detail.
        for (const auto role : {3u, 4u}) {
            if (!reused[role - 3]) {
                result.shared_roles->remember(role, archive.member(role).bytes(), shared_paths[role - 3], result.models[role]);
            }
        }
    }
    return result;
}

bool cockpit_left_hand_drive(std::uint32_t car_id) {
    // 42c0e0 initializes DAT_004d53e8; the original defaults to the other
    // branch for the special IDs without their own interior.
    return car_id == 1 || car_id == 2 || car_id == 4 || car_id == 7;
}

bool local_car_visible(std::uint16_t view) {
    // 4271d0 ->427170 suppresses all local LOD roots in cockpit1 only.
    // Bonnet4 retains its body/hood, with separate wheel-node masks.
    return view != 1;
}

Cockpit::Cockpit(std::uint32_t car_id, std::uint32_t weather) : car_id_(car_id), weather_(weather) {
    (void)cockpit_archive(car_id);
    if (weather < 1 || weather > 5) {
        throw std::runtime_error("Cockpit requires resolved original weather1..5");
    }
    wiper_phase_ = weather >= 3 ? 1 : 0;
    if (car_id == 9) {
        // 42e1d0 parks Flex's left glove at the lever;42c280 never moves it.
        hands_[0].position = gear_lever;
    }
    previous_hands_ = hands_;
}

void Cockpit::update(std::int16_t steering_angle) {
    previous_hands_ = hands_;
    previous_wheel_angle_ = wheel_angle_;
    previous_wiper_angle_ = wiper_angle_;
    // 42c220 caches4000000/398 (local steering limit+276);42c280
    // halves the signed16 difference before storing the smoothed angle.
    const bool changed = wheel_angle_ != steering_angle;
    if (changed) {
        wheel_angle_ = smoothed_angle(wheel_angle_, steering_angle);
    }
    const auto primary = cockpit_left_hand_drive(car_id_) ? 0u : 1u, secondary = 1u - primary;
    const auto on_wheel = grips(car_id_, wheel_angle_);
    const auto place = [&](std::size_t index, const Grip& target) {
        hands_[index].position = target.position;
        hands_[index].basis = glove_basis(target.position, target.shoulder, target.tangent_sine, target.tangent_cosine);
        hands_[index].transformed = true;
    };
    if (changed) {
        place(primary, on_wheel[0]);
    }
    if (car_id_ != 9 && car_id_ != 8) {
        switch (shift_phase_) {
            case 0:
                if (changed) {
                    place(secondary, on_wheel[1]);
                    shift_position_ = on_wheel[1].position;
                }
                break;
            case 1:
                // 42c280 preserves the last wheel-grip basis during the
                // fixed-step reach, then snaps to the authored lever point.
                if (shift_count_ == shift_duration_ - 1) {
                    shift_count_ = 5;
                    shift_phase_ = 3;
                    shift_position_ = gear_lever;
                } else {
                    shift_position_ = add(shift_position_, shift_step_);
                    ++shift_count_;
                }
                hands_[secondary].position = shift_position_;
                break;
            case 2: {
                // When the primary-hand condition did not run, the source
                // computes another half-error angle locally without storing
                // it in54e0. The returning hand aims at this current wheel
                // target while its translation catches up in0x1999 steps.
                const auto target = grips(car_id_, changed ? wheel_angle_ : smoothed_angle(wheel_angle_, steering_angle))[1];
                place(secondary, target);
                const auto difference = sub(target.position, shift_position_);
                const auto distance = length(difference);
                if (distance < 0x1999) {
                    shift_position_ = target.position;
                    shift_phase_ = 0;
                } else {
                    shift_position_ = add(shift_position_, scale(scale(difference, reciprocal(distance)), 0x1999));
                }
                hands_[secondary].position = shift_position_;
                break;
            }
            case 3:
                if (shift_count_ < 1) {
                    shift_phase_ = 2;
                } else {
                    --shift_count_;
                }
                break;
        }
    }
    // 42e440/42db50: dry parked, rain continuous, snow pauses30 ticks.
    const auto increment = weather_ == 5 ? 0x22 : 0x2d;
    std::int32_t stopping_distance = 0;
    for (std::int32_t velocity = 0; velocity < (weather_ == 5 ? 0x71 : 0xaa); velocity += increment) {
        stopping_distance += velocity;
    }
    if (wiper_phase_ == 1 || wiper_phase_ == 2) {
        const bool outward = wiper_phase_ == 1;
        if (wiper_angle_ < stopping_distance + 0x71 && outward) {
            wiper_speed_ += increment;
        } else if (wiper_angle_ > 0x800 - stopping_distance && outward) {
            wiper_speed_ = std::max(wiper_speed_ - increment, increment);
        } else if (wiper_angle_ > 0x800 - stopping_distance && !outward) {
            wiper_speed_ += increment;
        } else if (wiper_angle_ < stopping_distance + 0x71 && !outward) {
            wiper_speed_ = std::max(wiper_speed_ - increment, increment);
        }
        wiper_angle_ += outward ? wiper_speed_ : -wiper_speed_;
        if (wiper_angle_ >= 0x800) {
            wiper_angle_ = 0x800;
            wiper_phase_ = 2;
        } else if (wiper_angle_ < 0x72) {
            wiper_angle_ = 0x71;
            wiper_phase_ = weather_ == 5 ? 3 : 1;
            wiper_hold_ = 30;
        }
    } else if (wiper_phase_ == 3) {
        if (wiper_hold_ < 1) {
            wiper_phase_ = 1;
        } else {
            --wiper_hold_;
        }
    }
}

void Cockpit::shift() {
    if (car_id_ == 8 || car_id_ == 9 || cockpit_archive(car_id_).empty()) {
        return;
    }
    const auto difference = sub(gear_lever, shift_position_);
    const auto distance = length(difference);
    if (distance > 0x1998) {
        shift_step_ = scale(scale(difference, reciprocal(distance)), 0x1999);
        shift_count_ = 0;
        shift_phase_ = 1;
        shift_duration_ = mul(distance, 0xa0000) / 65536;
    } else {
        shift_position_ = gear_lever;
        hands_[cockpit_left_hand_drive(car_id_) ? 1 : 0].position = gear_lever;
        shift_count_ = 5;
        shift_phase_ = 3;
    }
}

std::uint8_t Cockpit::shift_phase() const {
    return shift_phase_;
}

std::int16_t Cockpit::wheel_angle() const {
    return wheel_angle_;
}

std::int32_t Cockpit::wiper_angle() const {
    return wiper_angle_;
}

CockpitFrame Cockpit::frame(const Matrix& body, bool chun, bool handy, float alpha) const {
    if (!std::isfinite(alpha)) {
        throw std::runtime_error("Cockpit presentation requires a finite fraction");
    }
    alpha = std::clamp(alpha, 0.f, 1.f);
    const bool left_drive = cockpit_left_hand_drive(car_id_);
    const Vector pivot{0xcccc, -0xccc, left_drive ? 0x6666 : -0x6666};
    const auto difference = (std::uint32_t(std::int32_t(wheel_angle_) - previous_wheel_angle_) + 2048u) & 4095u;
    const auto angle = alpha == 1 ? std::int32_t(wheel_angle_) : std::int32_t(previous_wheel_angle_) + std::int32_t(std::lround(float(std::int32_t(difference) - 2048) * alpha));
    const auto wiper_angle = std::int32_t(std::lround(std::lerp(double(previous_wiper_angle_), double(wiper_angle_), double(alpha))));
    CockpitFrame result;
    const auto hand_matrix = [&](const Hand& hand) {
        HorizonTransform transform{};
        transform[15] = one;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            auto basis = hand.basis[axis];
            if (hand.transformed) {
                basis = scale(basis, 0x18000);
                if (handy) {
                    basis = scale(basis, 0x1b333);
                }
            }
            for (std::size_t component = 0; component < 3; ++component) {
                transform[axis * 4 + component] = basis[component];
            }
            transform[12 + axis] = hand.position[axis];
        }
        return transform;
    };
    for (std::size_t index = 0; index < hands_.size(); ++index) {
        auto transform = hand_matrix(hands_[index]);
        if (alpha != 1) {
            transform = interpolate_horizon_transform(hand_matrix(previous_hands_[index]), transform, alpha);
        }
        for (std::size_t component = 0; component < transform.size(); ++component) {
            result.transforms[index][component] = float(transform[component]) / one;
        }
    }
    // 42de80: dashboard+(.1 negative,.05,0), quarter turn about X.
    result.transforms[2] = fixed_pose_matrix({Vector{one, 0, 0}, Vector{0, 0, one}, Vector{0, -one, 0}}, {-0x1999, 0xccc, 0});
    result.transforms[4] = fixed_pose_matrix({Vector{one, 0, 0}, Vector{0, sine(angle + 0x400), -sine(angle)}, Vector{0, sine(angle), sine(angle + 0x400)}}, pivot);
    // 42de80's separate wiper parent: Z turn512,+ (1.2,.3,0).
    const auto wiper_parent = fixed_pose_matrix({Vector{sine(0x600), sine(0x200), 0}, Vector{-sine(0x200), sine(0x600), 0}, Vector{0, 0, one}}, {0x13333, 0x4ccc, 0});
    const auto wiper_scale = handy ? 2 : 1;
    const auto cosine = sine(wiper_angle + 0x400) * (left_drive ? 1 : -1) * wiper_scale;
    const auto sine_value = sine(wiper_angle) * wiper_scale;
    const auto wiper_local = fixed_pose_matrix({Vector{0, -sine_value, cosine}, Vector{0, cosine, sine_value}, Vector{one, 0, 0}}, {});
    result.transforms[3] = wiper_local;
    if (!cockpit_archive(car_id_).empty()) {
        result.visible = {true, true, !chun, !chun, true};
    }
    for (auto& transform : result.transforms) {
        transform = multiply(transform, body);
    }
    result.transforms[3] = multiply(wiper_local, multiply(wiper_parent, body));
    return result;
}

CockpitFrame Cockpit::frame(const Matrix& body, const CockpitSnapshot& published) const {
    if (published.car_id != car_id_ || published.duplicate_wiper) {
        throw std::runtime_error("Published cockpit requires the loaded car and its single-wiper template");
    }
    CockpitFrame result;
    for (std::size_t role = 0; role < published.roles.size(); ++role) {
        const auto& pose = published.roles[role];
        const auto finite = [](float value) {
            return std::isfinite(value);
        };
        if (pose.actual_mask > 15 || pose.manual_mask > 15 || !std::ranges::all_of(pose.local, finite) || !std::ranges::all_of(pose.parent_local, finite)) {
            throw std::runtime_error("Published cockpit pose requires finite matrices and four viewport bits");
        }
        // 47f500 publishes the parent world before composing its child. The
        // wiper's extra parent requires this binary32 store between products.
        result.transforms[role] = multiply(pose.local, multiply(pose.parent_local, body));
        result.visible[role] = (pose.actual_mask & 1) != 0;
    }
    return result;
}
} // namespace toca::scene
