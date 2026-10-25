#include "frontend/championship_presentation.hpp"

#include "image/decode.hpp"

#include <stdexcept>

namespace toca::frontend {
namespace {
std::vector<std::string> lines(std::span<const std::uint8_t> bytes) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin < bytes.size()) {
        auto end = begin;
        while (end < bytes.size() && bytes[end] != '\r' && bytes[end] != '\n') {
            ++end;
        }
        if (end != begin) {
            result.emplace_back(bytes.begin() + std::ptrdiff_t(begin), bytes.begin() + std::ptrdiff_t(end));
        }
        begin = end;
        while (begin < bytes.size() && (bytes[begin] == '\r' || bytes[begin] == '\n')) {
            ++begin;
        }
    }
    if (result.size() <= 113) {
        throw std::runtime_error("Championship honors: original GSTRINGS labels missing");
    }
    return result;
}

std::string number(std::string text, std::uint32_t value) {
    const auto marker = text.find("%d");
    if (marker == std::string::npos || text.find('%', marker + 2) != std::string::npos) {
        throw std::runtime_error("Championship honors: invalid original number caption");
    }
    text.replace(marker, 2, std::to_string(value));
    return text;
}
} // namespace

Surface load_championship_honors_background(const AssetStore& store, std::uint32_t block, std::uint32_t width) {
    if (block >= 4 || (width != 640 && width != 512 && width != 320)) {
        throw std::runtime_error("Championship honors: invalid quarter or authored width");
    }
    const auto archive = store.open_archive("data/cscreens.bfp");
    const auto member = block * 3 + (width == 640 ? 0u : width == 512 ? 1u : 2u);
    auto surface = decode_bmp(archive.member(member).bytes());
    if (surface->w != int(width) || surface->h != int(width * 3 / 4)) {
        throw std::runtime_error("Championship honors: original background size mismatch");
    }
    return surface;
}

ChampionshipPresentationResources load_championship_presentation(const AssetStore& store) {
    const auto archive = store.open_archive("data/config.bfp");
    ChampionshipPresentationResources resources;
    for (std::uint32_t language = 0; language < 5; ++language) {
        const auto strings = lines(archive.member(2 + language).bytes());
        auto& labels = resources.honors[language];
        labels.title = strings[109];
        labels.firsts = strings[110];
        labels.seconds = strings[111];
        labels.thirds = strings[112];
        labels.no_award = strings[113];
        labels.tier = {"", strings[69], strings[71], strings[72]};
        labels.showdown_gold = strings[70];
        labels.secret_code = strings[73];
        labels.bonus_track = strings[74];
        labels.bonus_car = strings[75];
    }
    return resources;
}

std::vector<ChampionshipPresentationLine> championship_honors_lines(
    const ChampionshipPresentationResources& resources, const race::ChampionshipHonors& honors, std::uint32_t language) {
    if (language >= resources.honors.size() || honors.block >= 4 || honors.tier > 3) {
        throw std::runtime_error("Championship honors: invalid language, quarter or medal");
    }
    const auto& labels = resources.honors[language];
    std::vector<ChampionshipPresentationLine> output{{number(labels.title, honors.block + 1), 0xf5c, true}, {number(labels.firsts, honors.podiums[0]), 0x2e14, false},
        {number(labels.seconds, honors.podiums[1]), 0x4000, false}, {number(labels.thirds, honors.podiums[2]), 0x51eb, false}};
    if (honors.tier == 0 || (honors.showdown && honors.tier < 3)) {
        output.push_back({labels.no_award, 0xa000, false});
        return output;
    }
    output.push_back({number(honors.showdown ? labels.showdown_gold : labels.tier[honors.tier], honors.block + 1), 0x7333, true});
    for (std::size_t index = 0; index < honors.rewards.size(); ++index) {
        const auto& reward = honors.rewards[index];
        const auto text = reward.kind == race::ChampionshipRewardKind::code  ? "- " + labels.secret_code + " " + reward.name
                          : reward.kind == race::ChampionshipRewardKind::car ? "- " + reward.name + " " + labels.bonus_car
                                                                             : "- " + reward.name + " " + labels.bonus_track;
        // 64f58 returns the heading's bottom: authored BIG640 height36+3.
        // Keep the original pixel->Q16 truncation before source reward offsets.
        constexpr auto after_heading = ((std::uint32_t((std::uint64_t(0x7333) * 480) >> 16) + 39) << 16) / 480;
        constexpr std::array<std::uint32_t, 3> offsets{0xf5c, 0x23d6, 0x3850};
        output.push_back({text, after_heading + offsets.at(index), false});
    }
    return output;
}
} // namespace toca::frontend
