// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the computer players read from the world (docs/re/ai.md): tiles, cities, land, money.
#include <algorithm>

#include "ai_engine.hpp"

#include "o1602/sim/growth.hpp"

namespace o1602::sim::ai {

bool Engine::inside(std::uint16_t island, std::int32_t x, std::int32_t y) const {
    return island < m_world.islands().size() && m_world.islands()[island].map.contains(x, y);
}

Tile Engine::tile(std::uint16_t island, std::int32_t x, std::int32_t y) const {
    Tile t;
    if (!inside(island, x, y)) {
        return t;
    }
    const IslandMap& map = m_world.islands()[island].map;
    return tile_of(map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)), x, y);
}

Tile Engine::tile_of(const MapCell& c, std::int32_t x, std::int32_t y) const {
    Tile t;
    t.city = c.city;
    t.owner = c.owner;
    if (c.empty()) {
        return t;
    }
    const assets::BuildingType* b = m_buildings.by_sprite(c.sprite);
    if (b == nullptr) {
        return t;
    }
    const auto [ox, oy] = IslandMap::origin_of(c, *b, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    t.ox = ox;
    t.oy = oy;
    t.origin = ox == x && oy == y;
    // Growing and withered crops count as the ripe crop (the original adds or takes one record).
    const assets::BuildingType* n = b;
    if (b->prod_kind == prod::kGrowing) {
        const assets::BuildingType* r = b->withered ? ripe_of_withered(m_buildings, *b) : ripe_record(m_buildings, *b);
        if (r != nullptr) {
            n = r;
        }
    }
    t.type = n;
    t.kind = n->kind;
    t.prod = n->prod_kind;
    t.ware = n->ware;
    t.raw = n->raw_ware;
    t.growth = static_cast<std::uint8_t>(std::uint32_t{n->edge_growth} * 32U / 128U);
    return t;
}

City* Engine::current_city(AiPlayer& p) {
    return p.has_city ? city(p.island, p.city) : nullptr;
}

const City* Engine::current_city(const AiPlayer& p) const {
    return p.has_city ? city(p.island, p.city) : nullptr;
}

City* Engine::home_city(AiPlayer& p) {
    if (!p.home_valid) {
        return nullptr;
    }
    City* c = city(p.home_island, p.home_city);
    return c != nullptr && c->owner == p.player ? c : nullptr;
}

CityPlan& Engine::plan_of(AiPlayer& p, std::uint16_t island, std::uint8_t c) {
    for (CityPlan& r : p.plans) {
        if (r.island == island && r.city == c) {
            return r;
        }
    }
    CityPlan r;
    r.island = island;
    r.city = c;
    r.stage_ms = now_ms();
    p.plans.push_back(r);
    return p.plans.back();
}

CityPlan* Engine::current_plan(AiPlayer& p) {
    if (!p.has_city) {
        return nullptr;
    }
    return &plan_of(p, p.island, p.city);
}

std::uint32_t Engine::own_city_count(PlayerId owner) const {
    std::uint32_t n = 0;
    for (const City& c : m_world.cities()) {
        n += c.owner == owner ? 1U : 0U;
    }
    return n;
}

std::uint32_t Engine::population(const City& c, std::size_t from) const {
    std::uint32_t n = 0;
    for (std::size_t k = from; k < c.inhabitants.size(); ++k) {
        n += c.inhabitants[k];
    }
    return n;
}

std::uint32_t Engine::demand(const City& c, std::uint8_t w) const {
    // 0x0047F7B0: food by all inhabitants, the other consumer goods by the classes that want them.
    if (w == m_buildings.food_ware()) {
        return static_cast<std::uint32_t>((std::uint64_t{population(c)} * m_buildings.food_need() * 6) >> 8);
    }
    std::uint64_t sum = 0;
    const auto& classes = m_buildings.resident_classes();
    for (std::size_t k = 0; k < classes.size() && k < c.inhabitants.size(); ++k) {
        for (const assets::ResidentClass::Need& n : classes[k].needs) {
            if (n.ware == w) {
                sum += std::uint64_t{c.inhabitants[k]} * n.amount;
            }
        }
    }
    return static_cast<std::uint32_t>((sum * 6) >> 8);
}

std::int32_t Engine::kontor_size(const City& c) const {
    // The rings of paving and towers go round the Kontor's record (3 x 3 tiles in the original).
    constexpr std::int32_t kOriginalSize = 3;
    if (!c.warehouse_x || !c.warehouse_y) {
        return kOriginalSize;
    }
    const Tile t = tile(c.island, *c.warehouse_x, *c.warehouse_y);
    return t.valid() && t.type->size_x != 0 ? static_cast<std::int32_t>(t.type->size_x) : kOriginalSize;
}

std::uint32_t Engine::kontor_level(const City& c) const {
    if (!c.warehouse_x || !c.warehouse_y) {
        return 0;
    }
    const Tile t = tile(c.island, *c.warehouse_x, *c.warehouse_y);
    if (!t.valid() || t.prod != prod::kKontor || t.type->destroyed) {
        return 0;
    }
    const std::uint32_t level = id::kontor_level(t.type->id);
    return level != 0 ? level : 1;
}

std::uint32_t Engine::market_count(const City& c) const {
    std::uint32_t n = 0;
    const IslandMap& map = m_world.islands()[c.island].map;
    for (const Production& pr : m_world.productions()) {
        if (!pr.used || pr.island != c.island || !map.contains(pr.x, pr.y)) {
            continue;
        }
        const MapCell& cell = map.top(pr.x, pr.y);
        if (cell.city != c.index || cell.empty()) {
            continue;
        }
        const assets::BuildingType* b = m_buildings.by_sprite(cell.sprite);
        if (b != nullptr && (b->prod_kind == prod::kMarket || b->prod_kind == prod::kKontor) && !b->destroyed) {
            ++n;
        }
    }
    return n;
}

std::uint32_t Engine::building_count(const City& c) const {
    std::uint32_t n = 0;
    const IslandMap& map = m_world.islands()[c.island].map;
    for (const Production& pr : m_world.productions()) {
        if (pr.used && pr.island == c.island && map.contains(pr.x, pr.y) && map.top(pr.x, pr.y).city == c.index) {
            ++n;
        }
    }
    for (const House& h : m_world.houses()) {
        if (h.island == c.island && h.city == c.index) {
            ++n;
        }
    }
    return n;
}

std::uint32_t Engine::free_land(std::uint16_t island, std::uint8_t own, std::uint32_t* percent) const {
    const IslandMap& map = m_world.islands()[island].map;
    const std::uint32_t total = map.width() * map.height();
    std::int64_t n = total;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const MapCell& c = map.top(x, y);
            if (c.city != MapCell::kNoCity && c.city != own) {
                --n;
                continue;
            }
            const assets::BuildingType* b = c.empty() ? nullptr : m_buildings.by_sprite(c.sprite);
            if (b == nullptr) {
                continue;
            }
            switch (b->kind) {
                case kind::kWallBeach:
                case kind::kTowerBeach:
                case kind::kRock:
                case kind::kSea:
                case kind::kSurf:
                case kind::kSurfCorner:
                case kind::kRiverMouth:
                case kind::kBeach:
                case kind::kBeachVariant:
                case kind::kBeachRuin:
                case kind::kPier:
                case kind::kSlope:
                case kind::kSlopeCorner:
                    n -= 1;
                    break;
                case kind::kRiver:
                case kind::kRiverCorner:
                case kind::kBridge:
                    n -= 4;
                    break;
                case kind::kBeachOuter:
                case kind::kBeachInner:
                    n -= 2;
                    break;
                default:
                    break;
            }
        }
    }
    const auto result = static_cast<std::uint32_t>(std::max<std::int64_t>(n, 0));
    if (percent != nullptr) {
        *percent = total >= 100 ? result / (total / 100) : 0;
    }
    return result;
}

std::uint32_t Engine::civ_level(const AiPlayer& p) const {
    return player(p).ship_step();
}

bool Engine::unlocked(const AiPlayer& p, const assets::BuildingType& b) const {
    return player(p).unlocked(b.infra);
}

bool Engine::affordable(const AiPlayer& p, const City& c, const assets::BuildingType& b) const {
    // 0x0041D120. The original compares the gold unsigned, so a debt passes (S-096); Open1602 does not.
    const auto& m = m_buildings.materials();
    const auto& cost = b.build_cost;
    return std::int64_t{player(p).money} >= cost.money && c.stock(m.wood) >= cost.wood &&
           c.stock(m.bricks) >= cost.bricks && c.stock(m.tools) >= cost.tools && c.stock(m.cannons) >= cost.cannons;
}

const assets::BuildingType* Engine::producer(std::uint8_t w, std::uint8_t raw) const {
    if (w >= m_producers.size()) {
        return nullptr;
    }
    for (const assets::BuildingType* b : m_producers[w]) {
        if (b->raw_ware == raw) {
            return b;
        }
    }
    return nullptr;
}

std::uint32_t Engine::fertility(std::uint16_t island, std::uint8_t w) const {
    return island < m_world.islands().size() ? island_fertility(m_world.islands()[island], w) : 0;
}

std::uint32_t Engine::full_crops(std::uint16_t island) const {
    std::uint32_t n = 0;
    for (const std::uint8_t w : {ware::kSpiceField, ware::kTobaccoField, ware::kWineField, ware::kCottonField,
                                 ware::kCocoaField, ware::kSugarField}) {
        n += fertility(island, w) == 128 ? 1U : 0U;
    }
    return n;
}

bool Engine::is_settler(PlayerId owner) const {
    const Player* pl = m_world.player(owner);
    return pl != nullptr && (pl->role == PlayerRole::Human || pl->role == PlayerRole::Computer);
}

std::uint32_t Engine::island_size(std::uint16_t island) const {
    const assets::IslandHeader& h = m_world.islands()[island].header;
    if (h.template_number != assets::IslandHeader::kNoTemplate) {
        return h.template_size;
    }
    const std::uint32_t area = std::uint32_t{h.width} * h.height;
    return area <= 900 ? 0U : area <= 1600 ? 1U : area <= 2600 ? 2U : area <= 4200 ? 3U : 4U;
}

std::uint32_t Engine::island_region(std::uint16_t island, std::uint32_t nests) const {
    const WorldIsland& i = m_world.islands()[island];
    return pirate_region(m_world, i.x + i.map.width() / 2, i.y + i.map.height() / 2, nests);
}

void Engine::add_goods(City& c, std::uint8_t w, std::uint32_t amount) {
    // The computer players' gifts to themselves are neither production nor trade: not booked in
    // the goods statistics (nobody else may see them anyway).
    c.ensure_ware(w);
    const std::uint32_t room = c.capacity > c.goods[w] ? c.capacity - c.goods[w] : 0;
    c.goods[w] += std::min(amount, room);
}

std::uint32_t Engine::remove_goods(City& c, std::uint8_t w, std::uint32_t amount) {
    c.ensure_ware(w);
    const std::uint32_t taken = std::min(amount, c.goods[w]);
    c.goods[w] -= taken;
    c.book(w, GoodsFlow::Construction, taken);
    return taken;
}

std::uint32_t Engine::army_strength(PlayerId owner) const {
    return m_sim.military().army_size(owner);
}

std::uint32_t Engine::total_population(PlayerId owner) const {
    std::uint32_t n = 0;
    for (const City& c : m_world.cities()) {
        if (c.owner == owner) {
            n += population(c);
        }
    }
    return n;
}

bool Engine::behind(const AiPlayer& p) const {
    // 0x0040E9B0: behind the leading human in inhabitants (scaled by the difficulty) or in units.
    std::uint32_t best_pop = 0;
    std::uint32_t best_army = 0;
    for (std::size_t h = 0; h < m_world.players().size(); ++h) {
        if (m_world.players()[h].role != PlayerRole::Human) {
            continue;
        }
        best_pop = std::max(best_pop, total_population(static_cast<PlayerId>(h)));
        best_army = std::max(best_army, army_strength(static_cast<PlayerId>(h)));
    }
    std::uint32_t threshold = best_pop;
    switch (difficulty()) {
        case 0:
            threshold = best_pop - best_pop / 4;
            break;
        case 1:
            break;
        case 2:
            threshold = best_pop + best_pop / 4;
            break;
        default:
            threshold = best_pop + best_pop / 2;
            break;
    }
    return total_population(p.player) < threshold || army_strength(p.player) < best_army;
}

bool Engine::threatened(const AiPlayer& p) const {
    // 0x0040F1D0: the current city was attacked within 30 s, or a human not at peace has soldiers
    // on its island (the original looks only at the first such human, S-102).
    const City* c = current_city(p);
    if (c == nullptr) {
        return false;
    }
    if (c->last_attacked_ms != 0 && now_ms() - c->last_attacked_ms < 30'000) {
        return true;
    }
    for (const Soldier& s : m_world.soldiers()) {
        if (s.island != c->island || s.owner == p.player || s.dying()) {
            continue;
        }
        const Player* o = m_world.player(s.owner);
        if (o != nullptr && o->role == PlayerRole::Human && m_world.diplomacy().hostile(p.player, s.owner)) {
            return true;
        }
    }
    return false;
}

}  // namespace o1602::sim::ai
