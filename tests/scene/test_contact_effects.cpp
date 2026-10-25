#include "scene/contact_effects.hpp"
#include "simulation/fixed.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace toca::scene;
        using namespace toca::simulation::fixed;
        PropFragmentTables tables;
        tables.positive.fill(one / 2);
        tables.signed_values.fill(one / 4);
        tables.counts.fill(2);
        ContactSparks sparks(tables);
        ScrapeSparkRequest request{{0, 10 * one, 4 * one}, {one, 0, 0}, {0, 0, -1000}, one, false};
        sparks.emit(request);
        expect(sparks.size() == 2 && tables.cursor == 2, "418570 wall sparks must use count at current cursor and one entry per streak");
        const auto first = sparks.lines().front();
        expect(sparks.presentation_lines(0).front().from == first.from && sparks.presentation_lines(0).front().to == first.to,
            "Spark births must initialize record-local presentation geometry");
        const auto direction = normalized(Vector{one, mul(one / 4, 0x4ccc), 0});
        expect(first.from == request.position && first.to == add(request.position, scale(direction, 0x3333)) && first.color == 0xfffff096,
            "Original spark length0x3333, direction normalization and RGB255/240/150 must survive");
        sparks.update();
        expect(sparks.lines().front().from == add(first.from, add(scale(direction, one / 2), request.velocity)),
            "418430 must translate both endpoints by emission velocity without gravity or acceleration");
        const auto moved = sparks.lines().front();
        const auto presentation_cursor = tables.cursor;
        for (const auto fps : {60u, 144u}) {
            for (std::uint32_t sample = 0; sample * 25 < fps; ++sample) {
                const auto alpha = float(sample * 25) / float(fps);
                const auto rendered = sparks.presentation_lines(alpha).front();
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    expect(std::abs(double(rendered.from[axis]) - (double(first.from[axis]) + double(moved.from[axis] - first.from[axis]) * alpha)) <= 1
                               && std::abs(double(rendered.to[axis]) - (double(first.to[axis]) + double(moved.to[axis] - first.to[axis]) * alpha)) <= 1,
                        "Both spark endpoints must move continuously between25Hz updates at60/144Hz");
                }
            }
        }
        expect(sparks.lines().front().from == moved.from && tables.cursor == presentation_cursor && sparks.size() == 2,
            "Spark presentation must preserve current geometry, lifetime and random cursor");
        for (unsigned tick = 1; tick < 6; ++tick) {
            sparks.update();
        }
        expect(sparks.size() == 2, "Original streaks must advance six ticks before retiring");
        sparks.update();
        expect(sparks.size() == 0, "Original streak lifetime must release every expired record");
        expect(sparks.presentation_lines(.5f).empty(), "Retired spark records must not survive in presentation");
        request.ground = true;
        tables.cursor = 36;
        sparks.emit(request);
        expect(sparks.size() == 2 && tables.cursor == 3 && sparks.lines().front().to[1] > request.position[1],
            "Ground sparks must consume two entries with source wrap and positiveY scatter");
        tables.counts.fill(10);
        for (unsigned burst = 0; burst < 7; ++burst) {
            sparks.emit(request);
        }
        expect(sparks.size() == 64, "Spark ring must remain independent of the63-triangle pool and cap at64");
        expect(sparks.lines().empty(), "418920 must preserve the original full-ring head/tail export behavior");
        expect(sparks.presentation_lines(.5f).empty(), "Interpolated spark presentation must preserve the full-ring export quirk");
        const auto cursor = tables.cursor;
        sparks.emit(request);
        expect(tables.cursor == cursor, "Full spark pool must not consume cyclic entries");
        sparks.clear();
        expect(sparks.size() == 0 && tables.cursor == cursor, "Scene reset must clear sparks while retaining shared table phase");
        request.position[0] = 100 * one;
        sparks.emit(request);
        expect(sparks.presentation_lines(0).front().from == request.position, "Spark pool reuse must never connect the previous object's geometry");
        std::cout << "PASS: original wall/ground spark requests, cyclic order, straight flight, pool, lifetime and reset\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
