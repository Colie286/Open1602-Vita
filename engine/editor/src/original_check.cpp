// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/original_check.hpp"

#include "o1602/sim/faction.hpp"
#include "o1602/sim/original_limits.hpp"

namespace o1602::editor {

namespace {

using Kind = LimitProblem::Kind;

void at_most(std::vector<LimitProblem>& problems, Kind kind, std::size_t count, std::size_t limit) {
    if (count > limit) {
        problems.push_back(LimitProblem{kind, count, limit});
    }
}

}  // namespace

std::vector<LimitProblem> original_limit_problems(const sim::World& world) {
    namespace original = sim::original;
    std::vector<LimitProblem> problems;
    if (world.width() != original::kWorldWidth || world.height() != original::kWorldHeight) {
        LimitProblem p{Kind::MapSize};
        p.width = world.width();
        p.height = world.height();
        problems.push_back(p);
    }
    at_most(problems, Kind::Islands, world.islands().size(), original::kMaxIslands);
    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        const sim::WorldIsland& island = world.islands()[i];
        if (island.map.width() > original::kMaxIslandSide || island.map.height() > original::kMaxIslandSide) {
            LimitProblem p{Kind::IslandSize};
            p.island = i;
            p.limit = original::kMaxIslandSide;
            p.width = island.map.width();
            p.height = island.map.height();
            problems.push_back(p);
        }
        if (island.header.deposits.size() > original::kMaxDeposits) {
            LimitProblem p{Kind::Deposits, island.header.deposits.size(), original::kMaxDeposits};
            p.island = i;
            problems.push_back(p);
        }
    }
    at_most(problems, Kind::Cities, world.cities().size(), original::kMaxCities);
    at_most(problems, Kind::Ships, world.ships().size(), original::kMaxShips);
    at_most(problems, Kind::Soldiers, world.soldiers().size(), original::kMaxSoldiers);
    at_most(problems, Kind::Towers, world.towers().size(), original::kMaxTowers);
    std::size_t starting = 0;
    for (const sim::Player& p : world.players()) {
        starting += p.role == sim::PlayerRole::Neutral ? 0U : 1U;
    }
    at_most(problems, Kind::Players, starting, original::kMaxStartPlayers);
    if (!world.own_buildings().empty() || !world.own_figures().empty() || !world.goods().own_goods().empty()) {
        problems.push_back(LimitProblem{Kind::PackageContent});
    }
    if (!world.resident_classes().empty() || !world.infra_levels().empty()) {
        problems.push_back(LimitProblem{Kind::PackageClasses});
    }
    if (world.factions() != sim::base_factions()) {
        problems.push_back(LimitProblem{Kind::PackageFactions});
    }
    if (world.balance() != sim::Balance{}) {
        problems.push_back(LimitProblem{Kind::Balance});
    }
    if (!world.scenario_script().empty()) {
        problems.push_back(LimitProblem{Kind::Script});
    }
    return problems;
}

std::string describe(const LimitProblem& p, const sim::World& world) {
    namespace original = sim::original;
    const auto n = [](std::size_t v) { return std::to_string(v); };
    const auto counted = [&](const char* what) {
        return n(p.count) + " " + what + ", the original holds " + n(p.limit);
    };
    const std::string island =
        "island " + (p.island < world.islands().size() ? n(world.islands()[p.island].header.number) : n(p.island));
    switch (p.kind) {
        case Kind::MapSize:
            return "the map is " + n(p.width) + " x " + n(p.height) + " tiles, the original's is " +
                   n(original::kWorldWidth) + " x " + n(original::kWorldHeight);
        case Kind::Islands:
            return counted("islands");
        case Kind::Cities:
            return counted("cities");
        case Kind::Ships:
            return counted("ships");
        case Kind::Soldiers:
            return counted("soldiers");
        case Kind::Towers:
            return counted("towers");
        case Kind::Players:
            return counted("players");
        case Kind::IslandSize:
            return island + " is " + n(p.width) + " x " + n(p.height) + " tiles, the original's are at most " +
                   n(p.limit) + " a side";
        case Kind::Deposits:
            return island + " has " + n(p.count) + " deposits, the original's have at most " + n(p.limit);
        case Kind::PackageContent:
            return "buildings, figures or goods of a content package";
        case Kind::PackageClasses:
            return "classes of inhabitants or unlock levels of a content package";
        case Kind::PackageFactions:
            return "factions of a content package";
        case Kind::Balance:
            return "values of the balance changed by a content package";
        case Kind::Script:
            return "a script";
    }
    return {};
}

std::vector<std::string> check_original_limits(const sim::World& world) {
    std::vector<std::string> lines;
    for (const LimitProblem& p : original_limit_problems(world)) {
        lines.push_back(describe(p, world));
    }
    return lines;
}

}  // namespace o1602::editor
