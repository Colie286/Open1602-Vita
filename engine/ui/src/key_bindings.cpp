// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/key_bindings.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "o1602/core/text.hpp"

namespace o1602::ui {

namespace {

constexpr std::pair<KeyAction, std::string_view> kNames[] = {
    {KeyAction::ScrollUp, "scroll_up"},
    {KeyAction::ScrollDown, "scroll_down"},
    {KeyAction::ScrollLeft, "scroll_left"},
    {KeyAction::ScrollRight, "scroll_right"},
    {KeyAction::RotateViewLeft, "rotate_view_left"},
    {KeyAction::RotateViewRight, "rotate_view_right"},
    {KeyAction::ZoomIn, "zoom_in"},
    {KeyAction::ZoomOut, "zoom_out"},
    {KeyAction::JumpToMessage, "jump_to_message"},
    {KeyAction::CentreOnSelection, "centre_on_selection"},
    {KeyAction::NextCity, "next_city"},
    {KeyAction::Pause, "pause"},
    {KeyAction::PauseVideo, "pause_video"},
    {KeyAction::SpeedUp, "speed_up"},
    {KeyAction::SpeedDown, "speed_down"},
    {KeyAction::QuickSave, "quick_save"},
    {KeyAction::QuickLoad, "quick_load"},
    {KeyAction::GameMenu, "game_menu"},
    {KeyAction::Screenshot, "screenshot"},
    {KeyAction::Help, "help"},
    {KeyAction::Cancel, "cancel"},
    {KeyAction::ModeBuild, "mode_build"},
    {KeyAction::ModeMilitary, "mode_military"},
    {KeyAction::ModeInfo, "mode_info"},
    {KeyAction::ModeOptions, "mode_options"},
    {KeyAction::ShipList, "ship_list"},
    {KeyAction::CityList, "city_list"},
    {KeyAction::Diplomacy, "diplomacy"},
    {KeyAction::GoodsStatistics, "goods_statistics"},
    {KeyAction::Chat, "chat"},
    {KeyAction::RotateBuilding, "rotate_building"},
    {KeyAction::RotateBuildingBack, "rotate_building_back"},
    {KeyAction::PickBuilding, "pick_building"},
    {KeyAction::DemolishTool, "demolish_tool"},
    {KeyAction::NextBuilding, "next_building"},
    {KeyAction::PlantFields, "plant_fields"},
    {KeyAction::ToggleGates, "toggle_gates"},
    {KeyAction::GroupStore, "group_store"},
    {KeyAction::GroupAdd, "group_add"},
    {KeyAction::GroupRecall, "group_recall"},
    {KeyAction::NextIdleShip, "next_idle_ship"},
    {KeyAction::NextIdleUnit, "next_idle_unit"},
    {KeyAction::NextKindInSelection, "next_kind_in_selection"},
    {KeyAction::SelectInfantry, "select_infantry"},
    {KeyAction::SelectMusketeers, "select_musketeers"},
    {KeyAction::SelectCavalry, "select_cavalry"},
    {KeyAction::SelectCannoneers, "select_cannoneers"},
    {KeyAction::AttackMove, "attack_move"},
    {KeyAction::Stop, "stop"},
    {KeyAction::HoldPosition, "hold_position"},
    {KeyAction::Patrol, "patrol"},
    {KeyAction::Formation, "formation"},
    {KeyAction::Scatter, "scatter"},
    {KeyAction::WhiteFlag, "white_flag"},
    {KeyAction::Explore, "explore"},
    {KeyAction::Trade, "trade"},
    {KeyAction::EditRoute, "edit_route"},
    {KeyAction::Delete, "delete"},
    {KeyAction::PixelScaleUp, "pixel_scale_up"},
    {KeyAction::PixelScaleDown, "pixel_scale_down"},
    {KeyAction::ShowOwners, "show_owners"},
    {KeyAction::ShowFertility, "show_fertility"},
    {KeyAction::ShowFps, "show_fps"},
    {KeyAction::ShowCoordinates, "show_coordinates"},
    {KeyAction::ShowUnitArms, "show_unit_arms"},
    {KeyAction::Fullscreen, "fullscreen"},
    {KeyAction::Quit, "quit"},
};

}  // namespace

void KeyBindings::bind(KeyContext context, KeyChord chord, KeyAction action, std::uint8_t param) {
    std::erase_if(m_bindings, [&](const KeyBinding& b) { return b.context == context && b.chord == chord; });
    m_bindings.push_back(KeyBinding{context, chord, action, param});
}

void KeyBindings::unbind(KeyAction action, std::uint8_t param) {
    std::erase_if(m_bindings, [&](const KeyBinding& b) { return b.action == action && b.param == param; });
}

std::optional<KeyBinding> KeyBindings::find(KeyContext context, const KeyChord& chord) const {
    std::optional<KeyBinding> general;
    for (const KeyBinding& b : m_bindings) {
        if (b.chord != chord) {
            continue;
        }
        if (b.context == context) {
            return b;
        }
        if (b.context == KeyContext::General) {
            general = b;
        }
    }
    return general;
}

std::optional<KeyBinding> KeyBindings::find_pressed(KeyContext context, const KeyPress& press) const {
    // 0: the context's own by place, 1: its own by meaning, 2 and 3: the general ones.
    std::optional<KeyBinding> best;
    int best_rank = 4;
    for (const KeyBinding& b : m_bindings) {
        if (!b.chord.matches(press) || (b.context != context && b.context != KeyContext::General)) {
            continue;
        }
        const int rank = (b.context == context ? 0 : 2) + (b.chord.physical ? 0 : 1);
        if (rank < best_rank) {
            best = b;
            best_rank = rank;
        }
    }
    return best;
}

std::vector<KeyChord> KeyBindings::keys(KeyAction action, std::uint8_t param) const {
    std::vector<KeyChord> out;
    for (const KeyBinding& b : m_bindings) {
        if (b.action == action && b.param == param) {
            out.push_back(b.chord);
        }
    }
    return out;
}

std::string_view KeyBindings::name(KeyAction action) noexcept {
    for (const auto& [a, n] : kNames) {
        if (a == action) {
            return n;
        }
    }
    return {};
}

std::optional<KeyAction> KeyBindings::action_named(std::string_view name) noexcept {
    for (const auto& [a, n] : kNames) {
        if (n == name) {
            return a;
        }
    }
    return std::nullopt;
}

namespace {

constexpr std::pair<KeyContext, std::string_view> kContexts[] = {
    {KeyContext::General, "general"},
    {KeyContext::Building, "building"},
    {KeyContext::Units, "units"},
};

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

bool starts_with_nocase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string_view KeyBindings::context_name(KeyContext context) noexcept {
    for (const auto& [c, n] : kContexts) {
        if (c == context) {
            return n;
        }
    }
    return {};
}

std::optional<KeyContext> KeyBindings::context_named(std::string_view name) noexcept {
    for (const auto& [c, n] : kContexts) {
        if (n == name) {
            return c;
        }
    }
    return std::nullopt;
}

std::string KeyBindings::chord_text(const KeyChord& chord, const KeyName& key_name) {
    std::string out;
    if (chord.ctrl) {
        out += "Ctrl+";
    }
    if (chord.shift) {
        out += "Shift+";
    }
    if (chord.alt) {
        out += "Alt+";
    }
    const std::string name = key_name(chord.key, chord.physical);
    return chord.physical ? out + '[' + name + ']' : out + name;
}

std::optional<KeyChord> KeyBindings::parse_chord(std::string_view text, const KeyFromName& key_from_name) {
    KeyChord chord;
    text = trim(text);
    for (bool more = true; more;) {
        more = false;
        for (const auto& [prefix, flag] : {std::pair<std::string_view, bool*>{"ctrl+", &chord.ctrl},
                                           std::pair<std::string_view, bool*>{"shift+", &chord.shift},
                                           std::pair<std::string_view, bool*>{"alt+", &chord.alt}}) {
            if (text.size() > prefix.size() && starts_with_nocase(text, prefix)) {
                *flag = true;
                text.remove_prefix(prefix.size());
                more = true;
            }
        }
    }
    text = trim(text);
    if (text.size() > 2 && text.front() == '[' && text.back() == ']') {
        chord.physical = true;
        text = trim(text.substr(1, text.size() - 2));
    }
    const auto key = key_from_name(text, chord.physical);
    if (!key) {
        return std::nullopt;
    }
    chord.key = *key;
    return chord;
}

std::string KeyBindings::write_text(const KeyName& key_name, bool as_comments) const {
    std::string out =
        "# Open1602: key bindings (ADR 0010), UTF-8. Lines: context action [number] = key\n"
        "# Context: general (always), building (while building), units (units selected).\n"
        "# An action listed here has only the keys of its lines; \"= none\" takes every key from it.\n"
        "# Actions that are missing keep their default. Keys: names like F, Space, PageUp, Keypad +,\n"
        "# preceded by Ctrl+, Shift+, Alt+. A name in brackets, [W], is the key at the place of W on a\n"
        "# US keyboard, whatever the layout prints on it. Lines starting with # are ignored.\n";
    for (const KeyBinding& b : m_bindings) {
        if (as_comments) {
            out += "# ";
        }
        out += context_name(b.context);
        out += ' ';
        out += name(b.action);
        if (b.action == KeyAction::GroupStore || b.action == KeyAction::GroupAdd ||
            b.action == KeyAction::GroupRecall) {
            out += ' ' + std::to_string(b.param);
        }
        out += " = " + chord_text(b.chord, key_name) + '\n';
    }
    return out;
}

std::vector<std::string> KeyBindings::read_text(std::string_view text, const KeyFromName& key_from_name) {
    std::vector<std::string> warnings;
    struct Seen {
        KeyContext context;
        KeyAction action;
        std::uint8_t param;
    };
    std::vector<Seen> seen;
    std::size_t line_no = 0;
    text = core::skip_utf8_bom(text);
    while (!text.empty()) {
        const std::size_t nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        ++line_no;
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto warn = [&](const std::string& what) {
            warnings.push_back("line " + std::to_string(line_no) + ": " + what);
        };
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            warn("no '='");
            continue;
        }
        // Left: context, action and an optional number.
        std::vector<std::string_view> words;
        std::string_view left = trim(line.substr(0, eq));
        while (!left.empty()) {
            const std::size_t sp = left.find_first_of(" \t");
            words.push_back(left.substr(0, sp));
            left = sp == std::string_view::npos ? std::string_view{} : trim(left.substr(sp));
        }
        const auto context = words.empty() ? std::nullopt : context_named(words[0]);
        const auto action = words.size() < 2 ? std::nullopt : action_named(words[1]);
        if (!context || !action || words.size() > 3) {
            warn("unknown context or action");
            continue;
        }
        std::uint8_t param = 0;
        if (words.size() == 3) {
            const std::string_view p = words[2];
            if (p.size() != 1 || p[0] < '0' || p[0] > '9') {
                warn("number 0..9 expected");
                continue;
            }
            param = static_cast<std::uint8_t>(p[0] - '0');
        }
        const std::string_view right = trim(line.substr(eq + 1));
        std::optional<KeyChord> chord;
        if (right != "none") {
            chord = parse_chord(right, key_from_name);
            if (!chord) {
                warn("unknown key '" + std::string(right) + "'");
                continue;
            }
        }
        // The first line of an action replaces its default keys in that context.
        const bool first = std::none_of(seen.begin(), seen.end(), [&](const Seen& s) {
            return s.context == *context && s.action == *action && s.param == param;
        });
        if (first) {
            seen.push_back(Seen{*context, *action, param});
            std::erase_if(m_bindings, [&](const KeyBinding& b) {
                return b.context == *context && b.action == *action && b.param == param;
            });
        }
        if (chord) {
            bind(*context, *chord, *action, param);
        }
    }
    return warnings;
}

}  // namespace o1602::ui
