// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/computer_slots.hpp"

#include <algorithm>

#include "ai/ai_state.hpp"

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/houses.hpp"

namespace o1602::sim {

namespace {

using Kind = ComputerSlot::Kind;
using Wait = ProducerWait;

/// A slot that only counts.
ComputerSlot counter(std::uint8_t number, std::string_view name) {
    ComputerSlot s;
    s.name = name;
    s.number = number;
    return s;
}

/// The fields of a producer's Need.
struct Need {
    std::uint8_t task;
    std::uint8_t ware;
    std::uint8_t raw;
    std::uint32_t building;
    std::uint8_t field;
    Wait wait;
    std::uint8_t attempts;
    std::uint8_t fertile;
    bool from_start;
    std::uint8_t after = kNoProducer;
};

/// How the survey counts it.
struct Count {
    std::uint8_t ware = 0;
    std::uint8_t raw = 0;
    std::uint32_t building = 0;
};

ComputerSlot producer(std::uint8_t number, std::string_view name, const Need& n, const Count& c = {},
                      Kind kind = Kind::Producer) {
    ComputerSlot s = counter(number, name);
    s.kind = kind;
    s.task = n.task;
    s.ware = n.ware;
    s.raw = n.raw;
    s.building = n.building;
    s.field = n.field;
    s.wait = n.wait;
    s.attempts = n.attempts;
    s.fertile = n.fertile;
    s.from_start = n.from_start;
    s.after = n.after;
    s.count_ware = c.ware;
    s.count_raw = c.raw;
    s.count_building = c.building;
    return s;
}

ComputerSlot public_building(std::uint8_t number, std::string_view name, std::uint8_t task, std::uint32_t building,
                             std::uint32_t flag, std::uint16_t service, std::int32_t min_houses, bool roads_on_fail) {
    ComputerSlot s = counter(number, name);
    s.kind = Kind::Public;
    s.task = task;
    s.building = building;
    s.flag = flag;
    s.service = service;
    s.min_houses = min_houses;
    s.roads_on_fail = roads_on_fail;
    return s;
}

std::vector<ComputerSlot> make_original_slots() {
    using namespace ai;
    namespace hs = house_service;
    namespace cf = city_flag;
    constexpr Wait A = Wait::None;
    constexpr Wait B = Wait::Money;
    constexpr Wait D = Wait::Placed;
    std::vector<ComputerSlot> s;
    s.reserve(kSlots);
    s.push_back(counter(kSlotKontor, "kontor"));
    s.push_back(producer(kSlotWoodcutter, "woodcutter",
                         {kWoodcutter, ware::kWood, ware::kTree, 0, ware::kTree, B, 2, 0, false}, {ware::kWood}));
    s.push_back(
        producer(kSlotHunter, "hunter", {kHunter, 0, 0, id::kHunter, 0, A, 2, 0, false}, {ware::kFood, ware::kGame}));
    s.push_back(
        producer(kSlotGrain, "grain_farm",
                 {kGrainFarm, ware::kGrain, ware::kCornField, 0, ware::kCornField, B, 2, ware::kCornField, false},
                 {ware::kGrain}));
    s.push_back(producer(kSlotMill, "mill", {kMill, ware::kFlour, ware::kGrain, 0, 0, B, 2, ware::kCornField, false},
                         {ware::kFlour}));
    s.push_back(producer(kSlotBakery, "bakery", {kBakery, ware::kFood, ware::kFlour, 0, 0, B, 2, 0, false, kSlotMill},
                         {ware::kFood, ware::kFlour}));
    s.push_back(producer(kSlotQuarry, "quarry", {kQuarry, ware::kStone, 0, 0, 0, A, 1, 0, true}, {ware::kStone}));
    s.push_back(producer(kSlotStonemason, "stonemason",
                         {kStonemason, ware::kBricks, ware::kStone, 0, 0, B, 1, 0, false}, {ware::kBricks}));
    s.push_back(counter(kSlotMarket, "market"));
    s.push_back(counter(kSlotHouses, "houses"));
    s.push_back(public_building(kSlotKapelle, "chapel", kKapelle, id::kKapelle, cf::kKapelle,
                                hs::kKapelle | hs::kKirche, 3, true));
    s.push_back(
        producer(kSlotCotton, "cotton",
                 {kCotton, ware::kWool, ware::kCottonField, 0, ware::kCottonField, B, 2, ware::kCottonField, false},
                 {ware::kWool, ware::kCottonField}));
    s.push_back(producer(kSlotWebstube, "weaving_hut", {kWeaving, 0, 0, id::kWebstube, 0, B, 2, 0, false, kSlotSheep},
                         {ware::kCloth, 0, id::kWebstube}));
    // The original counts every alcohol producer as a cocoa plantation too (S-098).
    s.push_back(producer(kSlotWine, "wine",
                         {kWine, ware::kAlcohol, ware::kWineField, 0, ware::kWineField, B, 2, ware::kWineField, false},
                         {ware::kAlcohol, ware::kWineField}));
    s.push_back(public_building(kSlotWirt, "tavern", kWirt, id::kWirt, cf::kWirt, hs::kWirtshaus, 8, true));
    s.push_back(
        producer(kSlotSpices, "spices",
                 {kSpices, ware::kSpices, ware::kSpiceField, 0, ware::kSpiceField, B, 2, ware::kSpiceField, false},
                 {ware::kSpices}));
    s.push_back(public_building(kSlotSchule, "school", kSchule, id::kSchule, cf::kSchule, hs::kSchule | hs::kHochschule,
                                8, true));
    s.push_back(
        producer(kSlotCocoa, "cocoa",
                 {kCocoa, ware::kCocoa, ware::kCocoaField, 0, ware::kCocoaField, B, 2, ware::kCocoaField, false},
                 {ware::kCocoa}));
    s.push_back(producer(kSlotCattle, "cattle", {kCattle, ware::kMeat, ware::kGrass, 0, ware::kGrass, B, 2, 0, false},
                         {ware::kMeat}));
    s.push_back(producer(kSlotButcher, "butcher",
                         {kButcher, ware::kFood, ware::kMeat, 0, 0, B, 2, 0, false, kSlotCattle},
                         {ware::kFood, ware::kMeat}));
    s.push_back(producer(
        kSlotTobacco, "tobacco",
        {kTobacco, ware::kTobacco, ware::kTobaccoField, 0, ware::kTobaccoField, B, 2, ware::kTobaccoField, false},
        {ware::kTobacco}));
    s.push_back(producer(kSlotTobaccoGoods, "tobacco_goods",
                         {kTobaccoGoods, ware::kTobaccoGoods, ware::kTobacco, 0, 0, B, 2, 0, false, kSlotTobacco},
                         {ware::kTobaccoGoods}));
    s.push_back(producer(kSlotIronMine, "iron_mine", {kIronMine, 0, 0, id::kOreMine, 0, A, 2, 0, true}, {ware::kOre}));
    s.push_back(
        producer(kSlotSmelter, "smelter", {kSmelter, ware::kIron, ware::kOre, 0, 0, A, 2, 0, false}, {ware::kIron}));
    s.push_back(producer(kSlotToolsmith, "toolsmith",
                         {kToolsmith, ware::kTools, ware::kIron, 0, 0, A, 2, 0, false, kSlotSmelter}, {ware::kTools}));
    s.push_back(counter(kSlotShipyard, "shipyard"));
    s.push_back(counter(kSlotStoneRoads, "stone_roads"));
    s.push_back(producer(kSlotTailor, "tailor", {kTailor, ware::kClothes, ware::kCloth, 0, 0, B, 2, 0, false},
                         {ware::kClothes}));
    s.push_back(producer(kSlotSheep, "sheep", {kSheep, ware::kWool, ware::kGrass, 0, ware::kGrass, B, 2, 0, false},
                         {ware::kWool}));
    s.push_back(
        producer(kSlotSugar, "sugar",
                 {kSugar, ware::kSugar, ware::kSugarField, 0, ware::kSugarField, B, 2, ware::kSugarField, false},
                 {ware::kSugar}));
    s.push_back(producer(kSlotRum, "rum", {kRum, ware::kAlcohol, ware::kSugar, 0, 0, B, 2, 0, false, kSlotSugar},
                         {ware::kAlcohol}));
    // The original counts fishers as hunters too (S-098).
    s.push_back(producer(kSlotFisher, "fisher", {kFisher, ware::kFood, ware::kFish, 0, 0, B, 2, 0, false},
                         {ware::kFood, ware::kFish}));
    s.push_back(counter(kSlotFood, "food"));
    s.push_back(counter(kSlotAlcohol, "alcohol"));
    s.push_back(public_building(kSlotKlinik, "doctor", kKlinik, id::kKlinik, cf::kKlinik, hs::kKlinik, 8, true));
    s.push_back(producer(kSlotWeberei, "weaving_mill", {kWeaving, 0, 0, id::kWeberei, 0, B, 2, 0, false, kSlotCotton},
                         {ware::kCloth}));
    s.push_back(counter(kSlotRumBuilt, "rum_built"));
    s.push_back(public_building(kSlotKirche, "church", kKirche, id::kKirche, cf::kKirche, hs::kKirche, 16, true));
    s.push_back(
        public_building(kSlotBadehaus, "bath_house", kBadehaus, id::kBadehaus, cf::kBadehaus, hs::kBadehaus, 8, true));
    s.push_back(
        producer(kSlotWoodTower, "wood_tower", {kTowers, 0, 0, id::kWoodTower, 0, D, 2, 0, false}, {}, Kind::Tower));
    s.push_back(
        producer(kSlotStoneTower, "stone_tower", {kTowers, 0, 0, id::kStoneTower, 0, D, 2, 0, false}, {}, Kind::Tower));
    s.push_back(counter(kSlotPirateTower, "pirate_tower"));
    s.push_back(producer(kSlotSwords, "swords",
                         {kSwords, ware::kSwords, ware::kIron, 0, 0, A, 2, 0, false, kSlotSmelter}, {ware::kSwords}));
    s.push_back(producer(kSlotMuskets, "muskets",
                         {kMuskets, ware::kMuskets, ware::kIron, 0, 0, A, 2, 0, false, kSlotSmelter},
                         {ware::kMuskets}));
    s.push_back(producer(kSlotCannons, "cannons",
                         {kCannonFoundry, ware::kCannons, ware::kIron, 0, 0, D, 2, 0, false, kSlotSmelter},
                         {ware::kCannons}));
    s.push_back(counter(kSlotCastle, "castle"));
    s.push_back(counter(kSlotPirateHouses, "pirate_houses"));
    s.push_back(
        producer(kSlotGoldMine, "gold_mine", {kGoldMine, 0, 0, id::kGoldMine, 0, A, 1, 0, true}, {ware::kGold}));
    s.push_back(public_building(kSlotHochschule, "college", kHochschule, id::kHochschule, cf::kHochschule,
                                hs::kHochschule, 10, true));
    s.push_back(public_building(kSlotTheater, "theatre", kTheater, id::kTheater, cf::kTheater, hs::kTheater, 10, true));
    s.push_back(public_building(kSlotGalgen, "gallows", kGalgen, id::kGalgen, cf::kGalgen, hs::kGalgen, 10, false));
    s.push_back(producer(kSlotGoldsmith, "goldsmith", {kGoldsmith, ware::kJewellery, ware::kGold, 0, 0, A, 2, 0, false},
                         {ware::kJewellery}));
    s.push_back(
        public_building(kSlotBrunnen, "fire_brigade", kBrunnen, id::kBrunnen, cf::kBrunnen, hs::kBrunnen, 6, false));
    s.push_back(counter(kSlotDenkmal, "monument"));
    return s;
}

}  // namespace

const std::vector<ComputerSlot>& original_computer_slots() {
    static const std::vector<ComputerSlot> slots = make_original_slots();
    return slots;
}

const ComputerSlot* find_computer_slot(std::span<const ComputerSlot> slots, std::string_view name) noexcept {
    const auto it = std::ranges::find(slots, name, &ComputerSlot::name);
    return it == slots.end() ? nullptr : &*it;
}

const ComputerSlot* computer_slot(std::span<const ComputerSlot> slots, std::uint8_t number) noexcept {
    const auto it = std::ranges::find(slots, number, &ComputerSlot::number);
    return it == slots.end() ? nullptr : &*it;
}

}  // namespace o1602::sim
