// SPDX-License-Identifier: GPL-3.0-or-later
//
// Numbers of the original's data that the engine relies on: ware numbers, HAUS Kind and
// HAUS_PRODTYP Kind values, figure numbers, the base prices and the buildings the computer players
// name by Id. They are defined here once instead of in every file that needs them, so that a later
// data loader for mods (ADR 0007) has one place to replace and a renumbered ware cannot be missed
// in one file and kept in another. The simulation re-exports them (sim/game_data.hpp); assets,
// render and the app use them from here.
//
// The names follow the symbols of haeuser.cod and figuren.cod (assets/cod_predefined.cpp).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace o1602::assets {

/// Warehouse stock is stored in 1/32 t (confirmed in the running game: 800 = 25 t).
inline constexpr std::uint32_t kStockUnitsPerTon = 32;
/// A supply level or satisfaction of a fully provided good (houses and consumption).
inline constexpr std::uint8_t kFullSupply = 128;

/// Ware numbers (HAUS Ware, Kontor stock, ship holds).
namespace ware {
inline constexpr std::uint8_t kNone = 0;  ///< NOWARE
inline constexpr std::uint8_t kAll = 1;   ///< ALLWARE: markets and Kontors hold every good
inline constexpr std::uint8_t kOre = 2;   ///< EISENERZ
inline constexpr std::uint8_t kGold = 3;
inline constexpr std::uint8_t kWool = 4;
inline constexpr std::uint8_t kSugar = 5;
inline constexpr std::uint8_t kTobacco = 6;
inline constexpr std::uint8_t kMeat = 7;
inline constexpr std::uint8_t kGrain = 8;
inline constexpr std::uint8_t kFlour = 9;
inline constexpr std::uint8_t kIron = 10;
inline constexpr std::uint8_t kSwords = 11;
inline constexpr std::uint8_t kMuskets = 12;
inline constexpr std::uint8_t kCannons = 13;
inline constexpr std::uint8_t kFood = 14;
inline constexpr std::uint8_t kTobaccoGoods = 15;
inline constexpr std::uint8_t kSpices = 16;
inline constexpr std::uint8_t kCocoa = 17;
inline constexpr std::uint8_t kAlcohol = 18;
inline constexpr std::uint8_t kCloth = 19;
inline constexpr std::uint8_t kClothes = 20;
inline constexpr std::uint8_t kJewellery = 21;
inline constexpr std::uint8_t kTools = 22;
inline constexpr std::uint8_t kWood = 23;
inline constexpr std::uint8_t kBricks = 24;
inline constexpr std::uint8_t kFirstSoldier = 25;  ///< wSOLDAT1: soldiers in a ship's hold ...
inline constexpr std::uint8_t kLastSoldier = 44;   ///< ... up to wPIONIER4
/// A unit in a hold is the ware of its figure plus this (SOLDAT1 = 1 is wSOLDAT1 = 25).
inline constexpr std::uint8_t kSoldierOffset = kFirstSoldier - 1;
inline constexpr std::uint8_t kCornField = 45;
inline constexpr std::uint8_t kTobaccoField = 46;
inline constexpr std::uint8_t kSpiceField = 47;
inline constexpr std::uint8_t kSugarField = 48;
inline constexpr std::uint8_t kCottonField = 49;
inline constexpr std::uint8_t kWineField = 50;
inline constexpr std::uint8_t kCocoaField = 51;
inline constexpr std::uint8_t kGrass = 52;
inline constexpr std::uint8_t kTree = 53;
inline constexpr std::uint8_t kStone = 54;        ///< STEINE: the quarry's rock
inline constexpr std::uint8_t kOreMountain = 55;  ///< ERZE: the ware of ore mountains
inline constexpr std::uint8_t kGame = 56;
inline constexpr std::uint8_t kFish = 57;
inline constexpr std::uint8_t kTreasure = 58;            ///< SCHATZ
inline constexpr std::uint8_t kFirstField = kCornField;  ///< what grows or stands on the map ...
inline constexpr std::uint8_t kLastField = kTreasure;    ///< ... up to the treasure

/// Goods a Kontor stores and ships trade: EISENERZ to ZIEGEL.
inline constexpr std::uint8_t kFirstTrade = kOre;
inline constexpr std::uint8_t kLastTrade = kBricks;
}  // namespace ware

/// What a ware number stands for (MD-3). The original lays the groups end to end; the tests below
/// ask this table, so a data set with another numbering changes it here and nowhere else.
enum class WareGroup : std::uint8_t {
    None,     ///< no ware (NOWARE, ALLWARE, unused numbers)
    Trade,    ///< stored by Kontors, traded by ships and the free trader
    Soldier,  ///< a unit in a ship's hold
    Field,    ///< what grows or stands on the map (fields, trees, rock, fish, game, treasure)
};

/// The group of every ware number.
inline constexpr std::array<WareGroup, 256> kWareGroups = [] {
    std::array<WareGroup, 256> groups{};
    for (std::size_t w = ware::kFirstTrade; w <= ware::kLastTrade; ++w) {
        groups[w] = WareGroup::Trade;
    }
    for (std::size_t w = ware::kFirstSoldier; w <= ware::kLastSoldier; ++w) {
        groups[w] = WareGroup::Soldier;
    }
    for (std::size_t w = ware::kFirstField; w <= ware::kLastField; ++w) {
        groups[w] = WareGroup::Field;
    }
    return groups;
}();

[[nodiscard]] constexpr WareGroup ware_group(std::uint32_t w) noexcept {
    return w < kWareGroups.size() ? kWareGroups[w] : WareGroup::None;
}

/// A good a Kontor stores and ships trade.
[[nodiscard]] constexpr bool is_trade_ware(std::uint32_t w) noexcept {
    return ware_group(w) == WareGroup::Trade;
}

/// Soldiers carried in a ship's hold (one slot per unit type and level).
[[nodiscard]] constexpr bool is_soldier_ware(std::uint32_t w) noexcept {
    return ware_group(w) == WareGroup::Soldier;
}
/// The ware of a unit in a hold or in a castle's list: SOLDAT1 is wSOLDAT1 (MD-6).
[[nodiscard]] constexpr std::uint8_t unit_ware(std::uint32_t figure) noexcept {
    return static_cast<std::uint8_t>(figure + ware::kSoldierOffset);
}
/// The figure of a unit ware (is_soldier_ware).
[[nodiscard]] constexpr std::uint16_t unit_of_ware(std::uint32_t w) noexcept {
    return static_cast<std::uint16_t>(w - ware::kSoldierOffset);
}

/// Base prices of the wares per ton (the original's table at 0x0049AE50): the default prices of a
/// Kontor (sell 50 %, buy 75 %) and what the free trader pays. Indexed by ware number.
inline constexpr std::array<std::uint16_t, ware::kLastTrade + 1> kBasePrices = {
    0, 0, 90, 700, 24, 30, 35, 10, 5, 10, 130, 180, 250, 500, 26, 100, 60, 50, 80, 50, 200, 900, 140, 30, 45};

/// Base price of a ware per ton (0 for wares that are not traded).
[[nodiscard]] constexpr std::uint16_t base_price(std::uint8_t w) noexcept {
    return w < kBasePrices.size() ? kBasePrices[w] : 0;
}

/// Resident classes the rules know the needs of (pioneers to aristocrats, houses.cpp); a
/// haeuser.cod with more is refused at loading (MD-2).
inline constexpr std::size_t kResidentClasses = 5;

/// Unlock levels: INFRA numbers of haeuser.cod (BAUINFRA, Bauinfra). Player::infra_mask holds
/// level n in bit n - 1 (MD-8).
namespace infra {
inline constexpr std::uint8_t kNone = 0;        ///< INFRA_NIX: always allowed
inline constexpr std::uint8_t kCastle = 11;     ///< INFRA_SCHLOSS: one per player
inline constexpr std::uint8_t kCathedral = 12;  ///< INFRA_KATHETRALE: one per player
inline constexpr std::uint8_t kArch = 13;       ///< INFRA_TRIUMPH: by allowance
inline constexpr std::uint8_t kMonument = 14;   ///< INFRA_DENKMAL: by allowance
inline constexpr std::uint8_t kLevel1A = 15;    ///< INFRA_STUFE_1A
inline constexpr std::uint8_t kLevel2A = 16;
inline constexpr std::uint8_t kLevel2C = 18;  ///< the computer players start selling
inline constexpr std::uint8_t kLevel2D = 19;
inline constexpr std::uint8_t kLevel3C = 25;
inline constexpr std::uint8_t kLevel3D = 26;
inline constexpr std::uint8_t kLevel3F = 28;  ///< computers mine deep veins below 120 t
inline constexpr std::uint8_t kLevel4A = 29;
inline constexpr std::uint8_t kLevel4B = 30;
inline constexpr std::uint8_t kLevel5A = 31;
inline constexpr std::uint8_t kLevel5B = 32;  ///< INFRA_STUFE_5B, the last
/// The highest level Player::infra_mask holds; a haeuser.cod with higher ones is refused.
inline constexpr std::uint8_t kMaxLevel = 32;

/// The level a ware is wanted from by the next class (house panel; exe constants): tobacco goods,
/// spices and alcohol from 2A, cocoa and clothes from 3D, jewellery from 4A; others always.
[[nodiscard]] constexpr std::uint8_t ware_level(std::uint8_t w) noexcept {
    switch (w) {
        case ware::kTobaccoGoods:
        case ware::kSpices:
        case ware::kAlcohol:
            return kLevel2A;
        case ware::kCocoa:
        case ware::kClothes:
            return kLevel3D;
        case ware::kJewellery:
            return kLevel4A;
        default:
            return kNone;
    }
}

/// A step of the levels that the pirates' fleet (Pirates_MaxShips 0x0040F310) and the computer
/// players' war fleets grow by: from `level` on, `ships`.
struct ShipStep {
    std::uint8_t level;
    std::uint8_t ships;
};
inline constexpr std::array<ShipStep, 4> kShipSteps{{{kLevel5A, 4}, {kLevel4B, 3}, {kLevel3C, 2}, {kLevel2D, 1}}};
}  // namespace infra

/// The score of the end screen and the monument allowance (docs/re/missions.md).
namespace scoring {
/// A straight segment of a score curve: entries a to b run from `from` to `to` (0x00474F90).
struct Segment {
    std::int32_t a;
    std::int32_t b;
    std::int32_t from;
    std::int32_t to;
};

/// Last index of the city curve (20,000 Einwohner; the index is a city's Einwohner / 20).
inline constexpr std::size_t kCityCurveLast = 1000;
/// The city curve: points per 64 Einwohner by the size of the city (a byte table).
inline constexpr std::array<Segment, 8> kCityCurve{{{0, 10, 12, 12},
                                                    {10, 25, 12, 19},
                                                    {25, 50, 19, 32},
                                                    {50, 100, 32, 51},
                                                    {100, 200, 51, 76},
                                                    {200, 400, 76, 102},
                                                    {400, 500, 102, 160},
                                                    {500, 1000, 160, 254}}};
/// Last index of the fight curve.
inline constexpr std::size_t kFightCurveLast = 200;
/// The fight curve: points of fights by the number of wins (entry 0 is always 0).
inline constexpr std::array<Segment, 8> kFightCurve{{{0, 1, 0, 0},
                                                     {1, 4, 100, 400},
                                                     {4, 10, 400, 1000},
                                                     {10, 20, 1000, 2000},
                                                     {20, 40, 2000, 3000},
                                                     {40, 80, 3000, 5000},
                                                     {80, 140, 5000, 7000},
                                                     {140, 200, 7000, 10000}}};

/// Points of money: the first step whose limit the balance reaches, highest first.
struct MoneyStep {
    std::int64_t limit;
    std::int64_t points;
};
inline constexpr std::array<MoneyStep, 7> kMoneySteps{{{10'000'000, 5000},
                                                       {5'000'000, 4000},
                                                       {1'000'000, 3000},
                                                       {500'000, 2500},
                                                       {250'000, 2000},
                                                       {100'000, 1500},
                                                       {50'000, 1000}}};

/// Points of monuments: allowances times this, divided by the game time in 1/10 s ...
inline constexpr std::int64_t kMonumentFactor = 90'000'000;
/// ... counted only after this game time in 1/10 s (the tenth minute).
inline constexpr std::int64_t kMonumentStart = 6000;
/// Points per opponent defeated (triumphal arch allowance).
inline constexpr std::int64_t kDefeatPoints = 1500;

/// A growing city earns its owner a monument allowance every this many milliseconds ...
inline constexpr std::uint64_t kMonumentIntervalMs = 30ULL * 60 * 1000;
/// ... when it has this many inhabitants ...
inline constexpr std::uint32_t kMonumentMinInhabitants = 200;
/// ... of this resident class and higher (2: Bürger).
inline constexpr std::size_t kMonumentFirstClass = 2;
}  // namespace scoring

/// HAUS Kind values (record +0x04).
namespace kind {
inline constexpr std::int32_t kRoad = 1;
inline constexpr std::int32_t kGate = 3;
inline constexpr std::int32_t kWall = 4;
inline constexpr std::int32_t kWallBeach = 5;
inline constexpr std::int32_t kTower = 6;
inline constexpr std::int32_t kTowerBeach = 7;
inline constexpr std::int32_t kForest = 10;
inline constexpr std::int32_t kGround = 11;
inline constexpr std::int32_t kRuin = 12;
inline constexpr std::int32_t kSquare = 13;
inline constexpr std::int32_t kBuilding = 14;
inline constexpr std::int32_t kRock = 15;
inline constexpr std::int32_t kRiver = 16;
inline constexpr std::int32_t kRiverCorner = 17;
inline constexpr std::int32_t kBridge = 18;
inline constexpr std::int32_t kSea = 19;
inline constexpr std::int32_t kSurf = 20;
inline constexpr std::int32_t kSurfCorner = 21;
inline constexpr std::int32_t kRiverMouth = 22;  ///< MUENDUNG
inline constexpr std::int32_t kBeach = 23;
inline constexpr std::int32_t kBeachMouth = 24;
inline constexpr std::int32_t kBeachOuter = 25;
inline constexpr std::int32_t kBeachInner = 26;
inline constexpr std::int32_t kBeachVariant = 27;
inline constexpr std::int32_t kBeachHouse = 28;
inline constexpr std::int32_t kBeachRuin = 29;
inline constexpr std::int32_t kPier = 30;
inline constexpr std::int32_t kSlope = 31;
inline constexpr std::int32_t kSlopeCorner = 32;
inline constexpr std::int32_t kSlopeSpring = 33;
inline constexpr std::int32_t kMine = 34;
inline constexpr std::int32_t kHq = 35;
inline constexpr std::int32_t kHarbour = 36;
inline constexpr std::int32_t kMill = 37;
}  // namespace kind

/// HAUS_PRODTYP Kind values (what a building does).
namespace prod {
inline constexpr std::int32_t kWorkshop = 1;      ///< HANDWERK
inline constexpr std::int32_t kFarm = 2;          ///< PLANTAGE
inline constexpr std::int32_t kMine = 3;          ///< BERGWERK
inline constexpr std::int32_t kPasture = 4;       ///< WEIDETIER
inline constexpr std::int32_t kHunter = 5;        ///< JAGDHAUS
inline constexpr std::int32_t kFisher = 6;        ///< FISCHEREI
inline constexpr std::int32_t kMarket = 7;        ///< MARKT
inline constexpr std::int32_t kKontor = 8;        ///< KONTOR
inline constexpr std::int32_t kRipe = 9;          ///< ROHSTOFF: ripe, can be harvested
inline constexpr std::int32_t kGrowing = 10;      ///< ROHSTWACHS: growing (or withered)
inline constexpr std::int32_t kQuarry = 11;       ///< STEINBRUCH
inline constexpr std::int32_t kOreMountain = 12;  ///< ROHSTERZ
inline constexpr std::int32_t kHouse = 13;        ///< WOHNUNG
inline constexpr std::int32_t kShipyard = 14;     ///< WERFT
inline constexpr std::int32_t kMilitary = 15;     ///< MILITAR
inline constexpr std::int32_t kTower = 16;        ///< WACHTURM
inline constexpr std::int32_t kWirt = 17;
inline constexpr std::int32_t kKapelle = 18;
inline constexpr std::int32_t kKirche = 19;
inline constexpr std::int32_t kBadehaus = 20;
inline constexpr std::int32_t kTheater = 21;
inline constexpr std::int32_t kKlinik = 22;  ///< the doctor
inline constexpr std::int32_t kSchule = 23;
inline constexpr std::int32_t kHochschule = 24;
inline constexpr std::int32_t kGalgen = 25;
inline constexpr std::int32_t kBrunnen = 26;  ///< the fire brigade
inline constexpr std::int32_t kSchloss = 27;
inline constexpr std::int32_t kDenkmal = 28;
inline constexpr std::int32_t kTriumph = 29;
inline constexpr std::int32_t kHaupt = 30;
inline constexpr std::int32_t kPirateHouse = 31;  ///< PIRATWOHN
inline constexpr std::int32_t kWall = 32;         ///< pMAUER
/// The public buildings, WIRT to SCHLOSS.
inline constexpr std::int32_t kPublicFirst = kWirt;
inline constexpr std::int32_t kPublicLast = kSchloss;
}  // namespace prod

/// Figure numbers (figuren.cod).
/// The climates of islands (island header +0x64), each with its template folders and crops.
namespace climate {
inline constexpr std::uint8_t kNorth = 0;  ///< Nord: grain, tobacco, sugar cane, wine ...
inline constexpr std::uint8_t kSouth = 1;  ///< Sued: cotton, spices, cocoa ...
inline constexpr std::uint8_t kCount = 2;
}  // namespace climate

namespace figure {
/// The families of land units, each with four levels: SOLDAT1, KAVALERIE1, MUSKETIER1, KANONIER1.
inline constexpr std::array<std::uint32_t, 4> kUnitFamilies{1, 5, 9, 13};
/// Ships the shipyards build, by type: HANDEL1, HANDEL2, KRIEG1, KRIEG2 (each followed by its
/// wreck).
inline constexpr std::array<std::uint16_t, 4> kShips{21, 23, 25, 27};
/// The colours a unit family has figures in (the original's four players; the others use the
/// last, sim::four_colour_index).
inline constexpr std::int32_t kUnitColours = 4;

/// The figure of a unit of `family` (an index of kUnitFamilies) in `colour` (0..kUnitColours-1),
/// 0 if there is none (MD-6).
[[nodiscard]] constexpr std::uint16_t unit_figure(std::size_t family, std::int32_t colour) noexcept {
    return family < kUnitFamilies.size() && colour >= 0 && colour < kUnitColours
               ? static_cast<std::uint16_t>(kUnitFamilies[family] + static_cast<std::uint32_t>(colour))
               : std::uint16_t{0};
}
/// The ship a shipyard builds of `type`, 0 for a type that does not exist (MD-6).
[[nodiscard]] constexpr std::uint16_t ship_figure(std::size_t type) noexcept {
    return type < kShips.size() ? kShips[type] : std::uint16_t{0};
}
inline constexpr std::uint16_t kTrader = 29;      ///< HANDLER: the free trader's ship
inline constexpr std::uint16_t kPirate = 31;      ///< PIRAT
inline constexpr std::uint16_t kTradeCart = 37;   ///< TRADER1: a player's trade cart on land
inline constexpr std::uint16_t kSinking = 45;     ///< UNTERGANG: the splash of a sinking ship
inline constexpr std::uint16_t kIdle = 46;        ///< NOAKTIV: status icon of production buildings
inline constexpr std::uint16_t kMarketFire = 47;  ///< BRANDMARKT: fire on a market
inline constexpr std::uint16_t kReaper = 56;      ///< SENSENMANN: plague and fire on houses
inline constexpr std::uint16_t kShowOre = 57;     ///< SHOWEISEN: the ore deposit symbol
inline constexpr std::uint16_t kShowGold = 58;    ///< SHOWGOLD: the gold vein symbol
inline constexpr std::uint16_t kKontorFlag = 60;  ///< FAHNEKONTOR1: the flag on the first Kontor
inline constexpr std::uint16_t kShipSunk = 67;    ///< SHIPSUNK: the wreckage of a sunk ship
inline constexpr std::uint16_t kVolcano = 68;     ///< VULKAN: the fire over the mountain
inline constexpr std::uint16_t kLava = 69;        ///< VULKANFIRE1: a lump of lava
inline constexpr std::uint16_t kJuggler = 70;     ///< GAUKLER1
inline constexpr std::uint16_t kCarrier = 80;     ///< TRAEGER: brings the second raw material
inline constexpr std::uint16_t kDeer = 89;        ///< HIRSCH: the northern islands' game
inline constexpr std::uint16_t kAntelope = 91;    ///< ANTILOPE: the southern islands' game
inline constexpr std::uint16_t kRobber = 92;      ///< RAEUBER
/// The townsfolk, ADELWEIBL to PILGER (93 to 100).
inline constexpr std::uint16_t kNoblewoman = 93;  ///< ADELWEIBL
inline constexpr std::uint16_t kNobleman = 94;    ///< ADEL
inline constexpr std::uint16_t kOldMan = 95;      ///< ALTER
inline constexpr std::uint16_t kWoman = 96;       ///< FRAU
inline constexpr std::uint16_t kPasserby = 97;    ///< PASSANT
inline constexpr std::uint16_t kVeteran = 98;     ///< VETERAN
inline constexpr std::uint16_t kChild = 99;       ///< KIND
inline constexpr std::uint16_t kPilgrim = 100;    ///< PILGER
inline constexpr std::uint16_t kWhale = 101;      ///< WAL
inline constexpr std::uint16_t kDolphin = 102;    ///< DELPHIN
inline constexpr std::uint16_t kKraken = 103;     ///< KRAKE
/// Ship flags by player colour (table at 0x00498F2C): FAHNE1..4, FAHNEPIRAT.
inline constexpr std::array<std::uint32_t, 5> kFlags{106, 107, 108, 109, 110};
inline constexpr std::uint16_t kWhiteFlag = 111;  ///< FAHNEWEISS
}  // namespace figure

/// FIGURE Kind values of the land units (figuren.cod FIGTYP_*).
namespace figtype {
inline constexpr std::int32_t kSwordsman = 1;  ///< FIGTYP_SCHWERT
inline constexpr std::int32_t kRider = 2;      ///< FIGTYP_KAVALERIE
inline constexpr std::int32_t kMusketeer = 3;  ///< FIGTYP_MUSKETIER
inline constexpr std::int32_t kGunner = 4;     ///< FIGTYP_KANONIER
}  // namespace figtype

/// Buildings of the base game named by Id, mostly by the computer players.
namespace id {
inline constexpr std::uint32_t kRoadStone = 20401;
inline constexpr std::uint32_t kRoadDirt = 20411;
inline constexpr std::uint32_t kSquare = 20421;
inline constexpr std::uint32_t kHouse = 20605;
inline constexpr std::uint32_t kKapelle = 20803;
inline constexpr std::uint32_t kKirche = 20805;
inline constexpr std::uint32_t kWirt = 20807;
inline constexpr std::uint32_t kMarket = 20809;
inline constexpr std::uint32_t kBrunnen = 20811;
inline constexpr std::uint32_t kBadehaus = 20813;
inline constexpr std::uint32_t kTheater = 20815;
inline constexpr std::uint32_t kKlinik = 20817;
inline constexpr std::uint32_t kHochschule = 20819;
inline constexpr std::uint32_t kSchule = 20821;
inline constexpr std::uint32_t kGalgen = 20841;
inline constexpr std::uint32_t kDenkmal = 20851;
inline constexpr std::uint32_t kGate = 20304;
inline constexpr std::uint32_t kStoneTower = 20305;
inline constexpr std::uint32_t kStoneTowerAlt = 20306;  ///< the record after it, counted as the same tower
inline constexpr std::uint32_t kCastle1 = 20311;
inline constexpr std::uint32_t kCastle2 = 20313;
inline constexpr std::uint32_t kCastle3 = 20315;
inline constexpr std::uint32_t kWebstube = 20507;
inline constexpr std::uint32_t kWeberei = 20531;
inline constexpr std::uint32_t kGoldsmith = 20533;  ///< also marks the treasure entries of a world plan
inline constexpr std::uint32_t kWoodTower = 21405;
inline constexpr std::uint32_t kWoodTowerAlt = 21406;  ///< the record after it, counted as the same tower
inline constexpr std::uint32_t kBridgeWood = 21601;
inline constexpr std::uint32_t kBridgeStone = 21603;
inline constexpr std::uint32_t kPier = 21073;
inline constexpr std::uint32_t kHunter = 22003;
inline constexpr std::uint32_t kKontor1 = 22101;
/// The Kontors of the four levels, KONTOR1 to KONTOR4 (MD-4).
inline constexpr std::array<std::uint32_t, 4> kKontors{kKontor1, 22103, 22105, 22107};
/// The Kontor of `level`, held between 1 and 4.
[[nodiscard]] constexpr std::uint32_t kontor(std::uint32_t level) noexcept {
    return kKontors[level < 1 ? 0 : (level > kKontors.size() ? kKontors.size() - 1 : level - 1)];
}
/// The level of a Kontor's record, 0 for another record: a record between two levels' counts as
/// the lower one, as the original's range check from KONTOR1 to KONTOR4.
[[nodiscard]] constexpr std::uint32_t kontor_level(std::uint32_t building) noexcept {
    if (building < kKontors.front() || building > kKontors.back()) {
        return 0;
    }
    std::uint32_t level = 0;
    for (const std::uint32_t k : kKontors) {
        level += building >= k ? 1U : 0U;
    }
    return level;
}
inline constexpr std::uint32_t kShipyardBig = 22111;
inline constexpr std::uint32_t kShipyardSmall = 22115;
inline constexpr std::uint32_t kPirateKontor = 22221;
inline constexpr std::uint32_t kPirateHouse1 = 22222;
inline constexpr std::uint32_t kPirateHouse2 = 22223;
inline constexpr std::uint32_t kPirateHouse3 = 22224;
inline constexpr std::uint32_t kPirateTower = 22241;
inline constexpr std::uint32_t kPirateTowerAlt = 22242;  ///< the record after it, counted as the same tower
inline constexpr std::uint32_t kOreMine = 22401;
inline constexpr std::uint32_t kOreMineDeep = 22403;
inline constexpr std::uint32_t kGoldMine = 22405;
inline constexpr std::uint32_t kQuarry = 22407;
inline constexpr std::uint32_t kWall = 22501;
inline constexpr std::uint32_t kBakery = 20503;  ///< IDHANDW+2
inline constexpr std::uint32_t kForester = 22001;
inline constexpr std::uint32_t kFisher = 21075;
inline constexpr std::uint32_t kSheepFarm = 21515;
inline constexpr std::uint32_t kBigOreMountain = 21108;  ///< IDFELS+7: the mountain a volcano is
inline constexpr std::uint32_t kRuin = 22601;            ///< IDRUINE: the first ruin record
inline constexpr std::uint32_t kMeadow = 22722;          ///< IDWEIDE+21: what demolished forest becomes
/// The ruin slots (Ruinenr) of the original (table 0x00619B20, filled at 0x00462F6A): land and
/// beach ruins of wooden houses, brick houses, plantations, field paths, roads and mines (IDRUINE
/// + offset), the destroyed Kontors I-IV ("Kontor zerstört"), the destroyed native buildings and
/// the destroyed market.
inline constexpr std::array<std::uint32_t, 15> kRuins{kRuin,      kRuin + 9,  kRuin + 10, kRuin + 19, kRuin + 20,
                                                      kRuin + 30, kRuin + 35, kRuin + 40, 22121,      22122,
                                                      22123,      22124,      22210,      22240,      20823};
/// The ripe fields the original's plans place (Endlosspiel<D><V>.szs), in their order.
inline constexpr std::array<std::uint32_t, 6> kPlanCrops{21336, 21342, 21340, 21332, 21344, 21338};
/// The list the original keeps after the build menu's (0x00498610), named from Text.cod
/// [LANDSCHAFT]: the natives' and pirates' buildings and a few others.
inline constexpr std::array<std::uint32_t, 17> kLandscape{22201, 22202, 22203, 22204, 22205, 22231, 22232, 22233, 22234,
                                                          22235, 22211, 22221, 22222, 22223, 22241, 21304, 22802};
/// The records a game cannot run without (BuildingTable::missing): demolition, volcanoes and ruins
/// place them.
inline constexpr std::array<std::uint32_t, 3> kRequired{kMeadow, kBigOreMountain, kRuin};
}  // namespace id

}  // namespace o1602::assets
