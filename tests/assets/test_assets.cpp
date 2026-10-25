#include "assets/store.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

namespace {
void require(bool valid, const char* reason) {
    if (!valid) {
        throw std::runtime_error(reason);
    }
}

template <class Function> void rejects(Function function, const char* reason) {
    bool rejected = false;
    try {
        function();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, reason);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.at(offset + shift / 8) = static_cast<std::uint8_t>(value >> shift);
    }
}

std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> bytes(65);
    put_u32(bytes, 0, 2);
    put_u32(bytes, 4, 48);
    put_u32(bytes, 8, 3);
    const std::string name = "..\\same.bmp";
    for (const std::size_t position : {12, 36}) {
        std::copy(name.begin(), name.end(), bytes.begin() + static_cast<std::ptrdiff_t>(position));
        bytes[position + 15] = 0xa5;
    }
    put_u32(bytes, 28, 0);
    put_u32(bytes, 32, 3);
    put_u32(bytes, 52, 3);
    put_u32(bytes, 56, 2);
    const std::string payload = "abcde";
    std::copy(payload.begin(), payload.end(), bytes.begin() + 60);
    return bytes;
}

std::string text(std::span<const std::uint8_t> bytes) {
    return {bytes.begin(), bytes.end()};
}

struct PrivateDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("toca-assets-" + std::to_string(std::random_device{}()));

    PrivateDirectory() {
        require(std::filesystem::create_directory(path), "Cannot create private asset-test directory");
    }

    ~PrivateDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(bool(output), "Cannot write synthetic asset fixture");
}

void scoped_archives(const toca::AssetStore& store, const std::filesystem::path& root) {
    auto bytes = fixture();
    write(root / "Data/SESSION.BFP", bytes);
    std::optional<toca::BfpMember> retained;
    {
        const auto loading = store.for_loading();
        const auto first = loading.open_archive("data/session.bfp");
        const auto second = loading.open_archive("DATA\\SESSION.BFP");
        require(first.entries().data() == second.entries().data() && first.member(0).bytes().data() == second.member(0).bytes().data(),
            "One loading store must share archive directory and payload storage across path spellings");
        auto first_cursor = first.cursor(), second_cursor = second.cursor();
        require(first_cursor.skip() && first_cursor.read()->entry().index == 1 && second_cursor.read()->entry().index == 0,
            "Shared archive handles must retain independent cursor positions");
        retained = second_cursor.read();
        bytes[60] = 'X';
        write(root / "Data/SESSION.BFP", bytes);
        require(loading.read_file("data/session.bfp") == bytes && text(loading.open_archive("data/session.bfp").member(0).bytes()) == "abc",
            "Scoped archive snapshots must not cache ordinary read_file results");
        for (const auto path : {"Data/../Data/SESSION.BFP", "Data//SESSION.BFP", "Data/SESSION.BFP/", "/Data/SESSION.BFP"}) {
            rejects(
                [&] {
                    (void)loading.open_archive(path);
                },
                "A cached archive must not bypass relative-path validation");
        }
        const auto ordinary = store.open_archive("data/session.bfp");
        require(text(ordinary.member(0).bytes()) == "Xbc" && ordinary.entries().data() != first.entries().data(), "A loading cache must not affect its source store");
        const auto independent = loading.for_loading().open_archive("data/session.bfp");
        require(text(independent.member(0).bytes()) == "Xbc", "Each new loading store must start with an empty archive snapshot");
    }
    require(retained && text(retained->bytes()) == "de", "Explicitly retained members must survive loading-store destruction");
    const auto after = store.for_loading().open_archive("data/session.bfp");
    require(text(after.member(0).bytes()) == "Xbc", "A later loading store must observe fresh archive bytes");
    const auto ordinary = store.open_archive("data/session.bfp");
    const auto another = store.open_archive("data/session.bfp");
    require(ordinary.entries().data() != another.entries().data(), "Ordinary archive opens must remain independent");

    {
        const auto loading = store.for_loading();
        write(root / "Data/BROKEN.BFP", std::span(bytes).first(11));
        rejects(
            [&] {
                (void)loading.open_archive("data/broken.bfp");
            },
            "A loading store must reject malformed BFP data");
        write(root / "Data/BROKEN.BFP", bytes);
        require(loading.open_archive("data/broken.bfp").size() == 2, "Failed parsing must not leave an unusable cached entry");
    }
    rejects(
        [&] {
            const auto loading = store.for_loading();
            (void)loading.open_archive("data/session.bfp");
            throw std::runtime_error("Synthetic caller failure during loading");
        },
        "Synthetic loading failure must unwind the temporary store");
    bytes[60] = 'Y';
    write(root / "Data/SESSION.BFP", bytes);
    require(text(store.for_loading().open_archive("data/session.bfp").member(0).bytes()) == "Ybc", "An exception must not preserve a later load's archive snapshot");
}

void bundled_assets() {
    PrivateDirectory directory;
    const auto root = directory.path / "game", bundled = directory.path / "bundled";
    const auto original = fixture();
    auto replacement = original;
    replacement[60] = 'X';
    write(root / "Data/SHARED.BFP", original);
    write(root / "Data/LOCAL.BFP", original);
    write(bundled / "data/shared.bfp", replacement);
    write(bundled / "data/bundled.bfp", replacement);
    const toca::AssetStore store(root, bundled);
    require(store.root() == std::filesystem::canonical(root), "A bundled overlay must preserve the main data root identity");
    require(store.find("DATA\\SHARED.BFP") == std::filesystem::canonical(bundled / "data/shared.bfp") && store.read_file("data/shared.bfp") == replacement,
        "Bundled assets must take precedence with independent ASCII-insensitive path resolution");
    require(store.resolve("data/local.bfp") == std::filesystem::canonical(root / "Data/LOCAL.BFP") && store.read_file("data/local.bfp") == original,
        "Assets absent from the bundle must resolve from the main root");
    require(store.read_file("data/bundled.bfp") == replacement && !store.find("data/missing.bfp"), "Bundles must supply files missing from the main root");
    require(text(store.open_archive("data/shared.bfp").member(0).bytes()) == "Xbc", "Archive loading must use the bundled override");
    const auto loading = store.for_loading();
    const auto shared = loading.open_archive("data/shared.bfp");
    require(shared.entries().data() == loading.open_archive("DATA/SHARED.BFP").entries().data(), "Loading stores must cache bundled archives across case spellings");
    require(text(loading.for_loading().open_archive("data/shared.bfp").member(0).bytes()) == "Xbc" && text(loading.open_archive("data/local.bfp").member(0).bytes()) == "abc",
        "Nested loading stores must inherit bundle priority and main-root fallback");
    write(bundled / "data/shared.bfp", std::span(replacement).first(11));
    rejects(
        [&] {
            (void)store.open_archive("data/shared.bfp");
        },
        "Invalid bundled archives must fail instead of silently selecting a local copy");
    write(bundled / "data/shared.bfp", replacement);
    for (const auto& selected_root : {root / "Data", bundled / "data"}) {
        write(selected_root / "Collision", original);
        write(selected_root / "collision", original);
        if (!std::filesystem::equivalent(selected_root / "Collision", selected_root / "collision")) {
            rejects(
                [&] {
                    (void)store.find("data/COLLISION");
                },
                "Each asset root must reject ambiguous ASCII case matches");
        }
        std::filesystem::remove(selected_root / "Collision");
        std::filesystem::remove(selected_root / "collision");
    }
    rejects(
        [&] {
            (void)loading.find("data/../shared.bfp");
        },
        "Bundled loading stores must retain complete relative-path validation");
#ifndef _WIN32
    // The other trusted root is still outside this root's own boundary.
    std::filesystem::create_directory_symlink(root / "Data", bundled / "escape");
    write(root / "escape/LOCAL.BFP", original);
    rejects(
        [&] {
            (void)store.find("escape/LOCAL.BFP");
        },
        "An escaping bundle symlink must fail before main-root fallback");
    std::filesystem::create_directory_symlink(bundled / "data", root / "local-escape");
    rejects(
        [&] {
            (void)store.find("local-escape/bundled.bfp");
        },
        "Main-root fallback must enforce its own symlink boundary");
#endif
}

void synthetic() {
    using namespace toca;
    const BfpArchive archive(fixture());
    require(archive.size() == 2 && archive.largest_member_size() == 3, "BFP header fields differ");
    require(archive.entries()[0].raw_name == archive.entries()[1].raw_name && archive.entries()[0].raw_name.back() == 0xa5, "Raw16 names must remain exact metadata");
    require(archive.entries()[1].index == 1 && archive.entries()[1].payload_offset == 3 && archive.entries()[1].size == 2, "Directory index/offset/size identity differs");
    require(text(archive.member(0).bytes()) == "abc" && text(archive.member(1).bytes()) == "de", "Indexed payload spans differ");
    rejects(
        [&] {
            (void)archive.member(2);
        },
        "Out-of-range member index must fail");
    auto cursor = BfpArchive(fixture()).cursor();
    require(cursor.position() == 0 && cursor.remaining() == 2 && !cursor.eof(), "New cursor must start at the first member");
    const auto first = cursor.read();
    const auto second = cursor.read();
    require(first && second && text(first->bytes()) == "abc" && text(second->bytes()) == "de", "Sequential reads must retain independent stable spans");
    require(cursor.position() == 2 && cursor.remaining() == 0 && cursor.eof() && !cursor.read() && !cursor.skip(), "EOF must not advance or read beyond the directory");
    cursor.close();
    require(text(first->bytes()) == "abc" && text(second->bytes()) == "de", "Members must survive cursor close and archive destruction");
    require(cursor.eof() && !cursor.read() && !cursor.skip(), "Closed cursors must not read or advance");
    auto skipped = archive.cursor();
    require(skipped.skip() && skipped.position() == 1 && skipped.read()->entry().index == 1, "Skip must consume exactly one indexed member");
    const BfpArchive empty(std::vector<std::uint8_t>(12));
    require(empty.size() == 0 && empty.cursor().eof(), "Empty archive must have a clean EOF");
    auto zero_size = fixture();
    put_u32(zero_size, 32, 0);
    put_u32(zero_size, 52, 0);
    put_u32(zero_size, 8, 2);
    zero_size.erase(zero_size.begin() + 60, zero_size.begin() + 63);
    const BfpArchive zero_archive(std::move(zero_size));
    require(zero_archive.member(0).bytes().empty() && text(zero_archive.member(1).bytes()) == "de", "Zero-length members must retain index order");
    for (const std::size_t size : {0, 11, 30, 60, 64}) {
        auto bytes = fixture();
        bytes.resize(size);
        rejects(
            [&] {
                (void)BfpArchive(bytes);
            },
            "Truncated BFP data must fail");
    }
    for (const auto& [position, value] :
        std::array<std::pair<std::size_t, std::uint32_t>, 8>{{{4, 24}, {8, 999}, {28, 1}, {32, 500}, {52, 1}, {0, 0xffffffff}, {4, 0xfffffff0}, {56, 0xffffffff}}}) {
        auto bytes = fixture();
        put_u32(bytes, position, value);
        rejects(
            [&] {
                (void)BfpArchive(bytes);
            },
            "Invalid BFP header/member bounds must fail");
    }
    auto trailing = fixture();
    trailing.push_back(0);
    rejects(
        [&] {
            (void)BfpArchive(trailing);
        },
        "Unaccounted payload tails must fail");

    PrivateDirectory directory;
    const auto root = directory.path / "game";
    const auto bytes = fixture();
    write(root / "Data/SubDir/Example.BIN", bytes);
    write(root / "Data/ARCHIVE.BFP", bytes);
    const AssetStore store(root);
    require(store.read_file("dAtA\\sUbDiR/eXaMpLe.BiN") == bytes, "Each path component must resolve using ASCII case folding");
    require(store.find("dAtA\\sUbDiR/eXaMpLe.BiN") == store.resolve("Data/SubDir/Example.BIN"), "Optional lookup must preserve ASCII-insensitive resolution");
    require(!store.find("Data/missing.ogg") && !store.find("missing/voice.ogg"), "Optional lookup must report missing files and directories without an exception");
    for (const auto path : {"../missing.ogg", "missing/../voice.ogg", "missing//voice.ogg", "Data/"}) {
        rejects(
            [&] {
                (void)store.find(path);
            },
            "Optional lookup must validate the complete path before checking existence");
    }
    require(store.open_archive("data/archive.bfp").size() == 2, "Native archives must load directly from the root");
    auto private_copy = store.read_file("data/archive.bfp");
    private_copy[0] = 0;
    require(store.read_file("data/archive.bfp") == bytes, "Consumer byte ownership must not write game data");
    for (const auto path :
        {"", "/Data/ARCHIVE.BFP", "C:\\Data\\ARCHIVE.BFP", "../Data/ARCHIVE.BFP", "Data/../Data/ARCHIVE.BFP", "Data//ARCHIVE.BFP", "Data/", "Data/missing.bin"}) {
        rejects(
            [&] {
                (void)store.read_file(path);
            },
            "Invalid or missing asset paths must fail");
    }
    rejects(
        [&] {
            (void)store.read_file("Data");
        },
        "Directory assets must not be read as files");
    write(root / "Data/Collision", bytes);
    write(root / "Data/collision", bytes);
    if (!std::filesystem::equivalent(root / "Data/Collision", root / "Data/collision")) {
        rejects(
            [&] {
                (void)store.read_file("Data/COLLISION");
            },
            "Ambiguous ASCII case matches must fail consistently");
        rejects(
            [&] {
                (void)store.find("Data/COLLISION");
            },
            "Optional lookup must not hide ambiguous paths");
    }
#ifndef _WIN32
    write(directory.path / "outside/secret.bin", bytes);
    std::filesystem::create_directory_symlink(directory.path / "outside", root / "escape");
    rejects(
        [&] {
            (void)store.read_file("escape/secret.bin");
        },
        "Symlinks must not escape the read-only root");
    rejects(
        [&] {
            (void)store.find("escape/voice.ogg");
        },
        "Optional lookup must reject an escaping parent even when the final file is absent");
#endif
    scoped_archives(store, root);
    bundled_assets();
    std::cout << "Synthetic BFP ownership/cursors/bounds and read-only asset paths passed\n";
}

void corpus(const std::filesystem::path& root) {
    const toca::AssetStore store(root);
    std::size_t archives = 0, members = 0;
    for (const auto& file : std::filesystem::directory_iterator(root / "data")) {
        if (file.path().extension() != ".bfp") {
            continue;
        }
        const auto name = "data/" + file.path().filename().string();
        const auto bytes = store.read_file(name);
        const auto word = [&](std::size_t offset) {
            require(offset <= bytes.size() && bytes.size() - offset >= 4, "Bundled BFP field exceeds archive");
            return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) | (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
        };
        const auto count = word(0), table_bytes = word(4), largest = word(8);
        const auto archive = store.open_archive(name);
        require(archive.size() == count && table_bytes == count * 24 && archive.largest_member_size() == largest, "Bundled BFP header differs from native archive metadata");
        auto cursor = archive.cursor();
        std::uint32_t maximum_size = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const auto table = 12 + index * 24;
            const auto offset = word(table + 16), size = word(table + 20);
            const auto payload = std::uint64_t(12) + table_bytes + offset;
            require(payload <= bytes.size() && size <= bytes.size() - payload, "Bundled BFP payload exceeds archive");
            const auto member = archive.member(index);
            require(member.entry().index == index && member.entry().payload_offset == offset && member.entry().size == size,
                "Bundled BFP index, offset or size differs from original table");
            require(std::equal(member.entry().raw_name.begin(), member.entry().raw_name.end(), bytes.begin() + std::ptrdiff_t(table)),
                "Bundled BFP raw member name differs from original table");
            const auto raw = std::span(bytes).subspan(std::size_t(payload), size);
            require(member.bytes().size() == raw.size() && std::equal(raw.begin(), raw.end(), member.bytes().begin()),
                "Bundled BFP payload differs from loose immutable archive bytes");
            const auto next = cursor.read();
            require(next && next->entry().index == index && next->bytes().size() == raw.size() && std::equal(raw.begin(), raw.end(), next->bytes().begin()),
                "Bundled BFP cursor must preserve every indexed payload in order");
            maximum_size = std::max(maximum_size, size);
            ++members;
        }
        require(cursor.eof() && !cursor.read() && maximum_size == largest, "Bundled BFP cursor EOF or maximum member size differs");
        ++archives;
    }
    require(archives == 48 && members == 504, "Approved bundled BFP corpus coverage changed");
    std::cout << "Native corpus matched504 indexed members and raw table/payload bytes in48 approved BFP archives\n";
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 1) {
            synthetic();
        } else if (argc == 2) {
            corpus(argv[1]);
        } else {
            throw std::runtime_error("Usage: toca-assets-tests [bundled-game-data-root]");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
