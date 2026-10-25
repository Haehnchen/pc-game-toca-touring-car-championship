#include "assets/store.hpp"
#include "formats/hpc.hpp"
#include "scene/placement.hpp"

#include <bit>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::int32_t unit = 65536;

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char* message) {
    bool rejected = false;
    try {
        function();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    expect(rejected, message);
}

void put16(Bytes& bytes, std::size_t position, std::uint16_t value) {
    bytes.at(position) = std::uint8_t(value);
    bytes.at(position + 1) = std::uint8_t(value >> 8);
}

void put32(Bytes& bytes, std::size_t position, std::int32_t value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    put16(bytes, position, std::uint16_t(bits));
    put16(bytes, position + 2, std::uint16_t(bits >> 16));
}

Bytes hpc() {
    // Five levels, two overlapping planes. Leaf source order is face 1, then 0.
    Bytes bytes(386);
    put16(bytes, 8, 1);
    for (std::size_t level = 1; level < 5; ++level) {
        put16(bytes, 8 + 2 * level, 16);
    }
    put16(bytes, 20, 0xffff);
    for (std::size_t level = 1; level < 4; ++level) {
        put16(bytes, 24 + (level - 1) * 64, 0xffff);
    }
    for (std::size_t index = 0; index < 16; ++index) {
        put16(bytes, 216 + index * 4, 2);
    }
    put16(bytes, 280, 6);
    for (std::size_t face = 0; face < 2; ++face) {
        const std::array<std::array<std::int32_t, 3>, 3> vertices{
            {{0, (2 + std::int32_t(face)) * unit, 0}, {0, (1 + std::int32_t(face)) * unit, 4 * unit}, {4 * unit, (4 + std::int32_t(face)) * unit, 0}}};
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            for (std::size_t component = 0; component < 3; ++component) {
                put32(bytes, 284 + (face * 3 + vertex) * 12 + component * 4, vertices[vertex][component]);
            }
        }
    }
    put16(bytes, 356, 2);
    for (std::size_t face = 0; face < 2; ++face) {
        for (std::size_t vertex = 0; vertex < 3; ++vertex) {
            put16(bytes, 360 + face * 8 + vertex * 2, std::uint16_t(face * 3 + vertex));
        }
        put16(bytes, 366 + face * 8, std::uint16_t(face == 0 ? 0 : 0x83));
    }
    put16(bytes, 376, 2);
    put16(bytes, 378, 1);
    put16(bytes, 380, 0);
    put16(bytes, 382, 1);
    put16(bytes, 384, 1);
    return bytes;
}

void synthetic() {
    // Construction from a temporary proves decoded ownership after input death.
    const toca::HpcSurface surface(hpc());
    const auto sample = surface.sample(unit, unit);
    expect(sample && sample->triangle == 1 && sample->height == 13 * unit / 4 && sample->flags == 0x83, "HPC must descend all levels and retain first containing face order/flags");
    expect(sample->normal_ratio == std::array<std::int32_t, 3>{-unit / 2, unit, unit / 4}, "HPC plane ratios must retain their source sign and fixed scale");
    const auto cached = surface.sample(unit, unit, 0);
    expect(cached && cached->triangle == 0 && cached->height == 9 * unit / 4, "A cached containing face must win over tree order");
    const auto prop = surface.prop_sample(unit, unit);
    expect(prop && prop->triangle == 1 && prop->height == 212992 && prop->flags == 0x83 && prop->normal == std::array<std::int32_t, 3>{-28604, 57207, 14302},
        "Prop ground must normalize each edge before the cross product, then divide its own height plane");
    const auto cached_prop = surface.prop_sample(unit, unit, 0);
    expect(cached_prop && cached_prop->triangle == 0 && cached_prop->height == 147456 && cached_prop->normal == prop->normal,
        "Prop query must preserve the cached containing face and original normalized plane");
    expect(!surface.prop_sample(-1, 0) && !surface.prop_sample(8 * unit, 8 * unit), "Prop query must not invent ground outside the HPC tree or triangle");
    expect(surface.sample(0, 0)->height == 3 * unit, "Triangle edges and vertices are inclusive");
    expect(!surface.sample(3 * unit, 3 * unit) && !surface.sample(8 * unit, 8 * unit), "Outside triangles and empty cells must not invent a surface");
    expect(!surface.sample(-1, 0) && !surface.sample(256 * unit, 0), "Root bounds must reject negative and next-cell coordinates");
    const auto retained = surface.sample_retaining_cached(8 * unit, 8 * unit, 0);
    const auto other_actor = surface.sample_retaining_cached(8 * unit, 8 * unit, 1);
    const auto retained_prop = surface.prop_sample_retaining_cached(8 * unit, 8 * unit, 0);
    expect(retained && retained->triangle == 0 && retained->height == 4 * unit && other_actor && other_actor->triangle == 1 && other_actor->height == 5 * unit,
        "42e6f0 must retain each actor's explicitly supplied cached plane when42eed0 finds no containing face");
    expect(retained_prop && retained_prop->triangle == 0 && retained_prop->height == 262146,
        "42e9f0 must extrapolate its actor-owned retained prop plane through the same source plane arithmetic");
    expect(!surface.sample_retaining_cached(8 * unit, 8 * unit, std::nullopt) && !surface.prop_sample_retaining_cached(8 * unit, 8 * unit, std::nullopt),
        "A query without a previous valid actor face must not manufacture a retained surface");
    expect(!surface.sample_retaining_cached(-1, 0, 0) && !surface.sample_retaining_cached(256 * unit, 0, 0) && !surface.prop_sample_retaining_cached(-1, 0, 0)
               && !surface.prop_sample_retaining_cached(256 * unit, 0, 0),
        "Retained actor planes must preserve the native boundary outside the bounded HPC root rectangle");
    expect(!surface.sample(8 * unit, 8 * unit, 0) && !surface.prop_sample(8 * unit, 8 * unit, 0),
        "Strict placement queries must retain their containing-face contract after actor-cache queries");
    rejects(
        [&] {
            (void)surface.sample(unit, unit, 2);
        },
        "Invalid cached face must not index outside owned data");
    rejects(
        [&] {
            (void)surface.prop_sample(unit, unit, 2);
        },
        "Invalid cached prop face must not index outside owned data");
    for (const auto position : {8u, 22u, 216u, 360u, 378u, 382u}) {
        auto broken = hpc();
        put16(broken, position, position == 216 ? 0xffff : 0x7fff);
        rejects(
            [&] {
                (void)toca::HpcSurface(broken);
            },
            "Invalid HPC level, branch, vertex, leaf or root bounds accepted");
    }
    auto truncated = hpc();
    truncated.pop_back();
    rejects(
        [&] {
            (void)toca::HpcSurface(truncated);
        },
        "Truncated HPC footer accepted");
    auto vertical = hpc();
    put32(vertical, 284 + 2 * 12, 0);
    rejects(
        [&] {
            (void)toca::HpcSurface(vertical).sample(0, unit, 0);
        },
        "A selected vertical plane must reject a zero height divisor");

    std::array<toca::scene::GridSlot, 32> grid{};
    grid[15] = {{0, 0}, {0, 2 * unit}, -321, 0};
    const auto race = toca::scene::single_race_start(grid);
    // sqrt's midpoint table makes the normalized component 65522, not 65536.
    expect(race.xz == std::array<std::int32_t, 2>{163805, unit} && race.heading == -321 && race.source_index == 15,
        "Race placement must use last grid rank, midpoint, source lookup rounding and stored heading");
    rejects(
        [&] {
            (void)toca::scene::race_grid_start(grid, 16);
        },
        "Runtime qualifying records are not geometric race grid slots");

    Bytes route(4 + 18 * 64 + 9); // Unconsumed actor suffix is permitted.
    put32(route, 0, 18);
    put32(route, 4 + 12, 3 * unit);
    put32(route, 4 + 20, -2 * unit);
    put32(route, 4 + 64 + 12, 4 * unit);
    put32(route, 4 + 64 + 20, -2 * unit);
    const auto qualifying = toca::scene::single_qualifying_start(route);
    expect(qualifying.xz == std::array<std::int32_t, 2>{3 * unit, -2 * unit} && qualifying.heading == 1 && qualifying.source_index == 0,
        "Qualifying must use count-18, the next 3D route direction and original lookup heading rounding");
    put32(route, 0, 19);
    rejects(
        [&] {
            (void)toca::scene::single_qualifying_start(route);
        },
        "POINTS count outside record prefix accepted");
    put32(route, 0, 17);
    rejects(
        [&] {
            (void)toca::scene::single_qualifying_start(route);
        },
        "Short route must not underflow the qualifying index");
}

void canonical(const std::filesystem::path& root) {
    const toca::AssetStore store(root);
    const auto keep = store.open_archive("DATA/DONLKEEP.BFP");
    const auto free = store.open_archive("DATA/DONLFREE.BFP");
    const toca::HpcSurface surface(keep.member(0).bytes());
    expect(surface.triangle_count() == 5215, "Canonical Donington collision face count changed");
    const auto grid = toca::scene::decode_start_grid(keep.member(3).bytes());
    const auto race = toca::scene::single_race_start(grid);
    expect(race.xz == std::array<std::int32_t, 2>{-9642299, -5298024} && race.heading == 2161 && race.source_index == 15,
        "Donington no-qualifying single-player race placement changed");
    const auto front = toca::scene::race_grid_start(grid, 0);
    expect(front.xz == std::array<std::int32_t, 2>{-16680686, -3591021} && front.heading == 2139, "Donington geometric front grid placement changed");
    const auto race_height = surface.sample(race.xz[0], race.xz[1]);
    expect(race_height && race_height->triangle == 3268 && race_height->height == 335053 && race_height->flags == 0
               && race_height->normal_ratio == std::array<std::int32_t, 3>{-480, unit, -2849},
        "Donington race surface must use first tree match and original per-product plane rounding");
    const auto front_height = surface.sample(front.xz[0], front.xz[1]);
    expect(front_height && front_height->triangle == 2656 && front_height->height == 276643, "Donington front grid surface changed");
    for (const auto& member : {keep.member(1), free.member(0)}) {
        const auto qualifying = toca::scene::single_qualifying_start(member.bytes());
        expect(qualifying.xz == std::array<std::int32_t, 2>{-7749269, -9039511} && qualifying.heading == -376 && qualifying.source_index == 236,
            "Donington POINTS/LOWPOINT qualifying start must follow original local slot permutation");
        const auto height = surface.sample(qualifying.xz[0], qualifying.xz[1]);
        expect(height && height->triangle == 3888 && height->height == 509805 && height->normal_ratio == std::array<std::int32_t, 3>{1347, unit, -1604},
            "Donington qualifying surface must come from HPC, not route or visible mesh Y");
    }

    struct TrackStartFixture {
        const char* archive_prefix;
        std::uint32_t qualifying_index;
    };

    for (const auto& fixture : {TrackStartFixture{"data/silv", 194}, TrackStartFixture{"data/thru", 210}, TrackStartFixture{"data/bran", 130}, TrackStartFixture{"data/oult", 140},
             TrackStartFixture{"data/dons", 183}, TrackStartFixture{"data/crof", 210}, TrackStartFixture{"data/knoc", 122}, TrackStartFixture{"data/snet", 182},
             TrackStartFixture{"data/land", 127}}) {
        const auto prefix = std::string(fixture.archive_prefix);
        const auto track_keep = store.open_archive(prefix + "keep.bfp");
        const auto track_free = store.open_archive(prefix + "free.bfp");
        const toca::HpcSurface track_surface(track_keep.member(0).bytes());
        const auto track_grid = toca::scene::decode_start_grid(track_keep.member(3).bytes());
        for (std::uint32_t rank = 0; rank < 16; ++rank) {
            const auto start = toca::scene::race_grid_start(track_grid, rank);
            const auto height = track_surface.sample(start.xz[0], start.xz[1]);
            expect(height.has_value(), "Every track geometric grid slot must lie on original HPC ground");
            if (rank == 0 || rank == 15) {
                std::cout << prefix << " grid " << rank << " XZ/heading/height " << start.xz[0] << ' ' << start.xz[1] << ' ' << start.heading << ' ' << height->height << '\n';
            }
        }
        for (const auto& member : {track_keep.member(1), track_free.member(0)}) {
            const auto start = toca::scene::single_qualifying_start(member.bytes());
            const auto height = track_surface.sample(start.xz[0], start.xz[1]);
            expect(start.source_index == fixture.qualifying_index && height.has_value(), "Track POINTS/LOWPOINT qualifying must select count-18 on actual HPC ground");
            std::cout << prefix << " qualifying XZ/heading/height " << start.xz[0] << ' ' << start.xz[1] << ' ' << start.heading << ' ' << height->height << '\n';
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else {
            throw std::runtime_error("Usage: scene-placement-tests [canonical-root]");
        }
        std::cout << "PASS: owning HPC tree/plane queries and source qualifying/race horizontal placement\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
