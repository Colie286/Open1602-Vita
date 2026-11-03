// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/computer_orders.hpp"

#include <algorithm>
#include <charconv>
#include <optional>

#include "ai/ai_state.hpp"

#include "o1602/assets/original_names.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/sim/computer_slots.hpp"

namespace o1602::sim {

const std::array<std::string_view, kComputerLists> kComputerListNames = {
    "founded",          "colony",          "colony_materials",
    "pioneers",         "settlers",        "second",
    "second_luxury",    "second_farming",  "main",
    "main_rich",        "main2",           "citizens",
    "citizens2",        "citizens_small",  "merchants",
    "merchants_luxury", "merchants_tail",  "merchants2",
    "merchants2_tail",  "merchants_small", "merchants_small_tail",
    "aristocrats",      "aristocrats2",    "first_crops"};

namespace {

using Kind = ComputerNeed::Kind;
using When = ComputerNeed::When;

// ---------------------------------------------------------------------------------------------
// Names

struct SlotName {
    std::string_view name;
    std::uint8_t slot;
};

/// What a kind without a slot is called and what its count may be.
struct KindInfo {
    Kind kind;
    std::string_view name;  ///< empty: named by its slot (kProducers, kPublic, a good)
    bool counted;           ///< takes a count
    std::uint32_t min;
    std::uint32_t max;
};

/// Tiles per luxury field a colony may ask for at most.
constexpr std::uint32_t kMaxTilesPerField = 10'000;

constexpr std::array<KindInfo, ComputerNeed::kKinds> kKindInfo = {{
    {Kind::Building, "", true, 0, kMaxOrderCount},
    {Kind::Market, "market", true, 0, kMaxOrderCount},
    {Kind::Houses, "houses", true, 0, kMaxOrderCount},
    {Kind::Public, "", true, 0, kMaxOrderCount},
    {Kind::Kontor, "kontor", true, 1, 4},
    {Kind::Castle, "castle", false, 0, 0},
    {Kind::Monument, "monument", false, 0, 0},
    {Kind::Shipyard, "shipyard", false, 0, 0},
    {Kind::Processing, "processing", false, 0, 0},
    {Kind::IronTools, "iron_tools", false, 0, 0},
    {Kind::Weapons, "weapons", false, 0, 0},
    {Kind::Luxury, "luxury", true, 0, kMaxOrderCount},
    {Kind::ColonyLuxury, "colony_luxury", true, 1, kMaxTilesPerField},
    {Kind::Tower, "tower", true, 0, kMaxOrderCount},
    {Kind::RoadsIfMason, "roads_if_mason", false, 0, 0},
    {Kind::Colony, "colony", true, 1, 4},
    {Kind::Package, "", true, 0, kMaxOrderCount},
    {Kind::Crop, "", false, 0, 0},
}};

/// The crops `Kind::Crop` names: what a first colony's island is to grow (the original: by
/// player % 4, ore for the first).
constexpr std::array<SlotName, 7> kCropNames = {{
    {"crop_ore", assets::ware::kOre},
    {"crop_tobacco", assets::ware::kTobaccoField},
    {"crop_spices", assets::ware::kSpiceField},
    {"crop_sugar", assets::ware::kSugarField},
    {"crop_cotton", assets::ware::kCottonField},
    {"crop_wine", assets::ware::kWineField},
    {"crop_cocoa", assets::ware::kCocoaField},
}};

/// The crops a producer's `fertile` names: the field the island must grow.
constexpr std::array<SlotName, 7> kFertileNames = {{
    {"grain", assets::ware::kCornField},
    {"tobacco", assets::ware::kTobaccoField},
    {"spices", assets::ware::kSpiceField},
    {"sugar", assets::ware::kSugarField},
    {"cotton", assets::ware::kCottonField},
    {"wine", assets::ware::kWineField},
    {"cocoa", assets::ware::kCocoaField},
}};

/// The words of `wait`, by ProducerWait.
constexpr std::array<std::string_view, kProducerWaits> kWaitNames = {"none", "money", "unlock", "placed"};

const KindInfo& info(Kind k) {
    return kKindInfo[static_cast<std::size_t>(k)];
}

/// The words of a condition: on the flag after `prefix` ("covered", "one_crop"), on the entry "gold".
constexpr std::array<std::string_view, ComputerNeed::kWhens> kGoldSuffix = {"", "", "_4000", "_5000", "_8000"};

std::optional<std::uint8_t> slot_of(std::span<const SlotName> names, std::string_view name) {
    for (const SlotName& n : names) {
        if (n.name == name) {
            return n.slot;
        }
    }
    return std::nullopt;
}

std::string_view name_of(std::span<const SlotName> names, std::uint8_t slot) {
    for (const SlotName& n : names) {
        if (n.slot == slot) {
            return n.name;
        }
    }
    return {};
}

/// The producers `Kind::Building` names and the public buildings `Kind::Public` names: the slots of
/// that kind (computer_slots.hpp).
constexpr ComputerSlot::Kind kProducers = ComputerSlot::Kind::Producer;
constexpr ComputerSlot::Kind kPublic = ComputerSlot::Kind::Public;

std::optional<std::uint8_t> slot_of(ComputerSlot::Kind kind, std::string_view name) {
    const ComputerSlot* s = find_computer_slot(original_computer_slots(), name);
    return s != nullptr && s->kind == kind ? std::optional<std::uint8_t>(s->number) : std::nullopt;
}

std::string_view name_of(ComputerSlot::Kind kind, std::uint8_t slot) {
    const ComputerSlot* s = computer_slot(original_computer_slots(), slot);
    return s != nullptr && s->kind == kind ? std::string_view(s->name) : std::string_view{};
}

/// The word of a flag or a condition `w` after `prefix`; empty for When::Never.
std::string when_word(std::string_view prefix, When w) {
    if (w == When::Never) {
        return {};
    }
    return std::string(prefix) + std::string(kGoldSuffix[static_cast<std::size_t>(w)]);
}

/// The condition a word names after `prefix` ("covered" → Always, "covered_4000" → Gold4000).
std::optional<When> when_of(std::string_view prefix, std::string_view word) {
    if (!word.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view rest = word.substr(prefix.size());
    for (std::size_t i = 1; i < kGoldSuffix.size(); ++i) {
        if (rest == kGoldSuffix[i]) {
            return static_cast<When>(i);
        }
    }
    return std::nullopt;
}

/// The word of the flag of `k`: Public and Luxury have one.
std::string_view flag_prefix(Kind k) {
    return k == Kind::Public ? "covered" : k == Kind::Luxury ? "one_crop" : "";
}

// ---------------------------------------------------------------------------------------------
// The original's orders (task 5)

constexpr ComputerNeed building(std::uint8_t slot, std::uint32_t n) {
    return {Kind::Building, slot, n};
}
constexpr ComputerNeed market(std::uint32_t n) {
    return {Kind::Market, 0, n};
}
constexpr ComputerNeed houses(std::uint32_t n) {
    return {Kind::Houses, 0, n};
}
constexpr ComputerNeed public_building(std::uint8_t slot, std::uint32_t n, When cov) {
    return {Kind::Public, slot, n, cov};
}
constexpr ComputerNeed kontor(std::uint32_t level) {
    return {Kind::Kontor, 0, level};
}
constexpr ComputerNeed castle() {
    return {Kind::Castle};
}
constexpr ComputerNeed monument() {
    return {Kind::Monument};
}
constexpr ComputerNeed shipyard() {
    return {Kind::Shipyard};
}
constexpr ComputerNeed processing(When when = When::Always) {
    return {Kind::Processing, 0, 0, When::Never, when};
}
constexpr ComputerNeed iron_tools(When when = When::Always) {
    return {Kind::IronTools, 0, 0, When::Never, when};
}
constexpr ComputerNeed weapons() {
    return {Kind::Weapons};
}
constexpr ComputerNeed luxury(std::uint32_t n, When exclusive, When when = When::Always) {
    return {Kind::Luxury, 0, n, exclusive, when};
}
constexpr ComputerNeed colony_luxury(std::uint32_t tiles_per_field) {
    return {Kind::ColonyLuxury, 0, tiles_per_field};
}
constexpr ComputerNeed tower(std::uint32_t n, When when = When::Always) {
    return {Kind::Tower, 0, n, When::Never, when};
}
constexpr ComputerNeed roads_if_mason() {
    return {Kind::RoadsIfMason};
}
constexpr ComputerNeed colony(std::uint32_t mode) {
    return {Kind::Colony, 0, mode};
}
constexpr ComputerNeed crop(std::uint8_t ware) {
    return {Kind::Crop, ware, 0};
}

constexpr When kAlways = When::Always;
constexpr When kNever = When::Never;
constexpr When k4000 = When::Gold4000;
constexpr When k5000 = When::Gold5000;
constexpr When k8000 = When::Gold8000;

using namespace ai;  // the slots

constexpr std::array kFoundedOrder = {market(1), building(kSlotFisher, 1), building(kSlotWoodcutter, 1), houses(8)};
constexpr std::array kColonyOrder = {market(0), processing()};
constexpr std::array kColonyMaterials = {building(kSlotQuarry, 1), building(kSlotWoodcutter, 1),
                                         building(kSlotStonemason, 1)};
constexpr std::array kPioneersOrder = {market(0),
                                       building(kSlotWoodcutter, 1),
                                       houses(8),
                                       public_building(kSlotKapelle, 0, kAlways),
                                       houses(16),
                                       building(kSlotCattle, 2),
                                       building(kSlotButcher, 1)};
constexpr std::array kSettlersOrder = {market(0),
                                       building(kSlotWoodcutter, 2),
                                       building(kSlotSheep, 2),
                                       building(kSlotWebstube, 1),
                                       building(kSlotFisher, 2),
                                       building(kSlotCattle, 2),
                                       building(kSlotButcher, 1),
                                       houses(18),
                                       public_building(kSlotKapelle, 0, kAlways),
                                       building(kSlotQuarry, 1),
                                       building(kSlotStonemason, 1),
                                       houses(24)};
constexpr std::array kSecondOrder = {market(0),
                                     building(kSlotSheep, 2),
                                     building(kSlotWebstube, 1),
                                     building(kSlotFisher, 2),
                                     houses(18),
                                     public_building(kSlotKapelle, 0, kAlways),
                                     building(kSlotQuarry, 1),
                                     building(kSlotStonemason, 2),
                                     houses(22),
                                     building(kSlotCattle, 2),
                                     building(kSlotButcher, 1),
                                     public_building(kSlotBrunnen, 1, kNever)};
constexpr std::array kSecondLuxuryOrder = {market(0),
                                           houses(18),
                                           public_building(kSlotKapelle, 0, kAlways),
                                           houses(24),
                                           public_building(kSlotBrunnen, 1, kNever),
                                           public_building(kSlotKlinik, 1, kNever),
                                           processing(),
                                           colony_luxury(128),
                                           tower(6)};
constexpr std::array kSecondFarmingOrder = {market(0),
                                            public_building(kSlotKapelle, 0, kAlways),
                                            houses(24),
                                            public_building(kSlotBrunnen, 1, kNever),
                                            public_building(kSlotKlinik, 1, kNever),
                                            processing()};
constexpr std::array kMainOrder = {market(0),
                                   kontor(2),
                                   building(kSlotFisher, 3),
                                   building(kSlotWoodcutter, 2),
                                   building(kSlotGrain, 4),
                                   building(kSlotMill, 2),
                                   building(kSlotBakery, 1),
                                   building(kSlotQuarry, 1),
                                   building(kSlotStonemason, 1),
                                   houses(30),
                                   public_building(kSlotKapelle, 0, kAlways),
                                   houses(32),
                                   building(kSlotSheep, 4),
                                   building(kSlotWebstube, 2),
                                   castle()};
/// With 4000 gold the main city also works up its goods (one condition in the original).
constexpr std::array kMainRichOrder = {processing(k4000), iron_tools(k4000), luxury(4, kAlways, k4000)};
constexpr std::array kMain2Order = {
    market(0),         public_building(kSlotKapelle, 0, kAlways), houses(34), castle(), processing(), iron_tools(),
    luxury(4, kAlways)};
constexpr std::array kCitizensOrder = {processing(),
                                       weapons(),
                                       market(0),
                                       kontor(3),
                                       public_building(kSlotWirt, 1, k4000),
                                       public_building(kSlotSchule, 1, k4000),
                                       public_building(kSlotKlinik, 1, k4000),
                                       building(kSlotQuarry, 1),
                                       building(kSlotStonemason, 1),
                                       public_building(kSlotBrunnen, 1, k4000),
                                       public_building(kSlotKapelle, 0, kAlways),
                                       houses(36),
                                       castle(),
                                       iron_tools(),
                                       luxury(6, kAlways),
                                       roads_if_mason()};
constexpr std::array kCitizens2Order = {processing(),
                                        weapons(),
                                        market(0),
                                        public_building(kSlotWirt, 1, k4000),
                                        public_building(kSlotSchule, 1, k4000),
                                        public_building(kSlotKlinik, 1, k4000),
                                        public_building(kSlotGalgen, 1, kAlways),
                                        public_building(kSlotBrunnen, 1, kAlways),
                                        castle(),
                                        building(kSlotCattle, 4),
                                        building(kSlotButcher, 2),
                                        public_building(kSlotKapelle, 0, kAlways),
                                        houses(38),
                                        building(kSlotSheep, 6),
                                        building(kSlotWebstube, 3),
                                        monument(),
                                        iron_tools(),
                                        luxury(6, kNever),
                                        tower(6, k4000),
                                        roads_if_mason(),
                                        colony(2)};
constexpr std::array kCitizensSmallOrder = {processing(),
                                            weapons(),
                                            market(0),
                                            public_building(kSlotWirt, 0, k4000),
                                            public_building(kSlotSchule, 0, k4000),
                                            public_building(kSlotKlinik, 0, k4000),
                                            castle(),
                                            public_building(kSlotGalgen, 2, kAlways),
                                            public_building(kSlotBrunnen, 1, kAlways),
                                            building(kSlotCattle, 4),
                                            public_building(kSlotKapelle, 0, kAlways),
                                            houses(48),
                                            building(kSlotQuarry, 1),
                                            building(kSlotStonemason, 1),
                                            iron_tools(),
                                            luxury(6, kNever),
                                            tower(16, k4000),
                                            roads_if_mason(),
                                            colony(2)};
constexpr std::array kMerchantsOrder = {processing(),
                                        weapons(),
                                        market(0),
                                        castle(),
                                        public_building(kSlotWirt, 0, k4000),
                                        public_building(kSlotSchule, 0, k4000),
                                        public_building(kSlotKlinik, 0, k4000),
                                        public_building(kSlotGalgen, 1, kAlways),
                                        public_building(kSlotBrunnen, 1, kAlways),
                                        building(kSlotCattle, 4),
                                        building(kSlotButcher, 2),
                                        public_building(kSlotKapelle, 0, kAlways),
                                        houses(40),
                                        building(kSlotQuarry, 1),
                                        building(kSlotStonemason, 2),
                                        monument(),
                                        iron_tools(),
                                        luxury(8, kAlways),
                                        colony(3)};
/// Once the merchants' luxury is in stock: bath house and church.
constexpr std::array kMerchantsLuxuryOrder = {public_building(kSlotBadehaus, 1, kNever),
                                              public_building(kSlotKirche, 1, kNever)};
constexpr std::array kMerchantsTail = {tower(16, k5000), roads_if_mason()};
/// The second stage of merchants; a city with little land stays there (kMerchantsSmall), which
/// differs only in the luxury goods it shares.
constexpr std::array<ComputerNeed, 19> merchants2_order(When exclusive) {
    return {weapons(),
            market(0),
            castle(),
            kontor(4),
            public_building(kSlotWirt, 0, k4000),
            public_building(kSlotSchule, 0, k4000),
            public_building(kSlotKlinik, 0, k4000),
            public_building(kSlotGalgen, 1, kAlways),
            public_building(kSlotBrunnen, 1, kAlways),
            public_building(kSlotBadehaus, 1, kNever),
            public_building(kSlotKirche, 1, kNever),
            building(kSlotCattle, 6),
            building(kSlotButcher, 3),
            houses(42),
            shipyard(),
            monument(),
            processing(),
            iron_tools(),
            luxury(8, exclusive)};
}
constexpr std::array kMerchants2Order = merchants2_order(kAlways);
constexpr std::array kMerchantsSmallOrder = merchants2_order(kNever);
constexpr std::array kMerchants2Tail = {tower(16, k5000), roads_if_mason(), colony(3)};
constexpr std::array kMerchantsSmallTail = {tower(20, k4000), roads_if_mason(), colony(3)};
constexpr std::array kAristocratsOrder = {processing(),
                                          weapons(),
                                          market(0),
                                          castle(),
                                          building(kSlotTailor, 1),
                                          public_building(kSlotKapelle, 0, kAlways),
                                          public_building(kSlotWirt, 0, k8000),
                                          public_building(kSlotSchule, 0, k8000),
                                          public_building(kSlotKlinik, 0, k8000),
                                          public_building(kSlotGalgen, 1, kAlways),
                                          public_building(kSlotBrunnen, 1, kAlways),
                                          public_building(kSlotBadehaus, 1, k8000),
                                          public_building(kSlotKirche, 1, k8000),
                                          building(kSlotCattle, 6),
                                          building(kSlotButcher, 3),
                                          houses(44),
                                          building(kSlotGoldsmith, 1),
                                          shipyard(),
                                          monument(),
                                          iron_tools(),
                                          luxury(10, kAlways),
                                          tower(16, k5000),
                                          roads_if_mason()};
constexpr std::array kAristocrats2Order = {processing(),
                                           weapons(),
                                           market(0),
                                           castle(),
                                           kontor(4),
                                           public_building(kSlotKapelle, 0, kAlways),
                                           public_building(kSlotWirt, 0, kNever),
                                           public_building(kSlotSchule, 0, kNever),
                                           public_building(kSlotKlinik, 0, kNever),
                                           public_building(kSlotGalgen, 1, kAlways),
                                           public_building(kSlotBrunnen, 1, kAlways),
                                           public_building(kSlotBadehaus, 1, kNever),
                                           public_building(kSlotKirche, 1, kNever),
                                           public_building(kSlotHochschule, 1, k8000),
                                           public_building(kSlotTheater, 1, k8000),
                                           building(kSlotCattle, 6),
                                           building(kSlotButcher, 3),
                                           houses(46),
                                           building(kSlotGoldsmith, 1),
                                           shipyard(),
                                           monument(),
                                           iron_tools(),
                                           luxury(10, kAlways),
                                           tower(20, k4000),
                                           roads_if_mason(),
                                           colony(4)};

/// The original's orders, by ComputerList.
/// The original's first_crop by player % 4.
constexpr std::array kFirstCrops = {crop(assets::ware::kOre), crop(assets::ware::kSpiceField),
                                    crop(assets::ware::kTobaccoField), crop(assets::ware::kWineField)};

const std::array<std::span<const ComputerNeed>, kComputerLists> kDefaultOrders = {
    kFoundedOrder,         kColonyOrder,       kColonyMaterials,    kPioneersOrder,      kSettlersOrder,
    kSecondOrder,          kSecondLuxuryOrder, kSecondFarmingOrder, kMainOrder,          kMainRichOrder,
    kMain2Order,           kCitizensOrder,     kCitizens2Order,     kCitizensSmallOrder, kMerchantsOrder,
    kMerchantsLuxuryOrder, kMerchantsTail,     kMerchants2Order,    kMerchants2Tail,     kMerchantsSmallOrder,
    kMerchantsSmallTail,   kAristocratsOrder,  kAristocrats2Order,  kFirstCrops};

// ---------------------------------------------------------------------------------------------
// computers.toml

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

std::string_view trim(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string_view> split(std::string_view s, char by) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == by) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

std::vector<std::string_view> words(std::string_view s) {
    std::vector<std::string_view> out;
    for (std::string_view w : split(s, ' ')) {
        w = trim(w);
        if (!w.empty()) {
            out.push_back(w);
        }
    }
    return out;
}

std::optional<std::uint32_t> number(std::string_view w) {
    std::uint32_t n = 0;
    const auto [end, error] = std::from_chars(w.data(), w.data() + w.size(), n);
    if (error != std::errc{} || end != w.data() + w.size()) {
        return std::nullopt;
    }
    return n;
}

/// One entry of an order ("cattle 4 gold_4000"); a message on what is wrong otherwise.
std::optional<ComputerNeed> read_need(std::string_view text, const assets::Goods& goods, std::string& problem) {
    const std::vector<std::string_view> w = words(text);
    if (w.empty()) {
        problem = "an empty entry";
        return std::nullopt;
    }
    ComputerNeed need;
    if (const auto slot = slot_of(kProducers, w[0])) {
        need.kind = Kind::Building;
        need.slot = *slot;
    } else if (const auto pub = slot_of(kPublic, w[0])) {
        need.kind = Kind::Public;
        need.slot = *pub;
    } else if (const auto crop_ware = slot_of(kCropNames, w[0])) {
        need.kind = Kind::Crop;
        need.slot = *crop_ware;
    } else if (w[0].find(':') != std::string_view::npos) {
        const assets::OwnGood* good = goods.own(w[0]);
        if (good == nullptr) {
            problem = "'" + std::string(w[0]) + "' is no good of a package";
            return std::nullopt;
        }
        need.kind = Kind::Package;
        need.slot = good->number;
    } else {
        const auto k = std::find_if(kKindInfo.begin(), kKindInfo.end(),
                                    [&](const KindInfo& i) { return !i.name.empty() && i.name == w[0]; });
        if (k == kKindInfo.end()) {
            problem = "'" + std::string(w[0]) + "' is nothing the computers build";
            return std::nullopt;
        }
        need.kind = k->kind;
    }
    const KindInfo& kind = info(need.kind);
    std::size_t at = 1;
    if (kind.counted) {
        const std::optional<std::uint32_t> n = at < w.size() ? number(w[at]) : std::nullopt;
        if (!n || *n < kind.min || *n > kind.max) {
            problem = "'" + std::string(w[0]) + "' needs a number from " + std::to_string(kind.min) + " to " +
                      std::to_string(kind.max);
            return std::nullopt;
        }
        need.n = *n;
        ++at;
    }
    if (need.kind == Kind::Crop && w.size() > 1) {
        problem = "a crop takes no condition";
        return std::nullopt;
    }
    for (; at < w.size(); ++at) {
        const std::string_view prefix = flag_prefix(need.kind);
        if (const auto f = prefix.empty() ? std::nullopt : when_of(prefix, w[at])) {
            need.flag = *f;
        } else if (const auto g = when_of("gold", w[at]); g && *g != When::Always) {
            need.when = *g;
        } else if (w[at] == "never") {
            need.when = When::Never;
        } else {
            problem = "'" + std::string(w[at]) + "' is no condition of '" + std::string(w[0]) + "'";
            return std::nullopt;
        }
    }
    return need;
}

/// A building a producer names: "anno1602:<name or number>" or a package's own.
std::optional<std::uint32_t> building_of(std::string_view id, std::span<const assets::OwnBuilding> own) {
    if (const auto original = assets::original_building_id(id)) {
        return original;
    }
    if (const assets::OwnBuilding* b = assets::find_own_building(own, id)) {
        return b->number;
    }
    return std::nullopt;
}

/// Reads [producer.<name>] into `producers`, the fields over those an earlier package gave.
void read_producer(const core::DataTable& table, std::string_view name, const std::string& where,
                   std::vector<ComputerProducer>& producers, std::span<const assets::OwnBuilding> own,
                   std::vector<std::string>& warnings) {
    const auto slot = slot_of(kProducers, name);
    if (!slot) {
        warnings.push_back(line_of(where, table.line) + "there is no producer '" + std::string(name) +
                           "' (the producers of an order: woodcutter, bakery, ...)");
        return;
    }
    ComputerProducer p;
    p.slot = *slot;
    if (const ComputerProducer* before = producer_change(producers, *slot)) {
        p = *before;
    }
    for (const core::DataEntry& e : table.entries) {
        const std::string field = line_of(where, e.value.line) + "[" + table.name + "] '" + e.key + "' ";
        const std::string* text = e.value.string();
        const std::int64_t* n = e.value.integer();
        if (e.key == "building") {
            const auto id = text != nullptr ? building_of(*text, own) : std::nullopt;
            if (id) {
                p.building = id;
            } else {
                warnings.push_back(field + "names no building (anno1602:<name> or <package>:<name>)");
            }
        } else if (e.key == "attempts") {
            if (n != nullptr && *n >= 1 && *n <= 255) {
                p.attempts = static_cast<std::uint8_t>(*n);
            } else {
                warnings.push_back(field + "is a number from 1 to 255");
            }
        } else if (e.key == "fertile") {
            if (text != nullptr && text->empty()) {
                p.fertile = std::uint8_t{0};
            } else if (const auto crop = text != nullptr ? slot_of(kFertileNames, *text) : std::nullopt) {
                p.fertile = crop;
            } else {
                warnings.push_back(field + "is a crop (grain, tobacco, spices, sugar, cotton, wine, cocoa) or \"\"");
            }
        } else if (e.key == "after") {
            if (text != nullptr && text->empty()) {
                p.after = kNoProducer;
            } else if (const auto first = text != nullptr ? slot_of(kProducers, *text) : std::nullopt;
                       first && *first != *slot) {
                p.after = first;
            } else {
                warnings.push_back(field + "is another producer of an order or \"\"");
            }
        } else if (e.key == "wait") {
            const auto it = text != nullptr ? std::find(kWaitNames.begin(), kWaitNames.end(), *text) : kWaitNames.end();
            if (it != kWaitNames.end()) {
                p.wait = static_cast<ProducerWait>(it - kWaitNames.begin());
            } else {
                warnings.push_back(field + "is none, money, unlock or placed");
            }
        } else if (e.key == "counts") {
            if (text == nullptr) {
                warnings.push_back(field + "is a list of buildings");
                continue;
            }
            std::vector<std::uint32_t> counts;
            for (const std::string_view entry : split(*text, ',')) {
                const std::string_view id = trim(entry);
                if (id.empty()) {
                    continue;
                }
                const auto b = building_of(id, own);
                if (!b) {
                    warnings.push_back(field + "'" + std::string(id) + "' is no building; it is passed over");
                } else if (counts.size() >= kMaxProducerCounts) {
                    warnings.push_back(field + "names more than " + std::to_string(kMaxProducerCounts) +
                                       " buildings; the rest are passed over");
                    break;
                } else if (std::find(counts.begin(), counts.end(), *b) == counts.end()) {
                    counts.push_back(*b);
                }
            }
            p.counts = std::move(counts);
        } else {
            warnings.push_back(field + "is no field of a producer and is passed over");
        }
    }
    std::erase_if(producers, [&](const ComputerProducer& c) { return c.slot == p.slot; });
    const auto place =
        std::find_if(producers.begin(), producers.end(), [&](const ComputerProducer& c) { return c.slot > p.slot; });
    producers.insert(place, std::move(p));
}

}  // namespace

std::span<const ComputerNeed> default_order(ComputerList list) {
    const auto i = static_cast<std::size_t>(list);
    return i < kDefaultOrders.size() ? kDefaultOrders[i] : std::span<const ComputerNeed>{};
}

std::span<const ComputerNeed> order_of(const std::vector<ComputerOrder>& orders, ComputerList list) {
    for (const ComputerOrder& o : orders) {
        if (o.list == list) {
            return o.needs;
        }
    }
    return default_order(list);
}

std::uint8_t first_crop(const std::vector<ComputerOrder>& orders, std::uint32_t player) {
    std::vector<std::uint8_t> crops;
    for (const ComputerNeed& n : order_of(orders, ComputerList::FirstCrops)) {
        if (n.kind == Kind::Crop) {
            crops.push_back(n.slot);
        }
    }
    return crops.empty() ? std::uint8_t{0} : crops[player % crops.size()];
}

bool valid(const ComputerNeed& need) noexcept {
    if (static_cast<std::uint8_t>(need.kind) >= ComputerNeed::kKinds ||
        static_cast<std::uint8_t>(need.flag) >= ComputerNeed::kWhens ||
        static_cast<std::uint8_t>(need.when) >= ComputerNeed::kWhens) {
        return false;
    }
    const KindInfo& kind = info(need.kind);
    if (kind.counted ? (need.n < kind.min || need.n > kind.max) : need.n != 0) {
        return false;
    }
    if (flag_prefix(need.kind).empty() && need.flag != When::Never) {
        return false;
    }
    switch (need.kind) {
        case Kind::Building:
            return !name_of(kProducers, need.slot).empty();
        case Kind::Public:
            return !name_of(kPublic, need.slot).empty();
        case Kind::Package:
            return need.slot >= assets::ware::kFirstOwn;
        case Kind::Crop:
            return need.when == ComputerNeed::When::Always && !name_of(kCropNames, need.slot).empty();
        default:
            return need.slot == 0;
    }
}

bool valid(const std::vector<ComputerOrder>& orders) noexcept {
    for (std::size_t i = 0; i < orders.size(); ++i) {
        const ComputerOrder& o = orders[i];
        if (static_cast<std::size_t>(o.list) >= kComputerLists || o.needs.size() > kMaxOrderNeeds ||
            (i > 0 && orders[i - 1].list >= o.list) ||
            !std::all_of(o.needs.begin(), o.needs.end(), [](const ComputerNeed& n) { return valid(n); })) {
            return false;
        }
    }
    return true;
}

void apply_computers_file(std::vector<ComputerOrder>& orders, std::string_view package, std::string_view utf8,
                          std::vector<std::string>& warnings, const assets::Goods& goods,
                          std::vector<ComputerProducer>* producers, std::span<const assets::OwnBuilding> own) {
    const std::string where = std::string(package) + "/" + std::string(kComputersFile);
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
        if (table.name.starts_with("producer.")) {
            if (producers != nullptr) {
                read_producer(table, std::string_view(table.name).substr(9), where, *producers, own, warnings);
            }
            continue;
        }
        const auto name = std::find(kComputerListNames.begin(), kComputerListNames.end(), table.name);
        if (name == kComputerListNames.end()) {
            warnings.push_back(line_of(where, table.line) + "there is no order [" + table.name + "]");
            continue;
        }
        const auto list = static_cast<ComputerList>(name - kComputerListNames.begin());
        const std::span<const ComputerNeed> before = order_of(orders, list);
        std::vector<ComputerNeed> needs(before.begin(), before.end());
        for (const core::DataEntry& e : table.entries) {
            const std::string at = line_of(where, e.value.line);
            const std::string* text = e.value.string();
            if (e.key != "order" && e.key != "append") {
                warnings.push_back(at + "unknown field '" + e.key + "'");
                continue;
            }
            if (text == nullptr) {
                warnings.push_back(at + "'" + e.key + "' must be a string of entries");
                continue;
            }
            if (e.key == "order") {
                needs.clear();
            }
            if (trim(*text).empty()) {
                continue;
            }
            const std::string in = at + "[" + table.name + "]: ";
            for (const std::string_view entry : split(*text, ',')) {
                std::string problem;
                const std::optional<ComputerNeed> need = read_need(entry, goods, problem);
                if (!need) {
                    problem += ", the entry is passed over";
                    warnings.push_back(in + problem);
                } else if ((need->kind == Kind::Crop) != (list == ComputerList::FirstCrops)) {
                    warnings.push_back(
                        in + "'" + std::string(trim(entry)) +
                        (list == ComputerList::FirstCrops ? "' is no crop" : "' belongs in [first_crops]") +
                        ", the entry is passed over");
                } else if (needs.size() >= kMaxOrderNeeds) {
                    warnings.push_back(in + "more than " + std::to_string(kMaxOrderNeeds) +
                                       " entries, the rest is passed over");
                    break;
                } else {
                    needs.push_back(*need);
                }
            }
        }
        // An order as the original's is none a game has to keep.
        std::erase_if(orders, [&](const ComputerOrder& o) { return o.list == list; });
        const std::span<const ComputerNeed> original = default_order(list);
        if (!std::equal(needs.begin(), needs.end(), original.begin(), original.end())) {
            const auto place =
                std::find_if(orders.begin(), orders.end(), [&](const ComputerOrder& o) { return o.list > list; });
            orders.insert(place, ComputerOrder{list, std::move(needs)});
        }
    }
}

std::string order_text(std::span<const ComputerNeed> needs, const assets::Goods& goods) {
    std::string out;
    for (const ComputerNeed& n : needs) {
        if (!out.empty()) {
            out += ", ";
        }
        switch (n.kind) {
            case Kind::Building:
                out += name_of(kProducers, n.slot);
                break;
            case Kind::Public:
                out += name_of(kPublic, n.slot);
                break;
            case Kind::Crop:
                out += name_of(kCropNames, n.slot);
                break;
            case Kind::Package:
                if (const assets::OwnGood* good = goods.own(n.slot)) {
                    out += good->id;
                } else {
                    out += "ware:" + std::to_string(n.slot);  // no good of these: cannot be read back
                }
                break;
            default:
                out += info(n.kind).name;
                break;
        }
        if (info(n.kind).counted) {
            out += " " + std::to_string(n.n);
        }
        if (const std::string_view prefix = flag_prefix(n.kind); !prefix.empty() && n.flag != When::Never) {
            out += " " + when_word(prefix, n.flag);
        }
        if (n.when == When::Never) {
            out += " never";
        } else if (n.when != When::Always) {
            out += " " + when_word("gold", n.when);
        }
    }
    return out;
}

const ComputerProducer* producer_change(const std::vector<ComputerProducer>& producers, std::uint8_t slot) noexcept {
    const auto it =
        std::find_if(producers.begin(), producers.end(), [&](const ComputerProducer& p) { return p.slot == slot; });
    return it == producers.end() ? nullptr : &*it;
}

bool valid(const std::vector<ComputerProducer>& producers) noexcept {
    for (std::size_t i = 0; i < producers.size(); ++i) {
        const ComputerProducer& p = producers[i];
        const bool known = !name_of(kProducers, p.slot).empty();
        const bool fertile = !p.fertile || *p.fertile == 0 || !name_of(kFertileNames, *p.fertile).empty();
        const bool after =
            !p.after || *p.after == kNoProducer || (*p.after != p.slot && !name_of(kProducers, *p.after).empty());
        if (!known || (i > 0 && producers[i - 1].slot >= p.slot) || (p.attempts && *p.attempts == 0) || !fertile ||
            !after || (p.wait && static_cast<std::uint8_t>(*p.wait) >= kProducerWaits) ||
            p.counts.size() > kMaxProducerCounts || (p.building && *p.building == 0)) {
            return false;
        }
    }
    return true;
}

std::string producer_text(const ComputerProducer& p) {
    std::string out = "[producer." + std::string(name_of(kProducers, p.slot)) + "]";
    if (p.building) {
        out += " building = anno1602:" + std::to_string(*p.building);
    }
    if (p.attempts) {
        out += " attempts = " + std::to_string(*p.attempts);
    }
    if (p.fertile) {
        out += " fertile = \"" + std::string(name_of(kFertileNames, *p.fertile)) + "\"";
    }
    if (p.after) {
        out += " after = \"" + std::string(name_of(kProducers, *p.after)) + "\"";
    }
    if (p.wait) {
        out += " wait = " + std::string(kWaitNames[static_cast<std::size_t>(*p.wait)]);
    }
    if (!p.counts.empty()) {
        out += " counts =";
        for (const std::uint32_t c : p.counts) {
            out += " anno1602:" + std::to_string(c);
        }
    }
    return out;
}

}  // namespace o1602::sim
