// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/computer_slots.hpp"

#include <algorithm>
#include <charconv>
#include <optional>

#include "ai/ai_state.hpp"

#include "o1602/assets/original_names.hpp"
#include "o1602/core/data_file.hpp"
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
    // The rules that name these slots.
    const auto at = [&](std::uint8_t number) -> ComputerSlot& {
        return *std::ranges::find(s, number, &ComputerSlot::number);
    };
    const auto swap = [&](std::uint8_t slot, std::uint8_t instead) {
        at(slot).instead = instead;
        at(slot).instead_crop = ware::kCottonField;  // wool and cloth from cotton where it grows fully
        at(slot).instead_unlocked = kSlotWeberei;
    };
    swap(kSlotSheep, kSlotCotton);
    swap(kSlotWebstube, kSlotWeberei);
    at(kSlotSmelter).single_below_stock = 30;  // more than one with ore in stock and three iron workshops
    at(kSlotSmelter).single_below_users = 3;
    at(kSlotIronMine).deposit = ware::kOre;
    at(kSlotGoldMine).deposit = ware::kGold;
    at(kSlotGoldsmith).raw_stock = 1;
    const auto process = [&](std::uint8_t slot, std::uint8_t from, std::uint8_t per, bool up, std::uint8_t rank) {
        at(slot).process_from = from;
        at(slot).process_per = per;
        at(slot).process_round_up = up;
        at(slot).process_rank = rank;
    };
    process(kSlotMill, kSlotGrain, 2, false, 1);
    process(kSlotBakery, kSlotMill, 2, true, 2);
    process(kSlotWebstube, kSlotSheep, 2, false, 3);
    process(kSlotWeberei, kSlotCotton, 2, true, 4);
    process(kSlotButcher, kSlotCattle, 2, true, 5);
    process(kSlotTobaccoGoods, kSlotTobacco, 2, true, 6);
    process(kSlotRum, kSlotSugar, 2, true, 7);
    process(kSlotStonemason, kSlotQuarry, 1, false, 8);
    at(kSlotStonemason).process_max = 2;  // two stonemasons at most for each quarry
    const auto subsidy = [&](std::uint8_t slot, std::uint8_t w, std::uint8_t tons, std::uint8_t above) {
        at(slot).subsidy_ware = w;
        at(slot).subsidy_tons = tons;
        at(slot).subsidy_above = above;
    };
    subsidy(kSlotSpices, ware::kSpices, 1, 3);
    subsidy(kSlotTobacco, ware::kTobacco, 1, 3);
    subsidy(kSlotWine, ware::kAlcohol, 1, 3);
    subsidy(kSlotSugar, ware::kSugar, 1, 3);
    subsidy(kSlotCotton, ware::kWool, 1, 3);
    subsidy(kSlotIronMine, ware::kOre, 4, 0);
    subsidy(kSlotCocoa, ware::kCocoa, 1, 3);
    at(kSlotCocoa).subsidy_class = 3;
    at(kSlotCocoa).subsidy_people = 100;
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

namespace {

/// Whether slot `n` of `slots` may be named by a rule of another: kNoProducer, or a producer or a
/// tower that is not `self`.
bool producer_ref(const std::vector<ComputerSlot>& slots, std::uint8_t n, std::uint8_t self) noexcept {
    if (n == kNoProducer) {
        return true;
    }
    const ComputerSlot* s = computer_slot(slots, n);
    return n != self && s != nullptr && (s->kind == Kind::Producer || s->kind == Kind::Tower);
}

/// The slot `original` as removed.
ComputerSlot removed_slot(const ComputerSlot& original) {
    ComputerSlot s = counter(original.number, original.name);
    s.removed = true;
    return s;
}

/// Whether `s` keeps every field of `o` a package cannot change in slots.toml.
bool same_fixed(const ComputerSlot& s, const ComputerSlot& o) noexcept {
    return s.kind == o.kind && s.task == o.task && s.building == o.building && s.ware == o.ware && s.raw == o.raw &&
           s.field == o.field && s.wait == o.wait && s.attempts == o.attempts && s.fertile == o.fertile &&
           s.from_start == o.from_start && (s.after == o.after || s.after == kNoProducer) && s.deposit == o.deposit &&
           s.count_ware == o.count_ware && s.count_raw == o.count_raw && s.count_building == o.count_building &&
           s.flag == o.flag && s.service == o.service;
}

/// Whether the counter of food or alcohol may count goods.
bool counts_goods(std::uint8_t number) noexcept {
    return number == ai::kSlotFood || number == ai::kSlotAlcohol;
}

}  // namespace

bool valid(const std::vector<ComputerSlot>& slots) noexcept {
    if (slots.empty()) {
        return true;
    }
    const std::vector<ComputerSlot>& original = original_computer_slots();
    if (slots.size() != original.size()) {
        return false;
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const ComputerSlot& s = slots[i];
        const ComputerSlot& o = original[i];
        if (s.number != o.number || s.name != o.name) {
            return false;
        }
        if (s.removed) {
            if (s != removed_slot(o) || (o.kind != Kind::Producer && o.kind != Kind::Tower && o.kind != Kind::Public)) {
                return false;
            }
            continue;
        }
        const bool instead_ok = s.instead == kNoProducer
                                    ? s.instead_crop == 0 && s.instead_unlocked == kNoProducer
                                    : s.instead_crop != 0 && producer_ref(slots, s.instead, s.number) &&
                                          producer_ref(slots, s.instead_unlocked, kNoProducer);
        const bool process_ok = s.process_rank == 0 || (s.process_from != kNoProducer && s.process_per != 0);
        const bool subsidy_ok = s.subsidy_tons == 0 || assets::is_good_number(s.subsidy_ware);
        const bool goods_ok =
            s.counts_goods.size() <= kMaxSlotGoods && (s.counts_goods.empty() || counts_goods(s.number)) &&
            std::ranges::all_of(s.counts_goods, [](std::uint8_t w) { return assets::is_good_number(w); });
        if (!same_fixed(s, o) || !instead_ok || !producer_ref(slots, s.process_from, s.number) || !process_ok ||
            !producer_ref(slots, s.after, s.number) || !subsidy_ok || s.subsidy_class >= 16 || !goods_ok ||
            s.min_houses < 0 || s.min_houses > kMaxSlotHouses ||
            (s.kind != Kind::Public && s.min_houses != o.min_houses) ||
            (s.kind != Kind::Public && s.roads_on_fail != o.roads_on_fail)) {
            return false;
        }
    }
    return true;
}

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + ":" + std::to_string(line) + ": ";
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

/// The crops a swap may name: the field that must grow fully.
constexpr std::array<std::pair<std::string_view, std::uint8_t>, 7> kCrops = {{
    {"grain", ware::kCornField},
    {"tobacco", ware::kTobaccoField},
    {"spices", ware::kSpiceField},
    {"sugar", ware::kSugarField},
    {"cotton", ware::kCottonField},
    {"wine", ware::kWineField},
    {"cocoa", ware::kCocoaField},
}};

/// Removes slot `at` of `slots` and the rules of others that name it.
void remove_slot(std::vector<ComputerSlot>& slots, std::size_t at) {
    const std::uint8_t n = slots[at].number;
    slots[at] = removed_slot(slots[at]);
    for (ComputerSlot& s : slots) {
        if (s.instead == n || s.instead_unlocked == n) {
            s.instead = kNoProducer;
            s.instead_crop = 0;
            s.instead_unlocked = kNoProducer;
        }
        if (s.process_from == n) {
            s.process_from = kNoProducer;
            s.process_per = 0;
            s.process_round_up = false;
            s.process_rank = 0;
            s.process_max = 0;
        }
        if (s.after == n) {
            s.after = kNoProducer;
        }
    }
}

/// Reads table `table` of slots.toml into slot `at` of `slots`.
void read_slot(const core::DataTable& table, std::vector<ComputerSlot>& slots, std::size_t at, const std::string& where,
               const assets::Goods& goods, std::vector<std::string>& warnings) {
    ComputerSlot s = slots[at];
    const auto producer = [&](const std::string* text) -> std::optional<std::uint8_t> {
        if (text == nullptr) {
            return std::nullopt;
        }
        if (text->empty()) {
            return kNoProducer;
        }
        const ComputerSlot* k = find_computer_slot(slots, *text);
        if (k == nullptr || k->number == s.number || (k->kind != Kind::Producer && k->kind != Kind::Tower)) {
            return std::nullopt;
        }
        return k->number;
    };
    for (const core::DataEntry& e : table.entries) {
        const std::string field = line_of(where, e.value.line) + "[" + table.name + "] '" + e.key + "' ";
        const std::string* text = e.value.string();
        const std::int64_t* n = e.value.integer();
        const bool* flag = e.value.boolean();
        const auto small = [&](std::uint8_t& into, std::int64_t min) {
            if (n != nullptr && *n >= min && *n <= 255) {
                into = static_cast<std::uint8_t>(*n);
            } else {
                warnings.push_back(field + "is a number from " + std::to_string(min) + " to 255");
            }
        };
        const auto yes_no = [&](bool& into) {
            if (flag != nullptr) {
                into = *flag;
            } else {
                warnings.push_back(field + "is true or false");
            }
        };
        const auto slot_field = [&](std::uint8_t& into) {
            if (const auto k = producer(text)) {
                into = *k;
            } else {
                warnings.push_back(field + "is another producer (woodcutter, bakery, ...) or \"\"");
            }
        };
        const bool producer_rule = s.kind == Kind::Producer || s.kind == Kind::Tower;
        if (e.key == "remove") {
            if (flag == nullptr) {
                warnings.push_back(field + "is true or false");
            } else if (*flag) {
                if (s.kind == Kind::None) {
                    warnings.push_back(field + "removes producers and public buildings only");
                    continue;
                }
                slots[at] = s;
                remove_slot(slots, at);
                return;  // the fields of a removed slot do not count
            }
        } else if ((e.key == "min_houses" || e.key == "roads_on_fail") && s.kind != Kind::Public) {
            warnings.push_back(field + "is a field of a public building and is passed over");
        } else if (e.key == "min_houses") {
            if (n != nullptr && *n >= 0 && *n <= kMaxSlotHouses) {
                s.min_houses = static_cast<std::int32_t>(*n);
            } else {
                warnings.push_back(field + "is a number from 0 to " + std::to_string(kMaxSlotHouses));
            }
        } else if (e.key == "roads_on_fail") {
            yes_no(s.roads_on_fail);
        } else if (e.key == "counts") {
            if (!counts_goods(s.number)) {
                warnings.push_back(field + "is a field of food and alcohol and is passed over");
                continue;
            }
            if (text == nullptr) {
                warnings.push_back(field + "is a list of goods");
                continue;
            }
            std::vector<std::uint8_t> list;
            std::string_view rest = *text;
            while (!rest.empty()) {
                const std::size_t comma = rest.find(',');
                const std::string_view id = trim(rest.substr(0, comma));
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
                if (id.empty()) {
                    continue;
                }
                const auto w = assets::ware_of(goods, id);
                if (!w) {
                    warnings.push_back(field + "'" + std::string(id) + "' is no good; it is passed over");
                } else if (list.size() >= kMaxSlotGoods) {
                    warnings.push_back(field + "names more than " + std::to_string(kMaxSlotGoods) +
                                       " goods; the rest are passed over");
                    break;
                } else if (std::ranges::find(list, *w) == list.end()) {
                    list.push_back(*w);
                }
            }
            s.counts_goods = std::move(list);
        } else if (!producer_rule) {
            warnings.push_back(field + "is no field of slot '" + table.name + "' and is passed over");
        } else if (e.key == "instead") {
            slot_field(s.instead);
        } else if (e.key == "instead_unlocked") {
            slot_field(s.instead_unlocked);
        } else if (e.key == "instead_crop") {
            const auto it = text != nullptr ? std::ranges::find(kCrops, std::string_view(*text),
                                                                &std::pair<std::string_view, std::uint8_t>::first)
                                            : kCrops.end();
            if (it != kCrops.end()) {
                s.instead_crop = it->second;
            } else {
                warnings.push_back(field + "is a crop (grain, tobacco, spices, sugar, cotton, wine, cocoa)");
            }
        } else if (e.key == "single_below_stock") {
            small(s.single_below_stock, 0);
        } else if (e.key == "single_below_users") {
            small(s.single_below_users, 0);
        } else if (e.key == "raw_stock") {
            small(s.raw_stock, 0);
        } else if (e.key == "process_from") {
            slot_field(s.process_from);
        } else if (e.key == "process_per") {
            small(s.process_per, 1);
        } else if (e.key == "process_round_up") {
            yes_no(s.process_round_up);
        } else if (e.key == "process_rank") {
            small(s.process_rank, 0);
        } else if (e.key == "process_max") {
            small(s.process_max, 0);
        } else if (e.key == "subsidy_ware") {
            if (text != nullptr && text->empty()) {
                s.subsidy_ware = 0;
                s.subsidy_tons = 0;
            } else if (const auto w = text != nullptr ? assets::ware_of(goods, *text) : std::nullopt) {
                s.subsidy_ware = *w;
            } else {
                warnings.push_back(field + "is a good (anno1602:<name> or <package>:<name>) or \"\"");
            }
        } else if (e.key == "subsidy_tons") {
            small(s.subsidy_tons, 0);
        } else if (e.key == "subsidy_above") {
            small(s.subsidy_above, 0);
        } else if (e.key == "subsidy_people") {
            if (n != nullptr && *n >= 0 && *n <= 65535) {
                s.subsidy_people = static_cast<std::uint16_t>(*n);
            } else {
                warnings.push_back(field + "is a number from 0 to 65535");
            }
        } else if (e.key == "subsidy_class") {
            const std::string_view id = text != nullptr ? std::string_view(*text) : std::string_view{};
            const assets::OriginalName* c =
                id.starts_with("anno1602:") ? assets::find_original_name(assets::original_class_names(), id.substr(9))
                                            : nullptr;
            if (c != nullptr) {
                s.subsidy_class = static_cast<std::uint8_t>(c->first);
            } else {
                warnings.push_back(field + "is a class of inhabitants (anno1602:pioneers to anno1602:aristocrats)");
            }
        } else {
            warnings.push_back(field + "is no field of a slot and is passed over");
        }
    }
    // Rules that do not hold together are the slot's before the table.
    if (s.instead != kNoProducer && s.instead_crop == 0) {
        warnings.push_back(line_of(where, table.line) + "[" + table.name +
                           "] 'instead' needs 'instead_crop'; its swap stays as it was");
        s.instead = slots[at].instead;
        s.instead_crop = slots[at].instead_crop;
        s.instead_unlocked = slots[at].instead_unlocked;
    }
    if (s.instead == kNoProducer) {
        s.instead_crop = 0;
        s.instead_unlocked = kNoProducer;
    }
    if (s.process_rank != 0 && (s.process_from == kNoProducer || s.process_per == 0)) {
        warnings.push_back(line_of(where, table.line) + "[" + table.name +
                           "] 'process_rank' needs 'process_from' and 'process_per'; it is not processed");
        s.process_rank = 0;
    }
    if (s.subsidy_tons != 0 && s.subsidy_ware == 0) {
        warnings.push_back(line_of(where, table.line) + "[" + table.name +
                           "] 'subsidy_tons' needs 'subsidy_ware'; there is no subsidy");
        s.subsidy_tons = 0;
    }
    slots[at] = std::move(s);
}

}  // namespace

void apply_slots_file(std::vector<ComputerSlot>& slots, std::string_view package, std::string_view utf8,
                      std::vector<std::string>& warnings, const assets::Goods& goods) {
    const std::string where = std::string(package) + "/" + std::string(kSlotsFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& table : *tables) {
        if (table.name.empty()) {
            warnings.push_back(line_of(where, table.entries.front().value.line) +
                               "fields before the first [table] belong to none and are passed over");
            continue;
        }
        const std::vector<ComputerSlot>& now = computer_slots_of(slots);
        const auto it = std::ranges::find(now, std::string_view(table.name), &ComputerSlot::name);
        if (it == now.end()) {
            warnings.push_back(line_of(where, table.line) + "there is no slot '" + table.name +
                               "' (the original's: woodcutter, chapel, food, ...)");
            continue;
        }
        if (it->removed) {
            warnings.push_back(line_of(where, table.line) + "slot '" + table.name +
                               "' was removed by an earlier table or package and is passed over");
            continue;
        }
        const auto at = static_cast<std::size_t>(it - now.begin());
        if (slots.empty()) {
            slots = original_computer_slots();
        }
        read_slot(table, slots, at, where, goods, warnings);
    }
    if (slots == original_computer_slots()) {
        slots.clear();  // the original's: nothing to keep
    }
}

}  // namespace o1602::sim
