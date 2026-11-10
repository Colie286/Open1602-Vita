// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/game_directory.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <system_error>

#include "o1602/assets/history_texts.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace o1602::assets {

namespace fs = std::filesystem;

using core::ErrorCode;
using core::make_error;

namespace {

/// The History Edition's archive of its own data, among them its texts.
constexpr std::string_view kHistoryArchive = "ahe/a1he0.rda";

void add_if_folder(std::vector<fs::path>& out, const fs::path& folder) {
    std::error_code ec;
    if (!folder.empty() && fs::is_directory(folder, ec) && std::find(out.begin(), out.end(), folder) == out.end()) {
        out.push_back(folder);
    }
}

/// The folders in `parent` named after the game.
void add_named_below(std::vector<fs::path>& out, const fs::path& parent) {
    for (const fs::path& folder : GameDirectory::folders_named_1602(parent)) {
        add_if_folder(out, folder);
    }
}

/// The folders of a Windows drive where the game is usually installed: GOG's, GOG Galaxy's, Ubisoft
/// Connect's (History Edition) and the CD installer's (Program Files, Sunflowers' folder there, or
/// the drive itself).
void add_windows_drive(std::vector<fs::path>& out, const fs::path& drive) {
    add_named_below(out, drive / "GOG Games");
    for (const char* program_files : {"Program Files (x86)", "Program Files"}) {
        add_named_below(out, drive / program_files / "GOG Galaxy" / "Games");
        add_named_below(out, drive / program_files / "GOG Games");
        add_named_below(out, drive / program_files / "Ubisoft" / "Ubisoft Game Launcher" / "games");
        add_named_below(out, drive / program_files / "Sunflowers");
        add_named_below(out, drive / program_files);
    }
    add_named_below(out, drive);
}

/// The drive C of each Wine prefix in `parent` (CrossOver's and Bottles' bottles, Heroic's prefixes).
void add_prefixes_below(std::vector<fs::path>& out, const fs::path& parent) {
    std::error_code ec;
    for (fs::directory_iterator it(parent, ec), last; !ec && it != last; it.increment(ec)) {
        if (it->is_directory(ec)) {
            add_windows_drive(out, it->path() / "drive_c");
        }
    }
}

std::optional<fs::path> environment_path(const char* name) {
    const auto value = core::environment_variable(name);
    if (!value) {
        return std::nullopt;
    }
    return core::path_from_utf8(GameDirectory::clean_path_text(*value));
}

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return out;
}

/// The History Edition's own folder of the user: "Documents/Anno 1602 History Edition" with its
/// Game.dat and Savegame/. Below Wine (Heroic, Lutris) it is in the prefix the game is installed
/// in: drive_c/users/<user>/Documents/...
std::optional<fs::path> history_user_folder(const fs::path& root) {
    constexpr std::string_view kFolder = "Documents/Anno 1602 History Edition";
    std::vector<fs::path> homes;
#if defined(_WIN32)
    if (const auto profile = environment_path("USERPROFILE")) {
        homes.push_back(*profile);
    }
#endif
    std::error_code ec;
    for (fs::path p = fs::absolute(root, ec); !ec && p.has_relative_path(); p = p.parent_path()) {
        if (lower_ascii(core::path_to_utf8(p.filename())) != "drive_c") {
            continue;
        }
        if (const auto users = core::find_case_insensitive(p, "users")) {
            std::vector<fs::path> found;
            for (fs::directory_iterator it(*users, ec), last; !ec && it != last; it.increment(ec)) {
                if (it->is_directory(ec)) {
                    found.push_back(it->path());
                }
            }
            std::sort(found.begin(), found.end());
            homes.insert(homes.end(), found.begin(), found.end());
        }
        break;
    }
    for (const fs::path& home : homes) {
        if (auto folder = core::find_case_insensitive(home, kFolder)) {
            return folder;
        }
    }
    return std::nullopt;
}

/// The language part of a code: "de" of "de-DE", "de_DE.UTF-8" or "de".
std::string primary_language(std::string_view code) {
    return lower_ascii(code.substr(0, code.find_first_of("-_.@")));
}

/// The language of the user's system ("de", "en", ...); empty if unknown.
std::string system_language() {
#if defined(_WIN32)
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> name{};
    if (GetUserDefaultLocaleName(name.data(), static_cast<int>(name.size())) > 0) {
        std::string ascii;
        for (const wchar_t* c = name.data(); *c != L'\0' && *c < 0x80; ++c) {
            ascii.push_back(static_cast<char>(*c));
        }
        return primary_language(ascii);
    }
    return {};
#else
    // The order of precedence of the C library (LANGUAGE may list several: the first counts).
    for (const char* variable : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
        if (const auto value = core::environment_variable(variable); value && !value->empty()) {
            std::string code = primary_language(std::string_view(*value).substr(0, value->find(':')));
            if (!code.empty() && code != "c" && code != "posix") {
                return code;
            }
        }
    }
    return {};
#endif
}

#if defined(_WIN32)
/// The install folders GOG's installer records below `games`, one subkey per game with a "path" value.
void add_gog_games(std::vector<fs::path>& out, HKEY root, const wchar_t* games) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, games, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return;
    }
    std::array<wchar_t, 256> subkey{};
    for (DWORD index = 0;; ++index) {
        auto subkey_size = static_cast<DWORD>(subkey.size());
        if (RegEnumKeyExW(key, index, subkey.data(), &subkey_size, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS) {
            break;
        }
        std::array<wchar_t, MAX_PATH * 2> value{};
        auto value_size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        if (RegGetValueW(key, subkey.data(), L"path", RRF_RT_REG_SZ, nullptr, value.data(), &value_size) ==
            ERROR_SUCCESS) {
            add_if_folder(out, fs::path(value.data()));
        }
    }
    RegCloseKey(key);
}

/// The install folders GOG's installer records, for all users and for this one.
void add_gog_registry(std::vector<fs::path>& out) {
    for (const HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (const wchar_t* games : {L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", L"SOFTWARE\\GOG.com\\Games"}) {
            add_gog_games(out, root, games);
        }
    }
}

/// The History Edition's folder as Ubisoft Connect records it: one subkey per game below
/// "Installs" with an "InstallDir" value; only the folders named after the game count.
void add_ubisoft_registry(std::vector<fs::path>& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs", 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return;
    }
    std::array<wchar_t, 256> subkey{};
    for (DWORD index = 0;; ++index) {
        auto subkey_size = static_cast<DWORD>(subkey.size());
        if (RegEnumKeyExW(key, index, subkey.data(), &subkey_size, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS) {
            break;
        }
        std::array<wchar_t, MAX_PATH * 2> value{};
        auto value_size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        if (RegGetValueW(key, subkey.data(), L"InstallDir", RRF_RT_REG_SZ, nullptr, value.data(), &value_size) ==
            ERROR_SUCCESS) {
            const fs::path folder = fs::path(value.data()).lexically_normal();
            // "C:/.../games/Anno 1602 - History Edition/" ends with a separator: its name is the parent's.
            const fs::path name = folder.has_filename() ? folder.filename() : folder.parent_path().filename();
            if (core::path_to_utf8(name).find("1602") != std::string::npos) {
                add_if_folder(out, folder);
            }
        }
    }
    RegCloseKey(key);
}
#endif

}  // namespace

std::optional<std::string_view> GameDirectory::missing_file(const fs::path& root) {
    // Files every supported edition contains. 1602.exe itself is not required.
    constexpr std::array<std::string_view, 4> kRequired = {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col",
                                                           "gfx/stadtfld.bsh"};
    for (const std::string_view file : kRequired) {
        if (!core::find_case_insensitive(root, file)) {
            return file;
        }
    }
    return std::nullopt;
}

core::Result<GameDirectory> GameDirectory::open(const fs::path& root) {
    if (const auto file = missing_file(root)) {
        return make_error(ErrorCode::NotFound, core::path_to_utf8(root) +
                                                   " does not look like an Anno 1602 installation (missing " +
                                                   std::string(*file) + ")");
    }
    GameDirectory dir;
    dir.m_root = root;
    dir.m_localized = language_folder(root).value_or(fs::path());
    if (const auto rda = core::find_case_insensitive(root, kHistoryArchive)) {
        if (auto texts = HistoryTexts::load(*rda)) {
            dir.m_history_texts = std::make_shared<const HistoryTexts>(std::move(texts).value());
        }
        dir.m_user = history_user_folder(root).value_or(fs::path());
    }
    return dir;
}

void GameDirectory::choose_language(std::string_view language) {
    // language_folder() falls back to other languages; only one of `language` replaces the folder.
    if (auto folder = language_folder(m_root, language);
        folder && primary_language(core::path_to_utf8(folder->filename())) == primary_language(language)) {
        m_localized = std::move(*folder);
    } else if (!m_localized.empty() && primary_language(language) == "en") {
        m_localized.clear();  // the History Edition's own folders are English
    }
}

core::Result<GameDirectory> GameDirectory::from_environment() {
    const auto root = environment_path(kEnvVar);
    if (!root || root->empty()) {
        return make_error(ErrorCode::NotFound, std::string("environment variable ") + kEnvVar + " is not set");
    }
    return open(*root);
}

std::vector<fs::path> GameDirectory::installation_candidates() {
    std::vector<fs::path> out;
#if defined(_WIN32)
    add_gog_registry(out);
    add_ubisoft_registry(out);
    for (const char* program_files : {"ProgramFiles(x86)", "ProgramFiles"}) {
        if (const auto folder = environment_path(program_files)) {
            add_named_below(out, *folder / "GOG Galaxy" / "Games");
            add_named_below(out, *folder / "GOG Games");
            add_named_below(out, *folder / "Ubisoft" / "Ubisoft Game Launcher" / "games");
        }
    }
    // "C:" / "/" is C:\, the drive's root.
    add_windows_drive(out, environment_path("SystemDrive").value_or(fs::path("C:")) / "/");
#else
    if (const auto home = environment_path("HOME"); home && !home->empty()) {
        out = home_candidates(*home);
    }
#if defined(__APPLE__)
    add_named_below(out, "/Applications");
#endif
#endif
    return out;
}

std::vector<fs::path> GameDirectory::home_candidates(const fs::path& home) {
    std::vector<fs::path> out;
    add_named_below(out, home / "GOG Games");
    add_named_below(out, home / "Games" / "Heroic");  // Heroic Games Launcher
    add_named_below(out, home / "Games");             // Lutris
    add_named_below(out, home / "Applications");
    add_windows_drive(out, home / ".wine" / "drive_c");
    add_prefixes_below(out, home / "Library" / "Application Support" / "CrossOver" / "Bottles");
    add_prefixes_below(out, home / ".local" / "share" / "bottles" / "bottles");
    add_prefixes_below(out, home / ".var" / "app" / "com.usebottles.bottles" / "data" / "bottles" / "bottles");
    add_prefixes_below(out, home / "Games" / "Heroic" / "Prefixes");
    // Heroic's prefixes are one per game in Prefixes/default (e.g. "Ubisoft Connect" for the
    // History Edition installed through it).
    add_prefixes_below(out, home / "Games" / "Heroic" / "Prefixes" / "default");
    // Lutris: a prefix per game, in the game's folder.
    std::error_code ec;
    for (fs::directory_iterator it(home / "Games", ec), last; !ec && it != last; it.increment(ec)) {
        if (it->is_directory(ec) && it->path().filename() != "Heroic") {
            add_windows_drive(out, it->path() / "drive_c");
        }
    }
    return out;
}

std::vector<fs::path> GameDirectory::folders_named_1602(const fs::path& parent) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(parent, ec), last; !ec && it != last; it.increment(ec)) {
        if (it->is_directory(ec) && core::path_to_utf8(it->path().filename()).find("1602") != std::string::npos) {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<fs::path> GameDirectory::language_folder(const fs::path& root, std::string_view language) {
    const auto loca = core::find_case_insensitive(root, "Loca");
    if (!loca) {
        return std::nullopt;
    }
    std::vector<fs::path> folders;
    std::error_code ec;
    for (fs::directory_iterator it(*loca, ec), last; !ec && it != last; it.increment(ec)) {
        if (it->is_directory(ec)) {
            folders.push_back(it->path());
        }
    }
    if (folders.empty()) {
        return std::nullopt;
    }
    std::sort(folders.begin(), folders.end());
    const auto of_language = [&](const std::string& wanted) -> std::optional<fs::path> {
        if (wanted.empty()) {
            return std::nullopt;
        }
        // An exact code first ("en-GB" before "en-US" when en-GB is asked for), then the language.
        for (const fs::path& folder : folders) {
            if (lower_ascii(core::path_to_utf8(folder.filename())) == lower_ascii(language)) {
                return folder;
            }
        }
        for (const fs::path& folder : folders) {
            if (primary_language(core::path_to_utf8(folder.filename())) == wanted) {
                return folder;
            }
        }
        return std::nullopt;
    };
    for (const std::string& wanted : {primary_language(language), system_language(), std::string("en")}) {
        if (auto folder = of_language(wanted)) {
            return folder;
        }
    }
    return folders.front();
}

std::string GameDirectory::clean_path_text(std::string_view text) {
    const auto trim = [](std::string_view& s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
            s.remove_suffix(1);
        }
    };
    trim(text);
    // Quotes never belong to a Windows path; each end is stripped on its own (see the header).
    if (!text.empty() && text.front() == '"') {
        text.remove_prefix(1);
    }
    if (!text.empty() && text.back() == '"') {
        text.remove_suffix(1);
    }
    trim(text);
    return std::string(text);
}

std::optional<fs::path> GameDirectory::resolve(std::string_view relative) const {
    if (!m_localized.empty()) {
        if (auto path = core::find_case_insensitive(m_localized, relative)) {
            return path;
        }
    }
    if (auto path = core::find_case_insensitive(m_root, relative)) {
        return path;
    }
    if (!m_user.empty()) {
        if (auto path = core::find_case_insensitive(m_user, relative)) {
            return path;
        }
    }
    return resolve_named_scenario(relative);
}

std::optional<fs::path> GameDirectory::resolve_named_scenario(std::string_view relative) const {
    // "Szenes/Das Atoll.szs" is "Szenes/[[7]].szs", "Szenes/Endlosspiel12.szm" "Szenes/[[363]]12.szm"
    // when [[363]] is "Endlosspiel" too: the digits at the end stay, the name before them is looked up.
    if (m_history_texts == nullptr) {
        return std::nullopt;
    }
    const std::size_t slash = relative.find_last_of("/\\");
    if (slash == std::string_view::npos || !core::equals_ignore_case(relative.substr(0, slash), "Szenes")) {
        return std::nullopt;
    }
    const std::string_view file = relative.substr(slash + 1);
    const std::size_t dot = file.rfind('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view stem = file.substr(0, dot);
    std::size_t digits = stem.size();
    while (digits > 0 && stem[digits - 1] >= '0' && stem[digits - 1] <= '9') {
        --digits;
    }
    const std::string_view suffix = stem.substr(digits);
    const std::string_view extension = file.substr(dot);
    // Several lines may have the same name (the endless game of one and of several players): the
    // one whose file exists counts.
    const std::string language = text_language();
    for (const std::int32_t id : m_history_texts->find_all(stem.substr(0, digits), language)) {
        const std::string name = "Szenes/[[" + std::to_string(id) + "]]" + std::string(suffix) + std::string(extension);
        if (auto path = core::find_case_insensitive(m_root, name)) {
            return path;
        }
    }
    return std::nullopt;
}

std::string GameDirectory::text_language() const {
    if (!m_localized.empty()) {
        return primary_language(core::path_to_utf8(m_localized.filename()));
    }
    return m_history_texts != nullptr ? "en" : std::string();
}

std::string GameDirectory::expand(std::string_view text) const {
    if (m_history_texts == nullptr || text.find("[[") == std::string_view::npos) {
        return std::string(text);
    }
    return core::utf8_to_cp1252(m_history_texts->expand(core::cp1252_to_utf8(text), text_language()));
}

}  // namespace o1602::assets
