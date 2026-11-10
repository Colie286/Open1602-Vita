// SPDX-License-Identifier: GPL-3.0-or-later
//
// Victory conditions of an endless game (victory.hpp).
#include "o1602/sim/victory.hpp"

#include <set>
#include <vector>

#include "o1602/sim/mission.hpp"
#include "o1602/sim/score.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

namespace {

bool contender(const Player& p) {
    return (p.role == PlayerRole::Human || p.role == PlayerRole::Computer) && p.status != PlayerStatus::Absent;
}

/// Every human and computer opponent that took part is defeated, and there was one.
bool opponents_defeated(const World& world, PlayerId player) {
    bool any = false;
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const Player& o = world.players()[i];
        if (static_cast<PlayerId>(i) == player || !contender(o)) {
            continue;
        }
        if (o.status == PlayerStatus::Playing) {
            return false;
        }
        any = true;
    }
    return any;
}

bool monuments_built(const Player& p, Monument m) {
    const bool castle = p.castles_built >= 1;
    const bool cathedral = p.cathedrals_built >= 1;
    switch (m) {
        case Monument::Castle:
            return castle;
        case Monument::Cathedral:
            return cathedral;
        case Monument::Both:
            return castle && cathedral;
        case Monument::None:
            break;
    }
    return false;
}

}  // namespace

std::uint32_t victory_required(const VictoryConditions& v) noexcept {
    std::uint32_t r = 0;
    r |= v.inhabitants != 0 ? victory::kInhabitants : 0U;
    r |= v.class_inhabitants != 0 ? victory::kClass : 0U;
    r |= v.money != 0 ? victory::kMoney : 0U;
    r |= v.balance != 0 ? victory::kBalance : 0U;
    r |= v.eliminate ? victory::kEliminate : 0U;
    r |= v.islands_percent != 0 ? victory::kIslands : 0U;
    r |= v.score != 0 ? victory::kScore : 0U;
    r |= v.monument != Monument::None ? victory::kMonument : 0U;
    return r;
}

bool can_win(const World& world, const VictoryConditions& v, PlayerId player) {
    const Player* p = world.player(player);
    return p != nullptr && p->status == PlayerStatus::Playing &&
           (p->role == PlayerRole::Human || (p->role == PlayerRole::Computer && v.computers));
}

VictoryCheck check_victory(const World& world, const assets::BuildingTable& buildings,
                           const assets::FigureTable& figures, const VictoryConditions& v, PlayerId player) {
    VictoryCheck check;
    check.required = victory_required(v);
    if (check.required == 0 || !can_win(world, v, player)) {
        return check;
    }
    const Player& p = *world.player(player);
    std::uint32_t a = 0;
    if ((check.required & (victory::kInhabitants | victory::kClass | victory::kIslands)) != 0) {
        const std::vector<std::uint32_t> workers = city_workers(world, buildings);
        std::uint64_t einwohner = 0;
        std::uint64_t upper = 0;
        std::set<std::uint16_t> islands;
        for (std::size_t i = 0; i < world.cities().size(); ++i) {
            const City& c = world.cities()[i];
            if (c.owner != player) {
                continue;
            }
            einwohner += city_einwohner(c, i < workers.size() ? workers[i] : 0);
            for (std::size_t k = v.resident_class; k < c.inhabitants.size(); ++k) {
                upper += c.inhabitants[k];
            }
            if (c.island < world.islands().size()) {
                islands.insert(c.island);
            }
        }
        a |= einwohner >= v.inhabitants ? victory::kInhabitants : 0U;
        a |= upper >= v.class_inhabitants ? victory::kClass : 0U;
        const std::uint64_t all = world.islands().size();
        a |= all != 0 && islands.size() * 100U >= all * v.islands_percent ? victory::kIslands : 0U;
    }
    a |= p.money >= 0 && static_cast<std::uint64_t>(p.money) >= v.money ? victory::kMoney : 0U;
    a |= v.eliminate && opponents_defeated(world, player) ? victory::kEliminate : 0U;
    a |= monuments_built(p, v.monument) ? victory::kMonument : 0U;
    // Balance and score take the longest: looked at only while they can still decide the check.
    std::uint32_t looked = ~(victory::kBalance | victory::kScore);
    const auto open = [&](std::uint32_t bit) {
        const std::uint32_t seen = check.required & looked;
        looked |= bit;
        return (check.required & bit) != 0 && (v.all ? (seen & ~a) == 0 : (seen & a) == 0);
    };
    if (open(victory::kBalance) && player_balance(world, buildings, figures, player) >= std::int64_t{v.balance}) {
        a |= victory::kBalance;
    }
    if (open(victory::kScore) && player_score(world, buildings, player) >= std::int64_t{v.score}) {
        a |= victory::kScore;
    }
    check.achieved = a & check.required;
    check.won = v.all ? check.achieved == check.required : check.achieved != 0;
    return check;
}

PlayerId time_limit_winner(const World& world, const assets::BuildingTable& buildings, const VictoryConditions& v) {
    PlayerId best = kNoPlayer;
    std::int64_t best_points = 0;
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const auto id = static_cast<PlayerId>(i);
        if (!can_win(world, v, id)) {
            continue;
        }
        const std::int64_t points = player_score(world, buildings, id);
        if (best == kNoPlayer || points > best_points) {
            best = id;
            best_points = points;
        }
    }
    return best;
}

}  // namespace o1602::sim
