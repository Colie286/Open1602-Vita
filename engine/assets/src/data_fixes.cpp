// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/data_fixes.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"

namespace o1602::assets::fixes {

namespace {

constexpr std::uint32_t kBakery = id::kBakery;
constexpr std::uint16_t kKontorFlag = figure::kKontorFlag;
constexpr std::size_t kBlock = 2;                       // FORM_QUAD
constexpr std::size_t kBlockSlot = 17;                  // the third place of the right-hand row
constexpr FigureTable::FormationSlot kBlockWas{2, -1};  // slot 15's place again
constexpr FigureTable::FormationSlot kBlockIs{2, 1};    // the place the row lacks

}  // namespace

std::vector<std::string_view> fix_buildings(BuildingTable& buildings) {
    std::vector<std::string_view> applied;
    // D-001: `Bausample: WAV_BAECKER` names a sound the game does not define, so it counts as 0.
    if (BuildingType* bakery = buildings.by_id(kBakery); bakery != nullptr && bakery->build_sound == 0) {
        bakery->build_sound = kBakerySound;
        applied.emplace_back("D-001");
    }
    // D-002: `Rauchfignr: FAHNEKONTOR` (without the number of a Kontor level) is undefined, so 0.
    if (BuildingType* kontor = buildings.by_id(id::kPirateKontor);
        kontor != nullptr && kontor->prod_kind == prod::kKontor && kontor->attached_figures[0] == 0) {
        kontor->attached_figures[0] = kKontorFlag;
        applied.emplace_back("D-002");
    }
    return applied;
}

std::vector<std::string_view> fix_figures(FigureTable& figures) {
    std::vector<std::string_view> applied;
    // D-014: the right-hand row of the block reads (2, -1), (2, 0), (2, -1); from 18 units on two
    // stood on one place.
    auto& formations = figures.formations();
    if (kBlock < formations.size() && kBlockSlot < formations[kBlock].size()) {
        auto& slot = formations[kBlock][kBlockSlot];
        if (slot.x == kBlockWas.x && slot.y == kBlockWas.y) {
            slot = kBlockIs;
            applied.emplace_back("D-014");
        }
    }
    return applied;
}

}  // namespace o1602::assets::fixes
