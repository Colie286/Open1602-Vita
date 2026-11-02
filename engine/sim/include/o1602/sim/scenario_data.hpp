// SPDX-License-Identifier: GPL-3.0-or-later
//
// What makes a saved world a scenario of Open1602's editor (ADR 0013, docs/formats/o1602-save.md):
// its names and briefings in several languages, its difficulty and author, whether it keeps to the
// original's limits, named markers for scripts, a script of its own and the editor's own state.
// A world without ScenarioInfo is an ordinary savegame; markers and a script can belong to a
// running game too, since the scenario's script runs on in it.
//
// Unlike the names of players, cities and ships (Windows-1252, as the game's fonts draw them),
// these texts are UTF-8: they are typed in the editor and read by scripts and lists.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace o1602::sim {

/// A text in one language: `language` is a code such as "en", "de" or "pt-BR".
struct ScenarioText {
    std::string language;
    std::string text;  ///< UTF-8

    friend bool operator==(const ScenarioText&, const ScenarioText&) = default;
};

/// The description of a scenario (section SCNI).
struct ScenarioInfo {
    static constexpr std::uint8_t kMaxStars = 5;
    static constexpr std::size_t kMaxLanguages = 32;
    static constexpr std::size_t kMaxLanguageLength = 16;
    static constexpr std::size_t kMaxNameLength = 256;
    static constexpr std::size_t kMaxBriefingLength = 16 * 1024;
    static constexpr std::size_t kMaxAuthorLength = 256;
    static constexpr std::size_t kMaxVersionLength = 64;

    std::vector<ScenarioText> names;      ///< the name in the scenario lists, per language
    std::vector<ScenarioText> briefings;  ///< the mission's text, per language
    std::uint8_t stars = 0;               ///< difficulty 0..kMaxStars, as the original's stars
    std::string author;                   ///< UTF-8
    std::string version;                  ///< UTF-8, free form ("1.2")
    /// The editor keeps the scenario within the original's limits (ADR 0013): its size, islands,
    /// players, the original's buildings and goods only, no packages and no script.
    bool original_limits = false;

    friend bool operator==(const ScenarioInfo&, const ScenarioInfo&) = default;
};

/// A named place or area on the world map that scripts find by its name (section MARK).
struct ScenarioMarker {
    static constexpr std::size_t kMaxNameLength = 64;
    static constexpr std::size_t kMaxMarkers = 4096;

    enum class Shape : std::uint8_t {
        Point,  ///< one tile; width and height are 1
        Area,   ///< a rectangle of tiles
        Last = Area,
    };
    std::string name;  ///< lower case letters, digits and '_', unique in the scenario
    Shape shape = Shape::Point;
    std::uint32_t x = 0;  ///< world tiles, the rectangle's top left
    std::uint32_t y = 0;
    std::uint32_t width = 1;
    std::uint32_t height = 1;

    friend bool operator==(const ScenarioMarker&, const ScenarioMarker&) = default;
};

/// The largest script a scenario carries (section SLUA) and the largest state of the editor (EDIT).
inline constexpr std::size_t kMaxScenarioScriptLength = 1024 * 1024;
inline constexpr std::size_t kMaxEditorStateLength = 4 * 1024 * 1024;

/// Whether `code` can name a language: 2 to ScenarioInfo::kMaxLanguageLength bytes of ASCII
/// letters, digits and '-', starting with a letter.
[[nodiscard]] bool valid_language_code(std::string_view code) noexcept;

/// Whether `name` can name a marker: 1 to ScenarioMarker::kMaxNameLength bytes of lower case
/// letters, digits and '_' (the names of scenarios.toml).
[[nodiscard]] bool valid_marker_name(std::string_view name) noexcept;

/// Whether `text` is UTF-8 without control characters other than line breaks and tabs (`lines`) or
/// without any (one line).
[[nodiscard]] bool valid_scenario_text(std::string_view text, bool lines) noexcept;

/// What is wrong with `info`, one line each; empty if nothing. Languages must be valid and each
/// come once per list, texts within their limits.
[[nodiscard]] std::vector<std::string> check_scenario_info(const ScenarioInfo& info);

/// What is wrong with `markers` in a world of `width` x `height` tiles, one line each: an invalid
/// or repeated name, a rectangle outside the world, a point that is not 1 x 1, too many markers.
[[nodiscard]] std::vector<std::string> check_markers(const std::vector<ScenarioMarker>& markers, std::uint32_t width,
                                                     std::uint32_t height);

/// The text of `texts` in `language`, else the English one, else the first; empty if none.
[[nodiscard]] std::string_view scenario_text(const std::vector<ScenarioText>& texts, std::string_view language);

}  // namespace o1602::sim
