// SPDX-License-Identifier: GPL-3.0-or-later
//
// Typing the name of an own ship or city in its window (docs/re/ui.md#name-input). The names live in
// the world only (sim::Ship::name, sim::City::name): every window, list and message reads them from
// there, so a new name shows everywhere at once. The typed name becomes a command (ShipCommand or
// CityOrder Rename) that every computer of the game carries out at the same tick and the savegame
// keeps. Needs no platform, so that the tests have it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// What a name belongs to: a ship or a city.
struct NameTarget {
    enum class Kind : std::uint8_t { Ship, City };
    Kind kind = Kind::Ship;
    std::uint32_t ship = 0;    ///< Ship: its id
    std::uint16_t island = 0;  ///< City: its island and number there
    std::uint8_t city = 0;

    [[nodiscard]] static NameTarget of(const sim::Ship& ship) noexcept;
    [[nodiscard]] static NameTarget of(const sim::City& city) noexcept;
    bool operator==(const NameTarget&) const = default;
};

/// The name of `target` as the world has it (Windows-1252), nullptr if the ship or city is gone.
[[nodiscard]] const std::string* name_of(const sim::World& world, const NameTarget& target);

/// Whether `player` may rename `target` now: an own city, an own ship that is not sinking (the
/// original takes clicks on the name only then, ShipPanel_Refresh 0x00436170).
[[nodiscard]] bool can_rename(const sim::World& world, sim::PlayerId player, const NameTarget& target);

/// The command that gives `target` the name `name`.
[[nodiscard]] sim::Command rename_command(sim::PlayerId player, const NameTarget& target, std::string name);

/// The name being typed (the original's text input 0x00409910 / 0x00409630). A click on the name
/// starts it; Enter, a click elsewhere in the window or closing the window keeps the name, Escape
/// takes it back (Open1602: the original has no way back and changes the name with every key).
class NameEdit {
public:
    /// Characters of a name: the original's input takes 16 (0x0043CE00: 0x00409910(…, 17)).
    static constexpr std::size_t kMaxLength = 16;
    /// The cursor behind the text while typing, as in the original.
    static constexpr char kCursor = '_';

    /// Starts typing the name of `target` with its current name; nothing if `player` may not rename
    /// it, or if that name is typed already. A name being typed for another target is kept first:
    /// its command comes back.
    std::optional<sim::Command> start(const sim::World& world, sim::PlayerId player, const NameTarget& target);
    [[nodiscard]] bool active() const noexcept { return m_target.has_value(); }
    [[nodiscard]] const std::optional<NameTarget>& target() const noexcept { return m_target; }
    [[nodiscard]] const std::string& text() const noexcept { return m_text; }

    /// What a window shows as the name of `target`: while it is typed the text with the cursor,
    /// else `name` (the world's). Tells the editor that the target is still on screen.
    [[nodiscard]] std::string shown(const NameTarget& target, std::string_view name);

    /// Text typed (Windows-1252): control characters are dropped, the name stops at kMaxLength.
    void type(std::string_view text);
    /// A key while typing: Backspace (8) takes the last character, Enter (13) keeps the name (its
    /// command comes back), Escape (27) takes it back.
    std::optional<sim::Command> key(int key);
    /// Ends typing and keeps the name: the command, if the name changed and is one.
    std::optional<sim::Command> commit();
    /// Ends typing; the name stays as it was.
    void cancel() noexcept;

    /// Brackets a click in a window: unless the click started typing this target again (start()),
    /// the name being typed is kept.
    void begin_click() noexcept { m_clicked_name = false; }
    std::optional<sim::Command> end_click();

    /// Once per frame, after the windows were filled: typing ends when the target is gone or no
    /// longer the player's (the name is dropped), or when no window showed it (the name is kept).
    std::optional<sim::Command> end_frame(const sim::World& world);

private:
    std::optional<NameTarget> m_target;
    sim::PlayerId m_player = sim::kNoPlayer;
    std::string m_text;
    std::string m_original;  ///< the name when typing started
    bool m_shown = false;    ///< a window showed the target this frame
    bool m_clicked_name = false;
};

}  // namespace o1602::app
