// SPDX-License-Identifier: GPL-3.0-or-later
//
// Open1602's own texts (menus, questions and messages the original does not have) in the language
// of the installation: a German Anno 1602 gets German texts, an English one English texts. The
// original's texts keep coming from its data (Text.cod and the like).
//
// Each language is a catalogue of "key = value" lines in UTF-8 (data/locale/<code>.lang, compiled
// into the program). A catalogue in the folder "locale" of the user's data folder extends or
// replaces the built-in one of the same code, so a new language needs no rebuild
// (docs/modding/languages.md); a package brings its catalogues in its folder "locale". A key the language lacks falls
// back to English, a key no catalogue has shows as the key itself.
#pragma once

#include <filesystem>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/core/result.hpp"

namespace o1602::assets {
class GameDirectory;
class TextTable;
}  // namespace o1602::assets

namespace o1602::ui {

/// The language every catalogue falls back to.
inline constexpr std::string_view kFallbackLanguage = "en";

class Catalogue {
public:
    /// Reads "key = value" lines (UTF-8; lines starting with # and blank lines are passed over).
    /// "\n" in a value is a line break. Values are kept in Windows-1252, as the game's fonts draw.
    static Catalogue parse(std::string_view utf8);

    /// The value of `key`, or nullptr.
    [[nodiscard]] const std::string* find(std::string_view key) const;
    /// Adds the entries of `other`; where both have a key, `other` wins.
    void merge(const Catalogue& other);
    [[nodiscard]] std::size_t size() const noexcept { return m_texts.size(); }
    [[nodiscard]] std::vector<std::string> keys() const;

private:
    std::map<std::string, std::string, std::less<>> m_texts;
};

/// The codes of the built-in catalogues ("de", "en", ...).
[[nodiscard]] std::vector<std::string> builtin_languages();
/// A built-in catalogue (empty if the code has none).
[[nodiscard]] Catalogue builtin_catalogue(std::string_view code);

/// The language of an installation, or "" if nothing tells: the catalogue whose "language.detect"
/// texts (separated by '|') equal a line of Text.cod's [PLAYCOL] section ("Roter Spieler" is German).
[[nodiscard]] std::string detect_language(const assets::TextTable& texts);
/// The language GOG's installer wrote into goggame-*.info ("de-DE" gives "de"), or "".
[[nodiscard]] std::string gog_language(const assets::GameDirectory& game);
/// The language of an installation: detect_language(), else gog_language(), else English.
[[nodiscard]] std::string installation_language(const assets::GameDirectory& game, const assets::TextTable* texts);

/// The folder of a package that holds its catalogues (<package>/locale/<code>.lang).
inline constexpr std::string_view kPackageLocaleFolder = "locale";

/// Makes `code` the language of tr(): its built-in catalogue, then <package>/locale/<code>.lang
/// of each of `package_folders` in their load order, then, if `user_folder` is given,
/// <user_folder>/<code>.lang, each over the one before and all over English. Call it on the main
/// thread while no text of tr() is held; until the first call tr() answers in English.
void set_language(std::string_view code, const std::filesystem::path& user_folder = {},
                  std::span<const std::filesystem::path> package_folders = {});

[[nodiscard]] std::string tr_utf8(std::string_view key);
/// The code given to set_language() ("en" before).
[[nodiscard]] const std::string& current_language();

/// The text of `key` in the current language (Windows-1252).
[[nodiscard]] const std::string& tr(std::string_view key);
/// The same with {1}, {2}, ... replaced by the arguments (Windows-1252 as well); the translation
/// decides where each argument stands.
[[nodiscard]] std::string tr(std::string_view key, std::initializer_list<std::string_view> args);

/// The problems of a catalogue of a package (`where` names it, "<package>/locale/fr.lang"): a key
/// English has not, which no part of the game asks for, and a {N} the game does not fill in for
/// that key, each as "<where> line <N>: ...".
void check_catalogue(std::string_view utf8, std::string_view where, std::vector<std::string>& problems);

/// What went wrong, for the player: the kind of error in the current language (Windows-1252). The
/// technical message, which is English, goes to the log (stderr) with `context` before it.
[[nodiscard]] std::string error_text(const core::Error& error, std::string_view context);

}  // namespace o1602::ui
