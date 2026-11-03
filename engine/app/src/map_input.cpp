// SPDX-License-Identifier: GPL-3.0-or-later
#include "map_input.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <span>
#include <string_view>
#include <tuple>

#include "key_help.hpp"
#include "order_questions.hpp"

#include "o1602/core/file.hpp"
#include "o1602/render/island_renderer.hpp"
#include "o1602/sim/combat.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr double kScrollSpeed = 900.0;  // pixels per second
/// How close to the screen's edge the mouse scrolls the map, in window coordinates.
constexpr double kEdgeMargin = 2.0;
/// How far a unit's pictures may reach from its position, in tile widths (ships with sails and
/// soldiers on high ground stay well inside). Units further from a picked rectangle are skipped.
constexpr double kPickMarginTiles = 4.0;

/// What an action does, for the key overview: keys.action.<its name in keys.cfg>.
const std::string& action_label(ui::KeyAction a) {
    return ui::tr("keys.action." + std::string(ui::KeyBindings::name(a)));
}

/// A key as the overview shows it: the usual keys by the names of the language, a key bound by
/// its place by what the keyboard's layout puts there.
std::string key_label(const ui::KeyChord& c) {
    static constexpr std::pair<SDL_Keycode, std::string_view> kNames[] = {
        {SDLK_SPACE, "space"},
        {SDLK_DELETE, "delete"},
        {SDLK_ESCAPE, "escape"},
        {SDLK_PAGEUP, "page_up"},
        {SDLK_PAGEDOWN, "page_down"},
        {SDLK_HOME, "home"},
        {SDLK_UP, "up"},
        {SDLK_DOWN, "down"},
        {SDLK_LEFT, "left"},
        {SDLK_RIGHT, "right"},
        {SDLK_RETURN, "return"},
        {SDLK_PRINTSCREEN, "print_screen"},
        {SDLK_KP_PLUS, "keypad_plus"},
        {SDLK_KP_MINUS, "keypad_minus"},
        {SDLK_KP_ENTER, "keypad_enter"},
        {SDLK_TAB, "tab"},
        {SDLK_PAUSE, "pause"},
    };
    const SDL_Keycode keycode = c.physical
                                    ? SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(c.key), SDL_KMOD_NONE, true)
                                    : static_cast<SDL_Keycode>(c.key);
    std::string name;
    for (const auto& [key, label] : kNames) {
        if (key == keycode) {
            name = ui::tr("keys.name." + std::string(label));
        }
    }
    if (name.empty()) {
        const char* n = SDL_GetKeyName(keycode);
        name = n != nullptr ? n : "?";
    }
    return (c.ctrl ? ui::tr("keys.modifier.ctrl") : std::string()) +
           (c.shift ? ui::tr("keys.modifier.shift") : std::string()) +
           (c.alt ? ui::tr("keys.modifier.alt") : std::string()) + name;
}

ui::KeyChord chord(SDL_Keycode key, bool ctrl = false, bool shift = false) {
    return ui::KeyChord{static_cast<std::int32_t>(key), ctrl, shift, false};
}

/// A key by its place on the keyboard: the same on QWERTY, QWERTZ and AZERTY.
ui::KeyChord place(SDL_Scancode key, bool ctrl = false, bool shift = false) {
    return ui::KeyChord{static_cast<std::int32_t>(key), ctrl, shift, false, true};
}

std::string key_name(std::int32_t key, bool physical) {
    const char* name =
        physical ? SDL_GetScancodeName(static_cast<SDL_Scancode>(key)) : SDL_GetKeyName(static_cast<SDL_Keycode>(key));
    return std::string(name != nullptr ? name : "");
}

std::optional<std::int32_t> key_from_name(std::string_view name, bool physical) {
    const std::string text(name);
    if (physical) {
        const SDL_Scancode sc = SDL_GetScancodeFromName(text.c_str());
        return sc == SDL_SCANCODE_UNKNOWN ? std::nullopt : std::optional<std::int32_t>(static_cast<std::int32_t>(sc));
    }
    const SDL_Keycode key = SDL_GetKeyFromName(text.c_str());
    return key == SDLK_UNKNOWN ? std::nullopt : std::optional<std::int32_t>(static_cast<std::int32_t>(key));
}

/// Buildings a click shows in the panel: workshops to the Kontor, houses and public buildings,
/// the castle (the kinds of HAUS_PRODTYP, so the buildings of mods are included).
bool selectable(const assets::BuildingType& b) {
    const std::int32_t k = b.prod_kind;
    return (k >= sim::prod::kWorkshop && k <= sim::prod::kKontor) ||
           (k >= sim::prod::kHouse && k <= sim::prod::kPublicLast) || k == sim::prod::kHaupt;
}

ui::KeyPress key_press(int key, int scancode, std::uint16_t mod) {
    return ui::KeyPress{key, scancode, (mod & SDL_KMOD_CTRL) != 0, (mod & SDL_KMOD_SHIFT) != 0,
                        (mod & SDL_KMOD_ALT) != 0};
}

ui::MouseButton mouse_button(const SDL_MouseButtonEvent& b) {
    return b.button == SDL_BUTTON_RIGHT ? ui::MouseButton::Right : ui::MouseButton::Left;
}

}  // namespace

MapInput::MapInput(const Parts& parts)
    : m_host(parts.host),
      m_buildings(parts.buildings),
      m_figures(parts.figures),
      m_state(parts.state),
      m_local_player(parts.local_player),
      m_camera(parts.camera),
      m_tool(parts.tool),
      m_commands(parts.commands),
      m_sound(parts.sound),
      m_window(parts.window),
      m_hud(parts.hud),
      m_original_mouse(parts.original_mouse),
      m_confirm(parts.confirm),
      m_keys_file(parts.keys_file) {
    setup_keys();
}

bool MapInput::editor_key(ui::KeyAction action) noexcept {
    using ui::KeyAction;
    switch (action) {
        case KeyAction::ScrollUp:
        case KeyAction::ScrollDown:
        case KeyAction::ScrollLeft:
        case KeyAction::ScrollRight:
        case KeyAction::RotateViewLeft:
        case KeyAction::RotateViewRight:
        case KeyAction::ZoomIn:
        case KeyAction::ZoomOut:
        case KeyAction::Screenshot:
        case KeyAction::Help:
        case KeyAction::Cancel:
        case KeyAction::PixelScaleUp:
        case KeyAction::PixelScaleDown:
        case KeyAction::ShowFps:
        case KeyAction::ShowCoordinates:
        case KeyAction::Fullscreen:
        case KeyAction::Quit:
        case KeyAction::EditorUndo:
        case KeyAction::EditorRedo:
        case KeyAction::EditorSave:
        case KeyAction::EditorNew:
        case KeyAction::EditorOpen:
        case KeyAction::EditorSaveAs:
        case KeyAction::EditorRotate:
        case KeyAction::EditorBrushBigger:
        case KeyAction::EditorBrushSmaller:
        case KeyAction::EditorToolSelect:
        case KeyAction::EditorToolIsland:
        case KeyAction::EditorToolPlaceholder:
        case KeyAction::EditorToolLand:
        case KeyAction::EditorToolWater:
        case KeyAction::EditorToolCoast:
        case KeyAction::EditorToolBrush:
        case KeyAction::EditorToolRandom:
        case KeyAction::EditorToolFill:
        case KeyAction::EditorToolReplace:
        case KeyAction::EditorToolPipette:
        case KeyAction::EditorToolArea:
        case KeyAction::EditorToolPaste:
        case KeyAction::EditorToolBuild:
        case KeyAction::EditorToolTemplate:
        case KeyAction::EditorToolShip:
        case KeyAction::EditorToolUnit:
        case KeyAction::EditorToolResource:
        case KeyAction::EditorToolRemove:
            return true;
        default:
            return false;
    }
}

void MapInput::setup_keys() {
    using ui::KeyAction;
    using ui::KeyContext;
    ui::KeyBindings& k = m_keys;
    const auto general = [&](ui::KeyChord c, KeyAction a, std::uint8_t p = 0) { k.bind(KeyContext::General, c, a, p); };
    const auto building = [&](ui::KeyChord c, KeyAction a, std::uint8_t p = 0) {
        k.bind(KeyContext::Building, c, a, p);
    };
    const auto units = [&](ui::KeyChord c, KeyAction a) { k.bind(KeyContext::Units, c, a); };
    // Camera and game. W A S D, Q and E by their place, as the digits of the groups: on AZERTY they
    // lie where Z Q S D, A and E are. Keys that stand for a word (B for building) go by meaning.
    general(place(SDL_SCANCODE_W), KeyAction::ScrollUp);
    general(chord(SDLK_UP), KeyAction::ScrollUp);
    general(place(SDL_SCANCODE_S), KeyAction::ScrollDown);
    general(chord(SDLK_DOWN), KeyAction::ScrollDown);
    general(place(SDL_SCANCODE_A), KeyAction::ScrollLeft);
    general(chord(SDLK_LEFT), KeyAction::ScrollLeft);
    general(place(SDL_SCANCODE_D), KeyAction::ScrollRight);
    general(chord(SDLK_RIGHT), KeyAction::ScrollRight);
    general(place(SDL_SCANCODE_Q), KeyAction::RotateViewLeft);
    general(place(SDL_SCANCODE_E), KeyAction::RotateViewRight);
    general(chord(SDLK_PAGEUP), KeyAction::ZoomIn);
    general(chord(SDLK_PAGEDOWN), KeyAction::ZoomOut);
    general(chord(SDLK_SPACE), KeyAction::JumpToMessage);
    general(chord(SDLK_HOME), KeyAction::CentreOnSelection);
    general(chord(SDLK_N), KeyAction::NextCity);
    general(chord(SDLK_F3), KeyAction::Pause);
    general(chord(SDLK_PAUSE), KeyAction::PauseVideo);
    // + and - of the main keys first (keyboards without a keypad; "=" is the + key of QWERTY), then
    // the keypad's.
    general(chord(SDLK_PLUS), KeyAction::SpeedUp);
    general(chord(SDLK_EQUALS), KeyAction::SpeedUp);
    general(chord(SDLK_MINUS), KeyAction::SpeedDown);
    general(chord(SDLK_KP_PLUS), KeyAction::SpeedUp);
    general(chord(SDLK_KP_MINUS), KeyAction::SpeedDown);
    general(chord(SDLK_F5), KeyAction::QuickSave);
    general(chord(SDLK_F9), KeyAction::QuickLoad);
    general(chord(SDLK_F10), KeyAction::GameMenu);
    general(chord(SDLK_F12), KeyAction::Screenshot);
    general(chord(SDLK_PRINTSCREEN), KeyAction::Screenshot);
    general(chord(SDLK_F1), KeyAction::Help);
    general(chord(SDLK_ESCAPE), KeyAction::Cancel);
    // Windows
    general(chord(SDLK_B), KeyAction::ModeBuild);
    general(chord(SDLK_K), KeyAction::ModeMilitary);
    general(chord(SDLK_I), KeyAction::ModeInfo);
    general(chord(SDLK_O), KeyAction::ModeOptions);
    general(chord(SDLK_L), KeyAction::ShipList);
    general(chord(SDLK_L, false, true), KeyAction::CityList);
    general(chord(SDLK_V), KeyAction::Diplomacy);
    general(chord(SDLK_F2), KeyAction::GoodsStatistics);
    general(chord(SDLK_TAB), KeyAction::GoodsStatistics);  // with units selected Tab narrows them
    general(chord(SDLK_RETURN), KeyAction::Chat);
    general(chord(SDLK_KP_ENTER), KeyAction::Chat);
    general(chord(SDLK_DELETE), KeyAction::DemolishTool);
    general(chord(SDLK_C, true), KeyAction::CopyTool);
    general(chord(SDLK_S, true), KeyAction::SaveTemplate);
    general(chord(SDLK_U), KeyAction::BlueprintMode);  // "Umriss"
    general(chord(SDLK_U, false, true), KeyAction::BuildBlueprints);
    general(chord(SDLK_F), KeyAction::PlantFields);  // "Felder"
    general(chord(SDLK_G), KeyAction::ToggleGates);  // "Gates"; with units selected G holds them
    // Groups: keys 1 .. 9, 0 are groups 0 .. 9.
    const std::array<SDL_Scancode, 10> digits{SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
                                              SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
                                              SDL_SCANCODE_9, SDL_SCANCODE_0};
    for (std::size_t g = 0; g < digits.size(); ++g) {
        const auto p = static_cast<std::uint8_t>(g);
        general(place(digits[g], true), KeyAction::GroupStore, p);
        general(place(digits[g], false, true), KeyAction::GroupAdd, p);
        general(place(digits[g]), KeyAction::GroupRecall, p);
    }
    general(chord(SDLK_PERIOD), KeyAction::NextIdleShip);
    general(chord(SDLK_COMMA), KeyAction::NextIdleUnit);
    // All own units of a kind on screen (History Edition): Alt with the first four digits.
    general(ui::KeyChord{SDL_SCANCODE_1, false, false, true, true}, KeyAction::SelectInfantry);
    general(ui::KeyChord{SDL_SCANCODE_2, false, false, true, true}, KeyAction::SelectMusketeers);
    general(ui::KeyChord{SDL_SCANCODE_3, false, false, true, true}, KeyAction::SelectCavalry);
    general(ui::KeyChord{SDL_SCANCODE_4, false, false, true, true}, KeyAction::SelectCannoneers);
    // Viewer
    general(chord(SDLK_PLUS, true), KeyAction::PixelScaleUp);
    general(chord(SDLK_EQUALS, true), KeyAction::PixelScaleUp);
    general(chord(SDLK_MINUS, true), KeyAction::PixelScaleDown);
    general(chord(SDLK_O, true), KeyAction::ShowOwners);
    general(chord(SDLK_B, true), KeyAction::ShowFertility);  // "Boden"
    general(chord(SDLK_F, true), KeyAction::ShowFps);
    general(chord(SDLK_K, true), KeyAction::ShowCoordinates);
    general(chord(SDLK_M), KeyAction::ShowUnitArms);
    general(chord(SDLK_F11), KeyAction::Fullscreen);
    general(ui::KeyChord{static_cast<std::int32_t>(SDLK_RETURN), false, false, true}, KeyAction::Fullscreen);
    general(chord(SDLK_Q, true), KeyAction::Quit);
    // The editor (ADR 0013)
    const auto editor = [&](ui::KeyChord c, KeyAction a) { k.bind(KeyContext::Editor, c, a); };
    editor(chord(SDLK_Z, true), KeyAction::EditorUndo);
    editor(chord(SDLK_Y, true), KeyAction::EditorRedo);
    editor(chord(SDLK_Z, true, true), KeyAction::EditorRedo);
    editor(chord(SDLK_S, true), KeyAction::EditorSave);
    editor(chord(SDLK_N, true), KeyAction::EditorNew);
    editor(chord(SDLK_O, true), KeyAction::EditorOpen);
    editor(chord(SDLK_S, true, true), KeyAction::EditorSaveAs);
    editor(chord(SDLK_R), KeyAction::EditorRotate);
    editor(chord(SDLK_PLUS), KeyAction::EditorBrushBigger);
    editor(chord(SDLK_EQUALS), KeyAction::EditorBrushBigger);
    editor(chord(SDLK_KP_PLUS), KeyAction::EditorBrushBigger);
    editor(chord(SDLK_MINUS), KeyAction::EditorBrushSmaller);
    editor(chord(SDLK_KP_MINUS), KeyAction::EditorBrushSmaller);
    // The tools: keys that the view's (W A S D, Q, E by their place) leave free.
    editor(chord(SDLK_V), KeyAction::EditorToolSelect);
    editor(chord(SDLK_I), KeyAction::EditorToolIsland);
    editor(chord(SDLK_H), KeyAction::EditorToolPlaceholder);
    editor(chord(SDLK_L), KeyAction::EditorToolLand);
    editor(chord(SDLK_O), KeyAction::EditorToolWater);
    editor(chord(SDLK_K), KeyAction::EditorToolCoast);
    editor(chord(SDLK_P), KeyAction::EditorToolBrush);
    editor(chord(SDLK_Z), KeyAction::EditorToolRandom);
    editor(chord(SDLK_F), KeyAction::EditorToolFill);
    editor(chord(SDLK_F, false, true), KeyAction::EditorToolReplace);
    editor(chord(SDLK_C), KeyAction::EditorToolPipette);
    editor(chord(SDLK_B), KeyAction::EditorToolArea);
    editor(chord(SDLK_V, true, false), KeyAction::EditorToolPaste);
    editor(chord(SDLK_G), KeyAction::EditorToolBuild);
    editor(chord(SDLK_T), KeyAction::EditorToolTemplate);
    editor(chord(SDLK_S, false, true), KeyAction::EditorToolShip);
    editor(chord(SDLK_U), KeyAction::EditorToolUnit);
    editor(chord(SDLK_R, false, true), KeyAction::EditorToolResource);
    editor(chord(SDLK_X), KeyAction::EditorToolRemove);
    // Building
    building(chord(SDLK_R), KeyAction::RotateBuilding);
    building(chord(SDLK_R, false, true), KeyAction::RotateBuildingBack);
    building(chord(SDLK_C), KeyAction::PickBuilding);
    building(chord(SDLK_DELETE), KeyAction::DemolishTool);
    building(chord(SDLK_C, true), KeyAction::CopyTool);
    building(chord(SDLK_S, true), KeyAction::SaveTemplate);
    building(chord(SDLK_U), KeyAction::BlueprintMode);
    building(chord(SDLK_U, false, true), KeyAction::BuildBlueprints);
    building(chord(SDLK_F), KeyAction::PlantFields);
    // Orders to the selected units
    // None of these means anything in general (no clash with W A S D).
    units(chord(SDLK_F), KeyAction::AttackMove);  // "Feuer"
    units(chord(SDLK_H), KeyAction::Stop);        // "Halt"
    units(chord(SDLK_G), KeyAction::HoldPosition);
    units(chord(SDLK_P), KeyAction::Patrol);
    units(chord(SDLK_C), KeyAction::Formation);
    units(chord(SDLK_C, false, true), KeyAction::Scatter);
    units(chord(SDLK_Y), KeyAction::WhiteFlag);
    units(chord(SDLK_X), KeyAction::Explore);
    units(chord(SDLK_T), KeyAction::Trade);
    units(chord(SDLK_R), KeyAction::EditRoute);
    units(chord(SDLK_DELETE), KeyAction::Delete);
    units(chord(SDLK_TAB), KeyAction::NextKindInSelection);
}

void MapInput::load_key_settings(const std::filesystem::path& default_file) {
    const std::filesystem::path path = m_keys_file.empty() ? default_file : m_keys_file;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (!m_keys_file.empty()) {
            std::fprintf(stderr, "keys: %s not found, defaults used\n", core::path_to_utf8(path).c_str());
            return;
        }
        // The defaults as a file to edit (in the user's folder, never in the installation).
        const std::string text = m_keys.write_text(key_name, true);
        std::filesystem::create_directories(path.parent_path(), ec);
        const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
        if (auto status = core::write_file_atomic(path, std::span<const std::uint8_t>(data, text.size())); !status) {
            std::fprintf(stderr, "keys: %s\n", status.error().message.c_str());
        }
        return;
    }
    auto bytes = core::read_file(path);
    if (!bytes) {
        std::fprintf(stderr, "keys: %s\n", bytes.error().message.c_str());
        return;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    for (const std::string& w : m_keys.read_text(text, key_from_name)) {
        std::fprintf(stderr, "keys: %s: %s\n", core::path_to_utf8(path).c_str(), w.c_str());
    }
}

void MapInput::show_key_help() {
    if (!m_hud) {
        return;
    }
    using ui::KeyAction;
    std::vector<KeyHelpEntry> entries;
    for (const ui::KeyBinding& b : m_keys.all()) {
        if (m_editor ? !editor_key(b.action) : b.context == ui::KeyContext::Editor) {
            continue;
        }
        std::string key = key_label(b.chord);
        if (b.action == KeyAction::GroupStore || b.action == KeyAction::GroupAdd ||
            b.action == KeyAction::GroupRecall) {
            if (b.param != 0) {
                continue;
            }
            key = key.substr(0, key.size() - 1) + ui::tr("keys.group_digits");  // the digits, 1 for group 1
        }
        entries.push_back(KeyHelpEntry{b.action, std::move(key)});
    }
    auto pages = key_help_pages(entries, [](KeyAction a) { return action_label(a); });
    add_click_lines(pages);
    m_hud->show_help(std::move(pages));
}

std::string MapInput::editor_keys(ui::KeyAction action) const {
    for (const ui::KeyBinding& b : m_keys.all()) {
        if (b.action == action && b.context == ui::KeyContext::Editor) {
            return key_label(b.chord);
        }
    }
    return {};
}

ui::KeyContext MapInput::key_context() const {
    if (m_editor) {
        return ui::KeyContext::Editor;
    }
    if (m_tool.active() || (m_hud && m_hud->mode() == Hud::kModeBuild)) {
        return ui::KeyContext::Building;
    }
    if (!m_units.empty() && m_units.owner(world()) == local()) {
        return ui::KeyContext::Units;
    }
    return ui::KeyContext::General;
}

void MapInput::handle_key(int key, int scancode, std::uint16_t mod) {
    const ui::KeyPress press = key_press(key, scancode, mod);
    if (m_hud && m_hud->menu_active() && !m_hud->asking() && !m_hud->helping()) {
        // The menu knows Escape (the window "Anzeige" closed, or a screen back) and the program's
        // keys, nothing of the game.
        if (key == SDLK_ESCAPE) {
            (void)m_hud->back();
            return;
        }
        if (const auto b = m_keys.find_pressed(ui::KeyContext::General, press);
            b && (b->action == ui::KeyAction::Screenshot || b->action == ui::KeyAction::Quit ||
                  b->action == ui::KeyAction::Fullscreen || b->action == ui::KeyAction::Help)) {
            run_action(*b);
        }
        return;
    }
    if (m_hud && m_hud->end_screen_active() && (key == SDLK_LEFT || key == SDLK_RIGHT)) {
        m_hud->end_screen_step(key == SDLK_LEFT ? -1 : 1);
        return;
    }
    if (m_hud && m_hud->helping()) {
        // Tab and the arrows turn the pages of the key overview, any other key closes it.
        const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
        if (key == SDLK_TAB || key == SDLK_RIGHT || key == SDLK_LEFT) {
            m_hud->turn_help_page(key == SDLK_LEFT || (key == SDLK_TAB && shift) ? -1 : 1);
            return;
        }
        if (key == SDLK_LSHIFT || key == SDLK_RSHIFT) {
            return;  // Shift on its way to Shift+Tab
        }
        m_hud->close_help();
        return;
    }
    if (m_hud && m_hud->choosing()) {
        // A choice: the keys 1 to 9 take that option, Enter and Escape the first.
        std::optional<int> index;
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            index = 0;
        } else if (key >= static_cast<int>(SDLK_1) && key <= static_cast<int>(SDLK_9)) {
            const int n = key - static_cast<int>(SDLK_1);
            if (static_cast<std::size_t>(n) < m_hud->choice_count()) {
                index = n;
            }
        }
        if (index) {
            m_hud->close_question();
            Hud::Action a{Hud::Action::Kind::Answer};
            a.step = *index;
            m_host.apply_hud(a);
        }
        return;
    }
    if (m_hud && m_hud->asking()) {
        // The question: Enter answers yes, Escape no.
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
            m_hud->close_question();
            Hud::Action a{Hud::Action::Kind::Answer};
            a.step = key == SDLK_ESCAPE ? 0 : 1;
            m_host.apply_hud(a);
        }
        return;
    }
    if (cheat_key(key, scancode, mod)) {
        return;
    }
    if (m_hud && m_hud->display_open() && key == SDLK_ESCAPE) {
        m_hud->close_display();
        return;
    }
    if (m_hud && m_hud->goods_open() && key == SDLK_ESCAPE) {
        m_hud->goods_back();
        return;
    }
    if (const auto b = m_keys.find_pressed(key_context(), press)) {
        run_action(*b);
    }
}

bool MapInput::cheat_key(int pressed, int scancode, std::uint16_t mod) {
    using Key = CheatConsole::Key;
    // The key without the modifiers in the keyboard's layout: Shift+M is "m" here, as the
    // original's letters are.
    const auto key = static_cast<int>(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode), SDL_KMOD_NONE, true));
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    const bool alt = (mod & SDL_KMOD_ALT) != 0;
    const bool plain = !shift && !ctrl && !alt;
    std::optional<Key> k;
    char digit = 0;
    if (key == SDLK_W && shift && ctrl && alt) {
        k = Key::Open;
    } else if (key >= static_cast<int>(SDLK_0) && key <= static_cast<int>(SDLK_9) && !ctrl && !alt) {
        k = Key::Digit;
        digit = static_cast<char>('0' + (key - static_cast<int>(SDLK_0)));
    } else if (pressed >= static_cast<int>(SDLK_KP_1) && pressed <= static_cast<int>(SDLK_KP_9)) {
        k = Key::Digit;
        digit = static_cast<char>('1' + (pressed - static_cast<int>(SDLK_KP_1)));
    } else if (pressed == SDLK_KP_0) {
        k = Key::Digit;
        digit = '0';
    } else if (pressed == SDLK_BACKSPACE) {
        k = Key::Backspace;
    } else if (pressed == SDLK_RETURN || pressed == SDLK_KP_ENTER) {
        k = Key::Enter;
    } else if (pressed == SDLK_ESCAPE) {
        k = Key::Escape;
    } else if (key == SDLK_A && plain) {
        k = Key::A;
    } else if (key == SDLK_V && plain) {
        k = Key::V;
    } else if (shift && !ctrl && !alt) {
        if (key == SDLK_M) {
            k = Key::ShiftM;
        } else if (key == SDLK_Z) {
            k = Key::ShiftZ;
        } else if (key == SDLK_T) {
            k = Key::ShiftT;
        } else if (key == SDLK_H) {
            k = Key::ShiftH;
        } else if (key == SDLK_K) {
            k = Key::ShiftK;
        }
    }
    if (!k) {
        if (!m_cheats.open()) {
            return false;
        }
        k = Key::Other;  // the open line takes every key
    }
    // The selected ship, else the city under the mouse.
    CheatConsole::Target target;
    for (const ui::UnitRef& r : m_units.units()) {
        if (r.kind == ui::UnitRef::Kind::Ship) {
            target.ship = r.id;
            break;
        }
    }
    if (const auto tile = tile_at_mouse()) {
        target.x = tile->x;
        target.y = tile->y;
    }
    const CheatConsole::Result r = m_cheats.key(*k, digit, simulation().cheats_allowed(local()), local(), target);
    if (r.order) {
        submit({*r.order}, false, false);
    }
    return r.taken;
}

void MapInput::scroll_with_keys(std::uint32_t delta_ms) {
    if (m_hud && (m_hud->modal() || m_hud->typing())) {
        return;
    }
    const bool* state = SDL_GetKeyboardState(nullptr);
    const SDL_Keymod mod = SDL_GetModState();
    const bool fast = (mod & SDL_KMOD_SHIFT) != 0;
    const ui::KeyContext context = key_context();
    const auto held = [&](ui::KeyAction action) {
        for (const ui::KeyChord& k : m_keys.keys(action)) {
            // A key that means something else here does not scroll (none does with the defaults).
            const SDL_Scancode sc = k.physical ? static_cast<SDL_Scancode>(k.key)
                                               : SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(k.key), nullptr);
            const auto b = m_keys.find_pressed(
                context,
                key_press(static_cast<int>(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, true)), static_cast<int>(sc), 0));
            if (b && b->action != action) {
                continue;
            }
            if (sc != SDL_SCANCODE_UNKNOWN && state[sc] && (mod & SDL_KMOD_CTRL) == 0) {
                return true;
            }
        }
        return false;
    };
    double dx = 0.0;
    double dy = 0.0;
    dx -= held(ui::KeyAction::ScrollLeft) ? 1.0 : 0.0;
    dx += held(ui::KeyAction::ScrollRight) ? 1.0 : 0.0;
    dy -= held(ui::KeyAction::ScrollUp) ? 1.0 : 0.0;
    dy += held(ui::KeyAction::ScrollDown) ? 1.0 : 0.0;
    if (dx != 0.0 || dy != 0.0) {
        const double distance = kScrollSpeed * (fast ? 3.0 : 1.0) * delta_ms / 1000.0;
        m_camera.scroll(dx * distance, dy * distance);
    }
}

bool MapInput::edge_scroll_paused() const noexcept {
    return m_hud != nullptr && (m_hud->modal() || m_hud->typing() || m_hud->menu_active());
}

void MapInput::scroll_at_edges(std::uint32_t delta_ms, std::int32_t speed_percent) {
    if (edge_scroll_paused() || m_drag.dragging() || !m_window.mouse_confined()) {
        return;
    }
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window.output_size(w, h);
    const double margin = std::max(1.0, std::round(kEdgeMargin * static_cast<double>(m_window.pixel_density())));
    const EdgeScroll e = edge_scroll_at(m_mouse_x, m_mouse_y, w, h, margin);
    if (e.dx != 0 || e.dy != 0) {
        const bool fast = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        const double distance = kScrollSpeed * speed_percent / 100.0 * (fast ? 3.0 : 1.0) * delta_ms / 1000.0;
        m_camera.scroll(e.dx * distance, e.dy * distance);
    }
}

void MapInput::run_action(const ui::KeyBinding& b) {
    using ui::KeyAction;
    std::int32_t w = 0;
    std::int32_t h = 0;
    m_window.output_size(w, h);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    if (m_editor && !editor_key(b.action)) {
        return;  // a key of the game: the editor's world does not run (ADR 0013)
    }
    switch (b.action) {
        case KeyAction::ScrollUp:
        case KeyAction::ScrollDown:
        case KeyAction::ScrollLeft:
        case KeyAction::ScrollRight:
            break;  // while held (scroll_with_keys)
        case KeyAction::RotateViewLeft:
        case KeyAction::RotateViewRight:
            m_camera.set_rotation(
                static_cast<std::uint8_t>(m_camera.view().rotation + (b.action == KeyAction::RotateViewLeft ? 3 : 1)),
                cx, cy);
            break;
        case KeyAction::ZoomIn:
            m_host.zoom_by(-kKeyZoomSteps, cx, cy);
            break;
        case KeyAction::ZoomOut:
            m_host.zoom_by(kKeyZoomSteps, cx, cy);
            break;
        case KeyAction::JumpToMessage:
            if (const auto t = m_hud ? m_hud->last_message_tile() : std::nullopt) {
                m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, t->first)),
                                                   static_cast<std::uint32_t>(std::max(0.0, t->second))});
            }
            break;
        case KeyAction::CentreOnSelection:
            if (const auto c = m_units.centre(world())) {
                m_camera.center_on(render::TilePos{static_cast<std::uint32_t>(std::max(0.0, c->first)),
                                                   static_cast<std::uint32_t>(std::max(0.0, c->second))});
            } else {
                m_camera.center_on(m_host.home_tile());
            }
            break;
        case KeyAction::NextCity: {
            // The player's cities with a Kontor, one after the other (History Edition).
            std::vector<std::size_t> own;
            const std::vector<sim::City>& cities = world().cities();
            for (std::size_t i = 0; i < cities.size(); ++i) {
                const sim::City& c = cities[i];
                if (c.owner == local() && c.warehouse_x && c.warehouse_y && c.island < world().islands().size()) {
                    own.push_back(i);
                }
            }
            if (const auto next = next_in_cycle(own, m_last_city)) {
                m_last_city = own[*next];
                const sim::City& c = cities[own[*next]];
                const sim::WorldIsland& island = world().islands()[c.island];
                m_camera.center_on(render::TilePos{island.x + *c.warehouse_x, island.y + *c.warehouse_y});
            }
            break;
        }
        case KeyAction::Pause:
            m_commands.submit(sim::GameSpeedOrder{
                local(), simulation().paused() ? sim::GameSpeedOrder::Op::Resume : sim::GameSpeedOrder::Op::Pause,
                simulation().speed()});
            break;
        case KeyAction::PauseVideo:
            m_host.toggle_pause_video();
            break;
        case KeyAction::SpeedUp:
            m_commands.submit(
                sim::GameSpeedOrder{local(), sim::GameSpeedOrder::Op::SetSpeed,
                                    std::min(simulation().rules().max_game_speed, simulation().speed() * 2)});
            break;
        case KeyAction::SpeedDown:
            m_commands.submit(sim::GameSpeedOrder{local(), sim::GameSpeedOrder::Op::SetSpeed,
                                                  std::max(1U, simulation().speed() / 2)});
            break;
        case KeyAction::QuickSave:
            m_host.quick_save();
            break;
        case KeyAction::QuickLoad:
            m_host.quick_load();
            break;
        case KeyAction::GameMenu:  // the original's game menu is the options with saving and loading
            set_mode(Hud::kModeOptions);
            break;
        case KeyAction::Screenshot:  // U-002: the original's screenshot key does not work
            m_host.request_screenshot();
            break;
        case KeyAction::Help:
            show_key_help();
            break;
        case KeyAction::Cancel:
            if (m_editor) {
                if (m_hud) {
                    (void)m_hud->back();
                }
                break;
            }
            cancel_action();
            break;
        case KeyAction::EditorUndo:
        case KeyAction::EditorRedo:
        case KeyAction::EditorSave:
        case KeyAction::EditorNew:
        case KeyAction::EditorOpen:
        case KeyAction::EditorSaveAs:
        case KeyAction::EditorRotate:
        case KeyAction::EditorBrushBigger:
        case KeyAction::EditorBrushSmaller:
        case KeyAction::EditorToolSelect:
        case KeyAction::EditorToolIsland:
        case KeyAction::EditorToolPlaceholder:
        case KeyAction::EditorToolLand:
        case KeyAction::EditorToolWater:
        case KeyAction::EditorToolCoast:
        case KeyAction::EditorToolBrush:
        case KeyAction::EditorToolRandom:
        case KeyAction::EditorToolFill:
        case KeyAction::EditorToolReplace:
        case KeyAction::EditorToolPipette:
        case KeyAction::EditorToolArea:
        case KeyAction::EditorToolPaste:
        case KeyAction::EditorToolBuild:
        case KeyAction::EditorToolTemplate:
        case KeyAction::EditorToolShip:
        case KeyAction::EditorToolUnit:
        case KeyAction::EditorToolResource:
        case KeyAction::EditorToolRemove:
            if (m_editor) {
                m_host.editor_key(b.action);
            }
            break;
        case KeyAction::ModeBuild:
            set_mode(m_hud && m_hud->mode() == Hud::kModeBuild ? Hud::kModeInfo : Hud::kModeBuild);
            if (!m_hud) {
                m_tool.set_mode(m_tool.mode() == BuildTool::Mode::Build ? BuildTool::Mode::Off
                                                                        : BuildTool::Mode::Build);
            }
            break;
        case KeyAction::ModeMilitary:
            set_mode(Hud::kModeMilitary);
            break;
        case KeyAction::ModeInfo:
            set_mode(Hud::kModeInfo);
            break;
        case KeyAction::ModeOptions:
            set_mode(Hud::kModeOptions);
            break;
        case KeyAction::ShipList:
        case KeyAction::CityList:
            if (m_hud) {
                if (const auto a = m_hud->toggle_window(b.action == KeyAction::ShipList ? Selection::Kind::ShipList
                                                                                        : Selection::Kind::CityList)) {
                    m_host.apply_hud(*a);
                }
            }
            break;
        case KeyAction::GoodsStatistics:
            if (m_hud) {
                m_hud->toggle_goods();
            }
            break;
        case KeyAction::Chat:
            if (m_hud) {
                (void)m_hud->open_chat();  // only in a multiplayer game
            }
            break;
        case KeyAction::Diplomacy:
            if (m_hud) {
                if (const auto a = m_hud->toggle_window(Selection::Kind::Diplomacy)) {
                    m_host.apply_hud(*a);
                }
            }
            break;
        case KeyAction::RotateBuilding:
            m_tool.rotate(1);
            break;
        case KeyAction::RotateBuildingBack:
            m_tool.rotate(-1);
            break;
        case KeyAction::PickBuilding:
            // The pipette: the building under the mouse becomes the order, as the player can order
            // it (a grown house as the first house, a road crossing as a road, a ripe field sown).
            if (const auto spot = spot_at_mouse()) {
                const auto at = sim::building_at(world(), m_buildings, spot->island, {spot->x, spot->y});
                if (const assets::BuildingType* pick = at ? m_tool.orderable(*at->type) : nullptr) {
                    if (m_hud) {
                        (void)m_hud->show_in_build_menu(pick->id);  // the panel shows what is in hand
                    }
                    set_mode(Hud::kModeBuild);
                    if (m_tool.select(pick->id)) {
                        m_tool.set_mode(BuildTool::Mode::Build);
                        drop_units();
                    }
                }
            }
            break;
        case KeyAction::DemolishTool:
            if (m_hud && m_hud->mode() != Hud::kModeBuild) {
                set_mode(Hud::kModeBuild);
            }
            m_tool.set_mode(m_tool.mode() == BuildTool::Mode::Demolish ? BuildTool::Mode::Off
                                                                       : BuildTool::Mode::Demolish);
            if (m_hud && m_tool.mode() == BuildTool::Mode::Demolish) {
                m_hud->show_tool_in_build_menu(BuildPanel::Tool::Demolish);
            }
            if (m_tool.active()) {
                drop_units();
            }
            break;
        case KeyAction::CopyTool:
            // The copy tool (Open1602): a piece of the city to build again elsewhere.
            if (m_hud && m_hud->mode() != Hud::kModeBuild) {
                set_mode(Hud::kModeBuild);
            }
            // Always starts it afresh (the piece in hand goes), so that the menu's title, which names
            // the copy tool, never names a tool put away; Escape puts it away.
            m_tool.set_mode(BuildTool::Mode::Copy);
            if (m_hud) {
                m_hud->show_tool_in_build_menu(BuildPanel::Tool::Copy);
            }
            if (m_tool.active()) {
                drop_units();
            }
            break;
        case KeyAction::SaveTemplate:
            // Templates (Open1602): the piece in hand under a name, else the list of them.
            m_host.save_template();
            break;
        case KeyAction::BlueprintMode:
            toggle_blueprint();
            break;
        case KeyAction::BuildBlueprints:
            build_blueprints();
            break;
        case KeyAction::PlantFields:
            plant_fields();
            break;
        case KeyAction::ToggleGates:
            toggle_gates();
            break;
        case KeyAction::GroupStore:
            m_units.store_group(b.param);
            break;
        case KeyAction::GroupAdd:
            m_units.add_to_group(world(), b.param);
            break;
        case KeyAction::GroupRecall: {
            const bool twice = m_group_key.press(b.param, platform::ticks_ms());
            if (m_units.recall_group(world(), b.param)) {
                selection_changed(false);
                if (twice) {
                    run_action(ui::KeyBinding{ui::KeyContext::General, {}, KeyAction::CentreOnSelection, 0});
                }
            }
            break;
        }
        case KeyAction::NextIdleShip:
        case KeyAction::NextIdleUnit: {
            // The next of the player's ships (soldiers) without an order, after the selected one.
            const bool ships = b.action == KeyAction::NextIdleShip;
            std::vector<ui::UnitRef> idle;
            if (ships) {
                for (const sim::Ship& s : world().ships()) {
                    if (s.owner == local() && !s.sinking && !s.cart() && s.target.empty() && !s.route.active) {
                        idle.push_back({ui::UnitRef::Kind::Ship, s.id});
                    }
                }
            } else {
                for (const sim::Soldier& s : world().soldiers()) {
                    if (s.owner == local() && !s.away() && s.target.empty() && !s.patrol.active) {
                        idle.push_back({ui::UnitRef::Kind::Soldier, s.id});
                    }
                }
            }
            const auto next = next_idle(
                idle, m_units.size() == 1 ? std::optional<ui::UnitRef>(m_units.units().front()) : std::nullopt);
            if (!next) {
                break;
            }
            m_units.select(world(), idle[*next]);
            selection_changed(false);
            run_action(ui::KeyBinding{ui::KeyContext::General, {}, KeyAction::CentreOnSelection, 0});
            break;
        }
        case KeyAction::SelectInfantry:
        case KeyAction::SelectMusketeers:
        case KeyAction::SelectCavalry:
        case KeyAction::SelectCannoneers: {
            // FIGUR Kind: 1 infantry, 2 cavalry, 3 musketeers, 4 cannoneers.
            const std::int32_t kind = b.action == KeyAction::SelectInfantry     ? 1
                                      : b.action == KeyAction::SelectCavalry    ? 2
                                      : b.action == KeyAction::SelectMusketeers ? 3
                                                                                : 4;
            if (m_hud) {
                const ui::Rect& v = m_hud->game_view();
                if (m_units.select_kind(world(), m_figures, units_in(v.x, v.y, v.x + v.w, v.y + v.h), local(), kind)) {
                    selection_changed(false);
                }
            }
            break;
        }
        case KeyAction::NextKindInSelection:
            if (m_units.cycle_types(world())) {
                selection_changed(false);
            }
            break;
        case KeyAction::AttackMove:
            m_pick = Pick::Attack;
            break;
        case KeyAction::Stop:
            submit_orders(m_units.stop_orders(world(), local()), false);
            break;
        case KeyAction::HoldPosition:
            submit_orders(m_units.hold_orders(world(), local()), false);
            break;
        case KeyAction::Patrol:
            m_pick = Pick::Patrol;
            break;
        case KeyAction::Formation:
        case KeyAction::Scatter: {
            if (b.action == KeyAction::Formation) {
                m_units.formation(world(), m_figures, m_formation);
                m_formation = (m_formation + 1) % 3;
            } else {
                m_units.scatter(world());
            }
            // The group takes its places around where it stands.
            if (const auto c = m_units.centre(world())) {
                submit_orders(m_units.move_orders(world(), local(), c->first, c->second, false));
            }
            break;
        }
        case KeyAction::WhiteFlag: {
            std::vector<sim::Command> orders;
            const bool on = !m_units.any_white_flag(world());
            for (const ui::UnitRef& r : m_units.units()) {
                if (r.kind == ui::UnitRef::Kind::Ship) {
                    sim::ShipCommand o;
                    o.player = local();
                    o.ship = r.id;
                    o.op = sim::ShipCommand::Op::WhiteFlag;
                    o.on = on;
                    orders.emplace_back(o);
                }
            }
            submit_orders(std::move(orders), false);
            break;
        }
        case KeyAction::Explore:
            if (m_units.size() == 1 && m_units.kind() == ui::UnitRef::Kind::Ship) {
                m_commands.submit(sim::ExploreOrder{local(), m_units.units().front().id});
            }
            break;
        case KeyAction::Trade:
        case KeyAction::EditRoute:
            set_mode(Hud::kModeInfo);  // the ship's window has trade and route
            break;
        case KeyAction::Delete: {
            std::vector<sim::Command> orders;
            for (const ui::UnitRef& r : m_units.units()) {
                if (r.kind == ui::UnitRef::Kind::Ship) {
                    sim::ShipCommand o;
                    o.player = local();
                    o.ship = r.id;
                    o.op = sim::ShipCommand::Op::Scuttle;
                    orders.emplace_back(o);
                } else {
                    sim::SoldierCommand o;
                    o.player = local();
                    o.soldier = r.id;
                    o.op = sim::SoldierCommand::Op::Disband;
                    orders.emplace_back(o);
                }
            }
            submit_orders(std::move(orders), false);
            break;
        }
        case KeyAction::PixelScaleUp:
            m_host.scale_by(1, cx, cy);
            break;
        case KeyAction::PixelScaleDown:
            m_host.scale_by(-1, cx, cy);
            break;
        case KeyAction::ShowOwners:
            m_host.toggle_owners();
            break;
        case KeyAction::ShowFertility:
            m_host.toggle_fertility();
            break;
        case KeyAction::ShowFps:
            m_host.toggle_fps();
            break;
        case KeyAction::ShowCoordinates:
            m_host.toggle_coordinates();
            break;
        case KeyAction::ShowUnitArms:
            m_host.toggle_unit_arms();
            break;
        case KeyAction::Fullscreen:
            m_host.toggle_fullscreen();
            break;
        case KeyAction::Quit:
            m_host.quit();
            break;
    }
}

void MapInput::handle_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (m_hud && m_hud->typing()) {
                // A savegame's name is typed: the keys go there, not to the key table.
                if (event.key.key == SDLK_BACKSPACE) {
                    m_hud->type_key(8);
                } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                    m_hud->type_key(13);
                } else if (event.key.key == SDLK_ESCAPE) {
                    m_hud->type_key(27);
                } else if (event.key.key == SDLK_TAB) {
                    m_hud->type_key(9);
                } else if (event.key.key == SDLK_A && (event.key.mod & SDL_KMOD_CTRL) != 0) {
                    m_hud->type_key(1);  // select the whole text (Open1602's own fields)
                }
                break;
            }
            handle_key(static_cast<int>(event.key.key), static_cast<int>(event.key.scancode),
                       static_cast<std::uint16_t>(event.key.mod));
            break;
        case SDL_EVENT_TEXT_INPUT:
            if (m_hud && m_hud->typing() && event.text.text != nullptr) {
                m_hud->type_text(utf8_to_cp1252(event.text.text));
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            m_mouse_x = static_cast<double>(event.motion.x);
            m_mouse_y = static_cast<double>(event.motion.y);
            if (m_hud) {
                if (const auto a = m_hud->mouse(static_cast<std::int32_t>(event.motion.x),
                                                static_cast<std::int32_t>(event.motion.y), m_left_down)) {
                    m_host.apply_hud(*a);
                }
            }
            if (m_left_down || m_right_stroke) {
                if (m_left_down) {
                    m_press.move(static_cast<double>(event.motion.x), static_cast<double>(event.motion.y));
                }
                // A stroke of the editor's terrain tools goes on where the mouse is over the map.
                if (m_editor && !(m_hud && m_hud->covers(static_cast<std::int32_t>(event.motion.x),
                                                         static_cast<std::int32_t>(event.motion.y)))) {
                    if (const auto tile = tile_at_mouse(); tile.has_value()) {
                        (void)m_host.editor_stroke(tile, Host::Stroke::Continue, m_right_stroke);
                    }
                }
            }
            if (m_drag.move(static_cast<double>(event.motion.xrel), static_cast<double>(event.motion.yrel))) {
                m_camera.scroll(-static_cast<double>(event.motion.xrel), -static_cast<double>(event.motion.yrel));
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_left_down = true;
            }
            if (m_hud && m_hud->modal()) {
                // The question (or the key overview) takes every click; the window "Anzeige" also the
                // right button (the previous value).
                if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT) {
                    (void)m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                       static_cast<std::int32_t>(event.button.y), true, mouse_button(event.button));
                }
                break;
            }
            if (m_hud &&
                m_hud->covers(static_cast<std::int32_t>(event.button.x), static_cast<std::int32_t>(event.button.y))) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_sound.click();
                    if (const auto a = m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                                    static_cast<std::int32_t>(event.button.y), true)) {
                        m_host.apply_hud(*a);
                    }
                } else if (event.button.button == SDL_BUTTON_RIGHT && (m_hud->menu_active() || m_editor)) {
                    // The menus' settings and the editor's: the previous value.
                    (void)m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                       static_cast<std::int32_t>(event.button.y), true, ui::MouseButton::Right);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    // A right click on the minimap sends the selection there (ADR 0010), elsewhere on
                    // the panel it goes back to the player status; from a page that leads somewhere
                    // (a ship's route editor) a page back. In the build menu it first only
                    // deselects the category, the menu stays (as in the original).
                    if (const auto t = m_hud->minimap_tile(static_cast<std::int32_t>(event.button.x),
                                                           static_cast<std::int32_t>(event.button.y))) {
                        order_to_tile(t->first, t->second, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
                    } else if (m_hud->deselect_build_category()) {
                        m_tool.set_mode(BuildTool::Mode::Off);
                    } else if (!m_hud->step_back()) {
                        (void)back_to_status();
                    }
                }
                break;  // the map does not see clicks on the frame
            }
            if (event.button.button == SDL_BUTTON_LEFT && m_hud && m_hud->picking()) {
                // A window waits for a place on the map (a unit's destination).
                if (const auto tile = tile_at_mouse()) {
                    m_hud->map_click(static_cast<std::uint32_t>(tile->x), static_cast<std::uint32_t>(tile->y));
                }
                break;
            }
            if (event.button.button == SDL_BUTTON_RIGHT && m_editor && !m_left_down) {
                // The editor's right button: the shaping tools draw the other way (land and water
                // swapped); otherwise it drags the map, and a click puts back what is in hand.
                const auto tile = tile_at_mouse();
                m_right_stroke = tile && m_host.editor_stroke(tile, Host::Stroke::Begin, true);
                if (!m_right_stroke) {
                    m_drag.press();
                }
            } else if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                m_drag.press();
            } else if (m_editor) {
                // The editor's map: no selection, tool or order of the game (ADR 0013); the left
                // button picks what the editor's panel shows, or puts down what is being placed.
                if (event.button.button == SDL_BUTTON_LEFT) {
                    const auto tile = tile_at_mouse();
                    if (!tile || !m_host.editor_place(*tile)) {
                        if (!m_host.editor_stroke(tile, Host::Stroke::Begin, false)) {
                            editor_pick_at_mouse();
                        }
                    }
                }
            } else if (event.button.button == SDL_BUTTON_LEFT && m_tool.active()) {
                if (const auto spot = spot_at_mouse()) {
                    m_tool.press(*spot);
                }
            } else if (event.button.button == SDL_BUTTON_LEFT) {
                m_press.press(static_cast<double>(event.button.x), static_cast<double>(event.button.y));
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                m_left_down = false;
                if (m_editor) {
                    (void)m_host.editor_stroke(std::nullopt, Host::Stroke::End, false);
                }
                if (m_hud) {
                    if (const auto a = m_hud->mouse(static_cast<std::int32_t>(event.button.x),
                                                    static_cast<std::int32_t>(event.button.y), false)) {
                        m_host.apply_hud(*a);
                    }
                }
                if (m_press.at()) {
                    map_left_up(static_cast<double>(event.button.x), static_cast<double>(event.button.y),
                                static_cast<std::uint16_t>(SDL_GetModState()), event.button.clicks);
                    m_press.reset();
                }
            }
            if (event.button.button == SDL_BUTTON_RIGHT && m_hud) {
                // Ends a right click on a setting of the menus or the window "Anzeige".
                (void)m_hud->mouse(static_cast<std::int32_t>(event.button.x), static_cast<std::int32_t>(event.button.y),
                                   false, ui::MouseButton::Right);
            }
            if (event.button.button == SDL_BUTTON_RIGHT && m_right_stroke) {
                m_right_stroke = false;
                (void)m_host.editor_stroke(std::nullopt, Host::Stroke::End, true);
            } else if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                // A right click (not a drag): an order (ADR 0010), or back with the original's mouse;
                // in the editor it puts back what is being placed.
                const bool click = m_drag.release() && event.button.button == SDL_BUTTON_RIGHT;
                if (click && m_editor) {
                    (void)m_host.editor_cancel_place();
                } else if (click && !(m_hud && (m_hud->modal() || m_hud->menu_active()))) {
                    if (m_original_mouse) {
                        cancel_action();
                    } else if (m_tool.mode() == BuildTool::Mode::Copy && m_tool.layout()) {
                        // The copied piece goes away; the copy tool and the build menu stay.
                        m_tool.drop_layout();
                    } else if (m_tool.mode() == BuildTool::Mode::Move && m_tool.move_hand()) {
                        // The building in hand stays where it stands; the move tool stays.
                        m_tool.drop_move();
                    } else if (m_hud && m_hud->picking() && m_hud->step_back()) {
                        // Choosing a route stop on the map: the right click ends the choice, the
                        // ship does not sail off.
                    } else if (!m_units.empty() && cancel_build_step()) {
                        // A building in hand goes first, even with a ship selected (the Kontor to
                        // found from it); the ship stays selected and does not sail off.
                        while (cancel_build_step()) {
                        }
                    } else if (!m_units.empty() || !back_to_status()) {
                        map_order(static_cast<double>(event.button.x), static_cast<double>(event.button.y),
                                  (SDL_GetModState() & SDL_KMOD_SHIFT) != 0, (SDL_GetModState() & SDL_KMOD_CTRL) != 0);
                    }
                }
            } else if (event.button.button == SDL_BUTTON_LEFT && m_tool.dragging()) {
                if (const auto spot = spot_at_mouse(); spot && m_tool.copy_selecting()) {
                    // The copy tool's drag ends: the piece goes into the hand (nothing if it
                    // holds nothing of the player's own).
                    (void)m_tool.take_layout(world(), local(), *spot);
                } else if (spot && m_tool.move_selecting()) {
                    // The move tool picks up the building clicked (nothing if it may not move).
                    (void)m_tool.take_move(world(), m_buildings, local(), *spot);
                } else if (spot && m_tool.mode() == BuildTool::Mode::Move) {
                    // The building in hand goes where the mouse is; the tool waits for the next.
                    if (auto order = m_tool.move_order(local(), *spot)) {
                        m_commands.submit(std::move(*order));
                        m_tool.drop_move();
                    }
                } else if (spot && m_tool.blueprint() && m_tool.places()) {
                    // Blueprint mode: the buildings become blueprints, paid for when built.
                    if (auto order = m_tool.blueprint_order(world(), local(), *spot)) {
                        m_commands.submit(std::move(*order));
                    }
                } else if (spot) {
                    if (auto order = m_tool.build_order(world(), local(), *spot)) {
                        // A Kontor founded from a ship is the only one there: the tool is put away
                        // once it can be built (the ship's window gave it).
                        const bool founded =
                            order->ship && std::ranges::all_of(sim::plan_build(world(), m_buildings, *order).items,
                                                               [](const sim::PlannedBuild& i) { return i.ok(); });
                        m_commands.submit(std::move(*order));
                        if (founded) {
                            m_tool.set_mode(BuildTool::Mode::Off);
                            if (m_hud && m_hud->founding_ship()) {
                                set_mode(Hud::kModeInfo);  // back to the ship's window
                            }
                        }
                    } else if (auto demolish = m_tool.demolish_order(local(), *spot)) {
                        submit({*demolish}, false, m_confirm);  // asks before land is lost (S-035)
                    }
                }
                m_tool.release();
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: {  // U-001: the original has no wheel zoom
            const float y = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            if (m_hud && m_hud->goods_open()) {
                // The window "Warenbilanz" scrolls its table, the map stays.
                m_wheel_rest += y;
                const auto notches = static_cast<std::int32_t>(m_wheel_rest);
                m_wheel_rest -= static_cast<float>(notches);
                m_hud->goods_wheel(notches);
                break;
            }
            if (m_editor && (SDL_GetModState() & SDL_KMOD_CTRL) != 0) {
                // Ctrl and the wheel: the editor's brush bigger or smaller, a size a notch.
                m_wheel_rest += y;
                auto notches = static_cast<std::int32_t>(m_wheel_rest);
                m_wheel_rest -= static_cast<float>(notches);
                for (; notches > 0; --notches) {
                    m_host.editor_key(ui::KeyAction::EditorBrushBigger);
                }
                for (; notches < 0; ++notches) {
                    m_host.editor_key(ui::KeyAction::EditorBrushSmaller);
                }
                break;
            }
            if (m_hud && m_hud->covers(static_cast<std::int32_t>(event.wheel.mouse_x),
                                       static_cast<std::int32_t>(event.wheel.mouse_y))) {
                // Over the side panel a list scrolls (FB-6), whole notches as the table above.
                const float rest = m_wheel_rest + y;
                const auto notches = static_cast<std::int32_t>(rest);
                if (m_hud->panel_wheel(static_cast<std::int32_t>(event.wheel.mouse_x),
                                       static_cast<std::int32_t>(event.wheel.mouse_y), notches)) {
                    m_wheel_rest = rest - static_cast<float>(notches);
                    break;
                }
            }
            if (y != 0.0F) {
                // Touchpads send parts of a notch: the free zoom follows them finely.
                m_host.zoom_by(-static_cast<double>(y), static_cast<double>(event.wheel.mouse_x),
                               static_cast<double>(event.wheel.mouse_y));
            }
            break;
        }
        default:
            break;
    }
}

void MapInput::sync_text_input() {
    // SDL sends no text (SDL_EVENT_TEXT_INPUT) without it.
    const bool typing = m_hud && m_hud->typing();
    if (typing == m_text_input) {
        return;
    }
    m_text_input = typing;
    if (typing) {
        SDL_StartTextInput(m_window.sdl_window());
    } else {
        SDL_StopTextInput(m_window.sdl_window());
    }
}

void MapInput::follow_selection() {
    // A window changed the selection (filters, formations): the windows follow.
    if (m_units.take_changed()) {
        selection_changed(false);
    }
    // Units that died, sank or changed hands leave the selection.
    if (m_units.prune(world())) {
        selection_changed(false);
        if (m_units.empty()) {
            leave_military_mode();
        }
    }
}

void MapInput::leave_military_mode() {
    // Open1602: the units or ships deselected (or all of them fallen or sunk), the military mode
    // gives way to the player status; selecting units again goes back to it (selection_changed).
    if (m_hud && m_hud->mode() == Hud::kModeMilitary) {
        set_mode(Hud::kModeInfo);
    }
}

void MapInput::reset_game() {
    m_units.reset();
    m_pending_orders.clear();
    m_pick = Pick::None;
    m_press.reset();
    m_group_key.reset();
    m_formation = 0;
}

void MapInput::set_mode(std::int32_t mode) {
    if (m_hud) {
        if (const auto a = m_hud->set_mode(mode)) {
            m_host.apply_hud(*a);
        }
    }
}

std::optional<render::TilePos> MapInput::tile_at(double sx, double sy) const {
    const sim::World& w = world();
    const render::Zoom zoom = m_camera.view().zoom;
    return m_camera.ground_tile_at_screen(
        sx, sy, [&](render::TilePos t) { return render::tile_elevation(w, m_buildings, local(), t.x, t.y, zoom); });
}

std::optional<render::TilePos> MapInput::tile_at_mouse() const {
    if (m_hud && m_hud->modal()) {
        return std::nullopt;
    }
    return tile_at(m_mouse_x, m_mouse_y);
}

std::optional<ui::UnitRef> MapInput::unit_at_mouse() const {
    if (m_hud && m_hud->modal()) {
        return std::nullopt;
    }
    return unit_at(m_mouse_x, m_mouse_y);
}

std::optional<BuildTool::Spot> MapInput::spot_at_mouse() const {
    const auto tile = tile_at_mouse();
    if (!tile) {
        return std::nullopt;
    }
    const auto& cell = world().cell(tile->x, tile->y);
    if (cell.island == sim::World::kSea) {
        return std::nullopt;
    }
    return BuildTool::Spot{cell.island, cell.local_x, cell.local_y};
}

void MapInput::select_building_at_mouse(const Selection& before) {
    if (!m_hud) {
        return;
    }
    const sim::World& w = world();
    const assets::BuildingTable& buildings = m_buildings;
    Selection sel;
    // A building: its origin, so that every tile of it selects the same. Picked by its picture:
    // a click anywhere on it, also where it reaches over the tiles behind it.
    const auto hit = building_tile_at(m_mouse_x, m_mouse_y, [&](std::uint16_t island, const sim::SubTile& t) {
        return t.building != nullptr && w.islands()[island].map.contains(t.origin_x, t.origin_y) &&
               selectable(*t.building);
    });
    if (hit) {
        const sim::SubTile t = w.islands()[hit->island].map.sub_tile(buildings, hit->local_x, hit->local_y);
        sel.kind = Selection::Kind::Building;
        sel.island = hit->island;
        sel.x = static_cast<std::uint16_t>(t.origin_x);
        sel.y = static_cast<std::uint16_t>(t.origin_y);
    }
    m_hud->set_selection(clicked_selection(before, sel));
    if (const std::int32_t mode = mode_after_selecting(m_hud->mode(), sel.kind != Selection::Kind::None);
        mode != m_hud->mode()) {
        set_mode(mode);
    }
    if (sel.kind == Selection::Kind::Building) {
        // Looking at a farm, a plantation or a forester picks its field in the build menu:
        // the field whose grown stage gives the raw material.
        const sim::SubTile t = w.islands()[sel.island].map.sub_tile(buildings, sel.x, sel.y);
        if (t.building != nullptr && t.building->raw_ware != 0) {
            for (const assets::BuildingType& field : buildings.types()) {
                const assets::BuildingType* ripe =
                    field.prod_kind == sim::prod::kGrowing ? sim::ripe_record(buildings, field) : nullptr;
                if (ripe != nullptr && ripe->ware == t.building->raw_ware && m_hud->show_in_build_menu(field.id)) {
                    break;
                }
            }
        }
    }
}

void MapInput::plant_fields() {
    // The selected farm's field (the one its window picks in the build menu) on all
    // open ground in its catchment; with nothing selected or another building nothing happens.
    if (m_hud && m_hud->selection().kind == Selection::Kind::Building) {
        const Selection& sel = m_hud->selection();
        plant_fields_at(sel.island, sel.x, sel.y);
    }
}

void MapInput::plant_fields_at(std::uint16_t island, std::int32_t x, std::int32_t y) {
    const sim::World& w = world();
    if (island >= w.islands().size() || !w.islands()[island].map.contains(x, y)) {
        return;
    }
    const sim::SubTile t =
        w.islands()[island].map.sub_tile(m_buildings, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    if (t.building == nullptr || t.building->raw_ware == 0) {
        return;
    }
    for (const assets::BuildingType& field : m_buildings.types()) {
        const assets::BuildingType* ripe =
            field.prod_kind == sim::prod::kGrowing ? sim::ripe_record(m_buildings, field) : nullptr;
        if (ripe == nullptr || ripe->ware != t.building->raw_ware ||
            ui::build_menu().category_of(field.id) == nullptr) {
            continue;
        }
        if (auto order = sim::plant_fields_order(w, m_buildings, local(), island, {x, y}, field.id,
                                                 static_cast<std::uint32_t>(w.time_ms()));
            order && !order->items.empty()) {
            submit({std::move(*order)}, false, false);
        }
        return;
    }
}

void MapInput::toggle_gates() {
    const sim::Player* p = world().player(local());
    if (p == nullptr) {
        return;
    }
    const bool closed = !p->gates_closed;
    submit({sim::GateOrder{local(), closed}}, false, false);
    const char* key = closed ? "notice.gates_closed" : "notice.gates_opened";
    m_host.notify(ui::tr(key), key);
}

void MapInput::drop_units() {
    if (!m_units.empty()) {
        m_units.clear();
        selection_changed(false);
    }
}

bool MapInput::cancel_build_step() {
    if (m_hud && m_hud->close_build_popup()) {
        return true;
    }
    if (m_tool.mode() == BuildTool::Mode::Copy && m_tool.layout()) {
        m_tool.drop_layout();  // the piece in hand goes first, the copy tool stays
        return true;
    }
    if (m_tool.mode() == BuildTool::Mode::Move && m_tool.move_hand()) {
        m_tool.drop_move();  // the building in hand stays where it was, the move tool stays
        return true;
    }
    if (m_tool.active()) {
        m_tool.set_mode(BuildTool::Mode::Off);
        if (m_hud && m_hud->founding_ship()) {
            set_mode(Hud::kModeInfo);  // the Kontor from the ship: back to its window
        }
        return true;
    }
    return false;
}

bool MapInput::back_to_status() {
    if (!m_hud || m_hud->menu_active()) {
        return false;
    }
    // Everything at once, so one click is enough: the build menu's pop-up and the building in
    // hand, the pages of a window (tribute -> treaties, a ship's trade page) or of the settings,
    // another mode, the selection.
    bool changed = false;
    while (cancel_build_step()) {
        changed = true;
    }
    for (int page = 0; page < kMaxBackSteps && m_hud->back(); ++page) {
        changed = true;
    }
    if (m_hud->mode() != Hud::kModeInfo) {
        set_mode(Hud::kModeInfo);
        changed = true;
    }
    if (!m_units.empty()) {
        m_units.clear();
        selection_changed(false);
        changed = true;
    }
    if (m_hud->selection().kind != Selection::Kind::None) {
        m_hud->set_selection(Selection{});
        changed = true;
    }
    return changed;
}

std::pair<double, double> MapInput::screen_of(double tx, double ty) const {
    // Tile centres lie on a lattice: interpolate between three of them.
    const render::WorldView& view = m_camera.view();
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::int32_t x2 = 0;
    std::int32_t y2 = 0;
    render::world_tile_center(view, 0, 0, x0, y0);
    render::world_tile_center(view, 1, 0, x1, y1);
    render::world_tile_center(view, 0, 1, x2, y2);
    const double fx = tx - 0.5;
    const double fy = ty - 0.5;
    const double px = x0 + fx * (x1 - x0) + fy * (x2 - x0);
    const double py = y0 + fx * (y1 - y0) + fy * (y2 - y0);
    double sx = 0.0;
    double sy = 0.0;
    m_camera.world_to_screen(px, py, sx, sy);
    return {sx, sy};
}

std::optional<std::array<double, 4>> MapInput::unit_box(const ui::UnitRef& ref) const {
    const ZoomGraphics* graphics = m_host.graphics();
    if (const auto sprites = m_host.unit_sprites(ref); sprites && graphics) {
        // The pictures as drawn: world pixels * scale - origin.
        double ox = 0.0;
        double oy = 0.0;
        m_camera.screen_origin(ox, oy);
        const double k = m_camera.scale();
        std::optional<std::array<std::int32_t, 4>> u;
        for (std::uint8_t i = 0; i < sprites->count; ++i) {
            const auto b = render::command_bounds(sprites->parts[i], graphics->graphics.atlas);
            if (!b) {
                continue;
            }
            if (!u) {
                u = b;
                continue;
            }
            const std::int32_t x0 = std::min((*u)[0], (*b)[0]);
            const std::int32_t y0 = std::min((*u)[1], (*b)[1]);
            const std::int32_t x1 = std::max((*u)[0] + (*u)[2], (*b)[0] + (*b)[2]);
            const std::int32_t y1 = std::max((*u)[1] + (*u)[3], (*b)[1] + (*b)[3]);
            u = std::array<std::int32_t, 4>{x0, y0, x1 - x0, y1 - y0};
        }
        if (u) {
            return std::array<double, 4>{(*u)[0] * k - ox, (*u)[1] * k - oy, (*u)[2] * k, (*u)[3] * k};
        }
    }
    // No graphics yet: an estimate around the unit's position.
    const auto pos = ui::unit_position(world(), ref);
    if (!pos) {
        return std::nullopt;
    }
    const auto [sx, sy] = screen_of(pos->first, pos->second);
    const double tw = render::tile_width(m_camera.view().zoom) * m_camera.scale();
    if (ref.kind == ui::UnitRef::Kind::Ship) {
        const double w = 1.4 * tw;
        const double h = 1.5 * tw;
        return std::array<double, 4>{sx - w / 2, sy - h * 0.85, w, h};
    }
    const double w = 0.45 * tw;
    const double h = 0.75 * tw;
    return std::array<double, 4>{sx - w / 2, sy - h * 0.9, w, h};
}

std::vector<ui::UnitCandidate> MapInput::without_carts(std::vector<ui::UnitCandidate> units) const {
    // A trade cart does not go with ships or soldiers into "all of this kind on screen".
    std::erase_if(units, [&](const ui::UnitCandidate& c) {
        const sim::Ship* s = c.ref.kind == ui::UnitRef::Kind::Ship ? world().ship(c.ref.id) : nullptr;
        return s != nullptr && s->cart();
    });
    return units;
}

std::vector<ui::UnitCandidate> MapInput::units_in(double x0, double y0, double x1, double y1) const {
    const double left = std::min(x0, x1);
    const double right = std::max(x0, x1);
    const double top = std::min(y0, y1);
    const double bottom = std::max(y0, y1);
    std::vector<ui::UnitCandidate> out;
    // A cheap test of the unit's position first (FB-1): the screen point is affine in the tile
    // position, and no picture reaches further than a few tiles from it. Only the units near the
    // rectangle get their pictures built.
    // Plain pairs, not structured bindings: the static analyser loses those in the lambda.
    const std::pair<double, double> p0 = screen_of(0.0, 0.0);
    const std::pair<double, double> p1 = screen_of(1.0, 0.0);
    const std::pair<double, double> p2 = screen_of(0.0, 1.0);
    const double margin = kPickMarginTiles * render::tile_width(m_camera.view().zoom) * m_camera.scale();
    const auto near = [&](double tx, double ty) {
        const double px = p0.first + tx * (p1.first - p0.first) + ty * (p2.first - p0.first);
        const double py = p0.second + tx * (p1.second - p0.second) + ty * (p2.second - p0.second);
        return px >= left - margin && px <= right + margin && py >= top - margin && py <= bottom + margin;
    };
    const auto test = [&](const ui::UnitRef& ref) {
        const auto b = unit_box(ref);
        if (!b) {
            return;
        }
        const double w = std::min(right, (*b)[0] + (*b)[2]) - std::max(left, (*b)[0]);
        const double h = std::min(bottom, (*b)[1] + (*b)[3]) - std::max(top, (*b)[1]);
        if (w > 0.0 && h > 0.0) {
            out.push_back({ref, static_cast<std::int64_t>(w * h)});
        }
    };
    for (const sim::Ship& s : world().ships()) {
        if (!s.sinking &&
            near(s.pos_x / static_cast<double>(sim::kShipUnit), s.pos_y / static_cast<double>(sim::kShipUnit))) {
            test({ui::UnitRef::Kind::Ship, s.id});
        }
    }
    for (const sim::Soldier& s : world().soldiers()) {
        if (!s.away() && near(s.x(), s.y())) {
            test({ui::UnitRef::Kind::Soldier, s.id});
        }
    }
    return out;
}

std::pair<std::int32_t, std::int32_t> MapInput::world_pixel(double sx, double sy) const {
    double ox = 0.0;
    double oy = 0.0;
    m_camera.screen_origin(ox, oy);
    const double k = m_camera.scale();
    return {static_cast<std::int32_t>(std::floor((sx + ox) / k)), static_cast<std::int32_t>(std::floor((sy + oy) / k))};
}

std::optional<render::TileHit> MapInput::building_tile_at(double sx, double sy, const BuildingFilter& wanted) const {
    const sim::World& w = world();
    const auto accepts = [&](std::uint16_t island, std::uint16_t x, std::uint16_t y) {
        // An island the player has not discovered shows only its ground: nothing there to click.
        if (island == sim::World::kSea || (local() != sim::kNoPlayer && !w.islands()[island].discovered_by(local()))) {
            return false;
        }
        return wanted(island, w.islands()[island].map.sub_tile(m_buildings, x, y));
    };
    // Pixel exact, as the units: the pictures under the point, the front-most first.
    if (m_host.graphics() != nullptr) {
        const auto [px, py] = world_pixel(sx, sy);
        m_host.tiles_at_pixel(px, py, m_tile_hits);
        for (const render::TileHit& hit : m_tile_hits) {
            if (accepts(hit.island, hit.local_x, hit.local_y)) {
                return hit;
            }
        }
    }
    const auto tile = tile_at(sx, sy);
    if (!tile || tile->x >= w.width() || tile->y >= w.height()) {
        return std::nullopt;
    }
    const sim::World::Cell& cell = w.cell(tile->x, tile->y);
    if (!accepts(cell.island, cell.local_x, cell.local_y)) {
        return std::nullopt;
    }
    return render::TileHit{tile->x, tile->y, cell.island, cell.local_x, cell.local_y, kBehindEverything, false};
}

std::optional<ui::UnitRef> MapInput::unit_at(double sx, double sy) const {
    // Pixel exact: the unit whose visible pixel is under the mouse,
    // the one drawn last if several overlap - unless a map tile is drawn over it there, a building
    // in front of it: the front-most picture takes the click.
    const ZoomGraphics* graphics = m_host.graphics();
    if (graphics) {
        const auto [wx, wy] = world_pixel(sx, sy);
        std::optional<ui::UnitRef> best;
        std::tuple<std::int32_t, std::int32_t, int> best_order{};
        for (const ui::UnitCandidate& c : units_in(sx, sy, sx + 1.0, sy + 1.0)) {
            const auto sprites = m_host.unit_sprites(c.ref);
            if (!sprites) {
                continue;
            }
            bool hit = false;
            for (std::uint8_t i = 0; i < sprites->count && !hit; ++i) {
                hit = render::command_hit(sprites->parts[i], graphics->graphics.atlas, graphics->hit_mask, wx, wy);
            }
            // Drawn by row and depth; land units after ships on the same place.
            const std::tuple<std::int32_t, std::int32_t, int> order{sprites->row, sprites->depth,
                                                                    c.ref.kind == ui::UnitRef::Kind::Soldier ? 1 : 0};
            if (hit && (!best || order > best_order)) {
                best = c.ref;
                best_order = order;
            }
        }
        if (best) {
            // Water lies flat under the ships and never hides one.
            m_host.tiles_at_pixel(wx, wy, m_tile_hits);
            const auto front = std::find_if(m_tile_hits.begin(), m_tile_hits.end(), [&](const render::TileHit& h) {
                if (h.island == sim::World::kSea) {
                    return false;
                }
                const sim::MapCell& top = world().islands()[h.island].map.top(h.local_x, h.local_y);
                const assets::BuildingType* b = m_buildings.by_sprite(top.sprite);
                return b != nullptr && b->kind != sim::building_kind::kSea && b->kind != sim::building_kind::kSurf &&
                       b->kind != sim::building_kind::kSurfCorner;
            });
            if (front == m_tile_hits.end() || !front->covers_figure(std::get<0>(best_order))) {
                return best;
            }
            return std::nullopt;  // hidden there: what is drawn in front is meant
        }
    }
    // Small figures (zoomed out) are hard to hit: then the nearest whose picture box is within a
    // few pixels, land units and the player's own first.
    const double slack = 3.0 * m_camera.scale();
    std::optional<ui::UnitRef> best;
    double best_d = 0.0;
    bool best_own = false;
    for (const ui::UnitCandidate& c : units_in(sx - slack, sy - slack, sx + slack, sy + slack)) {
        const auto b = unit_box(c.ref);
        if (!b || (*b)[2] * (*b)[3] > 400.0 * m_camera.scale() * m_camera.scale()) {
            continue;  // only small pictures; a large one is hit by its pixels
        }
        const double dx = (*b)[0] + (*b)[2] / 2 - sx;
        const double dy = (*b)[1] + (*b)[3] / 2 - sy;
        const double d = dx * dx + dy * dy;
        const bool own = ui::unit_owner(world(), c.ref) == local();
        const double rank = d - (c.ref.kind == ui::UnitRef::Kind::Soldier ? 1e6 : 0.0);
        if (!best || (own && !best_own) || (own == best_own && rank < best_d)) {
            best = c.ref;
            best_d = rank;
            best_own = own;
        }
    }
    return best;
}

void MapInput::editor_pick_at_mouse() {
    const sim::World& w = world();
    EditorPick pick;
    if (const auto unit = unit_at(m_mouse_x, m_mouse_y)) {
        pick.kind = unit->kind == ui::UnitRef::Kind::Ship ? EditorPick::Kind::Ship : EditorPick::Kind::Soldier;
        pick.id = unit->id;
    } else if (const auto hit =
                   building_tile_at(m_mouse_x, m_mouse_y, [&](std::uint16_t island, const sim::SubTile& t) {
                       // What was built, not the ground, a wild tree or a rock: a click there picks
                       // the island. Fields and planted trees are a city's.
                       if (t.building == nullptr || !sim::destroyable(*t.building, true) ||
                           !w.islands()[island].map.contains(t.origin_x, t.origin_y)) {
                           return false;
                       }
                       return t.building->kind != sim::building_kind::kForest ||
                              w.islands()[island]
                                      .map
                                      .top(static_cast<std::uint32_t>(t.origin_x),
                                           static_cast<std::uint32_t>(t.origin_y))
                                      .city != sim::MapCell::kNoCity;
                   })) {
        const sim::SubTile t = w.islands()[hit->island].map.sub_tile(m_buildings, hit->local_x, hit->local_y);
        pick.kind = EditorPick::Kind::Building;
        pick.island = hit->island;
        pick.x = static_cast<std::uint32_t>(t.origin_x);
        pick.y = static_cast<std::uint32_t>(t.origin_y);
    } else if (const auto tile = tile_at_mouse()) {
        const sim::World::Cell& cell = w.cell(tile->x, tile->y);
        pick.kind = cell.island == sim::World::kSea ? EditorPick::Kind::Sea : EditorPick::Kind::Island;
        pick.island = cell.island == sim::World::kSea ? 0 : cell.island;
        pick.x = tile->x;
        pick.y = tile->y;
    } else {
        return;
    }
    m_host.editor_pick(pick);
}

void MapInput::map_left_up(double x, double y, std::uint16_t mod, int clicks) {
    const bool shift = (mod & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    if (m_pick != Pick::None) {
        map_order(x, y, shift, ctrl);
        return;
    }
    if (m_press.rubber_band() && m_press.at()) {
        m_units.box(world(), units_in(m_press.at()->first, m_press.at()->second, x, y), local(), shift);
        selection_changed(true);
        return;
    }
    if (const auto unit = unit_at(x, y)) {
        if (ctrl || clicks >= 2) {
            // All of this kind on screen.
            const ui::Rect v = m_hud ? m_hud->game_view() : ui::Rect{0, 0, 1 << 15, 1 << 15};
            m_units.select_alike(world(), without_carts(units_in(v.x, v.y, v.x + v.w, v.y + v.h)), *unit);
        } else if (shift) {
            m_units.toggle(world(), *unit);
        } else {
            m_units.select(world(), *unit);
        }
        selection_changed(false);
        return;
    }
    // Ctrl + left click is a command on the object in both mappings, as in the original
    // ("command on this object"): wounded soldiers onto their own doctor to be healed.
    if ((m_original_mouse || ctrl) && !m_units.empty() && m_units.owner(world()) == local()) {
        map_order(x, y, shift, ctrl);
        return;
    }
    // What was selected before the click: the same building clicked again shows more (a Kontor
    // its stock). Taken before the units are dropped, which clears the selection.
    const Selection before = m_hud ? m_hud->selection() : Selection{};
    if (!m_units.empty()) {
        m_units.clear();
        selection_changed(false);
        leave_military_mode();
    }
    if (open_blueprint_at_mouse()) {
        return;
    }
    select_building_at_mouse(before);
}

void MapInput::toggle_blueprint() {
    m_tool.set_blueprint(!m_tool.blueprint());
    if (m_hud) {
        m_hud->set_blueprint_in_build_menu(m_tool.blueprint());
        if (m_tool.blueprint() && m_hud->mode() != Hud::kModeBuild) {
            set_mode(Hud::kModeBuild);
        }
    }
}

void MapInput::build_blueprints() {
    const auto spot = spot_at_mouse();
    if (!spot) {
        return;
    }
    const sim::IslandMap& map = world().islands()[spot->island].map;
    if (!map.contains(spot->x, spot->y)) {
        return;
    }
    sim::BlueprintOrder order;
    order.player = local();
    order.kind = sim::BlueprintOrder::Kind::Build;
    order.island = spot->island;
    order.city = map.top(static_cast<std::uint32_t>(spot->x), static_cast<std::uint32_t>(spot->y)).city;
    if (order.city == sim::MapCell::kNoCity) {
        // Beside the city's land: the city of the player's nearest blueprint on the island.
        std::int64_t best = std::numeric_limits<std::int64_t>::max();
        for (const sim::Blueprint& b : world().blueprints()) {
            if (b.player != local() || b.island != spot->island) {
                continue;
            }
            const std::int64_t dx = b.x - spot->x;
            const std::int64_t dy = b.y - spot->y;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                order.city = sim::blueprint_city(world(), b);
            }
        }
    }
    if (order.city != sim::MapCell::kNoCity) {
        submit({std::move(order)}, true, false);
    }
}

bool MapInput::open_blueprint_at_mouse() {
    const auto spot = spot_at_mouse();
    if (!m_hud || !spot) {
        return false;
    }
    const auto index = sim::blueprint_at(world(), m_buildings, local(), spot->island, {spot->x, spot->y});
    if (!index) {
        return false;
    }
    const sim::Blueprint& b = world().blueprints()[*index];
    m_hud->open_blueprint(b.island, b.x, b.y);
    return true;
}

void MapInput::map_order(double x, double y, bool queue, bool on_object) {
    const Pick pick = std::exchange(m_pick, Pick::None);
    if (m_units.empty() || m_units.owner(world()) != local()) {
        return;
    }
    const auto tile = tile_at(x, y);
    if (pick == Pick::Patrol) {
        if (!tile) {
            return;
        }
        // Back and forth between where the units are and the click.
        std::vector<sim::Command> orders;
        for (const ui::UnitRef& r : m_units.units()) {
            if (r.kind == ui::UnitRef::Kind::Ship) {
                sim::ShipCommand o;
                o.player = local();
                o.ship = r.id;
                o.op = sim::ShipCommand::Op::Patrol;
                o.x = static_cast<std::uint16_t>(tile->x);
                o.y = static_cast<std::uint16_t>(tile->y);
                orders.emplace_back(o);
            } else if (const sim::Soldier* s = world().soldier(r.id)) {
                sim::SoldierCommand o;
                o.player = local();
                o.soldier = r.id;
                o.op = sim::SoldierCommand::Op::SetWaypoint;
                o.index = 0;
                o.point =
                    sim::Target::spot(static_cast<std::uint32_t>(s->cell_x()), static_cast<std::uint32_t>(s->cell_y()));
                orders.emplace_back(o);
                o.index = 1;
                o.point = sim::Target::tile_spot(tile->x, tile->y);
                orders.emplace_back(o);
                o.op = sim::SoldierCommand::Op::Patrol;
                o.on = true;
                orders.emplace_back(o);
            }
        }
        submit_orders(std::move(orders));
        return;
    }
    const bool attack_pick = pick == Pick::Attack;
    // Ctrl (or F first) also takes a tree or field as a target, to shoot it down, and another
    // player's road, bridge or pier; a plain click there is a move.
    const bool fell = attack_pick || on_object;
    const auto is_tree = [](const sim::SubTile& t) {
        return t.building != nullptr && t.building->kind == sim::building_kind::kForest;
    };
    const auto others = [&](std::uint16_t island, const sim::SubTile& t) {
        const sim::PlayerId owner =
            world()
                .islands()[island]
                .map.top(static_cast<std::uint32_t>(t.origin_x), static_cast<std::uint32_t>(t.origin_y))
                .owner;
        return owner != local() && owner != sim::kNoPlayer;
    };
    // Only with Ctrl or F: a tree or field (anybody's but one's own, which units only walk to), or
    // another player's road, bridge or pier.
    const auto ctrl_target = [&](std::uint16_t island, const sim::SubTile& t) {
        return fell && sim::destroyable(*t.building, true) && (is_tree(t) || others(island, t));
    };
    // Onto a unit: attack somebody else's, follow or board one's own.
    if (const auto unit = unit_at(x, y); unit && !m_units.contains(*unit)) {
        const sim::Target t =
            unit->kind == ui::UnitRef::Kind::Ship ? sim::Target::other_ship(unit->id) : sim::Target::soldier(unit->id);
        const bool foreign = ui::unit_owner(world(), *unit) != local();
        submit_orders(m_units.target_orders(world(), local(), t, queue, foreign || attack_pick));
        return;
    }
    if (!tile) {
        return;
    }
    // Onto a building, picked by its picture as a click selects it.
    if (m_units.kind() == ui::UnitRef::Kind::Ship) {
        // A ship to a Kontor: its city.
        const sim::City* city = nullptr;
        const auto kontor = building_tile_at(x, y, [&](std::uint16_t island, const sim::SubTile& t) {
            if (t.building == nullptr) {
                return false;
            }
            for (const sim::City& c : world().cities()) {
                if (c.island == island && c.warehouse_x && c.warehouse_y) {
                    const auto b = sim::building_at(
                        world(), m_buildings, c.island,
                        {static_cast<std::int32_t>(*c.warehouse_x), static_cast<std::int32_t>(*c.warehouse_y)});
                    if (b && b->origin.x == t.origin_x && b->origin.y == t.origin_y) {
                        city = &c;
                        return true;
                    }
                }
            }
            return false;
        });
        if (kontor && city != nullptr) {
            // Somebody else's Kontor: an attack on the building unless it is there to trade at
            // (asks first when it means war, as for soldiers).
            if (ship_attacks_kontor(world(), local(), city->owner, attack_pick)) {
                const sim::SubTile b =
                    world().islands()[kontor->island].map.sub_tile(m_buildings, kontor->local_x, kontor->local_y);
                const sim::Target t = sim::Target::building(kontor->island, static_cast<std::uint32_t>(b.origin_x),
                                                            static_cast<std::uint32_t>(b.origin_y));
                submit_orders(m_units.target_orders(world(), local(), t, queue, true));
                return;
            }
            const sim::Target t = sim::Target::city_of(city->island, city->index);
            submit_orders(m_units.target_orders(world(), local(), t, queue, false));
            return;
        }
        // Another player's building is attacked, as in the original's military mode;
        // terrain never.
        if (const auto hit = building_tile_at(x, y, [&](std::uint16_t island, const sim::SubTile& t) {
                if (t.building == nullptr) {
                    return false;
                }
                if (is_tree(t)) {
                    return fell;
                }
                return (sim::destroyable(*t.building, false) && others(island, t)) || ctrl_target(island, t);
            })) {
            const sim::SubTile b = world().islands()[hit->island].map.sub_tile(m_buildings, hit->local_x, hit->local_y);
            const sim::Target t = sim::Target::building(hit->island, static_cast<std::uint32_t>(b.origin_x),
                                                        static_cast<std::uint32_t>(b.origin_y));
            submit_orders(m_units.target_orders(world(), local(), t, queue, true));
            return;
        }
    } else if (const auto hit = building_tile_at(x, y, [&](std::uint16_t island, const sim::SubTile& t) {
                   // Not terrain such as a beach or rock: a click there is a move.
                   return t.building != nullptr && ((!is_tree(t) && sim::destroyable(*t.building, false)) ||
                                                    ctrl_target(island, t) || (fell && is_tree(t)));
               })) {
        // Land units onto a building: attack another's, walk to (or heal at) one's own.
        const sim::SubTile b = world().islands()[hit->island].map.sub_tile(m_buildings, hit->local_x, hit->local_y);
        const sim::Target t = sim::Target::building(hit->island, static_cast<std::uint32_t>(b.origin_x),
                                                    static_cast<std::uint32_t>(b.origin_y));
        const bool foreign = target_owner(world(), t) != local() && target_owner(world(), t) != sim::kNoPlayer;
        submit_orders(m_units.target_orders(world(), local(), t, queue, foreign || fell));
        return;
    }
    // With F first: an attack move (fights on the way, ADR 0010).
    submit_orders(m_units.move_orders(world(), local(), tile->x + 0.5, tile->y + 0.5, queue, attack_pick));
}

void MapInput::order_to_tile(double tx, double ty, bool queue) {
    if (m_units.empty() || m_units.owner(world()) != local()) {
        return;
    }
    submit_orders(m_units.move_orders(world(), local(), tx, ty, queue));
}

void MapInput::cancel_action() {
    if (m_pick != Pick::None) {
        m_pick = Pick::None;
        return;
    }
    if (m_hud && m_hud->back()) {
        return;  // a window went back a page or stopped picking
    }
    if (m_hud && m_hud->mode() == Hud::kModeBuild) {
        set_mode(Hud::kModeInfo);
        return;
    }
    if (m_tool.active()) {
        m_tool.set_mode(BuildTool::Mode::Off);
        return;
    }
    if (!m_units.empty()) {
        m_units.clear();
        selection_changed(false);
        leave_military_mode();
        return;
    }
    if (m_hud && m_hud->selection().kind != Selection::Kind::None) {
        m_hud->set_selection(Selection{});
        return;
    }
    if (m_hud && m_hud->mode() == Hud::kModeMilitary) {
        set_mode(Hud::kModeInfo);
    }
}

void MapInput::selection_changed(bool box) {
    if (!m_hud) {
        return;
    }
    Selection sel;
    if (m_units.size() == 1 && m_units.kind() == ui::UnitRef::Kind::Ship) {
        sel.kind = Selection::Kind::Ship;
        sel.ship = m_units.units().front().id;
    }
    m_hud->set_selection(sel);
    // In the info mode a group (by rectangle) or a land unit goes to the military mode.
    if (!m_units.empty() && m_hud->mode() != Hud::kModeMilitary && m_hud->mode() != Hud::kModeBuild &&
        ((box && m_units.size() >= 2) || m_units.kind() == ui::UnitRef::Kind::Soldier)) {
        set_mode(Hud::kModeMilitary);
    }
    // A ship selected from the settings: its window in the info mode.
    if (const std::int32_t mode = mode_after_selecting(m_hud->mode(), !m_units.empty()); mode != m_hud->mode()) {
        set_mode(mode);
    }
}

void MapInput::submit_orders(std::vector<sim::Command> orders, bool sound) {
    submit(std::move(orders), sound, m_confirm);
}

void MapInput::answer(bool yes) {
    if (yes) {
        submit(std::move(m_pending_orders), true, false);
    }
    m_pending_orders.clear();
}

void MapInput::submit(std::vector<sim::Command> orders, bool sound, bool ask) {
    if (orders.empty()) {
        return;
    }
    if (ask && m_hud) {
        for (const sim::Command& c : orders) {
            if (const auto q = order_question(world(), m_buildings, m_figures, local(), c)) {
                m_pending_orders = std::move(orders);
                m_hud->ask(*q);
                return;
            }
        }
    }
    bool attack = false;
    for (const sim::Command& c : orders) {
        if (const auto* o = std::get_if<sim::ShipOrder>(&c)) {
            attack = attack || o->attack;
        } else if (const auto* so = std::get_if<sim::SoldierOrder>(&c)) {
            attack = attack || so->attack;
        }
    }
    for (sim::Command& c : orders) {
        m_commands.submit(std::move(c));
    }
    // One acknowledgement of the first unit, as in the original: ships and soldiers answer, a market cart
    // does not.
    if (sound && !m_units.empty()) {
        const ui::UnitRef first = m_units.units().front();
        const sim::Ship* cart = first.kind == ui::UnitRef::Kind::Ship ? world().ship(first.id) : nullptr;
        if (cart != nullptr && cart->cart()) {
            return;
        }
        if (const auto pos = ui::unit_position(world(), first)) {
            m_sound.order(m_units.kind() == ui::UnitRef::Kind::Ship, attack, pos->first, pos->second);
        }
    }
}

void MapInput::draw_overlay() {
    SDL_Renderer* r = m_window.sdl_renderer();
    const ZoomGraphics* graphics = m_host.graphics();
    const double k = m_camera.scale();
    double ox = 0.0;
    double oy = 0.0;
    m_camera.screen_origin(ox, oy);
    const std::int32_t tw = render::tile_width(m_camera.view().zoom);
    const std::optional<std::uint32_t> numbers =
        graphics ? graphics->graphics.sheet_base(assets::FigureSheet::Numbers) : std::nullopt;
    std::vector<render::DrawCommand> digits;
    for (const ui::UnitRef& ref : m_units.units()) {
        const auto b = unit_box(ref);
        const auto health = ui::unit_health(world(), m_figures, ref);
        if (!b || !health) {
            continue;
        }
        // In world pixels: a green and red bar right above the picture
        // (ship: flag or hull), centred on the unit; its group's number (NUMBERS.BSH, sprite
        // group + 1) 2 pixels right of the bar, half the bar's height higher.
        const bool ship = ref.kind == ui::UnitRef::Kind::Ship;
        const std::int32_t w = (ship ? 76 : 51) * tw / 128;
        const std::int32_t h = std::max(1, (ship ? 15 : 10) * tw / 128);
        double centre = (*b)[0] + (*b)[2] / 2;
        if (const auto sprites = m_host.unit_sprites(ref); sprites && graphics) {
            if (const auto body = render::command_bounds(sprites->parts[0], graphics->graphics.atlas)) {
                centre = ((*body)[0] + (*body)[2] / 2.0) * k - ox;
            }
        }
        const auto left = static_cast<std::int32_t>(std::floor((centre + ox) / k)) - w / 2;
        const auto top = static_cast<std::int32_t>(std::floor(((*b)[1] + oy) / k)) - h;
        const auto filled = static_cast<std::int32_t>(w * *health);
        const auto rect = [&](std::int32_t x, std::int32_t width) {
            return SDL_FRect{static_cast<float>(x * k - ox), static_cast<float>(top * k - oy),
                             static_cast<float>(width * k), static_cast<float>(h * k)};
        };
        const SDL_FRect green = rect(left, filled);
        const SDL_FRect red = rect(left + filled, w - filled);
        SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
        SDL_RenderFillRect(r, &green);
        SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
        SDL_RenderFillRect(r, &red);
        if (const auto g = m_units.group_of(ref)) {
            const auto sprite = static_cast<std::uint32_t>((*g + 1) % 10);
            if (numbers && graphics && !graphics->graphics.atlas.rect(*numbers + sprite).empty()) {
                render::DrawCommand c;
                c.sprite = *numbers + sprite;
                c.x = left + w + 2;
                c.y = top - h / 2 + graphics->graphics.atlas.rect(c.sprite).height;  // left / bottom anchor
                digits.push_back(c);
            } else {
                const char digit[2] = {static_cast<char>('0' + sprite), 0};
                SDL_SetRenderDrawColor(r, 0, 255, 255, 255);
                SDL_RenderDebugText(r, static_cast<float>((left + w + 2) * k - ox),
                                    static_cast<float>((top - h / 2) * k - oy), digit);
            }
        }
    }
    if (graphics != nullptr && !digits.empty()) {
        graphics->renderer->draw(digits, ox, oy, k);
    }
    // The selection rectangle: a thin white outline.
    if (const auto& at = m_press.at(); m_press.rubber_band() && at) {
        const SDL_FRect band{
            static_cast<float>(std::min(at->first, m_mouse_x)), static_cast<float>(std::min(at->second, m_mouse_y)),
            static_cast<float>(std::abs(m_mouse_x - at->first)), static_cast<float>(std::abs(m_mouse_y - at->second))};
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderRect(r, &band);
    }
}

}  // namespace o1602::app
