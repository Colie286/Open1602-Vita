// SPDX-License-Identifier: GPL-3.0-or-later
// The production chains of the packages' goods for the computer players (ai_chains.hpp, MD-4):
// derived from the building table and the classes' needs.

#include <cstdint>
#include <string>
#include <vector>

#include "../../engine/sim/src/ai/ai_chains.hpp"
#include "../../engine/sim/src/ai/ai_tables.hpp"
#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"

using namespace o1602;
using namespace o1602::sim::ai;

namespace {

using Counts = std::vector<std::uint32_t>;

/// A record of a producer: `kind` of HAUS_PRODTYP, `ware` from `raw`, one ton in 20 s from one ton.
std::string producer(std::uint32_t number, std::uint32_t id, std::uint32_t kind, std::uint32_t ware, std::uint32_t raw,
                     std::uint32_t size = 2) {
    return "  @Nummer: " + std::to_string(number) + "\n  Id: " + std::to_string(id) +
           "\n  Kind: 14\n  Gfx: " + std::to_string(number) + "\n  Size: " + std::to_string(size) + ", " +
           std::to_string(size) + "\n  Objekt: HAUS_PRODTYP\n    Kind: " + std::to_string(kind) +
           "\n    Ware: " + std::to_string(ware) + "\n    Rohstoff: " + std::to_string(raw) +
           "\n    Prodmenge: 1\n    Rohmenge: 1\n    Interval: 20\n  EndObj\n";
}

assets::BuildingTable table(const std::string& records, const std::vector<std::uint8_t>& needed) {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse("Objekt: HAUS\n" + records + "EndObj\n", types);
    auto t = assets::BuildingTable::from_cod(doc);
    REQUIRE(t.ok());
    assets::BuildingTable buildings = std::move(t).value();
    assets::ResidentClass c;
    for (const std::uint8_t w : needed) {
        c.needs.push_back({w, 100});
    }
    buildings.set_resident_classes({c});
    return buildings;
}

}  // namespace

TEST_CASE("ai chains: a needed good of a package gets its producer, its raw material the producer's") {
    // Pepper sauce (60) from pepper (59) from pepper fields (tobacco fields, 46); cloth of a
    // package (61) from the original's tools (22).
    const auto buildings =
        table(producer(0, 21000, 1, 60, 59) + producer(1, 21001, 2, 59, 46) + producer(2, 21002, 1, 61, 22), {60, 61});
    const std::vector<PackageNeed> needs = package_needs(buildings);
    REQUIRE(needs.size() == 3);
    CHECK(needs[0].ware == 60);
    CHECK(needs[0].building == 21000);
    CHECK(needs[0].slot == kSlots);
    CHECK(needs[0].task == kWeaving);
    CHECK(needs[0].raw_slot == needs[2].slot);  // pepper, found after the cloth
    CHECK(needs[0].output == 96);               // 1 t in 20 s: 3 t a minute
    CHECK(needs[0].input == 96);
    CHECK(needs[1].ware == 61);
    CHECK(needs[1].raw_slot == kSlotToolsmith);
    CHECK(needs[2].ware == 59);
    CHECK(needs[2].task == kTobacco);
    CHECK(needs[2].field == 46);
    CHECK(needs[2].fertile == 46);
    CHECK(needs[2].raw_slot == kNoSlot);
}

TEST_CASE("ai chains: the original's goods, needless goods and producers no task places get no Need") {
    // Tools (22) are the original's; 62 nobody needs; 63's producer is a mine; 64's farm is 3 x 3;
    // 65's workshop uses a field.
    const auto buildings =
        table(producer(0, 21000, 1, 22, 10) + producer(1, 21001, 1, 62, 0) + producer(2, 21002, 3, 63, 0) +
                  producer(3, 21003, 2, 64, 46, 3) + producer(4, 21004, 1, 65, 46),
              {22, 63, 64, 65});
    CHECK(package_needs(buildings).empty());
}

TEST_CASE("ai chains: a chain that comes round to itself does not wait for itself: its first Need waits for none") {
    const auto buildings = table(producer(0, 21000, 1, 60, 59) + producer(1, 21001, 1, 59, 60), {60});
    const std::vector<PackageNeed> needs = package_needs(buildings);
    REQUIRE(needs.size() == 2);
    CHECK(needs[0].raw_slot == kNoSlot);
    CHECK(needs[1].raw_slot == needs[0].slot);
}

TEST_CASE("ai chains: a city wants producers for its inhabitants and for the producers that use the good") {
    const auto buildings =
        table(producer(0, 21000, 1, 60, 59) + producer(1, 21001, 2, 59, 46) + producer(2, 21002, 1, 61, 59), {60, 61});
    const std::vector<PackageNeed> needs = package_needs(buildings);
    REQUIRE(needs.size() == 3);
    REQUIRE(needs[2].ware == 59);
    // 2.5 t of sauce a minute: one producer makes 3 t, so one; nothing of the cloth.
    CHECK((package_wanted(needs, {80, 0, 0}) == Counts{1, 0, 1}));
    // 9 t of sauce and 5 t of cloth: three and two producers, which use 15 t of pepper, five farms.
    CHECK((package_wanted(needs, {288, 160, 0}) == Counts{3, 2, 5}));
    // At most eight of each.
    CHECK((package_wanted(needs, {10000, 0, 0}) == Counts{8, 0, 8}));
    CHECK((package_wanted(needs, {}) == Counts{0, 0, 0}));
}

TEST_CASE("ai chains: the original's Need of a raw material") {
    CHECK(original_need_slot(sim::ware::kTools) == kSlotToolsmith);
    CHECK(original_need_slot(sim::ware::kOre) == kSlotIronMine);
    CHECK(original_need_slot(sim::ware::kGold) == kSlotGoldMine);
    CHECK(original_need_slot(sim::ware::kWool) == kSlotSheep);
    CHECK(original_need_slot(sim::ware::kIron) == kSlotSmelter);
    CHECK(original_need_slot(sim::ware::kCannons) == kSlotCannons);
    CHECK(original_need_slot(sim::ware::kBricks) == kSlotStonemason);
    CHECK(original_need_slot(sim::ware::kFood) == kSlotBakery);
    CHECK(original_need_slot(sim::ware::kAlcohol) == kSlotWine);
    CHECK(original_need_slot(60) == kNoSlot);
}
