// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/goals.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

#include "o1602/assets/game_data.hpp"
#include "o1602/editor/islands.hpp"
#include "o1602/editor/players.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;
namespace goal = sim::goal;
namespace loss = sim::loss;

namespace {

// The values each field goes through after "off".
constexpr std::array<std::int64_t, 20> kCityCounts = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                                      11, 12, 13, 14, 15, 16, 18, 20, 25, 30};
constexpr std::array<std::int64_t, 18> kTotals = {100,  200,  500,  1000,  1500,  2000,  2500,  3000,  4000,
                                                  5000, 6000, 8000, 10000, 12500, 15000, 20000, 25000, 30000};
constexpr std::array<std::int64_t, 16> kBalances = {0,   10,  25,  50,   100,  150,  200,  300,
                                                    400, 500, 750, 1000, 1500, 2000, 3000, 5000};
constexpr std::array<std::int64_t, 11> kOpponents = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::array<std::int64_t, 12> kTons = {10, 20, 30, 50, 75, 100, 150, 200, 300, 500, 750, 1000};
constexpr std::array<std::int64_t, 16> kCitySizes = {50,   100,  200,  300,  400,  500,  750,  1000,
                                                     1500, 2000, 2500, 3000, 4000, 5000, 6000, 8000};
constexpr std::array<std::int64_t, 12> kClassCounts = {10, 25, 50, 100, 200, 300, 500, 750, 1000, 1500, 2000, 3000};

/// The next value of `values` after `now` (forward) or before it, with 0 ("off") before the first
/// and after the last. A value between two goes to the next one of the list.
std::int64_t step_in(std::span<const std::int64_t> values, std::int64_t now, bool forward, bool off = true) {
    if (values.empty()) {
        return 0;
    }
    const auto it = std::lower_bound(values.begin(), values.end(), now);
    const bool listed = it != values.end() && *it == now;
    const bool is_off = off && now == 0 && !listed;
    if (forward) {
        if (is_off) {
            return values.front();
        }
        const auto next = listed ? it + 1 : it;
        return next == values.end() ? (off ? 0 : values.front()) : *next;
    }
    if (is_off) {
        return values.back();
    }
    if (it == values.begin()) {
        return off ? 0 : values.back();
    }
    return *(it - 1);
}

/// The next of `choices` after `now` (forward) or before it, around through 0 when `off`.
template <typename T>
T step_choice(const std::vector<T>& choices, T now, bool forward, bool off, T none) {
    if (choices.empty()) {
        return none;
    }
    const auto it = std::find(choices.begin(), choices.end(), now);
    if (it == choices.end()) {
        return forward ? choices.front() : choices.back();
    }
    if (forward) {
        return it + 1 == choices.end() ? (off ? none : choices.front()) : *(it + 1);
    }
    return it == choices.begin() ? (off ? none : choices.back()) : *(it - 1);
}

void set_bit(std::uint32_t& bits, std::uint32_t bit, bool on) {
    bits = on ? bits | bit : bits & ~bit;
}

/// The city goal of `field` and the bit that asks for it (0 for none: every, target, protected).
struct CityField {
    sim::Mission::CityGoal* goal = nullptr;
    std::uint32_t bit = 0;
    int part = 0;  ///< 0 its size, 1 its class, 2 the number of that class
};

CityField city_field(sim::Mission& m, GoalField f) {
    const auto at = static_cast<int>(f);
    if (f >= GoalField::City1 && f <= GoalField::City3Count) {
        const int k = at - static_cast<int>(GoalField::City1);
        return {&m.cities[static_cast<std::size_t>(k / 3)], goal::kCity1 << static_cast<unsigned>(k / 3), k % 3};
    }
    if (f >= GoalField::EveryCity && f <= GoalField::EveryCityCount) {
        return {&m.every_city, goal::kEveryCity, at - static_cast<int>(GoalField::EveryCity)};
    }
    if (f == GoalField::TargetCity) {
        return {&m.target_city, goal::kTargetCity, 0};
    }
    if (f == GoalField::ProtectedCity) {
        return {&m.protected_city, 0, 0};
    }
    return {};
}

bool playing(const sim::Player& p) {
    return p.role == sim::PlayerRole::Human || p.role == sim::PlayerRole::Computer;
}

bool valid_text(const std::string& text) {
    if (text.size() > kMaxMissionText) {
        return false;
    }
    return std::none_of(text.begin(), text.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u < 0x20 && u != '\n';
    });
}

}  // namespace

std::int64_t goal_value(const sim::Mission& m, GoalField field) {
    const auto on = [&](std::uint32_t bit, std::int64_t value) { return (m.goals & bit) != 0 ? value : 0; };
    switch (field) {
        case GoalField::Cities:
            return on(goal::kCityCount, m.min_cities);
        case GoalField::Inhabitants:
            return on(goal::kInhabitants, m.min_inhabitants);
        case GoalField::Balance:
            return on(goal::kBalance, m.min_balance);
        case GoalField::TradeBalance:
            return on(goal::kTradeBalance, m.min_trade_balance);
        case GoalField::Defeated:
            return on(goal::kDefeatedCount, m.min_defeated);
        case GoalField::DefeatPlayer:
            return (m.goals & goal::kDefeat) != 0 && !m.defeat.empty() ? m.defeat.front() + 1 : 0;
        case GoalField::Castle:
            return on(goal::kCastle, 1);
        case GoalField::Cathedral:
            return on(goal::kCathedral, 1);
        case GoalField::Goods1:
            return on(goal::kGoods1, m.goods[0].ware);
        case GoalField::Goods1Amount:
            return m.goods[0].amount / assets::kStockUnitsPerTon;
        case GoalField::Goods2:
            return on(goal::kGoods2, m.goods[1].ware);
        case GoalField::Goods2Amount:
            return m.goods[1].amount / assets::kStockUnitsPerTon;
        case GoalField::Islands1:
            return on(goal::kIslands1, m.islands[0].resource);
        case GoalField::Islands2:
            return on(goal::kIslands2, m.islands[1].resource);
        case GoalField::Target:
            return m.target == sim::kNoPlayer ? 0 : m.target + 1;
        case GoalField::ProtectedCities:
            return (m.losses & loss::kProtectedCount) != 0 ? m.protected_min_cities : 0;
        case GoalField::Computers:
            return on(goal::kComputers, 1);
        case GoalField::Message:
            return m.success_message;
        case GoalField::Count:
            return 0;
        default:
            break;
    }
    sim::Mission copy = m;
    const CityField c = city_field(copy, field);
    if (c.goal == nullptr) {
        return 0;
    }
    if (c.part == 0) {
        const bool asked = c.bit != 0 ? (m.goals & c.bit) != 0 : (m.losses & loss::kProtectedCity) != 0;
        return asked ? c.goal->inhabitants : 0;
    }
    return c.part == 1 ? c.goal->resident_class : static_cast<std::int64_t>(c.goal->count);
}

bool goal_asked(const sim::Mission& m, GoalField field) {
    const auto bit = [&](std::uint32_t b) { return (m.goals & b) != 0; };
    switch (field) {
        case GoalField::Cities:
            return bit(goal::kCityCount);
        case GoalField::Inhabitants:
            return bit(goal::kInhabitants);
        case GoalField::Balance:
            return bit(goal::kBalance);
        case GoalField::TradeBalance:
            return bit(goal::kTradeBalance);
        case GoalField::Defeated:
            return bit(goal::kDefeatedCount);
        case GoalField::DefeatPlayer:
            return bit(goal::kDefeat);
        case GoalField::Castle:
            return bit(goal::kCastle);
        case GoalField::Cathedral:
            return bit(goal::kCathedral);
        case GoalField::Goods1:
        case GoalField::Goods1Amount:
            return bit(goal::kGoods1);
        case GoalField::Goods2:
        case GoalField::Goods2Amount:
            return bit(goal::kGoods2);
        case GoalField::Islands1:
            return bit(goal::kIslands1);
        case GoalField::Islands2:
            return bit(goal::kIslands2);
        case GoalField::Target:
            return m.target != sim::kNoPlayer;
        case GoalField::ProtectedCities:
            return (m.losses & loss::kProtectedCount) != 0;
        case GoalField::Computers:
            return bit(goal::kComputers);
        case GoalField::Message:
        case GoalField::Count:
            return true;
        default:
            break;
    }
    sim::Mission copy = m;
    const CityField c = city_field(copy, field);
    if (c.goal == nullptr) {
        return false;
    }
    return c.bit != 0 ? bit(c.bit) : (m.losses & loss::kProtectedCity) != 0;
}

bool goal_used(const sim::Mission& m, GoalField field) {
    switch (field) {
        case GoalField::Goods1Amount:
            return (m.goals & goal::kGoods1) != 0;
        case GoalField::Goods2Amount:
            return (m.goals & goal::kGoods2) != 0;
        case GoalField::Target:
            return (m.goals & goal::kTargetCity) != 0 ||
                   (m.losses & (loss::kProtectedCity | loss::kProtectedCount)) != 0;
        default:
            break;
    }
    sim::Mission copy = m;
    const CityField c = city_field(copy, field);
    if (c.goal == nullptr || c.part == 0) {
        return true;
    }
    const auto size = static_cast<GoalField>(static_cast<int>(field) - c.part);
    if (goal_value(m, size) == 0) {
        return false;
    }
    return c.part == 2 || c.goal->count > 0;
}

std::vector<std::uint8_t> goal_goods(const sim::World& world) {
    return world.goods().trade_wares();
}

std::vector<std::uint8_t> goal_resources() {
    namespace ware = assets::ware;
    return {ware::kTobaccoField, ware::kSpiceField, ware::kSugarField, ware::kCottonField,
            ware::kWineField,    ware::kCocoaField, ware::kOre,        ware::kGold};
}

std::vector<sim::PlayerId> goal_players(const sim::World& world) {
    std::vector<sim::PlayerId> out;
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const sim::Player& p = world.players()[i];
        const sim::FactionBehaviour b = world.behaviour(p);
        if (playing(p) ? p.status != sim::PlayerStatus::Absent
                       : (b == sim::FactionBehaviour::Pirates || b == sim::FactionBehaviour::Natives) &&
                             p.status != sim::PlayerStatus::Absent) {
            out.push_back(static_cast<sim::PlayerId>(i));
        }
    }
    return out;
}

void step_goal(const sim::World& world, sim::Mission& m, GoalField field, bool forward) {
    const auto number = [&](std::uint32_t bit, std::span<const std::int64_t> values, auto& target) {
        const std::int64_t now = (m.goals & bit) != 0 ? target : 0;
        const std::int64_t next = step_in(values, now, forward);
        set_bit(m.goals, bit, next != 0);
        target = static_cast<std::remove_reference_t<decltype(target)>>(next);
    };
    switch (field) {
        case GoalField::Cities:
            number(goal::kCityCount, kCityCounts, m.min_cities);
            return;
        case GoalField::Inhabitants:
            number(goal::kInhabitants, kTotals, m.min_inhabitants);
            return;
        case GoalField::Balance:
        case GoalField::TradeBalance: {
            // 0 is a value here (no loss): "off" is the bit alone.
            const bool trade = field == GoalField::TradeBalance;
            const std::uint32_t bit = trade ? goal::kTradeBalance : goal::kBalance;
            std::int32_t& value = trade ? m.min_trade_balance : m.min_balance;
            std::vector<std::int64_t> choices(kBalances.begin(), kBalances.end());
            const std::int64_t none = -1;
            const std::int64_t now = (m.goals & bit) != 0 ? value : none;
            const std::int64_t next = step_choice<std::int64_t>(choices, now, forward, true, none);
            set_bit(m.goals, bit, next != none);
            value = next == none ? 0 : static_cast<std::int32_t>(next);
            return;
        }
        case GoalField::Defeated:
            number(goal::kDefeatedCount, kOpponents, m.min_defeated);
            return;
        case GoalField::DefeatPlayer: {
            const std::vector<sim::PlayerId> players = goal_players(world);
            const sim::PlayerId now =
                (m.goals & goal::kDefeat) != 0 && !m.defeat.empty() ? m.defeat.front() : sim::kNoPlayer;
            const sim::PlayerId next = step_choice(players, now, forward, true, sim::kNoPlayer);
            set_bit(m.goals, goal::kDefeat, next != sim::kNoPlayer);
            m.defeat.clear();
            if (next != sim::kNoPlayer) {
                m.defeat.push_back(next);
            }
            return;
        }
        case GoalField::Castle:
            m.goals ^= goal::kCastle;
            return;
        case GoalField::Cathedral:
            m.goals ^= goal::kCathedral;
            return;
        case GoalField::Computers:
            m.goals ^= goal::kComputers;
            return;
        case GoalField::Goods1:
        case GoalField::Goods2: {
            const std::size_t i = field == GoalField::Goods1 ? 0 : 1;
            const std::uint32_t bit = i == 0 ? goal::kGoods1 : goal::kGoods2;
            const std::vector<std::uint8_t> goods = goal_goods(world);
            const std::uint8_t now = (m.goals & bit) != 0 ? m.goods[i].ware : std::uint8_t{0};
            const std::uint8_t next = step_choice<std::uint8_t>(goods, now, forward, true, 0);
            set_bit(m.goals, bit, next != 0);
            m.goods[i].ware = next;
            if (next != 0 && m.goods[i].amount == 0) {
                m.goods[i].amount = static_cast<std::uint32_t>(kTons.front()) * assets::kStockUnitsPerTon;
            }
            return;
        }
        case GoalField::Goods1Amount:
        case GoalField::Goods2Amount: {
            const std::size_t i = field == GoalField::Goods1Amount ? 0 : 1;
            const std::int64_t tons = m.goods[i].amount / assets::kStockUnitsPerTon;
            const std::int64_t next = step_in(kTons, tons, forward, false);
            m.goods[i].amount = static_cast<std::uint32_t>(next) * assets::kStockUnitsPerTon;
            return;
        }
        case GoalField::Islands1:
        case GoalField::Islands2: {
            const std::size_t i = field == GoalField::Islands1 ? 0 : 1;
            const std::uint32_t bit = i == 0 ? goal::kIslands1 : goal::kIslands2;
            const std::uint8_t now = (m.goals & bit) != 0 ? m.islands[i].resource : std::uint8_t{0};
            const std::uint8_t next = step_choice<std::uint8_t>(goal_resources(), now, forward, true, 0);
            set_bit(m.goals, bit, next != 0);
            m.islands[i].resource = next;
            m.islands[i].islands.clear();
            return;
        }
        case GoalField::Target: {
            const std::vector<sim::PlayerId> players = goal_players(world);
            m.target = step_choice(players, m.target, forward, true, sim::kNoPlayer);
            return;
        }
        case GoalField::ProtectedCities: {
            const std::int64_t now = (m.losses & loss::kProtectedCount) != 0 ? m.protected_min_cities : 0;
            const std::int64_t next = step_in(kCityCounts, now, forward);
            set_bit(m.losses, loss::kProtectedCount, next != 0);
            m.protected_min_cities = static_cast<std::int32_t>(next);
            return;
        }
        case GoalField::Message:
            m.success_message = m.success_message == kMessageAccomplished ? kMessageColourWin : kMessageAccomplished;
            return;
        default:
            break;
    }
    const CityField c = city_field(m, field);
    if (c.goal == nullptr) {
        return;
    }
    switch (c.part) {
        case 0: {
            const bool asked = c.bit != 0 ? (m.goals & c.bit) != 0 : (m.losses & loss::kProtectedCity) != 0;
            const std::int64_t next = step_in(kCitySizes, asked ? c.goal->inhabitants : 0, forward);
            c.goal->inhabitants = static_cast<std::int32_t>(next);
            if (c.bit != 0) {
                set_bit(m.goals, c.bit, next != 0);
            } else {
                set_bit(m.losses, loss::kProtectedCity, next != 0);
            }
            if (next == 0) {
                c.goal->count = 0;
                c.goal->resident_class = 0;
            }
            break;
        }
        case 1:
            c.goal->resident_class =
                static_cast<std::uint8_t>(forward ? (c.goal->resident_class + 1) % kGoalClasses
                                                  : (c.goal->resident_class + kGoalClasses - 1) % kGoalClasses);
            break;
        default:
            c.goal->count = static_cast<std::uint32_t>(step_in(kClassCounts, c.goal->count, forward));
            break;
    }
}

core::Result<std::unique_ptr<Operation>> set_mission(const sim::World& world, std::size_t index, sim::Mission mission) {
    std::vector<sim::Mission> missions = world.missions();
    if (index > missions.size()) {
        return make_error(ErrorCode::NotFound, "no mission " + std::to_string(index));
    }
    if (index == missions.size() && missions.size() >= kMaxMissions) {
        return make_error(ErrorCode::LimitExceeded, "no more missions");
    }
    if (!valid_text(mission.text)) {
        return make_error(ErrorCode::InvalidFormat, "a mission's text the game cannot show");
    }
    if (index == missions.size()) {
        missions.push_back(std::move(mission));
    } else {
        missions[index] = std::move(mission);
    }
    sim::World after = world;
    after.set_missions(std::move(missions));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "mission_change"));
}

core::Result<std::unique_ptr<Operation>> remove_mission(const sim::World& world, std::size_t index) {
    std::vector<sim::Mission> missions = world.missions();
    if (index >= missions.size()) {
        return make_error(ErrorCode::NotFound, "no mission " + std::to_string(index));
    }
    missions.erase(missions.begin() + static_cast<std::ptrdiff_t>(index));
    sim::World after = world;
    after.set_missions(std::move(missions));
    for (std::size_t i = 0; i < after.players().size(); ++i) {
        sim::Player* p = after.player(static_cast<sim::PlayerId>(i));
        if (p == nullptr || p->mission == sim::kNoMission || p->mission < index) {
            continue;
        }
        p->mission = p->mission == index ? sim::kNoMission : static_cast<sim::MissionId>(p->mission - 1);
    }
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "mission_remove"));
}

core::Result<std::unique_ptr<Operation>> set_mission_holder(const sim::World& world, sim::PlayerId player,
                                                            sim::MissionId mission) {
    const sim::Player* p = world.player(player);
    if (p == nullptr || !playing(*p)) {
        return make_error(ErrorCode::NotFound, "no human or computer in slot " + std::to_string(player));
    }
    if (mission != sim::kNoMission && mission >= world.missions().size()) {
        return make_error(ErrorCode::NotFound, "no mission " + std::to_string(mission));
    }
    sim::World after = world;
    if (sim::Player* q = after.player(player)) {
        q->mission = mission;
    }
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "mission_holder"));
}

std::vector<GoalProblem> goal_problems(const sim::World& world) {
    using Kind = GoalProblem::Kind;
    std::vector<GoalProblem> out;
    const std::vector<sim::PlayerId> slots = player_slots(world);
    const std::vector<sim::PlayerId> players = goal_players(world);
    const auto in_game = [&](sim::PlayerId q) { return std::find(players.begin(), players.end(), q) != players.end(); };
    const bool any_held = std::any_of(slots.begin(), slots.end(),
                                      [&](sim::PlayerId s) { return world.mission_of(world.players()[s]) != nullptr; });
    for (std::size_t mi = 0; mi < world.missions().size(); ++mi) {
        const sim::Mission& m = world.missions()[mi];
        std::vector<sim::PlayerId> holders;
        for (const sim::PlayerId s : slots) {
            if (world.players()[s].mission == mi) {
                holders.push_back(s);
            }
        }
        if (holders.empty()) {
            out.push_back({Kind::NobodyHolds, mi});
        }
        if ((m.goals & ~goal::kComputers) == 0) {
            out.push_back({Kind::NoGoals, mi});
            continue;  // without goals its loss conditions do nothing either
        }
        for (std::size_t i = 0; i < 2; ++i) {
            const std::uint32_t bit = i == 0 ? goal::kIslands1 : goal::kIslands2;
            if ((m.goals & bit) == 0 || world.planned_islands()) {
                continue;  // a world plan makes islands at the start: they may have it
            }
            const std::uint8_t r = m.islands[i].resource;
            const bool some =
                std::any_of(world.islands().begin(), world.islands().end(), [&](const sim::WorldIsland& w) {
                    if (r >= assets::ware::kCornField && r <= assets::ware::kTreasure) {
                        return (w.header.fertility >> (r - assets::ware::kCornField) & 1U) != 0;
                    }
                    return std::any_of(w.header.deposits.begin(), w.header.deposits.end(),
                                       [&](const auto& d) { return d.ware == r && d.amount > 0; });
                });
            if (!some) {
                out.push_back({Kind::NoIsland, mi, sim::kNoPlayer, i});
            }
        }
        for (std::size_t i = 0; i < 2; ++i) {
            const std::uint32_t bit = i == 0 ? goal::kGoods1 : goal::kGoods2;
            if ((m.goals & bit) != 0 && !world.goods().is_trade(m.goods[i].ware)) {
                out.push_back({Kind::UnknownGood, mi, sim::kNoPlayer, i});
            }
        }
        if ((m.goals & goal::kDefeat) != 0) {
            for (const sim::PlayerId q : m.defeat) {
                if (!in_game(q)) {
                    out.push_back({Kind::NoPlayer, mi, q});
                } else if (std::find(holders.begin(), holders.end(), q) != holders.end() && holders.size() == 1) {
                    out.push_back({Kind::DefeatSelf, mi, q});
                }
            }
        }
        const bool targets =
            (m.goals & goal::kTargetCity) != 0 || (m.losses & (loss::kProtectedCity | loss::kProtectedCount)) != 0;
        if (targets && m.target != sim::kNoPlayer && !in_game(m.target)) {
            out.push_back({Kind::NoPlayer, mi, m.target});
        }
        if ((m.goals & goal::kDefeatedCount) != 0) {
            // Opponents of one holder: everyone in the game but himself.
            const std::size_t opponents = players.empty() ? 0 : players.size() - 1;
            if (m.min_defeated > opponents) {
                out.push_back({Kind::TooFewOpponents, mi, sim::kNoPlayer, opponents});
            }
        }
        if ((m.losses & loss::kProtectedCount) != 0 && !holders.empty()) {
            const auto n = std::count_if(world.cities().begin(), world.cities().end(), [&](const sim::City& c) {
                return m.target == sim::kNoPlayer ? c.owner != holders.front() && c.owner != sim::kNoPlayer
                                                  : c.owner == m.target;
            });
            if (n < m.protected_min_cities) {
                out.push_back({Kind::TargetNoCity, mi, m.target});
            }
        }
    }
    if (any_held) {
        for (const sim::PlayerId s : slots) {
            const sim::Player& p = world.players()[s];
            if (p.role == sim::PlayerRole::Human && world.mission_of(p) == nullptr) {
                out.push_back({Kind::HumanWithout, 0, s});
            }
        }
    }
    return out;
}

}  // namespace o1602::editor
