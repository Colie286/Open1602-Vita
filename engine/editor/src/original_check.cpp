// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/original_check.hpp"

#include <cstddef>

#include "o1602/sim/faction.hpp"
#include "o1602/sim/original_limits.hpp"

namespace o1602::editor {

namespace {

void at_most(std::vector<std::string>& problems, std::size_t count, std::size_t limit, const char* what) {
    if (count > limit) {
        problems.push_back(std::to_string(count) + " " + what + ", the original holds " + std::to_string(limit));
    }
}

}  // namespace

std::vector<std::string> check_original_limits(const sim::World& world) {
    namespace original = sim::original;
    std::vector<std::string> problems;
    if (world.width() != original::kWorldWidth || world.height() != original::kWorldHeight) {
        problems.push_back("the map is " + std::to_string(world.width()) + " x " + std::to_string(world.height()) +
                           " tiles, the original's is " + std::to_string(original::kWorldWidth) + " x " +
                           std::to_string(original::kWorldHeight));
    }
    at_most(problems, world.islands().size(), original::kMaxIslands, "islands");
    for (const sim::WorldIsland& island : world.islands()) {
        const std::string name = "island " + std::to_string(island.header.number);
        if (island.map.width() > original::kMaxIslandSide || island.map.height() > original::kMaxIslandSide) {
            problems.push_back(name + " is " + std::to_string(island.map.width()) + " x " +
                               std::to_string(island.map.height()) + " tiles, the original's are at most " +
                               std::to_string(original::kMaxIslandSide) + " a side");
        }
        if (island.header.deposits.size() > original::kMaxDeposits) {
            problems.push_back(name + " has " + std::to_string(island.header.deposits.size()) +
                               " deposits, the original's have at most " + std::to_string(original::kMaxDeposits));
        }
    }
    at_most(problems, world.cities().size(), original::kMaxCities, "cities");
    at_most(problems, world.ships().size(), original::kMaxShips, "ships");
    at_most(problems, world.soldiers().size(), original::kMaxSoldiers, "soldiers");
    at_most(problems, world.towers().size(), original::kMaxTowers, "towers");
    std::size_t starting = 0;
    for (const sim::Player& p : world.players()) {
        starting += p.role == sim::PlayerRole::Neutral ? 0U : 1U;
    }
    at_most(problems, starting, original::kMaxStartPlayers, "players");
    if (!world.own_buildings().empty() || !world.own_figures().empty() || !world.goods().own_goods().empty()) {
        problems.push_back("buildings, figures or goods of a content package");
    }
    if (!world.resident_classes().empty() || !world.infra_levels().empty()) {
        problems.push_back("classes of inhabitants or unlock levels of a content package");
    }
    if (world.factions() != sim::base_factions()) {
        problems.push_back("factions of a content package");
    }
    if (world.balance() != sim::Balance{}) {
        problems.push_back("values of the balance changed by a content package");
    }
    if (!world.scenario_script().empty()) {
        problems.push_back("a script");
    }
    return problems;
}

}  // namespace o1602::editor
