// SPDX-License-Identifier: GPL-3.0-or-later
//
// The white texts that rise over the local player's production buildings when their store gains a
// ton ("Lager: 3") or is full ("Lager voll"), as the original shows them (0x0047DAF0 -> 0x00446BD0,
// docs/re/production.md#floating-texts), and the gold of a buried treasure ("+1000", 0x004084D0).
// Only a picture: the game never reads them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/production_cycle.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::app {

/// How long a text rises: 2.5 units at 0.04 per 20 ms step (Figures_Step 0x00451890), 1.25 s.
inline constexpr double kStockNoteMs = 1250.0;
/// Height at which a text starts, in half tile widths (0x00446BD0 -> Figure_Alloc).
inline constexpr double kStockNoteStartHeight = 0.5;
/// How fast it rises, in half tile widths per second (0.04 per 20 ms step).
inline constexpr double kStockNoteRise = 2.0;
/// The original keeps the texts in 176 slots (0x004E8870); when they are taken, no new one shows.
inline constexpr std::size_t kMaxStockNotes = 176;

/// One text over a building.
struct StockNote {
    double x = 0.0;  ///< the building's centre in world tiles (Island_BuildingCenter 0x00463A70)
    double y = 0.0;
    std::string text;
    double start_ms = 0.0;  ///< game time it appeared

    /// Height in half tile widths `now_ms` (game time), for as long as it is shown.
    [[nodiscard]] double height(double now_ms) const noexcept {
        return kStockNoteStartHeight + kStockNoteRise * (now_ms - start_ms) / 1000.0;
    }
};

/// The text of an event of the production cycle, empty for the kinds that show none: Text.cod
/// [GAME] 0 with the tons in store ("%s %d"), or [GAME] 17 ("Lager voll").
[[nodiscard]] std::string stock_note_text(const assets::TextTable& texts, const sim::ProductionEvent& event);

class StockNotes {
public:
    /// Forgets every text, e.g. when another game is loaded.
    void clear() noexcept { m_notes.clear(); }

    /// The texts of the events of the ticks since the last call, for the buildings in cities of
    /// `local`, appearing at `now_ms`. A cycle that fills the store shows "Lager voll" instead of
    /// the new ton, as the original's second text replaces the first.
    void add(const sim::World& world, const assets::BuildingTable& buildings, const assets::TextTable& texts,
             std::span<const sim::ProductionEvent> events, sim::PlayerId local, double now_ms);

    /// "+<gold>" over the buildings of `local` that were built on a treasure (sprintf "+%d" over
    /// Island_BuildingCenter, 0x004084D0), appearing at `now_ms`.
    void add_treasures(const sim::World& world, const assets::BuildingTable& buildings,
                       std::span<const sim::ConstructionEvent> events, sim::PlayerId local, double now_ms);

    /// Removes the texts that have risen out of sight by `now_ms`.
    void expire(double now_ms);

    [[nodiscard]] const std::vector<StockNote>& notes() const noexcept { return m_notes; }

private:
    std::vector<StockNote> m_notes;
};

}  // namespace o1602::app
