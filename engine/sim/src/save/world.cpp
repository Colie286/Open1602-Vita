// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's settings and clocks: RULE, WRLD and EPOC.
#include <algorithm>
#include <array>
#include <iterator>
#include <utility>
#include <vector>

#include "section_io.hpp"

#include "o1602/sim/simulation.hpp"

namespace o1602::sim::save {

namespace {

// Keys of the rules (RULE): a key per setting, so settings can be added and old files keep theirs.
enum RuleKey : std::uint16_t {
    kRefund = 1,
    kRefundPercent = 2,
    kMaxShips = 3,
    kGuaranteeResources = 4,
    kMaxGameSpeed = 5,
    kDroughts = 6,
    kVolcanoRule = 7,
    kTraderPrices = 8,
    kMaxArmy = 9,
    kClassicAi = 10,
    kPirateRule = 23,
    kPirateNests = 24,
    kTraderReach = 25,
    kEndlessVeins = 26,  // 1: OreVeins::Endless
    kReveal = 27,        // 2: Reveal::All (Original has none; 1, the towns of players, is now Original)
    kRevisedBread = 28,
    kOreVeins = 29,  // OreVeins::Finite (Endless keeps the old key, Game has none)
    kComputerPlayers = 30,
    // The bugs of the original brought back (OriginalBugs; classic_ai has kClassicAi).
    kBugGallows = 31,
    kBugFireRisk = 32,
    kBugRobberVictims = 33,
    kBugRobberFlight = 34,
    kBugGoldsheep = 35,
    kBugBuildingRadius = 36,
    kBugInfertileFields = 37,
    kBugRemoteTrade = 38,
    kBugRemoteRepair = 39,
    kBugSmallYardWarships = 40,
    kRevealNone = 41,       // 1: Reveal::None, under its own key so older versions read the original's way
    kNoShipPurchases = 42,  // 1: the computers buy no ships (Rules::computers_buy_ships off)
    kBugNativeLand = 43,
    // The victory conditions (VictoryConditions).
    kWinInhabitants = 11,
    kWinResidentClass = 12,
    kWinClassInhabitants = 13,
    kWinMoney = 14,
    kWinBalance = 15,
    kWinEliminate = 16,
    kWinIslandsPercent = 17,
    kWinScore = 18,
    kWinMonument = 19,
    kWinTimeLimit = 20,
    kWinAll = 21,
    kWinComputers = 22,
};

/// The keys of the bugs, written only when one is brought back.
constexpr std::pair<RuleKey, bool OriginalBugs::*> kBugKeys[] = {
    {kBugGallows, &OriginalBugs::gallows},
    {kBugFireRisk, &OriginalBugs::fire_risk},
    {kBugRobberVictims, &OriginalBugs::robber_victims},
    {kBugRobberFlight, &OriginalBugs::robber_flight},
    {kBugGoldsheep, &OriginalBugs::goldsheep},
    {kBugBuildingRadius, &OriginalBugs::building_radius},
    {kBugInfertileFields, &OriginalBugs::infertile_fields},
    {kBugRemoteTrade, &OriginalBugs::remote_trade},
    {kBugRemoteRepair, &OriginalBugs::remote_repair},
    {kBugSmallYardWarships, &OriginalBugs::small_yard_warships},
    {kBugNativeLand, &OriginalBugs::native_land},
};
static_assert(std::size(kBugKeys) + 1 == std::size(OriginalBugs::kAll), "every bug but classic_ai has a key");

template <class A, class R>
void io_random(A& a, R& r) {
    std::uint64_t state = r.state();
    std::uint64_t increment = r.increment();
    a.u64(state);
    a.u64(increment);
    if constexpr (A::kReading) {
        r = Random::from_state(state, increment);
    }
}

template <class A, class W>
void io_world(A& a, W& w) {
    a.u64(w.time_ms);
    if constexpr (A::kReading) {
        if (w.time_ms % Simulation::kTickMs != 0) {
            a.fail();  // game time moves in ticks of 50 ms; the 5 s steps (diplomacy, players) rely on it
        }
    }
    a.u32(w.city_name_index);
    a.u8(w.settings.difficulty);
    a.u32(w.settings.flags);
    a.u64(w.next_drought_ms);
    // The drought seed: whether there is one, and its value (0 without one).
    bool seeded = w.drought_seed.has_value();
    std::uint8_t seed = w.drought_seed.value_or(0);
    a.boolean(seeded);
    a.u8(seed);
    if constexpr (A::kReading) {
        if (seeded) {
            w.drought_seed = seed;
        }
    }
    a.list(w.randoms, 16, [&](auto& r) { io_random(a, r); });
    for (std::size_t g = 0; g < HouseClocks::kGroups; ++g) {
        a.u32(w.house_clocks.time_ms[g]);
        a.u8(w.house_clocks.epoch[g]);
    }
    for (std::size_t c = 0; c < GrowthState::kClasses; ++c) {
        a.u32(w.growth_ms[c]);
        a.u8(w.growth_counter[c]);
    }
    a.u32(w.next_disaster_id);
}

template <class A, class E>
void io_epochs(A& a, E& e, std::uint16_t version) {
    a.u8_masked(e.phase, 7U);
    a.list(e.islands, version >= kWideEpochs ? 3 : 2, [&](auto& item) {
        if (version >= kWideEpochs) {
            a.u16(item.first);  // island number
        } else {
            a.u8(item.first);
        }
        a.u8_masked(item.second, 7U);
    });
}

}  // namespace

// RULE is a list of keys and values rather than a record: the reader keeps a setting of a later
// version out (unknown key) and clamps the values into what the setting allows.
void write_rules(Writer& out, const Rules& r) {
    // Room for every key up front: the optional ones below then never reallocate (GCC 13 also
    // misreads that reallocation as an overflow).
    std::vector<std::pair<RuleKey, std::uint32_t>> values;
    values.reserve(48);
    const std::pair<RuleKey, std::uint32_t> always[] = {
        {kRefund, static_cast<std::uint32_t>(r.demolish_refund)},
        {kRefundPercent, r.refund_percent},
        {kMaxShips, r.max_ships_per_player},
        {kGuaranteeResources, r.guarantee_resources ? 1U : 0U},
        {kMaxGameSpeed, r.max_game_speed},
        {kDroughts, static_cast<std::uint32_t>(r.droughts)},
        {kVolcanoRule, static_cast<std::uint32_t>(r.volcanoes)},
        {kTraderPrices, static_cast<std::uint32_t>(r.trader_prices)},
        {kMaxArmy, r.max_army},
        {kClassicAi, r.bugs.classic_ai ? 1U : 0U},
    };
    values.assign(std::begin(always), std::end(always));
    // The pirates only when they differ from the game's setting: a game without the choice keeps
    // the bytes of the versions before it.
    if (r.pirates != Pirates::Game) {
        values.emplace_back(kPirateRule, static_cast<std::uint32_t>(r.pirates));
    }
    // The pirate nests only when they are not one for every four players: up to four players that
    // is the original, so older games keep their bytes.
    if (r.pirate_nests != PirateNests::Scaled) {
        values.emplace_back(kPirateNests, static_cast<std::uint32_t>(r.pirate_nests));
    }
    // The trader's reach likewise only when it is the original's.
    if (r.trader_reach != TraderReach::Scaled) {
        values.emplace_back(kTraderReach, static_cast<std::uint32_t>(r.trader_reach));
    }
    // The large veins only when they do not follow the game; endless as the versions before wrote it.
    // Their games without the key had finite veins, which now follow the game's settings instead.
    if (r.ore_veins == OreVeins::Endless) {
        values.emplace_back(kEndlessVeins, 1U);
    } else if (r.ore_veins == OreVeins::Finite) {
        values.emplace_back(kOreVeins, static_cast<std::uint32_t>(r.ore_veins));
    }
    // The computer opponents only when they do not follow the game.
    if (r.computer_players != Computers::Game) {
        values.emplace_back(kComputerPlayers, static_cast<std::uint32_t>(r.computer_players));
    }
    // The revised bread chain only when it is on: off is the original, so older games keep their bytes.
    if (r.revised_bread) {
        values.emplace_back(kRevisedBread, 1U);
    }
    // The computers' purchases of ships only when off: on is Open1602's way.
    if (!r.computers_buy_ships) {
        values.emplace_back(kNoShipPurchases, 1U);
    }
    // The towns shown from the start only when there are any: the original's way keeps the bytes.
    if (r.reveal == Reveal::All) {
        values.emplace_back(kReveal, 2U);
    } else if (r.reveal == Reveal::None) {
        values.emplace_back(kRevealNone, 1U);
    }
    // A bug of the original only when it is brought back: fixed is Open1602's way, which the
    // versions before them played.
    for (const auto& [key, bug] : kBugKeys) {
        if (r.bugs.*bug) {
            values.emplace_back(key, 1U);
        }
    }
    // The victory conditions only when there are any: a game without them keeps the bytes of the
    // versions before them.
    if (const VictoryConditions& v = r.victory; v != VictoryConditions{}) {
        const std::pair<RuleKey, std::uint32_t> wins[] = {
            {kWinInhabitants, v.inhabitants},
            {kWinResidentClass, v.resident_class},
            {kWinClassInhabitants, v.class_inhabitants},
            {kWinMoney, v.money},
            {kWinBalance, v.balance},
            {kWinEliminate, v.eliminate ? 1U : 0U},
            {kWinIslandsPercent, v.islands_percent},
            {kWinScore, v.score},
            {kWinMonument, static_cast<std::uint32_t>(v.monument)},
            {kWinTimeLimit, v.time_limit_minutes},
            {kWinAll, v.all ? 1U : 0U},
            {kWinComputers, v.computers ? 1U : 0U},
        };
        values.insert(values.end(), std::begin(wins), std::end(wins));
    }
    out.list(values, 6, [&](const auto& item) {
        out.u16(item.first);
        out.u32(item.second);
    });
}

Rules read_rules(Reader& in) {
    Rules r;
    const std::uint32_t n = in.in().count(6);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint16_t key = in.in().u16();
        const std::uint32_t v = in.in().u32();
        switch (key) {
            case kRefund:
                r.demolish_refund = static_cast<Refund>(std::min<std::uint32_t>(v, 2));
                break;
            case kRefundPercent:
                r.refund_percent = static_cast<std::uint8_t>(std::min<std::uint32_t>(v, 100));
                break;
            case kMaxShips:
                r.max_ships_per_player = v;
                break;
            case kGuaranteeResources:
                r.guarantee_resources = v != 0;
                break;
            case kMaxGameSpeed:
                r.max_game_speed = v;
                break;
            case kDroughts:
                r.droughts = static_cast<Droughts>(std::min<std::uint32_t>(v, 4));
                break;
            case kVolcanoRule:
                r.volcanoes = static_cast<Volcanoes>(std::min<std::uint32_t>(v, 2));
                break;
            case kTraderPrices:
                r.trader_prices = static_cast<TraderPrices>(std::min<std::uint32_t>(v, 1));
                break;
            case kMaxArmy:
                r.max_army = v;
                break;
            case kPirateRule:
                r.pirates = static_cast<Pirates>(std::min<std::uint32_t>(v, 2));
                break;
            case kPirateNests:
                r.pirate_nests = static_cast<PirateNests>(std::min<std::uint32_t>(v, 1));
                break;
            case kTraderReach:
                r.trader_reach = static_cast<TraderReach>(std::min<std::uint32_t>(v, 1));
                break;
            case kEndlessVeins:
                r.ore_veins = v != 0 ? OreVeins::Endless : OreVeins::Game;
                break;
            case kOreVeins:
                r.ore_veins = static_cast<OreVeins>(std::min<std::uint32_t>(v, 2));
                break;
            case kComputerPlayers:
                r.computer_players = static_cast<Computers>(std::min<std::uint32_t>(v, 2));
                break;
            case kRevisedBread:
                r.revised_bread = v != 0;
                break;
            case kClassicAi:
                r.bugs.classic_ai = v != 0;
                break;
            case kBugGallows:
            case kBugFireRisk:
            case kBugRobberVictims:
            case kBugRobberFlight:
            case kBugGoldsheep:
            case kBugBuildingRadius:
            case kBugInfertileFields:
            case kBugRemoteTrade:
            case kBugRemoteRepair:
            case kBugSmallYardWarships:
            case kBugNativeLand:
                for (const auto& [k, bug] : kBugKeys) {
                    if (k == key) {
                        r.bugs.*bug = v != 0;
                    }
                }
                break;
            case kReveal:
                r.reveal = v >= 2 ? Reveal::All : Reveal::Original;
                break;
            case kRevealNone:
                if (v != 0) {
                    r.reveal = Reveal::None;
                }
                break;
            case kNoShipPurchases:
                r.computers_buy_ships = v == 0;
                break;
            case kWinInhabitants:
                r.victory.inhabitants = v;
                break;
            case kWinResidentClass:
                r.victory.resident_class = static_cast<std::uint8_t>(std::min<std::uint32_t>(v, 4));
                break;
            case kWinClassInhabitants:
                r.victory.class_inhabitants = v;
                break;
            case kWinMoney:
                r.victory.money = v;
                break;
            case kWinBalance:
                r.victory.balance = v;
                break;
            case kWinEliminate:
                r.victory.eliminate = v != 0;
                break;
            case kWinIslandsPercent:
                r.victory.islands_percent = static_cast<std::uint8_t>(std::min<std::uint32_t>(v, 100));
                break;
            case kWinScore:
                r.victory.score = v;
                break;
            case kWinMonument:
                r.victory.monument = static_cast<Monument>(std::min<std::uint32_t>(v, 3));
                break;
            case kWinTimeLimit:
                r.victory.time_limit_minutes = v;
                break;
            case kWinAll:
                r.victory.all = v != 0;
                break;
            case kWinComputers:
                r.victory.computers = v != 0;
                break;
            default:
                break;  // a setting of a later version
        }
    }
    return r;
}

void write_world(Writer& out, const World& w) {
    Decoded::WorldPart part;
    part.time_ms = w.time_ms();
    part.city_name_index = w.city_name_index();
    part.settings = w.game_settings();
    part.next_drought_ms = w.next_drought_ms();
    part.drought_seed = w.drought_seed();
    for (std::size_t s = 0; s < static_cast<std::size_t>(RandomStream::Count); ++s) {
        part.randoms.push_back(w.random(static_cast<RandomStream>(s)));
    }
    part.house_clocks = w.house_clocks();
    part.growth_ms = w.growth().time_ms;
    part.growth_counter = w.growth().counter;
    part.next_disaster_id = w.peek_disaster_id();
    io_world(out, std::as_const(part));
}

Decoded::WorldPart read_world(Reader& in) {
    Decoded::WorldPart part;
    io_world(in, part);
    return part;
}

std::uint16_t epochs_version(const World& w) {
    return std::ranges::any_of(w.islands(), [](const WorldIsland& i) { return i.header.number > 0xFF; })
               ? kWideEpochs
               : std::uint16_t{1};
}

void write_epochs(Writer& out, const World& w, std::uint16_t version) {
    Decoded::EpochPart part;
    part.phase = w.island_phase();
    for (const WorldIsland& i : w.islands()) {
        part.islands.emplace_back(i.header.number, i.last_phase);
    }
    io_epochs(out, std::as_const(part), version);
}

Decoded::EpochPart read_epochs(Reader& in, std::uint16_t version) {
    Decoded::EpochPart part;
    io_epochs(in, part, version);
    return part;
}

}  // namespace o1602::sim::save
