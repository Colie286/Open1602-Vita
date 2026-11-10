// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/world.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/volcanoes.hpp"

namespace o1602::sim {

using core::ErrorCode;
using core::make_error;

bool valid_name(std::string_view name, std::size_t max_length) noexcept {
    return !name.empty() && name.size() <= max_length &&
           std::none_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

World::World(std::uint32_t width, std::uint32_t height)
    : m_width(width), m_height(height), m_cells(std::size_t{width} * height) {}

core::Status World::add_island(WorldIsland island) {
    const std::uint32_t w = island.map.width();
    const std::uint32_t h = island.map.height();
    // In 64 bits: a position near 2^32 from a file must not wrap around into the map.
    if (std::uint64_t{island.x} + w > m_width || std::uint64_t{island.y} + h > m_height || w > kMaxIslandSide ||
        h > kMaxIslandSide) {
        return make_error(ErrorCode::OutOfRange,
                          "island " + std::to_string(island.header.number) + " leaves the world map");
    }
    if (w == 0 || h == 0) {  // no save could hold it (IO-5, HI-15)
        return make_error(ErrorCode::InvalidFormat, "island " + std::to_string(island.header.number) + " has no tiles");
    }
    static_assert(kMaxIslands <= kSea, "the sea's number is no island's");
    if (m_islands.size() >= kMaxIslands) {  // named limit instead of the original's 255 (L-003)
        return make_error(ErrorCode::LimitExceeded, "more than " + std::to_string(kMaxIslands) + " islands");
    }
    for (std::uint32_t ly = 0; ly < h; ++ly) {
        for (std::uint32_t lx = 0; lx < w; ++lx) {
            if (m_cells[std::size_t{island.y + ly} * m_width + island.x + lx].island != kSea) {
                return make_error(ErrorCode::InvalidFormat,
                                  "island " + std::to_string(island.header.number) + " overlaps another island");
            }
        }
    }
    const auto index = static_cast<std::uint16_t>(m_islands.size());
    for (std::uint32_t ly = 0; ly < h; ++ly) {
        for (std::uint32_t lx = 0; lx < w; ++lx) {
            m_cells[std::size_t{island.y + ly} * m_width + island.x + lx] =
                Cell{index, static_cast<std::uint16_t>(lx), static_cast<std::uint16_t>(ly)};
        }
    }
    m_islands.push_back(std::move(island));
    return core::Ok{};
}

City* World::city(std::uint16_t island, std::uint8_t index) noexcept {
    return const_cast<City*>(std::as_const(*this).city(island, index));
}

const City* World::city(std::uint16_t island, std::uint8_t index) const noexcept {
    const std::uint64_t key = (std::uint64_t{island} << 8 | index) << 32;
    const auto it = std::lower_bound(m_city_keys.begin(), m_city_keys.end(), key);
    if (it == m_city_keys.end() || (*it >> 32) != (key >> 32)) {
        return nullptr;
    }
    const City& c = m_cities[static_cast<std::size_t>(*it & 0xFFFF'FFFFU)];
    return c.island == island && c.index == index ? &c : nullptr;
}

void World::add_city(City city) {
    m_cities.push_back(std::move(city));
    index_cities();
}

void World::reveal_islands(Reveal reveal, std::uint16_t island) {
    // As the original, a city shows its island when it is founded, not the towns there at the start.
    if (reveal == Reveal::None || (reveal == Reveal::Original && island == kSea)) {
        return;
    }
    for (const City& c : m_cities) {
        if (island != kSea && c.island != island) {
            continue;
        }
        const Player* owner = player(c.owner);
        if (owner == nullptr || c.island >= m_islands.size()) {
            continue;
        }
        const FactionBehaviour b = behaviour(*owner);
        const bool shown =
            b == FactionBehaviour::Settlers ||
            (reveal == Reveal::All && (b == FactionBehaviour::Natives || b == FactionBehaviour::Pirates));
        if (!shown) {
            continue;
        }
        WorldIsland& isl = m_islands[c.island];
        for (std::size_t p = 0; p < m_players.size(); ++p) {
            isl.discover(static_cast<PlayerId>(p));
        }
    }
}

void World::set_cities(std::vector<City> cities) {
    m_cities = std::move(cities);
    index_cities();
}

void World::index_cities() {
    m_city_keys.clear();
    m_city_keys.reserve(m_cities.size());
    for (std::size_t i = 0; i < m_cities.size(); ++i) {
        const City& c = m_cities[i];
        m_city_keys.push_back((std::uint64_t{c.island} << 8 | c.index) << 32 | i);
    }
    std::sort(m_city_keys.begin(), m_city_keys.end());
}

void World::set_factions(std::vector<Faction> factions) {
    m_factions = factions.empty() ? base_factions() : std::move(factions);
    for (Player& p : m_players) {
        if (p.faction >= m_factions.size()) {
            p.faction = faction::kSettlers;
        }
    }
}

PlayerId World::first_of(FactionBehaviour behaviour) const noexcept {
    for (std::size_t p = 0; p < m_players.size(); ++p) {
        if (this->behaviour(m_players[p]) == behaviour) {
            return static_cast<PlayerId>(p);
        }
    }
    return kNoPlayer;
}

void World::add_ship(Ship ship) {
    const bool matched = m_ship_index.matches(m_ships, m_next_ship_id);
    if (ship.id == 0) {
        ship.id = m_next_ship_id;
    }
    m_next_ship_id = std::max(m_next_ship_id, ship.id + 1);
    m_ships.push_back(std::move(ship));
    m_ship_index.appended(m_ships, matched, m_next_ship_id);
}

void World::add_soldier(Soldier soldier) {
    const bool matched = m_soldier_index.matches(m_soldiers, m_next_soldier_id);
    if (soldier.id == 0) {
        soldier.id = m_next_soldier_id;
    }
    m_next_soldier_id = std::max(m_next_soldier_id, soldier.id + 1);
    m_soldiers.push_back(std::move(soldier));
    m_soldier_index.appended(m_soldiers, matched, m_next_soldier_id);
}

namespace {

/// Removes the entries `gone` says so, keeping the order of the others, and tells the index.
template <typename T, typename Gone>
void erase_by_id(std::vector<T>& list, IdIndex& index, std::uint32_t next_id, std::vector<std::uint32_t>& removed,
                 Gone&& gone) {
    const bool matched = index.matches(list, next_id);
    removed.clear();
    std::size_t kept = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (gone(list[i])) {
            removed.push_back(static_cast<std::uint32_t>(i));
        } else {
            if (kept != i) {
                list[kept] = std::move(list[i]);
            }
            ++kept;
        }
    }
    if (removed.empty()) {
        return;
    }
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(kept), list.end());
    index.erased(list, matched, removed);
}

}  // namespace

void World::erase_soldiers(const std::function<bool(const Soldier&)>& gone) {
    erase_by_id(m_soldiers, m_soldier_index, m_next_soldier_id, m_removed, gone);
}

void World::erase_ships(const std::function<bool(const Ship&)>& gone) {
    erase_by_id(m_ships, m_ship_index, m_next_ship_id, m_removed, gone);
}

const Soldier* World::soldier(std::uint32_t id) const noexcept {
    const std::size_t i = m_soldier_index.find(m_soldiers, id, m_next_soldier_id);
    return i == IdIndex::kNone ? nullptr : &m_soldiers[i];
}

Soldier* World::soldier(std::uint32_t id) noexcept {
    const std::size_t i = m_soldier_index.find(m_soldiers, id, m_next_soldier_id);
    return i == IdIndex::kNone ? nullptr : &m_soldiers[i];
}

const Ship* World::ship(std::uint32_t id) const noexcept {
    const std::size_t i = m_ship_index.find(m_ships, id, m_next_ship_id);
    return i == IdIndex::kNone ? nullptr : &m_ships[i];
}

Ship* World::ship(std::uint32_t id) noexcept {
    const std::size_t i = m_ship_index.find(m_ships, id, m_next_ship_id);
    return i == IdIndex::kNone ? nullptr : &m_ships[i];
}

namespace {

constexpr std::uint64_t production_key(std::uint16_t island, std::uint16_t x, std::uint16_t y) noexcept {
    return (std::uint64_t{island} << 32) | (std::uint64_t{y} << 16) | x;
}

/// Ships get the id of their slot in the original's table plus one.
constexpr std::uint32_t ship_id_of_slot(std::uint32_t slot) noexcept {
    return slot + 1;
}

/// Soldiers likewise.
constexpr std::uint32_t soldier_id_of_slot(std::uint32_t slot) noexcept {
    return slot + 1;
}

}  // namespace

namespace {

Target decode_ref(const World& world, std::uint32_t ref) {
    const auto type = static_cast<std::uint8_t>(ref);
    const auto b1 = static_cast<std::uint8_t>(ref >> 8);
    const auto b2 = static_cast<std::uint8_t>(ref >> 16);
    const auto b3 = static_cast<std::uint8_t>(ref >> 24);
    switch (type) {
        case 1:
        case 2:
        case 3:
        case 5:
            return Target::other_ship(ship_id_of_slot(ref >> 16));
        case 0x32:
        case 0x33:
        case 0x34:
            if (const auto island = world.island_index(b1)) {
                return Target::building(*island, b2, b3);
            }
            break;
        case 0x35:
            if (const auto island = world.island_index(b1)) {
                return Target::city_of(*island, b2);
            }
            break;
        case 4:
            return Target::soldier(soldier_id_of_slot(ref >> 16));
        case 6:
            // A tower by its slot: the building it stands in.
            for (const Tower& t : world.towers()) {
                if (t.source_slot == (ref >> 16)) {
                    return Target::building(t.island, t.x, t.y);
                }
            }
            break;
        case 0x37:
            return Target::position((std::uint32_t{b1} & 0x0FU) << 8 | b2, (std::uint32_t{b1} >> 4) << 8 | b3);
        case 0x38:
            return Target::spot((std::uint32_t{b1} & 0x0FU) << 8 | b2, (std::uint32_t{b1} >> 4) << 8 | b3);
        default:
            break;
    }
    return {};
}

}  // namespace

Target target_from_ref(const World& world, std::uint32_t ref) {
    Target t = decode_ref(world, ref);
    t.raw = ref;
    return t;
}

namespace {

std::uint8_t ware_of_producer(const assets::BuildingTable& buildings, std::uint32_t producer) {
    const assets::BuildingType* b = producer != 0 ? buildings.by_id(producer) : nullptr;
    return b != nullptr ? b->ware : std::uint8_t{0};
}

/// SHIP4 and HANDLER: after the cities, so that route stops can name them.
template <typename Warn>
void load_ships(World& world, const assets::ChunkFile& file, const assets::BuildingTable& buildings, Warn&& warn) {
    auto records = assets::load_ships(file);
    if (!records) {
        warn("SHIP4: " + records.error().message);
        return;
    }
    auto traders = assets::load_traders(file);
    if (!traders) {
        warn("HANDLER: " + traders.error().message);
    }
    constexpr std::uint8_t kKindTrader = 2;
    constexpr std::uint8_t kKindCart = 5;
    constexpr std::uint8_t kFlagRoute = 1U << 1;
    constexpr std::uint8_t kFlagOneWay = 1U << 3;
    constexpr std::uint8_t kFlagForSale = 1U << 2;
    constexpr std::uint8_t kFlagRepair = 1U << 4;
    for (const auto& s : *records) {
        if (!world.contains(s.x, s.y)) {
            warn("ship '" + s.name + "' outside the world map");
            continue;
        }
        Ship ship;
        ship.id = ship_id_of_slot(s.slot);
        if (world.ship(ship.id) != nullptr) {
            ship.id = 0;  // a duplicate slot: the next free id
        }
        ship.source_slot = s.slot;
        ship.name = s.name;
        ship.place(s.x, s.y);
        // Kind 5 is a player's trade cart on land (TRADER1), kept in the ship table.
        ship.figure = s.kind == kKindCart ? figure::kTradeCart : s.figure;
        ship.owner = player_from_original(s.owner);
        ship.direction = static_cast<std::uint8_t>(s.direction & 7U);
        ship.health = s.health;
        ship.cannons = s.cannons;
        // The original keeps eight holds for every ship and uses as many as the type has.
        ship.cargo.resize(s.cargo.size());
        std::size_t next = 0;
        for (const auto& c : s.cargo) {
            // Cargo names the good by its producer, like the warehouse chunk KONTOR2.
            const std::uint8_t ware = ware_of_producer(buildings, c.producer);
            if (ware != 0 && c.amount != 0) {
                if (next == ship.cargo.size()) {
                    ship.cargo.emplace_back();
                }
                ship.cargo[next++] = CargoSlot{ware, c.amount, c.price};
            }
        }
        ship.trader = s.kind == kKindTrader;
        ship.route.active = (s.flags & kFlagRoute) != 0;
        ship.route.one_way = (s.flags & kFlagOneWay) != 0;
        ship.for_sale = (s.flags & kFlagForSale) != 0;
        ship.repairing = (s.flags & kFlagRepair) != 0;
        ship.sale_factor = s.sale_factor != 0 ? s.sale_factor : std::uint16_t{128};
        if (s.ai_mission != 0 || s.ai_param != 0 || s.ai_counter != 0) {
            ship.original_job = Ship::OriginalJob{s.ai_mission, s.ai_param, s.ai_counter};
        }
        ship.route.current = static_cast<std::uint8_t>(s.route_stop & 3U);
        for (std::size_t r = 0; r < s.route.size(); ++r) {
            RouteStop& stop = ship.route.stops[r];
            stop.target = target_from_ref(world, s.route[r].ref);
            for (std::size_t e = 0; e < s.route[r].entries.size(); ++e) {
                const auto& in = s.route[r].entries[e];
                stop.entries[e] = RouteEntry{ware_of_producer(buildings, in.producer), in.unload, in.amount};
            }
        }
        ship.order = target_from_ref(world, s.order);
        ship.target = target_from_ref(world, s.target);
        ship.previous = target_from_ref(world, s.previous);
        if (ship.trader && traders) {
            for (const auto& t : *traders) {
                if (t.ship_slot != s.slot) {
                    continue;
                }
                for (const auto& g : t.goods) {
                    const std::uint8_t ware = ware_of_producer(buildings, g.producer);
                    if (ware == 0) {
                        continue;
                    }
                    if (ship.trader_goods.size() <= ware) {
                        ship.trader_goods.resize(std::size_t{ware} + 1);
                    }
                    ship.trader_goods[ware] = TraderGood{g.price, g.age, g.amount};
                }
            }
        }
        world.add_ship(std::move(ship));
    }
}

}  // namespace

const Production* World::production(std::uint16_t island, std::uint16_t x, std::uint16_t y) const {
    const auto it = m_production_index.find(production_key(island, x, y));
    return it == m_production_index.end() ? nullptr : &m_productions[it->second];
}

const House* World::house(std::uint16_t island, std::uint16_t x, std::uint16_t y) const {
    const auto it = m_house_index.find(production_key(island, x, y));
    return it == m_house_index.end() ? nullptr : &m_houses[it->second];
}

House* World::house(std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    const auto it = m_house_index.find(production_key(island, x, y));
    return it == m_house_index.end() ? nullptr : &m_houses[it->second];
}

void World::add_house(const House& house) {
    const auto [it, inserted] = m_house_index.try_emplace(production_key(house.island, house.x, house.y),
                                                          static_cast<std::uint32_t>(m_houses.size()));
    if (inserted) {
        m_houses.push_back(house);
    } else {
        m_houses[it->second] = house;
    }
}

void World::remove_house(std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    const auto it = m_house_index.find(production_key(island, x, y));
    if (it == m_house_index.end()) {
        return;
    }
    const std::uint32_t index = it->second;
    m_house_index.erase(it);
    if (index + 1 != m_houses.size()) {
        m_houses[index] = m_houses.back();
        const House& moved = m_houses[index];
        m_house_index[production_key(moved.island, moved.x, moved.y)] = index;
    }
    m_houses.pop_back();
}

void World::set_houses(std::vector<House> houses) {
    m_houses = std::move(houses);
    m_house_index.clear();
    for (std::uint32_t i = 0; i < m_houses.size(); ++i) {
        m_house_index[production_key(m_houses[i].island, m_houses[i].x, m_houses[i].y)] = i;
    }
}

void World::set_productions(std::vector<Production> productions, std::vector<std::uint32_t> free) {
    m_productions = std::move(productions);
    m_free_productions = std::move(free);
    m_production_index.clear();
    for (std::uint32_t i = 0; i < m_productions.size(); ++i) {
        if (m_productions[i].used) {
            m_production_index[production_key(m_productions[i].island, m_productions[i].x, m_productions[i].y)] = i;
        }
    }
}

std::optional<std::uint32_t> World::production_index(std::uint16_t island, std::uint16_t x, std::uint16_t y) const {
    const auto it = m_production_index.find(production_key(island, x, y));
    return it == m_production_index.end() ? std::nullopt : std::optional<std::uint32_t>(it->second);
}

void World::set_production(const Production& p) {
    const auto key = production_key(p.island, p.x, p.y);
    if (const auto it = m_production_index.find(key); it != m_production_index.end()) {
        m_productions[it->second] = p;
        m_productions[it->second].used = true;
        return;
    }
    std::uint32_t index = static_cast<std::uint32_t>(m_productions.size());
    if (!m_free_productions.empty()) {
        index = m_free_productions.back();
        m_free_productions.pop_back();
        m_productions[index] = p;
    } else {
        m_productions.push_back(p);
    }
    m_productions[index].used = true;
    m_production_index.emplace(key, index);
}

std::optional<std::uint32_t> World::remove_production(std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    const auto it = m_production_index.find(production_key(island, x, y));
    if (it == m_production_index.end()) {
        return std::nullopt;
    }
    const std::uint32_t index = it->second;
    m_production_index.erase(it);
    Production free;
    free.island = island;
    free.x = x;
    free.y = y;
    free.used = false;
    free.enabled = false;
    m_productions[index] = free;
    m_free_productions.push_back(index);
    return index;
}

void World::remove_city(std::uint16_t island, std::uint8_t index) {
    std::erase_if(m_cities, [&](const City& c) { return c.island == island && c.index == index; });
    index_cities();
}

std::optional<std::uint16_t> World::island_index(std::uint32_t number) const noexcept {
    for (std::size_t i = 0; i < m_islands.size(); ++i) {
        if (m_islands[i].header.number == number) {
            return static_cast<std::uint16_t>(i);
        }
    }
    return std::nullopt;
}

const assets::BuildingType* kontor_of(const World& world, const assets::BuildingTable& buildings, const City& city) {
    if (!city.warehouse_x || !city.warehouse_y || city.island >= world.islands().size()) {
        return nullptr;
    }
    const IslandMap& map = world.islands()[city.island].map;
    if (!map.contains(*city.warehouse_x, *city.warehouse_y)) {
        return nullptr;
    }
    const MapCell& c = map.top(*city.warehouse_x, *city.warehouse_y);
    return c.empty() ? nullptr : buildings.by_sprite(c.sprite);
}

bool trim_trade_settings(City& city, const assets::BuildingType& kontor) {
    if (kontor.max_wares == 0 && kontor.max_wares2 == 0) {
        return false;  // data without Maxware: no limit
    }
    std::size_t sell = 0;
    std::size_t buy = 0;
    for (const City::Trade& t : city.trade) {
        sell += t.sell ? 1U : 0U;
        buy += t.buy ? 1U : 0U;
    }
    if (sell <= kontor.max_wares && buy <= kontor.max_wares2) {
        return false;
    }
    for (City::Trade& t : city.trade) {
        t.sell = false;
        t.buy = false;
    }
    return true;
}

void fit_goodwill(World& world) {
    const std::size_t players = world.players().size();
    for (const City& c : world.cities()) {
        City* found = world.city(c.island, c.index);
        if (found == nullptr || !world.acts_as(found->owner, FactionBehaviour::Natives)) {
            continue;
        }
        City& city = *found;
        for (std::size_t p = city.goodwill.size(); p < players; ++p) {
            const PlayerRole role = world.players()[p].role;
            city.goodwill.push_back(role == PlayerRole::Human || role == PlayerRole::Computer ? kNativeGoodwillStart
                                                                                              : 0U);
        }
        for (std::uint32_t& g : city.goodwill) {
            g = std::min(g, kMaxNativeGoodwill);
        }
    }
}

PlayerId first_human_player(const World& world) noexcept {
    const auto& players = world.players();
    for (std::size_t i = 0; i < players.size() && i < kNoPlayer; ++i) {
        if (players[i].role == PlayerRole::Human) {
            return static_cast<PlayerId>(i);
        }
    }
    return kNoPlayer;
}

namespace {

/// Collects the warnings of a load, if the caller asked for them.
struct WarningSink {
    std::vector<std::string>* warnings = nullptr;

    void operator()(std::string message) const {
        if (warnings != nullptr) {
            warnings->push_back(std::move(message));
        }
    }
};

/// INSEL5 and the island templates: adds the islands with their maps to the world.
void add_islands(World& world, const std::vector<assets::Island>& islands, const assets::BuildingTable& buildings,
                 const assets::GameDirectory& game, std::vector<std::string>* warnings, const WarningSink& warn) {
    std::map<std::string, std::vector<assets::Island>> templates;  // cache: many islands share one
    for (const auto& island : islands) {
        const assets::Island* base = nullptr;
        if (const std::string path = island.header.template_path(); !path.empty()) {
            auto it = templates.find(path);
            if (it == templates.end()) {
                std::vector<assets::Island> loaded;
                if (const auto resolved = game.resolve(path)) {
                    if (auto tfile = assets::ChunkFile::load(*resolved)) {
                        if (auto tislands = assets::load_islands(*tfile)) {
                            loaded = std::move(tislands).value();
                        }
                    }
                }
                if (loaded.empty()) {
                    warn("island template not found or invalid: " + path);
                }
                it = templates.emplace(path, std::move(loaded)).first;
            }
            if (!it->second.empty()) {
                base = &it->second.front();
            }
        }
        WorldIsland wi{island.header, IslandMap::from_island(island, buildings, warnings, base), island.header.pos_x,
                       island.header.pos_y};
        if (auto status = world.add_island(std::move(wi)); !status) {
            warn(status.error().message);
        }
    }
}

/// PLAYER4: the players with their money, names and counters.
void add_players(World& world, const std::vector<assets::PlayerRecord>& players) {
    std::vector<Player> slots;
    for (const auto& p : players) {
        Player player{role_from_original(p.kind), banner_from_original(p.colour), p.money, p.name,
                      faction_from_original(p.kind)};
        player.infra_mask = p.infra_mask;
        player.castles_built = p.castles_built;
        player.cathedrals_built = p.cathedrals_built;
        player.arches_allowed = p.arches_allowed;
        player.arches_built = p.arches_built;
        player.monuments_allowed = p.monuments_allowed;
        player.monuments_built = p.monuments_built;
        player.soldiers_killed = p.soldiers_killed;
        player.soldiers_lost = p.soldiers_lost;
        player.ships_sunk = p.ships_sunk;
        player.ships_lost = p.ships_lost;
        if (p.kind == assets::PlayerKind::Human || p.kind == assets::PlayerKind::Computer) {
            // The loader 0x00477911 takes the scenario's switches for these slots only.
            player.scenario = {p.scenario_flags, p.rival_inhabitants, p.own_inhabitants, p.own_development,
                               p.rival_development};
        }
        slots.push_back(std::move(player));
    }
    world.set_players(std::move(slots));
}

/// PLAYER4 +0x40 .. +0x1FF: treaties, diplomacy events and blame between the players.
void load_diplomacy(World& world, const std::vector<assets::PlayerRecord>& players) {
    // Times in 1/10 s.
    Diplomacy& dipl = world.diplomacy();
    for (std::size_t a = 0; a < players.size() && a < world.players().size(); ++a) {
        const auto& p = players[a];
        const auto pa = static_cast<PlayerId>(a);
        for (std::size_t b = 0; b < 7 && b < world.players().size(); ++b) {
            Relation& r = dipl.relation(pa, static_cast<PlayerId>(b));
            r.peace = static_cast<Treaty>(p.peace[b] & 3U);
            r.trade = static_cast<Treaty>(p.trade[b] & 3U);
            r.war_time_ms = std::uint64_t{p.war_time[b]} * 100;
            r.tribute = p.tribute[b];
            r.trade_mood = p.trade_mood[b];
            r.damage = p.damage[b];
        }
        std::array<DiplomacyEvent, Diplomacy::kEvents> events{};
        for (std::size_t i = 0; i < p.events.size(); ++i) {
            const auto& e = p.events[i];
            if (e.type != 0 && e.type <= 13) {
                events[i] = {static_cast<DiplomacyEventType>(e.type), player_from_original(e.player),
                             std::uint64_t{e.time} * 100};
            }
        }
        dipl.set_events(pa, events);
        dipl.set_blamed(pa, player_from_original(p.blamed));
    }
}

/// STADT4 and KONTOR2: the cities, then their warehouses with stock and trade settings.
void load_cities(World& world, const assets::GameState& state, const WarningSink& warn) {
    for (const auto& c : state.cities) {
        // STADT4 names the island by its number; find its index in the world.
        const auto island = world.island_index(c.island);
        if (!island) {
            warn("city '" + c.name + "' on unknown island " + std::to_string(c.island));
            continue;
        }
        City city;
        city.island = *island;
        city.index = c.index;
        city.owner = player_from_original(c.owner);
        city.name = c.name;
        city.inhabitants.assign(c.inhabitants.begin(), c.inhabitants.end());
        // The loader of the original (0x00484C6C, 0x00484C13) turns a stored 0 into 128 in the
        // satisfaction, the tax rate and the food supply.
        const auto loaded = [](std::uint8_t v) { return v == 0 ? std::uint8_t{128} : v; };
        for (const std::uint8_t v : c.satisfaction) {
            city.satisfaction.push_back(loaded(v));
        }
        for (const std::uint8_t t : c.tax) {
            city.tax_rate.push_back(loaded(t));
        }
        city.food_ratio = loaded(c.food_ratio);
        city.material_lock = c.material_lock;
        city.population_message = c.population_message;
        city.purchases = c.purchases;
        city.sales = c.sales;
        city.goodwill.assign(c.goodwill.begin(), c.goodwill.end());
        world.add_city(std::move(city));
    }
    for (const auto& w : state.warehouses) {
        const auto island = world.island_index(w.island);
        City* city = island ? world.city(*island, w.city) : nullptr;
        if (city == nullptr) {
            warn("warehouse of unknown city " + std::to_string(w.island) + "/" + std::to_string(w.city));
            continue;
        }
        city->warehouse_x = w.x;
        city->warehouse_y = w.y;
        for (const auto& slot : w.wares) {
            city->ensure_ware(slot.ware);
            city->goods[slot.ware] = slot.stock();
            if (city->trade.size() <= slot.ware) {
                city->trade.resize(std::size_t{slot.ware} + 1);
            }
            city->trade[slot.ware] = City::Trade{slot.selling(),   slot.buying(), slot.sell_price(),
                                                 slot.buy_price(), slot.keep(),   slot.limit()};
        }
    }
}

/// SIEDLER: the houses that stand on a dwelling of the map.
void load_houses(World& world, const assets::GameState& state, const assets::BuildingTable& buildings,
                 const WarningSink& warn) {
    // Only on a dwelling (HAUS_PRODTYP Kind WOHNUNG); the class is the building's, as in the loader
    // of the original (0x00483EE0).
    for (const auto& r : state.houses) {
        const auto island = world.island_index(r.island);
        if (!island || !world.islands()[*island].map.contains(r.x, r.y)) {
            warn("house outside its island " + std::to_string(r.island));
            continue;
        }
        const MapCell& cell = world.islands()[*island].map.top(r.x, r.y);
        const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
        if (b == nullptr || b->prod_kind != prod::kHouse) {
            continue;
        }
        House h;
        h.island = *island;
        h.x = r.x;
        h.y = r.y;
        h.city = cell.city;
        h.resident_class = b->resident_class;
        h.inhabitants64 = r.inhabitants64;
        h.timer_group = r.timer_group;
        h.epoch = r.epoch;
        h.queue_epoch = r.queue_epoch;
        h.market_distance = r.market_distance;
        h.market = r.market;
        h.services_complete = r.services_complete;
        h.services = r.services;
        world.add_house(h);
    }
}

/// TIMERS: the game time, the island phase and the next city name.
void load_world_clocks(World& world, const assets::GameState& state) {
    if (state.game_time) {
        world.set_time_ms(std::uint64_t{*state.game_time} * 100);
    }
    if (state.island_phase) {
        world.set_island_phase(*state.island_phase);
    }
    if (state.city_name_index) {
        world.set_city_name_index(*state.city_name_index);
    }
}

/// INSEL5 header: deer phase, drought, discovery and the craters of each island.
void load_island_states(World& world, const assets::ChunkFile& file, const assets::BuildingTable& buildings) {
    const bool scenario = file.find("SZENE") != nullptr;
    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        WorldIsland& island = world.island(i);
        island.deer_phase = island.header.deer_phase;
        island.last_phase = island.header.last_phase;
        island.drought.strength = island.header.drought_strength;
        island.drought.end_ms = std::uint64_t{island.header.drought_end} * 100;
        island.drought.curse_pending = island.header.curse_pending;
        for (std::uint8_t p = 0; p < assets::kOriginalPlayerSlots; ++p) {
            if ((island.header.discovered >> p & 1U) != 0) {
                island.discover(p);
            }
            if ((static_cast<std::uint32_t>(island.header.explored) >> p & 1U) != 0) {
                island.explore(p);
            }
        }
        // Craters: the list of the savegame. The original searches them when it builds a world from
        // a scenario (SZENE chunk, 0x0048C5C0), whose list may be empty (Bündnispolitik), and for
        // islands without a list (INSEL3/4).
        constexpr std::size_t kCraterFields = 0x4C;
        if (island.header.raw.size() >= kCraterFields && !scenario) {
            for (const auto& c : island.header.craters) {
                island.craters.push_back(Crater{c.x, c.y, c.steps_left});
            }
        } else {
            island.craters = find_craters(island.map, buildings);
        }
        island.volcano_erupted = island.header.volcano_erupted;
    }
}

/// TIMERS: the volcano clock, the last eruption, and a running eruption started anew.
void load_volcanoes(World& world, const assets::GameState& state, const WarningSink& warn) {
    // The crater count is not taken from TIMERS +0xD0 but follows the islands (a scenario may carry
    // a stale one, S-063).
    VolcanoState& volcanoes = world.volcanoes();
    if (state.next_volcano) {
        volcanoes.next_ms = std::uint64_t{*state.next_volcano} * 100;
    }
    const auto crater_ref = [&](const assets::GameState::CraterRef& r) -> std::optional<CraterRef> {
        const auto island = world.island_index(r.island);
        if (!island) {
            return std::nullopt;
        }
        return CraterRef{*island, r.x, r.y};
    };
    if (state.last_volcano) {
        volcanoes.last = crater_ref(*state.last_volcano);
    }
    if (state.running_volcano) {
        // The original keeps no lava in its savegames and starts a running eruption anew, with a
        // new pattern and without the message (0x0048C0F0, S-062).
        if (const auto ref = crater_ref(*state.running_volcano)) {
            const auto seed = static_cast<std::uint16_t>(world.random(RandomStream::Islands).below(0x8000));
            if (!erupt(world, *ref, seed, false, nullptr)) {
                warn("running eruption on an island without that crater");
            }
        }
    }
}

/// TIMERS: the game settings, the drought clock and the clocks of the house groups.
void load_timers(World& world, const assets::GameState& state) {
    if (state.difficulty) {
        world.set_game_settings(World::GameSettings{*state.difficulty, state.settings.value_or(0),
                                                    state.pirate_interval.value_or(1500),
                                                    state.tutorial.value_or(0) != 0});
    }
    if (state.next_drought) {
        // A stored 0 becomes 60 minutes, as in the original's loader (0x00484E3C).
        const std::uint32_t next = *state.next_drought != 0 ? *state.next_drought : 36'000U;
        world.set_next_drought_ms(std::uint64_t{next} * 100);
    }
    if (state.house_clocks) {
        HouseClocks& clocks = world.house_clocks();
        for (std::size_t g = 0; g < HouseClocks::kGroups; ++g) {
            clocks.epoch[g] = state.house_clocks->epoch[g];
            clocks.time_ms[g] = state.house_clocks->time_ms[g] % (15'000U + 64U * static_cast<std::uint32_t>(g));
        }
    }
}

/// WERFT and MILITAR: the buildings of one kind with running costs (shipyards or castles).
void add_cost_buildings(World& world, const std::vector<assets::CostRecord>& records, CostBuilding::Kind kind) {
    for (const auto& r : records) {
        const auto island = world.island_index(r.island);
        if (!island || !world.islands()[*island].map.contains(r.x, r.y)) {
            continue;
        }
        CostBuilding b{kind, *island, r.x, r.y, r.enabled};
        if (kind == CostBuilding::Kind::Shipyard) {
            b.order = r.order < 4 ? r.order : CostBuilding::kNoOrder;
            b.progress = r.progress;
            b.wood = r.wood;
            b.countdown = r.countdown;
            b.repair_ship = r.repair != 0xFFFF ? ship_id_of_slot(r.repair) : 0U;
        } else {
            b.countdown = r.countdown;
            for (std::size_t w = 0; w < b.weapons.size(); ++w) {
                b.weapons[w] = r.weapons[w];
            }
            // Used slots are kept together at the front (removing one shifts the rest down).
            for (const auto& slot : r.slots) {
                if (slot.figure != 0) {
                    // Units by family, as soldiers (the colour follows the owner's banner).
                    const auto variant = assets::FigureTable::unit_variant(slot.figure);
                    const auto figure = static_cast<std::uint16_t>(variant ? variant->family : slot.figure);
                    b.slots.push_back(CastleSlot{figure, slot.training, slot.armed, slot.health, slot.progress});
                }
            }
        }
        world.add_cost_building(b);
    }
}

/// MARKT2: demand and supply of each city per ware.
void load_consumption(World& world, const assets::GameState& state, const assets::BuildingTable& buildings,
                      const WarningSink& warn) {
    for (const auto& m : state.consumption) {
        const auto island = world.island_index(m.island);
        City* city = island ? world.city(*island, m.city) : nullptr;
        if (city == nullptr) {
            warn("consumption of unknown city " + std::to_string(m.island) + "/" + std::to_string(m.city));
            continue;
        }
        for (const auto& g : m.goods) {
            const assets::BuildingType* producer = buildings.by_id(g.producer_id);
            if (producer == nullptr || producer->ware == 0) {
                continue;
            }
            city->ensure_ware(producer->ware);
            if (city->supply.size() < city->goods.size()) {
                city->supply.resize(city->goods.size());
            }
            City::Supply& s = city->supply[producer->ware];
            s.demand = g.demand;
            s.supplied = g.supplied;
            s.ratio = g.ratio;
        }
    }
}

/// PRODLIST2: the production buildings with their stores and cycles.
void load_production(World& world, const assets::GameState& state, const WarningSink& warn) {
    for (const auto& p : state.production) {
        const auto island = world.island_index(p.island);
        if (!island || p.x >= world.islands()[*island].map.width() || p.y >= world.islands()[*island].map.height()) {
            warn("production building outside its island " + std::to_string(p.island));
            continue;
        }
        Production q;
        q.island = *island;
        q.x = p.x;
        q.y = p.y;
        q.enabled = p.enabled;
        q.rate = p.rate;
        q.anim_phase = p.anim_phase;
        q.idle_cycles = p.idle_cycles;
        q.flags = p.flags;
        q.countdown = p.countdown;
        q.raw = p.raw;
        q.raw2 = p.raw2;
        q.stock = p.stock;
        q.produced = p.produced;
        q.elapsed = p.elapsed;
        world.set_production(q);
    }
}

/// TURM: the towers, placed on the building they name.
void load_towers(World& world, const assets::GameState& state, const WarningSink& warn) {
    for (const auto& r : state.towers) {
        const Target building = target_from_ref(world, r.building);
        if (building.kind != Target::Kind::Building) {
            warn("tower without its building");
            continue;
        }
        Tower t;
        t.island = building.island;
        t.x = static_cast<std::uint16_t>(building.x);
        t.y = static_cast<std::uint16_t>(building.y);
        t.owner = player_from_original(r.owner);
        t.figure = r.figure;
        t.direction = r.direction;
        t.source_slot = r.slot;
        world.towers().push_back(t);
    }
}

/// SOLDAT3: the soldiers, then their homes, targets and waypoints.
void load_soldiers(World& world, const assets::GameState& state, const WarningSink& warn) {
    // The ids taken so far in a set of their own (HI-5): asking the world rebuilt its id index after
    // every soldier whose slot came out of order, O(n^2 log n) for a hostile file.
    std::unordered_set<std::uint32_t> taken;
    taken.reserve(world.soldiers().size() + state.soldiers.size());
    for (const Soldier& s : world.soldiers()) {
        taken.insert(s.id);
    }
    for (const auto& r : state.soldiers) {
        Soldier soldier;
        soldier.id = soldier_id_of_slot(r.slot);
        if (taken.contains(soldier.id)) {
            soldier.id = 0;  // a duplicate slot: the next free id
        }
        soldier.source_slot = r.slot;
        soldier.place_cell(r.x2, r.y2);  // the loader: v * 0.5 + 0.25, the centre of the cell
        if (!world.contains(r.x2 / 2U, r.y2 / 2U)) {
            warn("soldier outside the world map");
            continue;
        }
        soldier.figure = r.figure;
        if (const auto variant = assets::FigureTable::unit_variant(r.figure)) {
            soldier.figure = static_cast<std::uint16_t>(variant->family);
        }
        soldier.owner = player_from_original(r.owner);
        soldier.direction = r.direction;
        soldier.anim = r.anim;
        soldier.health = r.health;
        const World::Cell& cell = world.cell(r.x2 / 2U, r.y2 / 2U);
        soldier.island = cell.island;
        soldier.radius = r.radius != 0 ? r.radius : std::uint8_t{0x2D};
        soldier.patrol.active = (r.patrol & 1U) != 0;
        soldier.patrol.one_way = (r.patrol & 2U) != 0;
        soldier.patrol.index = static_cast<std::uint8_t>(r.waypoint & 1U);
        world.add_soldier(soldier);
        taken.insert(world.soldiers().back().id);
    }
    // References after all units are known (they may name each other), each to the first soldier
    // loaded from that slot.
    std::unordered_map<std::uint32_t, std::size_t> by_slot;
    by_slot.reserve(world.soldiers().size());
    for (std::size_t i = 0; i < world.soldiers().size(); ++i) {
        by_slot.try_emplace(world.soldiers()[i].source_slot, i);
    }
    for (const auto& r : state.soldiers) {
        const auto it = by_slot.find(r.slot);
        if (it == by_slot.end()) {
            continue;
        }
        Soldier& s = world.soldiers()[it->second];
        s.home = target_from_ref(world, r.home);
        s.target = target_from_ref(world, r.target);
        s.patrol.points = {target_from_ref(world, r.waypoints[0]), target_from_ref(world, r.waypoints[1])};
    }
}

/// HIRSCH2: the animals and the deer spots they take.
void load_animals(World& world, const assets::GameState& state, const WarningSink& warn) {
    // HIRSCH2 stores only position and timer; the kind follows the island's climate: deer
    // (HIRSCH) in the north, antelopes (ANTILOPE) in the south (confirmed in the running game).
    for (const auto& a : state.animals) {
        const auto island = world.island_index(a.island);
        if (!island || a.x >= world.islands()[*island].map.width() || a.y >= world.islands()[*island].map.height()) {
            warn("animal outside its island " + std::to_string(a.island));
            continue;
        }
        const bool south = (world.islands()[*island].header.climate & 1U) != 0;
        world.add_animal(Animal{*island, a.x, a.y, south ? figure::kAntelope : figure::kDeer});
        for (DeerSpot& spot : world.island(*island).deer_spots) {
            if (!spot.used) {
                spot = DeerSpot{true, a.x, a.y, std::uint64_t{a.timer} * 100};
                break;
            }
        }
    }
}

/// TIMERS and ROHWACHS2: the growth clocks and the growing tiles.
void load_growth(World& world, const assets::GameState& state, const assets::BuildingTable& buildings,
                 const WarningSink& warn) {
    // Growing tiles the file lists no entry for get one (add_missing_growth).
    GrowthState& growth = world.growth();
    if (state.growth_clocks) {
        growth.counter = state.growth_clocks->counter;
        for (std::size_t c = 0; c < GrowthState::kClasses; ++c) {
            growth.time_ms[c] = state.growth_clocks->time_ms[c] % growth_period_ms(static_cast<std::uint32_t>(c));
        }
    }
    for (const auto& r : state.growth) {
        const auto island = world.island_index(r.island);
        if (!island || !world.islands()[*island].map.contains(r.x, r.y)) {
            warn("growing tile outside its island " + std::to_string(r.island));
            continue;
        }
        GrowthEntry e;
        e.island = *island;
        e.x = r.x;
        e.y = r.y;
        e.growth_class = static_cast<std::uint8_t>(std::min<std::uint8_t>(r.growth_class, 31));
        e.seen = static_cast<std::uint8_t>(r.seen & 7U);
        e.stage = r.stage;
        growth.entries.push_back(e);
    }
    add_missing_growth(world, buildings);
}

/// AUFTRAG4: the missions, with a slot for each one a player names.
void load_missions(World& world, const assets::ChunkFile& file, const std::vector<assets::PlayerRecord>& players,
                   std::vector<std::string>* warnings, const WarningSink& warn) {
    // docs/re/missions.md: the record of slot s goes to mission s; players name theirs by slot.
    if (auto records = assets::load_missions(file); !records) {
        warn(records.error().message);
    } else {
        std::vector<Mission> missions;
        for (const auto& r : *records) {
            if (r.slot >= 0xFF) {
                warn("mission slot " + std::to_string(r.slot) + " out of range");
                continue;
            }
            if (r.slot >= missions.size()) {
                missions.resize(std::size_t{r.slot} + 1);
            }
            missions[r.slot] = mission_from_record(r, warnings);
        }
        world.set_missions(std::move(missions));
    }
    // The original always has its 4 slots (empty ones zeroed): a player may name one the file
    // does not fill.
    constexpr std::uint8_t kOriginalMissionSlots = 4;
    for (const auto& r : players) {
        if (r.mission < kOriginalMissionSlots && r.mission >= world.missions().size()) {
            auto missions = world.missions();
            missions.resize(std::size_t{r.mission} + 1);
            world.set_missions(std::move(missions));
        }
    }
}

/// PLAYER4: status, mission and defeat counters, once the cities, ships and missions are known.
void load_player_states(World& world, const std::vector<assets::PlayerRecord>& players, const WarningSink& warn) {
    for (std::size_t i = 0; i < players.size() && i < world.players().size(); ++i) {
        const auto& r = players[i];
        Player* pp = world.player(static_cast<PlayerId>(i));
        if (pp == nullptr) {
            continue;
        }
        Player& p = *pp;
        p.status = status_from_original(r.kind);
        // Only humans and computers have missions (Player_Tick reads the slot for controller 0, 0x0C).
        if (r.mission < world.missions().size() && (p.role == PlayerRole::Human || p.role == PlayerRole::Computer)) {
            p.mission = r.mission;
        } else if (r.mission != 0xFF && (p.role == PlayerRole::Human || p.role == PlayerRole::Computer)) {
            warn("player " + std::to_string(i) + ": mission " + std::to_string(r.mission) + " not in the file");
        }
        p.opponents_defeated = r.arches_allowed;
        p.bankrupt_checks = static_cast<std::uint8_t>(std::min<std::uint16_t>(r.defeat_counter, 39));
        // A human or computer with no city and no ship is a slot nobody plays (0x0048C0F0).
        if (p.status == PlayerStatus::Playing && (p.role == PlayerRole::Human || p.role == PlayerRole::Computer) &&
            !owns_city(world, static_cast<PlayerId>(i)) && !owns_ship(world, static_cast<PlayerId>(i))) {
            p.status = PlayerStatus::Absent;
        }
    }
}

/// O1602: Open1602's own chunk, the state the original's chunks cannot hold.
void load_extension(World& world, const assets::ChunkFile& file, const assets::BuildingTable& buildings,
                    std::vector<std::string>* warnings, const WarningSink& warn) {
    for (const assets::Chunk& c : file.chunks()) {
        if (c.name == kExtensionChunk) {
            constexpr std::size_t kHeader = 20;
            const auto before = file.data().subspan(0, c.offset - kHeader);
            if (auto status = apply_extension(world, buildings, file.payload(c), before, warnings); !status) {
                warn(status.error().message + " (loaded without it)");
            }
            break;
        }
    }
}

/// Clears the trade settings of each player city that trades more wares than its Kontor allows.
void trim_all_trade_settings(World& world, const assets::BuildingTable& buildings) {
    // 0x00481EE0 on loading: such a city loses all its trade settings.
    for (const City& c : world.cities()) {
        const Player* owner = world.player(c.owner);
        const assets::BuildingType* kontor = kontor_of(world, buildings, c);
        if (owner != nullptr && (owner->role == PlayerRole::Human || owner->role == PlayerRole::Computer) &&
            kontor != nullptr && kontor->prod_kind == prod::kKontor) {
            trim_trade_settings(*world.city(c.island, c.index), *kontor);
        }
    }
}

}  // namespace

core::Result<World> load_world(const assets::ChunkFile& file, const assets::BuildingTable& buildings,
                               const assets::GameDirectory& game, std::vector<std::string>* warnings,
                               const WorldStart& start) {
    if (plans_islands(file)) {
        // The SZENE handler of the original makes the islands while the scenario loads (0x0048C5C0).
        auto generated = generate_islands(file, game, buildings, start, warnings);
        if (!generated) {
            return generated.error();
        }
        return load_world(generated->file, buildings, game, warnings, start);
    }
    auto islands = assets::load_islands(file);
    if (!islands) {
        return islands.error();
    }
    const WarningSink warn{warnings};
    // The stages run in this order, and later ones rely on earlier ones: cities, houses, production
    // and growth need the islands; ships name cities; soldiers name towers by slot; the player
    // states look at cities, ships and missions; the running eruption draws its pattern before the
    // reseed; the extension chunk overwrites what the original's chunks hold; the trade trim sees
    // the final cities.
    // The original's map is always 500 x 350; Open1602's own worlds name their size.
    const auto size = map_size(file);
    World world(size ? size->first : World::kOriginalWidth, size ? size->second : World::kOriginalHeight);
    add_islands(world, *islands, buildings, game, warnings, warn);
    auto players = assets::load_players(file);
    if (!players) {
        return players.error();
    }
    add_players(world, *players);
    load_diplomacy(world, *players);
    auto state = assets::load_game_state(file);
    if (!state) {
        return state.error();
    }
    load_cities(world, *state, warn);
    load_ships(world, file, buildings, warn);
    load_houses(world, *state, buildings, warn);
    load_world_clocks(world, *state);
    load_island_states(world, file, buildings);
    load_volcanoes(world, *state, warn);
    load_timers(world, *state);
    add_cost_buildings(world, state->shipyards, CostBuilding::Kind::Shipyard);
    add_cost_buildings(world, state->castles, CostBuilding::Kind::Castle);
    load_consumption(world, *state, buildings, warn);
    load_production(world, *state, warn);
    load_towers(world, *state, warn);
    load_soldiers(world, *state, warn);
    load_animals(world, *state, warn);
    load_growth(world, *state, buildings, warn);
    load_missions(world, file, *players, warnings, warn);
    load_player_states(world, *players, warn);
    prepare_missions(world);
    // The generators of the simulation (fires, plague, houses, computer players ...) follow the
    // seed of this start, so that games differ by more than their islands; seed 0 keeps the
    // streams of earlier versions. A game that carries Open1602's state continues its own
    // generators (the extension chunk, loaded next).
    world.reseed(start.seed + kDefaultSimulationSeed);
    load_extension(world, file, buildings, warnings, warn);
    trim_all_trade_settings(world, buildings);
    if (auto status = validate(world); !status) {
        return status.error();
    }
    world.set_source(std::make_shared<const assets::ChunkFile>(file));
    return world;
}

}  // namespace o1602::sim
