#include "assets/store.hpp"
#include "scene/pit_route.hpp"

#include <bit>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Bytes = std::vector<std::uint8_t>;
using Vector = std::array<std::int32_t, 3>;

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

void put32(Bytes& bytes, std::size_t position, std::uint32_t value) {
    put16(bytes, position, std::uint16_t(value));
    put16(bytes, position + 2, std::uint16_t(value >> 16));
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    auto crc = UINT32_C(0xffffffff);
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) != 0 ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

void synthetic() {
    Bytes bytes(4 + 2 * 64);
    put16(bytes, 0, 2);
    put16(bytes, 2, 0xfedc); // Ignored header word is not part of a 32-bit count.
    const std::array<std::uint32_t, 16> fields{
        0x80000000, 1, 0xffffffff, 0x7fffffff, 65537, 0xffff0001, 32769, 0x80000001, 17, 0xfffffffd, 0, 123456789, 236, 73, 0xffffabcd, 0x87654321};
    for (std::size_t index = 0; index < fields.size(); ++index) {
        put32(bytes, 4 + index * 4, fields[index]);
    }
    put32(bytes, 4 + 64 + 12, 987654321);
    const auto route = toca::scene::decode_pit_route(bytes);
    bytes.assign(bytes.size(), 0); // Decoded data must outlive the archive/member.
    expect(route.reserved == 0xfedc && route.points.size() == 2, "PITS must read a 16-bit count and retain the ignored header word");
    const auto& first = route.points.front();
    expect(first.left == Vector{INT32_MIN, 1, -1} && first.best == Vector{INT32_MAX, 65537, -65535} && first.over == Vector{32769, INT32_MIN + 1, 17}
               && first.right == Vector{-3, 0, 123456789},
        "PITS four named vectors must retain signed integer precision and source field order");
    expect(first.metadata == std::array<std::uint32_t, 4>{236, 73, 0xffffabcd, 0x87654321} && route.points.back().best[0] == 987654321,
        "PITS must retain independent raw metadata words and all owning records");

    bytes.assign(68, 0);
    put16(bytes, 0, 1);
    expect(toca::scene::decode_pit_route(bytes).points.size() == 1, "Data decoder must not invent a minimum gameplay route length");
    for (const auto size : {0u, 1u, 2u, 3u, 67u}) {
        rejects(
            [&] {
                (void)toca::scene::decode_pit_route(std::span(bytes).first(size));
            },
            "Truncated PITS header or record accepted");
    }
    bytes.push_back(0);
    rejects(
        [&] {
            (void)toca::scene::decode_pit_route(bytes);
        },
        "Unaccounted PITS bytes accepted");
    bytes.resize(68);
    for (const auto count : {0u, 2u, 0x8000u, 0xffffu}) {
        put16(bytes, 0, std::uint16_t(count));
        rejects(
            [&] {
                (void)toca::scene::decode_pit_route(bytes);
            },
            "Invalid signed PITS count or count beyond bounded records accepted");
    }
}

void canonical(const std::filesystem::path& root) {
    struct Fixture {
        const char* prefix;
        std::size_t count;
        std::uint32_t crc;
        Vector first_left, last_best;
        std::array<std::uint32_t, 4> first_metadata;
    };

    // KEEP member 2 only; original track order 0..9. No extracted game assets.
    constexpr std::array fixtures{
        Fixture{"donl", 34, 0xc8e3231d, {-13867470, 466050, -8113156}, {-33939271, 59491, -1419587}, {236, 236, 236, 236}},
        Fixture{"silv", 52, 0x9f01ef85, {-17318340, 3669731, 19873371}, {22696478, 3878799, 23918434}, {0, 0, 0, 0}},
        Fixture{"thru", 21, 0x9cde1ed4, {-35992298, 200365, -24274628}, {-40912795, 148416, -15218047}, {70, 70, 70, 70}},
        Fixture{"bran", 50, 0xc4c0d355, {-19025027, 194034, -3912369}, {2329458, 78622, 10859313}, {70, 70, 70, 70}},
        Fixture{"oult", 40, 0xb6a90fc8, {22564055, 1397003, -14509620}, {863695, 1168874, -15204910}, {70, 70, 70, 70}},
        Fixture{"dons", 26, 0x8f3102cd, {-13487384, 310081, -5825961}, {-33963374, 59862, -1409437}, {236, 236, 236, 236}},
        Fixture{"crof", 25, 0xb80cd63f, {3722440, 404283, -8388584}, {9760391, 199711, 9562865}, {70, 70, 70, 70}},
        Fixture{"knoc", 20, 0x891980c7, {-9542056, 362106, 12333838}, {2207133, 352741, 10143478}, {236, 236, 236, 236}},
        Fixture{"snet", 38, 0xf17ec791, {5898276, 898664, -9030802}, {-17526853, 1114216, -12155074}, {70, 73, 70, 70}},
        Fixture{"land", 30, 0x8385343a, {-12956603, -677199, -7313690}, {-32941047, 54605, -1715381}, {236, 236, 236, 236}},
    };
    const toca::AssetStore store(root);
    for (const auto& fixture : fixtures) {
        const auto archive = store.open_archive("data/" + std::string(fixture.prefix) + "keep.bfp");
        const auto member = archive.member(2);
        expect(crc32(member.bytes()) == fixture.crc, "Canonical PITS member CRC32 changed");
        const auto route = toca::scene::decode_pit_route(member.bytes());
        expect(route.points.size() == fixture.count && route.reserved == 0, "Canonical PITS count/header changed");
        expect(route.points.front().left == fixture.first_left && route.points.back().best == fixture.last_best, "Canonical PITS endpoint geometry changed");
        const auto last_value = std::string(fixture.prefix) == "thru" ? 70u : 100u;
        expect(
            route.points.front().metadata == fixture.first_metadata && route.points.back().metadata == std::array<std::uint32_t, 4>{last_value, last_value, last_value, last_value},
            "Canonical PITS raw metadata changed");
        std::cout << fixture.prefix << " PITS records " << fixture.count << " CRC32 " << std::hex << std::setw(8) << std::setfill('0') << fixture.crc << std::dec << '\n';
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
            throw std::runtime_error("Usage: scene-pit-route-tests [canonical-root]");
        }
        std::cout << "PASS: owning original PITS header, named fixed vectors and raw metadata\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
