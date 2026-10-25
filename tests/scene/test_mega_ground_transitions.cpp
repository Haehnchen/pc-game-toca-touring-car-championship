#include "formats/terrain.hpp"
#include "scene/mega_ground_transitions.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace {
using namespace toca;
using namespace toca::scene;

void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::array<float, 2> original_uv(const MegaGroundTransitionVertex& vertex, const std::array<ModelVertex, 3>& source) {
    std::array<float, 2> result{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        result[0] += source[index].u * vertex.source_weights[index];
        result[1] += source[index].v * vertex.source_weights[index];
    }
    return result;
}

float area(const std::array<std::array<float, 2>, 3>& points) {
    return .5f * std::abs((points[1][0] - points[0][0]) * (points[2][1] - points[0][1]) - (points[1][1] - points[0][1]) * (points[2][0] - points[0][0]));
}

void check_face(const std::array<ModelVertex, 3>& source, const MegaGroundTransition& result) {
    expect(!result.gravel.empty() && result.gravel.size() <= 120, "Transition tessellation must remain bounded per original face");
    for (std::size_t index = 0; index < source.size(); ++index) {
        expect(result.grass[index].position == source[index].position && result.grass[index].opacity == 255, "Opaque base must retain the source face and exact positions");
    }
    float overlay_area = 0;
    bool mixed = false;
    for (const auto& triangle : result.gravel) {
        std::array<std::array<float, 2>, 3> uv;
        for (std::size_t corner = 0; corner < triangle.size(); ++corner) {
            const auto& vertex = triangle[corner];
            float weight_sum = 0;
            for (const auto weight : vertex.source_weights) {
                expect(std::isfinite(weight) && weight >= -.00001f && weight <= 1.00001f, "Overlay must stay inside its source face");
                weight_sum += weight;
            }
            expect(std::abs(weight_sum - 1) < .00001f, "Overlay interpolation must preserve the source plane");
            for (std::size_t axis = 0; axis < vertex.position.size(); ++axis) {
                float expected = 0;
                for (std::size_t index = 0; index < source.size(); ++index) {
                    expected += source[index].position[axis] * vertex.source_weights[index];
                }
                expect(std::isfinite(vertex.position[axis]) && std::abs(expected - vertex.position[axis]) < .00001f,
                    "Overlay must interpolate exact terrain height without a depth offset");
            }
            uv[corner] = original_uv(vertex, source);
            mixed |= vertex.opacity > 0 && vertex.opacity < 255;
        }
        overlay_area += area(uv);
        mixed |= std::min({triangle[0].opacity, triangle[1].opacity, triangle[2].opacity}) < std::max({triangle[0].opacity, triangle[1].opacity, triangle[2].opacity});
    }
    const std::array source_uv{std::array{source[0].u, source[0].v}, std::array{source[1].u, source[1].v}, std::array{source[2].u, source[2].v}};
    expect(overlay_area <= area(source_uv) * 1.0001f, "Clipped gravel coverage must not overlap or exceed the source face");
    expect(mixed || std::abs(overlay_area - area(source_uv)) < .00001f, "A face outside the feather must retain its full solid gravel coverage");
}

float sampled_opacity(const MegaGroundTransition& result, const std::array<ModelVertex, 3>& source, std::array<float, 2> point) {
    for (const auto& triangle : result.gravel) {
        const auto a = original_uv(triangle[0], source), b = original_uv(triangle[1], source), c = original_uv(triangle[2], source);
        const auto det = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        if (std::abs(det) < 1.e-10f) {
            continue;
        }
        const auto second = ((point[0] - a[0]) * (c[1] - a[1]) - (point[1] - a[1]) * (c[0] - a[0])) / det;
        const auto third = ((b[0] - a[0]) * (point[1] - a[1]) - (b[1] - a[1]) * (point[0] - a[0])) / det;
        const auto first = 1 - second - third;
        if (std::min({first, second, third}) >= -.0001f) {
            return (first * triangle[0].opacity + second * triangle[1].opacity + third * triangle[2].opacity) / 255.f;
        }
    }
    return 0;
}

void shoreline_quality(std::uint32_t track_id, float feather_half_width = .05f) {
    // Independently measured canonical source rows, before removing individual
    // pixel spikes. Verify the actual clipped coverage, not the control list.
    constexpr std::array<float, 64> oulton_columns{14, 14, 14, 15, 13, 12, 13, 12, 14, 13, 16, 12, 17, 15, 16, 11, 17, 14, 13, 13, 11, 10, 9, 10, 8, 8, 8, 8, 11, 15, 17, 12, 11,
        12, 13, 13, 13, 12, 12, 12, 11, 12, 13, 14, 14, 15, 15, 17, 11, 17, 16, 15, 11, 15, 13, 13, 13, 13, 14, 15, 15, 15, 15, 16};
    // Donington's independent RGB-segmentation measurements avoid treating
    // bright grass flecks as gravel when the two palettes have close luma.
    constexpr std::array<float, 64> donington_columns{11, 11, 11, 12, 11, 14, 14, 16, 16, 17, 17, 18, 17, 18, 18, 17, 17, 16, 16, 16, 16, 15, 15, 15, 13, 13, 14, 13, 12, 12, 12,
        11, 11, 11, 10, 9, 9, 9, 9, 9, 9, 10, 12, 15, 13, 13, 13, 14, 13, 15, 15, 17, 16, 17, 17, 17, 17, 17, 18, 17, 17, 15, 15, 15};
    // Independently segmented Silverstone GG source, CRC32 f351a508. The
    // three-row median removes the isolated8/10pixel fluctuations at its plateau.
    constexpr std::array<float, 64> silverstone_columns{11, 11, 11, 11, 11, 12, 13, 14, 16, 17, 17, 18, 17, 18, 18, 17, 17, 17, 17, 16, 16, 15, 15, 15, 13, 13, 13, 12, 12, 12, 11,
        11, 10, 10, 9, 10, 9, 9, 9, 9, 8, 9, 11, 13, 13, 14, 14, 15, 14, 16, 16, 17, 16, 17, 17, 17, 17, 17, 17, 17, 16, 15, 15, 14};
    constexpr std::array<float, 64> brands_columns{6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 9, 9, 10, 10, 10, 10, 10, 10, 9, 9, 9, 8, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        5, 4, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 6};
    // Raw Knockhill RGB endpoint-palette splits, CRC32 2890fd36. Retain the
    // isolated row31 dip instead of comparing against a smoothed copy.
    constexpr std::array<float, 64> knockhill_columns{16, 15, 15, 15, 18, 21, 21, 19, 19, 18, 21, 23, 21, 20, 19, 21, 22, 23, 21, 20, 12, 12, 15, 14, 14, 14, 14, 17, 17, 16, 18,
        12, 16, 16, 16, 15, 15, 15, 15, 15, 17, 15, 15, 15, 15, 16, 16, 15, 17, 18, 18, 18, 18, 19, 18, 18, 19, 18, 16, 17, 15, 15, 16, 17};
    // Independent raw Snetterton source rows, CRC32 fcea437a. Comparing every
    // row protects the sharp grass dip and the adjacent gravel excursion.
    constexpr std::array<float, 64> snetterton_columns{13, 15, 14, 14, 15, 14, 14, 14, 16, 15, 14, 15, 12, 16, 16, 14, 12, 14, 12, 12, 13, 12, 12, 12, 11, 10, 12, 13, 7, 12, 14,
        16, 14, 13, 15, 15, 16, 13, 17, 15, 18, 17, 15, 15, 15, 13, 11, 19, 19, 18, 18, 17, 16, 13, 10, 10, 9, 12, 10, 12, 11, 14, 15, 15};
    const auto& original_columns = track_id == 4   ? oulton_columns
                                   : track_id == 1 ? silverstone_columns
                                   : track_id == 3 ? brands_columns
                                   : track_id == 7 ? knockhill_columns
                                   : track_id == 8 ? snetterton_columns
                                                   : donington_columns;
    std::array<ModelVertex, 3> first{{{{0, 2, 0}, {0, 1, 0}, 0, 0}, {{0, 2, 10}, {0, 1, 0}, 0, 1}, {{10, 2, 10}, {0, 1, 0}, 1, 1}}};
    std::array<ModelVertex, 3> second{{first[0], first[2], {{10, 2, 0}, {0, 1, 0}, 1, 0}}};
    if (track_id == 3) {
        for (auto* triangle : {&first, &second}) {
            for (auto& vertex : *triangle) {
                std::swap(vertex.u, vertex.v);
            }
        }
    }
    const auto a = prepare_mega_ground_transition(track_id, first, feather_half_width), b = prepare_mega_ground_transition(track_id, second, feather_half_width);
    expect(a && b, "Both source UV halves must prepare for shoreline sampling");
    if (track_id == 7 || track_id == 8) {
        check_face(first, *a);
        check_face(second, *b);
    }
    float maximum_main_error = 0, maximum_raw_error = 0;
    for (std::size_t row = 0; row < original_columns.size(); ++row) {
        const auto v = (float(row) + .5f) / 64.f;
        if (track_id == 7 || track_id == 8) {
            const auto grass_alpha = v >= .02f ? sampled_opacity(*a, first, {.02f, v}) : sampled_opacity(*b, second, {.02f, v});
            const auto gravel_alpha = v >= .984985352f ? sampled_opacity(*a, first, {.984985352f, v}) : sampled_opacity(*b, second, {.984985352f, v});
            expect(grass_alpha == 0 && gravel_alpha > .999f, "Neutral source endpoints must stay entirely grass and gravel on every row");
        }
        float low = 0, high = 1;
        for (unsigned step = 0; step < 20; ++step) {
            const auto u = (low + high) * .5f;
            const std::array point = track_id == 3 ? std::array{v, u} : std::array{u, v};
            const auto opacity = u <= v ? sampled_opacity(*a, first, point) : sampled_opacity(*b, second, point);
            (opacity < .5f ? low : high) = u;
        }
        const auto column = (low + high) * (track_id == 3 ? 8.f : 16.f);
        std::array neighborhood{original_columns[row == 0 ? row : row - 1], original_columns[row], original_columns[std::min(row + 1, original_columns.size() - 1)]};
        std::ranges::sort(neighborhood);
        maximum_main_error = std::max(maximum_main_error, std::abs(column - (track_id == 4 || track_id == 1 ? neighborhood[1] : original_columns[row])));
        maximum_raw_error = std::max(maximum_raw_error, std::abs(column - original_columns[row]));
        if (track_id == 1 && row >= 35 && row <= 41) {
            expect(std::abs(column - 9) < .01f, "Silverstone's inward median shoreline plateau must remain fixed");
        } else if (track_id == 3 && ((row >= 16 && row <= 21) || (row >= 28 && row <= 40))) {
            expect(std::abs(column - original_columns[row]) < .01f, "Brands' rotated source plateaus must remain fixed");
        } else if (track_id == 7 && row >= 30 && row <= 32) {
            expect(std::abs(column - original_columns[row]) < .01f, "Knockhill's isolated source-row inward dip and its shoulders must remain fixed");
        } else if (track_id == 8 && (row == 28 || (row >= 46 && row <= 48))) {
            expect(std::abs(column - original_columns[row]) < .01f, "Snetterton's isolated source-row dip and gravel excursion must remain fixed");
        } else if ((track_id == 4 && row >= 24 && row <= 27) || ((track_id == 0 || track_id == 5) && row >= 35 && row <= 40)) {
            expect(std::abs(column - original_columns[row]) < .01f, "The canonical inward shoreline plateau must remain fixed");
        }
    }
    expect(maximum_main_error <= (track_id == 1 ? 1.36f : track_id == 3 ? .73f : 2.01f), "Simplified main shoreline must stay within its audited source-pixel bound");
    expect(maximum_raw_error <= (track_id == 4 ? 6.01f : track_id == 1 ? 1.36f : track_id == 3 ? .73f : 2.01f), "Only bounded isolated source-pixel spikes may be removed");
    std::cout << "PASS: track" << track_id << " half-width " << feather_half_width << " half-coverage shoreline maximum error " << maximum_main_error << " main / "
              << maximum_raw_error << " raw source pixels\n";
}

void knockhill_boundaries() {
    const auto profile = mega_ground_transition_profile(7);
    expect(profile && profile->mixed == "g&g" && profile->grass == "GRASS2" && profile->gravel == "gravel" && profile->feather_half_width == .12f && profile->cap_width == 0,
        "Knockhill must use its exact case-sensitive material slots and the audited cap-free width");
    ModelTexture texture{"g&g", {}, ImageEncoding::webp};
    expect(is_mega_ground_transition(true, 7, texture) && !is_mega_ground_transition(false, 7, texture) && !is_mega_ground_transition(true, 0, texture),
        "Knockhill's mixed slot must be isolated from original rendering and other tracks");
    for (const auto name : {"G&G", "GG", "NGG", "GRASS2", "gravel", "WHITEL"}) {
        texture.name = name;
        expect(!is_mega_ground_transition(true, 7, texture), "Other material slots and casing must never substitute for Knockhill's mixed strip");
    }
    texture.name = "g&g";
    texture.encoding = ImageEncoding::bmp;
    expect(!is_mega_ground_transition(true, 7, texture), "Knockhill's original BMP profile must retain authored rendering");
    // Canonical chunk84 face83 edge0 borders WHITEL84:60. Its constant low-U
    // boundary must remain entirely grass, with no sand/gravel cap into paint.
    const std::array<ModelVertex, 3> source{{{{0, 2, 0}, {0, 1, 0}, .02f, .985000610f}, {{0, 2, 10}, {0, 1, 0}, .02f, .02f}, {{10, 2, 10}, {0, 1, 0}, .984985352f, .02f}}};
    const auto prepared = prepare_mega_ground_transition(7, source, profile->feather_half_width);
    expect(prepared.has_value(), "Knockhill's protected marking boundary must prepare on its original source plane");
    check_face(source, *prepared);
    for (unsigned step = 0; step <= 100; ++step) {
        const auto v = source[0].v + (source[1].v - source[0].v) * float(step) / 100;
        expect(sampled_opacity(*prepared, source, {.02f, v}) == 0, "Knockhill's authored WHITEL boundary must retain zero gravel opacity");
    }
    std::cout << "PASS: Knockhill exact slots, original-profile isolation and zero-alpha protected marking boundary\n";
}

void snetterton_boundaries() {
    const auto profile = mega_ground_transition_profile(8);
    expect(profile && profile->mixed == "G&G2" && profile->grass == "GRASS3" && profile->gravel == "GRAVEL" && profile->feather_half_width == .12f && profile->cap_width == 0,
        "Snetterton must use its exact audited material slots and cap-free width");
    ModelTexture texture{"G&G2", {}, ImageEncoding::webp};
    expect(is_mega_ground_transition(true, 8, texture) && !is_mega_ground_transition(false, 8, texture) && !is_mega_ground_transition(true, 0, texture),
        "Snetterton's mixed slot must be isolated from original rendering and other tracks");
    for (const auto name : {"G&G", "g&g", "GG", "NGG", "GRASS3", "GRAVEL", "W_LINE", "RUMSTRIP"}) {
        texture.name = name;
        expect(!is_mega_ground_transition(true, 8, texture), "Plain slots, paint and other tracks' mixed names must not substitute for Snetterton's strip");
    }
    texture.name = "G&G2";
    texture.encoding = ImageEncoding::bmp;
    expect(!is_mega_ground_transition(true, 8, texture), "Original Snetterton BMP rendering must stay unchanged");
    // All20 W_LINE and both RUMSTRIP boundaries share these authored low-U
    // endpoints (for example69:67 and70:54), so the full edge stays grass.
    const std::array<ModelVertex, 3> source{{{{0, 2, 0}, {0, 1, 0}, .02f, .985000610f}, {{0, 2, 10}, {0, 1, 0}, .02f, .02f}, {{10, 2, 10}, {0, 1, 0}, .984985352f, .02f}}};
    const auto prepared = prepare_mega_ground_transition(8, source, profile->feather_half_width);
    expect(prepared.has_value(), "Snetterton's protected source boundary must prepare on its unchanged plane");
    check_face(source, *prepared);
    for (unsigned step = 0; step <= 200; ++step) {
        const auto v = source[0].v + (source[1].v - source[0].v) * float(step) / 200;
        expect(sampled_opacity(*prepared, source, {.02f, v}) == 0, "Every point of the authored Snetterton paint/rumble boundary must retain zero gravel alpha");
    }
    std::cout << "PASS: Snetterton exact slots, original-profile isolation and protected W_LINE/RUMSTRIP boundary\n";
}

void wider_shoreline_band() {
    const std::array<ModelVertex, 3> source{{{{0, 2, 0}, {0, 1, 0}, .02f, .02f}, {{0, 8, 8}, {0, 1, 0}, .02f, .99f}, {{10, 2, 10}, {0, 1, 0}, .99f, .99f}}};
    const auto original = prepare_mega_ground_transition(0, source);
    const auto explicit_default = prepare_mega_ground_transition(0, source, .05f);
    const auto wider = prepare_mega_ground_transition(0, source, .12f);
    expect(original && explicit_default && wider, "Default and selected wider shoreline bands must prepare");
    expect(original->gravel.size() == explicit_default->gravel.size(), "Default half-width must preserve the accepted tessellation");
    for (std::size_t triangle = 0; triangle < original->gravel.size(); ++triangle) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& a = original->gravel[triangle][corner];
            const auto& b = explicit_default->gravel[triangle][corner];
            expect(a.position == b.position && a.source_weights == b.source_weights && a.opacity == b.opacity,
                "Default half-width must preserve every accepted source weight, position and opacity");
        }
    }
    check_face(source, *wider);
    // Canonical Donington rows35–40 form a constant nine-pixel inward plateau.
    const auto v = 37.5f / 64;
    const auto shoreline = 9.f / 32;
    expect(std::abs(sampled_opacity(*original, source, {shoreline, v}) - .5f) < .003f && std::abs(sampled_opacity(*wider, source, {shoreline, v}) - .5f) < .003f,
        "Widening must leave the canonical shoreline's half-coverage contour fixed");
    expect(sampled_opacity(*original, source, {shoreline - .08f, v}) == 0 && sampled_opacity(*wider, source, {shoreline - .08f, v}) > .15f,
        "A wider band must extend partial gravel coverage into the original grass side");
    expect(sampled_opacity(*original, source, {shoreline + .08f, v}) > .999f && sampled_opacity(*wider, source, {shoreline + .08f, v}) < .85f,
        "A wider band must extend partial grass coverage into the original gravel side");
    expect(sampled_opacity(*wider, source, {shoreline - .13f, v}) == 0 && sampled_opacity(*wider, source, {shoreline + .13f, v}) > .999f,
        "The wider band must remain bounded with solid material beyond its selected width");
    for (const auto width : {0.f, -.05f, .21f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        expect(!prepare_mega_ground_transition(0, source, width), "Invalid or unbounded feather widths must retain the original fallback");
    }
    expect(prepare_mega_ground_transition(0, source, .2f).has_value(), "The supported maximum width must remain available");
    std::cout << "PASS: wider bounded shoreline band, fixed half-coverage contour and unchanged default (" << original->gravel.size() << " / " << wider->gravel.size()
              << " overlay triangles)\n";
}

void silverstone_partial_uv_boundary() {
    const auto profile = mega_ground_transition_profile(1);
    expect(profile && profile->feather_half_width == .065f, "Silverstone's accepted profile must retain its narrower partial-UV boundary guard");
    // Exact audited UV endpoints of chunk283 face81 edge0, adjacent to plain
    // GRASS2. The third corner only supplies the interior of this synthetic face.
    const std::array<ModelVertex, 3> source{
        {{{0, 2, 0}, {0, 1, 0}, .274215698f, .020706177f}, {{0, 3, 10}, {0, 1, 0}, .057388306f, .973190308f}, {{10, 4, 10}, {0, 1, 0}, .985f, .985f}}};
    const auto accepted = prepare_mega_ground_transition(1, source, profile->feather_half_width);
    const auto excessive = prepare_mega_ground_transition(1, source, .12f);
    expect(accepted && excessive, "The audited partial-UV face must prepare without changing its physical geometry");
    check_face(source, *accepted);
    float maximum_accepted = 0, maximum_excessive = 0;
    for (unsigned step = 0; step <= 100; ++step) {
        const auto fraction = float(step) / 100;
        const std::array uv{source[0].u + (source[1].u - source[0].u) * fraction, source[0].v + (source[1].v - source[0].v) * fraction};
        maximum_accepted = std::max(maximum_accepted, sampled_opacity(*accepted, source, uv));
        maximum_excessive = std::max(maximum_excessive, sampled_opacity(*excessive, source, uv));
    }
    expect(maximum_accepted == 0, "The accepted Silverstone width must leave every point of the partial-UV grass boundary entirely grass");
    expect(maximum_excessive > .2f, "The partial-UV regression must detect the gravel leak caused by an excessive wider band");
    std::cout << "PASS: Silverstone partial-UV grass boundary retains zero gravel alpha with accepted width\n";
}

void direct_bands() {
    const std::array<ModelVertex, 3> source{{{{0, 2, 0}, {0, 1, 0}, 0, 0}, {{10, 2, 0}, {0, 1, 0}, 1, 0}, {{0, 2, 10}, {0, 1, 0}, 0, 1}}};
    const MegaGroundDirectSegment edge{{source[0].position, source[1].position}, .3f};
    const auto prepared = [&](std::span<const MegaGroundDirectSegment> segments) {
        MegaGroundTransition result;
        result.gravel = prepare_mega_ground_direct_band(source, segments);
        for (std::size_t corner = 0; corner < 3; ++corner) {
            result.grass[corner] = {source[corner].position, {}, 255};
            result.grass[corner].source_weights[corner] = 1;
        }
        return result;
    };
    const auto band = prepared(std::span(&edge, 1));
    check_face(source, band);
    expect(
        sampled_opacity(band, source, {.5f, 0}) > .999f && std::abs(sampled_opacity(band, source, {.5f, .015f}) - .5f) < .005f && sampled_opacity(band, source, {.5f, .031f}) == 0,
        "Direct gravel alpha must fall from one to zero inside the selected world width");
    float coverage = 0;
    for (const auto& triangle : band.gravel) {
        const std::array uv{original_uv(triangle[0], source), original_uv(triangle[1], source), original_uv(triangle[2], source)};
        expect(area(uv) > 1.e-12f, "Direct bands must contain no degenerate triangles");
        coverage += area(uv);
    }
    expect(std::abs(coverage - .02955f) < 1.e-6f, "Direct strip triangles must partition their clipped band without duplicate coverage");
    const std::array joined{edge, MegaGroundDirectSegment{{source[0].position, source[2].position}, .3f}};
    const auto corner = prepared(joined);
    check_face(source, corner);
    expect(std::abs(sampled_opacity(corner, source, {.015f, .015f}) - .5f) < .005f, "Two joining bands must use maximum alpha rather than overlapping translucent layers");
    float joined_coverage = 0;
    for (const auto& triangle : corner.gravel) {
        joined_coverage += area({original_uv(triangle[0], source), original_uv(triangle[1], source), original_uv(triangle[2], source)});
    }
    expect(std::abs(joined_coverage - .0582f) < 1.e-6f, "Joining bands must exactly cover their geometric union without overlapping alpha layers or merge gaps");
    const std::array<ModelVertex, 3> neighbor{{{{-10, 2, 0}, {0, 1, 0}, 0, 0}, source[0], source[2]}};
    auto neighbor_uv = neighbor;
    neighbor_uv[1].u = 1;
    MegaGroundTransition cap;
    cap.gravel = prepare_mega_ground_direct_band(neighbor_uv, std::span(&edge, 1));
    expect(!cap.gravel.empty() && std::abs(sampled_opacity(cap, neighbor_uv, {.99f, .01f}) - sampled_opacity(band, source, {0, .01f})) < .005f,
        "Shared segment caps must preserve alpha across the adjoining grass triangle");
    auto tapered = edge;
    tapered.taper[0] = true;
    const auto ending = prepared(std::span(&tapered, 1));
    expect(
        sampled_opacity(ending, source, {0, 0}) == 0 && std::abs(sampled_opacity(ending, source, {.015f, 0}) - .5f) < .005f && sampled_opacity(ending, source, {.031f, 0}) > .999f,
        "Protected endpoints must taper longitudinally before recovering full boundary coverage");
    auto no_uv = source;
    for (auto& vertex : no_uv) {
        vertex.u = vertex.v = 0;
    }
    expect(!prepare_mega_ground_direct_band(no_uv, std::span(&edge, 1)).empty(), "World bands must not depend on authored grass UV area");
    auto invalid = edge;
    invalid.width = 0;
    expect(prepare_mega_ground_direct_band(source, std::span(&invalid, 1)).empty(), "Invalid world band widths must not generate geometry");
    no_uv[1].position = no_uv[0].position;
    expect(prepare_mega_ground_direct_band(no_uv, std::span(&edge, 1)).empty(), "Degenerate physical source triangles must not generate band geometry");
    std::cout << "PASS: bounded coplanar direct bands, disjoint max-alpha joins, continuous caps and protected endpoints\n";
}

void brands_caps() {
    const auto profile = mega_ground_transition_profile(3);
    expect(profile && profile->mixed == "NGG" && profile->grass == "GRASS2" && profile->gravel == "NLSAND" && profile->cap_width == .3f,
        "Brands must expose its audited rotated grass/sand profile and bounded collar width");
    const std::array<ModelVertex, 3> source{{{{0, 2, 0}, {0, 1, 0}, .02f, .985f}, {{10, 2, 0}, {0, 1, 0}, .02f, .02f}, {{10, 2, 10}, {0, 1, 0}, .985f, .02f}}};
    const auto original = prepare_mega_ground_transition(3, source, profile->feather_half_width);
    const MegaGroundTransitionCap sand{{{source[0].position, source[2].position}, .3f}, true};
    auto grass = sand;
    grass.gravel = false;
    const auto sand_result = prepare_mega_ground_transition(3, source, profile->feather_half_width, std::span(&sand, 1));
    const auto grass_result = prepare_mega_ground_transition(3, source, profile->feather_half_width, std::span(&grass, 1));
    expect(original && sand_result && grass_result, "Both diagonal neutral cap directions must prepare");
    check_face(source, *sand_result);
    check_face(source, *grass_result);
    for (unsigned step = 0; step <= 32; ++step) {
        const auto t = float(step) / 32;
        const std::array uv{source[0].u * (1 - t) + source[2].u * t, source[0].v * (1 - t) + source[2].v * t};
        expect(sampled_opacity(*sand_result, source, uv) > .995f && std::abs(sampled_opacity(*grass_result, source, uv)) < .005f,
            "Diagonal cap alpha must join the neighboring plain sand or grass without material bleed");
    }
    for (unsigned second = 1; second < 20; ++second) {
        for (unsigned third = 1; second + third < 20; ++third) {
            const std::array weights{1 - float(second + third) / 20, float(second) / 20, float(third) / 20};
            std::array<float, 2> uv{};
            for (std::size_t corner = 0; corner < 3; ++corner) {
                uv[0] += source[corner].u * weights[corner];
                uv[1] += source[corner].v * weights[corner];
            }
            // Distance from this face's diagonal edge is 10*w1/sqrt(2).
            if (10 * weights[1] / std::sqrt(2.f) > .31f) {
                const auto alpha = sampled_opacity(*original, source, uv);
                expect(std::abs(sampled_opacity(*sand_result, source, uv) - alpha) < .005f && std::abs(sampled_opacity(*grass_result, source, uv) - alpha) < .005f,
                    "Cap correction must leave the authored affine contour unchanged outside its bounded world collar");
            }
        }
    }
    auto protected_sand = sand;
    protected_sand.edge.taper[0] = true;
    MegaGroundTransitionCap protected_grass{{{source[0].position, source[1].position}, .3f}, false};
    protected_grass.edge.taper[0] = true;
    const std::array junction{protected_sand, protected_grass};
    const auto joined = prepare_mega_ground_transition(3, source, profile->feather_half_width, junction);
    expect(joined.has_value(), "Protected opposite-material cap junctions must prepare a single layer");
    float coverage = 0;
    for (const auto& triangle : joined->gravel) {
        const auto triangle_area = area({original_uv(triangle[0], source), original_uv(triangle[1], source), original_uv(triangle[2], source)});
        std::array<double, 3> first{}, second{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            first[axis] = double(triangle[1].position[axis]) - triangle[0].position[axis];
            second[axis] = double(triangle[2].position[axis]) - triangle[0].position[axis];
        }
        expect(std::hypot(first[1] * second[2] - first[2] * second[1], first[2] * second[0] - first[0] * second[2], first[0] * second[1] - first[1] * second[0]) > 1.e-10,
            "Cap partitions must not emit degenerate physical triangles");
        coverage += triangle_area;
    }
    expect(coverage <= .5f * .965f * .965f + 1.e-5f, "Protected max/min cap fields must not overlap or extend beyond the source triangle");
    std::cout << "PASS: rotated Brands mask, both diagonal cap materials, unchanged core contour and bounded protected junctions\n";
}

void synthetic() {
    ModelTexture texture{"G&G", {}, ImageEncoding::webp};
    expect(is_mega_ground_transition(true, 4, texture) && is_mega_ground_transition(true, 0, texture) && is_mega_ground_transition(true, 5, texture),
        "Accepted Mega track-specific transitions must be recognized");
    expect(!is_mega_ground_transition(false, 4, texture) && !is_mega_ground_transition(true, 1, texture),
        "Other profiles and mismatched track material names must retain authored rendering");
    const auto silverstone = mega_ground_transition_profile(1);
    expect(silverstone && silverstone->mixed == "GG" && silverstone->grass == "GRASS2" && silverstone->gravel == "GRAVEL",
        "Silverstone must use its exact audited canonical mixed and plain material names");
    for (const auto track_id : {0u, 4u, 5u}) {
        const auto profile = mega_ground_transition_profile(track_id);
        expect(profile && profile->mixed == "G&G" && profile->grass == "GRASS" && profile->gravel == "GRAVEL" && profile->feather_half_width == .12f,
            "Existing accepted tracks must retain their exact material pair and rollout width");
    }
    texture.name = "GG";
    expect(is_mega_ground_transition(true, 1, texture) && !is_mega_ground_transition(false, 1, texture) && !is_mega_ground_transition(true, 0, texture),
        "Silverstone's GG strip must be isolated from original profiles and other tracks");
    texture.name = "G&G";
    texture.encoding = ImageEncoding::bmp;
    expect(!is_mega_ground_transition(true, 4, texture), "Original BMP comparison must retain the canonical G&G image");
    texture.encoding = ImageEncoding::webp;
    texture.name = "GRID_L";
    expect(!is_mega_ground_transition(true, 0, texture), "Authored markings must stay outside the transition pass");
    const std::array<ModelVertex, 3> source{{{{0, 2, 0}, {0, 1, 0}, .02f, .02f}, {{0, 2, 10}, {0, 1, 0}, .02f, .99f}, {{10, 2, 10}, {0, 1, 0}, .99f, .99f}}};
    const auto result = prepare_mega_ground_transition(4, source);
    expect(result.has_value(), "Finite authored UV triangle must prepare a transition");
    expect(!prepare_mega_ground_transition(2, source), "Tracks without an audited source shoreline must retain original rendering");
    check_face(source, *result);
    // The key inward curve stays fixed; the lower boundary remains within a
    // fraction of a source pixel despite the reduced number of draw corners.
    expect(std::abs(sampled_opacity(*result, source, {8.f / 32, 24.5f / 64}) - .5f) < .003f, "Original inward shoreline control must remain fixed");
    expect(std::abs(sampled_opacity(*result, source, {15.f / 32, 62.5f / 64}) - .5f) < .25f, "Original lower shoreline must remain within a fraction of a source pixel");
    expect(sampled_opacity(*result, source, {.05f, .8f}) == 0 && sampled_opacity(*result, source, {.7f, .8f}) > .999f, "Far sides must stay entirely grass and gravel");
    const MegaGroundTransitionVertex lit{{}, {.25f, .5f, .25f}, 127};
    expect(mega_ground_transition_color(lit, {0xff204060, 0xff6080a0, 0xffa0c0e0}) == 0x7f6080a0, "Blend alpha must preserve interpolated original lighting channels");
    auto invalid = source;
    invalid[0].u = std::numeric_limits<float>::quiet_NaN();
    expect(!prepare_mega_ground_transition(4, invalid), "Invalid UVs must retain the original fallback");
    invalid = source;
    invalid[1].u = invalid[0].u;
    invalid[1].v = invalid[0].v;
    expect(!prepare_mega_ground_transition(4, invalid), "Degenerate UVs must retain the original fallback");
    std::cout << "PASS: authored shoreline, bounded coplanar coverage, source lighting and profile isolation (" << result->gravel.size() << " overlay triangles)\n";
}

void canonical(const char* path, std::uint32_t track_id) {
    std::ifstream file(path, std::ios::binary);
    expect(bool(file), "Cannot open selective canonical ground input");
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto terrain = parse_terrain(bytes);
    const auto materials = mega_ground_transition_profile(track_id);
    expect(materials.has_value(), "Canonical input must have an audited transition profile");
    std::size_t faces = 0, triangles = 0, maximum = 0, wider_triangles = 0, wider_maximum = 0;
    for (const auto& chunk : terrain.chunks) {
        for (const auto& face : chunk.mesh.faces) {
            if (face.texture < 0 || terrain.textures.at(std::size_t(face.texture)).name != materials->mixed) {
                continue;
            }
            const std::array source{chunk.mesh.vertices.at(face.indices[0]), chunk.mesh.vertices.at(face.indices[1]), chunk.mesh.vertices.at(face.indices[2])};
            const auto prepared = prepare_mega_ground_transition(track_id, source);
            expect(prepared.has_value(), "Every canonical transition face must prepare");
            check_face(source, *prepared);
            if (track_id == 0 || track_id == 1 || track_id == 7 || track_id == 8) {
                const auto wider = prepare_mega_ground_transition(track_id, source, materials->feather_half_width);
                expect(wider.has_value(), "Canonical accepted faces must support the selected wider band");
                check_face(source, *wider);
                wider_triangles += wider->gravel.size();
                wider_maximum = std::max(wider_maximum, wider->gravel.size());
            }
            ++faces;
            triangles += prepared->gravel.size();
            maximum = std::max(maximum, prepared->gravel.size());
        }
    }
    expect(faces
               == (track_id == 4   ? 46
                   : track_id == 1 ? 162
                   : track_id == 3 ? 70
                   : track_id == 7 ? 102
                   : track_id == 8 ? 124
                                   : 196),
        "Canonical shoreline inventory must match its track-specific original triangle count");
    const auto triangle_budget = track_id == 8 ? 68u : track_id == 7 ? 56u : 40u;
    const auto maximum_budget = track_id == 8 ? 80u : 60u;
    expect(triangles <= faces * triangle_budget && maximum <= maximum_budget, "Canonical transition tessellation must stay bounded with compact source curves");
    std::cout << "PASS: track" << track_id << ' ' << faces << " canonical faces; " << triangles << " gravel triangles total, " << maximum << " maximum per face\n";
    if (track_id == 0 || track_id == 1 || track_id == 7 || track_id == 8) {
        expect(wider_triangles <= faces * triangle_budget && wider_maximum <= maximum_budget, "Widening must retain compact canonical tessellation");
        std::cout << "PASS: track" << track_id << " wider band " << wider_triangles << " gravel triangles total, " << wider_maximum << " maximum per face\n";
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        synthetic();
        wider_shoreline_band();
        silverstone_partial_uv_boundary();
        knockhill_boundaries();
        snetterton_boundaries();
        direct_bands();
        brands_caps();
        for (const auto track_id : {0u, 1u, 3u, 4u, 5u, 7u, 8u}) {
            shoreline_quality(track_id);
        }
        shoreline_quality(0, .12f);
        shoreline_quality(1, .065f);
        shoreline_quality(3, .12f);
        shoreline_quality(7, .12f);
        shoreline_quality(8, .12f);
        if (argc == 2 || argc == 3) {
            canonical(argv[1], argc == 3 ? unsigned(std::stoul(argv[2])) : 4u);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
