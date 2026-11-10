// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the player selected on the map, and how the windows tell a fresh selection from a repeated
// click: a window always opens on its first page; a Kontor or market place clicked again shows its
// stock (Panel_UpdateSelection 0x0043AA00). Needs no platform, so that the tests have it.
#pragma once

#include <cstdint>

namespace o1602::app {

/// The modes of the side panel (CTRL.GAD, Hud_SetMode 0x0043B660).
namespace side_mode {
inline constexpr std::int32_t kBuild = 30011;
inline constexpr std::int32_t kMilitary = 30012;
inline constexpr std::int32_t kInfo = 30013;
inline constexpr std::int32_t kOptions = 30014;
/// A second military button in the place of the first and drawn over it. The original hides it and
/// makes it unclickable at the start of a game (0x0042BF22); left in, it takes the clicks meant for
/// the military mode and covers the military button's selected picture.
inline constexpr std::int32_t kMilitaryDuplicate = 30015;
}  // namespace side_mode

/// The mode after a left click on the map in `mode` (`selected`: it selected a building or a
/// unit). The settings give way to the info mode, which shows the window of the selection
/// (Map_OnLeftUp 0x0040AFF0 handles the settings like the info mode and calls 0x0043B7F0); a
/// click on nothing keeps them. The build and the military mode stay.
[[nodiscard]] constexpr std::int32_t mode_after_selecting(std::int32_t mode, bool selected) noexcept {
    return mode == side_mode::kOptions && selected ? side_mode::kInfo : mode;
}

/// What the player selected on the map.
struct Selection {
    /// ShipList, CityList, Diplomacy: the windows behind the player status (no object selected).
    enum class Kind : std::uint8_t { None, Building, Ship, ShipList, CityList, Diplomacy };
    Kind kind = Kind::None;
    std::uint16_t island = 0;  ///< Building: its origin on the island
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint32_t ship = 0;  ///< Ship: Ship::id
    /// Diplomacy: the partner (player number) the window shows first, 0xFF none (an offer's icon in
    /// the status bar opens the window with its partner).
    std::uint8_t partner = 0xFF;
    /// Building: how often the same building was clicked again since it was selected (a Kontor
    /// then shows its stock). 0 on the first click.
    std::uint16_t again = 0;
    /// Counts the selections: a new one for every object selected anew, also the same one after it
    /// was deselected, so that its window starts on its first page again (Hud::set_selection).
    std::uint32_t serial = 0;
    friend bool operator==(const Selection&, const Selection&) = default;

    /// The same object, however it was clicked.
    [[nodiscard]] constexpr bool same_object(const Selection& o) const noexcept {
        return kind == o.kind && island == o.island && x == o.x && y == o.y && ship == o.ship;
    }
};

/// A click on the map picked `picked` (a building or nothing) while `before` was selected: the
/// same building once more counts its repeated clicks.
[[nodiscard]] constexpr Selection clicked_selection(const Selection& before, Selection picked) noexcept {
    picked.again = 0;
    if (picked.kind == Selection::Kind::Building && before.same_object(picked)) {
        picked.again = static_cast<std::uint16_t>(before.again == UINT16_MAX ? 1 : before.again + 1);
    }
    return picked;
}

/// The selection the windows see after `next` replaced `before`: the serial stays for the same
/// object and is new (`counter` counts up) for another one or after nothing was selected.
[[nodiscard]] constexpr Selection stamped_selection(const Selection& before, Selection next,
                                                    std::uint32_t& counter) noexcept {
    if (next.kind != Selection::Kind::None && before.kind != Selection::Kind::None && before.same_object(next)) {
        next.serial = before.serial;
    } else {
        next.serial = ++counter;
    }
    return next;
}

/// For a window with sub-pages: what a new frame's selection means for the page shown.
class SelectionPages {
public:
    enum class Change : std::uint8_t {
        None,    ///< the same selection as before: the page stays
        Opened,  ///< selected anew: the first page
        Again,   ///< the same building clicked again: the stock (Kontor, market place)
    };

    [[nodiscard]] constexpr Change update(const Selection& s) noexcept {
        if (!m_seen || s.serial != m_last.serial || !s.same_object(m_last)) {
            m_seen = true;
            m_last = s;
            return s.again > 0 ? Change::Again : Change::Opened;
        }
        const bool again = s.again != m_last.again && s.again > 0;
        m_last = s;
        return again ? Change::Again : Change::None;
    }

private:
    Selection m_last;
    bool m_seen = false;
};

}  // namespace o1602::app
