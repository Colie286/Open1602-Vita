// SPDX-License-Identifier: GPL-3.0-or-later
#include "endless_setup.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/sim/player.hpp"
#include "o1602/sim/victory.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Amount = sim::WorldSettings::Amount;
using Sizes = sim::WorldSettings::Sizes;
using Shapes = sim::WorldSettings::Shapes;
using Starts = sim::WorldSettings::Starts;
using Landscape = sim::WorldSettings::Landscape;
using Share = sim::shapes::Share;
using Roughness = sim::shapes::Roughness;

// The values the dialog offers.
constexpr std::array<std::uint8_t, 3> kRefundPercents = {25, 50, 75};  // between none and full
constexpr std::array<std::uint32_t, 4> kShips = {33, 100, 200, 0};     // 0: unlimited
constexpr std::array<std::uint32_t, 4> kUnits = {100, 250, 500, 0};
constexpr std::array<std::uint32_t, 2> kSpeeds = {4, 8};
/// Map sizes: smaller than the original's, its 500 x 350, then larger ones of the same shape, then
/// the generator's largest (sim::WorldSettings::kMaxSide).
constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 9> kSizes = {{{350, 250},
                                                                            {500, 350},
                                                                            {700, 490},
                                                                            {1000, 700},
                                                                            {1400, 980},
                                                                            {2000, 1400},
                                                                            {2800, 1960},
                                                                            {4000, 2800},
                                                                            {4000, 4000}}};
static_assert(kSizes.back().first <= sim::WorldSettings::kMaxSide &&
              kSizes.back().second <= sim::WorldSettings::kMaxSide);
constexpr std::uint8_t kFewestPlayers = 2;
constexpr auto kMostPlayers = static_cast<std::uint8_t>(sim::kMaxPlayers);  // as in multiplayer
constexpr std::array<Amount, 3> kIslandAmounts = {Amount::Few, Amount::Normal, Amount::Many};
constexpr std::array<Amount, 4> kAmounts = {Amount::None, Amount::Few, Amount::Normal, Amount::Many};
/// Islands with gold: 0 by the level and the world's size, then fixed numbers.
constexpr std::array<std::uint32_t, 16> kGoldIslands = {0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 25, 30, 35, 40};
// The victory conditions, each first "off" (0). Later Anno titles ask for a few thousand of the
// highest class or a million in gold; an endless game of Anno 1602 reaches 1,000 aristocrats in
// a few hours (the multiplayer map "Das Atoll" asks for them).
constexpr std::array<std::uint32_t, 7> kWinInhabitants = {0, 1000, 2500, 5000, 10'000, 20'000, 50'000};
constexpr std::uint8_t kFirstWinClass = 1;  // settlers: pioneers are every city's start
constexpr std::uint8_t kLastWinClass = 4;   // aristocrats
constexpr std::array<std::uint32_t, 7> kWinClassInhabitants = {0, 100, 250, 500, 1000, 2500, 5000};
constexpr std::array<std::uint32_t, 6> kWinMoney = {0, 100'000, 250'000, 500'000, 1'000'000, 2'500'000};
constexpr std::array<std::uint32_t, 5> kWinBalance = {0, 250, 500, 1000, 2500};
constexpr std::array<std::uint32_t, 6> kWinIslands = {0, 25, 33, 50, 75, 100};
constexpr std::array<std::uint32_t, 6> kWinScore = {0, 5000, 10'000, 25'000, 50'000, 100'000};
constexpr std::array<std::uint32_t, 7> kWinMinutes = {0, 60, 120, 180, 240, 360, 480};

/// The index of the value nearest to `v` (0 counts as the largest: unlimited).
template <std::size_t N>
std::size_t nearest(const std::array<std::uint32_t, N>& values, std::uint32_t v) {
    if (v == 0) {
        for (std::size_t i = 0; i < N; ++i) {
            if (values[i] == 0) {
                return i;
            }
        }
    }
    std::size_t best = 0;
    std::uint64_t best_distance = UINT64_MAX;
    for (std::size_t i = 0; i < N; ++i) {
        if (values[i] == 0) {
            continue;
        }
        const std::uint64_t d = values[i] > v ? values[i] - v : v - values[i];
        if (d < best_distance) {
            best = i;
            best_distance = d;
        }
    }
    return best;
}

template <class T, std::size_t N>
std::size_t index_of(const std::array<T, N>& values, T v) {
    for (std::size_t i = 0; i < N; ++i) {
        if (values[i] == v) {
            return i;
        }
    }
    return 0;
}

std::string number_text(std::uint32_t v) {
    return v == 0 ? ui::tr("setup.unlimited") : std::to_string(v);
}

/// A number with its thousands marked as the language does ("10,000", "10.000").
std::string grouped(std::uint32_t v) {
    const std::string digits = std::to_string(v);
    const std::string mark = ui::tr("setup.thousands");
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && (digits.size() - i) % 3 == 0) {
            out += mark;
        }
        out += digits[i];
    }
    return out;
}

/// A condition's value: "off" for 0.
template <std::size_t N>
std::string condition_text(const std::array<std::uint32_t, N>& values, std::size_t choice, std::string_view key) {
    return values[choice] == 0 ? ui::tr("setup.off") : ui::tr(key, {grouped(values[choice])});
}

std::string amount_text(Amount a) {
    switch (a) {
        case Amount::None:
            return ui::tr("setup.amount.none");
        case Amount::Few:
            return ui::tr("setup.amount.few");
        case Amount::Normal:
            break;
        case Amount::Many:
            return ui::tr("setup.amount.many");
    }
    return ui::tr("setup.amount.normal");
}

std::optional<std::uint32_t> parse_number(std::string_view v) {
    std::uint32_t n = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    return ec == std::errc{} && end == v.data() + v.size() && !v.empty() ? std::optional(n) : std::nullopt;
}

// Names of the values in settings.cfg.
constexpr std::array<std::string_view, 5> kDroughtNames = {"game", "off", "rare", "normal", "frequent"};
constexpr std::array<std::string_view, 4> kVolcanoNames = {"game", "off", "on", "rare"};
constexpr std::array<std::string_view, 3> kOutbreakNames = {"original", "rare", "off"};
constexpr std::array<std::string_view, 3> kPirateNames = {"game", "off", "on"};
constexpr std::array<std::string_view, 3> kComputerNames = {"game", "off", "on"};
constexpr std::array<std::string_view, 3> kMovingNames = {"free", "rebuild", "off"};  // sim::Moving
constexpr std::array<std::string_view, 3> kOreVeinNames = {"game", "finite", "endless"};
constexpr std::array<std::string_view, 2> kPirateNestNames = {"one", "per4"};
constexpr std::array<std::string_view, 2> kTraderReachNames = {"original", "per4"};
constexpr std::array<std::string_view, 3> kRevealNames = {"none", "original", "all"};
constexpr std::array<std::string_view, 4> kAmountNames = {"none", "few", "normal", "many"};
constexpr std::array<std::string_view, 3> kSizeNames = {"small", "mixed", "large"};
constexpr std::array<std::string_view, 2> kStartNames = {"spread", "together"};
constexpr std::array<std::string_view, 3> kShapeNames = {"templates", "mixed", "own"};
constexpr std::array<std::string_view, 4> kMonumentNames = {"none", "castle", "cathedral", "both"};
constexpr const auto& kSpreadNames = sim::landscape_names::kSpread;
constexpr const auto& kGapNames = sim::landscape_names::kGap;
constexpr const auto& kCoastNames = sim::landscape_names::kCoast;
constexpr const auto& kOutlineNames = sim::landscape_names::kOutline;
constexpr const auto& kShareNames = sim::landscape_names::kShare;

// The page of the islands: percent of the map that is land, the sides of the smallest and the
// largest island in tiles.
constexpr std::array<std::uint32_t, 8> kLandShares = {10, 15, 20, 25, 30, 35, 40, 45};
constexpr std::array<std::uint32_t, 11> kSmallest = {16, 20, 25, 30, 40, 50, 60, 80, 100, 150, 200};
constexpr std::array<std::uint32_t, 14> kLargest = {40, 50, 60, 80, 100, 120, 150, 200, 250, 300, 400, 500, 700, 1000};
static_assert(kLandShares.front() >= Landscape::kMinLand && kLandShares.back() <= Landscape::kMaxLand);
static_assert(kSmallest.front() >= sim::shapes::kMinSide && kLargest.back() <= sim::shapes::kMaxSide);

/// The kinds of world of the page's first item, after the original's islands: Open1602's
/// (setup.island_preset.<name>), then the packages' (landscapes.toml).
constexpr const auto& kIslandPresets = sim::kLandscapePresets;

std::vector<sim::PackageLandscape>& package_presets() {
    static std::vector<sim::PackageLandscape> presets;
    return presets;
}

/// The presets of the first item after the original's islands.
std::size_t island_preset_count() {
    return kIslandPresets.size() + package_presets().size();
}

/// The landscape of preset `i` (0: Open1602's first).
const sim::WorldSettings::Landscape& island_preset_landscape(std::size_t i) {
    return i < kIslandPresets.size() ? kIslandPresets[i].landscape
                                     : package_presets()[i - kIslandPresets.size()].landscape;
}

/// The preset choice the setup is (0 the original's islands), or nothing.
std::optional<std::size_t> island_preset(const sim::WorldSettings& w) {
    if (w.shapes == Shapes::Templates) {
        return 0;
    }
    if (w.shapes != Shapes::Own) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < island_preset_count(); ++i) {
        if (island_preset_landscape(i) == w.landscape) {
            return i + 1;
        }
    }
    return std::nullopt;
}

template <class E>
std::size_t enum_choice(E e, std::size_t n) {
    return std::min<std::size_t>(static_cast<std::size_t>(e), n - 1);
}

template <std::size_t N>
std::optional<std::size_t> name_index(const std::array<std::string_view, N>& names, std::string_view v) {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == v) {
            return i;
        }
    }
    return std::nullopt;
}

std::string_view on_off(bool b) {
    return b ? "on" : "off";
}

/// One "victory.<key>" line: any number, as far as the condition allows it.
bool read_victory_line(sim::VictoryConditions& v, std::string_view key, std::string_view value) {
    const auto n = parse_number(value);
    if (key == "inhabitants") {
        v.inhabitants = n.value_or(v.inhabitants);
    } else if (key == "class") {
        // As the dialog: pioneers are every city's start, so "pioneers or higher" would be won at once.
        if (n && *n >= kFirstWinClass && *n <= kLastWinClass) {
            v.resident_class = static_cast<std::uint8_t>(*n);
        }
    } else if (key == "class_inhabitants") {
        v.class_inhabitants = n.value_or(v.class_inhabitants);
    } else if (key == "money") {
        v.money = n.value_or(v.money);
    } else if (key == "balance") {
        v.balance = n.value_or(v.balance);
    } else if (key == "islands") {
        if (n && *n <= 100) {
            v.islands_percent = static_cast<std::uint8_t>(*n);
        }
    } else if (key == "eliminate") {
        if (value == "on" || value == "off") {
            v.eliminate = value == "on";
        }
    } else if (key == "score") {
        v.score = n.value_or(v.score);
    } else if (key == "monument") {
        if (const auto i = name_index(kMonumentNames, value)) {
            v.monument = static_cast<sim::Monument>(*i);
        }
    } else if (key == "minutes") {
        v.time_limit_minutes = n.value_or(v.time_limit_minutes);
    } else if (key == "combine") {
        if (value == "all" || value == "any") {
            v.all = value == "all";
        }
    } else if (key == "computers") {
        if (value == "on" || value == "off") {
            v.computers = value == "on";
        }
    } else {
        return false;
    }
    return true;
}

}  // namespace

EndlessSetup EndlessSetup::defaults(SetupScope scope) {
    EndlessSetup s;
    if (scope == SetupScope::MultiplayerEndless) {
        s.world.starts = Starts::Together;  // all ships at one place, as the original's multiplayer maps
    }
    return s;
}

EndlessSetup EndlessSetup::original(SetupScope scope) {
    EndlessSetup s = defaults(scope);
    s.rules = sim::Rules::original();
    return s;
}

sim::WorldSettings EndlessSetup::world_settings() const {
    sim::WorldSettings w = world;
    if (auto_size) {
        const sim::WorldSettings sized = sim::settings_for_players(w.players);
        w.width = sized.width;
        w.height = sized.height;
        w = sim::fit_landscape(w);  // room for the largest island of its own size
    }
    return w;
}

// Without the level and the seed: they are chosen apart from the dialog (the level by the player's
// start, the seed anew for every game), and the dialog's "original" and "defaults" buttons compare
// what it shows.
bool operator==(const EndlessSetup& a, const EndlessSetup& b) noexcept {
    const sim::WorldSettings& x = a.world;
    const sim::WorldSettings& y = b.world;
    return a.rules == b.rules && a.own_world == b.own_world && a.auto_size == b.auto_size && x.width == y.width &&
           x.height == y.height && x.players == y.players && x.islands == y.islands && x.sizes == y.sizes &&
           x.natives == y.natives && x.volcanoes == y.volcanoes && x.gold_islands == y.gold_islands &&
           x.starts == y.starts && x.shapes == y.shapes && x.landscape == y.landscape;
}

namespace {

/// The items of the page "Original settings": the bug each switches and its name in settings.cfg
/// ("bug.<name>") and in the texts ("setup.bug.<name>" with ".fixed" and ".original"). The computer
/// players also keep their line ("ai") from before the page.
struct BugEntry {
    SetupItem item;
    bool sim::OriginalBugs::*bug;
    std::string_view name;
};
constexpr BugEntry kBugEntries[] = {
    {SetupItem::BugFireRisk, &sim::OriginalBugs::fire_risk, "fire_risk"},
    {SetupItem::BugGallows, &sim::OriginalBugs::gallows, "gallows"},
    {SetupItem::BugRobberVictims, &sim::OriginalBugs::robber_victims, "robber_victims"},
    {SetupItem::BugRobberFlight, &sim::OriginalBugs::robber_flight, "robber_flight"},
    {SetupItem::BugGoldsheep, &sim::OriginalBugs::goldsheep, "goldsheep"},
    {SetupItem::BugBuildingRadius, &sim::OriginalBugs::building_radius, "building_radius"},
    {SetupItem::BugInfertileFields, &sim::OriginalBugs::infertile_fields, "infertile_fields"},
    {SetupItem::BugRemoteTrade, &sim::OriginalBugs::remote_trade, "remote_trade"},
    {SetupItem::BugRemoteRepair, &sim::OriginalBugs::remote_repair, "remote_repair"},
    {SetupItem::BugSmallYardWarships, &sim::OriginalBugs::small_yard_warships, "small_yard_warships"},
    {SetupItem::BugNativeLand, &sim::OriginalBugs::native_land, "native_land"},
    {SetupItem::ClassicAi, &sim::OriginalBugs::classic_ai, "classic_ai"},
};
static_assert(std::size(kBugEntries) == std::size(sim::OriginalBugs::kAll), "every bug has its item");

const BugEntry* bug_entry(SetupItem item) noexcept {
    for (const BugEntry& e : kBugEntries) {
        if (e.item == item) {
            return &e;
        }
    }
    return nullptr;
}

/// The text key of a bug's item: "setup.bug.<key>".
std::string bug_key(SetupItem item) {
    const BugEntry* e = bug_entry(item);
    return e != nullptr ? "setup.bug." + std::string(e->name) : std::string{};
}

bool sim::OriginalBugs::*bug_named(std::string_view name) noexcept {
    for (const BugEntry& e : kBugEntries) {
        if (e.name == name) {
            return e.bug;
        }
    }
    return nullptr;
}

}  // namespace

bool sim::OriginalBugs::*bug_of(SetupItem item) noexcept {
    const BugEntry* e = bug_entry(item);
    return e != nullptr ? e->bug : nullptr;
}

std::size_t setup_choices(SetupItem item) noexcept {
    switch (item) {
        case SetupItem::Refund:
            return kRefundPercents.size() + 2;
        case SetupItem::Moving:
            return kMovingNames.size();
        case SetupItem::Ships:
            return kShips.size();
        case SetupItem::Units:
            return kUnits.size();
        case SetupItem::GameSpeed:
            return kSpeeds.size();
        case SetupItem::Cheats:
            return 3;
        case SetupItem::Droughts:
            return kDroughtNames.size();
        case SetupItem::Volcanoes:
            return kVolcanoNames.size();
        case SetupItem::Plagues:
        case SetupItem::Fires:
            return kOutbreakNames.size();
        case SetupItem::Pirates:
            return kPirateNames.size();
        case SetupItem::ComputerPlayers:
            return kComputerNames.size();
        case SetupItem::OreVeins:
            return kOreVeinNames.size();
        case SetupItem::PirateNests:
            return kPirateNestNames.size();
        case SetupItem::Reveal:
            return kRevealNames.size();
        case SetupItem::WorldSize:
            return kSizes.size() + 1;  // first: by the players
        case SetupItem::Players:
            return kMostPlayers - kFewestPlayers + 1;
        case SetupItem::Islands:
            return kIslandAmounts.size();
        case SetupItem::IslandSizes:
            return kSizeNames.size();
        case SetupItem::IslandPreset:
            return island_preset_count() + 1;
        case SetupItem::IslandShapes:
            return kShapeNames.size();
        case SetupItem::LandShare:
            return kLandShares.size();
        case SetupItem::SmallestIsland:
            return kSmallest.size();
        case SetupItem::LargestIsland:
            return kLargest.size();
        case SetupItem::SizeSpread:
            return kSpreadNames.size();
        case SetupItem::IslandGap:
            return kGapNames.size();
        case SetupItem::Coast:
            return kCoastNames.size();
        case SetupItem::Outline:
            return kOutlineNames.size();
        case SetupItem::Forest:
        case SetupItem::OreMountains:
            return kShareNames.size();
        case SetupItem::Villages:
        case SetupItem::VolcanoIslands:
            return kAmounts.size();
        case SetupItem::GoldIslands:
            return kGoldIslands.size();
        case SetupItem::TraderPrices:
        case SetupItem::TraderReach:
        case SetupItem::Balance:
        case SetupItem::TradeCarts:
        case SetupItem::FertileFields:
        case SetupItem::ClimateCrops:
        case SetupItem::BigOreVeins:
        case SetupItem::ShipPurchases:
        case SetupItem::Map:
        case SetupItem::Starts:
        case SetupItem::Resources:
        case SetupItem::WinEliminate:
        case SetupItem::WinAll:
        case SetupItem::WinComputers:
        case SetupItem::BugFireRisk:
        case SetupItem::BugGallows:
        case SetupItem::BugRobberVictims:
        case SetupItem::BugRobberFlight:
        case SetupItem::BugGoldsheep:
        case SetupItem::BugBuildingRadius:
        case SetupItem::BugInfertileFields:
        case SetupItem::BugRemoteTrade:
        case SetupItem::BugRemoteRepair:
        case SetupItem::BugSmallYardWarships:
        case SetupItem::BugNativeLand:
        case SetupItem::ClassicAi:
            return 2;
        case SetupItem::WinInhabitants:
            return kWinInhabitants.size();
        case SetupItem::WinClass:
            return kLastWinClass - kFirstWinClass + 1;
        case SetupItem::WinClassInhabitants:
            return kWinClassInhabitants.size();
        case SetupItem::WinMoney:
            return kWinMoney.size();
        case SetupItem::WinBalance:
            return kWinBalance.size();
        case SetupItem::WinIslands:
            return kWinIslands.size();
        case SetupItem::WinScore:
            return kWinScore.size();
        case SetupItem::WinMonument:
            return kMonumentNames.size();
        case SetupItem::WinTimeLimit:
            return kWinMinutes.size();
        case SetupItem::Count:
            break;
    }
    return 0;
}

std::size_t setup_choice(const EndlessSetup& setup, SetupItem item) noexcept {
    const sim::Rules& r = setup.rules;
    const sim::VictoryConditions& v = r.victory;
    const sim::WorldSettings& w = setup.world;
    // A condition that is on shows the nearest value that is on too, not "off".
    const auto condition = [](const auto& values, std::uint32_t value) {
        return value == 0 ? std::size_t{0} : std::max<std::size_t>(nearest(values, value), 1);
    };
    switch (item) {
        case SetupItem::Refund: {
            switch (r.demolish_refund) {
                case sim::Refund::None:
                    return 0;
                case sim::Refund::Full:
                    return kRefundPercents.size() + 1;
                case sim::Refund::Partial:
                    break;
            }
            std::array<std::uint32_t, kRefundPercents.size()> percents{};
            std::copy(kRefundPercents.begin(), kRefundPercents.end(), percents.begin());
            return 1 + nearest(percents, std::max<std::uint32_t>(r.refund_percent, 1));
        }
        case SetupItem::Moving:
            return std::min<std::size_t>(static_cast<std::size_t>(r.moving), kMovingNames.size() - 1);
        case SetupItem::TraderPrices:
            return r.trader_prices == sim::TraderPrices::Market ? 1 : 0;
        case SetupItem::TraderReach:
            return r.trader_reach == sim::TraderReach::Scaled ? 1 : 0;
        case SetupItem::OreVeins:
            return std::min<std::size_t>(static_cast<std::size_t>(r.ore_veins), kOreVeinNames.size() - 1);
        case SetupItem::Balance:
            return r.revised_bread && r.wider_hunting ? 1 : 0;
        case SetupItem::TradeCarts:
            return r.more_carts ? 1 : 0;
        case SetupItem::FertileFields:
            return r.fertile_fields ? 1 : 0;
        case SetupItem::ClimateCrops:
            return r.climate_crops ? 1 : 0;
        case SetupItem::BigOreVeins:
            return r.big_ore_veins ? 1 : 0;
        case SetupItem::ShipPurchases:
            return r.computers_buy_ships ? 1 : 0;
        case SetupItem::Ships:
            return nearest(kShips, r.max_ships_per_player);
        case SetupItem::Units:
            return nearest(kUnits, r.max_army);
        case SetupItem::GameSpeed:
            return nearest(kSpeeds, r.max_game_speed);
        case SetupItem::Cheats:
            return std::min<std::size_t>(static_cast<std::size_t>(r.cheats), 2);
        case SetupItem::Droughts:
            return std::min<std::size_t>(static_cast<std::size_t>(r.droughts), kDroughtNames.size() - 1);
        case SetupItem::Volcanoes:
            return enum_choice(r.volcanoes, kVolcanoNames.size());
        case SetupItem::Plagues:
            return enum_choice(r.plagues, kOutbreakNames.size());
        case SetupItem::Fires:
            return enum_choice(r.fires, kOutbreakNames.size());
        case SetupItem::Map:
            return setup.own_world ? 1 : 0;
        case SetupItem::WorldSize: {
            if (setup.auto_size) {
                return 0;
            }
            // Two sizes share a width: the nearest by area (in thousands of tiles, which keeps them apart).
            std::array<std::uint32_t, kSizes.size()> areas{};
            for (std::size_t i = 0; i < kSizes.size(); ++i) {
                areas[i] = kSizes[i].first * kSizes[i].second / 1000;
            }
            return 1 + nearest(areas, std::max<std::uint32_t>(w.width * w.height / 1000, 1));
        }
        case SetupItem::Players:
            return static_cast<std::size_t>(std::clamp(w.players, kFewestPlayers, kMostPlayers) - kFewestPlayers);
        case SetupItem::Islands:
            return w.islands == Amount::None ? 0 : index_of(kIslandAmounts, w.islands);
        case SetupItem::IslandSizes:
            return std::min<std::size_t>(static_cast<std::size_t>(w.sizes), kSizeNames.size() - 1);
        case SetupItem::IslandPreset:
            return island_preset(w).value_or(0);
        case SetupItem::IslandShapes:
            return enum_choice(w.shapes, kShapeNames.size());
        case SetupItem::LandShare:
            return nearest(kLandShares, w.landscape.land);
        case SetupItem::SmallestIsland:
            return nearest(kSmallest, w.landscape.min_side);
        case SetupItem::LargestIsland:
            return nearest(kLargest, w.landscape.max_side);
        case SetupItem::SizeSpread:
            return enum_choice(w.landscape.spread, kSpreadNames.size());
        case SetupItem::IslandGap:
            return enum_choice(w.landscape.gap, kGapNames.size());
        case SetupItem::Coast:
            return enum_choice(w.landscape.coast, kCoastNames.size());
        case SetupItem::Outline:
            return enum_choice(w.landscape.outline, kOutlineNames.size());
        case SetupItem::Forest:
            return enum_choice(w.landscape.forest, kShareNames.size());
        case SetupItem::OreMountains:
            return enum_choice(w.landscape.mountains, kShareNames.size());
        case SetupItem::Villages:
            return index_of(kAmounts, w.natives);
        case SetupItem::VolcanoIslands:
            return index_of(kAmounts, w.volcanoes);
        case SetupItem::GoldIslands:
            return nearest(kGoldIslands, w.gold_islands);
        case SetupItem::Starts:
            return w.starts == Starts::Together ? 1 : 0;
        case SetupItem::Resources:
            return r.guarantee_resources ? 0 : 1;
        case SetupItem::BugFireRisk:
        case SetupItem::BugGallows:
        case SetupItem::BugRobberVictims:
        case SetupItem::BugRobberFlight:
        case SetupItem::BugGoldsheep:
        case SetupItem::BugBuildingRadius:
        case SetupItem::BugInfertileFields:
        case SetupItem::BugRemoteTrade:
        case SetupItem::BugRemoteRepair:
        case SetupItem::BugSmallYardWarships:
        case SetupItem::BugNativeLand:
        case SetupItem::ClassicAi: {
            const auto bug = bug_of(item);
            return bug != nullptr && r.bugs.*bug ? 1 : 0;
        }
        case SetupItem::ComputerPlayers:
            return std::min<std::size_t>(static_cast<std::size_t>(r.computer_players), kComputerNames.size() - 1);
        case SetupItem::Pirates:
            return std::min<std::size_t>(static_cast<std::size_t>(r.pirates), kPirateNames.size() - 1);
        case SetupItem::PirateNests:
            return std::min<std::size_t>(static_cast<std::size_t>(r.pirate_nests), kPirateNestNames.size() - 1);
        case SetupItem::Reveal:
            return std::min<std::size_t>(static_cast<std::size_t>(r.reveal), kRevealNames.size() - 1);
        case SetupItem::WinInhabitants:
            return condition(kWinInhabitants, v.inhabitants);
        case SetupItem::WinClass:
            return static_cast<std::size_t>(std::clamp(v.resident_class, kFirstWinClass, kLastWinClass) -
                                            kFirstWinClass);
        case SetupItem::WinClassInhabitants:
            return condition(kWinClassInhabitants, v.class_inhabitants);
        case SetupItem::WinMoney:
            return condition(kWinMoney, v.money);
        case SetupItem::WinBalance:
            return condition(kWinBalance, v.balance);
        case SetupItem::WinIslands:
            return condition(kWinIslands, v.islands_percent);
        case SetupItem::WinEliminate:
            return v.eliminate ? 1 : 0;
        case SetupItem::WinScore:
            return condition(kWinScore, v.score);
        case SetupItem::WinMonument:
            return std::min<std::size_t>(static_cast<std::size_t>(v.monument), kMonumentNames.size() - 1);
        case SetupItem::WinTimeLimit:
            return condition(kWinMinutes, v.time_limit_minutes);
        case SetupItem::WinAll:
            return v.all ? 0 : 1;
        case SetupItem::WinComputers:
            return v.computers ? 0 : 1;
        case SetupItem::Count:
            break;
    }
    return 0;
}

void choose(EndlessSetup& setup, SetupItem item, std::size_t choice) {
    if (choice >= setup_choices(item)) {
        return;
    }
    sim::Rules& r = setup.rules;
    sim::VictoryConditions& v = r.victory;
    sim::WorldSettings& w = setup.world;
    switch (item) {
        case SetupItem::Refund:
            if (choice == 0) {
                r.demolish_refund = sim::Refund::None;
            } else if (choice > kRefundPercents.size()) {
                r.demolish_refund = sim::Refund::Full;
            } else {
                r.demolish_refund = sim::Refund::Partial;
                r.refund_percent = kRefundPercents[choice - 1];
            }
            break;
        case SetupItem::Moving:
            r.moving = static_cast<sim::Moving>(choice);
            break;
        case SetupItem::TraderPrices:
            r.trader_prices = choice == 1 ? sim::TraderPrices::Market : sim::TraderPrices::Original;
            break;
        case SetupItem::TraderReach:
            r.trader_reach = choice == 1 ? sim::TraderReach::Scaled : sim::TraderReach::Original;
            break;
        case SetupItem::OreVeins:
            r.ore_veins = static_cast<sim::OreVeins>(choice);
            break;
        case SetupItem::Balance:
            r.revised_bread = choice == 1;
            r.wider_hunting = choice == 1;
            break;
        case SetupItem::TradeCarts:
            r.more_carts = choice == 1;
            break;
        case SetupItem::FertileFields:
            r.fertile_fields = choice == 1;
            break;
        case SetupItem::ClimateCrops:
            r.climate_crops = choice == 1;
            break;
        case SetupItem::BigOreVeins:
            r.big_ore_veins = choice == 1;
            break;
        case SetupItem::ShipPurchases:
            r.computers_buy_ships = choice == 1;
            break;
        case SetupItem::Ships:
            r.max_ships_per_player = kShips[choice];
            break;
        case SetupItem::Units:
            r.max_army = kUnits[choice];
            break;
        case SetupItem::GameSpeed:
            r.max_game_speed = kSpeeds[choice];
            break;
        case SetupItem::Cheats:
            r.cheats = static_cast<sim::Cheats>(std::min<std::size_t>(choice, 2));
            break;
        case SetupItem::Droughts:
            r.droughts = static_cast<sim::Droughts>(choice);
            break;
        case SetupItem::Volcanoes:
            r.volcanoes = static_cast<sim::Volcanoes>(choice);
            break;
        case SetupItem::Plagues:
            r.plagues = static_cast<sim::Outbreaks>(choice);
            break;
        case SetupItem::Fires:
            r.fires = static_cast<sim::Outbreaks>(choice);
            break;
        case SetupItem::Map:
            setup.own_world = choice == 1;
            break;
        case SetupItem::WorldSize:
            setup.auto_size = choice == 0;
            if (choice > 0) {
                w.width = kSizes[choice - 1].first;
                w.height = kSizes[choice - 1].second;
            }
            break;
        case SetupItem::Players:
            w.players = static_cast<std::uint8_t>(kFewestPlayers + choice);
            break;
        case SetupItem::Islands:
            w.islands = kIslandAmounts[choice];
            break;
        case SetupItem::IslandSizes:
            w.sizes = static_cast<Sizes>(choice);
            break;
        case SetupItem::IslandPreset:
            if (choice == 0) {
                w.shapes = Shapes::Templates;
            } else {
                w.shapes = Shapes::Own;
                w.landscape = island_preset_landscape(std::min(choice, island_preset_count()) - 1);
            }
            break;
        case SetupItem::IslandShapes:
            w.shapes = static_cast<Shapes>(std::min<std::size_t>(choice, kShapeNames.size() - 1));
            break;
        case SetupItem::LandShare:
            w.landscape.land = static_cast<std::uint8_t>(kLandShares[choice]);
            break;
        case SetupItem::SmallestIsland: {
            // The largest island at least as large: the next side offered.
            Landscape& l = w.landscape;
            l.min_side = static_cast<std::uint16_t>(kSmallest[choice]);
            if (l.max_side < l.min_side) {
                l.max_side = static_cast<std::uint16_t>(*std::find_if(
                    kLargest.begin(), kLargest.end(), [&](std::uint32_t side) { return side >= l.min_side; }));
            }
            break;
        }
        case SetupItem::LargestIsland: {
            Landscape& l = w.landscape;
            l.max_side = static_cast<std::uint16_t>(kLargest[choice]);
            if (l.min_side > l.max_side) {
                l.min_side = static_cast<std::uint16_t>(*std::find_if(
                    kSmallest.rbegin(), kSmallest.rend(), [&](std::uint32_t side) { return side <= l.max_side; }));
            }
            break;
        }
        case SetupItem::SizeSpread:
            w.landscape.spread = static_cast<Landscape::Spread>(choice);
            break;
        case SetupItem::IslandGap:
            w.landscape.gap = static_cast<Landscape::Gap>(choice);
            break;
        case SetupItem::Coast:
            w.landscape.coast = static_cast<Roughness>(choice);
            break;
        case SetupItem::Outline:
            w.landscape.outline = static_cast<Landscape::Outline>(choice);
            break;
        case SetupItem::Forest:
            w.landscape.forest = static_cast<Share>(choice);
            break;
        case SetupItem::OreMountains:
            w.landscape.mountains = static_cast<Share>(choice);
            break;
        case SetupItem::Villages:
            w.natives = kAmounts[choice];
            break;
        case SetupItem::VolcanoIslands:
            w.volcanoes = kAmounts[choice];
            break;
        case SetupItem::GoldIslands:
            w.gold_islands = static_cast<std::uint8_t>(kGoldIslands[choice]);
            break;
        case SetupItem::Starts:
            w.starts = choice == 1 ? Starts::Together : Starts::Spread;
            break;
        case SetupItem::Resources:
            r.guarantee_resources = choice == 0;
            break;
        case SetupItem::BugFireRisk:
        case SetupItem::BugGallows:
        case SetupItem::BugRobberVictims:
        case SetupItem::BugRobberFlight:
        case SetupItem::BugGoldsheep:
        case SetupItem::BugBuildingRadius:
        case SetupItem::BugInfertileFields:
        case SetupItem::BugRemoteTrade:
        case SetupItem::BugRemoteRepair:
        case SetupItem::BugSmallYardWarships:
        case SetupItem::BugNativeLand:
        case SetupItem::ClassicAi:
            if (const auto bug = bug_of(item); bug != nullptr) {
                r.bugs.*bug = choice == 1;
            }
            break;
        case SetupItem::ComputerPlayers:
            r.computer_players = static_cast<sim::Computers>(choice);
            break;
        case SetupItem::Pirates:
            r.pirates = static_cast<sim::Pirates>(choice);
            break;
        case SetupItem::PirateNests:
            r.pirate_nests = static_cast<sim::PirateNests>(choice);
            break;
        case SetupItem::Reveal:
            r.reveal = static_cast<sim::Reveal>(choice);
            break;
        case SetupItem::WinInhabitants:
            v.inhabitants = kWinInhabitants[choice];
            break;
        case SetupItem::WinClass:
            v.resident_class = static_cast<std::uint8_t>(kFirstWinClass + choice);
            break;
        case SetupItem::WinClassInhabitants:
            v.class_inhabitants = kWinClassInhabitants[choice];
            break;
        case SetupItem::WinMoney:
            v.money = kWinMoney[choice];
            break;
        case SetupItem::WinBalance:
            v.balance = kWinBalance[choice];
            break;
        case SetupItem::WinIslands:
            v.islands_percent = static_cast<std::uint8_t>(kWinIslands[choice]);
            break;
        case SetupItem::WinEliminate:
            v.eliminate = choice == 1;
            break;
        case SetupItem::WinScore:
            v.score = kWinScore[choice];
            break;
        case SetupItem::WinMonument:
            v.monument = static_cast<sim::Monument>(choice);
            break;
        case SetupItem::WinTimeLimit:
            v.time_limit_minutes = kWinMinutes[choice];
            break;
        case SetupItem::WinAll:
            v.all = choice == 0;
            break;
        case SetupItem::WinComputers:
            v.computers = choice == 0;
            break;
        case SetupItem::Count:
            break;
    }
}

void choose_next(EndlessSetup& setup, SetupItem item) {
    const std::size_t n = setup_choices(item);
    if (n != 0) {
        // From a value of its own the first step goes to the nearest offered one (one players: 2).
        const std::size_t step = setup_value_custom(setup, item) ? 0 : 1;
        choose(setup, item, (setup_choice(setup, item) + step) % n);
    }
}

void choose_previous(EndlessSetup& setup, SetupItem item) {
    const std::size_t n = setup_choices(item);
    if (n != 0) {
        const std::size_t step = setup_value_custom(setup, item) ? 0 : n - 1;
        choose(setup, item, (setup_choice(setup, item) + step) % n);
    }
}

bool setup_item_used(const EndlessSetup& setup, SetupItem item) noexcept {
    switch (item) {
        case SetupItem::Islands:
        case SetupItem::IslandSizes:
            // Islands of their own size follow the page of the islands.
            return setup.own_world && setup.world.shapes != Shapes::Own;
        case SetupItem::LandShare:
        case SetupItem::SmallestIsland:
        case SetupItem::LargestIsland:
        case SetupItem::SizeSpread:
        case SetupItem::IslandGap:
        case SetupItem::Coast:
        case SetupItem::Outline:
        case SetupItem::Forest:
        case SetupItem::OreMountains:
            return setup.own_world && setup.world.shapes == Shapes::Own;
        case SetupItem::WorldSize:
        case SetupItem::Players:
        case SetupItem::IslandPreset:
        case SetupItem::IslandShapes:
        case SetupItem::Villages:
        case SetupItem::VolcanoIslands:
        case SetupItem::GoldIslands:
        case SetupItem::Starts:
            return setup.own_world;
        case SetupItem::WinClass:
            return setup.rules.victory.class_inhabitants != 0;
        case SetupItem::WinAll:
            return std::popcount(sim::victory_required(setup.rules.victory)) >= 2;
        case SetupItem::WinComputers:
            return setup.rules.victory.active();
        default:
            return item != SetupItem::Count;
    }
}

bool victory_offered(SetupScope scope) noexcept {
    return scope != SetupScope::Scenario;
}

std::size_t victory_count(const sim::VictoryConditions& v) noexcept {
    return static_cast<std::size_t>(std::popcount(sim::victory_required(v))) + (v.time_limit_minutes != 0 ? 1U : 0U);
}

std::string victory_summary(const sim::VictoryConditions& v) {
    const std::size_t n = victory_count(v);
    if (n == 0) {
        return ui::tr("setup.victory.none");
    }
    return ui::tr(n == 1 ? "setup.victory.one" : "setup.victory.many", {std::to_string(n)});
}

sim::OriginalBugs page_bugs(sim::OriginalBugs bugs) noexcept {
    bugs.classic_ai = false;
    return bugs;
}

sim::Rules original_disasters(sim::Rules rules) noexcept {
    const sim::Rules original = sim::Rules::original();
    rules.droughts = original.droughts;
    rules.volcanoes = original.volcanoes;
    rules.plagues = original.plagues;
    rules.fires = original.fires;
    return rules;
}

std::string disasters_summary(const sim::Rules& rules) {
    const sim::Rules original = original_disasters(rules);
    const std::size_t n = (rules.droughts != original.droughts ? 1U : 0U) +
                          (rules.volcanoes != original.volcanoes ? 1U : 0U) +
                          (rules.plagues != original.plagues ? 1U : 0U) + (rules.fires != original.fires ? 1U : 0U);
    if (n == 0) {
        return ui::tr("setup.disasters.original");
    }
    return ui::tr(n == 1 ? "setup.disasters.one" : "setup.disasters.many", {std::to_string(n)});
}

std::string islands_summary(const EndlessSetup& setup) {
    return setup_value(setup, SetupItem::IslandPreset);
}

std::string bugs_summary(const sim::OriginalBugs& bugs) {
    const std::uint32_t n = page_bugs(bugs).count();
    if (n == 0) {
        return ui::tr("setup.bugs.none");
    }
    return ui::tr(n == 1 ? "setup.bugs.one" : "setup.bugs.many", {std::to_string(n)});
}

std::string victory_text(const sim::VictoryConditions& v) {
    if (!v.active()) {
        return {};
    }
    std::string parts;
    const auto append = [&parts](const std::string& part) {
        parts = parts.empty() ? part : ui::tr("setup.victory.next", {parts, part});
    };
    const auto add = [&](SetupItem item, const std::string& value) {
        append(ui::tr("setup.victory.part", {setup_label(item), value}));
    };
    if (v.inhabitants != 0) {
        add(SetupItem::WinInhabitants, grouped(v.inhabitants));
    }
    if (v.class_inhabitants != 0) {
        const auto cls =
            static_cast<std::size_t>(std::clamp(v.resident_class, kFirstWinClass, kLastWinClass) - kFirstWinClass);
        add(SetupItem::WinClass,
            ui::tr("setup.win.class.text", {grouped(v.class_inhabitants), setup_value(SetupItem::WinClass, cls)}));
    }
    if (v.money != 0) {
        add(SetupItem::WinMoney, ui::tr("setup.win.gold", {grouped(v.money)}));
    }
    if (v.balance != 0) {
        add(SetupItem::WinBalance, ui::tr("setup.win.balance.value", {grouped(v.balance)}));
    }
    if (v.islands_percent != 0) {
        add(SetupItem::WinIslands, ui::tr("setup.win.percent", {std::to_string(v.islands_percent)}));
    }
    if (v.eliminate) {
        append(setup_label(SetupItem::WinEliminate));
    }
    if (v.score != 0) {
        add(SetupItem::WinScore, grouped(v.score));
    }
    if (v.monument != sim::Monument::None) {
        add(SetupItem::WinMonument, setup_value(SetupItem::WinMonument, static_cast<std::size_t>(v.monument)));
    }
    if (v.time_limit_minutes != 0) {
        const std::uint32_t m = v.time_limit_minutes;
        add(SetupItem::WinTimeLimit, m % 60 == 0 ? ui::tr("setup.win.hours", {std::to_string(m / 60)})
                                                 : ui::tr("setup.win.minutes", {std::to_string(m)}));
    }
    if (std::popcount(sim::victory_required(v)) >= 2) {
        add(SetupItem::WinAll, setup_value(SetupItem::WinAll, v.all ? 0 : 1));
    }
    if (!v.computers) {
        add(SetupItem::WinComputers, ui::tr("dialog.no"));
    }
    return ui::tr("setup.victory.text", {parts});
}

bool setup_item_offered(SetupScope scope, SetupItem item) noexcept {
    if (victory_item(item)) {
        return victory_offered(scope);
    }
    switch (scope) {
        case SetupScope::Endless:
        case SetupScope::MultiplayerEndless:
            return item != SetupItem::Count;
        case SetupScope::Scenario:
        case SetupScope::Savegame:
            break;
    }
    // A scenario's world is given: no world of its own, and its deposits are as its author placed them.
    switch (item) {
        case SetupItem::Refund:
        case SetupItem::Moving:
        case SetupItem::TraderPrices:
        case SetupItem::TraderReach:
        case SetupItem::OreVeins:
        case SetupItem::Balance:
        case SetupItem::TradeCarts:
        case SetupItem::FertileFields:
        case SetupItem::ShipPurchases:
        case SetupItem::Ships:
        case SetupItem::Units:
        case SetupItem::GameSpeed:
        case SetupItem::Cheats:
        case SetupItem::Droughts:
        case SetupItem::Volcanoes:
        case SetupItem::Plagues:
        case SetupItem::Fires:
        case SetupItem::ComputerPlayers:
        case SetupItem::Pirates:
        case SetupItem::PirateNests:
        case SetupItem::Reveal:
        case SetupItem::BugFireRisk:
        case SetupItem::BugGallows:
        case SetupItem::BugRobberVictims:
        case SetupItem::BugRobberFlight:
        case SetupItem::BugGoldsheep:
        case SetupItem::BugBuildingRadius:
        case SetupItem::BugInfertileFields:
        case SetupItem::BugRemoteTrade:
        case SetupItem::BugRemoteRepair:
        case SetupItem::BugSmallYardWarships:
        case SetupItem::BugNativeLand:
        case SetupItem::ClassicAi:
            return true;
        default:
            return false;
    }
}

bool setup_item_open(const EndlessSetup& setup, SetupScope scope, SetupItem item) noexcept {
    if (scope == SetupScope::Savegame || !setup_item_offered(scope, item)) {
        return false;
    }
    if (scope == SetupScope::MultiplayerEndless) {
        if (item == SetupItem::Players) {
            return false;  // the lobby's slots
        }
        if (item == SetupItem::Map && setup.world.players > sim::kOriginalPlayers) {
            return false;  // the original's maps have starts for four
        }
    }
    return setup_item_used(setup, item);
}

bool same_setup(const EndlessSetup& a, const EndlessSetup& b, SetupScope scope) noexcept {
    switch (scope) {
        case SetupScope::Endless:
        case SetupScope::MultiplayerEndless:
            return a == b;
        case SetupScope::Scenario:
        case SetupScope::Savegame:
            break;
    }
    return a.rules == b.rules;
}

EndlessSetup multiplayer_setup(EndlessSetup setup, std::uint8_t players) {
    setup.world.players = static_cast<std::uint8_t>(std::clamp<std::size_t>(players, 1, sim::kMaxPlayers));
    if (setup.world.players > sim::kOriginalPlayers) {
        setup.own_world = true;
    }
    // Every computer runs every tick in step with the slowest: no more than the dialog offers, for
    // which the tick budget holds (LC-8). A game alone may go faster through settings.cfg.
    setup.rules.max_game_speed = std::clamp<std::uint32_t>(setup.rules.max_game_speed, 1, kSpeeds.back());
    return setup;
}

std::string setup_label(SetupItem item) {
    static constexpr std::array<std::string_view, kSetupItems> kKeys = {
        "setup.refund",
        "setup.moving",
        "setup.trader",
        "setup.trader_reach",
        "setup.ore_veins",
        "setup.balance",
        "setup.trade_carts",
        "setup.ships",
        "setup.units",
        "setup.speed",
        "setup.fertile_fields",
        "setup.droughts",
        "setup.volcanoes",
        "setup.plagues",
        "setup.fires",
        "setup.map",
        "setup.size",
        "setup.players",
        "setup.islands",
        "setup.island_sizes",
        "setup.villages",
        "setup.volcano_islands",
        "setup.gold_islands",
        "setup.starts",
        "setup.resources",
        "setup.computers",
        "setup.bug.classic_ai",
        "setup.ship_purchases",
        "setup.pirates",
        "setup.pirate_nests",
        "setup.reveal",
        "setup.cheats",
        "setup.island_preset",
        "setup.island_shapes",
        "setup.coast",
        "setup.outline",
        "setup.land",
        "setup.smallest_island",
        "setup.largest_island",
        "setup.size_spread",
        "setup.island_gap",
        "setup.forest",
        "setup.ore_mountains",
        "setup.climate_crops",
        "setup.big_ore_veins",
        "setup.win.inhabitants",
        "setup.win.class",
        "setup.win.class_inhabitants",
        "setup.win.money",
        "setup.win.balance",
        "setup.win.islands",
        "setup.win.eliminate",
        "setup.win.score",
        "setup.win.monument",
        "setup.win.time",
        "setup.win.all",
        "setup.win.computers",
        "setup.bug.fire_risk",
        "setup.bug.gallows",
        "setup.bug.robber_victims",
        "setup.bug.robber_flight",
        "setup.bug.goldsheep",
        "setup.bug.building_radius",
        "setup.bug.infertile_fields",
        "setup.bug.remote_trade",
        "setup.bug.remote_repair",
        "setup.bug.small_yard_warships",
        "setup.bug.native_land",
    };
    static_assert(kKeys.size() == kSetupItems);
    const auto i = static_cast<std::size_t>(item);
    return i < kKeys.size() ? ui::tr(kKeys[i]) : std::string{};
}

std::string setup_value(SetupItem item, std::size_t choice) {
    if (choice >= setup_choices(item)) {
        return {};
    }
    switch (item) {
        case SetupItem::Refund:
            if (choice == 0) {
                return ui::tr("setup.refund.none");
            }
            if (choice > kRefundPercents.size()) {
                return ui::tr("setup.refund.full");
            }
            return ui::tr("setup.refund.partial", {std::to_string(kRefundPercents[choice - 1])});
        case SetupItem::Moving: {
            static constexpr std::array<std::string_view, 3> kKeys = {"setup.moving.free", "setup.moving.rebuild",
                                                                      "setup.off"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::TraderPrices:
            return ui::tr(choice == 1 ? "setup.trader.market" : "setup.trader.original");
        case SetupItem::TraderReach:
            return ui::tr(choice == 1 ? "setup.trader_reach.per4" : "setup.trader_reach.original");
        case SetupItem::OreVeins: {
            static constexpr std::array<std::string_view, 3> kKeys = {"setup.ore_veins.game", "setup.ore_veins.finite",
                                                                      "setup.ore_veins.endless"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::Balance:
            return ui::tr(choice == 1 ? "setup.on" : "setup.off");
        case SetupItem::TradeCarts:
            return ui::tr(choice == 1 ? "setup.trade_carts.more" : "setup.trade_carts.one");
        case SetupItem::FertileFields:
            return ui::tr(choice == 1 ? "setup.fertile_fields.all" : "setup.fertile_fields.original");
        case SetupItem::ClimateCrops:
            return ui::tr(choice == 1 ? "setup.climate_crops.all" : "setup.climate_crops.original");
        case SetupItem::BigOreVeins:
            return ui::tr(choice == 1 ? "setup.big_ore_veins.all" : "setup.big_ore_veins.original");
        case SetupItem::ShipPurchases:
            return ui::tr(choice == 1 ? "setup.on" : "setup.off");
        case SetupItem::Ships:
            return number_text(kShips[choice]);
        case SetupItem::Units:
            return number_text(kUnits[choice]);
        case SetupItem::GameSpeed:
            return ui::tr("setup.speed.value", {std::to_string(kSpeeds[choice])});
        case SetupItem::Cheats: {
            static constexpr std::array<std::string_view, 3> kKeys = {"setup.cheats.code", "setup.cheats.start",
                                                                      "setup.off"};
            return ui::tr(kKeys[std::min<std::size_t>(choice, 2)]);
        }
        case SetupItem::Droughts: {
            static constexpr std::array<std::string_view, 5> kKeys = {"setup.droughts.game", "setup.off",
                                                                      "setup.droughts.rare", "setup.droughts.normal",
                                                                      "setup.droughts.frequent"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::Volcanoes: {
            static constexpr std::array<std::string_view, 4> kKeys = {"setup.volcanoes.game", "setup.off", "setup.on",
                                                                      "setup.volcanoes.rare"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::Plagues:
        case SetupItem::Fires: {
            static constexpr std::array<std::string_view, 3> kKeys = {"setup.outbreaks.original",
                                                                      "setup.outbreaks.rare", "setup.off"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::Map:
            return ui::tr(choice == 1 ? "setup.map.own" : "setup.map.original");
        case SetupItem::WorldSize:
            if (choice == 0) {
                return ui::tr("setup.size.auto", {"500", "350"});
            }
            return ui::tr("setup.size.value",
                          {std::to_string(kSizes[choice - 1].first), std::to_string(kSizes[choice - 1].second)});
        case SetupItem::Players:
            return std::to_string(kFewestPlayers + choice);
        case SetupItem::Islands:
            return amount_text(kIslandAmounts[choice]);
        case SetupItem::IslandSizes: {
            static constexpr std::array<std::string_view, 3> kKeys = {
                "setup.island_sizes.small", "setup.island_sizes.mixed", "setup.island_sizes.large"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::IslandShapes: {
            static constexpr std::array<std::string_view, 3> kKeys = {
                "setup.island_shapes.templates", "setup.island_shapes.mixed", "setup.island_shapes.own"};
            return ui::tr(kKeys[std::min<std::size_t>(choice, kKeys.size() - 1)]);
        }
        case SetupItem::IslandPreset:
            if (choice > kIslandPresets.size() && choice <= island_preset_count()) {
                return package_presets()[choice - 1 - kIslandPresets.size()].title(ui::current_language());
            }
            return ui::tr(choice == 0 ? std::string("setup.island_preset.original")
                                      : "setup.island_preset." + std::string(kIslandPresets[choice - 1].name));
        case SetupItem::LandShare:
            return ui::tr("setup.win.percent", {std::to_string(kLandShares[choice])});
        case SetupItem::SmallestIsland:
            return ui::tr("setup.tiles", {std::to_string(kSmallest[choice])});
        case SetupItem::LargestIsland:
            return ui::tr("setup.tiles", {std::to_string(kLargest[choice])});
        case SetupItem::SizeSpread:
            return ui::tr("setup.size_spread." + std::string(kSpreadNames[choice]));
        case SetupItem::IslandGap:
            return ui::tr("setup.island_gap." + std::string(kGapNames[choice]));
        case SetupItem::Coast:
            return ui::tr("setup.coast." + std::string(kCoastNames[choice]));
        case SetupItem::Outline:
            return ui::tr("setup.outline." + std::string(kOutlineNames[choice]));
        case SetupItem::Forest:
        case SetupItem::OreMountains:
            return ui::tr("setup.share." + std::string(kShareNames[choice]));
        case SetupItem::Villages:
        case SetupItem::VolcanoIslands:
            return amount_text(kAmounts[choice]);
        case SetupItem::GoldIslands:
            return choice == 0 ? ui::tr("setup.gold_islands.auto") : std::to_string(kGoldIslands[choice]);
        case SetupItem::Starts:
            return ui::tr(choice == 1 ? "setup.starts.together" : "setup.starts.spread");
        case SetupItem::Resources:
            return ui::tr(choice == 0 ? "setup.on" : "setup.off");
        case SetupItem::BugFireRisk:
        case SetupItem::BugGallows:
        case SetupItem::BugRobberVictims:
        case SetupItem::BugRobberFlight:
        case SetupItem::BugGoldsheep:
        case SetupItem::BugBuildingRadius:
        case SetupItem::BugInfertileFields:
        case SetupItem::BugRemoteTrade:
        case SetupItem::BugRemoteRepair:
        case SetupItem::BugSmallYardWarships:
        case SetupItem::BugNativeLand:
        case SetupItem::ClassicAi:
            return ui::tr(std::string(bug_key(item)) + (choice == 1 ? ".original" : ".fixed"));
        case SetupItem::ComputerPlayers: {
            static constexpr std::array<std::string_view, 3> kKeys = {"setup.computers.game", "setup.off", "setup.on"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::Pirates: {
            static constexpr std::array<std::string_view, 3> kKeys = {"setup.pirates.game", "setup.off", "setup.on"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::PirateNests: {
            static constexpr std::array<std::string_view, 2> kKeys = {"setup.pirate_nests.one",
                                                                      "setup.pirate_nests.per4"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::Reveal: {
            static constexpr std::array<std::string_view, 3> kKeys = {"setup.reveal.none", "setup.reveal.original",
                                                                      "setup.reveal.all"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::WinInhabitants:
            return condition_text(kWinInhabitants, choice, "setup.win.number");
        case SetupItem::WinClass: {
            static constexpr std::array<std::string_view, 4> kKeys = {
                "setup.win.class.settlers", "setup.win.class.citizens", "setup.win.class.merchants",
                "setup.win.class.aristocrats"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::WinClassInhabitants:
            return condition_text(kWinClassInhabitants, choice, "setup.win.number");
        case SetupItem::WinMoney:
            return condition_text(kWinMoney, choice, "setup.win.gold");
        case SetupItem::WinBalance:
            return condition_text(kWinBalance, choice, "setup.win.balance.value");
        case SetupItem::WinIslands:
            return condition_text(kWinIslands, choice, "setup.win.percent");
        case SetupItem::WinEliminate:
            return ui::tr(choice == 1 ? "setup.on" : "setup.off");
        case SetupItem::WinScore:
            return condition_text(kWinScore, choice, "setup.win.number");
        case SetupItem::WinMonument: {
            static constexpr std::array<std::string_view, 4> kKeys = {
                "setup.off", "setup.win.monument.castle", "setup.win.monument.cathedral", "setup.win.monument.both"};
            return ui::tr(kKeys[choice]);
        }
        case SetupItem::WinTimeLimit:
            return kWinMinutes[choice] == 0 ? ui::tr("setup.off")
                                            : ui::tr("setup.win.hours", {std::to_string(kWinMinutes[choice] / 60)});
        case SetupItem::WinAll:
            return ui::tr(choice == 0 ? "setup.win.all.every" : "setup.win.all.any");
        case SetupItem::WinComputers:
            return ui::tr(choice == 0 ? "dialog.yes" : "dialog.no");
        case SetupItem::Count:
            break;
    }
    return {};
}

namespace {

template <class T, std::size_t N>
bool offered(const std::array<T, N>& values, T v) {
    return std::find(values.begin(), values.end(), v) != values.end();
}

/// The setup's own value as text when none of the item's choices is exactly it (settings.cfg or a
/// host may set any), or nothing: the dialog shows what the game will use (LC-7).
std::optional<std::string> custom_value(const EndlessSetup& setup, SetupItem item) {
    const sim::Rules& r = setup.rules;
    const sim::VictoryConditions& v = r.victory;
    const sim::WorldSettings& w = setup.world;
    const auto condition = [](const auto& values, std::uint32_t value, std::string_view key) {
        return value == 0 || offered(values, value) ? std::nullopt
                                                    : std::optional<std::string>(ui::tr(key, {grouped(value)}));
    };
    switch (item) {
        case SetupItem::Refund:
            if (r.demolish_refund == sim::Refund::Partial && !offered(kRefundPercents, r.refund_percent)) {
                return ui::tr("setup.refund.partial", {std::to_string(r.refund_percent)});
            }
            break;
        case SetupItem::Ships:
            if (!offered(kShips, r.max_ships_per_player)) {
                return number_text(r.max_ships_per_player);
            }
            break;
        case SetupItem::Units:
            if (!offered(kUnits, r.max_army)) {
                return number_text(r.max_army);
            }
            break;
        case SetupItem::GameSpeed:
            if (!offered(kSpeeds, r.max_game_speed)) {
                return ui::tr("setup.speed.value", {std::to_string(r.max_game_speed)});
            }
            break;
        case SetupItem::WorldSize:
            if (!setup.auto_size && !offered(kSizes, std::pair{w.width, w.height})) {
                return ui::tr("setup.size.value", {std::to_string(w.width), std::to_string(w.height)});
            }
            break;
        case SetupItem::Players:
            if (w.players < kFewestPlayers || w.players > kMostPlayers) {
                return std::to_string(w.players);
            }
            break;
        case SetupItem::GoldIslands:
            if (!offered(kGoldIslands, std::uint32_t{w.gold_islands})) {
                return std::to_string(w.gold_islands);
            }
            break;
        case SetupItem::IslandPreset:
            if (!island_preset(w)) {
                return ui::tr("setup.island_preset.custom");
            }
            break;
        case SetupItem::LandShare:
            if (!offered(kLandShares, std::uint32_t{w.landscape.land})) {
                return ui::tr("setup.win.percent", {std::to_string(w.landscape.land)});
            }
            break;
        case SetupItem::SmallestIsland:
            if (!offered(kSmallest, std::uint32_t{w.landscape.min_side})) {
                return ui::tr("setup.tiles", {std::to_string(w.landscape.min_side)});
            }
            break;
        case SetupItem::LargestIsland:
            if (!offered(kLargest, std::uint32_t{w.landscape.max_side})) {
                return ui::tr("setup.tiles", {std::to_string(w.landscape.max_side)});
            }
            break;
        case SetupItem::WinInhabitants:
            return condition(kWinInhabitants, v.inhabitants, "setup.win.number");
        case SetupItem::WinClassInhabitants:
            return condition(kWinClassInhabitants, v.class_inhabitants, "setup.win.number");
        case SetupItem::WinMoney:
            return condition(kWinMoney, v.money, "setup.win.gold");
        case SetupItem::WinBalance:
            return condition(kWinBalance, v.balance, "setup.win.balance.value");
        case SetupItem::WinIslands:
            return condition(kWinIslands, std::uint32_t{v.islands_percent}, "setup.win.percent");
        case SetupItem::WinScore:
            return condition(kWinScore, v.score, "setup.win.number");
        case SetupItem::WinTimeLimit:
            if (v.time_limit_minutes != 0 && !offered(kWinMinutes, v.time_limit_minutes)) {
                const std::uint32_t m = v.time_limit_minutes;
                return m % 60 == 0 ? ui::tr("setup.win.hours", {std::to_string(m / 60)})
                                   : ui::tr("setup.win.minutes", {std::to_string(m)});
            }
            break;
        default:
            break;
    }
    return std::nullopt;
}

}  // namespace

bool setup_value_custom(const EndlessSetup& setup, SetupItem item) {
    return custom_value(setup, item).has_value();
}

std::string setup_value(const EndlessSetup& setup, SetupItem item) {
    if (item == SetupItem::WorldSize && setup.auto_size) {
        const sim::WorldSettings w = setup.world_settings();
        return ui::tr("setup.size.auto", {std::to_string(w.width), std::to_string(w.height)});
    }
    if (item == SetupItem::IslandPreset && !island_preset(setup.world)) {
        return ui::tr("setup.island_preset.custom");  // the items below say what
    }
    if (const auto custom = custom_value(setup, item)) {
        return ui::tr("setup.value.own", {*custom});
    }
    return setup_value(item, setup_choice(setup, item));
}

bool read_setup_line(EndlessSetup& setup, std::string_view key, std::string_view value) {
    sim::Rules& r = setup.rules;
    sim::WorldSettings& w = setup.world;
    const auto n = parse_number(value);
    if (key.starts_with("victory.")) {
        return read_victory_line(r.victory, key.substr(8), value);
    }
    if (key == "refund") {
        // 0: none, 100: full, else a share in percent.
        if (n && *n == 0) {
            r.demolish_refund = sim::Refund::None;
        } else if (n && *n >= 100) {
            r.demolish_refund = sim::Refund::Full;
        } else if (n) {
            r.demolish_refund = sim::Refund::Partial;
            r.refund_percent = static_cast<std::uint8_t>(*n);
        }
    } else if (key == "moving") {
        if (const auto i = name_index(kMovingNames, value)) {
            r.moving = static_cast<sim::Moving>(*i);
        }
    } else if (key == "trader_prices") {
        if (value == "market" || value == "original") {
            r.trader_prices = value == "market" ? sim::TraderPrices::Market : sim::TraderPrices::Original;
        }
    } else if (key == "trader_reach") {
        if (const auto i = name_index(kTraderReachNames, value)) {
            r.trader_reach = static_cast<sim::TraderReach>(*i);
        }
    } else if (key == "ore_veins") {
        if (value == "original") {
            r.ore_veins = sim::OreVeins::Finite;  // its name before "game" came
        } else if (const auto i = name_index(kOreVeinNames, value)) {
            r.ore_veins = static_cast<sim::OreVeins>(*i);
        }
    } else if (key == "computers") {
        if (const auto i = name_index(kComputerNames, value)) {
            r.computer_players = static_cast<sim::Computers>(*i);
        }
    } else if (key == "trade_carts") {
        if (value == "more" || value == "one") {
            r.more_carts = value == "more";
        }
    } else if (key == "fertile_fields") {
        if (value == "all" || value == "original") {
            r.fertile_fields = value == "all";
        }
    } else if (key == "cheats") {
        if (value == "code" || value == "start" || value == "off") {
            r.cheats = value == "code" ? sim::Cheats::Code : value == "start" ? sim::Cheats::Start : sim::Cheats::Off;
        }
    } else if (key == "hunting") {  // the same
        if (value == "wider" || value == "original") {
            r.wider_hunting = value == "wider";
        }
    } else if (key == "climate_crops") {
        if (value == "all" || value == "original") {
            r.climate_crops = value == "all";
        }
    } else if (key == "big_ore_veins") {
        if (value == "all" || value == "original") {
            r.big_ore_veins = value == "all";
        }
    } else if (key == "ship_purchases") {
        if (value == "on" || value == "off") {
            r.computers_buy_ships = value == "on";
        }
    } else if (key == "balance") {
        if (value == "alternative" || value == "original") {
            r.revised_bread = value == "alternative";
            r.wider_hunting = value == "alternative";
        }
    } else if (key == "bread") {  // before the alternative balance: its parts one by one
        if (value == "revised" || value == "original") {
            r.revised_bread = value == "revised";
        }
    } else if (key == "ships") {
        r.max_ships_per_player = n.value_or(r.max_ships_per_player);
    } else if (key == "units") {
        r.max_army = n.value_or(r.max_army);
    } else if (key == "game_speed") {
        if (n && *n >= 1 && *n <= 64) {
            r.max_game_speed = *n;
        }
    } else if (key == "droughts") {
        if (const auto i = name_index(kDroughtNames, value)) {
            r.droughts = static_cast<sim::Droughts>(*i);
        }
    } else if (key == "volcanoes") {
        if (const auto i = name_index(kVolcanoNames, value)) {
            r.volcanoes = static_cast<sim::Volcanoes>(*i);
        }
    } else if (key == "plagues" || key == "fires") {
        if (const auto i = name_index(kOutbreakNames, value)) {
            (key == "plagues" ? r.plagues : r.fires) = static_cast<sim::Outbreaks>(*i);
        }
    } else if (key == "event_speed") {
        // Since moved to the display settings ("display.event_speed"): a line of before is dropped.
    } else if (key == "pirates") {
        if (const auto i = name_index(kPirateNames, value)) {
            r.pirates = static_cast<sim::Pirates>(*i);
        }
    } else if (key == "pirate_nests") {
        if (const auto i = name_index(kPirateNestNames, value)) {
            r.pirate_nests = static_cast<sim::PirateNests>(*i);
        }
    } else if (key == "reveal") {
        if (const auto i = name_index(kRevealNames, value)) {
            r.reveal = static_cast<sim::Reveal>(*i);
        } else if (value == "players") {
            r.reveal = sim::Reveal::Original;  // the towns of players, which version 0.2 offered
        }
    } else if (key == "resources") {
        if (value == "on" || value == "off") {
            r.guarantee_resources = value == "on";
        }
    } else if (key == "ai") {
        if (value == "classic" || value == "open1602") {
            r.bugs.classic_ai = value == "classic";
        }
    } else if (const auto bug = key.starts_with("bug.") ? bug_named(key.substr(4)) : nullptr) {
        if (value == "original" || value == "fixed") {
            r.bugs.*bug = value == "original";
        }
    } else if (key == "own_world") {
        if (value == "on" || value == "off") {
            setup.own_world = value == "on";
        }
    } else if (key == "world") {
        if (value == "auto") {
            setup.auto_size = true;
            return true;
        }
        const std::size_t x = value.find('x');
        const auto width = x == std::string_view::npos ? std::nullopt : parse_number(value.substr(0, x));
        const auto height = x == std::string_view::npos ? std::nullopt : parse_number(value.substr(x + 1));
        constexpr auto lo = sim::WorldSettings::kMinSide;
        constexpr auto hi = sim::WorldSettings::kMaxSide;
        if (width && height && *width >= lo && *width <= hi && *height >= lo && *height <= hi) {
            w.width = *width;
            w.height = *height;
            setup.auto_size = false;
        }
    } else if (key == "players") {
        if (n && *n >= 1 && *n <= sim::kMaxPlayers) {
            w.players = static_cast<std::uint8_t>(*n);
        }
    } else if (key == "islands" || key == "villages" || key == "volcano_islands") {
        if (const auto i = name_index(kAmountNames, value); i && !(key == "islands" && *i == 0)) {
            (key == "islands" ? w.islands : key == "villages" ? w.natives : w.volcanoes) = kAmounts[*i];
        }
    } else if (key == "gold_islands") {
        if (value == "auto") {
            w.gold_islands = 0;
        } else if (n && *n >= 1 && *n <= sim::WorldSettings::kMaxGoldIslands) {
            w.gold_islands = static_cast<std::uint8_t>(*n);
        }
    } else if (key == "island_sizes") {
        if (const auto i = name_index(kSizeNames, value)) {
            w.sizes = static_cast<Sizes>(*i);
        }
    } else if (key == "island_shapes") {
        if (const auto i = name_index(kShapeNames, value)) {
            w.shapes = static_cast<Shapes>(*i);
        }
    } else if (key.starts_with("island_") &&
               std::ranges::find(sim::landscape_names::kKeys, key.substr(7)) != sim::landscape_names::kKeys.end()) {
        (void)sim::set_landscape(w.landscape, key.substr(7), value);
    } else if (key == "starts") {
        if (const auto i = name_index(kStartNames, value)) {
            w.starts = *i == 1 ? Starts::Together : Starts::Spread;
        }
    } else {
        return false;
    }
    return true;
}

std::string write_setup(const EndlessSetup& setup, std::string_view prefix) {
    const sim::Rules& r = setup.rules;
    const sim::WorldSettings& w = setup.world;
    std::string out;
    const auto line = [&out, prefix](std::string_view key, std::string_view value) {
        out += prefix;
        out += key;
        out += " = ";
        out += value;
        out += '\n';
    };
    line("refund", std::to_string(r.refund_share()));
    line("moving", kMovingNames[std::min<std::size_t>(static_cast<std::size_t>(r.moving), kMovingNames.size() - 1)]);
    line("trader_prices", r.trader_prices == sim::TraderPrices::Market ? "market" : "original");
    line("trader_reach", kTraderReachNames[std::min<std::size_t>(static_cast<std::size_t>(r.trader_reach), 1)]);
    line("ore_veins", kOreVeinNames[std::min<std::size_t>(static_cast<std::size_t>(r.ore_veins), 2)]);
    line("balance", r.revised_bread && r.wider_hunting ? "alternative" : "original");
    line("trade_carts", r.more_carts ? "more" : "one");
    line("cheats", r.cheats == sim::Cheats::Code ? "code" : r.cheats == sim::Cheats::Start ? "start" : "off");
    line("fertile_fields", r.fertile_fields ? "all" : "original");
    line("climate_crops", r.climate_crops ? "all" : "original");
    line("big_ore_veins", r.big_ore_veins ? "all" : "original");
    line("ships", std::to_string(r.max_ships_per_player));
    line("units", std::to_string(r.max_army));
    line("game_speed", std::to_string(r.max_game_speed));
    line("droughts", kDroughtNames[std::min<std::size_t>(static_cast<std::size_t>(r.droughts), 4)]);
    line("volcanoes", kVolcanoNames[enum_choice(r.volcanoes, kVolcanoNames.size())]);
    line("plagues", kOutbreakNames[enum_choice(r.plagues, kOutbreakNames.size())]);
    line("fires", kOutbreakNames[enum_choice(r.fires, kOutbreakNames.size())]);
    line("resources", on_off(r.guarantee_resources));
    line("ai", r.bugs.classic_ai ? "classic" : "open1602");
    line("ship_purchases", r.computers_buy_ships ? "on" : "off");
    line("computers", kComputerNames[std::min<std::size_t>(static_cast<std::size_t>(r.computer_players), 2)]);
    line("pirates", kPirateNames[std::min<std::size_t>(static_cast<std::size_t>(r.pirates), 2)]);
    line("pirate_nests", kPirateNestNames[std::min<std::size_t>(static_cast<std::size_t>(r.pirate_nests), 1)]);
    line("reveal", kRevealNames[std::min<std::size_t>(static_cast<std::size_t>(r.reveal), kRevealNames.size() - 1)]);
    for (const BugEntry& e : kBugEntries) {
        line("bug." + std::string(e.name), r.bugs.*e.bug ? "original" : "fixed");
    }
    line("own_world", on_off(setup.own_world));
    line("world", setup.auto_size ? "auto" : std::to_string(w.width) + "x" + std::to_string(w.height));
    line("players", std::to_string(w.players));
    line("islands", kAmountNames[std::min<std::size_t>(static_cast<std::size_t>(w.islands), kAmountNames.size() - 1)]);
    line("island_sizes", kSizeNames[std::min<std::size_t>(static_cast<std::size_t>(w.sizes), 2)]);
    line("island_shapes", kShapeNames[enum_choice(w.shapes, kShapeNames.size())]);
    const Landscape& l = w.landscape;
    line("island_land", std::to_string(l.land));
    line("island_min", std::to_string(l.min_side));
    line("island_max", std::to_string(l.max_side));
    line("island_spread", kSpreadNames[enum_choice(l.spread, kSpreadNames.size())]);
    line("island_gap", kGapNames[enum_choice(l.gap, kGapNames.size())]);
    line("island_coast", kCoastNames[enum_choice(l.coast, kCoastNames.size())]);
    line("island_outline", kOutlineNames[enum_choice(l.outline, kOutlineNames.size())]);
    line("island_forest", kShareNames[enum_choice(l.forest, kShareNames.size())]);
    line("island_mountains", kShareNames[enum_choice(l.mountains, kShareNames.size())]);
    line("villages", kAmountNames[std::min<std::size_t>(static_cast<std::size_t>(w.natives), kAmountNames.size() - 1)]);
    line("volcano_islands",
         kAmountNames[std::min<std::size_t>(static_cast<std::size_t>(w.volcanoes), kAmountNames.size() - 1)]);
    line("gold_islands", w.gold_islands == 0 ? "auto" : std::to_string(w.gold_islands));
    line("starts", kStartNames[w.starts == Starts::Together ? 1 : 0]);
    const sim::VictoryConditions& v = r.victory;
    line("victory.inhabitants", std::to_string(v.inhabitants));
    line("victory.class", std::to_string(v.resident_class));
    line("victory.class_inhabitants", std::to_string(v.class_inhabitants));
    line("victory.money", std::to_string(v.money));
    line("victory.balance", std::to_string(v.balance));
    line("victory.islands", std::to_string(v.islands_percent));
    line("victory.eliminate", on_off(v.eliminate));
    line("victory.score", std::to_string(v.score));
    line("victory.monument", kMonumentNames[std::min<std::size_t>(static_cast<std::size_t>(v.monument), 3)]);
    line("victory.minutes", std::to_string(v.time_limit_minutes));
    line("victory.combine", v.all ? "all" : "any");
    line("victory.computers", on_off(v.computers));
    return out;
}

void read_setup(EndlessSetup& setup, std::string_view text, std::string_view prefix) {
    const auto trim = [](std::string_view v) {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
            v.remove_prefix(1);
        }
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) {
            v.remove_suffix(1);
        }
        return v;
    };
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq));
        if (key.starts_with(prefix)) {
            (void)read_setup_line(setup, key.substr(prefix.size()), trim(line.substr(eq + 1)));
        }
    }
}

void set_package_island_presets(std::vector<sim::PackageLandscape> presets) {
    package_presets() = std::move(presets);
}

}  // namespace o1602::app
