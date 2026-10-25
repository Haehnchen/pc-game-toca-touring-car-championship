#include "scene/flag_animation.hpp"

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
        using namespace toca;
        using namespace toca::scene;
        constexpr std::array<float, 16> unit{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        PropResources flags;
        flags.model.nodes = {{-1, unit, 10, {}}, {0, unit, 1, {0, 1, 2}}, {0, unit, 5, {3}}, {-1, unit, 20, {}}, {3, unit, 1, {4, 5, 6}}, {-1, unit, 30, {}}};
        flags.actors = {{0, 1}, {3, 4}, {5, UINT32_MAX}};
        SourceRandom random(7), expected(7);
        const auto first = std::uint8_t(expected.next() % 3), second = std::uint8_t(expected.next() % 3);
        (void)expected.next(); // 45b880 also draws when80ce0 finds noID1 node.
        FlagAnimation animation(flags, random);
        expect(animation.actor_count() == 3 && animation.first_frame() == first && animation.variants()[1] == first && animation.variants()[4] == second
                   && animation.variants()[2] == 0 && animation.updates() == 0,
            "45b880 must seed each registered flag once while retaining its pole's initial mesh");
        expect(random.next() == expected.next(), "Flag initialization must consume the exact shared source random stream, including missing animation targets");
        for (unsigned tick = 1; tick <= 12; ++tick) {
            animation.update();
            const auto step = tick / 4;
            expect(animation.variants()[1] == (first + step) % 3 && animation.variants()[4] == (second + step) % 3 && animation.variants()[2] == 0 && animation.updates() == tick,
                "45b8c0 must hold each frame for four source updates, cycle all three frames and keep the pole unchanged");
            (void)animation.variants();
            (void)animation.variants();
            expect(animation.updates() == tick, "Presentation reads must never advance a flag countdown");
        }
        expect(random.next() == expected.next(), "Flag updates must consume no additional random draws");
        SourceRandom empty_random(17), empty_expected(17);
        FlagAnimation empty(PropResources{}, empty_random);
        expect(empty.variants().empty() && empty.actor_count() == 0 && empty_random.next() == empty_expected.next(),
            "An empty flag resource must allocate no controllers or random draws");
        std::cout << "PASS: original flag RNG initialization, four-tick mesh cycle, independent actors and unchanged pole\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
