// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/scenario_data.hpp"

#include <algorithm>

namespace o1602::sim {

namespace {

bool ascii_letter(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool ascii_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

/// The texts of one list: valid languages, each once, texts within `max_length`.
void check_texts(const std::vector<ScenarioText>& texts, std::string_view what, std::size_t max_length, bool lines,
                 std::vector<std::string>& problems) {
    if (texts.size() > ScenarioInfo::kMaxLanguages) {
        problems.push_back(std::string(what) + ": more than " + std::to_string(ScenarioInfo::kMaxLanguages) +
                           " languages");
    }
    for (std::size_t i = 0; i < texts.size(); ++i) {
        const ScenarioText& t = texts[i];
        if (!valid_language_code(t.language)) {
            problems.push_back(std::string(what) + ": \"" + t.language + "\" is no language code");
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (texts[j].language == t.language) {
                problems.push_back(std::string(what) + ": language " + t.language + " twice");
                break;
            }
        }
        if (t.text.size() > max_length) {
            problems.push_back(std::string(what) + " (" + t.language + "): longer than " + std::to_string(max_length) +
                               " bytes");
        } else if (!valid_scenario_text(t.text, lines)) {
            problems.push_back(std::string(what) + " (" + t.language + "): not UTF-8 or a control character");
        }
    }
}

}  // namespace

bool valid_language_code(std::string_view code) noexcept {
    if (code.size() < 2 || code.size() > ScenarioInfo::kMaxLanguageLength || !ascii_letter(code.front())) {
        return false;
    }
    return std::all_of(code.begin(), code.end(), [](char c) { return ascii_letter(c) || ascii_digit(c) || c == '-'; });
}

bool valid_marker_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > ScenarioMarker::kMaxNameLength) {
        return false;
    }
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || ascii_digit(c) || c == '_'; });
}

bool valid_scenario_text(std::string_view text, bool lines) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c == 0x7F || (c < 0x20 && !(lines && (c == '\n' || c == '\t')))) {
                return false;
            }
            ++i;
            continue;
        }
        std::size_t follow = 0;
        std::uint32_t code = 0;
        if ((c & 0xE0U) == 0xC0U) {
            follow = 1;
            code = c & 0x1FU;
        } else if ((c & 0xF0U) == 0xE0U) {
            follow = 2;
            code = c & 0x0FU;
        } else if ((c & 0xF8U) == 0xF0U) {
            follow = 3;
            code = c & 0x07U;
        } else {
            return false;
        }
        if (follow > text.size() - 1 - i) {
            return false;  // cut off
        }
        for (std::size_t k = 1; k <= follow; ++k) {
            const auto f = static_cast<unsigned char>(text[i + k]);
            if ((f & 0xC0U) != 0x80U) {
                return false;
            }
            code = code << 6 | (f & 0x3FU);
        }
        // Overlong forms, surrogates, beyond Unicode, and the C1 controls.
        static constexpr std::uint32_t kMin[] = {0, 0x80, 0x800, 0x10000};
        if (code < kMin[follow] || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF ||
            (code >= 0x80 && code < 0xA0)) {
            return false;
        }
        i += follow + 1;
    }
    return true;
}

std::vector<std::string> check_scenario_info(const ScenarioInfo& info) {
    std::vector<std::string> problems;
    check_texts(info.names, "name", ScenarioInfo::kMaxNameLength, false, problems);
    check_texts(info.briefings, "briefing", ScenarioInfo::kMaxBriefingLength, true, problems);
    if (info.stars > ScenarioInfo::kMaxStars) {
        problems.push_back("more than " + std::to_string(ScenarioInfo::kMaxStars) + " stars");
    }
    if (info.author.size() > ScenarioInfo::kMaxAuthorLength || !valid_scenario_text(info.author, false)) {
        problems.push_back("the author is too long, not UTF-8 or has a control character");
    }
    if (info.version.size() > ScenarioInfo::kMaxVersionLength || !valid_scenario_text(info.version, false)) {
        problems.push_back("the version is too long, not UTF-8 or has a control character");
    }
    return problems;
}

std::vector<std::string> check_markers(const std::vector<ScenarioMarker>& markers, std::uint32_t width,
                                       std::uint32_t height) {
    std::vector<std::string> problems;
    if (markers.size() > ScenarioMarker::kMaxMarkers) {
        problems.push_back("more than " + std::to_string(ScenarioMarker::kMaxMarkers) + " markers");
    }
    for (std::size_t i = 0; i < markers.size(); ++i) {
        const ScenarioMarker& m = markers[i];
        if (!valid_marker_name(m.name)) {
            problems.push_back("marker \"" + m.name + "\": a name is 1 to 64 lower case letters, digits and _");
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (markers[j].name == m.name) {
                problems.push_back("marker \"" + m.name + "\" twice");
                break;
            }
        }
        if (m.shape == ScenarioMarker::Shape::Point && (m.width != 1 || m.height != 1)) {
            problems.push_back("marker \"" + m.name + "\": a point is one tile");
        }
        const bool inside = m.width >= 1 && m.height >= 1 && m.x < width && m.y < height && m.width <= width - m.x &&
                            m.height <= height - m.y;
        if (!inside) {
            problems.push_back("marker \"" + m.name + "\": outside the world");
        }
    }
    return problems;
}

std::string_view scenario_text(const std::vector<ScenarioText>& texts, std::string_view language) {
    const auto in = [&](std::string_view code) {
        return std::find_if(texts.begin(), texts.end(), [&](const ScenarioText& t) { return t.language == code; });
    };
    if (auto it = in(language); it != texts.end()) {
        return it->text;
    }
    if (auto it = in("en"); it != texts.end()) {
        return it->text;
    }
    return texts.empty() ? std::string_view() : std::string_view(texts.front().text);
}

}  // namespace o1602::sim
