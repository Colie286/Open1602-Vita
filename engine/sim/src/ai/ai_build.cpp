// SPDX-License-Identifier: GPL-3.0-or-later
//
// The building tasks of the computer players (docs/re/ai.md, "Stages"): each runs its site
// search step by step and builds the best site through the normal build order; roads, fields,
// the survey of a city (task 8) and the maintenance sweep.
#include <algorithm>
#include <array>
#include <optional>

#include "ai_engine.hpp"
#include "ai_tables.hpp"

#include "o1602/sim/growth.hpp"
#include "o1602/sim/houses.hpp"

namespace o1602::sim::ai {

namespace {

namespace hs = house_service;

constexpr std::array<std::array<std::int32_t, 2>, 4> kStep{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

bool is_workshop_task(std::uint8_t task) {
    switch (task) {
        case kBakery:
        case kMill:
        case kWeaving:
        case kButcher:
        case kTobaccoGoods:
        case kSmelter:
        case kToolsmith:
        case kTailor:
        case kRum:
        case kSwords:
        case kMuskets:
        case kGoldsmith:
            return true;
        default:
            return false;
    }
}

bool is_farm_task(std::uint8_t task) {
    switch (task) {
        case kCotton:
        case kWine:
        case kSpices:
        case kCocoa:
        case kCattle:
        case kTobacco:
        case kSheep:
        case kSugar:
            return true;
        default:
            return false;
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Slot bookkeeping (0x0040FB20, 0x0040F560, 0x0040FAD0, 0x0040FB50)

void Engine::slot_built(AiPlayer& p, std::uint8_t slot) {
    if (CityPlan* r = current_plan(p)) {
        ++r->slots[slot].count;
    }
    p.present[slot] = true;
}

void Engine::slot_ok(AiPlayer& p, std::uint8_t slot) {
    if (CityPlan* r = current_plan(p)) {
        r->slots[slot].attempts = 0;
        r->slots[slot].fail_level = 0;
    }
}

std::uint16_t Engine::slot_fail(AiPlayer& p, std::uint8_t slot) {
    CityPlan* r = current_plan(p);
    if (r == nullptr) {
        return 2;
    }
    SlotState& s = r->slots[slot];
    ++s.attempts;
    if (s.attempts >= 2 && s.fail_level < 2) {
        ++s.fail_level;
        s.attempts = 0;
    }
    return s.fail_level >= 2 ? s.fail_level : 0;
}

std::int32_t Engine::slot_present(const AiPlayer& p, std::uint8_t slot) const {
    if (p.surveyed < p.cities) {
        return -1;  // not every city surveyed since the init: counts as present
    }
    return p.present[slot] ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
// Building through the build order

std::size_t Engine::build(AiPlayer& p, std::vector<BuildItem> items, bool orient) {
    if (items.empty()) {
        return 0;
    }
    BuildOrder order;
    order.player = p.player;
    order.island = p.island;
    order.items = std::move(items);
    order.orient_to_road = orient;
    const CommandResult r = execute(order);
    return r.build ? r.build->placed : 0;
}

std::size_t Engine::demolish_tile(AiPlayer& p, std::int32_t x, std::int32_t y) {
    log(p, "demolish %d,%d id %u", x, y, tile(p.island, x, y).valid() ? tile(p.island, x, y).type->id : 0U);
    DemolishOrder order;
    order.player = p.player;
    order.island = p.island;
    order.from = TilePos{x, y};
    order.to = TilePos{x, y};
    return execute(order).demolished;
}

std::int32_t Engine::road_line(AiPlayer& p, std::int32_t x, std::int32_t y, std::int32_t len, std::uint8_t dir,
                               bool force) {
    // 0x00411450: a straight dirt road with wooden bridges, at most 13 tiles (the original makes
    // 6 of a longer one). A line that runs into a road ends there.
    (void)force;
    len = std::clamp(len, 0, 13);
    const assets::BuildingType* dirt = by_id(id::kRoadDirt);
    const assets::BuildingType* bridge = by_id(id::kBridgeWood);
    if (dirt == nullptr || len == 0) {
        return 0;
    }
    const auto& d = kStep[dir & 3U];
    std::vector<BuildItem> items;
    std::int32_t cx = x;
    std::int32_t cy = y;
    for (std::int32_t i = 0; i < len; ++i, cx += d[0], cy += d[1]) {
        const Tile t = tile(p.island, cx, cy);
        if (!t.valid() || (t.city != p.city)) {
            break;
        }
        if (t.kind == kind::kRoad || t.kind == kind::kBridge) {
            if (!items.empty()) {
                break;
            }
            continue;
        }
        if (t.kind == kind::kForest || t.kind == kind::kGround || t.kind == kind::kRuin) {
            items.push_back(BuildItem{dirt->id, cx, cy, 0, 0});
            continue;
        }
        if (t.kind == kind::kRiver && bridge != nullptr) {
            items.push_back(BuildItem{bridge->id, cx, cy, static_cast<std::uint8_t>(dir & 1U), 0});
            continue;
        }
        break;
    }
    if (items.empty()) {
        return 0;
    }
    // One more tile to reach a road beside the end.
    const Tile after = tile(p.island, cx, cy);
    if (after.valid() && after.city == p.city && after.kind != kind::kRoad &&
        (after.kind == kind::kForest || after.kind == kind::kGround || after.kind == kind::kRuin)) {
        bool next_to_road = false;
        for (const auto& n : kStep) {
            const Tile o = tile(p.island, cx + n[0], cy + n[1]);
            next_to_road =
                next_to_road || (o.valid() && o.city == p.city && (o.kind == kind::kRoad || o.kind == kind::kBridge));
        }
        if (next_to_road) {
            items.push_back(BuildItem{dirt->id, cx, cy, 0, 0});
        }
    }
    return static_cast<std::int32_t>(build(p, std::move(items), false));
}

std::int32_t Engine::road_three(AiPlayer& p, std::int32_t x, std::int32_t y, std::uint8_t dir, std::int32_t right,
                                std::int32_t left, std::int32_t back, bool force) {
    // 0x004118A0: three straight roads from one point; seven or more tiles open new sites.
    std::int32_t placed = road_line(p, x, y, back, static_cast<std::uint8_t>((dir + 2U) & 3U), true);
    placed += road_line(p, x, y, right, static_cast<std::uint8_t>((dir + 1U) & 3U), force);
    placed += road_line(p, x, y, left, static_cast<std::uint8_t>((dir + 3U) & 3U), force);
    if (placed >= 7) {
        if (CityPlan* r = current_plan(p)) {
            for (SlotState& s : r->slots) {
                s.attempts = 0;
            }
            r->flags &= ~city_flag::kMarket;
        }
        push(p, kLayout, true);
    }
    return placed;
}

void Engine::plaza_ring(AiPlayer& p) {
    // 0x00418940: a square around the Kontor.
    const City* c = current_city(p);
    const assets::BuildingType* square = by_id(id::kSquare);
    if (c == nullptr || square == nullptr || !c->warehouse_x || !c->warehouse_y) {
        return;
    }
    const Tile k = tile(p.island, *c->warehouse_x, *c->warehouse_y);
    if (!k.valid()) {
        return;
    }
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    rotated_size(*k.type, island_of(p.island).map.top(*c->warehouse_x, *c->warehouse_y).rotation, w, h);
    const std::int32_t x0 = *c->warehouse_x - 1;
    const std::int32_t y0 = *c->warehouse_y - 1;
    const std::int32_t x1 = *c->warehouse_x + static_cast<std::int32_t>(w);
    const std::int32_t y1 = *c->warehouse_y + static_cast<std::int32_t>(h);
    std::vector<BuildItem> items;
    for (std::int32_t y = y0; y <= y1; ++y) {
        for (std::int32_t x = x0; x <= x1; ++x) {
            if (x != x0 && x != x1 && y != y0 && y != y1) {
                continue;
            }
            const Tile t = tile(p.island, x, y);
            if (t.valid() && t.city == p.city &&
                (t.kind == kind::kForest || t.kind == kind::kGround || t.kind == kind::kRuin)) {
                items.push_back(BuildItem{square->id, x, y, 0, 0});
            }
        }
    }
    build(p, std::move(items), false);
}

bool in_wood_reach(const std::vector<WoodReach>& reaches, std::int32_t x, std::int32_t y) {
    return std::any_of(reaches.begin(), reaches.end(), [&](const WoodReach& w) {
        const std::int32_t dx = std::max({w.x0 - x, 0, x - w.x1});
        const std::int32_t dy = std::max({w.y0 - y, 0, y - w.y1});
        const std::vector<std::uint8_t>& circle = circle_table(w.radius);
        return dy <= w.radius && static_cast<std::size_t>(dy) < circle.size() &&
               dx <= circle[static_cast<std::size_t>(dy)];
    });
}

std::vector<WoodReach> Engine::woodcutters_near(AiPlayer& p, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                                std::int32_t y1, std::int32_t n) {
    std::vector<WoodReach> reaches;
    const IslandMap& map = island_of(p.island).map;
    rings(p, RingFilter::Own, x0, y0, x1, y1, n + kWoodcutterReach,
          [&](const Tile& t, std::int32_t, std::int32_t, std::int32_t) {
              if (!t.origin || t.raw != ware::kTree || t.ware != ware::kWood || t.owner != p.player) {
                  return;
              }
              std::uint32_t w = 0;
              std::uint32_t h = 0;
              rotated_size(*t.type,
                           map.top(static_cast<std::uint32_t>(t.ox), static_cast<std::uint32_t>(t.oy)).rotation, w, h);
              reaches.push_back(WoodReach{t.ox, t.oy, t.ox + static_cast<std::int32_t>(w) - 1,
                                          t.oy + static_cast<std::int32_t>(h) - 1, t.type->radius});
          });
    return reaches;
}

void Engine::fill_fields(AiPlayer& p, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                         std::int32_t rings_n, std::uint8_t field_ware, std::uint8_t radius) {
    // 0x00411B50: fields in rings around the farm, inside its reach, on ruins, meadows and trees.
    // Fix S-122: other crops leave the trees in reach of the city's own woodcutters standing; the
    // original plants over them, and the woodcutter replants and is torn down in turn.
    const assets::BuildingType* ripe = producer(field_ware, 0);
    if (ripe == nullptr) {
        return;
    }
    const assets::BuildingType* growing = growing_record(m_buildings, *ripe);
    const assets::BuildingType* base = growing != nullptr ? growing : ripe;
    const std::vector<std::uint8_t> circle = circle_half_widths(radius);
    std::vector<WoodReach> woods;
    if (field_ware != ware::kTree) {
        woods = woodcutters_near(p, x0, y0, x1, y1, rings_n);
    }
    std::vector<BuildItem> items;
    for (std::int32_t r = 1; r <= rings_n; ++r) {
        const std::int32_t l = x0 - r;
        const std::int32_t rt = x1 + r;
        const std::int32_t t = y0 - r;
        const std::int32_t b = y1 + r;
        std::vector<std::pair<std::int32_t, std::int32_t>> ring;
        for (std::int32_t x = l; x <= rt; ++x) {
            ring.emplace_back(x, t);
        }
        for (std::int32_t y = t + 1; y < b; ++y) {
            ring.emplace_back(l, y);
            ring.emplace_back(rt, y);
        }
        for (std::int32_t x = l; x <= rt; ++x) {
            ring.emplace_back(x, b);
        }
        for (const auto& [x, y] : ring) {
            const Tile tl = tile(p.island, x, y);
            if (!tl.valid() || tl.city != p.city || tl.ware == field_ware || !tl.origin) {
                continue;
            }
            const std::int32_t dx = std::max({x0 - x, 0, x - x1});
            const std::int32_t dy = std::max({y0 - y, 0, y - y1});
            if (dy > radius || dx > circle[static_cast<std::size_t>(dy)]) {
                continue;
            }
            if (!((tl.ware == 0 && tl.kind == kind::kRuin) || tl.ware == ware::kGrass || tl.ware == ware::kTree)) {
                continue;
            }
            if (tl.ware == ware::kTree && in_wood_reach(woods, x, y)) {
                continue;
            }
            const std::uint32_t variants = std::max<std::uint32_t>(base->rand_count, 1);
            const std::uint32_t pick = rng().below(variants);
            const assets::BuildingType* v = m_buildings.by_record(base->record + pick * base->rand_add);
            items.push_back(BuildItem{(v != nullptr ? v : base)->id, x, y, 0, 0});
            if (items.size() >= 64) {
                break;
            }
        }
    }
    build(p, std::move(items), false);
}

// ---------------------------------------------------------------------------------------------
// The building tasks

void Engine::task_building(AiPlayer& p, std::uint8_t task) {
    if (task == kWoodcutter || task == kGrainFarm || is_farm_task(task)) {
        task_farm(p, task);
        return;
    }
    if (is_workshop_task(task) || task == kStonemason || task == kCannonFoundry || task == kHunter) {
        task_workshop(p);
        return;
    }
    if (public_task(task) != nullptr) {
        task_public(p, task);
        return;
    }
    switch (task) {
        case kHouses:
            task_houses(p);
            return;
        case kMarket:
            task_market(p);
            return;
        case kQuarry:
        case kIronMine:
        case kGoldMine:
            task_mine(p);
            return;
        case kFisher:
            task_fisher(p);
            return;
        case kShipyard:
            task_shipyard(p);
            return;
        case kShipyardUpgrade:
            task_shipyard_upgrade(p);
            return;
        case kCastle:
            task_military(p);
            return;
        case kDenkmal:
            task_denkmal(p);
            return;
        default:
            pop(p);
            return;
    }
}

void Engine::task_farm(AiPlayer& p, std::uint8_t task) {
    const bool done = task == kGrainFarm ? search_grain(p) : search_farm(p, task == kWoodcutter);
    if (!done) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    const assets::BuildingType* b = by_id(s.building);
    if (b == nullptr) {
        return;
    }
    if (s.best > 0) {
        std::vector<BuildItem> items;
        if (s.stub_x >= 0) {
            items.push_back(BuildItem{id::kRoadDirt, s.stub_x, s.stub_y, 0, 0});
        }
        items.push_back(BuildItem{b->id, s.best_x0, s.best_y0, 0, 0});
        build(p, std::move(items), true);
        const Tile t = tile(p.island, s.best_x0, s.best_y0);
        if (t.valid() && t.type != nullptr && t.type->id == b->id && t.origin) {
            std::int32_t rings_n = 2;
            if (task == kGrainFarm) {
                rings_n = 1;
            } else if (task == kSheep || task == kWoodcutter) {
                rings_n = 3;
            }
            const std::uint8_t field = task == kWoodcutter ? ware::kTree : b->raw_ware;
            fill_fields(p, s.best_x0, s.best_y0, s.best_x1, s.best_y1, rings_n, field, b->radius);
            slot_built(p, s.slot);
            if (task == kWine || task == kSugar) {
                p.present[kSlotAlcohol] = true;
            }
            slot_ok(p, s.slot);
            slot_ok(p, kSlotMarket);
            return;
        }
        return;  // the build failed: no failure counted (as the original)
    }
    if (slot_fail(p, s.slot) == 0) {
        push(p, kRoads, true);
    }
}

void Engine::task_workshop(AiPlayer& p) {
    const assets::BuildingType* b = by_id(p.search.building);
    if (b == nullptr) {
        pop(p);
        refund(p);
        return;
    }
    const std::uint8_t task = top(p);
    bool done = false;
    if (task == kHunter) {
        done = search_hunter(p);
    } else if (task == kStonemason) {
        done = search_stonemason(p);
    } else {
        done = search_workshop(p, task == kCannonFoundry);
    }
    if (!done) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    if (s.best > 0) {
        const std::uint8_t rotation = (s.best_x1 - s.best_x0 + 1) != static_cast<std::int32_t>(b->size_x) ? 1 : 0;
        if (build(p, {BuildItem{b->id, s.best_x0, s.best_y0, rotation, 0}}, true) != 0) {
            slot_built(p, s.slot);
            slot_ok(p, s.slot);
            slot_ok(p, kSlotMarket);
        }
        return;
    }
    if (slot_fail(p, s.slot) == 0 && task != kStonemason) {
        push(p, kRoads, true);
    }
}

void Engine::task_houses(AiPlayer& p) {
    if (!search_houses(p)) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    const assets::BuildingType* b = by_id(s.building);
    if (b == nullptr) {
        return;
    }
    if (s.best > 0) {
        const std::uint32_t variants = std::max<std::uint32_t>(b->rand_count, 1);
        const std::uint32_t pick = rng().below(variants);
        const assets::BuildingType* v = m_buildings.by_record(b->record + pick * b->rand_add);
        if (build(p, {BuildItem{(v != nullptr ? v : b)->id, s.best_x0, s.best_y0, 0, 0}}, true) == 0) {
            return;
        }
        slot_built(p, kSlotHouses);
        slot_ok(p, kSlotHouses);
        slot_ok(p, kSlotMarket);
        // 0x0047B200: what the new house lacks opens the public building again.
        if (CityPlan* r = current_plan(p)) {
            const std::uint16_t sv = house_services(p.island, s.best_x0, s.best_y0);
            if ((sv & (hs::kKapelle | hs::kKirche)) == 0) {
                r->flags &= ~city_flag::kKapelle;
            }
            if ((sv & (hs::kSchule | hs::kHochschule)) == 0) {
                r->flags &= ~city_flag::kSchule;
            }
            if ((sv & hs::kWirtshaus) == 0) {
                r->flags &= ~city_flag::kWirt;
            }
            if ((sv & hs::kKlinik) == 0) {
                r->flags &= ~city_flag::kKlinik;
            }
        }
        return;
    }
    if (slot_fail(p, kSlotHouses) == 0) {
        push(p, kRoads, true);
    }
}

void Engine::task_public(AiPlayer& p, std::uint8_t task) {
    const PublicTask* def = public_task(task);
    CityPlan* r = current_plan(p);
    if (def == nullptr || r == nullptr) {
        pop(p);
        refund(p);
        return;
    }
    p.search.service = def->service;
    if (!search_public(p)) {
        return;
    }
    pop(p);
    refund(p);
    r->flags |= def->flag;
    const Search s = p.search;
    if (s.best < 1 || s.extra < def->min_houses) {
        if (s.best == 0 && slot_fail(p, def->slot) == 0 && def->roads_on_fail) {
            push(p, kRoads, true);
        }
        return;
    }
    if (build(p, {BuildItem{def->id, s.best_x0, s.best_y0, static_cast<std::uint8_t>(s.rotation), 0}}, false) != 0) {
        slot_built(p, def->slot);
        slot_ok(p, def->slot);
        slot_ok(p, kSlotMarket);
    }
}

void Engine::task_market(AiPlayer& p) {
    if (!search_market(p)) {
        return;
    }
    pop(p);
    refund(p);
    CityPlan* r = current_plan(p);
    if (r == nullptr) {
        return;
    }
    r->flags |= city_flag::kMarket;
    const Search s = p.search;
    if (s.best > 0) {
        if (build(p, {BuildItem{id::kMarket, s.best_x0, s.best_y0, static_cast<std::uint8_t>(s.rotation), 0}}, false) !=
            0) {
            slot_built(p, kSlotMarket);
            slot_ok(p, kSlotMarket);
        }
        return;
    }
    const bool first = r->slots[kSlotMarket].count == 0;
    if (slot_fail(p, kSlotMarket) == 0 && first) {
        push(p, kRoads, true);
    }
}

void Engine::task_claim_land(AiPlayer& p) {
    if (!search_claim(p)) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    if (s.best > 0 && build(p, {BuildItem{id::kMarket, s.best_x0, s.best_y0, static_cast<std::uint8_t>(s.rotation), 0}},
                            false) != 0) {
        slot_built(p, kSlotMarket);
        log(p, "claim market at %d,%d towards %d,%d", s.best_x0, s.best_y0, s.target_x, s.target_y);
        return;
    }
    // No site on the way: the deposit is given up until land changes (land_freed) or the hourly
    // new start, rather than searched again at once.
    log(p, "no market site towards %d,%d", s.target_x, s.target_y);
    if (CityPlan* r = current_plan(p)) {
        r->slots[s.slot].attempts = 2;
        r->slots[s.slot].fail_level = 2;
    }
}

void Engine::task_mine(AiPlayer& p) {
    const assets::BuildingType* b = by_id(p.search.building);
    if (b == nullptr) {
        pop(p);
        refund(p);
        return;
    }
    if (!search_mine(p, b->ware)) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    log(p, "mine search ware %u: best %d at %d,%d segments %zu", b->ware, s.best, s.best_x1, s.best_y1,
        p.segments.size());
    std::vector<BuildItem> path;
    if (s.best < 1 && (classic() || !mine_path(p, b->ware, path))) {
        slot_fail(p, s.slot);
        return;
    }
    if (!path.empty()) {
        log(p, "mine road of %zu tiles to %d,%d", path.size(), p.search.best_x1, p.search.best_y1);
        build(p, std::move(path), false);
    } else if (s.extra > 0) {
        // The road from the road to the slope first.
        const auto& d = kStep[static_cast<std::uint32_t>(s.rotation) & 3U];
        road_line(p, s.best_x0 + d[0], s.best_y0 + d[1], s.extra, static_cast<std::uint8_t>(s.rotation), true);
    }
    // The mine on the slope (1 x 2, facing the rock).
    const assets::BuildingType* mine = by_id(p.search.building);
    if (mine == nullptr) {
        return;
    }
    const std::int32_t hx = p.search.best_x1;
    const std::int32_t hy = p.search.best_y1;
    std::size_t placed = 0;
    for (const auto& o : std::array<std::array<std::int32_t, 3>, 6>{
             {{0, 0, 0}, {-1, 0, 1}, {0, -1, 0}, {0, 0, 1}, {0, -1, 1}, {-1, 0, 0}}}) {
        BuildOrder order;
        order.player = p.player;
        order.island = p.island;
        order.orient_to_road = false;
        order.items.push_back(BuildItem{mine->id, hx + o[0], hy + o[1], static_cast<std::uint8_t>(o[2]), 0});
        const BuildPlan plan = plan_build(m_world, m_buildings, order);
        if (plan.items.front().ok()) {
            placed = build(p, order.items, false);
            break;
        }
    }
    log(p, "mine at %d,%d: %s", hx, hy, placed != 0 ? "built" : "failed");
    if (placed != 0) {
        slot_built(p, s.slot);
        slot_ok(p, s.slot);
        slot_ok(p, kSlotMarket);
        p.skip_layout = false;
    } else {
        slot_fail(p, s.slot);
    }
}

void Engine::task_fisher(AiPlayer& p) {
    if (!search_fisher(p)) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    const assets::BuildingType* b = by_id(s.building);
    if (b == nullptr) {
        return;
    }
    if (s.best <= 0) {
        slot_fail(p, kSlotFisher);
        return;
    }
    if (build(p, {BuildItem{b->id, s.best_x0, s.best_y0, 0, 0}}, true) == 0) {
        slot_fail(p, kSlotFisher);
        return;
    }
    if (s.extra >= 0 && static_cast<std::size_t>(s.extra) < p.segments.size()) {
        const Segment g = p.segments[static_cast<std::size_t>(s.extra)];
        const std::uint8_t out = static_cast<std::uint8_t>((g.dir + 2U) & 3U);
        std::int32_t x = g.x0;
        std::int32_t y = g.y0;
        if (s.phase != 0) {
            x += kStep[out][0];
            y += kStep[out][1];
        }
        road_three(p, x, y, g.dir, 7, 7, 7, g.length > 4);
    }
    slot_built(p, kSlotFisher);
    slot_ok(p, kSlotFisher);
    slot_ok(p, kSlotMarket);
}

void Engine::task_shipyard(AiPlayer& p) {
    if (!search_shipyard(p)) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    const assets::BuildingType* b = by_id(s.building);
    CityPlan* r = current_plan(p);
    if (b == nullptr || r == nullptr) {
        return;
    }
    if (s.best <= 0 || build(p, {BuildItem{b->id, s.best_x0, s.best_y0, 0, 0}}, false) == 0) {
        slot_fail(p, kSlotShipyard);
        return;
    }
    if (s.stub_x >= 0 && s.extra > 0) {
        road_line(p, s.stub_x, s.stub_y, s.extra, static_cast<std::uint8_t>(s.rotation), true);
    }
    r->slots[kSlotShipyard].count = b->id == id::kShipyardBig ? 2 : 1;
    p.present[kSlotShipyard] = true;
    p.yard_valid = true;
    p.yard_island = p.island;
    p.yard_x = static_cast<std::uint16_t>(s.best_x0);
    p.yard_y = static_cast<std::uint16_t>(s.best_y0);
    slot_ok(p, kSlotShipyard);
    slot_ok(p, kSlotMarket);
}

void Engine::task_shipyard_upgrade(AiPlayer& p) {
    // 0x00419C00: the big yard where the small one stands (it is demolished first).
    pop(p);
    refund(p);
    CityPlan* r = current_plan(p);
    const assets::BuildingType* big = by_id(id::kShipyardBig);
    if (r == nullptr || big == nullptr || !p.yard_valid || p.yard_island != p.island) {
        return;
    }
    const std::int32_t x = p.yard_x;
    const std::int32_t y = p.yard_y;
    demolish_tile(p, x, y);
    p.yard_valid = false;
    for (const auto& o : std::array<std::array<std::int32_t, 2>, 4>{{{0, 0}, {-1, 0}, {0, -1}, {-1, -1}}}) {
        if (build(p, {BuildItem{big->id, x + o[0], y + o[1], 0, 0}}, false) != 0) {
            r->slots[kSlotShipyard].count = 2;
            p.yard_valid = true;
            p.yard_island = p.island;
            p.yard_x = static_cast<std::uint16_t>(x + o[0]);
            p.yard_y = static_cast<std::uint16_t>(y + o[1]);
            slot_ok(p, kSlotShipyard);
            return;
        }
    }
    r->slots[kSlotShipyard].count = 0;
    slot_fail(p, kSlotShipyard);
}

void Engine::task_military(AiPlayer& p) {
    if (!search_military(p)) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    const assets::BuildingType* b = by_id(s.building);
    if (b == nullptr) {
        return;
    }
    if (s.best > 0 &&
        build(p, {BuildItem{b->id, s.best_x0, s.best_y0, static_cast<std::uint8_t>(s.rotation), 0}}, true) != 0) {
        // A square around it, then the survey registers the castle.
        const assets::BuildingType* square = by_id(id::kSquare);
        if (square != nullptr) {
            std::vector<BuildItem> items;
            for (std::int32_t y = s.best_y0 - 1; y <= s.best_y1 + 1; ++y) {
                for (std::int32_t x = s.best_x0 - 1; x <= s.best_x1 + 1; ++x) {
                    if (x >= s.best_x0 && x <= s.best_x1 && y >= s.best_y0 && y <= s.best_y1) {
                        continue;
                    }
                    const Tile t = tile(p.island, x, y);
                    if (t.valid() && t.city == p.city &&
                        (t.kind == kind::kRuin || t.ware == ware::kGrass || t.ware == ware::kTree)) {
                        items.push_back(BuildItem{square->id, x, y, 0, 0});
                    }
                }
            }
            build(p, std::move(items), false);
        }
        if (!classic()) {
            slot_built(p, kSlotCastle);  // the original never counts it (S-098)
        }
        slot_ok(p, kSlotCastle);
        push(p, kSurvey, true);
        return;
    }
    if (s.best == 0 && slot_fail(p, kSlotCastle) == 0) {
        push(p, kRoads, true);
    }
}

void Engine::task_denkmal(AiPlayer& p) {
    if (!search_denkmal(p)) {
        return;
    }
    pop(p);
    refund(p);  // the original keeps the reservation (S-096)
    const Search s = p.search;
    if (s.best > 0 && build(p, {BuildItem{id::kDenkmal, s.best_x0, s.best_y0, 0, 0}}, true) != 0) {
        slot_built(p, kSlotDenkmal);
    }
}

void Engine::task_pirate_houses(AiPlayer& p) {
    if (!search_pirate_houses(p)) {
        return;
    }
    pop(p);
    refund(p);
    const Search s = p.search;
    if (s.best > 0 && build(p, {BuildItem{s.building, s.best_x0, s.best_y0, 0, 0}}, true) != 0) {
        slot_built(p, kSlotPirateHouses);
        slot_ok(p, kSlotPirateHouses);
    }
}

void Engine::task_kontor_upgrade(AiPlayer& p) {
    // 0x0041CDD0: the next Kontor in place of the old one.
    pop(p);
    refund(p);
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || !c->warehouse_x || !c->warehouse_y) {
        return;
    }
    const std::uint32_t level = kontor_level(*c);
    if (level == 0 || level >= 4) {
        return;
    }
    if (build(p, {BuildItem{id::kontor(level + 1), *c->warehouse_x, *c->warehouse_y, 0, 0}}, true) != 0) {
        r->slots[kSlotKontor].count = static_cast<std::uint16_t>(level + 1);
        slot_ok(p, kSlotKontor);
    } else {
        slot_fail(p, kSlotKontor);
    }
}

void Engine::task_paving(AiPlayer& p) {
    // 0x00418D80: dirt roads and wooden bridges around the Kontor become stone, ring by ring.
    pop(p);
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || !c->warehouse_x || !c->warehouse_y) {
        return;
    }
    const IslandMap& map = island_of(p.island).map;
    const std::int32_t ring = r->paving_ring;
    const std::int32_t size = kontor_size(*c);
    const std::int32_t x0 = *c->warehouse_x - 1 - ring;
    const std::int32_t y0 = *c->warehouse_y - 1 - ring;
    const std::int32_t x1 = *c->warehouse_x + size + ring;
    const std::int32_t y1 = *c->warehouse_y + size + ring;
    std::vector<BuildItem> items;
    std::array<std::int32_t, 4> per_side{};
    for (std::int32_t y = y0; y <= y1; ++y) {
        for (std::int32_t x = x0; x <= x1; ++x) {
            const std::size_t side = y == y0 ? 0 : y == y1 ? 1 : x == x0 ? 2 : x == x1 ? 3 : 4;
            if (side == 4 || per_side[side] >= 7) {
                continue;
            }
            const Tile t = tile(p.island, x, y);
            if (!t.valid() || t.city != p.city || t.type->build_cost.bricks != 0) {
                continue;
            }
            if (t.kind == kind::kRoad) {
                items.push_back(BuildItem{id::kRoadStone, x, y, 0, 0});
                ++per_side[side];
            } else if (t.kind == kind::kBridge) {
                items.push_back(
                    BuildItem{id::kBridgeStone, x, y,
                              map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).rotation, 0});
                ++per_side[side];
            }
        }
    }
    if (items.empty()) {
        r->paving_ring = static_cast<std::uint16_t>(r->paving_ring + 1);
        if (r->paving_ring > map.width() / 2) {
            r->paving_ring = 0;
        }
        return;
    }
    build(p, std::move(items), false);
}

void Engine::task_towers(AiPlayer& p) {
    // 0x00418300: towers on the coast around the Kontor, apart from each other, near fish.
    pop(p);
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    const assets::BuildingType* b = by_id(p.search.building);
    if (c == nullptr || r == nullptr || b == nullptr || !c->warehouse_x || !c->warehouse_y) {
        refund(p);
        return;
    }
    const IslandMap& map = island_of(p.island).map;
    const std::int32_t ring = r->tower_ring;
    const std::int32_t size = kontor_size(*c);
    const std::int32_t x0 = *c->warehouse_x - 1 - ring;
    const std::int32_t y0 = *c->warehouse_y - 1 - ring;
    const std::int32_t x1 = *c->warehouse_x + size + ring;
    const std::int32_t y1 = *c->warehouse_y + size + ring;
    const bool many = r->slots[kSlotWoodTower].count > 1 || r->slots[kSlotStoneTower].count > 1 ||
                      r->slots[kSlotPirateTower].count > 1;
    const std::int32_t start = many ? 8 : 3;
    for (std::int32_t y = y0; y <= y1; ++y) {
        for (std::int32_t x = x0; x <= x1; ++x) {
            if (x != x0 && x != x1 && y != y0 && y != y1) {
                continue;
            }
            const Tile t = tile(p.island, x, y);
            if (!t.valid() || t.city != p.city) {
                continue;
            }
            if (!(t.kind == kind::kBeach || t.kind == kind::kWall || t.kind == kind::kWallBeach ||
                  t.kind == kind::kBeachOuter || t.kind == kind::kBeachInner || t.kind == kind::kBeachVariant ||
                  t.kind == kind::kBeachRuin)) {
                continue;
            }
            std::int32_t fish = 0;
            std::int32_t nearest = start;
            bool access = false;
            rings(p, RingFilter::OwnOrSea, x, y, x, y, 12,
                  [&](const Tile& o, std::int32_t, std::int32_t, std::int32_t d) {
                      if (o.ware == ware::kFish && d < 8) {
                          ++fish;
                      }
                      if (o.kind == kind::kTower || o.kind == kind::kTowerBeach) {
                          nearest = std::min(nearest, d);
                      }
                      if (d == 0 && (o.kind == kind::kRoad || o.kind == kind::kWall || o.kind == kind::kForest ||
                                     o.kind == kind::kGround || o.kind == kind::kRuin || o.kind == kind::kBuilding ||
                                     o.kind == kind::kRiver || o.kind == kind::kBridge)) {
                          access = true;
                      }
                  });
            if (nearest < start || fish <= 8 || !access) {
                continue;
            }
            if (p.pirates) {
                // The pirates get the cannons for their towers from nowhere.
                const std::uint32_t towers = r->slots[kSlotPirateTower].count + 1U;
                if (c->stock(m_buildings.materials().cannons) < towers * kTon) {
                    add_goods(*c, m_buildings.materials().cannons, kTon * towers);
                    return;
                }
            }
            refund(p);  // the original never gives the reservation back (S-096)
            if (build(p, {BuildItem{b->id, x, y, 0, 0}}, false) != 0) {
                ++r->slots[p.search.slot].count;
                p.present[p.search.slot] = true;
            }
            return;
        }
    }
    refund(p);
    r->tower_ring = static_cast<std::uint16_t>(r->tower_ring + 1);
    if (r->tower_ring > map.width()) {
        r->tower_ring = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Roads (task 6) and the layout of the runs (task 7)

void Engine::task_roads(AiPlayer& p) {
    // 0x00419210
    pop(p);
    City* c = current_city(p);
    if (c == nullptr || !c->warehouse_x || !c->warehouse_y) {
        return;
    }
    if (p.segments.empty()) {
        const std::int32_t kx = *c->warehouse_x;
        const std::int32_t ky = *c->warehouse_y;
        const std::uint8_t rot =
            island_of(p.island).map.top(static_cast<std::uint32_t>(kx), static_cast<std::uint32_t>(ky)).rotation;
        // In front of the Kontor, on its land side.
        static constexpr std::array<std::array<std::int32_t, 2>, 4> kFront{{{-1, 1}, {1, -1}, {2, 1}, {1, 2}}};
        const std::int32_t sx = kx + kFront[rot & 3U][0];
        const std::int32_t sy = ky + kFront[rot & 3U][1];
        if (p.pirates) {
            road_three(p, sx, sy, rot, 5, 5, 6, true);
        } else {
            road_three(p, sx, sy, rot, 7, 7, 7, true);
            plaza_ring(p);
        }
        return;
    }
    std::int32_t best = -1;
    std::size_t best_index = 0;
    std::int32_t bx = 0;
    std::int32_t by = 0;
    std::int32_t b_back = 0;
    std::int32_t b_left = 0;
    std::int32_t b_right = 0;
    for (std::size_t i = 0; i < p.segments.size(); ++i) {
        const Segment& g = p.segments[i];
        if (!g.open) {
            continue;
        }
        const std::uint8_t out = static_cast<std::uint8_t>((g.dir + 2U) & 3U);
        std::int32_t back = free_run(p.island, p.city, g.x0 + kStep[out][0], g.y0 + kStep[out][1], out);
        std::int32_t sx = g.x0;
        std::int32_t sy = g.y0;
        if (back != 0) {
            const Tile behind = tile(p.island, g.x0 + kStep[out][0], g.y0 + kStep[out][1]);
            if (behind.kind != kind::kRiver) {
                sx += kStep[out][0];
                sy += kStep[out][1];
            }
        }
        const std::int32_t right = free_run(p.island, p.city, sx, sy, static_cast<std::uint8_t>((g.dir + 1U) & 3U));
        const std::int32_t left = free_run(p.island, p.city, sx, sy, static_cast<std::uint8_t>((g.dir + 3U) & 3U));
        const std::int32_t score = (left + right) / 2 + 2 * back;
        if (score > best) {
            best = score;
            best_index = i;
            bx = sx;
            by = sy;
            b_back = back;
            b_left = left;
            b_right = right;
        }
    }
    if (best <= 0) {
        for (Segment& g : p.segments) {
            g.open = false;
        }
        return;
    }
    const Segment g = p.segments[best_index];
    const std::int32_t placed =
        road_three(p, bx, by, g.dir, std::min(b_right, 7), std::min(b_left, 7), std::min(b_back, 7), g.length > 3);
    if (placed == 0 && best_index < p.segments.size()) {
        p.segments[best_index].open = false;
    }
}

void Engine::task_layout(AiPlayer& p) {
    pop(p);
    build_segments(p);
    if (p.segments.empty()) {
        push(p, kRoads, false);
    }
}

// ---------------------------------------------------------------------------------------------
// Survey (task 8, 0x0041E100)

void Engine::task_survey(AiPlayer& p) {
    pop(p);
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr) {
        return;
    }
    for (SlotState& s : r->slots) {
        s.count = 0;
    }
    r->doctor.reset();
    if (p.castle_island && *p.castle_island == p.island) {
        p.castles.clear();
        p.castle_island.reset();
    }
    const IslandMap& map = island_of(p.island).map;
    const bool cotton_island = fertility(p.island, ware::kCottonField) == 128;
    const assets::BuildingType* weberei = by_id(id::kWeberei);
    const bool weberei_ok = weberei != nullptr && unlocked(p, *weberei);
    SurveyFinds finds;
    // Only the tiles of the city's buildings count. While no tile of the island changed, they are
    // where the last survey found them; the rest of the island is skipped (TB-11).
    const std::uint64_t revision = island_revision(p.island);
    CityScans& known = city_scans(p.island, p.city);
    if (known.survey_revision == revision) {
        for (const auto& [x, y] : known.survey_tiles) {
            survey_tile(p, *c, *r, x, y, finds);
        }
    } else {
        known.survey_revision = revision;
        known.survey_tiles.clear();
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                if (survey_tile(p, *c, *r, x, y, finds)) {
                    known.survey_tiles.emplace_back(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
                }
            }
        }
    }
    r->slots[kSlotKontor].count = static_cast<std::uint16_t>(kontor_level(*c));
    // Cleanup (docs/re/demolish.md): exhausted mines; sheep and small weavers where cotton grows.
    for (const auto& [x, y] : finds.starved_mines) {
        demolish_tile(p, x, y);
    }
    if (cotton_island && weberei_ok) {
        std::uint32_t n = 0;
        if (r->slots[kSlotCotton].count >= 2) {
            for (const auto& [x, y] : finds.sheep) {
                if (n++ < 20) {
                    demolish_tile(p, x, y);
                }
            }
        }
        if (r->slots[kSlotWeberei].count >= 1) {
            for (const auto& [x, y] : finds.webstuben) {
                if (n++ < 20) {
                    demolish_tile(p, x, y);
                }
            }
        }
    }
    if (p.surveyed < p.cities) {
        ++p.surveyed;
    }
    r->flags |= city_flag::kSurveyed;
}

bool Engine::survey_tile(AiPlayer& p, const City& c, CityPlan& r, std::uint32_t x, std::uint32_t y,
                         SurveyFinds& finds) {
    const auto count = [&](std::uint8_t k) {
        ++r.slots[k].count;
        p.present[k] = true;
    };
    const auto ix = static_cast<std::int32_t>(x);
    const auto iy = static_cast<std::int32_t>(y);
    const Tile t = tile(p.island, ix, iy);
    if (!t.valid() || !t.origin || t.city != p.city || t.prod == prod::kRipe || t.prod == prod::kGrowing) {
        return false;
    }
    const std::uint8_t w = t.ware;
    const std::uint8_t raw = t.raw;
    switch (t.prod) {
        case prod::kHouse:
            count(kSlotHouses);
            return true;
        case prod::kKapelle:
            count(kSlotKapelle);
            return true;
        case prod::kWirt:
            count(kSlotWirt);
            return true;
        case prod::kSchule:
            count(kSlotSchule);
            return true;
        case prod::kKlinik:
            count(kSlotKlinik);
            r.doctor = std::pair{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            return true;
        case prod::kKirche:
            count(kSlotKirche);
            return true;
        case prod::kBadehaus:
            count(kSlotBadehaus);
            return true;
        case prod::kHochschule:
            count(kSlotHochschule);
            return true;
        case prod::kTheater:
            count(kSlotTheater);
            return true;
        case prod::kGalgen:
            count(kSlotGalgen);
            return true;
        case prod::kBrunnen:
            count(kSlotBrunnen);
            return true;
        case prod::kPirateHouse:
            count(kSlotPirateHouses);
            return true;
        case prod::kShipyard:
            r.slots[kSlotShipyard].count = t.type->id == id::kShipyardBig ? 2 : 1;
            p.present[kSlotShipyard] = true;
            p.yard_valid = true;
            p.yard_island = p.island;
            p.yard_x = static_cast<std::uint16_t>(x);
            p.yard_y = static_cast<std::uint16_t>(y);
            return true;
        case prod::kMilitary:
            if (p.castles.size() < 3) {
                CastleEntry e;
                e.x = static_cast<std::uint16_t>(x);
                e.y = static_cast<std::uint16_t>(y);
                e.rally_x = c.warehouse_x.value_or(e.x);
                e.rally_y = c.warehouse_y.value_or(e.y);
                p.castles.push_back(e);
                p.castle_island = p.island;
            }
            count(kSlotCastle);
            return true;
        case prod::kMarket:
            count(kSlotMarket);
            return true;
        default:
            break;
    }
    if (t.kind == kind::kTower || t.kind == kind::kTowerBeach || t.prod == prod::kTower) {
        const std::uint32_t idv = t.type->id;
        if (idv == id::kWoodTower || idv == id::kWoodTowerAlt) {
            count(kSlotWoodTower);
        } else if (idv == id::kStoneTower || idv == id::kStoneTowerAlt) {
            count(kSlotStoneTower);
        } else if (idv == id::kPirateTower || idv == id::kPirateTowerAlt) {
            count(kSlotPirateTower);
        }
        return true;
    }
    if (w <= 1 || ((t.prod < prod::kWorkshop || t.prod > prod::kFisher) && t.prod != prod::kQuarry)) {
        return true;
    }
    // Producers by ware and raw material.
    if (w == m_buildings.food_ware()) {
        count(kSlotFood);
    }
    if (w == ware::kAlcohol) {
        count(kSlotAlcohol);
    }
    const std::optional<std::uint8_t> slot = producer_slot(w, raw, t.type->id);
    if (!slot) {
        return true;
    }
    count(*slot);
    if (*slot == kSlotSheep) {
        finds.sheep.emplace_back(ix, iy);
    } else if (*slot == kSlotWebstube) {
        finds.webstuben.emplace_back(ix, iy);
    } else if (*slot == kSlotIronMine || *slot == kSlotGoldMine) {
        const Production* pr =
            m_world.production(p.island, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
        if (pr != nullptr && t.type->max_idle_cycles != 0 && pr->idle_cycles >= t.type->max_idle_cycles) {
            finds.starved_mines.emplace_back(ix, iy);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Maintenance sweep (0x0041F550) and the Kontor after a fire (0x0041D1F0)

void Engine::sweep(AiPlayer& p) {
    CityPlan* r = current_plan(p);
    City* c = current_city(p);
    if (r == nullptr || c == nullptr || (r->flags & city_flag::kSurveyed) == 0) {
        return;
    }
    const IslandMap& map = island_of(p.island).map;
    if (r->sweep_y >= map.height()) {
        r->sweep_y = 0;
    }
    std::int32_t seen = 0;
    std::int32_t cleared = 0;
    std::vector<BuildItem> rebuild;
    const auto y = static_cast<std::int32_t>(r->sweep_y);
    while (r->sweep_x < map.width() && seen < 16 && cleared < 20) {
        const auto x = static_cast<std::int32_t>(r->sweep_x);
        ++r->sweep_x;
        const Tile t = tile(p.island, x, y);
        if (!t.valid() || t.city != p.city || !t.origin) {
            continue;
        }
        ++seen;
        if (t.kind == kind::kRuin) {
            if (t.owner == p.player) {
                demolish_tile(p, x, y);
                ++cleared;
            }
            continue;
        }
        if (t.kind == kind::kRoad) {
            bool joined = false;
            for (const auto& n : kStep) {
                const Tile o = tile(p.island, x + n[0], y + n[1]);
                joined = joined || (o.valid() && (o.kind == kind::kRoad || o.kind == kind::kBridge ||
                                                  o.kind == kind::kGate || o.prod == prod::kKontor));
            }
            if (!joined) {
                demolish_tile(p, x, y);  // an isolated piece of road
                ++cleared;
            }
            continue;
        }
        if (t.ware == ware::kAll && t.type->destroyed && rebuild.size() < 7) {
            // A burnt-out market or Kontor is built again.
            const std::uint32_t level = r->slots[kSlotKontor].count;
            const std::uint32_t target = t.kind == kind::kBuilding ? id::kMarket : id::kontor(level);
            if (const assets::BuildingType* b = by_id(target); b != nullptr && affordable(p, *c, *b)) {
                rebuild.push_back(BuildItem{
                    target, x, y, map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).rotation, 0});
            }
            continue;
        }
        // Farms need their fields and a road. The stonemason takes its stone like a farm (from the
        // quarry), but it has no fields.
        const bool farm = (t.prod == prod::kFarm || t.prod == prod::kPasture ||
                           (t.prod == prod::kWorkshop && t.ware == ware::kWood)) &&
                          t.raw >= ware::kCornField && t.raw <= ware::kTree;
        if (farm) {
            std::uint32_t w = 0;
            std::uint32_t h = 0;
            rotated_size(*t.type, map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).rotation, w, h);
            std::int32_t fields = 0;
            bool road = false;
            rings(p, RingFilter::Own, x, y, x + static_cast<std::int32_t>(w) - 1, y + static_cast<std::int32_t>(h) - 1,
                  t.type->radius, [&](const Tile& o, std::int32_t, std::int32_t, std::int32_t d) {
                      if (o.ware == t.raw) {
                          ++fields;
                      }
                      if (d == 0 && (o.kind == kind::kRoad || o.kind == kind::kBridge)) {
                          road = true;
                      }
                  });
            const std::int32_t need = t.ware == ware::kGrain ? 7 : 14;
            const std::int32_t x1 = x + static_cast<std::int32_t>(w) - 1;
            const std::int32_t y1 = y + static_cast<std::int32_t>(h) - 1;
            if (road && fields < need && farm_room(p, *t.type, x, y, x1, y1)) {
                // Fix S-100: plant the missing fields where there is room instead of tearing the
                // farm down; the original demolishes it and its farm task builds the same farm
                // there again (a woodcutter whose trees were felled went round every 11 seconds).
                fill_fields(p, x, y, x1, y1, t.type->radius, t.raw, t.type->radius);
            } else if (fields < need || !road) {
                demolish_tile(p, x, y);
                ++cleared;
            }
            continue;
        }
        // Fields of a crop without a farm of that crop within three rings.
        if (t.prod == prod::kRipe && t.ware >= ware::kCornField && t.ware <= ware::kCocoaField) {
            bool farm_near = false;
            rings(p, RingFilter::Own, x, y, x, y, 3, [&](const Tile& o, std::int32_t, std::int32_t, std::int32_t) {
                farm_near = farm_near || (o.raw == t.ware && o.prod != prod::kRipe);
            });
            if (!farm_near) {
                demolish_tile(p, x, y);
                ++cleared;
            }
        }
    }
    if (r->sweep_x >= map.width()) {
        r->sweep_x = 0;
        r->sweep_y = static_cast<std::uint16_t>((r->sweep_y + 1U) % std::max<std::uint32_t>(map.height(), 1));
    }
    if (!rebuild.empty()) {
        build(p, std::move(rebuild), false);
    }
}

void Engine::kontor_restore(AiPlayer& p) {
    // 0x0041D1F0: a destroyed Kontor is built again at its level (the original always builds
    // Kontor I, because it compares the intact Ids).
    City* c = current_city(p);
    CityPlan* r = current_plan(p);
    if (c == nullptr || r == nullptr || !c->warehouse_x || !c->warehouse_y || p.score <= 1) {
        return;
    }
    const Tile t = tile(p.island, *c->warehouse_x, *c->warehouse_y);
    if (!t.valid() || !t.type->destroyed || t.prod != prod::kKontor) {
        return;
    }
    const std::uint32_t level = std::clamp<std::uint32_t>(r->slots[kSlotKontor].count, 1, 4);
    const assets::BuildingType* b = by_id(id::kontor(level));
    if (b == nullptr || !affordable(p, *c, *b)) {
        return;
    }
    if (build(p, {BuildItem{b->id, *c->warehouse_x, *c->warehouse_y, 0, 0}}, false) != 0) {
        r->slots[kSlotKontor].count = static_cast<std::uint16_t>(level);
        slot_ok(p, kSlotKontor);
    }
}

}  // namespace o1602::sim::ai
