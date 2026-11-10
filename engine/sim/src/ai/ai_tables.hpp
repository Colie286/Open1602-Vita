// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the computer players know of the game's buildings, in one place (MD-4): which slot of a city
// record counts a producer, the Needs that build them, the public buildings and their thresholds,
// the crop of the first colony and the original's cheat amounts. The stage orders that ask the
// Needs are tables in ai_planner.cpp. Builders of a Need pick the record by ware and raw material
// from the building table (`Engine::producer`), so a mod's replacement of a producer is built and
// counted without a change here.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "ai_engine.hpp"

#include "o1602/sim/houses.hpp"

namespace o1602::sim::ai {

/// How the survey (task 8) recognises a producer: its ware, its raw material (0: any) and its
/// record (0: any). The first matching row wins, so specific rows come before the general one.
struct ProducerCount {
    std::uint8_t slot;
    std::uint8_t ware;
    std::uint8_t raw = 0;
    std::uint32_t id = 0;
};

inline constexpr std::array<ProducerCount, 30> kProducerCounts{{
    {kSlotWoodcutter, ware::kWood},
    {kSlotHunter, ware::kFood, ware::kGame},
    {kSlotBakery, ware::kFood, ware::kFlour},
    {kSlotFisher, ware::kFood, ware::kFish},  // the original counts them as hunters too (S-098)
    {kSlotButcher, ware::kFood, ware::kMeat},
    {kSlotGrain, ware::kGrain},
    {kSlotMill, ware::kFlour},
    {kSlotQuarry, ware::kStone},
    {kSlotStonemason, ware::kBricks},
    {kSlotCotton, ware::kWool, ware::kCottonField},
    {kSlotSheep, ware::kWool},
    {kSlotWebstube, ware::kCloth, 0, id::kWebstube},
    {kSlotWeberei, ware::kCloth},
    // The original counts every alcohol producer as a cocoa plantation too (S-098).
    {kSlotWine, ware::kAlcohol, ware::kWineField},
    {kSlotRum, ware::kAlcohol},
    {kSlotSpices, ware::kSpices},
    {kSlotCocoa, ware::kCocoa},
    {kSlotTobacco, ware::kTobacco},
    {kSlotTobaccoGoods, ware::kTobaccoGoods},
    {kSlotSugar, ware::kSugar},
    {kSlotCattle, ware::kMeat},
    {kSlotIronMine, ware::kOre},
    {kSlotGoldMine, ware::kGold},
    {kSlotSmelter, ware::kIron},
    {kSlotToolsmith, ware::kTools},
    {kSlotTailor, ware::kClothes},
    {kSlotSwords, ware::kSwords},
    {kSlotMuskets, ware::kMuskets},
    {kSlotCannons, ware::kCannons},
    {kSlotGoldsmith, ware::kJewellery},
}};

/// The slot a producer of ware `w` from `raw` (record `building_id`) counts for in the survey.
inline std::optional<std::uint8_t> producer_slot(std::uint8_t w, std::uint8_t raw, std::uint32_t building_id) noexcept {
    for (const ProducerCount& c : kProducerCounts) {
        if (c.ware == w && (c.raw == 0 || c.raw == raw) && (c.id == 0 || c.id == building_id)) {
            return c.slot;
        }
    }
    return std::nullopt;
}

/// A Need of the production table (docs/re/ai.md, table of the Needs).
struct NeedDef {
    std::uint8_t slot;
    std::uint8_t task;
    std::uint8_t ware;    ///< producer of this ware ...
    std::uint8_t raw;     ///< ... from this raw material
    std::uint32_t fixed;  ///< or this building
    std::uint8_t field;   ///< crop of the fields (farm money rule), 0 = none
    NeedType type;
    std::uint8_t attempts;  ///< tried while attempts < this
    std::uint8_t fertile;   ///< the island must grow this crop (0 = no test)
    bool from_start;        ///< mines: the search starts at the first point of segment 0
};

inline constexpr std::array<NeedDef, 32> kNeeds{{
    {kSlotHunter, kHunter, 0, 0, id::kHunter, 0, NeedType::A, 2, 0, false},
    {kSlotWoodcutter, kWoodcutter, ware::kWood, ware::kTree, 0, ware::kTree, NeedType::B, 2, 0, false},
    {kSlotGrain, kGrainFarm, ware::kGrain, ware::kCornField, 0, ware::kCornField, NeedType::B, 2, ware::kCornField,
     false},
    {kSlotMill, kMill, ware::kFlour, ware::kGrain, 0, 0, NeedType::B, 2, ware::kCornField, false},
    {kSlotBakery, kBakery, ware::kFood, ware::kFlour, 0, 0, NeedType::B, 2, 0, false},
    {kSlotQuarry, kQuarry, ware::kStone, 0, 0, 0, NeedType::A, 1, 0, true},
    {kSlotStonemason, kStonemason, ware::kBricks, ware::kStone, 0, 0, NeedType::B, 1, 0, false},
    {kSlotFisher, kFisher, ware::kFood, ware::kFish, 0, 0, NeedType::B, 2, 0, false},
    {kSlotCotton, kCotton, ware::kWool, ware::kCottonField, 0, ware::kCottonField, NeedType::B, 2, ware::kCottonField,
     false},
    {kSlotWebstube, kWeaving, 0, 0, id::kWebstube, 0, NeedType::B, 2, 0, false},
    {kSlotWeberei, kWeaving, 0, 0, id::kWeberei, 0, NeedType::B, 2, 0, false},
    {kSlotTailor, kTailor, ware::kClothes, ware::kCloth, 0, 0, NeedType::B, 2, 0, false},
    {kSlotCattle, kCattle, ware::kMeat, ware::kGrass, 0, ware::kGrass, NeedType::B, 2, 0, false},
    {kSlotButcher, kButcher, ware::kFood, ware::kMeat, 0, 0, NeedType::B, 2, 0, false},
    {kSlotTobacco, kTobacco, ware::kTobacco, ware::kTobaccoField, 0, ware::kTobaccoField, NeedType::B, 2,
     ware::kTobaccoField, false},
    {kSlotTobaccoGoods, kTobaccoGoods, ware::kTobaccoGoods, ware::kTobacco, 0, 0, NeedType::B, 2, 0, false},
    {kSlotSpices, kSpices, ware::kSpices, ware::kSpiceField, 0, ware::kSpiceField, NeedType::B, 2, ware::kSpiceField,
     false},
    {kSlotWine, kWine, ware::kAlcohol, ware::kWineField, 0, ware::kWineField, NeedType::B, 2, ware::kWineField, false},
    {kSlotSugar, kSugar, ware::kSugar, ware::kSugarField, 0, ware::kSugarField, NeedType::B, 2, ware::kSugarField,
     false},
    {kSlotRum, kRum, ware::kAlcohol, ware::kSugar, 0, 0, NeedType::B, 2, 0, false},
    {kSlotCocoa, kCocoa, ware::kCocoa, ware::kCocoaField, 0, ware::kCocoaField, NeedType::B, 2, ware::kCocoaField,
     false},
    {kSlotSheep, kSheep, ware::kWool, ware::kGrass, 0, ware::kGrass, NeedType::B, 2, 0, false},
    {kSlotSmelter, kSmelter, ware::kIron, ware::kOre, 0, 0, NeedType::A, 2, 0, false},
    {kSlotToolsmith, kToolsmith, ware::kTools, ware::kIron, 0, 0, NeedType::A, 2, 0, false},
    {kSlotSwords, kSwords, ware::kSwords, ware::kIron, 0, 0, NeedType::A, 2, 0, false},
    {kSlotMuskets, kMuskets, ware::kMuskets, ware::kIron, 0, 0, NeedType::A, 2, 0, false},
    {kSlotCannons, kCannonFoundry, ware::kCannons, ware::kIron, 0, 0, NeedType::D, 2, 0, false},
    {kSlotGoldsmith, kGoldsmith, ware::kJewellery, ware::kGold, 0, 0, NeedType::A, 2, 0, false},
    {kSlotIronMine, kIronMine, 0, 0, id::kOreMine, 0, NeedType::A, 2, 0, true},
    {kSlotGoldMine, kGoldMine, 0, 0, id::kGoldMine, 0, NeedType::A, 1, 0, true},
    {kSlotStoneTower, kTowers, 0, 0, id::kStoneTower, 0, NeedType::D, 2, 0, false},
    {kSlotWoodTower, kTowers, 0, 0, id::kWoodTower, 0, NeedType::D, 2, 0, false},
}};

inline const NeedDef* need_def(std::uint8_t slot) {
    for (const NeedDef& d : kNeeds) {
        if (d.slot == slot) {
            return &d;
        }
    }
    return nullptr;
}

/// A public building task: its building, slot, flag, the service its houses lack and how many
/// such houses a site must reach.
struct PublicTask {
    std::uint8_t task;
    std::uint32_t id;
    std::uint8_t slot;
    std::uint32_t flag;
    std::uint16_t service;
    std::int32_t min_houses;
    bool roads_on_fail;
};

inline constexpr std::array<PublicTask, 10> kPublicTasks{{
    {kKapelle, id::kKapelle, kSlotKapelle, city_flag::kKapelle, house_service::kKapelle | house_service::kKirche, 3,
     true},
    {kWirt, id::kWirt, kSlotWirt, city_flag::kWirt, house_service::kWirtshaus, 8, true},
    {kSchule, id::kSchule, kSlotSchule, city_flag::kSchule, house_service::kSchule | house_service::kHochschule, 8,
     true},
    {kKlinik, id::kKlinik, kSlotKlinik, city_flag::kKlinik, house_service::kKlinik, 8, true},
    {kKirche, id::kKirche, kSlotKirche, city_flag::kKirche, house_service::kKirche, 16, true},
    {kBadehaus, id::kBadehaus, kSlotBadehaus, city_flag::kBadehaus, house_service::kBadehaus, 8, true},
    {kHochschule, id::kHochschule, kSlotHochschule, city_flag::kHochschule, house_service::kHochschule, 10, true},
    {kTheater, id::kTheater, kSlotTheater, city_flag::kTheater, house_service::kTheater, 10, true},
    {kGalgen, id::kGalgen, kSlotGalgen, city_flag::kGalgen, house_service::kGalgen, 10, false},
    {kBrunnen, id::kBrunnen, kSlotBrunnen, city_flag::kBrunnen, house_service::kBrunnen, 6, false},
}};

inline const PublicTask* public_task(std::uint8_t task) {
    for (const PublicTask& t : kPublicTasks) {
        if (t.task == task) {
            return &t;
        }
    }
    return nullptr;
}

/// Crop the first colony of a computer must grow fully (0x00429070), by its slot.
inline std::uint8_t first_crop(PlayerId player) {
    switch (player % 4) {
        case 1:
            return ware::kSpiceField;
        case 2:
            return ware::kTobaccoField;
        case 3:
            return ware::kWineField;
        default:
            return ware::kOre;
    }
}

/// The original's cheats: money and goods the computers get on every difficulty.
struct Cheats {
    std::int32_t steady_below = 3791;   ///< task 5: money below this ...
    std::int32_t steady_money = 379;    ///< ... gets this much
    std::int32_t founding_below = 500;  ///< a founding with less money than this ...
    std::int32_t founding_money = 799;  ///< ... gets this much
    std::uint32_t founding_bricks = 8;  ///< tons into a new city
    /// Task 5's subsidy of a computer that is behind (0x00428370), per step: money below the
    /// first number gets the second, below the third (from settlers on) the fourth.
    std::int32_t subsidy_poor_below = 4733, subsidy_poor = 797;
    std::int32_t subsidy_below = 9376, subsidy = 1793;
    /// Tons of wood and tools while the stock is below as much.
    std::uint32_t subsidy_wood = 7, subsidy_tools = 3;
    /// The colony subsidy: one ware per call into a city that is behind, while its stock (tons)
    /// is below the first number, in this order; food before the second inhabited city, bricks after.
    std::uint32_t wood_below = 19, wood = 13;
    std::uint32_t tools_below = 15, tools = 9;
    std::uint32_t food_below = 19, food = 9;
    std::uint32_t bricks_below = 19, bricks = 9;
};
inline constexpr Cheats kCheats{};

/// Raw materials the subsidy tops up (one ton per step below three) in cities of 50 settlers
/// with more than three of their producers.
struct SubsidyRaw {
    std::uint8_t slot;
    std::uint8_t ware;
};
inline constexpr std::array<SubsidyRaw, 5> kSubsidyRaws{{{kSlotSpices, ware::kSpices},
                                                         {kSlotTobacco, ware::kTobacco},
                                                         {kSlotWine, ware::kAlcohol},
                                                         {kSlotSugar, ware::kSugar},
                                                         {kSlotCotton, ware::kWool}}};

}  // namespace o1602::sim::ai
