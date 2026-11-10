// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which key does what (ADR 0010): a table of actions and keys, freely assignable. A key may mean
// different things where the contexts never apply together (building, units selected); the general
// context applies everywhere else. Key codes are those of the platform layer (SDL keycodes and
// scancodes); this table only compares them.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace o1602::ui {

enum class KeyAction : std::uint16_t {
    // Camera and game
    ScrollUp,
    ScrollDown,
    ScrollLeft,
    ScrollRight,
    RotateViewLeft,
    RotateViewRight,
    ZoomIn,
    ZoomOut,
    JumpToMessage,
    CentreOnSelection,
    NextCity,  ///< the camera to the player's next city (History Edition)
    Pause,
    PauseVideo,  ///< the original's pause: the game stops, the pause video loops (U-018)
    SpeedUp,
    SpeedDown,
    QuickSave,
    QuickLoad,
    GameMenu,
    Screenshot,
    Help,
    Cancel,  ///< back a page, out of a tool, deselect
    // Windows
    ModeBuild,
    ModeMilitary,
    ModeInfo,
    ModeOptions,
    ShipList,
    CityList,
    Diplomacy,
    GoodsStatistics,  ///< the window "Warenbilanz" (Open1602)
    Chat,             ///< the chat of a multiplayer game (Open1602)
    // Building
    RotateBuilding,
    RotateBuildingBack,  ///< the other way round (History Edition)
    PickBuilding,
    DemolishTool,
    NextBuilding,  ///< Open1602's list of all buildings (tests and modding)
    PlantFields,   ///< the selected farm's field on all open ground in its catchment (History Edition text)
    ToggleGates,   ///< closes or opens all own city gates to foreign soldiers (Open1602)
    // Selection
    GroupStore,   ///< `param` 0..9
    GroupAdd,     ///< `param` 0..9
    GroupRecall,  ///< `param` 0..9
    NextIdleShip,
    NextIdleUnit,
    NextKindInSelection,
    SelectInfantry,  ///< all own units of a kind on screen (History Edition)
    SelectMusketeers,
    SelectCavalry,
    SelectCannoneers,
    // Orders
    AttackMove,
    Stop,
    HoldPosition,
    Patrol,
    Formation,
    Scatter,
    WhiteFlag,
    Explore,
    Trade,
    EditRoute,
    Delete,  ///< disband, scuttle (with a question)
    // Viewer
    PixelScaleUp,
    PixelScaleDown,
    ShowOwners,
    ShowFertility,    ///< whether the selected farm's crop bears on each tile (Open1602)
    ShowFps,          ///< the frame rate in the corner of the map (History Edition)
    ShowCoordinates,  ///< the tile under the mouse in the corner of the map (History Edition)
    ShowUnitArms,     ///< the owner's coat of arms over the own land units (History Edition)
    Fullscreen,       ///< window or full screen
    Quit,
};

enum class KeyContext : std::uint8_t { General, Building, Units };

/// A key as it was pressed: what it means in the keyboard layout (`key`, a keycode) and where it
/// sits on the keyboard (`scancode`).
struct KeyPress {
    std::int32_t key = 0;
    std::int32_t scancode = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

/// A key with its modifiers. `physical`: `key` is a scancode, the key at that place on every
/// layout (W A S D and the digits stay where they are on AZERTY); otherwise a keycode, the key
/// that means this letter (B for building wherever B is).
struct KeyChord {
    std::int32_t key = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool physical = false;
    friend bool operator==(const KeyChord&, const KeyChord&) = default;

    [[nodiscard]] bool matches(const KeyPress& press) const noexcept {
        return (physical ? press.scancode : press.key) == key && press.ctrl == ctrl && press.shift == shift &&
               press.alt == alt;
    }
};

struct KeyBinding {
    KeyContext context = KeyContext::General;
    KeyChord chord;
    KeyAction action = KeyAction::Cancel;
    std::uint8_t param = 0;
};

class KeyBindings {
public:
    /// Adds a binding; one that had the same chord in the same context is replaced.
    void bind(KeyContext context, KeyChord chord, KeyAction action, std::uint8_t param = 0);
    /// Removes every key of an action (to assign it anew).
    void unbind(KeyAction action, std::uint8_t param = 0);
    /// What a key does in `context` (its own bindings first, then the general ones).
    [[nodiscard]] std::optional<KeyBinding> find(KeyContext context, const KeyChord& chord) const;
    /// What a pressed key does in `context`: its own bindings first, then the general ones; on
    /// each level a key bound by its place comes before one bound by its meaning.
    [[nodiscard]] std::optional<KeyBinding> find_pressed(KeyContext context, const KeyPress& press) const;
    /// The keys of an action, for holding keys down (scrolling) and for the settings.
    [[nodiscard]] std::vector<KeyChord> keys(KeyAction action, std::uint8_t param = 0) const;
    [[nodiscard]] const std::vector<KeyBinding>& all() const noexcept { return m_bindings; }

    /// Name of an action in settings files ("scroll_up", "group_recall").
    [[nodiscard]] static std::string_view name(KeyAction action) noexcept;
    [[nodiscard]] static std::optional<KeyAction> action_named(std::string_view name) noexcept;
    [[nodiscard]] static std::string_view context_name(KeyContext context) noexcept;
    [[nodiscard]] static std::optional<KeyContext> context_named(std::string_view name) noexcept;

    /// Names of keys come from the platform (SDL_GetKeyName / SDL_GetKeyFromName, for a physical
    /// key SDL_GetScancodeName / SDL_GetScancodeFromName).
    using KeyName = std::function<std::string(std::int32_t key, bool physical)>;
    using KeyFromName = std::function<std::optional<std::int32_t>(std::string_view name, bool physical)>;
    /// "Ctrl+Shift+F" and back; modifiers first, the rest is the key's name. A physical key's name
    /// stands in brackets: "Ctrl+[1]".
    [[nodiscard]] static std::string chord_text(const KeyChord& chord, const KeyName& key_name);
    [[nodiscard]] static std::optional<KeyChord> parse_chord(std::string_view text, const KeyFromName& key_from_name);

    /// The settings file (keys.cfg, UTF-8 like the key names): one line per key, "context action
    /// [number] = keys". An action that appears there has only the keys of its lines in that
    /// context ("= none": no key); the others keep their defaults, so a file of an older version
    /// misses nothing new.
    /// `as_comments`: every line behind "#", the defaults to look up and change one by one.
    [[nodiscard]] std::string write_text(const KeyName& key_name, bool as_comments = false) const;
    /// Applies a settings file; returns what it could not read (line by line), the rest counts.
    std::vector<std::string> read_text(std::string_view text, const KeyFromName& key_from_name);

private:
    std::vector<KeyBinding> m_bindings;
};

}  // namespace o1602::ui
