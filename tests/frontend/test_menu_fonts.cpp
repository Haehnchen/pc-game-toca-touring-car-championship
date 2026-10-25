#include "frontend/font.hpp"
#include "frontend/menu_transition.hpp"
#include "frontend/menus.hpp"
#include "menu_test_support.hpp"

#include <algorithm>
#include <cmath>

namespace {
using namespace toca::frontend::test;

void u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.at(offset + shift / 8) = static_cast<std::uint8_t>(value >> shift);
    }
}

std::vector<std::uint8_t> font_fixture() {
    std::vector<std::uint8_t> bytes(0x436 + 16 + 24 + 32);
    bytes[0] = 'B';
    bytes[1] = 'M';
    u32(bytes, 2, static_cast<std::uint32_t>(bytes.size()));
    u32(bytes, 10, 0x436);
    u32(bytes, 14, 40);
    u32(bytes, 18, 4);
    u32(bytes, 22, 4);
    bytes[26] = 1;
    bytes[28] = 8;
    const std::size_t appendix = 0x436 + 16;
    bytes[appendix + 1] = 2;
    u32(bytes, appendix + 4, 3);
    u32(bytes, appendix + 8, 2);
    u32(bytes, appendix + 12, 1);
    for (std::size_t index = 0; index < 2; ++index) {
        const auto position = appendix + 24 + index * 16;
        bytes[position] = static_cast<std::uint8_t>('A' + index);
        bytes[position + 2] = static_cast<std::uint8_t>(index * 2);
        bytes[position + 6] = 2;
        bytes[position + 8] = 3;
        for (std::size_t field = 0; field < 6; ++field) {
            bytes[position + 10 + field] = 2;
        }
    }
    return bytes;
}

void font_checks() {
    using namespace toca::frontend;
    const auto bytes = font_fixture();
    const auto font = decode_font(bytes);
    require(font.glyphs.size() == 2 && font.text_width("AB A") == 11 && font.pair_spacing('A', 'B') == 2, "Font dimensions and signed three-band spacing differ");
    require(font.pair_spacing('A', ' ') == 0 && font.find('Z') == nullptr, "Spaces/missing glyphs must follow source lookup semantics");
    for (const std::size_t length : {54, 0x436 + 16 + 23, 0x436 + 16 + 24 + 31}) {
        auto truncated = bytes;
        truncated.resize(length);
        rejects(
            [&] {
                (void)decode_font(truncated);
            },
            "Truncated font appendix must fail");
    }
    auto invalid = bytes;
    invalid[0x436 + 16 + 24 + 2] = 4;
    rejects(
        [&] {
            (void)decode_font(invalid);
        },
        "Glyph rectangles outside the atlas must fail");
}

} // namespace

int main() {
    return toca::frontend::test::run([] {
        font_checks();
    });
}
