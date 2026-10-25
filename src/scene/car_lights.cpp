#include "scene/car_lights.hpp"

#include "core/contracts.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace toca::scene {
namespace {
using Point = std::array<std::int32_t, 3>;
//434f00 initializes Q16 coordinates, then offsets six vertices per car.
//The front table has a five-vertex stride: that sixth X also advances the
//next car's first vertex. These values retain that source overlap.
constexpr std::array<std::array<Point, 5>, 10> front_points{{
    {{{{142409, -1441, 24641}}, {{145555, -7471, 21430}}, {{134282, -1179, 43515}}, {{137625, -7405, 43581}}, {{655, 0, 0}}}},
    {{{{139001, -720, 26017}}, {{140377, -6225, 24248}}, {{132447, -262, 42991}}, {{134676, -5832, 42467}}, {{655, 0, 0}}}},
    {{{{141753, -589, 28049}}, {{143457, -5111, 25952}}, {{140312, -4784, 35061}}, {{132382, 458, 44105}}, {{134676, -4390, 45088}}}},
    {{{{134741, 1966, 33030}}, {{137100, -6291, 30736}}, {{127336, 2949, 49872}}, {{130809, -5701, 50003}}, {{655, 0, 0}}}},
    {{{{141032, 1114, 27656}}, {{142802, -4194, 28573}}, {{131071, 4849, 44171}}, {{135659, -4063, 44695}}, {{655, 0, 0}}}},
    {{{{136707, -2162, 25362}}, {{138673, -6356, 22675}}, {{130350, -1376, 42926}}, {{132775, -6488, 43188}}, {{655, 0, 0}}}},
    {{{{144178, 2031, 28114}}, {{147127, -1900, 25296}}, {{135003, -393, 46202}}, {{141884, -2424, 44826}}, {{3276, 0, 0}}}},
    {{{{141032, 1245, 28311}}, {{140967, -4259, 26935}}, {{136576, -4325, 41091}}, {{130743, 2359, 43188}}, {{133824, -4390, 44761}}}},
    {{{{1310, 0, 0}}, {{655, 0, 0}}, {{655, 0, 0}}, {{655, 0, 0}}, {{655, 0, 0}}}},
    {{{{188742, 16252, 43974}}, {{187235, 6291, 43974}}, {{188415, 16777, 68616}}, {{187432, 6619, 69206}}, {{655, 0, 0}}}},
}};
constexpr std::array<std::array<Point, 6>, 10> rear_points{{
    {{{{-148045, 9109, -37879}}, {{-148503, 5111, -37552}}, {{-146931, 9175, -46137}}, {{-147389, 5111, -46333}}, {{-140377, 9175, -50069}}, {{-140639, 5111, -50987}}}},
    {{{{-129432, 10223, -49545}}, {{-129629, 13238, -49086}}, {{-132578, 10616, -47906}}, {{-131923, 13631, -47710}}, {{-135986, 10616, -39976}}, {{-134544, 13631, -41680}}}},
    {{{{-137231, 12124, -34471}}, {{-137755, 7864, -33554}}, {{-132709, 12255, -43712}}, {{-133102, 7798, -43450}}, {{-132250, 12255, -43974}}, {{-129891, 7798, -45219}}}},
    {{{{-140836, 16777, -40697}}, {{-140901, 13303, -38993}}, {{-137887, 17039, -51970}}, {{-137887, 13697, -52101}}, {{-327, 0, 0}}, {{-327, 0, 0}}}},
    {{{{-140115, 20381, -40763}}, {{-141229, 13434, -38141}}, {{-140639, 13303, -42336}}, {{-133889, 14876, -47513}}, {{-137297, 11141, -45613}}, {{-327, 0, 0}}}},
    {{{{-141294, 7077, -38600}}, {{-141753, 655, -36831}}, {{-140770, 7208, -47382}}, {{-141163, 655, -48037}}, {{-133037, 7012, -49676}}, {{-135920, 655, -49610}}}},
    {{{{-141426, 6946, -44040}}, {{-142605, 2686, -42467}}, {{-140442, 7340, -50331}}, {{-141753, 2686, -49283}}, {{-133889, 7274, -53936}}, {{-139132, 2949, -51183}}}},
    {{{{-139198, 9502, -40042}}, {{-139787, 3801, -39649}}, {{-132972, 9568, -45416}}, {{-134282, 3997, -45809}}, {{-2621, 0, 0}}, {{-2621, 0, 0}}}},
    {{{{-327, 0, 0}}, {{-327, 0, 0}}, {{-327, 0, 0}}, {{-327, 0, 0}}, {{-327, 0, 0}}, {{-327, 0, 0}}}},
    {{{{-182386, 27721, 66715}}, {{-173210, 22872, 65011}}, {{-171900, 27852, 71237}}, {{-173014, 32571, 64487}}, {{-173079, 27721, 62193}}, {{-327, 0, 0}}}},
}};
constexpr std::array<std::array<Point, 6>, 10> medium_rear_points{{
    {{{{-147062, 9436, -37879}}, {{-147520, 5438, -37552}}, {{-145948, 9502, -46137}}, {{-146406, 5438, -46333}}, {{-139394, 9502, -50069}}, {{-139656, 5438, -50987}}}},
    {{{{-129432, 10223, -49545}}, {{-129629, 13238, -49086}}, {{-132578, 10616, -47906}}, {{-131923, 13631, -47710}}, {{-135986, 10616, -39976}}, {{-134544, 13631, -41680}}}},
    {{{{-135921, 14745, -33161}}, {{-135921, 10485, -32244}}, {{-135921, 14876, -39125}}, {{-135921, 10419, -40174}}, {{-130940, 14876, -42664}}, {{-128581, 10419, -43909}}}},
    {{{{-138215, 16777, -39059}}, {{-138280, 13303, -37355}}, {{-138280, 17039, -50332}}, {{-138215, 13697, -50463}}, {{2294, 0, 1638}}, {{2294, 0, 1638}}}},
    {{{{-140115, 20381, -40763}}, {{-141229, 13434, -38141}}, {{-140639, 13303, -42336}}, {{-133889, 14876, -47513}}, {{-137297, 11141, -45613}}, {{-327, 0, 0}}}},
    {{{{-141621, 7404, -38273}}, {{-142080, 982, -36504}}, {{-141097, 7535, -47055}}, {{-141490, 982, -47710}}, {{-133364, 7339, -49349}}, {{-136247, 982, -49283}}}},
    {{{{-141426, 7929, -45678}}, {{-142605, 3669, -44105}}, {{-140442, 8323, -51969}}, {{-141753, 3669, -50921}}, {{-133889, 8257, -55574}}, {{-139132, 3932, -52821}}}},
    {{{{-138871, 10485, -33489}}, {{-139460, 4784, -33096}}, {{-139460, 10551, -38863}}, {{-139460, 4980, -39256}}, {{-2294, 983, 6553}}, {{-2294, 983, 6553}}}},
    {{{{-327, 0, 0}}, {{-327, 0, 0}}, {{-327, 0, 0}}, {{-327, 0, 0}}, {{-327, 0, 0}}, {{-327, 0, 0}}}},
    {{{{-182386, 27721, 66715}}, {{-173210, 22872, 65011}}, {{-171900, 27852, 71237}}, {{-173014, 32571, 64487}}, {{-173079, 27721, 62193}}, {{-327, 0, 0}}}},
}};
} // namespace

CarLampFrame car_lamp_frame(std::uint32_t car, std::uint32_t detail, const CarLights& lights, bool front_view) {
    if (car == 10) {
        car = 2; //Actor5's alternate Vectra livery has the same geometry.
    }
    if (car > 9 || detail >= graphics_detail_count) {
        throw std::runtime_error("Car lamps require an original car and graphics detail");
    }
    CarLampFrame result;
    if ((front_view && !lights.headlights) || (!front_view && !lights.brake && !lights.tail) || car == 8) {
        return result;
    }
    // 499740 repacks the source RGBA bytes into D3D ARGB before drawing.
    const auto color = front_view ? 0xffffffe6u : lights.brake ? 0xffff0000u : 0xffc80000u;
    for (std::size_t side = 0; side < 2; ++side) {
        if (lights.broken[front_view ? side : 3 - side]) {
            continue;
        }
        const auto emit = [&](std::array<std::size_t, 3> indices) {
            auto& triangle = result.triangles.at(result.count++);
            triangle.color = color;
            if (side != 0) {
                std::swap(indices[1], indices[2]);
            }
            for (std::size_t vertex = 0; vertex < 3; ++vertex) {
                const auto& point = front_view ? front_points[car][indices[vertex]] : (detail < 2 ? medium_rear_points : rear_points)[car][indices[vertex]];
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    triangle.positions[vertex][axis] = float(point[axis]) / 65536.f;
                }
                if (side != 0) {
                    triangle.positions[vertex][2] = -triangle.positions[vertex][2];
                }
            }
        };
        if ((front_view && (car == 2 || car == 7)) || (!front_view && car == 4)) {
            emit({0, 2, 1});
            emit({3, 2, 0});
            emit({4, 2, 3});
        } else if (!front_view && car == 9) {
            emit({0, 1, 2});
            emit({0, 2, 3});
            emit({0, 3, 4});
            emit({0, 4, 1});
        } else {
            const bool extra = !front_view && (car == 1 || (detail >= 2 && (car == 0 || car == 2 || car == 5)));
            if (extra && !(car == 1 && !lights.brake)) {
                emit({2, 4, 3});
                emit({5, 3, 4});
            }
            emit({0, 2, 1});
            emit({3, 1, 2});
            if (extra && car == 1 && !lights.brake) {
                emit({2, 4, 3});
                emit({5, 3, 4});
            }
        }
    }
    return result;
}

std::vector<CarLampLensTriangle> mega_car_lamp_lenses(const Model& model, std::uint32_t car) {
    // Reuse the recovered illuminated contours only as clipping boundaries;
    // their source X coordinates must not replace a rebuilt lens's depth.
    const CarLights lit{.headlights = true, .tail = true, .brake = true};
    const std::array<CarLampFrame, 2> contours{car_lamp_frame(car, 3, lit, false), car_lamp_frame(car, 3, lit, true)};
    std::vector<CarLampLensTriangle> result;
    const auto cross = [](const auto& a, const auto& b, const auto& p) {
        return (b[1] - a[1]) * (p[2] - a[2]) - (b[2] - a[2]) * (p[1] - a[1]);
    };
    const auto interpolate = [](const ModelVertex& a, const ModelVertex& b, float fraction) {
        ModelVertex output;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            output.position[axis] = a.position[axis] + (b.position[axis] - a.position[axis]) * fraction;
            output.normal[axis] = a.normal[axis] + (b.normal[axis] - a.normal[axis]) * fraction;
        }
        output.u = a.u + (b.u - a.u) * fraction;
        output.v = a.v + (b.v - a.v) * fraction;
        return output;
    };
    const auto clip = [&](std::vector<ModelVertex> polygon, const auto& distance) {
        std::vector<ModelVertex> clipped;
        if (polygon.empty()) {
            return clipped;
        }
        auto previous = polygon.back();
        auto previous_distance = distance(previous);
        for (const auto& current : polygon) {
            const auto current_distance = distance(current);
            if ((current_distance >= 0) != (previous_distance >= 0)) {
                clipped.push_back(interpolate(previous, current, previous_distance / (previous_distance - current_distance)));
            }
            if (current_distance >= 0) {
                clipped.push_back(current);
            }
            previous = current;
            previous_distance = current_distance;
        }
        return clipped;
    };
    for (std::size_t node_index = 0; node_index < model.nodes.size(); ++node_index) {
        const auto& node = model.nodes[node_index];
        const auto id = node.id_flags & 0xffffffu;
        const bool front_node = id == 24 || id == 25;
        const bool body_lamps = car == 9 && id == 30;
        if ((!body_lamps && !front_node && id != 10 && id != 11) || node.variants.empty() || node.variants.front() == 0xffff) {
            continue;
        }
        // All canonical HIGH touring cars retain these lamp nodes, including
        // Mega's original-model fallback. MEDIUM/LOW have no separate glass.
        const auto& mesh = model.meshes.at(node.variants.front());
        for (const bool front : {false, true}) {
            if (!body_lamps && front != front_node) {
                continue;
            }
            const auto& frame = contours[front ? 1 : 0];
            for (const auto& face : mesh.faces) {
                if (face.texture < 0) {
                    continue; // Housing/gasket geometry must not emit light.
                }
                if (body_lamps) {
                    // Patriot's lamps are part of body30, unlike the separate
                    // touring-car glass nodes. Restrict its emission to actual
                    // outward end faces before clipping the authored footprint.
                    const auto& a = mesh.vertices.at(face.indices[0]).position;
                    const auto& b = mesh.vertices.at(face.indices[1]).position;
                    const auto& c = mesh.vertices.at(face.indices[2]).position;
                    const std::array normal{(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]), (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
                        (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])};
                    const auto length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
                    if (length == 0 || (front ? normal[0] : -normal[0]) < .2f * length) {
                        continue;
                    }
                }
                for (std::size_t contour_index = 0; contour_index < frame.count; ++contour_index) {
                    const auto& contour = frame.triangles[contour_index].positions;
                    const auto area = cross(contour[0], contour[1], contour[2]);
                    if (std::abs(area) < 1e-12f) {
                        continue;
                    }
                    const auto sign = area > 0 ? 1.f : -1.f;
                    std::vector<ModelVertex> polygon;
                    for (const auto index : face.indices) {
                        polygon.push_back(mesh.vertices.at(index));
                    }
                    if (body_lamps) {
                        const auto minimum_x = std::min({contour[0][0], contour[1][0], contour[2][0]}) - .025f;
                        const auto maximum_x = std::max({contour[0][0], contour[1][0], contour[2][0]}) + .025f;
                        polygon = clip(std::move(polygon), [&](const auto& vertex) {
                            return vertex.position[0] - minimum_x;
                        });
                        polygon = clip(std::move(polygon), [&](const auto& vertex) {
                            return maximum_x - vertex.position[0];
                        });
                    }
                    for (std::size_t edge = 0; edge < 3 && !polygon.empty(); ++edge) {
                        const auto& a = contour[edge];
                        const auto& b = contour[(edge + 1) % 3];
                        polygon = clip(std::move(polygon), [&](const auto& vertex) {
                            return sign * cross(a, b, vertex.position);
                        });
                    }
                    for (std::size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
                        CarLampLensTriangle triangle{{polygon[0], polygon[corner], polygon[corner + 1]}, node_index, node.id_flags, face.texture, 0, front};
                        if (std::abs(cross(triangle.vertices[0].position, triangle.vertices[1].position, triangle.vertices[2].position)) < 1e-12f) {
                            continue;
                        }
                        // The source front contour starts at +Z; its rear starts
                        // at -Z. The flexible bonus car reverses the latter.
                        const bool negative = contour[0][2] + contour[1][2] + contour[2][2] < 0;
                        triangle.broken_index = std::uint8_t(front ? negative ? 1 : 0 : (car == 9 ? negative ? 2 : 3 : negative ? 3 : 2));
                        for (auto& vertex : triangle.vertices) {
                            const auto length = std::sqrt(vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2]);
                            for (std::size_t axis = 0; axis < 3 && length > 0; ++axis) {
                                vertex.position[axis] += vertex.normal[axis] * (.00025f / length);
                            }
                        }
                        result.push_back(triangle);
                    }
                }
            }
        }
    }
    return result;
}
} // namespace toca::scene
