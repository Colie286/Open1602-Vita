// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/high_scores.hpp"

#include <algorithm>
#include <charconv>

namespace o1602::assets {

namespace {

constexpr std::uint32_t kNoCampaign = 0xFF;  // also: no mission number

/// The numbers of a line after its key, separated by blanks.
std::vector<std::int64_t> numbers(std::string_view s) {
    std::vector<std::int64_t> out;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const std::size_t a = s.find_first_of("-0123456789", pos);
        if (a == std::string_view::npos) {
            break;
        }
        std::int64_t v = 0;
        const auto [end, ec] = std::from_chars(s.data() + a, s.data() + s.size(), v);
        if (ec != std::errc{}) {
            break;
        }
        out.push_back(v);
        pos = static_cast<std::size_t>(end - s.data());
    }
    return out;
}

}  // namespace

std::string write_high_scores(const std::vector<HighScore>& scores) {
    // The layout of HighScore_Write (0x0048E7A0).
    std::string out = "\n  Objekt:   HISCORE\n\n";
    for (std::size_t i = 0; i < std::max(scores.size(), kHighScoreLines); ++i) {
        const HighScore h = i < scores.size() ? scores[i] : HighScore{};
        std::string name = h.name.substr(0, kHighScoreNameLength);
        std::replace(name.begin(), name.end(), '"', '\'');  // the reader stops at the next quote
        // A line break would split the entry; other control characters have no place in a name.
        std::replace_if(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }, ' ');
        // "%2d": the index right-aligned in two places.
        const std::string index = (i < 10 ? " " : "") + std::to_string(i);
        out += "    Entry:   ";
        out += index;
        out += ", \"";
        out += name;
        out += "\", ";
        out += std::to_string(h.points);
        out += ", ";
        out += std::to_string(h.stars);
        out += ", ";
        out += std::to_string(h.game_id);
        out += "\n";
    }
    out += "  \n  EndObj;\n\n";
    return out;
}

std::optional<std::size_t> insert_high_score(std::vector<HighScore>& scores, HighScore entry) {
    // Fix S-115: the original copies the name into 32 bytes unchecked.
    entry.name.resize(std::min(entry.name.size(), kHighScoreNameLength));
    if (scores.size() < kHighScoreLines) {
        scores.resize(kHighScoreLines);
    }
    const auto same = [&](const HighScore& h) { return h.game_id == entry.game_id && h.name == entry.name; };
    // HighScore_Insert (0x0048E540): above the first line with as many points or fewer; a better
    // line of the same game and player stays.
    std::size_t at = 0;
    while (at < kHighScoreLines && (scores[at].name.empty() ? 0 : scores[at].points) > entry.points) {
        if (same(scores[at])) {
            return at;
        }
        ++at;
    }
    if (at >= kHighScoreLines) {
        return std::nullopt;
    }
    // The lines below move down by one, up to a worse line of the same game and player or the last.
    std::size_t end = kHighScoreLines - 1;
    for (std::size_t j = kHighScoreLines; j-- > at;) {
        if (same(scores[j])) {
            end = j;
            break;
        }
    }
    std::move_backward(scores.begin() + static_cast<std::ptrdiff_t>(at),
                       scores.begin() + static_cast<std::ptrdiff_t>(end),
                       scores.begin() + static_cast<std::ptrdiff_t>(end) + 1);
    scores[at] = std::move(entry);
    scores.resize(kHighScoreLines);
    return at;
}

bool CampaignProgress::record_win(std::uint32_t campaign, std::uint32_t part, std::uint32_t mission_number) {
    // GameDat_Write (0x00487FB0): the numbered missions first, else the campaign.
    if (mission_number != kNoCampaign && numbered <= mission_number) {
        const std::uint32_t next = std::min(mission_number + 1, kLastNumbered);
        const bool changed = next != numbered;
        numbered = next;
        return changed;
    }
    if (campaign != kNoCampaign) {
        std::uint32_t& next = campaigns[campaign];
        if (part + 1 > next) {
            next = part + 1;
            return true;
        }
    }
    return false;
}

bool CampaignProgress::ends_campaigns(std::uint32_t campaign, std::uint32_t part,
                                      std::uint32_t mission_number) const noexcept {
    // GameDat_Write (0x00487FB0): in the campaign branch, campaign 5 with n = part + 1 > 3.
    constexpr std::uint32_t kLastCampaign = 5;
    constexpr std::uint32_t kLastParts = 3;
    const bool numbered_rule = mission_number != kNoCampaign && numbered <= mission_number;
    return !numbered_rule && campaign == kLastCampaign && part + 1 > kLastParts;
}

CampaignProgress parse_progress(std::string_view text) {
    CampaignProgress p;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] == '#') {
            continue;
        }
        line.remove_prefix(first);
        const std::vector<std::int64_t> v = numbers(line);
        const auto valid = [](std::int64_t x) { return x >= 0 && x <= 0xFFFF; };
        if (line.starts_with("campaign") && v.size() >= 2 && valid(v[0]) && valid(v[1])) {
            p.campaigns[static_cast<std::uint32_t>(v[0])] = static_cast<std::uint32_t>(v[1]);
        } else if (line.starts_with("numbered") && !v.empty()) {
            p.numbered = static_cast<std::uint32_t>(
                std::clamp<std::int64_t>(v[0], CampaignProgress::kFirstNumbered, CampaignProgress::kLastNumbered));
        }
    }
    return p;
}

std::string write_progress(const CampaignProgress& progress) {
    std::string out = "# Open1602: missions opened by won games (the original keeps this in Game.dat)\n";
    out += "numbered " + std::to_string(progress.numbered) + "\n";
    for (const auto& [campaign, next] : progress.campaigns) {
        out += "campaign " + std::to_string(campaign) + " " + std::to_string(next) + "\n";
    }
    return out;
}

}  // namespace o1602::assets
