#include "scene/visibility.hpp"

#include "simulation/fixed.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace toca::scene {
namespace {
using namespace simulation::fixed;

std::int32_t coordinate(float value, bool rounded) {
    const auto fixed = rounded ? std::nearbyint(double(value) * one) : std::trunc(double(value) * one);
    if (!std::isfinite(fixed) || fixed < std::numeric_limits<std::int32_t>::min() || fixed > std::numeric_limits<std::int32_t>::max()) {
        throw std::runtime_error("Visibility position exceeds original fixed coordinates");
    }
    return std::int32_t(fixed);
}

Vector coordinates(const std::array<float, 3>& value, bool rounded) {
    return {coordinate(value[0], rounded), coordinate(value[1], rounded), coordinate(value[2], rounded)};
}

std::array<std::int32_t, 2> grid_coordinates(const VisibilityData& data, const Vector& position) {
    const auto x = sub(add(data.half_size, position[0]), data.minimum_x);
    const auto z = add(sub(data.half_size, position[2]), data.maximum_z);
    // 47dab0 adds63 RAW units before its22-bit arithmetic shift.
    return {add(x, x < 0 ? 63 : 0) >> 22, add(z, z < 0 ? 63 : 0) >> 22};
}

VisibilityBounds bounds(const ModelMesh& mesh) {
    // 496980 initializes fixed extrema to +/-0x7fff0000 before conversion.
    VisibilityBounds result{32767.f, 32767.f, -32767.f, -32767.f};
    for (const auto& vertex : mesh.vertices) {
        result.minimum_x = std::min(result.minimum_x, vertex.position[0]);
        result.minimum_z = std::min(result.minimum_z, vertex.position[2]);
        result.maximum_x = std::max(result.maximum_x, vertex.position[0]);
        result.maximum_z = std::max(result.maximum_z, vertex.position[2]);
    }
    return result;
}

void expand(VisibilityBounds& target, const VisibilityBounds& source) {
    target.minimum_x = std::min(target.minimum_x, source.minimum_x);
    target.minimum_z = std::min(target.minimum_z, source.minimum_z);
    target.maximum_x = std::max(target.maximum_x, source.maximum_x);
    target.maximum_z = std::max(target.maximum_z, source.maximum_z);
}
} // namespace

std::uint16_t VisibilityData::sector(const std::array<std::int32_t, 3>& position) const {
    const auto [x, z] = grid_coordinates(*this, position);
    if (x < 0 || z < 0 || std::uint32_t(x) >= width || std::uint32_t(z) >= height) {
        return no_visibility_sector;
    }
    return std::uint16_t(std::uint32_t(z) * width + std::uint32_t(x));
}

RootSectorRegistration initial_root_registration(const VisibilityData& data, const std::array<float, 3>& local_position) {
    return {data.sector(coordinates(local_position, false))};
}

void update_root_registration(const VisibilityData& data, RootSectorRegistration& registration, const std::array<float, 3>& world_position, std::uint8_t manual_mask) {
    if (!manual_mask || registration.sector == no_visibility_sector) {
        return; //47f500 does not call47e3c0 for an unregistered/hidden root.
    }
    const auto sector = data.sector(coordinates(world_position, true));
    registration.neighbors.fill(no_visibility_sector);
    if (sector == no_visibility_sector) {
        registration.sector = sector;
        return;
    }
    const auto center = data.sectors.at(sector).center;
    const auto negative_x = coordinate(-world_position[0], false);
    const auto negative_z = coordinate(-world_position[2], false);
    constexpr auto margin = 0x48000;
    std::int32_t x_offset = 0;
    std::size_t next = 0;
    if (sub(-margin, negative_x) < sub(center[0], data.half_size)) {
        x_offset = -1;
    } else if (add(center[0], data.half_size) < sub(margin, negative_x)) {
        x_offset = 1;
    }
    if (x_offset != 0) {
        registration.neighbors[next++] = std::uint16_t(sector + x_offset);
    }
    std::int32_t z_offset = 0;
    if (add(center[1], data.half_size) < sub(margin, negative_z)) {
        z_offset = -std::int32_t(data.width);
    } else if (sub(center[1], data.half_size) > sub(-margin, negative_z)) {
        z_offset = std::int32_t(data.width);
    }
    if (z_offset != 0) {
        registration.neighbors[next++] = std::uint16_t(sector + z_offset);
        if (x_offset != 0) {
            registration.neighbors[next] = std::uint16_t(sector + z_offset + x_offset);
        }
    }
    registration.sector = sector;
}

VisibilityCamera visibility_camera(const std::array<float, 16>& matrix, const Projection& projection) {
    const std::array<float, 9> inverse{matrix[10] * matrix[5] - matrix[9] * matrix[6], matrix[9] * matrix[2] - matrix[10] * matrix[1],
        matrix[6] * matrix[1] - matrix[5] * matrix[2], matrix[8] * matrix[6] - matrix[4] * matrix[10], matrix[0] * matrix[10] - matrix[8] * matrix[2],
        matrix[4] * matrix[2] - matrix[0] * matrix[6], matrix[4] * matrix[9] - matrix[8] * matrix[5], matrix[8] * matrix[1] - matrix[0] * matrix[9],
        matrix[0] * matrix[5] - matrix[4] * matrix[1]};
    const auto determinant = matrix[0] * inverse[0] + matrix[4] * inverse[1] + matrix[8] * inverse[2];
    if (!std::isfinite(determinant) || determinant == 0) {
        throw std::runtime_error("Visibility requires an invertible camera basis");
    }
    VisibilityCamera result{matrix, {}, projection};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.world_position[axis] = float((-double(matrix[12]) * inverse[axis] - double(matrix[13]) * inverse[3 + axis] - double(matrix[14]) * inverse[6 + axis]) / determinant);
    }
    return result;
}

bool visibility_bounds_in_view(const VisibilityBounds& bounds, const VisibilityCamera& camera) {
    const auto& m = camera.world_to_view;
    // The stored combined projection matrix rounds each component to float.
    const float xx = m[0] * camera.projection.x, zx = m[8] * camera.projection.x, tx = m[12] * camera.projection.x;
    const std::array<std::array<float, 2>, 4> corners{
        {{bounds.minimum_x, bounds.minimum_z}, {bounds.maximum_x, bounds.minimum_z}, {bounds.maximum_x, bounds.maximum_z}, {bounds.minimum_x, bounds.maximum_z}}};
    std::array<float, 4> x{}, w{};
    for (std::size_t i = 0; i < 4; ++i) {
        // 47cf40 keeps the products/sums in x87, then stores each clip value.
        w[i] = float(double(m[2]) * corners[i][0] + double(m[10]) * corners[i][1] + m[14]);
        x[i] = float(double(xx) * corners[i][0] + double(zx) * corners[i][1] + tx);
        if (w[i] > 0 && -w[i] < x[i] && x[i] < w[i]) {
            return true;
        }
    }
    for (std::size_t edge = 0; edge < 4; ++edge) {
        // The closing edge is tested first->last, preserving exact-wall ties.
        const auto i = edge == 3 ? 0 : edge;
        const auto j = edge == 3 ? 3 : edge + 1;
        const bool left_cross = x[i] < -w[i] ? x[j] > -w[j] : x[j] < -w[j];
        const bool right_cross = x[i] < w[i] ? x[j] > w[j] : x[j] < w[j];
        if (left_cross && right_cross) {
            return true;
        }
    }
    return false;
}

std::uint8_t visibility_root_limit(std::uint32_t id_flags) {
    return id_flags & 0x04000000u ? 1 : id_flags & 0x02000000u ? 2 : id_flags & 0x01000000u ? 4 : 3;
}

bool VisibilityFrame::sector_visible(std::uint16_t sector) const {
    return sector != no_visibility_sector && (selected.empty() || (sector < selected.size() && (all_sectors || ordered_sectors.empty() || selected[sector] != 0)));
}

std::uint8_t VisibilityFrame::root_mask(std::uint32_t id) const {
    return root_masks.at(id);
}

Visibility::Visibility(const Terrain& ground) {
    const auto& centers = ground.sector_centers;
    if (centers.empty()) {
        return; // 489069 also has an explicit scene-without-GROUND-grid path.
    }
    if (centers.size() < 2 || centers.size() > 2048) {
        throw std::runtime_error("Visibility requires the original2..2048 sector grid");
    }
    source_->data.width = 1;
    while (source_->data.width < centers.size() && centers[source_->data.width][1] == centers.front()[1]) {
        ++source_->data.width;
    }
    if (centers.size() % source_->data.width != 0) {
        throw std::runtime_error("Visibility requires complete original sector rows");
    }
    source_->data.height = std::uint32_t(centers.size() / source_->data.width);
    source_->data.minimum_x = centers.front()[0];
    source_->data.maximum_z = centers.front()[1];
    auto spacing = std::int32_t(0x7fff0000);
    for (std::size_t i = 0; i < centers.size(); ++i) {
        source_->data.minimum_x = std::min(source_->data.minimum_x, centers[i][0]);
        source_->data.maximum_z = std::max(source_->data.maximum_z, centers[i][1]);
        if (i + 1 < centers.size() && centers[i][1] == centers[i + 1][1]) {
            spacing = std::min(spacing, simulation::fixed::abs(sub(centers[i + 1][0], centers[i][0])));
        }
    }
    if (spacing != 64 * one) {
        throw std::runtime_error("Visibility shift22 requires original64-unit sectors");
    }
    source_->data.half_size = spacing / 2;
    for (const auto& center : centers) {
        VisibilitySector sector;
        sector.center = center;
        sector.bounds = {float(sub(center[0], source_->data.half_size)) / one, float(sub(center[1], source_->data.half_size)) / one,
            float(add(center[0], source_->data.half_size)) / one, float(add(center[1], source_->data.half_size)) / one};
        source_->data.sectors.push_back(sector);
    }
    for (std::size_t i = 0; i < ground.chunks.size(); ++i) {
        const auto& chunk = ground.chunks[i];
        if (chunk.node_index == 0) {
            continue; // 4962e0's root has no sector; last variant wins below.
        }
        auto& sector = source_->data.sectors.at(chunk.node_index - 1);
        sector.ground_chunk = std::uint32_t(i);
        sector.bounds = bounds(chunk.mesh);
    }
    frame_.detail.resize(centers.size());
    frame_.selected.resize(centers.size());
    sector_roots_.resize(centers.size());
}

Visibility::Source& Visibility::writable_source() {
    if (!source_.unique()) {
        source_ = std::make_shared<Source>(*source_);
    }
    return *source_;
}

const VisibilityData& Visibility::data() const {
    return source_->data;
}

std::vector<std::uint32_t> Visibility::register_static(const Terrain& baked) {
    writable_source();
    std::vector<std::uint32_t> result(baked.chunks.size(), no_visibility_entry);
    for (std::size_t i = 0; i < baked.chunks.size(); ++i) {
        const auto& chunk = baked.chunks[i];
        if (i + 1 < baked.chunks.size() && baked.chunks[i + 1].node_index == chunk.node_index) {
            continue; // 494a40 overwrites one allocated entry's mesh per variant.
        }
        const auto id = source_->static_count++;
        result[i] = id;
        const auto sector_index = source_->data.sector({chunk.origin_xz[0], 0, chunk.origin_xz[1]});
        if (sector_index != no_visibility_sector) {
            auto& sector = source_->data.sectors[sector_index];
            sector.static_entries.insert(sector.static_entries.begin(), id);
            if (sector.ground_chunk != no_visibility_entry) {
                expand(sector.bounds, bounds(chunk.mesh));
            }
        }
    }
    return result;
}

std::uint32_t Visibility::register_root(const std::array<float, 3>& local_position, std::uint32_t id_flags, std::uint8_t manual_mask) {
    const auto id = std::uint32_t(roots_.size());
    const auto registration = initial_root_registration(source_->data, local_position);
    const auto sector = registration.sector;
    writable_source().root_limits.push_back(visibility_root_limit(id_flags));
    roots_.push_back({sector, manual_mask, registration.neighbors});
    if (sector != no_visibility_sector) {
        sector_roots_[sector].push_back(id);
    }
    return id;
}

void Visibility::initialize_roots(std::span<const std::array<float, 3>> local_positions) {
    if (local_positions.size() != roots_.size()) {
        throw std::runtime_error("Visibility initial positions must match registered root IDs");
    }
    for (auto& list : sector_roots_) {
        list.clear();
    }
    for (std::size_t id = 0; id < roots_.size(); ++id) {
        auto& root = roots_[id];
        const auto registration = initial_root_registration(source_->data, local_positions[id]);
        root.sector = registration.sector;
        root.neighbors = registration.neighbors;
        if (root.sector != no_visibility_sector) {
            sector_roots_[root.sector].push_back(std::uint32_t(id));
        }
    }
}

void Visibility::update_root(std::uint32_t id, const std::array<float, 3>& world_position, std::uint8_t manual_mask) {
    auto& root = roots_.at(id);
    root.manual_mask = manual_mask;
    if (!manual_mask || root.sector == no_visibility_sector) {
        return;
    }
    migrate_root(id, world_position);
}

void Visibility::set_root_mask(std::uint32_t id, std::uint8_t manual_mask) {
    roots_.at(id).manual_mask = manual_mask;
}

void Visibility::migrate_root(std::uint32_t id, const std::array<float, 3>& world_position) {
    auto& root = roots_.at(id);
    RootSectorRegistration registration{root.sector, root.neighbors};
    update_root_registration(source_->data, registration, world_position, 1);
    const auto sector = registration.sector;
    root.neighbors = registration.neighbors;
    if (sector != root.sector) {
        auto& old = sector_roots_[root.sector];
        old.erase(std::find(old.begin(), old.end(), id));
        if (sector != no_visibility_sector) {
            sector_roots_[sector].push_back(id);
        }
        root.sector = sector;
    }
}

std::uint16_t Visibility::root_sector(std::uint32_t id) const {
    return roots_.at(id).sector;
}

RootSectorRegistration Visibility::root_registration(std::uint32_t id) const {
    const auto& root = roots_.at(id);
    return {root.sector, root.neighbors};
}

std::uint8_t Visibility::root_manual_mask(std::uint32_t id) const {
    return roots_.at(id).manual_mask;
}

const VisibilityFrame& Visibility::select(const VisibilityCamera& camera, std::span<const std::uint16_t> camera_ancestry_sectors, bool all_sectors) {
    frame_.ordered_sectors.clear();
    frame_.terrain_sectors.clear();
    frame_.draws.clear();
    std::ranges::fill(frame_.selected, 0);
    frame_.all_sectors = all_sectors;
    if (source_->data.sectors.empty()) {
        // 489069's no-grid branch uses static allocation order, followed by
        // actor allocation order, without sector selection or root LOD.
        for (std::uint32_t id = 0; id < source_->static_count; ++id) {
            frame_.draws.push_back({id, no_visibility_sector, false});
        }
        frame_.root_masks.resize(roots_.size());
        for (std::size_t id = 0; id < roots_.size(); ++id) {
            frame_.root_masks[id] = roots_[id].manual_mask;
            if (roots_[id].manual_mask & camera.view_mask) {
                frame_.draws.push_back({std::uint32_t(id), no_visibility_sector, true});
            }
        }
        return frame_;
    }
    const auto append = [&](std::uint16_t sector) {
        if (sector < source_->data.sectors.size() && !frame_.selected[sector]) {
            frame_.selected[sector] = 1;
            frame_.ordered_sectors.push_back(sector);
        }
    };
    if (all_sectors) {
        for (std::size_t i = 0; i < source_->data.sectors.size(); ++i) {
            append(std::uint16_t(i));
            frame_.detail[i] = 0;
        }
    } else {
        const auto position = coordinates(camera.world_position, true);
        const auto detail_position = coordinates(camera.world_position, false);
        const auto camera_x = sub(detail_position[0], source_->data.minimum_x) / 64;
        const auto camera_z = sub(source_->data.maximum_z, detail_position[2]) / 64;
        const auto range = std::int32_t((camera.projection.far_plane + 150.f) / 64.f) * one;
        // Preserve the original short-object distances through Very High.
        // Extended projection distances also expose small props earlier;
        // otherwise their fixed2/3-sector limits would still cause pop-in.
        const auto short_scale = std::max(1.f, camera.projection.far_plane / 250.f);
        const std::array<std::int32_t, 4> limits{std::int32_t(2 * one * short_scale), std::int32_t(3 * one * short_scale), range - one, range};
        auto [column, row] = grid_coordinates(source_->data, position);
        int direction = 0, leg_progress = 0, leg_length = 1, failures = 2048, failure_budget = 9;
        for (std::size_t visited = 0; visited < source_->data.sectors.size() && frame_.ordered_sectors.size() < 2048 && failures > 0; ++visited) {
            if (column >= 0 && row >= 0 && std::uint32_t(column) < source_->data.width && std::uint32_t(row) < source_->data.height) {
                const auto index = std::uint16_t(std::uint32_t(row) * source_->data.width + std::uint32_t(column));
                const auto& sector = source_->data.sectors[index];
                if (!visibility_bounds_in_view(sector.bounds, camera)) {
                    --failures;
                } else {
                    const auto x = sub(sub(sector.center[0], source_->data.minimum_x) / 64, camera_x);
                    const auto z = sub(sub(source_->data.maximum_z, sector.center[1]) / 64, camera_z);
                    // 47d8f2 stores raw integer bits despite the decompiler's
                    // inferred float local; both multiplies are integer IMUL.
                    const auto squared = add(mul(x, x), mul(z, z));
                    std::uint8_t detail = 0;
                    while (detail < limits.size() && squared >= mul(limits[detail], limits[detail])) {
                        ++detail;
                    }
                    frame_.detail[index] = detail;
                    append(index);
                    failures = failure_budget;
                }
            }
            switch (direction) {
                case 0:
                    --row;
                    break;
                case 1:
                    ++column;
                    break;
                case 2:
                    ++row;
                    break;
                default:
                    --column;
                    break;
            }
            if (++leg_progress == leg_length) {
                direction = (direction + 1) & 3;
                if (direction == 0 || direction == 2) {
                    ++leg_length;
                    failure_budget += 5;
                }
                leg_progress = 0;
            }
        }
        for (const auto sector : camera_ancestry_sectors) {
            append(sector); // 47dd90 deliberately does not change detail.
        }
    }
    refresh_roots(camera.view_mask);
    return frame_;
}

void Visibility::refresh_roots(std::uint8_t view_mask) {
    frame_.draws.clear();
    frame_.terrain_sectors.clear();
    frame_.root_masks.resize(roots_.size());
    if (source_->data.sectors.empty()) {
        for (std::uint32_t id = 0; id < source_->static_count; ++id) {
            frame_.draws.push_back({id, no_visibility_sector, false});
        }
        for (std::size_t id = 0; id < roots_.size(); ++id) {
            frame_.root_masks[id] = roots_[id].manual_mask;
            if (roots_[id].manual_mask & view_mask) {
                frame_.draws.push_back({std::uint32_t(id), no_visibility_sector, true});
            }
        }
        return;
    }
    for (std::size_t i = 0; i < roots_.size(); ++i) {
        const auto& root = roots_[i];
        frame_.root_masks[i] = root.sector == no_visibility_sector || frame_.sector_visible(root.sector) ? root.manual_mask : 0;
    }
    for (const auto index : frame_.ordered_sectors) {
        const auto& sector = source_->data.sectors[index];
        if (sector.ground_chunk != no_visibility_entry && frame_.detail[index] < 3) {
            frame_.terrain_sectors.push_back(index);
        }
        for (const auto id : sector.static_entries) {
            frame_.draws.push_back({id, index, false});
        }
        for (const auto id : sector_roots_[index]) {
            if ((frame_.root_masks[id] & view_mask) && frame_.detail[index] < source_->root_limits[id]) {
                frame_.draws.push_back({id, index, true});
            }
        }
    }
}

const VisibilityFrame& Visibility::frame() const {
    return frame_;
}
} // namespace toca::scene
