// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_settings.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <map>
#include <system_error>
#include <utility>
#include <vector>

#include "o1602/core/file.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

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

template <class F>
void for_lines(std::string_view text, F f) {
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        f(trim(text.substr(0, nl)));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    }
}

bool flag(std::string_view v, bool fallback) {
    if (v == "on" || v == "1" || v == "true") {
        return true;
    }
    if (v == "off" || v == "0" || v == "false") {
        return false;
    }
    return fallback;
}

std::int32_t number(std::string_view v, std::int32_t fallback) {
    std::int32_t n = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    return ec == std::errc{} && end == v.data() + v.size() ? n : fallback;
}

void flags(std::string_view v, std::array<bool, audio::kMessageKinds>& out) {
    std::size_t i = 0;
    for (const char c : v) {
        if ((c == '0' || c == '1') && i < out.size()) {
            out[i++] = c == '1';
        }
    }
}

// The files are UTF-8; the names in them are kept in Windows-1252 like all typed text (the game's
// fonts). A value that is not UTF-8 comes from an older version, which wrote the bytes as they
// were: it is taken as Windows-1252. Control characters and the five bytes Windows-1252 leaves
// undefined are dropped, and the ends trimmed, so that a name reads back as it is written. The
// writer applies the same to a name typed in the game, so that write -> read is the identity (FB-36).
std::string clean_name(std::string_view text) {
    std::string out;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        const bool undefined = c == 0x81 || c == 0x8D || c == 0x8F || c == 0x90 || c == 0x9D;
        if (c >= 0x20 && c != 0x7F && !undefined) {
            out += ch;
        }
    }
    return std::string(trim(out));
}

std::string from_file(std::string_view value) {
    return clean_name(core::is_utf8(value) ? core::utf8_to_cp1252(value) : std::string(value));
}

std::string to_file(std::string_view name) {
    return core::cp1252_to_utf8(clean_name(name));
}

std::string flags_text(const std::array<bool, audio::kMessageKinds>& a) {
    std::string s;
    for (const bool b : a) {
        s += s.empty() ? "" : " ";
        s += b ? '1' : '0';
    }
    return s;
}

}  // namespace

AppSettings parse_settings(std::string_view text, const AppSettings& defaults) {
    AppSettings s = defaults;
    bool songs_listed = false;
    for_lines(core::skip_utf8_bom(text), [&](std::string_view line) {
        if (line.empty() || line.front() == '#') {
            return;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            return;
        }
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));
        audio::AudioSettings& a = s.audio;
        if (key == "logging") {
            s.logging = flag(value, s.logging);
        } else if (key == "music") {
            a.music = flag(value, a.music);
        } else if (key == "effects") {
            a.effects = flag(value, a.effects);
        } else if (key == "random_music") {
            a.random_music = flag(value, a.random_music);
        } else if (key == "music_volume") {
            a.music_volume = std::clamp(number(value, a.music_volume), -10000, 0);
        } else if (key == "effect_volume") {
            a.effect_volume = std::clamp(number(value, a.effect_volume), -10000, 0);
        } else if (key == "speech") {
            flags(value, a.speech);
        } else if (key == "video") {
            flags(value, a.video);
        } else if (key == "disabled_song") {
            if (!songs_listed) {
                a.disabled_songs.clear();  // the file's list replaces the default one
                songs_listed = true;
            }
            a.disabled_songs.emplace_back(value);
        } else if (key == "language") {
            s.language = value.empty() ? std::string("auto") : std::string(value);
        } else if (key == "master_server") {
            s.master_server = value=="off" ? std::string{} : value.empty() ? AppSettings{}.master_server : value.size()<=255 ? std::string(value) : std::string{};
        } else if (key == "game_folder") {
            s.game_folder = std::string(value);
        } else if (key == "player_name") {
            s.player_name = from_file(value);
        } else if (key == "remembered_host") {
            if (!value.empty() && s.remembered_hosts.size() < AppSettings::kMaxHosts) {
                s.remembered_hosts.push_back(from_file(value));
            }
        } else if (key == "disabled_mod") {
            if (!value.empty() &&
                std::find(s.disabled_mods.begin(), s.disabled_mods.end(), value) == s.disabled_mods.end()) {
                s.disabled_mods.emplace_back(value);
            }
        } else if (key == "mission_window") {
            s.mission_window = flag(value, s.mission_window);
        } else if (key == "tutorial_chapter") {
            s.tutorial_chapter = static_cast<std::uint8_t>(std::clamp(number(value, s.tutorial_chapter), 0, 4));
        } else if (key == "sea_creatures_every") {
            s.sea_creatures_every = std::clamp(number(value, s.sea_creatures_every), 0, 3600);
        } else if (key == "data_fixes") {
            s.data_fixes = flag(value, s.data_fixes);
        } else if (key.starts_with("display.")) {
            (void)read_display_line(s.display, key.substr(8), value);
        } else if (key.starts_with("endless.")) {
            (void)read_setup_line(s.endless, key.substr(8), value);
        } else if (key.starts_with("scenario.")) {
            (void)read_setup_line(s.scenario, key.substr(9), value);
        } else if (key.starts_with("multiplayer.")) {
            (void)read_setup_line(s.multiplayer, key.substr(12), value);
        }
    });
    s.scenario.rules.victory = {};  // a scenario has its own missions (victory_offered)
    return s;
}

std::string write_settings(const AppSettings& s) {
    const audio::AudioSettings& a = s.audio;
    std::string out = "# Open1602: settings (written by the game, UTF-8)\n";
    out += std::string("logging = ") + (s.logging ? "on" : "off") + "\n";
    out += std::string("music = ") + (a.music ? "on" : "off") + "\n";
    out += std::string("effects = ") + (a.effects ? "on" : "off") + "\n";
    out += std::string("random_music = ") + (a.random_music ? "on" : "off") + "\n";
    out += "music_volume = " + std::to_string(a.music_volume) + "\n";
    out += "effect_volume = " + std::to_string(a.effect_volume) + "\n";
    out += "speech = " + flags_text(a.speech) + "\n";
    out += "video = " + flags_text(a.video) + "\n";
    for (const std::string& t : a.disabled_songs) {
        out += "disabled_song = " + t + "\n";
    }
    out += std::string("mission_window = ") + (s.mission_window ? "on" : "off") + "\n";
    out += "# Language of Open1602's own texts: auto (as the installation), en, de, ...\n";
    out += "language = " + s.language + "\n";
    out += "# Optional Internet lobby/relay, e.g. 203.0.113.10:16022; empty keeps LAN/Direct-IP only\n";
    out += "# Master: hostname[:port] or IPv4[:port]; off disables the public service.\n";
    out += "master_server = " + (s.master_server.empty()?std::string("off"):s.master_server) + "\n";
    if (!s.game_folder.empty()) {
        out += "# The original game installation chosen at startup\n";
        out += "game_folder = " + s.game_folder + "\n";
    }
    out += "tutorial_chapter = " + std::to_string(s.tutorial_chapter) + "\n";
    out +=
        "# Whales, dolphins, krakens: on average one per screen of sea every this many seconds (original 5, 0 = off)\n";
    out += "sea_creatures_every = " + std::to_string(s.sea_creatures_every) + "\n";
    out += "# Corrections of the original's data (D-001, D-002, D-012, D-014); off: the data as it is\n";
    out += std::string("data_fixes = ") + (s.data_fixes ? "on" : "off") + "\n";
    if (const std::string name = to_file(s.player_name); !name.empty()) {
        out += "player_name = " + name + "\n";
    }
    for (const std::string& host : s.remembered_hosts) {
        if (const std::string text = to_file(host); !text.empty()) {
            out += "remembered_host = " + text + "\n";
        }
    }
    if (!s.disabled_mods.empty()) {
        out += "# Packages of the mods folder turned off in the menu \"Mods\"\n";
    }
    for (const std::string& id : s.disabled_mods) {
        out += "disabled_mod = " + id + "\n";
    }
    out += write_display(s.display);
    out += "# The last settings of the start dialog of the endless game\n";
    out += write_setup(s.endless);
    out += "# The last rules chosen for a scenario\n";
    out += write_setup(s.scenario, "scenario.");
    out += "# The last settings a host chose for a multiplayer game\n";
    out += write_setup(s.multiplayer, "multiplayer.");
    return out;
}

AppSettings load_user_settings(const std::filesystem::path& folder, const AppSettings& defaults,
                               bool separate_preferences) {
    AppSettings settings=defaults;
    const auto overlay=[&](const char* name){
        if(auto bytes=core::read_file(folder/name))
            settings=parse_settings(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()),settings);
    };
    if(separate_preferences)overlay("menu-preferences.cfg");
    overlay("settings.cfg");
    return settings;
}

std::string menu_preferences_text(const AppSettings& settings, std::string_view previous,
                                  std::string_view overrides) {
    using Lines=std::map<std::string,std::vector<std::string>>;
    const auto lines=[](std::string_view text){
        Lines result;
        if(text.starts_with("\xef\xbb\xbf"))text.remove_prefix(3);
        for_lines(text,[&](std::string_view line){
            if(line.empty()||line.front()=='#')return;
            const auto equal=line.find('=');if(equal==std::string_view::npos)return;
            const std::string key(trim(line.substr(0,equal)));
            result[key].push_back(key+" = "+std::string(trim(line.substr(equal+1)))+"\n");
        });
        return result;
    };
    auto current=lines(write_settings(settings));
    const auto defaults=lines(write_settings(AppSettings{}));
    const auto old=lines(previous), manual=lines(overrides);
    for(const auto& [key,values]:old)
        if(manual.contains(key)&&!current.contains(key))current.emplace(key,values);
    std::string result="# Open1602: saved menu preferences (UTF-8). Optional settings.cfg overrides these.\n";
    for(auto& [key,values]:current){
        // Neither option has a menu; keep their compiled defaults unless explicitly configured.
        if(key=="logging"||key=="master_server"||key=="display.vita_cpu_mhz")continue;
        if(manual.contains(key)){
            const auto found=old.find(key);
            if(found==old.end())continue;
            values=found->second;
        }
        const auto def=defaults.find(key);
        if(def!=defaults.end()&&values==def->second)continue;
        for(const auto& value:values)result+=value;
    }
    return result;
}

std::string set_setting_line(std::string_view text, std::string_view key, std::string_view value) {
    const std::string line = std::string(key) + " = " + std::string(value) + "\n";
    std::string out;
    bool set = false;
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        const std::string_view raw = text.substr(0, nl == std::string_view::npos ? text.size() : nl + 1);
        text.remove_prefix(raw.size());
        const std::string_view content = trim(raw.substr(0, raw.find('\n')));
        const std::size_t eq = content.find('=');
        if (!content.empty() && content.front() != '#' && eq != std::string_view::npos &&
            trim(content.substr(0, eq)) == key) {
            if (!std::exchange(set, true)) {
                out += line;
            }
            continue;
        }
        out += raw;
        if (raw.back() != '\n') {
            out += '\n';  // a last line without its line break
        }
    }
    if (!set) {
        out += line;
    }
    return out;
}

void SaveSlots::set(std::size_t slot, std::string name, std::uint8_t human_players) {
    if (slot >= kMaxCount) {
        return;
    }
    if (slot >= names.size()) {
        names.resize(slot + 1);
        humans.resize(slot + 1);
        meta.resize(slot + 1);
    }
    names[slot] = std::move(name);
    humans[slot] = human_players;
    // At least twelve, and one free slot after the last used one.
    std::size_t used = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!names[i].empty()) {
            used = i + 1;
        }
    }
    const std::size_t count = std::min(std::max(kMinCount, used + 1), kMaxCount);
    names.resize(count);
    humans.resize(count);
    meta.resize(count);
}

SaveSlots parse_slots(std::string_view text) {
    SaveSlots slots;
    for_lines(core::skip_utf8_bom(text), [&](std::string_view line) {
        if (line.empty() || line.front() == '#') {
            return;
        }
        const std::size_t a = line.find(' ');
        const std::size_t b = a == std::string_view::npos ? a : line.find(' ', a + 1);
        if (b == std::string_view::npos) {
            return;
        }
        const std::int32_t slot = number(line.substr(0, a), -1);
        const std::int32_t humans = number(line.substr(a + 1, b - a - 1), 0);
        if (slot < 0 || static_cast<std::size_t>(slot) >= SaveSlots::kMaxCount) {
            return;
        }
        slots.set(static_cast<std::size_t>(slot), from_file(line.substr(b + 1)),
                  static_cast<std::uint8_t>(std::clamp(humans, 0, 255)));
    });
    return slots;
}

std::string write_slots(const SaveSlots& slots) {
    std::string out = "# Open1602: savegame names (slot, humans, name; UTF-8)\n";
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (const std::string name = to_file(slots.names[i]); !name.empty()) {
            out += std::to_string(i) + " " + std::to_string(slots.humans[i]) + " " + name + "\n";
        }
    }
    return out;
}

std::filesystem::path slot_file(const std::filesystem::path& folder, std::size_t slot) {
    if (slot == kLastGameSlot) {
        return folder / (std::string(kLastGameName) + std::string(sim::kSaveExtension));
    }
    return folder / (std::string("spiel") + (slot < 10 ? "0" : "") + std::to_string(slot) + ".o1602");
}

void find_last_game(SaveSlots& slots, const std::filesystem::path& folder) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(slot_file(folder, kLastGameSlot), ec)) {
        slots.set(kLastGameSlot, {}, 0);
    } else if (slots.names[kLastGameSlot] != kLastGameName) {
        slots.set(kLastGameSlot, std::string(kLastGameName), 0);  // humans unknown until it is saved again
    }
}

void read_slot_meta(SaveSlots& slots, const std::filesystem::path& folder, std::optional<std::size_t> only) {
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (only && *only != i) {
            continue;
        }
        slots.meta[i].reset();
        if (slots.names[i].empty()) {
            continue;
        }
        // Only the head of each file: a list of large games starts quickly (FB-15).
        if (const auto head = core::read_file_head(slot_file(folder, i), sim::kSaveInfoHead)) {
            if (const auto info = sim::save_info(head->bytes, head->size)) {
                slots.meta[i] = info->meta;
            }
        }
    }
}

std::vector<std::string> slot_details(const SaveSlots& slots) {
    std::vector<std::string> out(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (i < slots.meta.size() && slots.meta[i]) {
            out[i] = save_details(*slots.meta[i]);
        }
    }
    return out;
}

std::string save_time(std::uint64_t ms) {
    const std::uint64_t minutes = ms / 60'000;
    const std::uint64_t m = minutes % 60;
    return ui::tr("save.time", {std::to_string(minutes / 60), (m < 10 ? "0" : "") + std::to_string(m)});
}

std::string save_details(const sim::SaveMeta& meta) {
    return ui::tr("save.details", {meta.player.empty() ? std::string("-") : meta.player, save_time(meta.time_ms),
                                   std::to_string(meta.width), std::to_string(meta.height)});
}

}  // namespace o1602::app
