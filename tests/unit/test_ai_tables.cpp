// SPDX-License-Identifier: GPL-3.0-or-later
// The AI's tables (ai_tables.hpp, MD-4): the producer table counts every producer in the slot the
// survey's former switch gave it, and every Need and public task names a slot of its own.

#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include "../../engine/sim/src/ai/ai_tables.hpp"
#include "../test_framework.hpp"

#include "o1602/sim/computer_slots.hpp"

namespace o1602::sim::ai {
namespace {

/// The survey's switch before the table (MD-4), kept as the reference.
std::optional<std::uint8_t> reference_slot(std::uint8_t w, std::uint8_t raw, std::uint32_t building_id) noexcept {
    switch (w) {
        case ware::kWood:
            return kSlotWoodcutter;
        case ware::kFood:
            if (raw == ware::kGame) {
                return kSlotHunter;
            }
            if (raw == ware::kFlour) {
                return kSlotBakery;
            }
            if (raw == ware::kFish) {
                return kSlotFisher;  // the original counts them as hunters too (S-098)
            }
            if (raw == ware::kMeat) {
                return kSlotButcher;
            }
            return std::nullopt;
        case ware::kGrain:
            return kSlotGrain;
        case ware::kFlour:
            return kSlotMill;
        case ware::kStone:
            return kSlotQuarry;
        case ware::kBricks:
            return kSlotStonemason;
        case ware::kWool:
            return raw == ware::kCottonField ? kSlotCotton : kSlotSheep;
        case ware::kCloth:
            return building_id == id::kWebstube ? kSlotWebstube : kSlotWeberei;
        case ware::kAlcohol:
            // The original counts every alcohol producer as a cocoa plantation too (S-098).
            return raw == ware::kWineField ? kSlotWine : kSlotRum;
        case ware::kSpices:
            return kSlotSpices;
        case ware::kCocoa:
            return kSlotCocoa;
        case ware::kTobacco:
            return kSlotTobacco;
        case ware::kTobaccoGoods:
            return kSlotTobaccoGoods;
        case ware::kSugar:
            return kSlotSugar;
        case ware::kMeat:
            return kSlotCattle;
        case ware::kOre:
            return kSlotIronMine;
        case ware::kGold:
            return kSlotGoldMine;
        case ware::kIron:
            return kSlotSmelter;
        case ware::kTools:
            return kSlotToolsmith;
        case ware::kClothes:
            return kSlotTailor;
        case ware::kSwords:
            return kSlotSwords;
        case ware::kMuskets:
            return kSlotMuskets;
        case ware::kCannons:
            return kSlotCannons;
        case ware::kJewellery:
            return kSlotGoldsmith;
        default:
            return std::nullopt;
    }
}

}  // namespace
}  // namespace o1602::sim::ai

using namespace o1602::sim::ai;
namespace id = o1602::sim::id;

TEST_CASE("ai tables: the producer table counts every ware, raw material and record as the former switch did") {
    for (const std::uint32_t building : {0U, id::kWebstube, id::kWeberei, id::kHunter, id::kOreMine, 1U}) {
        for (std::uint32_t w = 0; w < 256; ++w) {
            for (std::uint32_t raw = 0; raw < 256; ++raw) {
                const auto ww = static_cast<std::uint8_t>(w);
                const auto rr = static_cast<std::uint8_t>(raw);
                REQUIRE(producer_slot(ww, rr, building) == reference_slot(ww, rr, building));
            }
        }
    }
}

TEST_CASE("ai tables: Needs and public tasks name each slot once") {
    std::set<std::uint8_t> slots;
    std::set<std::string> names;
    std::size_t needs = 0;
    std::size_t publics = 0;
    for (const o1602::sim::ComputerSlot& c : o1602::sim::original_computer_slots()) {
        REQUIRE(slots.insert(c.number).second);
        REQUIRE(names.insert(c.name).second);
        REQUIRE(o1602::sim::computer_slot(o1602::sim::original_computer_slots(), c.number) == &c);
        REQUIRE(o1602::sim::find_computer_slot(o1602::sim::original_computer_slots(), c.name) == &c);
        if (const auto d = need_def(c.number)) {
            ++needs;
            REQUIRE(d->slot == c.number);
        }
        if (c.kind == o1602::sim::ComputerSlot::Kind::Public) {
            ++publics;
            const auto t = public_task(c.task);
            REQUIRE(t.has_value());
            REQUIRE(t->slot == c.number);
        }
    }
    // The original's 55 slots but the unused 0x2A, its 32 Needs and 10 public buildings.
    CHECK(slots.size() == kSlots - 1);
    CHECK(slots.count(0x2A) == 0);
    CHECK(needs == 32);
    CHECK(publics == 10);
    // A few Needs as the original's table had them.
    const auto bakery = need_def(kSlotBakery);
    REQUIRE(bakery.has_value());
    CHECK(bakery->task == kBakery);
    CHECK(bakery->ware == o1602::sim::ware::kFood);
    CHECK(bakery->raw == o1602::sim::ware::kFlour);
    CHECK(bakery->type == NeedType::B);
    CHECK(bakery->after == kSlotMill);
    const auto gold = need_def(kSlotGoldMine);
    REQUIRE(gold.has_value());
    CHECK(gold->fixed == id::kGoldMine);
    CHECK(gold->attempts == 1);
    CHECK(gold->from_start);
    CHECK(need_def(kSlotStoneTower)->type == NeedType::D);
    CHECK(!need_def(kSlotKontor).has_value());
    const auto church = public_task(kKirche);
    REQUIRE(church.has_value());
    CHECK(church->min_houses == 16);
    CHECK(!public_task(kGalgen)->roads_on_fail);
}

TEST_CASE("ai tables: the rules of the original's slots are fields of the slots") {
    using o1602::sim::ComputerSlot;
    const auto& slots = o1602::sim::original_computer_slots();
    const auto at = [&](std::uint8_t n) -> const ComputerSlot& { return *o1602::sim::computer_slot(slots, n); };
    // Wool and cloth from cotton where it grows fully, once the weaving mill is unlocked.
    CHECK(at(kSlotSheep).instead == kSlotCotton);
    CHECK(at(kSlotWebstube).instead == kSlotWeberei);
    CHECK(at(kSlotWebstube).instead_unlocked == kSlotWeberei);
    CHECK(at(kSlotWebstube).instead_crop == o1602::sim::ware::kCottonField);
    // One smelter unless 30 t of ore and three iron workshops.
    CHECK(at(kSlotSmelter).single_below_stock == 30);
    CHECK(at(kSlotSmelter).single_below_users == 3);
    std::size_t users = 0;
    for (const ComputerSlot& c : slots) {
        users += c.after == kSlotSmelter ? 1U : 0U;
    }
    CHECK(users == 4);
    // The mines, the goldsmith's gold, two stonemasons a quarry.
    CHECK(at(kSlotIronMine).deposit == o1602::sim::ware::kOre);
    CHECK(at(kSlotGoldMine).deposit == o1602::sim::ware::kGold);
    CHECK(at(kSlotGoldsmith).raw_stock == 1);
    CHECK(at(kSlotStonemason).process_max == 2);
    // The processing order: eight workshops, each rank once.
    std::set<std::uint8_t> ranks;
    for (const ComputerSlot& c : slots) {
        if (c.process_rank != 0) {
            REQUIRE(ranks.insert(c.process_rank).second);
            CHECK(c.process_from != o1602::sim::kNoProducer);
        }
    }
    CHECK(ranks.size() == 8);
    CHECK(at(kSlotMill).process_rank == 1);
    CHECK(!at(kSlotMill).process_round_up);
    CHECK(at(kSlotBakery).process_round_up);
    // The subsidy: four tons of ore for a mine, cocoa from 100 merchants.
    CHECK(at(kSlotIronMine).subsidy_tons == 4);
    CHECK(at(kSlotCocoa).subsidy_people == 100);
    CHECK(at(kSlotCotton).subsidy_ware == o1602::sim::ware::kWool);
}

TEST_CASE("ai tables: a city on a large island grows its orders with its free land") {
    CHECK(grown_count(500, 44) == 44);  // the original's islands: the original's orders
    CHECK(grown_count(kGrowFrom, 44) == 44);
    CHECK(grown_count(kGrowLand * 3 / 2, 44, kGrowLand) == 66);  // a thriving city on an original's island
    CHECK(grown_count(kGrowLand * 3, 44) == 132);
    CHECK(grown_count(kGrowLand * 5 / 2, 44) == 110);
    CHECK(grown_count(kGrowLand * 3, 1) == 3);
    CHECK(grown_count(kGrowLand * 3, 0) == 0);          // "while houses lack it" stays so
    CHECK(grown_count(kGrowLand * 100, 44) == 44 * 8);  // at most eight times as many
    CHECK(grown_count(kGrowLand * 100, 1000) == 1000);  // and no more than an order may ask for
}
