// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/localization.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <system_error>

#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/embedded_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"

namespace o1602::ui {

// data/locale/*.lang, compiled in by o1602_embed_files (engine/ui/CMakeLists.txt).
namespace locales {
extern const core::EmbeddedFile kEmbeddedFiles[];
extern const std::size_t kEmbeddedFileCount;
}  // namespace locales

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

/// "\n" becomes a line break, "\\" a backslash; anything else after a backslash stays as it is.
std::string unescape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            if (value[i + 1] == 'n') {
                out.push_back('\n');
                ++i;
                continue;
            }
            if (value[i + 1] == '\\') {
                out.push_back('\\');
                ++i;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

std::string lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

/// The texts tr() answers with: the current language over English.
struct State {
    std::string language{kFallbackLanguage};
    Catalogue texts = builtin_catalogue(kFallbackLanguage);
};

State& state() {
    static State s;
    return s;
}

}  // namespace

Catalogue Catalogue::parse(std::string_view utf8) {
    Catalogue c;
    if (utf8.starts_with("\xEF\xBB\xBF")) {
        utf8.remove_prefix(3);  // a byte order mark from an editor
    }
    while (!utf8.empty()) {
        const std::size_t nl = utf8.find('\n');
        const std::string_view line = trim(utf8.substr(0, nl));
        utf8 = nl == std::string_view::npos ? std::string_view{} : utf8.substr(nl + 1);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq));
        if (!key.empty() && !trim(line.substr(eq + 1)).empty()) {
            c.m_texts.insert_or_assign(std::string(key), core::utf8_to_cp1252(unescape(trim(line.substr(eq + 1)))));
        }
    }
    return c;
}

const std::string* Catalogue::find(std::string_view key) const {
    const auto it = m_texts.find(key);
    return it == m_texts.end() ? nullptr : &it->second;
}

std::vector<std::string> Catalogue::keys() const {
    std::vector<std::string> out;
    out.reserve(m_texts.size());
    for (const auto& entry : m_texts) {
        out.push_back(entry.first);
    }
    return out;
}

void Catalogue::merge(const Catalogue& other) {
    for (const auto& [key, value] : other.m_texts) {
        m_texts.insert_or_assign(key, value);
    }
}

std::vector<std::string> builtin_languages() {
    std::vector<std::string> codes;
    for (std::size_t i = 0; i < locales::kEmbeddedFileCount; ++i) {
        codes.emplace_back(locales::kEmbeddedFiles[i].name);
    }
    return codes;
}

Catalogue builtin_catalogue(std::string_view code) {
    for (std::size_t i = 0; i < locales::kEmbeddedFileCount; ++i) {
        if (core::equals_ignore_case(locales::kEmbeddedFiles[i].name, code)) {
            return Catalogue::parse(locales::kEmbeddedFiles[i].text());
        }
    }
    return {};
}

std::string detect_language(const assets::TextTable& texts) {
    const std::vector<std::string>& colours = texts.section("PLAYCOL");
    for (const std::string& code : builtin_languages()) {
        const Catalogue c = builtin_catalogue(code);
        const std::string* detect = c.find("language.detect");
        if (detect == nullptr) {
            continue;
        }
        // Both sides are Windows-1252: Text.cod as read, the catalogue as parsed.
        std::string_view rest = *detect;
        while (!rest.empty()) {
            const std::size_t bar = rest.find('|');
            const std::string_view wanted = trim(rest.substr(0, bar));
            rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
            const bool found = std::any_of(colours.begin(), colours.end(), [&](const std::string& line) {
                return !wanted.empty() && core::equals_ignore_case(trim(line), wanted);
            });
            if (found) {
                return code;
            }
        }
    }
    return {};
}

std::string gog_language(const assets::GameDirectory& game) {
    // goggame-<id>.info is JSON with "languages": ["de-DE"]; a look for the first entry is enough.
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(game.root(), ec)) {
        const std::u8string file = entry.path().filename().u8string();  // never throws, unlike string()
        const std::string name = lower_ascii(std::string(file.begin(), file.end()));
        if (!name.starts_with("goggame-") || !name.ends_with(".info")) {
            continue;
        }
        const auto bytes = core::read_file(entry.path());
        if (!bytes) {
            continue;
        }
        const std::string_view json(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        const std::size_t key = json.find("\"languages\"");
        const std::size_t open = key == std::string_view::npos ? key : json.find('[', key);
        const std::size_t quote = open == std::string_view::npos ? open : json.find('"', open);
        if (quote == std::string_view::npos) {
            continue;
        }
        const std::size_t end = json.find_first_of("-_\"", quote + 1);
        if (end != std::string_view::npos && end > quote + 1) {
            return lower_ascii(json.substr(quote + 1, end - quote - 1));
        }
    }
    return {};
}

std::string installation_language(const assets::GameDirectory& game, const assets::TextTable* texts) {
    if (texts != nullptr) {
        if (std::string code = detect_language(*texts); !code.empty()) {
            return code;
        }
    }
    if (std::string code = gog_language(game); !code.empty()) {
        return code;
    }
    // The History Edition's language folder (Loca/de-DE gives de).
    if (const auto& folder = game.localized_root(); !folder.empty()) {
        const std::u8string file = folder.filename().u8string();
        const std::string name(file.begin(), file.end());
        if (std::string code = lower_ascii(name.substr(0, name.find_first_of("-_"))); !code.empty()) {
            return code;
        }
    }
    return std::string(kFallbackLanguage);
}

void set_language(std::string_view code, const std::filesystem::path& user_folder,
                  std::span<const std::filesystem::path> package_folders) {
    State next;
    next.language = lower_ascii(trim(code));
    std::replace(next.language.begin(), next.language.end(), '_', '-');
    next.language.resize(next.language.find_first_of(".@") == std::string::npos
                             ? next.language.size() : next.language.find_first_of(".@"));
    if (next.language.empty() || !std::all_of(next.language.begin(), next.language.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        })) next.language = std::string(kFallbackLanguage);
    const auto base = next.language.substr(0, next.language.find('-'));
    // English -> base catalogue -> regional catalogue, with each catalogue's
    // user overrides. A missing entry never changes the original resource language.
    auto merge = [&](const std::string& language) {
        next.texts.merge(builtin_catalogue(language));
        for (const auto& folder : package_folders) {
            if (const auto bytes = core::read_file(folder / kPackageLocaleFolder / (language + ".lang")))
                next.texts.merge(Catalogue::parse(std::string_view(
                    reinterpret_cast<const char*>(bytes->data()), bytes->size())));
        }
        if (!user_folder.empty()) {
            if (const auto bytes = core::read_file(user_folder / (language + ".lang")))
                next.texts.merge(Catalogue::parse(std::string_view(
                    reinterpret_cast<const char*>(bytes->data()), bytes->size())));
        }
    };
    merge(std::string(kFallbackLanguage));
    if (base != kFallbackLanguage) merge(base);
    if (next.language != base) merge(next.language);
    state() = std::move(next);
}

std::string tr_utf8(std::string_view key) { return core::cp1252_to_utf8(tr(key)); }

void check_catalogue(std::string_view utf8, std::string_view where, std::vector<std::string>& problems) {
    const Catalogue english = builtin_catalogue(kFallbackLanguage);
    // The highest {N} of a text, 0 if it has none.
    const auto highest = [](std::string_view text) {
        int n = 0;
        for (std::size_t i = 0; i + 2 < text.size(); ++i) {
            if (text[i] == '{' && text[i + 2] == '}' && text[i + 1] >= '1' && text[i + 1] <= '9') {
                n = std::max(n, text[i + 1] - '0');
            }
        }
        return n;
    };
    if (utf8.starts_with("\xEF\xBB\xBF")) {
        utf8.remove_prefix(3);
    }
    std::size_t number = 0;
    while (!utf8.empty()) {
        ++number;
        const std::size_t nl = utf8.find('\n');
        const std::string_view line = trim(utf8.substr(0, nl));
        utf8 = nl == std::string_view::npos ? std::string_view{} : utf8.substr(nl + 1);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const std::string at = std::string(where) + " line " + std::to_string(number) + ": ";
        const std::size_t eq = line.find('=');
        const std::string_view key = eq == std::string_view::npos ? std::string_view{} : trim(line.substr(0, eq));
        if (key.empty()) {
            problems.push_back(at + "is no \"key = text\" line and is passed over");
            continue;
        }
        const std::string* original = english.find(key);
        if (original == nullptr) {
            problems.push_back(at + "'" + std::string(key) + "' is no text of Open1602; nothing shows it");
            continue;
        }
        if (const int used = highest(line.substr(eq + 1)); used > highest(*original)) {
            problems.push_back(at + "'" + std::string(key) + "' uses {" + std::to_string(used) +
                               "}, which the game does not fill in there");
        }
    }
}

const std::string& current_language() {
    return state().language;
}

const std::string& tr(std::string_view key) {
    if (const std::string* text = state().texts.find(key)) {
        return *text;
    }
    // A key without text shows as itself, so a missing line is easy to spot. Kept for the
    // program's lifetime, as callers hold the reference.
    static std::map<std::string, std::string, std::less<>> missing;
    auto it = missing.find(key);
    if (it == missing.end()) {
        it = missing.emplace(std::string(key), std::string(key)).first;
    }
    return it->second;
}

std::string tr(std::string_view key, std::initializer_list<std::string_view> args) {
    const std::string& pattern = tr(key);
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        // {1} .. {9}: the argument of that number; anything else stays as it is.
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && pattern[i + 1] >= '1' &&
            pattern[i + 1] <= '9') {
            const auto n = static_cast<std::size_t>(pattern[i + 1] - '1');
            if (n < args.size()) {
                out += *(args.begin() + n);
            }
            i += 2;
            continue;
        }
        out.push_back(pattern[i]);
    }
    return out;
}

std::string error_text(const core::Error& error, std::string_view context) {
    std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(context.size()), context.data(), error.message.c_str());
    switch (error.code) {
        case core::ErrorCode::IoError:
            return tr("error.io");
        case core::ErrorCode::NotFound:
            return tr("error.not_found");
        case core::ErrorCode::InvalidFormat:
        case core::ErrorCode::OutOfRange:
            return tr("error.damaged");
        case core::ErrorCode::LimitExceeded:
            return tr("error.limit", {error.message});
        case core::ErrorCode::Unsupported:
            return tr("error.unsupported");
        case core::ErrorCode::ScriptError:
            return tr("error.script", {error.message});
        case core::ErrorCode::PlatformError:
        case core::ErrorCode::Cancelled:
            break;
    }
    return tr("error.platform");
}

}  // namespace o1602::ui
