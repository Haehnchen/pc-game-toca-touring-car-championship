#include "assets/store.hpp"
#include "formats/terrain.hpp"
#include "simulation/barrier_contact.hpp"
#include "simulation/fixed.hpp"
#include "simulation/motion.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace toca;
using namespace toca::simulation;
constexpr std::int32_t one = 65536;
using Bytes = std::vector<std::uint8_t>;
constexpr std::array<std::array<std::int32_t, 2>, 4> centers{{{-32 * one, 32 * one}, {32 * one, 32 * one}, {-32 * one, -32 * one}, {32 * one, -32 * one}}};

void expect(bool valid, const char* message) {
    if (!valid) {
        throw std::runtime_error(message);
    }
}

void put32(Bytes& bytes, std::size_t offset, std::int32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(offset + i) = std::uint8_t(std::uint32_t(value) >> (8 * i));
    }
}

Bytes segment(std::uint8_t kind = 1, std::int32_t near_z = -16 * one, std::int32_t far_z = 16 * one) {
    Bytes bytes(96);
    put32(bytes, 4, 1);
    put32(bytes, 16, near_z);
    put32(bytes, 28, far_z);
    bytes[8 + 0x55] = kind;
    return bytes;
}

scene::BodyPose body(std::int32_t x) {
    scene::BodyPose result;
    result.basis = {{{one, 0, 0}, {0, one, 0}, {0, 0, one}}};
    result.position = {x, 2 * one, 0};
    scene::refresh_body_contacts(result, 0);
    return result;
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

BarrierContactResult ordinary(
    const scene::BarrierData& data, scene::BodyPose& pose, std::array<std::int32_t, 3>& velocity, const std::array<std::int32_t, 3>& previous, std::uint32_t car) {
    BarrierRecovery recovery;
    return resolve_barriers(data, pose, velocity, previous, car, recovery, false);
}

Bytes recovery_chain() {
    Bytes bytes(8 + 4 * 88);
    put32(bytes, 4, 4);
    constexpr std::array<std::array<std::int32_t, 4>, 4> edges{{{0, -16, 0, 16}, {0, 16, 8, 16}, {8, 16, 8, -16}, {8, -16, 0, -16}}};
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto begin = 8 + i * 88;
        put32(bytes, begin, edges[i][0] * one);
        put32(bytes, begin + 8, edges[i][1] * one);
        put32(bytes, begin + 12, edges[i][2] * one);
        put32(bytes, begin + 20, edges[i][3] * one);
        bytes[begin + 0x55] = i % 2 ? 3 : 2;
        // Poison runtime fields: the loader must reconstruct them itself.
        put32(bytes, begin + 0x4c, INT32_MAX);
    }
    return bytes;
}

void synthetic() {
    auto bytes = segment();
    const scene::BarrierData data(bytes, centers);
    bytes.assign(bytes.size(), 0);
    expect(data.segments().size() == 1 && data.segments()[0].normal == std::array<std::int32_t, 3>{one, 0, 0} && data.segments()[0].first[2] == -16 * one,
        "Barrier decoder must own coordinates and reconstruct the source axis normal independently of input lifetime");
    const auto half = scene::car_half_dimensions(0);
    auto contact = body(-half[0] + one / 5);
    const auto prior = std::array<std::int32_t, 3>{-half[0] - one, 2 * one, 0};
    std::array<std::int32_t, 3> velocity{one / 2, 0, 0};
    const auto result = ordinary(data, contact, velocity, prior, 0);
    expect(result.contacts == 1 && contact.position[0] == -half[0], "Crossing the finite static plane must move the exposed front body corner back to the barrier");
    expect(velocity == std::array<std::int32_t, 3>{-3277, 0, 0}, "4095b0 must retain its source -0x1999 normal restitution rather than a generic reflection");
    expect(result.angular_impulse_q16 != std::array<std::int32_t, 3>{}, "Off-center static impulse must provide the source torque for the next body step");
    expect(result.damage_contacts.size() == 1 && result.damage_contacts[0].impulse_q16[0] > 0 && result.damage_contacts[0].barrier_index == 0,
        "The original barrier caller must expose the raw source impulse and segment identity");
    expect(result.damage_contacts[0].position_q16 != contact.position, "Impact audio must use the original selected-corner representative position");
    expect(result.effects.fragment_strength_q16 == 0 && result.effects.spark_strength_q16 == 0 && result.effects.scrape_strength_q16 == 0,
        "An ordinary slow head-on contact must not invent a fragment burst, sparks or scrape loop");
    expect(result.effects.position_q16 == result.damage_contacts.front().position_q16 && result.effects.tangent_velocity_q16 == std::array<std::int32_t, 3>{},
        "409fa0 effects must share the selected representative contact point and remove the wall-normal velocity");
    //4095b0 calls47b250 for the impulse and lever arm before its fixed cross.
    //This3:4:5 yaw gives different torque with per-product Q16 truncation.
    auto angled = body(-half[0] + one / 5);
    angled.basis = {{{0xcccc, 0, 0x9999}, {0, one, 0}, {-0x9999, 0, 0xcccc}}};
    scene::refresh_body_contacts(angled, 0);
    std::array<std::int32_t, 3> angled_velocity{one / 2, 0, 0};
    const auto angled_hit = ordinary(data, angled, angled_velocity, prior, 0);
    //Source rounded inputs:{-57671,0,43253} and{153514,-205127,-60651}.
    expect(angled_hit.contacts == 1 && angled_hit.angular_impulse_q16 == std::array<std::int32_t, 3>{135382, 47945, 180509},
        "Barrier torque must retain47b250's single nearest/even conversion before the original fixed cross products");
    // Incoming normal values327705/327711 become exactly32768/32769 after
    // the original response. Test the strict fragment gate on that result.
    for (const auto kind : {0u, 1u}) {
        const scene::BarrierData wall(segment(std::uint8_t(kind)), centers);
        for (const auto input_normal : {327705, 327711}) {
            auto pose = body(-half[0] + one / 5);
            std::array<std::int32_t, 3> incoming{input_normal, 0, 0};
            const auto edge = ordinary(wall, pose, incoming, prior, 0);
            expect(edge.contacts == 1 && edge.effects.fragment_strength_q16 == (input_normal == 327705 ? 0 : 4),
                "409fa0 fragment strength must use its strict post-response half-unit threshold for both wall kinds");
            expect(edge.effects.spark_strength_q16 == 0 && edge.effects.scrape_strength_q16 == 0, "Normal-only impacts must never start tangential sparks or scraping");
        }
        auto pose = body(-half[0] + one / 5);
        std::array<std::int32_t, 3> incoming{8 * one, 0, one / 2};
        const auto hard = ordinary(wall, pose, incoming, prior, 0);
        expect(hard.contacts == 1 && hard.effects.fragment_strength_q16 == one, "A hard normal hit must saturate the original fragment strength at one");
        expect(hard.effects.tangent_velocity_q16 == (kind == 1 ? std::array<std::int32_t, 3>{0, 0, one / 2} : std::array<std::int32_t, 3>{}),
            "409fa0 must retain tangential flight velocity only for an actual wall-spark request");
        expect(hard.effects.spark_strength_q16 == (kind == 1 ? 49173 : 0) && hard.effects.scrape_strength_q16 == (kind == 1 ? 32775 : 0),
            "Only wall kind1 may emit the original tangent-length spark and scrape strengths");
        if (kind == 1) {
            expect(hard.effects.spark_direction_q16 == std::array<std::int32_t, 3>{0, 0, -131016},
                "Original sparks divide tangent by length minus one-quarter, then reverse against travel; they are not unit vectors");
        }
    }
    for (const auto tangent : {0x3fff, 0x4000, -0x8000, 0x20000}) {
        auto pose = body(-half[0] + one / 5);
        std::array<std::int32_t, 3> incoming{one / 2, 0, tangent};
        const auto scrape = ordinary(data, pose, incoming, prior, 0);
        expect(scrape.contacts == 1 && scrape.effects.fragment_strength_q16 == 0, "A glancing scrape must keep normal-fragment gating independent of tangential speed");
        if (tangent == 0x3fff) {
            expect(scrape.effects.spark_strength_q16 == 0 && scrape.effects.scrape_strength_q16 == 0,
                "A tangent below the source lookup threshold must produce no sparks or scrape loop");
        } else if (tangent == 0x4000) {
            expect(scrape.effects.spark_strength_q16 == 9 && scrape.effects.scrape_strength_q16 == 16387,
                "The original sqrt lookup rounds tangent0x4000 to0x4003, opening the strict source spark gate");
        } else if (tangent < 0) {
            expect(scrape.effects.spark_strength_q16 == 49173 && scrape.effects.scrape_strength_q16 == 32775
                       && scrape.effects.spark_direction_q16 == std::array<std::int32_t, 3>{0, 0, 131016},
                "Reverse tangential travel must reverse the original spark direction without changing its magnitude");
        } else {
            expect(
                scrape.effects.spark_strength_q16 == one && scrape.effects.scrape_strength_q16 == one, "Fast tangential travel must clamp both source spark and scrape strengths");
        }
    }
    const scene::BarrierData softer(segment(0), centers);
    auto other = body(-half[0] + one / 5);
    std::array<std::int32_t, 3> other_velocity{one / 2, 0, 0};
    const auto soft = ordinary(softer, other, other_velocity, prior, 0);
    expect(soft.damage_contacts.size() == 1 && soft.damage_contacts[0].impulse_q16[0] < result.damage_contacts[0].impulse_q16[0],
        "Original type-zero impact scaling5 must differ from type-one scaling16");
    auto departing = body(-half[0] + one / 5);
    const auto original = departing.position;
    std::array<std::int32_t, 3> away{-one / 2, 0, 0};
    const auto ignored = ordinary(data, departing, away, {-half[0] + one, 2 * one, 0}, 0);
    expect(ignored.contacts == 0 && departing.position == original && away[0] == -one / 2, "A body moving away on the same source plane side must not receive another bounce");
    expect(ignored.effects.position_q16 == std::array<std::int32_t, 3>{} && ignored.effects.tangent_velocity_q16 == std::array<std::int32_t, 3>{}
               && ignored.effects.spark_direction_q16 == std::array<std::int32_t, 3>{} && ignored.effects.fragment_strength_q16 == 0 && ignored.effects.spark_strength_q16 == 0
               && ignored.effects.scrape_strength_q16 == 0,
        "An unselected departing wall contact must not fabricate any source effect request");
    const scene::BarrierData remote(segment(1, 20 * one, 30 * one), centers);
    auto outside = body(-half[0] + one / 5);
    std::array<std::int32_t, 3> remote_velocity{one / 2, 0, 0};
    expect(ordinary(remote, outside, remote_velocity, prior, 0).contacts == 0, "Finite endpoint bounds must not turn an off-segment plane into an infinite wall");
    rejects(
        [] {
            const scene::BarrierData invalid(Bytes(3), centers);
        },
        "Truncated POINTS count must be rejected");
    rejects(
        [] {
            auto invalid = segment();
            put32(invalid, 0, INT32_MAX);
            const scene::BarrierData rejected(invalid, centers);
        },
        "Route multiplication must be checked before locating barrier records");
    rejects(
        [] {
            auto invalid = segment();
            put32(invalid, 4, INT32_MAX);
            const scene::BarrierData rejected(invalid, centers);
        },
        "Barrier multiplication must be checked before reading or allocating segments");
}

void sectors() {
    auto padded = segment();
    put32(padded, 8, 0x28000);
    put32(padded, 20, 0x28000);
    const scene::BarrierData inclusive(padded, centers);
    expect(
        inclusive.sector_count() == 4 && inclusive.sector({0, 0, 0}) == 3 && inclusive.sector({-1, 0, 0}) == 2, "47dab0 must select sectors from signed X/Z in original row order");
    expect(inclusive.sector({-64 * one - 1, 0, 32 * one}) == 0 && !inclusive.sector({-64 * one - 64, 0, 32 * one}),
        "Negative sector lookup must retain the source 63-raw-unit adjustment rather than truncating a full sector");
    expect(inclusive.candidates({-32 * one, 0, -32 * one}).size() == 1 && inclusive.candidates({32 * one, 0, -32 * one}).size() == 1,
        "47db40 must include an endpoint on the exact 2.5-unit expanded sector edge");
    put32(padded, 8, 0x28001);
    put32(padded, 20, 0x28001);
    const scene::BarrierData beyond(padded, centers);
    expect(beyond.candidates({-32 * one, 0, -32 * one}).empty() && beyond.candidates({32 * one, 0, -32 * one}).size() == 1,
        "A segment one raw unit beyond the expanded boundary must not become a neighboring candidate");

    // Both endpoints lie outside the selected rectangle; fixed clipping must
    // still retain the crossing segment, in ascending original record order.
    auto crossing = segment(0, -32 * one, -32 * one);
    put32(crossing, 8, -80 * one);
    put32(crossing, 20, 80 * one);
    crossing.insert(crossing.end(), padded.begin() + 8, padded.end());
    put32(crossing, 4, 2);
    const scene::BarrierData clipped(crossing, centers);
    const auto selected = clipped.candidates({32 * one, 0, -32 * one});
    expect(selected.size() == 2 && selected[0] == 0 && selected[1] == 1, "Sector clipping must retain crossing lines and source index order before reverse contact consumption");
    auto outside = body(100 * one);
    const auto before = outside.position;
    std::array<std::int32_t, 3> velocity{one, 0, 0};
    expect(!clipped.sector(outside.position) && ordinary(clipped, outside, velocity, before, 0).contacts == 0 && outside.position == before,
        "Out-of-grid positions must return no candidates without reading the original invalid sector pointer");
    // Lavaland rows retain a +1 raw Z offset. Candidate rectangles use that
    // stored center; 47dab0's arithmetic sector selection stays independent.
    auto offset_centers = centers;
    ++offset_centers[2][1];
    ++offset_centers[3][1];
    auto edge = segment(1, 0x28001, 0x28001);
    put32(edge, 8, -40 * one);
    put32(edge, 20, -20 * one);
    const scene::BarrierData exact(edge, centers), offset(edge, offset_centers);
    const std::array<std::int32_t, 3> position{-32 * one, 0, -32 * one};
    expect(exact.sector(position) == 2 && offset.sector(position) == 2 && exact.candidates(position).empty() && offset.candidates(position).size() == 1,
        "Stored one-raw-unit center offsets must affect candidate clipping without changing original arithmetic sector selection");
}

void recovery() {
    const scene::BarrierData data(recovery_chain(), centers);
    for (std::size_t i = 0; i < 4; ++i) {
        expect(data.segments()[i].next_index == (i + 1) % 4, "479182 must rebuild the full circular kind2/3 chain instead of trusting serialized runtime pointers");
    }
    expect(data.segments()[0].axis == 2 && data.segments()[0].orientation && data.segments()[1].axis == 3 && data.segments()[1].orientation,
        "478d10 must preserve axis classification and reverse kind3 orientation");
    auto bytes = recovery_chain();
    // A broken endpoint closes the preceding run, not an invalid file link.
    put32(bytes, 8 + 2 * 88, 9 * one);
    const scene::BarrierData split(bytes, centers);
    expect(split.segments()[1].next_index == 0 && split.segments()[2].next_index == 3 && split.segments()[3].next_index == 2,
        "Discontinuous special endpoints must close independent source runs, including the final record");

    auto pose = body(one / 5);
    std::array<std::int32_t, 3> velocity{one / 2, 0, one / 4};
    BarrierRecovery state;
    const auto entered = resolve_barriers(data, pose, velocity, {-one, 2 * one, 0}, 0, state, false);
    expect(entered.recovery_entered && state.active && state.head == 0 && !state.has_secondary && entered.damage_contacts.empty() && velocity[0] == one / 2,
        "Kind2 entry must save velocity and activate recovery without an invented immediate impulse or crash sound");
    expect(state.primary_push == std::array<std::int32_t, 3>{-0x7ae, 0, 0} && state.saved_velocity == velocity,
        "409da0 must orient the exact small normal push and retain the entry velocity");
    auto reverse_pose = pose;
    auto reverse_state = state;
    auto reverse_velocity = velocity;
    const auto advanced = resolve_barriers(data, pose, velocity, pose.position, 0, state, false);
    resolve_barriers(data, reverse_pose, reverse_velocity, reverse_pose.position, 0, reverse_state, true);
    expect(state.active && state.head == 0 && !advanced.recovery_entered && !advanced.recovery_exited && advanced.damage_contacts.empty() && velocity[0] < one / 2,
        "The active source path must retain the nearest head alias, add the small push and suppress ordinary impacts");
    expect(pose.basis[0][2] > 0 && reverse_pose.basis[0][2] < 0, "4074a0 must smooth the real basis toward saved velocity with the actual gear-ratio sign");
    bool exited = false;
    for (unsigned tick = 0; tick < 256 && state.active; ++tick) {
        const auto previous = pose.position;
        pose.position = fixed::add(pose.position, velocity);
        scene::refresh_body_contacts(pose, 0);
        const auto result = resolve_barriers(data, pose, velocity, previous, 0, state, false);
        expect(result.damage_contacts.empty(), "Linked recovery updates must not create ordinary impact audio events");
        exited = result.recovery_exited;
    }
    expect(exited && !state.active, "A complete source push/velocity progression must leave the linked half-space group and exit recovery");

    auto secondary_pose = body(one / 5);
    secondary_pose.position[2] = 16 * one - one / 2;
    scene::refresh_body_contacts(secondary_pose, 0);
    std::array<std::int32_t, 3> secondary_velocity{one / 2, 0, one / 4};
    BarrierRecovery secondary;
    resolve_barriers(data, secondary_pose, secondary_velocity, {-one, 2 * one, secondary_pose.position[2]}, 0, secondary, false);
    expect(secondary.active, "The finite kind2 entry must remain valid near its linked kind3 end");
    const auto corrected = resolve_barriers(data, secondary_pose, secondary_velocity, secondary_pose.position, 0, secondary, false);
    expect(secondary.has_secondary && secondary.secondary_segment == 1 && secondary.secondary_push[0] < 0 && corrected.damage_contacts.empty(),
        "408040 must project the kind3 contact, transfer normal speed to its source tangent and retain secondary state without crash audio");
    for (const auto& point : secondary_pose.contacts) {
        expect(point[2] <= 16 * one, "Kind3 projection must correct the furthest forbidden body corner to its prepared line");
    }
}

void canonical_recovery(const scene::BarrierData& barriers) {
    for (const auto& segment : barriers.segments()) {
        if (segment.kind != 2) {
            continue;
        }
        auto pose = body(0);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            pose.position[axis] = std::int32_t((std::int64_t(segment.first[axis]) + segment.second[axis]) / 2);
        }
        pose.position[1] += 2 * one;
        scene::refresh_body_contacts(pose, 0);
        auto velocity = fixed::scale(segment.normal, -one / 2);
        auto previous = fixed::add(pose.position, fixed::scale(segment.normal, one));
        BarrierRecovery state;
        const auto entry = resolve_barriers(barriers, pose, velocity, previous, 0, state, false);
        if (!entry.recovery_entered) {
            continue;
        }
        const auto head = state.head;
        bool active_update = false;
        for (unsigned tick = 1; tick <= 256; ++tick) {
            previous = pose.position;
            pose.position = fixed::add(pose.position, velocity);
            scene::refresh_body_contacts(pose, 0);
            const auto update = resolve_barriers(barriers, pose, velocity, previous, 0, state, false);
            expect(update.damage_contacts.empty(), "The real canonical recovery chain must not synthesize a crash cue");
            active_update = active_update || state.active;
            if (update.recovery_exited && active_update) {
                std::cout << "Donington linked recovery head " << head << " exited after " << tick << " direct consumer steps\n";
                return;
            }
            if (update.recovery_exited) {
                break;
            }
        }
    }
    throw std::runtime_error("No real Donington kind2/3 chain completed source entry, update and exit");
}

void canonical_effects(const scene::BarrierData& barriers) {
    std::array<bool, 2> exercised{};
    const auto half = scene::car_half_dimensions(0);
    for (const auto& wall : barriers.segments()) {
        if (wall.kind > 1 || exercised[wall.kind]) {
            continue;
        }
        const auto radius = fixed::add(fixed::mul(fixed::abs(wall.normal[0]), half[0]), fixed::mul(fixed::abs(wall.normal[2]), half[2]));
        auto pose = body(0);
        for (unsigned axis = 0; axis < 3; ++axis) {
            pose.position[axis] = std::int32_t((std::int64_t(wall.first[axis]) + wall.second[axis]) / 2);
        }
        pose.position[1] += 2 * one;
        const auto midpoint = pose.position;
        pose.position = fixed::sub(midpoint, fixed::scale(wall.normal, fixed::sub(radius, one / 5)));
        const auto previous = fixed::sub(midpoint, fixed::scale(wall.normal, fixed::add(radius, one)));
        scene::refresh_body_contacts(pose, 0);
        const std::array<std::int32_t, 3> tangent{fixed::neg(wall.normal[2]), 0, wall.normal[0]};
        auto velocity = fixed::add(fixed::scale(wall.normal, 6 * one), fixed::scale(tangent, one / 2));
        const auto result = ordinary(barriers, pose, velocity, previous, 0);
        if (result.damage_contacts.empty() || result.damage_contacts.front().barrier_index != wall.source_index) {
            continue;
        }
        expect(result.effects.fragment_strength_q16 > 0 && result.effects.fragment_strength_q16 <= one,
            "A selected canonical wall of either kind must publish its real bounded high-normal-speed fragment request");
        expect(result.effects.position_q16 == result.damage_contacts.front().position_q16,
            "Canonical wall effects and damage audio must use the same original selected body-corner position");
        if (wall.kind == 0) {
            expect(result.effects.spark_strength_q16 == 0 && result.effects.scrape_strength_q16 == 0,
                "Canonical kind0 barriers must retain fragment-only effects despite tangential travel");
        } else {
            expect(result.effects.spark_strength_q16 > 0 && result.effects.scrape_strength_q16 > 0,
                "Canonical kind1 barriers must publish source sparks and scrape-loop strength while glancing");
            expect(fixed::dot(result.effects.spark_direction_q16, velocity) <= 0, "Canonical wall-spark direction must oppose the actual selected post-response travel");
        }
        exercised[wall.kind] = true;
    }
    expect(exercised[0] && exercised[1], "Canonical Donington data must exercise actual selected kind0 andkind1 effect producers");
}

void canonical(const std::filesystem::path& root, std::uint32_t initial_coast_ticks = 0) {
    const AssetStore store(root);
    const auto lava_keep = store.open_archive("DATA/LANDKEEP.BFP");
    const auto lava_free = store.open_archive("DATA/LANDFREE.BFP");
    const auto lava_ground = parse_terrain(lava_free.member(1).bytes());
    const scene::BarrierData lava(lava_keep.member(1).bytes(), lava_ground.sector_centers);
    expect(lava.sector_count() == 120 && lava_ground.sector_centers[24][1] == -2205940 && lava_ground.sector_centers[0][1] - 128 * one == -2205941,
        "Lavaland must retain original nonintegral terrain row spacing rather than reconstructed centers");
    expect(lava.sector({2916107, 0, -677912}) == 30 && lava.sector({8397069, 0, 1453390}) == 20,
        "Lavaland race and qualifying must keep the original arithmetic sector IDs despite stored row offsets");
    const auto keep = store.open_archive("DATA/DONLKEEP.BFP");
    const auto free = store.open_archive("DATA/DONLFREE.BFP");
    const auto terrain = parse_terrain(free.member(1).bytes());
    const scene::BarrierData barriers(keep.member(1).bytes(), terrain.sector_centers);
    expect(barriers.sector_count() == terrain.sector_centers.size(), "Every non-root terrain node must supply its original sector");
    expect(barriers.segments().size() == 229, "Donington POINTS must expose all229 original static records");
    std::array<std::uint32_t, 4> kinds{};
    for (const auto& record : barriers.segments()) {
        ++kinds.at(record.kind);
    }
    expect(kinds == std::array<std::uint32_t, 4>{67, 144, 10, 8}, "Donington barrier kinds must retain their original identities");
    canonical_recovery(barriers);
    canonical_effects(barriers);
    const HpcSurface surface(keep.member(0).bytes());
    const auto start = scene::single_qualifying_start(keep.member(1).bytes());
    const auto initial_body = scene::initial_body_pose(surface, start, 0);
    const auto candidates = barriers.candidates(initial_body.position);
    expect(barriers.sector(initial_body.position).has_value() && !candidates.empty() && candidates.size() < barriers.segments().size(),
        "Real qualifying placement must use a finite original sector list rather than scanning every barrier");
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        expect(candidates[i] < barriers.segments().size() && (i == 0 || candidates[i - 1] < candidates[i]), "Canonical candidate IDs must retain bounded ascending source order");
    }
    SurfaceRoughness roughness;
    DamageContext damage(surface, &barriers);
    Motion motion(surface, initial_body, 0, true, 1, true, damage, roughness, &barriers);
    for (std::uint32_t tick = 1; tick <= initial_coast_ticks; ++tick) {
        motion.update({}, {});
        damage.advance();
        if (motion.state().boundary != MotionBoundary::none) {
            throw std::runtime_error("Qualifying coast phase left HPC coverage at tick " + std::to_string(tick));
        }
    }
    std::cout << "Initial coast ticks " << initial_coast_ticks << " position " << motion.state().body.position[0] << ' ' << motion.state().body.position[1] << ' '
              << motion.state().body.position[2] << " speed " << motion.state().speed_q16 << '\n';
    for (std::uint32_t tick = 1; tick <= 160; ++tick) {
        motion.update({.throttle = true, .left = true}, {});
        damage.advance();
        if (motion.state().boundary != MotionBoundary::none) {
            throw std::runtime_error("Qualifying barrier approach left real HPC coverage before contact at gas tick " + std::to_string(tick) + " total tick "
                                     + std::to_string(initial_coast_ticks + tick));
        }
        if (!motion.state().impacts.empty()) {
            const auto& event = motion.state().impacts.front();
            expect(event.kind >= 1 && event.kind <= 4 && motion.state().impacts.back().kind == 1 && event.strength_q16 > 0 && motion.state().impact_count > 0
                       && motion.state().barrier_contact_count > 0,
                "Real qualifying motion must publish a source barrier impulse and its audio event together");
            std::cout << "Donington qualifying gas+left impact tick " << initial_coast_ticks + tick << " gas tick " << tick << " segment " << event.barrier_index << " position "
                      << event.position_q16[0] << ' ' << event.position_q16[1] << ' ' << event.position_q16[2] << " strength " << event.strength_q16 << '\n';
            expect(std::ranges::any_of(motion.damage().levels,
                       [](auto level) {
                           return level > 0;
                       }),
                "The real qualifying wall impact must select original damaged body parts");
            const auto levels = motion.damage().levels;
            for (unsigned after = 0; after < 5; ++after) {
                motion.update({}, {});
                damage.advance();
                for (std::size_t part = 0; part < levels.size(); ++part) {
                    expect(motion.damage().levels[part] >= levels[part], "Driving after a wall impact must not heal damage");
                }
            }
            motion.reset_for_race(initial_body);
            damage.reset_parts();
            expect(motion.damage().levels == std::array<std::uint8_t, 17>{}
                       && std::ranges::none_of(damage.parts().parts(),
                           [](const auto& part) {
                               return part.active;
                           }),
                "Starting a new race must restore every original mesh and remove detached parts");
            return;
        }
    }
    throw std::runtime_error("The bounded qualifying gas+left approach did not reach a static Donington barrier");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            synthetic();
            sectors();
            recovery();
        } else if (argc == 2) {
            canonical(argv[1]);
        } else if (argc == 3) {
            const std::string_view text(argv[2]);
            std::uint32_t coast_ticks = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), coast_ticks);
            if (error != std::errc{} || end != text.data() + text.size() || coast_ticks > 100) {
                throw std::runtime_error("Initial coast ticks must be an integer from 0 to 100");
            }
            canonical(argv[1], coast_ticks);
        } else {
            throw std::runtime_error("Usage: vehicle-barrier-tests [canonical-root [initial-coast-ticks]]");
        }
        std::cout << "PASS: static barrier decoding, finite contact impulse and source impact event\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
