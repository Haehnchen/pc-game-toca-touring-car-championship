#include "formats/hpc.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using Vector = std::array<std::int32_t, 3>;
constexpr std::int32_t q = 65536;
constexpr std::array<std::array<std::int32_t, 2>, 5> points{{{0, 0}, {-123456, 234567}, {-16 * q, -16 * q}, {40 * q, 20 * q}, {-128 * q, -128 * q}}};

struct PlaneResult {
    bool valid;
    Vector normal;
    std::array<std::int32_t, points.size()> heights;
};

struct Case {
    std::array<Vector, 3> vertices;
    PlaneResult ground, prop;
};

// Synthetic outputs captured before immutable-plane caching. Ground ratios
// and edge-normalized prop planes intentionally have different rounding.
constexpr std::array<Case, 10> cases{{
    {{{{-16 * q, 2 * q, -16 * q}, {-16 * q, 2 * q, 16 * q}, {16 * q, 2 * q, -16 * q}}}, {true, {0, 65536, 0}, {131072, 131072, 131072, 131072, 131072}},
        {true, {0, 65532, 0}, {131072, 131072, 131072, 131072, 131072}}},
    {{{{-16 * q, -q, -16 * q}, {-16 * q, 5 * q, 16 * q}, {16 * q, 3 * q, -16 * q}}}, {true, {-8192, 65536, -12288}, {262144, 290694, -65536, 835584, -2359296}},
        {true, {-7992, 63931, -11987}, {262153, 290701, -65536, 835617, -2359360}}},
    {{{{-16 * q, 7 * q, -16 * q}, {-16 * q, -5 * q, 16 * q}, {16 * q, -3 * q, -16 * q}}}, {true, {20480, 65536, 24576}, {-262144, -311526, 458752, -1572864, 5505024}},
        {true, {18404, 58892, 22085}, {-262157, -311539, 458752, -1572899, 5505117}}},
    {{{{-16 * q, -q, -16 * q}, {16 * q, 3 * q, -16 * q}, {-16 * q, 5 * q, 16 * q}}}, {true, {-8192, 65536, -12288}, {262144, 290694, -65536, 835584, -2359296}},
        {true, {7991, -63932, 11986}, {262115, 290659, -65536, 835509, -2359095}}},
    {{{{0, 0, -q}, {0, 2 * q, 0}, {0, 0, q}}}, {}, {}},
    {{{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}}}, {}, {}},
    {{{{INT32_MAX - 100, 123456, -q}, {INT32_MIN + 100, -123456, q}, {1000, 34567, 12 * q}}},
        {true, {483479956, 65536, 2016240704}, {814948202, -1195855859, -2139194198, 1280478026, -1343354518}},
        {true, {-22, -30668, -57772}, {1540521, 1098732, 3516567, -930477, 17348895}}},
    {{{{-100 * q, 100 * q, -100 * q}, {-100 * q, -100 * q, 100 * q}, {100 * q, 50 * q, -100 * q}}}, {true, {-20000, 65536, 51072}, {3446400, 3225928, 3943552, 3224960, 7423616}},
        {true, {11409, 45638, 45639}, {-1638471, -1842179, -327740, -3604551, 8847380}}},
    {{{{0, 0, 0}, {0, q, 128 * q}, {512 * q, 2 * q, 0}}}, {}, {true, {-256, 65394, -511}, {0, 1349, -12298, 20504, -98389}}},
    {{{{0, 0, 0}, {0, q, 1}, {q, 0, 0}}}, {true, {0, 65536, 0}, {0, 0, 0, 0, 0}}, {}},
}};

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Query> void expect_error(Query query, std::string_view message) {
    try {
        (void)query();
    } catch (const std::runtime_error& error) {
        expect(error.what() == message, "HPC query error kind or precedence changed");
        return;
    }
    throw std::runtime_error("HPC query accepted an invalid cached index or height plane");
}

std::vector<std::uint8_t> fixture(const std::array<Vector, 3>& vertices) {
    std::vector<std::uint8_t> bytes;
    const auto half = [&](std::uint16_t value) {
        bytes.push_back(std::uint8_t(value));
        bytes.push_back(std::uint8_t(value >> 8));
    };
    const auto word = [&](std::int32_t value) {
        half(std::uint16_t(value));
        half(std::uint16_t(std::uint32_t(value) >> 16));
    };
    word(-128 * q);
    word(-128 * q);
    // Five cell counts/alignment, one leaf, then three owned vertices.
    for (const auto value : {1, 0, 0, 0, 0, 0, 1, 0, 3, 0}) {
        half(std::uint16_t(value));
    }
    for (const auto& vertex : vertices) {
        for (const auto component : vertex) {
            word(component);
        }
    }
    // One face, flags 0x83, one leaf index and a 1x1 root rectangle.
    for (const auto value : {1, 0, 0, 1, 2, 0x83, 1, 0, 1, 1}) {
        half(std::uint16_t(value));
    }
    return bytes;
}

template <class Query, class Normal> void check_sample(Query query, Normal normal, const PlaneResult& expected, std::size_t point, const char* invalid_plane) {
    if (!expected.valid) {
        expect_error(query, invalid_plane);
        return;
    }
    const auto result = query();
    expect(result && result->height == expected.heights[point] && normal(*result) == expected.normal && result->triangle == 0 && result->flags == 0x83,
        "HPC height, plane normal, face identity or flags changed from the source query");
}

void check() {
    std::vector<toca::HpcSurface> surfaces;
    for (const auto& test : cases) {
        // Even vertical, degenerate and independently invalid planes load.
        // Their errors belong to the selected query, never to construction.
        surfaces.emplace_back(fixture(test.vertices));
        expect(surfaces.back().triangle_count() == 1, "HPC decoded face ownership changed");
    }
    // Alternating surface order detects any accidentally shared plane cache;
    // each instance owns its vertices after its fixture bytes have gone away.
    for (unsigned pass = 0; pass < 3; ++pass) {
        for (std::size_t step = 0; step < cases.size(); ++step) {
            const auto index = pass % 2 ? cases.size() - 1 - step : step;
            const auto& surface = surfaces[index];
            for (std::size_t point = 0; point < points.size(); ++point) {
                const auto [x, z] = points[point];
                check_sample(
                    [&] {
                        return surface.sample_retaining_cached(x, z, 0);
                    },
                    [](const auto& value) {
                        return value.normal_ratio;
                    },
                    cases[index].ground, point, "HPC: selected triangle has no height plane");
                check_sample(
                    [&] {
                        return surface.prop_sample_retaining_cached(x, z, 0);
                    },
                    [](const auto& value) {
                        return value.normal;
                    },
                    cases[index].prop, point, "HPC: selected prop triangle has no height plane");
            }
            expect(!surface.sample_retaining_cached(128 * q, 0, 0) && !surface.prop_sample_retaining_cached(128 * q, 0, 0) && !surface.sample_retaining_cached(-128 * q - 1, 0, 0)
                       && !surface.prop_sample_retaining_cached(-128 * q - 1, 0, 0),
                "Outside-root rejection must precede deferred ground/prop plane errors");
            expect_error(
                [&] {
                    return surface.sample_retaining_cached(128 * q, 0, 1);
                },
                "HPC: cached triangle outside face table");
            expect_error(
                [&] {
                    return surface.prop_sample_retaining_cached(128 * q, 0, 1);
                },
                "HPC: cached prop triangle outside face table");
        }
    }
    const auto& flat = surfaces.front();
    expect(!flat.sample(40 * q, 20 * q, 0) && !flat.prop_sample(40 * q, 20 * q, 0) && !flat.sample_retaining_cached(40 * q, 20 * q, std::nullopt)
               && !flat.prop_sample_retaining_cached(40 * q, 20 * q, std::nullopt),
        "Only explicit retained-cache queries may extrapolate outside a triangle");
    const auto ground = flat.sample(0, 0);
    const auto prop = flat.prop_sample(0, 0);
    expect(ground && prop && ground->height == 2 * q && prop->height == 2 * q, "Ordinary containing-face queries must select the same immutable planes");
}
} // namespace

int main() {
    try {
        check();
        std::cout << "PASS HPC source plane goldens, independent validity, ownership and query precedence\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
