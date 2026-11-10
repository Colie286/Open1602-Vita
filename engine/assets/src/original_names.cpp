// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/original_names.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace o1602::assets {

namespace {

/// The package the original's content belongs to.
constexpr std::string_view kBasePackage = "anno1602";

// In the order of the build menu's categories (ui::original_build_menu) and their Text.cod
// sections, then a few of [LANDSCHAFT].
constexpr std::array kBuildings{
    // [MILITAR]
    OriginalName{"palisade", 22521},
    OriginalName{"wooden_gate", 21404},
    OriginalName{"wooden_watchtower", 21405},
    OriginalName{"wall", 22501},
    OriginalName{"gate", 20304},
    OriginalName{"watchtower", 20305},
    OriginalName{"city_gate", 20303},
    OriginalName{"castle", 20311},
    OriginalName{"large_castle", 20313},
    OriginalName{"fortress", 20315},
    // [STRASSE]
    OriginalName{"dirt_road", 20411},
    OriginalName{"paved_road", 20401},
    OriginalName{"wooden_bridge", 21601},
    OriginalName{"stone_bridge", 21603},
    OriginalName{"square_1", 20421},
    OriginalName{"square_2", 20422},
    OriginalName{"square_3", 20423},
    OriginalName{"ornamental_tree", 20424},
    // [BERGWERK]
    OriginalName{"quarry", 22407},
    OriginalName{"ore_mine", 22401},
    OriginalName{"deep_ore_mine", 22403},
    OriginalName{"gold_mine", 22405},
    // [HANDWERK]
    OriginalName{"weaving_hut", 20507},
    OriginalName{"butcher", 20511},
    OriginalName{"grain_mill", 20501},
    OriginalName{"bakery", 20503},
    OriginalName{"tobacco_products", 20525},
    OriginalName{"rum_distillery", 20519},
    OriginalName{"tailor", 20505},
    OriginalName{"weaving_mill", 20531},
    OriginalName{"stonemason", 20513},
    OriginalName{"water_mill", 20509},
    OriginalName{"ore_smelter", 20521},
    OriginalName{"toolmaker", 20515},
    OriginalName{"goldsmith", 20533},
    OriginalName{"swordsmith", 20517},
    OriginalName{"musket_maker", 20529},
    OriginalName{"cannon_foundry", 20527},
    // [FARM]
    OriginalName{"hunters_hut", 22003},
    OriginalName{"forester", 22001},
    OriginalName{"forest", 21303},
    OriginalName{"grain_farm", 21503},
    OriginalName{"grain_field", 21301},
    OriginalName{"sheep_farm", 21515},
    OriginalName{"cattle_farm", 21513},
    OriginalName{"tobacco_plantation", 21507},
    OriginalName{"tobacco_field", 21335},
    OriginalName{"spice_plantation", 21501},
    OriginalName{"spice_field", 21341},
    OriginalName{"winery", 21517},
    OriginalName{"vines", 21343},
    OriginalName{"sugarcane_plantation", 21511},
    OriginalName{"sugarcane_field", 21339},
    OriginalName{"cotton_plantation", 21505},
    OriginalName{"cotton_field", 21331},
    OriginalName{"cocoa_plantation", 21509},
    OriginalName{"cocoa_field", 21337},
    // [HAFEN]
    OriginalName{"warehouse_1", 22101},
    OriginalName{"warehouse_2", 22103},
    OriginalName{"warehouse_3", 22105},
    OriginalName{"warehouse_4", 22107},
    OriginalName{"fishers_hut", 21075},
    OriginalName{"pier", 21073},
    OriginalName{"small_shipyard", 22115},
    OriginalName{"large_shipyard", 22111},
    // [DIVERS]
    OriginalName{"house", 20605},
    OriginalName{"marketplace", 20809},
    OriginalName{"chapel", 20803},
    OriginalName{"church", 20805},
    OriginalName{"cathedral", 20801},
    OriginalName{"tavern", 20807},
    OriginalName{"doctor", 20817},
    OriginalName{"school", 20821},
    OriginalName{"college", 20819},
    OriginalName{"bathhouse", 20813},
    OriginalName{"theatre", 20815},
    OriginalName{"fire_station", 20811},
    OriginalName{"palace", 20831},
    OriginalName{"gallows", 20841},
    OriginalName{"monument", 20851},
    OriginalName{"triumphal_arch", 20853},
    // [LANDSCHAFT]
    OriginalName{"chieftains_hut", 22231},
    OriginalName{"aztec_pyramid", 22211},
    OriginalName{"pirate_warehouse", 22221},
    OriginalName{"pirate_house_1", 22222},
    OriginalName{"pirate_house_2", 22223},
    OriginalName{"pirate_tower", 22241},
    OriginalName{"mixed_forest", 21304},
    OriginalName{"palm_forest", 22802},
};

// figuren.cod's symbols (cod_predefined_figuren): the units and ships that fight, and the
// figures that carry and work.
constexpr std::array kFigures{
    OriginalName{"swordsman", 1, 4},          // SOLDAT1..4
    OriginalName{"cavalry", 5, 4},            // KAVALERIE1..4
    OriginalName{"musketeer", 9, 4},          // MUSKETIER1..4
    OriginalName{"cannoneer", 13, 4},         // KANONIER1..4
    OriginalName{"small_merchant_ship", 21},  // HANDEL1
    OriginalName{"large_merchant_ship", 23},  // HANDEL2
    OriginalName{"small_warship", 25},        // KRIEG1
    OriginalName{"large_warship", 27},        // KRIEG2
    OriginalName{"free_trader", 29},          // HANDLER
    OriginalName{"pirate_ship", 31},          // PIRAT
    OriginalName{"spearman", 33, 4},          // SPEER1..4
    OriginalName{"harvester", 74},            // MAEHER
    OriginalName{"picker", 75},               // PFLUECKER
    OriginalName{"picker_2", 76},             // PFLUECKER2
    OriginalName{"woodcutter", 77},           // HOLZFAELLER
    OriginalName{"stonecutter", 78},          // STEINKLOPFER
    OriginalName{"hunter", 79},               // JAEGER
    OriginalName{"carrier", 80},              // TRAEGER
    OriginalName{"carrier_2", 81},            // TRAEGER2
    OriginalName{"cart", 82},                 // KARREN
    OriginalName{"butcher", 83},              // FLEISCHER
    OriginalName{"doctor", 84},               // ARZT
    OriginalName{"firefighter", 85},          // LOESCH
    OriginalName{"pack_donkey", 86},          // PACKESEL
    OriginalName{"fisherman", 90},            // FISCHER
};

/// A whole number, decimal or 0x hex.
std::optional<std::uint32_t> number_of(std::string_view text) noexcept {
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
        base = 16;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// The trade goods, Text.cod [WARE] from Eisenerz to Ziegel.
constexpr std::array kWares{
    OriginalName{"ore", 2},      OriginalName{"gold", 3},           OriginalName{"wool", 4},
    OriginalName{"sugar", 5},    OriginalName{"tobacco", 6},        OriginalName{"cattle", 7},
    OriginalName{"grain", 8},    OriginalName{"flour", 9},          OriginalName{"iron", 10},
    OriginalName{"swords", 11},  OriginalName{"muskets", 12},       OriginalName{"cannons", 13},
    OriginalName{"food", 14},    OriginalName{"tobacco_goods", 15}, OriginalName{"spices", 16},
    OriginalName{"cocoa", 17},   OriginalName{"alcohol", 18},       OriginalName{"cloth", 19},
    OriginalName{"clothes", 20}, OriginalName{"jewellery", 21},     OriginalName{"tools", 22},
    OriginalName{"wood", 23},    OriginalName{"bricks", 24},
};

/// haeuser.cod's BAUINFRA by INFRA number: the public buildings a class asks for, the buildings
/// one has to be allowed, then the steps INFRA_STUFE_1A to 5B.
constexpr std::array kLevels{
    OriginalName{"market", 1},          OriginalName{"chapel", 2},    OriginalName{"school", 3},
    OriginalName{"tavern", 4},          OriginalName{"church", 5},    OriginalName{"bathhouse", 6},
    OriginalName{"theatre", 7},         OriginalName{"college", 8},   OriginalName{"doctor", 9},
    OriginalName{"gallows", 10},        OriginalName{"castle", 11},   OriginalName{"cathedral", 12},
    OriginalName{"triumphal_arch", 13}, OriginalName{"monument", 14}, OriginalName{"level_1a", 15},
    OriginalName{"level_2a", 16},       OriginalName{"level_2b", 17}, OriginalName{"level_2c", 18},
    OriginalName{"level_2d", 19},       OriginalName{"level_2e", 20}, OriginalName{"level_2f", 21},
    OriginalName{"level_3a", 22},       OriginalName{"level_2g", 23}, OriginalName{"level_3b", 24},
    OriginalName{"level_3c", 25},       OriginalName{"level_3d", 26}, OriginalName{"level_3e", 27},
    OriginalName{"level_3f", 28},       OriginalName{"level_4a", 29}, OriginalName{"level_4b", 30},
    OriginalName{"level_5a", 31},       OriginalName{"level_5b", 32},
};

/// haeuser.cod's BGRUPPE, as Text.cod's [GAME] names them.
constexpr std::array kClasses{
    OriginalName{"pioneers", 0},  OriginalName{"settlers", 1},    OriginalName{"citizens", 2},
    OriginalName{"merchants", 3}, OriginalName{"aristocrats", 4},
};

}  // namespace

std::optional<std::uint32_t> original_building_id(std::string_view id) noexcept {
    const std::size_t colon = id.find(':');
    if (colon == std::string_view::npos || id.substr(0, colon) != kBasePackage) {
        return std::nullopt;
    }
    const std::string_view after = id.substr(colon + 1);
    if (const OriginalName* named = find_original_name(original_building_names(), after)) {
        return named->first;
    }
    return number_of(after);
}

std::span<const OriginalName> original_building_names() noexcept {
    return kBuildings;
}

std::span<const OriginalName> original_ware_names() noexcept {
    return kWares;
}

std::span<const OriginalName> original_level_names() noexcept {
    return kLevels;
}

std::span<const OriginalName> original_class_names() noexcept {
    return kClasses;
}

std::span<const OriginalName> original_figure_names() noexcept {
    return kFigures;
}

const OriginalName* find_original_name(std::span<const OriginalName> names, std::string_view name) noexcept {
    const auto it = std::find_if(names.begin(), names.end(), [&](const OriginalName& n) { return n.name == name; });
    return it == names.end() ? nullptr : &*it;
}

}  // namespace o1602::assets
