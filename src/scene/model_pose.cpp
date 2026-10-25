#include "scene/model_pose.hpp"

#include "core/contracts.hpp"
#include "scene/camera.hpp"
#include "scene/horizon_actors.hpp"
#include "scene/matrix.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
using matrix::identity;
using matrix::Matrix;
using matrix::source_product;

Matrix horizon_world_transform(const CameraInputs& camera, std::uint32_t track_id) {
    // 431440 sets the track-specific Q16 camera-to-sky height; 431670
    // follows camera translation with that subtraction, never orientation.
    constexpr std::array<std::int32_t, 10> height_q16{0x18000, 0x14ccc, 0x14ccc, 0x14ccc, 0x1b333, 0x18000, 0x16666, 0x20000, 0x16666, 0x14ccc};
    Matrix pose = identity;
    if (camera.published_world_position) {
        std::copy(camera.published_world_position->begin(), camera.published_world_position->end(), pose.begin() + 12);
    } else {
        const auto world = matrix::inverse(camera.world_to_view, "Scene horizon camera has no invertible basis");
        std::copy_n(world.begin() + 12, 3, pose.begin() + 12);
    }
    pose[13] -= float(height_q16.at(track_id)) / 65536.f;
    return pose;
}

std::uint32_t local_car_model_index(std::uint32_t detail) {
    if (detail >= graphics_detail_count) {
        throw std::runtime_error("Invalid local car detail");
    }
    return detail < 2 ? 1u : 0u; // 004271d0 current-player branch.
}

std::uint32_t rival_model_id(std::uint32_t actor_id, std::uint32_t car_id) {
    return actor_id == 5 && car_id == 2 ? 10u : car_id;
}

Model prepare_car_model(const Model& model, std::uint32_t model_id, std::uint32_t graphics_detail, std::uint32_t model_index) {
    auto prepared = model;
    if (graphics_detail >= 2 || model_index != 1) {
        return prepared;
    }
    int front = 0x7ae, rear = 0x7ae;
    // 44d7f9 reads the physical car ID, so actor5's model10 uses car2.
    switch (model_id == 10 ? 2 : model_id) {
        case 1:
            front = 0x28e;
            rear = 0x10e4;
            break;
        case 2:
            front = 0x170a;
            rear = -0x106;
            break;
        case 7:
            front = rear = 0xdd2;
            break;
        default:
            break;
    }
    for (auto& node : prepared.nodes) {
        switch (node.id_flags & 0xffffffu) {
            case 1:
                node.transform[14] += float(front) / 65536.f;
                break;
            case 2:
                node.transform[14] -= float(front) / 65536.f;
                break;
            case 3:
                node.transform[14] += float(rear) / 65536.f;
                break;
            case 4:
                node.transform[14] -= float(rear) / 65536.f;
                break;
            default:
                break;
        }
    }
    return prepared;
}

void model_world_transforms_into(std::vector<Matrix>& result, const Model& model, const Matrix& object_to_world) {
    const auto root = object_to_world;
    result.clear();
    result.reserve(model.nodes.size());
    for (const auto& node : model.nodes) {
        if (node.parent < -1 || (node.parent >= 0 && std::size_t(node.parent) >= result.size())) {
            throw std::runtime_error("Scene model parent must precede child");
        }
        result.push_back(source_product(node.transform, node.parent < 0 ? root : result[std::size_t(node.parent)]));
    }
}

std::vector<Matrix> model_world_transforms(const Model& model, const Matrix& object_to_world) {
    std::vector<Matrix> result;
    model_world_transforms_into(result, model, object_to_world);
    return result;
}

void horizon_actor_world_transforms_into(std::vector<Matrix>& result, const Model& model, const HorizonModelFrame& frame) {
    if (frame.transforms.size() != model.nodes.size() || frame.visible.size() != model.nodes.size()) {
        throw std::runtime_error("Scene actor presentation must match its loaded hierarchy");
    }
    result.clear();
    result.reserve(model.nodes.size());
    for (std::size_t index = 0; index < model.nodes.size(); ++index) {
        const auto parent = model.nodes[index].parent;
        if (parent < -1 || (parent >= 0 && std::size_t(parent) >= result.size())) {
            throw std::runtime_error("Scene actor parent must precede child");
        }
        Matrix local;
        std::ranges::transform(frame.transforms[index], local.begin(), [](auto value) {
            return float(value) / 65536.f;
        });
        // 004608b0 copies the controller's Q16 local bases before the ordinary
        // scenegraph composition. Rotorcraft body/rotor retain both parents.
        result.push_back(source_product(local, parent < 0 ? identity : result[std::size_t(parent)]));
    }
}

std::vector<Matrix> horizon_actor_world_transforms(const Model& model, const HorizonModelFrame& frame) {
    std::vector<Matrix> result;
    horizon_actor_world_transforms_into(result, model, frame);
    return result;
}

void car_world_transforms_into(std::vector<Matrix>& result, const Model& model, const CarPose& pose, std::uint32_t detail) {
    (void)local_car_model_index(detail);
    const auto sine = [](std::int32_t angle) {
        constexpr double two_pi = 0x1.921fb54442d18p+2;
        return float(std::int32_t(std::sin(double(std::uint32_t(angle) & 0xfff) / 4096.0 * two_pi) * 65536.0)) / 65536.f;
    };
    result.clear();
    result.reserve(model.nodes.size());
    for (const auto& node : model.nodes) {
        if (node.parent < -1 || (node.parent >= 0 && std::size_t(node.parent) >= result.size())) {
            throw std::runtime_error("Car model parent must precede child");
        }
        auto local = node.transform;
        const auto id = node.id_flags & 0xffffffu;
        if (detail >= 2 && id == 300) {
            local = car_body_visual_transform(local, pose.visual_angles);
        }
        if (id >= 1 && id <= 4 && (detail >= 2 || id <= 2)) {
            // 4608b0 replaces wheel bases, retaining each model's local pivot.
            for (std::size_t row = 0; row < 3; ++row) {
                std::copy_n(identity.begin() + row * 4, 3, local.begin() + row * 4);
            }
            if (id <= 2) {
                // 480750 front-wheel yaw; the high-detail pair share this basis.
                local[0] = local[10] = sine(pose.steering_angle + 0x400);
                local[8] = sine(pose.steering_angle);
                local[2] = -local[8];
                if (detail < 2 && id == 2) {
                    for (const auto index : {0, 2, 8, 10}) {
                        local[std::size_t(index)] = -local[std::size_t(index)];
                    }
                }
            }
            if (detail >= 2) {
                const auto angle = pose.wheel_angles[id - 1];
                if (id > 2) {
                    // 4807f0 absolute Z rotation for the two rear wheels.
                    local[0] = local[5] = sine(angle + 0x400);
                    local[1] = sine(angle);
                    local[4] = -local[1];
                } else if (angle != 0) {
                    // 4809e0 ->47fe20 rotates about the existing local Z row.
                    local = matrix::source_axis_rotation(local, 2, sine(-std::int32_t(angle)), sine(0x400 - angle));
                }
            }
        }
        result.push_back(source_product(local, node.parent < 0 ? pose.object_to_world : result[std::size_t(node.parent)]));
    }
}

std::vector<Matrix> car_world_transforms(const Model& model, const CarPose& pose, std::uint32_t detail) {
    std::vector<Matrix> result;
    car_world_transforms_into(result, model, pose, detail);
    return result;
}

} // namespace toca::scene
