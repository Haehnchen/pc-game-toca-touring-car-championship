#include "core/random.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        // Original CRT vectors include repeated 32-bit state overflow.
        toca::SourceRandom seeded(1), wrap(0xffffffffu);
        for (const auto expected : std::array{41u, 18467u, 6334u, 26500u, 19169u, 15724u, 11478u, 29358u}) {
            if (seeded.next() != expected) {
                throw std::runtime_error("Original seeded random sequence changed");
            }
        }
        for (const auto expected : std::array{35u, 29739u, 3374u, 11141u, 31308u, 7870u, 5253u, 2445u}) {
            if (wrap.next() != expected) {
                throw std::runtime_error("Original unsigned random wrap changed");
            }
        }
        std::cout << "PASS: original random sequences and independent explicit seeds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
