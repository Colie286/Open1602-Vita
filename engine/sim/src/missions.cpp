// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mission goals and the values they look at (docs/re/missions.md).
#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_map>

#include "o1602/sim/consumption.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint8_t kFirstCrop = ware::kCornField;  // GETREIDE: resources 45..58 are fertility bits
constexpr std::uint8_t kLastCrop = ware::kTreasure;
constexpr std::uint16_t kMinDeposit = 32;  // a deposit counts with at least a ton left

std::uint64_t city_key(std::uint16_t island, std::uint8_t index) noexcept {
    return std::uint64_t{island} << 8 | index;
}

/// Index into World::cities() by island and city number.
std::unordered_map<std::uint64_t, std::size_t> city_index(const World& world) {
    std::unordered_map<std::uint64_t, std::size_t> index;
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        index.emplace(city_key(world.cities()[i].island, world.cities()[i].index), i);
    }
    return index;
}

Mission::CityGoal city_goal(const assets::MissionRecord::CityGoal& g, std::size_t classes,
                            std::vector<std::string>* warnings) {
    Mission::CityGoal out{g.size, 0, g.count};
    if (g.resident_class >= classes) {
        // The original reads a local array of 5 out of bounds (S-111).
        if (warnings != nullptr && (g.size != 0 || g.count != 0)) {
            warnings->push_back("mission: class " + std::to_string(g.resident_class) + " out of range");
        }
        out.resident_class = static_cast<std::uint8_t>(classes - 1);
    } else {
        out.resident_class = static_cast<std::uint8_t>(g.resident_class);
    }
    return out;
}

assets::MissionRecord::CityGoal record_goal(const Mission::CityGoal& g) {
    return {g.inhabitants, g.resident_class, g.count};
}

/// Whether an island has the resource of an island goal (0x0046B0A0).
bool island_has(const WorldIsland& island, std::uint8_t resource) {
    if (resource >= kFirstCrop && resource <= kLastCrop) {
        return (island.header.fertility >> (resource - kFirstCrop) & 1U) != 0;
    }
    return std::any_of(island.header.deposits.begin(), island.header.deposits.end(),
                       [&](const auto& d) { return d.ware == resource && d.amount >= kMinDeposit; });
}

std::uint32_t from_class(const City& city, std::uint8_t resident_class) {
    std::uint32_t n = 0;
    for (std::size_t c = resident_class; c < city.inhabitants.size(); ++c) {
        n += city.inhabitants[c];
    }
    return n;
}

bool meets(const City& city, std::uint32_t workers, const Mission::CityGoal& goal) {
    return std::int64_t{city_einwohner(city, workers)} >= goal.inhabitants &&
           from_class(city, goal.resident_class) >= goal.count;
}

/// Most of the given city goals that distinct cities meet at the same time (bipartite matching;
/// the original gives each city the first open goal it meets and can miss a solution, S-108).
/// Returns the goal bits of one largest matching.
std::uint32_t match_cities(const std::vector<std::size_t>& own, const std::vector<std::uint32_t>& workers,
                           const World& world, const Mission& m) {
    std::vector<std::size_t> goals;
    for (std::size_t g = 0; g < m.cities.size(); ++g) {
        if ((m.goals >> g & 1U) != 0) {
            goals.push_back(g);
        }
    }
    // fit[g][c]: city own[c] meets goal goals[g].
    std::vector<std::vector<bool>> fit(goals.size(), std::vector<bool>(own.size(), false));
    for (std::size_t g = 0; g < goals.size(); ++g) {
        for (std::size_t c = 0; c < own.size(); ++c) {
            fit[g][c] = meets(world.cities()[own[c]], workers[own[c]], m.cities[goals[g]]);
        }
    }
    std::vector<std::size_t> owner(own.size(), goals.size());  // goal holding city c, goals.size() = none
    std::vector<bool> seen;
    const auto augment = [&](auto&& self, std::size_t g) -> bool {
        for (std::size_t c = 0; c < own.size(); ++c) {
            if (!fit[g][c] || seen[c]) {
                continue;
            }
            seen[c] = true;
            if (owner[c] == goals.size() || self(self, owner[c])) {
                owner[c] = g;
                return true;
            }
        }
        return false;
    };
    std::uint32_t bits = 0;
    for (std::size_t g = 0; g < goals.size(); ++g) {
        seen.assign(own.size(), false);
        augment(augment, g);
    }
    for (std::size_t c = 0; c < own.size(); ++c) {
        if (owner[c] != goals.size()) {
            bits |= 1U << goals[owner[c]];
        }
    }
    return bits;
}

bool foreign(PlayerId owner, PlayerId target, PlayerId self) {
    return target == kNoPlayer ? owner != self && owner != kNoPlayer : owner == target;
}

bool has_ship(const World& world, PlayerId p) {
    return std::any_of(world.ships().begin(), world.ships().end(),
                       [&](const Ship& s) { return s.owner == p && !s.sinking && !s.cart(); });
}

bool has_city(const World& world, PlayerId p) {
    return std::any_of(world.cities().begin(), world.cities().end(), [&](const City& c) { return c.owner == p; });
}

}  // namespace

Mission mission_from_record(const assets::MissionRecord& r, std::vector<std::string>* warnings) {
    const auto warn = [&](const std::string& text) {
        if (warnings != nullptr) {
            warnings->push_back(text);
        }
    };
    // Five classes of inhabitants in the original; the goal's class is checked against that.
    constexpr std::size_t kClasses = 5;
    Mission m;
    m.goals = r.goals;
    m.losses = r.losses;
    m.target = player_from_original(r.target);
    m.min_defeated = r.min_defeated;
    for (std::size_t i = 0; i < r.defeat.size(); ++i) {
        if (r.defeat[i] != 0) {
            m.defeat.push_back(static_cast<PlayerId>(i));
        }
    }
    m.min_cities = r.min_cities;
    m.min_inhabitants = r.min_inhabitants;
    m.min_balance = r.min_balance;
    m.min_trade_balance = r.min_trade_balance;
    m.protected_min_cities = r.protected_min_cities;
    m.success_message = static_cast<std::uint16_t>(std::clamp(r.success_message, 0, 0xFFFF));
    m.text = r.text;
    for (std::size_t i = 0; i < 2; ++i) {
        m.goods[i] = {r.goods[i].ware, r.goods[i].amount};
        m.islands[i].resource = r.island_resource[i];
    }
    for (std::size_t i = 0; i < 3; ++i) {
        m.cities[i] = city_goal(r.cities[i], kClasses, warnings);
    }
    // The loader sorts the city goals by size, largest first (0x00477FEE), and sets the goal bit
    // of each one whose size is not 0.
    std::stable_sort(m.cities.begin(), m.cities.end(), [](const Mission::CityGoal& a, const Mission::CityGoal& b) {
        return a.inhabitants > b.inhabitants;
    });
    m.goals &= ~goal::kCities3;
    for (std::size_t i = 0; i < 3; ++i) {
        if (m.cities[i].inhabitants != 0) {
            m.goals |= 1U << i;
        }
    }
    m.every_city = city_goal(r.every_city, kClasses, warnings);
    m.target_city = city_goal(r.target_city, kClasses, warnings);
    m.protected_city = city_goal(r.protected_city, kClasses, warnings);
    m.unused_48 = r.unused_48;
    m.unused_58 = r.unused_58;
    // Bits the original never meets make a mission unwinnable, unknown loss bits lose at once
    // (S-111): dropped.
    if ((m.goals & ~(goal::kKnown | goal::kComputers)) != 0) {
        warn("mission: goal bits that can never be met dropped");
        m.goals &= goal::kKnown | goal::kComputers;
    }
    if ((m.losses & ~loss::kKnown) != 0) {
        warn("mission: unknown loss conditions dropped");
        m.losses &= loss::kKnown;
    }
    return m;
}

assets::MissionRecord mission_to_record(const Mission& m, std::uint32_t slot) {
    assets::MissionRecord r;
    r.slot = slot;
    r.goals = m.goals;
    r.losses = m.losses;
    r.target = m.target == kNoPlayer || m.target >= assets::kOriginalPlayerSlots ? assets::kNoOwner : m.target;
    r.min_defeated = static_cast<std::uint8_t>(std::min<std::uint16_t>(m.min_defeated, 0xFF));
    for (const PlayerId p : m.defeat) {
        if (p < r.defeat.size()) {
            r.defeat[p] = 1;
        }
    }
    r.min_cities = m.min_cities;
    r.min_inhabitants = m.min_inhabitants;
    r.min_balance = m.min_balance;
    r.min_trade_balance = m.min_trade_balance;
    r.protected_min_cities = static_cast<std::int16_t>(std::clamp(m.protected_min_cities, -32768, 32767));
    r.success_message = m.success_message;
    r.text = m.text;
    for (std::size_t i = 0; i < 2; ++i) {
        r.goods[i] = {m.goods[i].ware, static_cast<std::uint16_t>(std::min<std::uint32_t>(m.goods[i].amount, 0xFFFF))};
        r.island_resource[i] = m.islands[i].resource;
        r.island_count[i] = static_cast<std::uint8_t>(std::min<std::size_t>(m.islands[i].islands.size(), 0xFF));
    }
    for (std::size_t i = 0; i < 3; ++i) {
        r.cities[i] = record_goal(m.cities[i]);
    }
    r.every_city = record_goal(m.every_city);
    r.target_city = record_goal(m.target_city);
    r.protected_city = record_goal(m.protected_city);
    r.unused_48 = m.unused_48;
    r.unused_58 = m.unused_58;
    return r;
}

void prepare_missions(World& world) {
    std::vector<Mission> missions = world.missions();
    for (Mission& m : missions) {
        for (std::size_t i = 0; i < 2; ++i) {
            const std::uint32_t bit = i == 0 ? goal::kIslands1 : goal::kIslands2;
            Mission::IslandGoal& g = m.islands[i];
            if ((m.goals & bit) == 0 || !g.islands.empty()) {
                continue;
            }
            for (std::uint16_t k = 0; k < world.islands().size(); ++k) {
                if (island_has(world.islands()[k], g.resource)) {
                    g.islands.push_back(k);
                }
            }
        }
    }
    world.set_missions(std::move(missions));
    // Settings bit 5 when a mission of a player asks to defeat the pirates (0x00484EE1).
    World::GameSettings settings = world.game_settings();
    for (const Player& p : world.players()) {
        const Mission* m = world.mission_of(p);
        if (m == nullptr || (m->goals & goal::kDefeat) == 0) {
            continue;
        }
        for (const PlayerId q : m->defeat) {
            const Player* target = world.player(q);
            if (target != nullptr && world.behaviour(*target) == FactionBehaviour::Pirates) {
                settings.flags |= World::GameSettings::kPirateGoal;
            }
        }
    }
    world.set_game_settings(settings);
}

std::uint32_t city_einwohner(const City& city, std::uint32_t workers) noexcept {
    std::uint32_t n = workers;
    for (const std::uint32_t i : city.inhabitants) {
        n += i;
    }
    return n;
}

namespace {

/// Calls add(island, cell, building) for every building with workers (a used production, a
/// building with a running cost, a tower or castle) whose map cell holds a known building.
template <typename Add>
void for_each_workplace(const World& world, const assets::BuildingTable& buildings, Add&& add,
                        std::optional<std::uint16_t> only = std::nullopt) {
    const auto visit = [&](std::uint16_t island, std::uint16_t x, std::uint16_t y) {
        if (island >= world.islands().size() || (only && island != *only)) {
            return;
        }
        const IslandMap& map = world.islands()[island].map;
        if (!map.contains(x, y)) {
            return;
        }
        const MapCell& cell = map.top(x, y);
        const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
        if (b != nullptr) {
            add(island, cell, *b);
        }
    };
    for (const Production& p : world.productions()) {
        if (p.used) {
            visit(p.island, p.x, p.y);
        }
    }
    for (const CostBuilding& c : world.cost_buildings()) {
        visit(c.island, c.x, c.y);
    }
    for (const Tower& t : world.towers()) {
        visit(t.island, t.x, t.y);
    }
}

}  // namespace

std::vector<std::uint32_t> city_workers(const World& world, const assets::BuildingTable& buildings) {
    std::vector<std::uint32_t> workers(world.cities().size(), 0);
    const auto index = city_index(world);
    for_each_workplace(world, buildings, [&](std::uint16_t island, const MapCell& cell, const assets::BuildingType& b) {
        const auto it = index.find(city_key(island, cell.city));
        if (it != index.end()) {
            workers[it->second] += b.workers;
        }
    });
    return workers;
}

std::uint32_t city_workers(const World& world, const assets::BuildingTable& buildings, const City& city) {
    std::uint32_t workers = 0;
    for_each_workplace(
        world, buildings,
        [&](std::uint16_t, const MapCell& cell, const assets::BuildingType& b) {
            if (cell.city == city.index) {
                workers += b.workers;
            }
        },
        city.island);
    return workers;
}

std::vector<std::uint32_t> military_upkeep(const World& world, const assets::FigureTable& figures) {
    std::vector<std::uint32_t> upkeep(world.players().size(), 0);
    for (const Soldier& s : world.soldiers()) {
        const assets::FigureType* type = figures.get(s.figure);
        if (type != nullptr && s.owner < upkeep.size() && !s.dying()) {
            upkeep[s.owner] += figures.unit_upkeep(type->kind);
        }
    }
    // Units in training or waiting in a castle count too (player + 0xA6 from recruiting on).
    for (const CostBuilding& b : world.cost_buildings()) {
        if (b.kind != CostBuilding::Kind::Castle || b.island >= world.islands().size()) {
            continue;
        }
        const PlayerId owner = world.islands()[b.island].map.top(b.x, b.y).owner;
        for (const CastleSlot& slot : b.slots) {
            const assets::FigureType* type = figures.get(slot.figure);
            if (type != nullptr && owner < upkeep.size()) {
                upkeep[owner] += figures.unit_upkeep(type->kind);
            }
        }
    }
    // Soldiers on board ships (cargo wares wSOLDAT1..wPIONIER4 = figure + 24).
    for (const Ship& ship : world.ships()) {
        for (const CargoSlot& c : ship.cargo) {
            if (is_soldier_ware(c.ware) && ship.owner < upkeep.size()) {
                if (const assets::FigureType* type = figures.get(unit_of_ware(c.ware))) {
                    upkeep[ship.owner] += figures.unit_upkeep(type->kind);
                }
            }
        }
    }
    // Cannon towers (Turm: line of figuren.cod, player + 0xA4).
    for (const Tower& t : world.towers()) {
        if (t.owner < upkeep.size()) {
            upkeep[t.owner] += figures.tower_upkeep();
        }
    }
    // Computer players pay nothing (0x00475B00).
    for (std::size_t p = 0; p < upkeep.size(); ++p) {
        if (world.players()[p].role != PlayerRole::Human) {
            upkeep[p] = 0;
        }
    }
    return upkeep;
}

std::int32_t trade_balance(const Player& player) noexcept {
    return static_cast<std::int32_t>(player.sales / 480 - player.purchases / 480);
}

std::int32_t player_balance(const World& world, const assets::BuildingTable& buildings,
                            const assets::FigureTable& figures, PlayerId player) {
    const std::vector<std::int32_t> costs = running_costs(world, buildings);
    std::int64_t sum = 0;
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const City& c = world.cities()[i];
        if (c.owner == player) {
            sum += tax_income(c, buildings) - costs[i];
        }
    }
    const std::vector<std::uint32_t> upkeep = military_upkeep(world, figures);
    if (player < upkeep.size()) {
        sum -= upkeep[player];
    }
    if (const Player* p = world.player(player)) {
        sum += trade_balance(*p);
    }
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(sum, std::numeric_limits<std::int32_t>::min(),
                                                              std::numeric_limits<std::int32_t>::max()));
}

PlayerFigures player_figures(const World& world, const assets::BuildingTable& buildings,
                             const assets::FigureTable& figures, PlayerId player) {
    PlayerFigures f;
    const std::vector<std::int32_t> costs = running_costs(world, buildings);
    const std::vector<std::uint32_t> workers = city_workers(world, buildings);
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const City& c = world.cities()[i];
        if (c.owner == player) {
            f.inhabitants += city_einwohner(c, i < workers.size() ? workers[i] : 0);
            f.taxes += tax_income(c, buildings);
            f.running_costs += costs[i];
        }
    }
    const std::vector<std::uint32_t> upkeep = military_upkeep(world, figures);
    if (player < upkeep.size()) {
        f.military = upkeep[player];
    }
    if (const Player* p = world.player(player)) {
        f.sales = p->sales / 480;
        f.purchases = p->purchases / 480;
    }
    f.balance = f.taxes - f.running_costs - f.military + f.sales - f.purchases;
    return f;
}

MissionCheck check_mission(const World& world, const assets::BuildingTable& buildings,
                           const assets::FigureTable& figures, PlayerId player) {
    MissionCheck check;
    const Player* pl = world.player(player);
    const Mission* m = pl != nullptr ? world.mission_of(*pl) : nullptr;
    if (m == nullptr || (pl->role != PlayerRole::Human && pl->role != PlayerRole::Computer)) {
        return check;
    }
    const std::uint32_t required = m->goals & ~goal::kComputers;
    // A mission without goals (the endless games: a text only) does nothing, not even its loss
    // conditions (0x00475440).
    if (required == 0 || (pl->role == PlayerRole::Computer && (m->goals & goal::kComputers) == 0)) {
        return check;
    }
    const std::vector<std::uint32_t> workers = city_workers(world, buildings);
    std::vector<std::size_t> own;
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        if (world.cities()[i].owner == player) {
            own.push_back(i);
        }
    }
    std::uint32_t a = 0;
    if ((required & goal::kCities3) != 0) {
        a |= match_cities(own, workers, world, *m);
    }
    if ((required & goal::kEveryCity) != 0) {
        // The original counts it as met without any city (S-109).
        const bool all = std::all_of(
            own.begin(), own.end(), [&](std::size_t i) { return meets(world.cities()[i], workers[i], m->every_city); });
        if (all && !own.empty()) {
            a |= goal::kEveryCity;
        }
    }
    if ((required & goal::kTargetCity) != 0) {
        for (std::size_t i = 0; i < world.cities().size(); ++i) {
            const City& c = world.cities()[i];
            if (foreign(c.owner, m->target, player) && meets(c, workers[i], m->target_city)) {
                a |= goal::kTargetCity;
                break;
            }
        }
    }
    if ((required & goal::kCityCount) != 0 && static_cast<std::int64_t>(own.size()) >= m->min_cities) {
        a |= goal::kCityCount;
    }
    if ((required & goal::kInhabitants) != 0) {
        std::int64_t total = 0;
        for (const std::size_t i : own) {
            total += city_einwohner(world.cities()[i], workers[i]);
        }
        if (total >= m->min_inhabitants) {
            a |= goal::kInhabitants;
        }
    }
    if ((required & goal::kDefeatedCount) != 0 && pl->opponents_defeated >= m->min_defeated) {
        a |= goal::kDefeatedCount;
    }
    if ((required & goal::kDefeat) != 0) {
        bool all = true;
        for (const PlayerId q : m->defeat) {
            const Player* target = world.player(q);
            if (target == nullptr || q == player) {
                continue;  // nobody, or the mission's owner himself (missions shared by all players)
            }
            if (world.behaviour(*target) == FactionBehaviour::Natives) {
                // Natives are never defeated: their villages must be gone, the last destroyed
                // by this player.
                all = all && !has_city(world, q) && world.diplomacy().blamed(q) == player;
            } else {
                // A slot nobody plays counts as defeated (the original waits for it in vain).
                all = all && target->status != PlayerStatus::Playing;
            }
        }
        if (all) {
            a |= goal::kDefeat;
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        const std::uint32_t bit = i == 0 ? goal::kGoods1 : goal::kGoods2;
        if ((required & bit) == 0) {
            continue;
        }
        std::uint64_t stock = 0;
        for (const std::size_t c : own) {
            stock += world.cities()[c].stock(m->goods[i].ware);
        }
        if (stock >= m->goods[i].amount) {
            a |= bit;
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        const std::uint32_t bit = i == 0 ? goal::kIslands1 : goal::kIslands2;
        if ((required & bit) == 0) {
            continue;
        }
        // Every island of the list holds a city of the player and none of a human or computer
        // opponent. The original counts the player's cities, not the islands (S-110).
        bool ok = true;
        for (const std::uint16_t island : m->islands[i].islands) {
            bool mine = false;
            for (const City& c : world.cities()) {
                if (c.island != island) {
                    continue;
                }
                if (c.owner == player) {
                    mine = true;
                    continue;
                }
                const Player* o = world.player(c.owner);
                if (o != nullptr && o->active() && (o->role == PlayerRole::Human || o->role == PlayerRole::Computer)) {
                    ok = false;
                }
            }
            ok = ok && mine;
        }
        if (ok) {
            a |= bit;
        }
    }
    if ((required & goal::kTradeBalance) != 0 && trade_balance(*pl) >= m->min_trade_balance) {
        a |= goal::kTradeBalance;
    }
    if ((required & goal::kCastle) != 0 && pl->castles_built >= 1) {
        a |= goal::kCastle;
    }
    if ((required & goal::kCathedral) != 0 && pl->cathedrals_built >= 1) {
        a |= goal::kCathedral;
    }
    // The balance takes the longest; the original looks at it last, only when all else holds.
    if ((required & goal::kBalance) != 0 && ((required & ~goal::kBalance) & ~a) == 0 &&
        player_balance(world, buildings, figures, player) >= m->min_balance) {
        a |= goal::kBalance;
    }
    check.achieved = a;
    check.won = (required & ~a) == 0;
    // Loss conditions (0x00475440, rt + 0x04).
    if ((m->losses & loss::kProtectedCity) != 0) {
        bool kept = false;
        for (std::size_t i = 0; i < world.cities().size() && !kept; ++i) {
            const City& c = world.cities()[i];
            kept = foreign(c.owner, m->target, player) && meets(c, workers[i], m->protected_city);
        }
        check.lost = check.lost || !kept;
    }
    if ((m->losses & loss::kProtectedCount) != 0) {
        const auto n = std::count_if(world.cities().begin(), world.cities().end(),
                                     [&](const City& c) { return foreign(c.owner, m->target, player); });
        check.lost = check.lost || n < m->protected_min_cities;
    }
    return check;
}

bool owns_city(const World& world, PlayerId player) {
    return has_city(world, player);
}
bool owns_ship(const World& world, PlayerId player) {
    return has_ship(world, player);
}

}  // namespace o1602::sim
