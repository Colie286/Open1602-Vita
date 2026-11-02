// SPDX-License-Identifier: GPL-3.0-or-later
#include "message_table.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <system_error>

#include "o1602/assets/game_content.hpp"
#include "o1602/audio/sound_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/ui/package_icons.hpp"

namespace o1602::app {

namespace {

/// The original's messages (has 0x70).
constexpr MessageId kOriginalMessages = 0x70;

constexpr std::array<std::pair<std::string_view, MessageIcon>, 8> kIcons{{
    {"notice", MessageIcon::Notice},
    {"scroll", MessageIcon::Scroll},
    {"combat", MessageIcon::Combat},
    {"player", MessageIcon::Player},
    {"plague", MessageIcon::Plague},
    {"fire", MessageIcon::Fire},
    {"drought", MessageIcon::Drought},
    {"volcano", MessageIcon::Volcano},
}};

/// The kinds of voice messages of the options ([SPEECHKIND] in Text.cod), then
/// Open1602's own (audio::kSpeechRoutes).
constexpr std::array<std::string_view, audio::kMessageKinds> kKinds{
    "general", "events", "island", "players", "trade", "combat", "orders", "diplomacy", "routes"};

/// Which icon an original message shows: that is not in the data.
MessageIcon original_icon(std::uint16_t message) {
    switch (message) {
        case 0x01:
            return MessageIcon::Plague;
        case 0x02:
            return MessageIcon::Fire;
        case 0x03:
            return MessageIcon::Drought;
        case 0x5E:
            return MessageIcon::Volcano;
        default:
            break;
    }
    const audio::MessageAudio* m = audio::message_audio(message);
    const std::uint8_t kind = m != nullptr ? m->kind : 0;
    return kind == 5                ? MessageIcon::Combat
           : kind == 4 || kind == 7 ? MessageIcon::Scroll
           : kind == 3              ? MessageIcon::Player
                                    : MessageIcon::Notice;
}

/// "42" or "0x2A".
std::optional<std::uint32_t> parse_number(std::string_view s) {
    int base = 10;
    if (s.starts_with("0x") || s.starts_with("0X")) {
        base = 16;
        s.remove_prefix(2);
    }
    std::uint32_t n = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), n, base);
    if (s.empty() || ec != std::errc{} || end != s.data() + s.size()) {
        return std::nullopt;
    }
    return n;
}

/// A package's id: lower-case letters, digits, '_' and '-'.
bool valid_package(std::string_view package) {
    return !package.empty() && std::all_of(package.begin(), package.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

/// A file of the package, never outside its folder; nullopt for a path that leaves it.
std::optional<std::filesystem::path> package_path(std::string_view utf8) {
    std::filesystem::path relative = std::filesystem::path(core::path_from_utf8(utf8)).lexically_normal();
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || *relative.begin() == "..") {
        return std::nullopt;
    }
    return relative;
}

/// Whether the file is a Smacker (.smk: "SMK2", "SMK4") or Bink 2 file (.bk2: "KB2"), by its name
/// and its first bytes: the player picks the decoder by the name.
bool is_video_file(const std::filesystem::path& file) {
    std::string extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::array<char, 4> magic{};
    std::ifstream in(file, std::ios::binary);
    if (!in.read(magic.data(), magic.size())) {
        return false;
    }
    const std::string_view head(magic.data(), magic.size());
    if (extension == ".smk") {
        return head == "SMK2" || head == "SMK4";
    }
    return extension == ".bk2" && head.starts_with("KB2");
}

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

}  // namespace

std::string fill_placeholders(std::string_view pattern, const MessageArgs& args) {
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const std::size_t close = pattern.find('}', i + 1);
            if (close != std::string_view::npos) {
                const std::string_view name = pattern.substr(i + 1, close - i - 1);
                const auto it = std::find_if(args.begin(), args.end(), [&](const auto& a) { return a.first == name; });
                if (it != args.end()) {
                    out += it->second;
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

MessageTable::MessageTable() {
    m_messages.reserve(kOriginalMessages);
    for (MessageId n = 0; n < kOriginalMessages; ++n) {
        MessageDefinition m;
        m.id = std::string(kOriginal) + ":" + std::to_string(n);
        m.icon = original_icon(static_cast<std::uint16_t>(n));
        m.meldung = static_cast<std::uint16_t>(n);
        if (const audio::MessageAudio* a = audio::message_audio(static_cast<std::uint16_t>(n))) {
            m.playback = audio::MessagePlayback::of(*a);
        }
        m_ids.emplace(m.id, n);
        m_messages.push_back(std::move(m));
    }
}

const MessageTable& MessageTable::original() {
    static const MessageTable table;
    return table;
}

std::optional<MessageId> MessageTable::find(std::string_view id) const {
    if (const auto it = m_ids.find(std::string(id)); it != m_ids.end()) {
        return it->second;
    }
    // The original's numbers in hex, as the documentation writes them.
    if (id.size() > kOriginal.size() && id.starts_with(kOriginal) && id[kOriginal.size()] == ':') {
        if (const auto n = parse_number(id.substr(kOriginal.size() + 1)); n && *n < kOriginalMessages) {
            return n;
        }
    }
    return std::nullopt;
}

std::string MessageTable::text(MessageId id, std::string_view language, const MessageArgs& args) const {
    const MessageDefinition* m = get(id);
    if (m == nullptr) {
        return {};
    }
    for (const std::string_view code : {language, std::string_view{}, std::string_view("en")}) {
        if (const auto it = m->texts.find(code); it != m->texts.end()) {
            return fill_placeholders(it->second, args);
        }
    }
    return {};
}

void MessageTable::apply(const core::DataTable& table, MessageDefinition& m, const std::filesystem::path& folder,
                         const std::string& where, std::vector<std::string>& warnings) {
    // `like` first, wherever it stands: the other fields change what it brings.
    if (const core::DataValue* like = table.find("like")) {
        std::optional<MessageId> from;
        if (const std::int64_t* n = like->integer(); n != nullptr && *n >= 0 && *n < kOriginalMessages) {
            from = static_cast<MessageId>(*n);
        } else if (const std::string* s = like->string()) {
            from = find(*s);
        }
        if (const MessageDefinition* source = from ? get(*from) : nullptr) {
            m.icon = source->icon;
            m.picture = source->picture;
            m.meldung = source->meldung;
            m.playback = source->playback;
            m.texts = source->texts;
        } else {
            warnings.push_back(line_of(where, like->line) + "'like' names no message (an original number or an id)");
        }
    }
    for (const core::DataEntry& e : table.entries) {
        const std::string at = line_of(where, e.value.line);
        const std::string* s = e.value.string();
        const std::int64_t* n = e.value.integer();
        const bool* b = e.value.boolean();
        if (e.key == "like") {
            continue;
        }
        if (e.key == "text" || e.key.starts_with("text.")) {
            const std::string code = e.key == "text" ? std::string() : e.key.substr(5);
            if (s == nullptr) {
                warnings.push_back(at + "'" + e.key + "' must be a string");
            } else {
                m.texts.insert_or_assign(code, core::utf8_to_cp1252(*s));
            }
        } else if (e.key == "icon") {
            const auto it =
                s == nullptr ? kIcons.end()
                             : std::find_if(kIcons.begin(), kIcons.end(), [&](const auto& i) { return i.first == *s; });
            if (it != kIcons.end()) {
                m.icon = it->second;
                m.picture.clear();
            } else if (s != nullptr && ui::package_icon_sprite(*s)) {
                m.picture = *s;
            } else {
                warnings.push_back(at +
                                   "'icon' must be one of notice, scroll, combat, player, plague, fire, "
                                   "drought, volcano, or an icon of a package (icons.toml, <package>:<name>)");
            }
        } else if (e.key == "kind") {
            const auto it = s == nullptr ? kKinds.end() : std::find(kKinds.begin(), kKinds.end(), *s);
            if (it == kKinds.end()) {
                warnings.push_back(at +
                                   "'kind' must be one of general, events, island, players, trade, combat, "
                                   "orders, diplomacy");
            } else {
                m.playback.kind = static_cast<std::uint8_t>(it - kKinds.begin());
            }
        } else if (e.key == "sound") {
            if (b != nullptr && !*b) {
                m.playback.voices = 0;
                m.playback.sound = nullptr;
            } else if (n != nullptr && *n > 0 && *n < 0x10000) {
                // A file of Speech8 (<n>.wav).
                m.playback.voice_base = static_cast<std::uint16_t>(*n);
                m.playback.voices = 1;
                m.playback.sound = nullptr;
            } else if (s != nullptr) {
                const auto relative = package_path(*s);
                if (!relative) {
                    warnings.push_back(at + "'sound' must be a file in the package's folder");
                    continue;
                }
                const auto bytes = core::read_file(folder / *relative);
                if (!bytes) {
                    warnings.push_back(at + *s + ": " + bytes.error().message);
                    continue;
                }
                auto pcm = audio::decode_sound(*bytes);
                if (!pcm) {
                    warnings.push_back(at + *s + ": " + pcm.error().message);
                    continue;
                }
                m.playback.sound = std::make_shared<const audio::Pcm>(std::move(pcm).value());
            } else {
                warnings.push_back(
                    at +
                    "'sound' must be a sound file of the package (WAV, Ogg Vorbis, FLAC), a Speech8 number or "
                    "false");
            }
        } else if (e.key == "video") {
            if (b != nullptr && !*b) {
                m.playback.videos = 0;
            } else if (n != nullptr && *n >= 0 && *n < 0x10000) {
                m.playback.video_base = static_cast<std::uint16_t>(*n);
                m.playback.videos = 1;
            } else if (const auto relative = s != nullptr ? package_path(*s) : std::nullopt; relative) {
                // A Smacker or Bink 2 file of the package, as the original's videos are.
                if (const auto number = package_video(folder / *relative, at + *s + ": ", warnings)) {
                    m.playback.video_base = *number;
                    m.playback.videos = 1;
                }
            } else {
                warnings.push_back(at +
                                   "'video' must be a number of VideoSmk, a video file in the package's folder "
                                   "(.smk, .bk2) or false");
            }
        } else if (e.key == "video_always" || e.key == "normal_speed") {
            const std::uint16_t flag =
                e.key == "video_always" ? audio::kMessageVideoAlways : audio::kMessageNormalSpeed;
            if (b == nullptr) {
                warnings.push_back(at + "'" + e.key + "' must be true or false");
            } else if (*b) {
                m.playback.flags = static_cast<std::uint16_t>(m.playback.flags | flag);
            } else {
                m.playback.flags = static_cast<std::uint16_t>(m.playback.flags & ~flag);
            }
        } else {
            warnings.push_back(at + "unknown field '" + e.key + "'");
        }
    }
}

core::Status MessageTable::load_package(std::string_view package, std::string_view utf8,
                                        const std::filesystem::path& folder, std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kFileName);
    if (!valid_package(package) || package == kOriginal) {
        return core::make_error(core::ErrorCode::InvalidFormat,
                                where + ": a package's id is its folder's name, of a-z, 0-9, '_' and '-', and not " +
                                    std::string(kOriginal));
    }
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        return core::make_error(tables.error().code, where + " " + tables.error().message);
    }
    for (const core::DataTable& table : *tables) {
        if (table.name.empty()) {
            warnings.push_back(line_of(where, table.entries.front().value.line) +
                               "fields before the first [message] belong to none and are passed over");
            continue;
        }
        if (table.name.find(':') != std::string::npos) {
            // Another package's message, changed field by field.
            const auto id = find(table.name);
            if (!id) {
                warnings.push_back(line_of(where, table.line) + "there is no message " + table.name + " to change");
                continue;
            }
            apply(table, m_messages[*id], folder, where, warnings);
            continue;
        }
        MessageDefinition m;
        m.id = std::string(package) + ":" + table.name;
        if (m_ids.contains(m.id)) {
            warnings.push_back(line_of(where, table.line) + m.id + " is defined already");
            continue;
        }
        apply(table, m, folder, where, warnings);
        if (m.texts.empty() && !m.meldung) {
            warnings.push_back(line_of(where, table.line) + m.id + " has no text and no 'like' to take one from");
        }
        const auto index = static_cast<MessageId>(m_messages.size());
        m_ids.emplace(m.id, index);
        m_messages.push_back(std::move(m));
    }
    return core::Ok{};
}

std::optional<std::uint16_t> MessageTable::package_video(const std::filesystem::path& file, const std::string& at,
                                                         std::vector<std::string>& warnings) {
    const std::filesystem::path normal = file.lexically_normal();
    if (const auto it = std::ranges::find(m_videos, normal); it != m_videos.end()) {
        return static_cast<std::uint16_t>(kFirstPackageVideo + (it - m_videos.begin()));
    }
    if (!is_video_file(normal)) {
        warnings.push_back(at + "missing or no Smacker (.smk) or Bink 2 (.bk2) video");
        return std::nullopt;
    }
    if (m_videos.size() >= kMaxPackageVideos) {
        warnings.push_back(at + "the packages have at most " + std::to_string(kMaxPackageVideos) + " videos");
        return std::nullopt;
    }
    m_videos.push_back(normal);
    return static_cast<std::uint16_t>(kFirstPackageVideo + m_videos.size() - 1);
}

void MessageTable::load_mods(std::span<const assets::ModPackage> packages, std::vector<std::string>& warnings) {
    for (const assets::ModPackage& package : packages) {
        const auto bytes = core::read_file(package.folder / kFileName);
        if (!bytes) {
            continue;  // a package without messages
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (const auto status = load_package(package.id, text, package.folder, warnings); !status) {
            warnings.push_back(status.error().message);
        }
    }
}

}  // namespace o1602::app
