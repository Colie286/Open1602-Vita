// SPDX-License-Identifier: GPL-3.0-or-later
//
// The diplomacy window with more partners than VERTRAG.GAD has rows (Open1602): the original has
// rows for three partners, a game may have eleven. Up to three the window stays as it was; with
// more, a strip of their coats of arms in the frames the pirates' order window chooses a player
// with (PIRATORD.GAD, GFXPIRAT + 11) heads the window, and the chosen partner's treaties and the
// tribute follow below in the layout of the tribute page (TRIBUT.GAD), moved down by the strip.
//
// Offers a partner made that the local player has not answered yet also show in the status bar,
// as the original's treaty message icon in the partner's colour, until they are answered.
// Pure logic, so that the tests have it; the window itself is diplomacy_panel.cpp.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "o1602/sim/player.hpp"

namespace o1602::sim {
class World;
}

namespace o1602::app::diplomacy {

/// Partners VERTRAG.GAD has rows for.
inline constexpr std::size_t kOriginalRows = 3;
/// Most partners a game can have.
inline constexpr std::size_t kMaxPartners = sim::kMaxPlayers - 1;

/// The strip: frames of 41 x 49 with the coat of arms (32 x 40) 2 pixels in, six in a row, one
/// or two rows centred in the panel's 250 pixels from x = 6; in design units of the side panel.
inline constexpr std::int32_t kFrameWidth = 41;
inline constexpr std::int32_t kFrameHeight = 49;
inline constexpr std::int32_t kArmsInset = 2;
inline constexpr std::size_t kPerRow = 6;
inline constexpr std::int32_t kPanelLeft = 6;
inline constexpr std::int32_t kPanelWidth = 250;
inline constexpr std::int32_t kFirstRowY = 355;
inline constexpr std::int32_t kRowStep = 47;
inline constexpr std::int32_t kSingleRowY = 378;
/// How far the treaties and the hand of TRIBUT.GAD move down below the strip; the tribute's
/// slider, amount and button stay where they are.
inline constexpr std::int32_t kTreatiesShift = 96;

/// Whether the window shows the strip for this many partners (`rows`: what VERTRAG.GAD has).
[[nodiscard]] constexpr bool uses_strip(std::size_t partners, std::size_t rows = kOriginalRows) noexcept {
    return partners > rows;
}

/// Top left of the frame of place `index` of `count` in the strip.
[[nodiscard]] std::array<std::int32_t, 2> strip_slot(std::size_t index, std::size_t count) noexcept;

/// The other humans and computers still playing, by player number: the partners of the window.
[[nodiscard]] std::vector<sim::PlayerId> partners(const sim::World& world, sim::PlayerId local);

/// Treaties a partner offered the local player that are not answered yet.
struct Offer {
    sim::PlayerId from = sim::kNoPlayer;
    bool trade = false;
    bool peace = false;
    friend bool operator==(const Offer&, const Offer&) = default;
};
/// The open offers of the partners, by player number.
[[nodiscard]] std::vector<Offer> open_offers(const sim::World& world, sim::PlayerId local);

/// The other humans and computers still playing that `player` has no signed peace with (war, or
/// a peace offered and not yet answered), by player number.
[[nodiscard]] std::vector<sim::PlayerId> enemies(const sim::World& world, sim::PlayerId player);
/// The help line over a partner's coat of arms, in the current language: who `player` is at war
/// with, or that it is at peace with everyone.
[[nodiscard]] std::string wars_text(const sim::World& world, sim::PlayerId player);

/// The line of the status bar for an offer, in the current language: "{name} offers you ...".
[[nodiscard]] std::string offer_text(const Offer& offer, const std::string& name);

}  // namespace o1602::app::diplomacy
