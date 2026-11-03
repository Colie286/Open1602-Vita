// SPDX-License-Identifier: GPL-3.0-or-later
// The bytes of Open1602's own savegame sections (docs/formats/o1602-save.md, o1602-chunk.md) stay
// the same: a world with every saved field set to a value of its own is written as the own
// savegame and as the chunk in a .GAM, and every section must keep the size and CRC-32 recorded
// here. Reading those bytes and writing them again gives the same bytes, so each reader takes
// every field from where its writer put it.
//
// A change of the format on purpose gives the section a new version and updates the tables; the
// test prints the new ones.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/core/compress.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kHouse = 20501;
constexpr std::size_t kFileHeader = 32;   ///< own savegame, in front of the sections
constexpr std::size_t kChunkHeader = 20;  ///< chunk in a .GAM: magic, version, flags, fingerprint
constexpr std::size_t kPlayers = 4;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20501\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// Values that differ from field to field and fill the whole width of each, so that two fields
/// that swap places or a field that changes its width change the bytes.
class Values {
public:
    template <class T>
    T next() {
        std::uint64_t z = (m_n += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        z ^= z >> 31U;
        if constexpr (std::is_same_v<T, bool>) {
            return (z & 1U) != 0;
        } else {
            return static_cast<T>(z);
        }
    }
    template <class T>
    void operator()(T& v) {
        v = next<T>();
    }
    /// A value below `n`.
    std::uint32_t below(std::uint32_t n) { return next<std::uint32_t>() % n; }
    sim::PlayerId player() { return static_cast<sim::PlayerId>(below(kPlayers)); }
    std::string text() { return "text " + std::to_string(next<std::uint16_t>()); }

private:
    std::uint64_t m_n = 0;
};

sim::Target target(Values& v) {
    sim::Target t;
    t.kind = static_cast<sim::Target::Kind>(v.below(7));
    v(t.island);
    v(t.city);
    v(t.x);
    v(t.y);
    v(t.ship);
    v(t.raw);
    return t;
}

/// Islands of 12 x 10 tiles with ground, a road and a house; `wide` adds one of 260 x 3 tiles with
/// a crater and a deposit beyond 255 (version 3 of ISLE, 2 of VOLC). The wide world's players also have a
/// scenario's switches (version 2 of PSTA).
void add_islands(sim::World& world, const assets::BuildingTable& buildings, bool wide) {
    struct Shape {
        std::uint32_t x, y, w, h;
    };
    std::vector<Shape> shapes = {{20, 40, 12, 10}, {100, 40, 12, 10}};
    if (wide) {
        shapes.push_back({20, 200, 260, 3});
    }
    Values v;
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const Shape& s = shapes[i];
        sim::IslandMap map(s.w, s.h);
        for (std::uint32_t y = 0; y < s.h; ++y) {
            for (std::uint32_t x = 0; x < s.w; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), static_cast<std::int32_t>(x), static_cast<std::int32_t>(y),
                                  0, 0)
                            .ok());
            }
        }
        map.capture_ground();
        REQUIRE(map.build(*buildings.by_id(kRoad), 3, 1, 0, 0, 1, 0).ok());
        REQUIRE(map.build(*buildings.by_id(kHouse), 5, 1, 1, 2, 1, 0).ok());
        for (std::uint32_t y = 0; y < s.h; ++y) {
            for (std::uint32_t x = 0; x < s.w; ++x) {
                sim::MapCell top = map.top(x, y);
                top.rotation = static_cast<std::uint8_t>(v.below(4));
                top.anim_phase = static_cast<std::uint8_t>(v.below(16));
                const std::uint32_t owner = v.below(kPlayers + 1);
                top.owner = owner == kPlayers ? sim::kNoPlayer : static_cast<sim::PlayerId>(owner);
                v(top.city);
                v(top.random);
                map.set_top_cell(x, y, top);
                sim::MapCell ground = map.ground(x, y);
                ground.random = static_cast<std::uint8_t>(v.below(32));
                map.set_ground_cell(x, y, ground);
            }
        }
        assets::IslandHeader header;
        header.chunk_name = "INSEL5";
        header.number = static_cast<std::uint8_t>(i == 1 ? 7 : i);  // numbers need not follow the order
        header.width = static_cast<std::uint16_t>(s.w);
        header.height = static_cast<std::uint16_t>(s.h);
        v(header.flags);
        v(header.pos_x);
        v(header.pos_y);
        v(header.template_number);
        v(header.template_size);
        v(header.climate);
        v(header.stored_complete);
        REQUIRE(world.add_island(sim::WorldIsland{header, std::move(map), s.x, s.y}).ok());
    }
}

/// A world with every field the sections save set to a value of its own (within what the readers
/// accept and validate() allows).
sim::World make_world(const assets::BuildingTable& buildings, bool wide) {
    sim::World world(400, 300);
    add_islands(world, buildings, wide);
    Values v;

    for (std::size_t i = 0; i < world.islands().size(); ++i) {
        sim::WorldIsland& island = world.island(i);
        const auto w = static_cast<std::uint16_t>(island.map.width());
        const auto h = static_cast<std::uint16_t>(island.map.height());
        for (sim::DeerSpot& s : island.deer_spots) {
            v(s.used);
            s.x = static_cast<std::uint16_t>(v.below(w));
            s.y = static_cast<std::uint16_t>(v.below(h));
            v(s.respawn_ms);
        }
        island.deer_phase = static_cast<std::uint8_t>(v.below(4));
        island.last_phase = static_cast<std::uint8_t>(v.below(8));
        v(island.drought.strength);
        v(island.drought.end_ms);
        v(island.drought.curse_pending);
        for (int k = 0; k < 2; ++k) {
            sim::Crater c;
            c.x = static_cast<std::uint16_t>(v.below(std::min<std::uint16_t>(w, 256)));
            c.y = static_cast<std::uint16_t>(v.below(h));
            v(c.steps_left);
            island.craters.push_back(c);
        }
        if (w > 256) {
            island.craters[1].x = 258;
        }
        v(island.volcano_erupted);
        for (std::size_t p = 0; p < kPlayers + 1; ++p) {
            island.discovered.push_back(v.next<bool>());
            island.explored.push_back(v.next<bool>());
        }
        for (int k = 0; k < 2; ++k) {
            assets::IslandHeader::Deposit d;
            v(d.ware);
            d.x = v.next<std::uint8_t>();
            d.y = v.next<std::uint8_t>();
            v(d.mask);
            v(d.kind);
            v(d.amount);
            island.header.deposits.push_back(d);
        }
        if (w > 256) {
            island.header.deposits[1].x = 300;
        }
        v(island.header.fertility);
    }

    std::vector<sim::Player> players(kPlayers);
    for (std::size_t p = 0; p < players.size(); ++p) {
        sim::Player& pl = players[p];
        // A computer, or the free trader, the pirates or the natives.
        const std::uint32_t kind = p == 0 ? 0 : 1 + v.below(4);
        pl.role = kind == 0 ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
        if (kind >= 2) {
            sim::make_neutral(pl, sim::base_faction(static_cast<sim::FactionBehaviour>(kind - 1)));
        }
        v(pl.banner);
        v(pl.money);
        pl.name = v.text();
        v(pl.infra_mask);
        v(pl.castles_built);
        v(pl.cathedrals_built);
        v(pl.arches_allowed);
        v(pl.arches_built);
        v(pl.monuments_allowed);
        v(pl.monuments_built);
        pl.mission = p == 3 ? sim::kNoMission : static_cast<sim::MissionId>(v.below(2));
        pl.status = static_cast<sim::PlayerStatus>(v.below(3));
        v(pl.won);
        v(pl.opponents_defeated);
        v(pl.soldiers_killed);
        v(pl.soldiers_lost);
        v(pl.ships_sunk);
        v(pl.ships_lost);
        v(pl.bankrupt_checks);
        v(pl.empty_checks);
        v(pl.purchases);
        v(pl.sales);
        if (wide) {
            // The scenario's switches (version 2 of PSTA), apart from the values above so that the
            // narrow world keeps the bytes of version 1.
            const auto n = static_cast<std::uint32_t>(p + 1);
            pl.scenario = {0x3FEU & (n * 0x95U), static_cast<std::uint16_t>(n * 311),
                           static_cast<std::uint16_t>(n * 977), static_cast<std::uint8_t>(n * 29),
                           static_cast<std::uint8_t>(n * 41)};
        }
    }
    world.set_players(players);

    std::vector<sim::Mission> missions(2);
    for (sim::Mission& m : missions) {
        m.goals = v.next<std::uint32_t>() & (sim::goal::kKnown | sim::goal::kComputers);
        m.losses = v.next<std::uint32_t>() & sim::loss::kKnown;
        m.target = v.player();
        v(m.min_defeated);
        m.defeat = {v.player(), v.player(), v.player()};
        v(m.min_cities);
        v(m.min_inhabitants);
        v(m.min_balance);
        v(m.min_trade_balance);
        v(m.protected_min_cities);
        v(m.success_message);
        m.text = v.text();
        for (sim::Mission::GoodsGoal& g : m.goods) {
            v(g.ware);
            v(g.amount);
        }
        for (sim::Mission::IslandGoal& g : m.islands) {
            v(g.resource);
            g.islands = {static_cast<std::uint16_t>(v.below(2)), static_cast<std::uint16_t>(v.below(2))};
        }
        for (sim::Mission::CityGoal* g :
             {&m.cities[0], &m.cities[1], &m.cities[2], &m.every_city, &m.target_city, &m.protected_city}) {
            v(g->inhabitants);
            v(g->resident_class);
            v(g->count);
        }
        v(m.unused_48);
        v(m.unused_58);
    }
    world.set_missions(missions);
    world.set_game_end(sim::World::GameEnd{v.next<std::uint64_t>(), v.player()});

    for (std::uint8_t c = 0; c < 2; ++c) {
        sim::City city;
        city.island = c;
        city.index = static_cast<std::uint8_t>(c + 1);
        city.owner = v.player();
        city.name = v.text();
        for (int k = 0; k < 5; ++k) {
            city.goods.push_back(v.next<std::uint32_t>());
            city.reserved.push_back(v.next<std::uint32_t>());
            city.incoming.push_back(v.next<std::uint32_t>());
        }
        if (c == 0) {
            city.warehouse_x = v.next<std::uint16_t>();
            city.warehouse_y = v.next<std::uint16_t>();
        }
        v(city.capacity);
        for (int k = 0; k < 3; ++k) {
            city.inhabitants.push_back(v.next<std::uint32_t>());
            city.satisfaction.push_back(v.next<std::uint8_t>());
            city.tax_rate.push_back(v.next<std::uint8_t>());
            sim::City::Supply s;
            v(s.demand);
            v(s.supplied);
            v(s.ratio);
            for (std::uint8_t& x : s.history) {
                v(x);
            }
            city.supply.push_back(s);
            sim::City::UpgradeSlot u;
            v(u.inhabitants);
            v(u.x);
            v(u.y);
            city.upgrade_queue.push_back(u);
            sim::City::Trade t;
            v(t.sell);
            v(t.buy);
            v(t.sell_price);
            v(t.buy_price);
            v(t.keep);
            v(t.limit);
            city.trade.push_back(t);
            city.goodwill.push_back(v.next<std::uint32_t>());
        }
        v(city.food_ratio);
        v(city.satisfaction_bonus);
        v(city.epoch);
        v(city.material_lock);
        v(city.worst_good);
        v(city.next_event_ms);
        v(city.house_growth);
        v(city.growing_since_ms);
        v(city.trader_inbound);
        v(city.last_attacked_ms);
        v(city.cursed);
        v(city.purchases);
        v(city.sales);
        world.add_city(city);
    }

    // A place on island i (12 x 10 tiles, or the first 12 x 3 of the wide one).
    const auto at = [&](std::uint16_t& island, std::uint16_t& x, std::uint16_t& y) {
        island = static_cast<std::uint16_t>(v.below(static_cast<std::uint32_t>(world.islands().size())));
        x = static_cast<std::uint16_t>(v.below(12));
        y = static_cast<std::uint16_t>(v.below(3));
    };

    std::vector<sim::Production> productions(4);
    for (std::size_t k = 0; k < productions.size(); ++k) {
        sim::Production& p = productions[k];
        at(p.island, p.x, p.y);
        p.used = k != 2;
        p.enabled = p.used && v.next<bool>();  // a free slot never runs
        v(p.rate);
        v(p.anim_phase);
        v(p.idle_cycles);
        v(p.flags);
        v(p.countdown);
        v(p.raw);
        v(p.raw2);
        v(p.stock);
        v(p.produced);
        v(p.elapsed);
        v(p.reserved);
    }
    world.set_productions(productions, {2});

    std::vector<sim::House> houses(3);
    for (sim::House& h : houses) {
        at(h.island, h.x, h.y);
        v(h.city);
        v(h.resident_class);
        v(h.inhabitants64);
        v(h.timer_group);
        v(h.epoch);
        v(h.queue_epoch);
        v(h.market_distance);
        v(h.market);
        v(h.services_complete);
        std::uint16_t services = 0;  // the original's (HOUS version 1); kWideServices has its own test
        v(services);
        h.services = services;
    }
    world.set_houses(houses);

    for (int k = 0; k < 3; ++k) {
        sim::GrowthEntry e;
        at(e.island, e.x, e.y);
        v(e.growth_class);
        v(e.seen);
        v(e.stage);
        world.growth().entries.push_back(e);
    }
    for (std::size_t c = 0; c < sim::GrowthState::kClasses; ++c) {
        v(world.growth().time_ms[c]);
        v(world.growth().counter[c]);
    }
    for (std::size_t g = 0; g < sim::HouseClocks::kGroups; ++g) {
        v(world.house_clocks().time_ms[g]);
        v(world.house_clocks().epoch[g]);
    }

    for (int k = 0; k < 2; ++k) {
        sim::CostBuilding b;
        b.kind = k == 0 ? sim::CostBuilding::Kind::Shipyard : sim::CostBuilding::Kind::Castle;
        at(b.island, b.x, b.y);
        v(b.enabled);
        v(b.order);
        v(b.progress);
        v(b.wood);
        v(b.cloth);
        v(b.rate);
        v(b.countdown);
        v(b.repair_ship);
        v(b.idle_cycles);
        v(b.fetching);
        for (std::uint32_t& weapon : b.weapons) {
            v(weapon);
        }
        for (int s = 0; s < 2; ++s) {
            sim::CastleSlot slot;
            v(slot.figure);
            v(slot.training);
            v(slot.armed);
            v(slot.health);
            v(slot.progress);
            b.slots.push_back(slot);
        }
        world.add_cost_building(b);
    }

    for (std::uint32_t k = 0; k < 3; ++k) {
        sim::Ship s;
        s.place(150 + 7 * k, 100 + k);
        s.pos_x += static_cast<std::int32_t>(v.below(sim::kShipUnit / 2));
        s.pos_y += static_cast<std::int32_t>(v.below(sim::kShipUnit / 2));
        s.name = v.text();
        v(s.figure);
        s.owner = v.player();
        s.direction = static_cast<std::uint8_t>(v.below(8));
        v(s.health);
        v(s.cannons);
        for (int c = 0; c < 4; ++c) {
            sim::CargoSlot slot;
            slot.ware = c == 2 ? 0 : static_cast<std::uint8_t>(1 + v.below(60));  // one empty hold
            v(slot.amount);
            v(slot.price);
            s.cargo.push_back(slot);
        }
        for (std::size_t n = 0; n < sim::TradeRoute::kOriginalStops; ++n) {
            sim::RouteStop& stop = s.route.stops[n];
            stop.target = target(v);
            for (std::size_t e = 0; e < sim::RouteStop::kOriginalEntries; ++e) {
                v(stop.entries[e].ware);
                v(stop.entries[e].unload);
                v(stop.entries[e].amount);
            }
        }
        if (wide) {
            // The stops beyond the original's four and the wares beyond its three (versions 2
            // and 3 of FLEE), apart from the values above so that the narrow world keeps the bytes
            // of version 1.
            for (std::size_t n = sim::TradeRoute::kOriginalStops; n < sim::TradeRoute::kStops; ++n) {
                const auto m = static_cast<std::uint32_t>(n + 8 * k);
                s.route.stops[n].target = sim::Target::position(m * 7, m * 5);
                s.route.stops[n].entries[n % 3] =
                    sim::RouteEntry{static_cast<std::uint8_t>(1 + m % 24), m % 2 == 0, m * 173};
            }
            for (std::size_t n = 0; n < sim::TradeRoute::kStops; ++n) {
                const auto m = static_cast<std::uint32_t>(n + 8 * k);
                s.route.stops[n].entries[sim::RouteStop::kOriginalEntries + n % 5] =
                    sim::RouteEntry{static_cast<std::uint8_t>(1 + (m * 3) % 24), m % 3 == 0, m * 97};
            }
        }
        s.route.current = static_cast<std::uint8_t>(v.below(sim::TradeRoute::kOriginalStops));
        v(s.route.active);
        v(s.route.one_way);
        v(s.route.problem);
        v(s.route.problem_rounds);
        s.target = target(v);
        s.order = target(v);
        s.previous = target(v);
        if (k != 1) {
            s.queue = {target(v), target(v)};
        }
        s.attack_move = k == 0;
        s.hold = k != 1;
        v(s.held);
        v(s.sinking);
        v(s.next_shot_ms);
        v(s.repairing);
        v(s.for_sale);
        v(s.sale_factor);
        v(s.trader);
        for (int g = 0; g < 2; ++g) {
            sim::TraderGood good;
            v(good.price);
            v(good.age);
            v(good.amount);
            s.trader_goods.push_back(good);
        }
        v(s.trader_tools_ms);
        v(s.trader_ore_ms);
        v(s.near_island);
        v(s.source_slot);
        s.motion.phase = static_cast<sim::ShipMotion::Phase>(v.below(5));
        // A run, where the phase is one, must start and end on the map (validate).
        s.motion.count = static_cast<std::uint8_t>(v.below(16));
        v(s.motion.elapsed_us);
        v(s.motion.duration_us);
        s.motion.from_x = static_cast<std::int32_t>(16 * 4096 + v.below(368 * 4096));
        s.motion.from_y = static_cast<std::int32_t>(16 * 4096 + v.below(268 * 4096));
        s.motion.path = {v.next<std::uint8_t>(), v.next<std::uint8_t>(), v.next<std::uint8_t>()};
        s.motion.path_index = static_cast<std::uint8_t>(v.below(4));
        v(s.motion.radius);
        v(s.motion.failures);
        v(s.motion.turn_to);
        v(s.motion.next_follow_us);
        world.add_ship(s);
    }
    world.set_next_ship_id(9);

    for (std::uint32_t k = 0; k < 3; ++k) {
        sim::Soldier s;
        s.pos_x = static_cast<std::int32_t>((22 + k) * 4096 + v.below(4096));
        s.pos_y = static_cast<std::int32_t>(41 * 4096 + v.below(4096));
        v(s.figure);
        s.owner = v.player();
        s.direction = static_cast<std::uint8_t>(v.below(8));
        v(s.anim);
        s.island = k == 2 ? 0xFFFF : static_cast<std::uint16_t>(k);
        v(s.health);
        s.home = target(v);
        s.target = target(v);
        v(s.radius);
        v(s.failures);
        v(s.next_attack_ms);
        v(s.next_follow_ms);
        v(s.last_attack_ms);
        v(s.patrol.active);
        v(s.patrol.one_way);
        s.patrol.index = static_cast<std::uint8_t>(v.below(2));
        s.patrol.points = {target(v), target(v)};
        if (k != 0) {
            s.queue = {target(v)};
        }
        s.attack_move = k == 1;
        s.hold = k != 0;
        v(s.disbanded);
        v(s.source_slot);
        s.motion.phase = static_cast<sim::SoldierMotion::Phase>(v.below(5));
        s.motion.direction = static_cast<std::uint8_t>(v.below(8));
        s.motion.count = static_cast<std::uint8_t>(v.below(129));
        v(s.motion.elapsed_us);
        v(s.motion.duration_us);
        s.motion.from_x = static_cast<std::int32_t>(70 * 4096 + v.below(260 * 4096));
        s.motion.from_y = static_cast<std::int32_t>(70 * 4096 + v.below(160 * 4096));
        s.motion.path = {v.next<std::uint8_t>(), v.next<std::uint8_t>()};
        s.motion.path_index = static_cast<std::uint8_t>(v.below(3));
        world.add_soldier(s);
    }
    world.set_next_soldier_id(12);

    for (int k = 0; k < 2; ++k) {
        sim::Tower t;
        at(t.island, t.x, t.y);
        t.owner = v.player();
        v(t.figure);
        t.direction = static_cast<std::uint8_t>(v.below(8));
        v(t.next_shot_ms);
        v(t.next_think_us);
        v(t.source_slot);
        world.towers().push_back(t);

        sim::BuildingDamage d;
        at(d.island, d.x, d.y);
        v(d.damage);
        world.building_damage().push_back(d);

        sim::PendingHit hit;
        v(hit.due_ms);
        hit.shooter = target(v);
        hit.attacker = v.player();
        v(hit.cannon);
        v(hit.damage);
        hit.target = target(v);
        v(hit.from_x);
        v(hit.from_y);
        world.pending_hits().push_back(hit);

        sim::Animal a;
        at(a.island, a.x, a.y);
        v(a.figure);
        world.add_animal(a);

        sim::Disaster dis;
        dis.kind = k == 0 ? sim::Disaster::Kind::Plague : sim::Disaster::Kind::Fire;
        at(dis.island, dis.x, dis.y);
        v(dis.elapsed);
        v(dis.duration);
        v(dis.id);
        world.disasters().push_back(dis);
    }

    sim::Diplomacy& dip = world.diplomacy();
    for (sim::PlayerId a = 0; a < kPlayers; ++a) {
        for (sim::PlayerId b = 0; b < kPlayers; ++b) {
            sim::Relation& r = dip.relation(a, b);
            r.peace = static_cast<sim::Treaty>(v.below(4));
            r.trade = static_cast<sim::Treaty>(v.below(4));
            v(r.war_time_ms);
            v(r.tribute);
            v(r.trade_mood);
            v(r.damage);
        }
        std::array<sim::DiplomacyEvent, sim::Diplomacy::kEvents> events{};
        for (sim::DiplomacyEvent& e : events) {
            e.type = static_cast<sim::DiplomacyEventType>(v.below(14));
            e.partner = v.player();
            v(e.time_ms);
        }
        dip.set_events(a, events);
        dip.set_blamed(a, v.player());
    }

    sim::VolcanoState& volcanoes = world.volcanoes();
    v(volcanoes.next_ms);
    const std::uint16_t crater_island = wide ? 2 : 1;
    const sim::Crater& crater = world.islands()[crater_island].craters[1];
    volcanoes.last = sim::CraterRef{crater_island, crater.x, crater.y};
    sim::Eruption e;
    e.crater = sim::CraterRef{0, world.islands()[0].craters[0].x, world.islands()[0].craters[0].y};
    v(e.seed);
    v(e.step);
    v(e.next_us);
    volcanoes.eruptions.push_back(e);
    sim::LavaLump lump;
    lump.island = 1;
    for (std::int64_t* q : {&lump.x, &lump.y, &lump.z, &lump.dx, &lump.dy, &lump.dz, &lump.speed}) {
        v(*q);
    }
    v(lump.launch_us);
    v(lump.leg_start_us);
    v(lump.leg_end_us);
    v(lump.heading);
    v(lump.tilt);
    v(lump.grounded);
    v(lump.anim);
    volcanoes.lava.push_back(lump);

    world.set_time_ms(v.next<std::uint32_t>() / 50 * 50);  // game time moves in ticks of 50 ms
    world.set_city_name_index(v.next<std::uint32_t>());
    sim::World::GameSettings settings;
    v(settings.difficulty);
    v(settings.flags);
    v(settings.pirate_interval_s);
    world.set_game_settings(settings);
    world.set_next_drought_ms(v.next<std::uint64_t>());
    world.set_drought_seed(v.next<std::uint8_t>());
    for (std::size_t s = 0; s < static_cast<std::size_t>(sim::RandomStream::Count); ++s) {
        const auto state = v.next<std::uint64_t>();
        world.random(static_cast<sim::RandomStream>(s)) = sim::Random::from_state(state, v.next<std::uint64_t>());
    }
    world.set_next_disaster_id(v.next<std::uint32_t>());
    world.set_island_phase(v.next<std::uint8_t>());

    sim::Rules rules;
    rules.demolish_refund = sim::Refund::Partial;
    rules.refund_percent = 37;
    v(rules.max_ships_per_player);
    rules.guarantee_resources = false;
    v(rules.max_game_speed);
    rules.droughts = sim::Droughts::Frequent;
    rules.volcanoes = sim::Volcanoes::On;
    rules.trader_prices = sim::TraderPrices::Market;
    v(rules.max_army);
    rules.bugs.classic_ai = true;
    world.set_saved_rules(rules);

    // The original's file the game came from (ORIG keeps every chunk but the island tiles).
    assets::ChunkWriter chunks;
    const std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    chunks.add("INSEL5", payload);
    chunks.add("INSELHAUS", payload);
    chunks.add("SHIP4", std::span<const std::uint8_t>(payload).first(5));
    auto source = assets::ChunkFile::from_bytes(chunks.take());
    REQUIRE(source.ok());
    world.set_source(std::make_shared<const assets::ChunkFile>(std::move(source).value()));

    std::vector<std::uint8_t> state(40);
    for (std::uint8_t& b : state) {
        v(b);
    }
    world.set_simulation_state(state);

    // MLCK came later: its values are drawn last, so the other sections keep theirs.
    for (std::size_t c = 0; c < world.cities().size(); ++c) {
        const sim::City& city = world.cities()[c];
        v(world.city(city.island, city.index)->material_reminder_ms);
    }
    // CPOP came later still.
    for (std::size_t c = 0; c < world.cities().size(); ++c) {
        const sim::City& city = world.cities()[c];
        v(world.city(city.island, city.index)->population_message);
    }
    // CGUP came later still.
    for (std::size_t c = 0; c < world.cities().size(); ++c) {
        const sim::City& city = world.cities()[c];
        v(world.city(city.island, city.index)->ships_gave_up);
    }
    // The editor's sections (SCNI, MARK, SLUA, EDIT) came later still; the narrow world is no
    // scenario and has none of them.
    if (wide) {
        sim::ScenarioInfo info;
        info.names = {{"en", "Two Coasts"}, {"de", "Zwei K\xc3\xbcsten"}};
        info.briefings = {{"en", "Found two cities.\nKeep them fed."}};
        info.stars = v.next<std::uint8_t>() % (sim::ScenarioInfo::kMaxStars + 1);
        info.author = "Merlin";
        info.version = "1.2";
        info.original_limits = true;
        world.set_scenario_info(info);
        sim::ScenarioMarker harbour{"harbour", sim::ScenarioMarker::Shape::Point, 0, 0, 1, 1};
        harbour.x = v.next<std::uint32_t>() % 400;
        harbour.y = v.next<std::uint32_t>() % 300;
        world.set_markers({harbour, {"north_coast", sim::ScenarioMarker::Shape::Area, 10, 20, 30, 40}});
        world.set_scenario_script("function on_start()\n  print('hello')\nend\n");
        std::vector<std::uint8_t> editor(24);
        for (std::uint8_t& b : editor) {
            v(b);
        }
        world.set_editor_state(editor);
        // PLAN came later still.
        sim::PlannedIslands planned;
        planned.plan.natives = {v.next<std::uint32_t>(), v.next<std::uint32_t>()};
        v(planned.plan.max_crops);
        v(planned.plan.gold_min_size);
        v(planned.plan.gold_max_size);
        planned.plan.ores[0] = {20101, v.next<std::uint16_t>(), v.next<std::uint32_t>()};
        planned.plan.crops[11] = {20401, v.next<std::uint16_t>(), 0};
        planned.plan.treasures[3] = {20501, v.next<std::uint16_t>(), 0};
        assets::ScenarioPlan::Island planned_island;
        planned_island.climate = 2;
        planned_island.size = 4;
        planned_island.flags = v.next<std::uint8_t>();
        planned_island.number = 40;
        planned_island.template_number = v.next<std::uint16_t>();
        planned_island.x = v.next<std::uint32_t>() % 400;
        planned_island.y = v.next<std::uint32_t>() % 300;
        planned.plan.islands.push_back(planned_island);
        std::array<std::uint16_t, 512> table{};
        for (std::uint16_t& t : table) {
            v(t);
        }
        planned.table = table;
        world.set_planned_islands(planned);
    }
    return world;
}

/// What the tables record of a section: tag, version, flags, size and CRC-32 of the data.
struct Golden {
    const char* tag;
    std::uint16_t version;
    std::uint16_t flags;
    std::uint32_t size;
    std::uint32_t crc;
};

/// Compares the sections with a table; prints the table the bytes give if they differ.
bool matches(const char* name, std::span<const std::uint8_t> data, std::span<const Golden> expected) {
    struct Section {
        std::string tag;
        std::uint16_t version = 0;
        std::uint16_t flags = 0;
        std::uint32_t size = 0;
        std::uint32_t crc = 0;
    };
    std::vector<Section> got;
    std::size_t at = 0;
    const auto u16 = [&](std::size_t i) { return static_cast<std::uint16_t>(data[i] | data[i + 1] << 8U); };
    while (at + 12 <= data.size()) {
        Section s;
        s.tag.assign(reinterpret_cast<const char*>(data.data() + at), 4);
        s.version = u16(at + 4);
        s.flags = u16(at + 6);
        s.size = u16(at + 8) | static_cast<std::uint32_t>(u16(at + 10)) << 16U;
        REQUIRE(s.size <= data.size() - at - 12);
        s.crc = core::crc32(data.subspan(at + 12, s.size));
        got.push_back(s);
        at += 12 + s.size;
    }
    bool same = at == data.size() && got.size() == expected.size();
    for (std::size_t i = 0; same && i < got.size(); ++i) {
        const Section& a = got[i];
        const Golden& b = expected[i];
        same = a.tag == b.tag && a.version == b.version && a.flags == b.flags && a.size == b.size && a.crc == b.crc;
    }
    if (!same) {
        std::printf("  %s: the sections give\n", name);
        for (const Section& s : got) {
            std::printf("    {\"%s\", %u, %u, %u, 0x%08X},\n", s.tag.c_str(), s.version, s.flags, s.size, s.crc);
        }
    }
    return same;
}

/// The version of the section `tag` in an own savegame, 0 if it has none.
std::uint16_t section_version(const core::Bytes& bytes, std::string_view tag) {
    std::size_t at = kFileHeader;
    while (at + 12 <= bytes.size()) {
        const auto u16 = [&](std::size_t i) { return static_cast<std::uint16_t>(bytes[i] | bytes[i + 1] << 8U); };
        const std::uint32_t size = u16(at + 8) | static_cast<std::uint32_t>(u16(at + 10)) << 16U;
        if (std::string_view(reinterpret_cast<const char*>(bytes.data() + at), 4) == tag) {
            return u16(at + 4);
        }
        at += 12 + size;
    }
    return 0;
}

// clang-format off
constexpr Golden kSaveNarrow[] = {
    {"META", 1, 0, 37, 0x5A2EBCE3},
    {"MAPW", 2, 1, 1620, 0x46422F5B},
    {"RULE", 1, 1, 82, 0x56A9D418},
    {"WRLD", 1, 1, 419, 0xC3B8D9A4},
    {"WRL2", 1, 1, 4, 0x5ADE7D82},
    {"ISLE", 1, 1, 274, 0x6FCE0641},
    {"PLAY", 1, 1, 143, 0xFF1BD500},
    {"PSTA", 1, 1, 96, 0xAE93529B},
    {"MISN", 1, 1, 302, 0xD6CFC995},
    {"CITY", 1, 1, 474, 0x37ECCCBD},
    {"CSTA", 1, 1, 42, 0x2AD09545},
    {"MLCK", 1, 1, 26, 0x763D1485},
    {"CPOP", 1, 1, 12, 0xB3A6798A},
    {"CGUP", 1, 0, 12, 0xAD80A19D},
    {"SCOR", 2, 1, 102, 0xCDF6D496},
    {"PROD", 1, 1, 116, 0x24D09C54},
    {"HOUS", 1, 1, 64, 0x8D864860},
    {"GROW", 1, 1, 31, 0x1C17DE55},
    {"COST", 1, 1, 20, 0xFACEC437},
    {"ANIM", 1, 1, 20, 0x86AEF97B},
    {"DISA", 1, 1, 30, 0x5BCDD397},
    {"VOLC", 1, 1, 122, 0x1FEE9F6C},
    {"FLEE", 1, 1, 1289, 0xCD0A3C2E},
    {"EPOC", 1, 1, 9, 0xD322D3E6},
    {"ARMY", 1, 1, 842, 0x53D74BB0},
    {"DIPL", 1, 1, 1640, 0xE500956A},
    {"XPLR", 1, 1, 46, 0xC4686738},
    {"QUEU", 1, 1, 148, 0x52E1525C},
    {"STNC", 1, 1, 28, 0x6CE6EFC4},
    {"ORIG", 1, 0, 54, 0x7AEE655B},
    {"SIMU", 1, 1, 40, 0xB72D21DD},
};

constexpr Golden kSaveWide[] = {
    {"META", 1, 0, 38, 0xB89CD646},
    {"SCNI", 1, 0, 111, 0x88CEF8A7},
    {"MAPW", 2, 1, 5724, 0x3F3E2223},
    {"RULE", 1, 1, 82, 0xCC115664},
    {"WRLD", 1, 1, 419, 0x3DED0795},
    {"WRL2", 1, 1, 4, 0x637B2D3C},
    {"ISLE", 3, 1, 484, 0x77AE4E1B},
    {"PLAY", 1, 1, 143, 0x177E459C},
    {"PSTA", 2, 1, 136, 0x25534BE0},
    {"MISN", 1, 1, 302, 0x93D421B5},
    {"CITY", 1, 1, 473, 0x2E7EAC9E},
    {"CSTA", 1, 1, 42, 0x70987BC5},
    {"MLCK", 1, 1, 26, 0xC45417C3},
    {"CPOP", 1, 1, 12, 0x822F95B4},
    {"CGUP", 1, 0, 12, 0xDD9F271A},
    {"SCOR", 2, 1, 102, 0x5DBBA573},
    {"PROD", 1, 1, 116, 0x4814F74A},
    {"HOUS", 1, 1, 64, 0x15190742},
    {"GROW", 1, 1, 31, 0xA45C7F9C},
    {"COST", 1, 1, 20, 0x5E0C98CB},
    {"ANIM", 1, 1, 20, 0x45A8858E},
    {"DISA", 1, 1, 30, 0x3FF2904C},
    {"VOLC", 2, 1, 126, 0x3DB4583E},
    {"FLEE", 3, 1, 2475, 0x78D4078B},
    {"EPOC", 1, 1, 11, 0xFD024085},
    {"ARMY", 1, 1, 842, 0x21ECF2E9},
    {"DIPL", 1, 1, 1640, 0xE77D58A7},
    {"XPLR", 1, 1, 67, 0x743711C9},
    {"QUEU", 1, 1, 148, 0x58163040},
    {"STNC", 1, 1, 28, 0x6CE6EFC4},
    {"ORIG", 1, 0, 54, 0x7AEE655B},
    {"MARK", 1, 0, 64, 0xB6F559EA},
    {"SLUA", 1, 1, 45, 0x668C6608},
    {"EDIT", 1, 0, 28, 0xA5E4FCD5},
    {"PLAN", 1, 1, 1263, 0x36403ECF},
    {"SIMU", 1, 1, 40, 0xFA8DCE43},
};

constexpr Golden kChunkNarrow[] = {
    {"RULE", 1, 1, 82, 0x56A9D418},
    {"WRLD", 1, 1, 419, 0xC3B8D9A4},
    {"ISLE", 1, 1, 274, 0x6FCE0641},
    {"TILE", 1, 1, 8164, 0x769F7EC8},
    {"PLAY", 1, 1, 143, 0xFF1BD500},
    {"PSTA", 1, 1, 96, 0xAE93529B},
    {"MISN", 1, 1, 302, 0xD6CFC995},
    {"CITY", 1, 1, 474, 0x37ECCCBD},
    {"CSTA", 1, 1, 42, 0x2AD09545},
    {"MLCK", 1, 1, 26, 0x763D1485},
    {"CPOP", 1, 1, 12, 0xB3A6798A},
    {"CGUP", 1, 0, 12, 0xAD80A19D},
    {"SCOR", 2, 1, 102, 0xCDF6D496},
    {"PROD", 1, 1, 116, 0x24D09C54},
    {"HOUS", 1, 1, 64, 0x8D864860},
    {"GROW", 1, 1, 31, 0x1C17DE55},
    {"COST", 1, 1, 20, 0xFACEC437},
    {"SHIP", 1, 0, 127, 0xDA9C5944},
    {"ANIM", 1, 1, 20, 0x86AEF97B},
    {"DISA", 1, 1, 30, 0x5BCDD397},
    {"VOLC", 1, 1, 122, 0x1FEE9F6C},
    {"FLEE", 1, 1, 1289, 0xCD0A3C2E},
    {"EPOC", 1, 1, 9, 0xD322D3E6},
    {"ARMY", 1, 1, 842, 0x53D74BB0},
    {"DIPL", 1, 1, 1640, 0xE500956A},
    {"XPLR", 1, 1, 46, 0xC4686738},
    {"QUEU", 1, 1, 148, 0x52E1525C},
    {"STNC", 1, 1, 28, 0x6CE6EFC4},
    {"SIMU", 1, 1, 40, 0xB72D21DD},
};

constexpr Golden kChunkWide[] = {
    {"RULE", 1, 1, 82, 0xCC115664},
    {"WRLD", 1, 1, 419, 0x3DED0795},
    {"ISLE", 3, 1, 484, 0x77AE4E1B},
    {"TILE", 1, 1, 34684, 0x3991F216},
    {"PLAY", 1, 1, 143, 0x177E459C},
    {"PSTA", 2, 1, 136, 0x25534BE0},
    {"MISN", 1, 1, 302, 0x93D421B5},
    {"CITY", 1, 1, 473, 0x2E7EAC9E},
    {"CSTA", 1, 1, 42, 0x70987BC5},
    {"MLCK", 1, 1, 26, 0xC45417C3},
    {"CPOP", 1, 1, 12, 0x822F95B4},
    {"CGUP", 1, 0, 12, 0xDD9F271A},
    {"SCOR", 2, 1, 102, 0x5DBBA573},
    {"PROD", 1, 1, 116, 0x4814F74A},
    {"HOUS", 1, 1, 64, 0x15190742},
    {"GROW", 1, 1, 31, 0xA45C7F9C},
    {"COST", 1, 1, 20, 0x5E0C98CB},
    {"SHIP", 1, 0, 127, 0x0B22D35F},
    {"ANIM", 1, 1, 20, 0x45A8858E},
    {"DISA", 1, 1, 30, 0x3FF2904C},
    {"VOLC", 2, 1, 126, 0x3DB4583E},
    {"FLEE", 3, 1, 2475, 0x78D4078B},
    {"EPOC", 1, 1, 11, 0xFD024085},
    {"ARMY", 1, 1, 842, 0x21ECF2E9},
    {"DIPL", 1, 1, 1640, 0xE77D58A7},
    {"XPLR", 1, 1, 67, 0x743711C9},
    {"QUEU", 1, 1, 148, 0x58163040},
    {"STNC", 1, 1, 28, 0x6CE6EFC4},
    {"MARK", 1, 0, 64, 0xB6F559EA},
    {"SLUA", 1, 1, 45, 0x668C6608},
    {"PLAN", 1, 1, 1263, 0x36403ECF},
    {"SIMU", 1, 1, 40, 0xFA8DCE43},
};
// clang-format on

core::Bytes save(const sim::World& world, const assets::BuildingTable& buildings) {
    auto bytes = sim::write_game(world, buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    return std::move(bytes).value();
}

}  // namespace

TEST_CASE("save format: the sections of the own savegame keep their bytes") {
    const auto buildings = make_buildings();
    for (const bool wide : {false, true}) {
        const core::Bytes bytes = save(make_world(buildings, wide), buildings);
        CHECK(matches(wide ? "kSaveWide" : "kSaveNarrow", std::span<const std::uint8_t>(bytes).subspan(kFileHeader),
                      wide ? std::span<const Golden>(kSaveWide) : std::span<const Golden>(kSaveNarrow)));
    }
}

TEST_CASE("save format: the sections of the chunk in a .GAM keep their bytes") {
    const auto buildings = make_buildings();
    for (const bool wide : {false, true}) {
        const core::Bytes bytes = sim::encode_extension(make_world(buildings, wide), buildings, nullptr, {});
        CHECK(matches(wide ? "kChunkWide" : "kChunkNarrow", std::span<const std::uint8_t>(bytes).subspan(kChunkHeader),
                      wide ? std::span<const Golden>(kChunkWide) : std::span<const Golden>(kChunkNarrow)));
    }
}

TEST_CASE("save format: every field read comes back where it was written") {
    const auto buildings = make_buildings();
    for (const bool wide : {false, true}) {
        // Own savegame: the whole world from the sections.
        const core::Bytes bytes = save(make_world(buildings, wide), buildings);
        std::vector<std::string> warnings;
        auto loaded = sim::read_game(bytes, buildings, &warnings);
        REQUIRE(loaded.ok());
        CHECK(warnings.empty());
        CHECK(save(*loaded, buildings) == bytes);

        // Chunk in a .GAM: applied to a world with the same islands and nothing else.
        const core::Bytes chunk = sim::encode_extension(make_world(buildings, wide), buildings, nullptr, {});
        sim::World bare(400, 300);
        add_islands(bare, buildings, wide);
        REQUIRE(sim::apply_extension(bare, buildings, chunk, {}).ok());
        CHECK(sim::encode_extension(bare, buildings, nullptr, {}) == chunk);
    }
}

TEST_CASE("save format: who played each player comes back (version 4 of PSTA)") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings, false);
    const core::Bytes before = save(world, buildings);
    world.player(1)->owner = 0x1234'5678'9ABC'DEF0ULL;
    world.player(2)->gates_closed = true;
    const core::Bytes bytes = save(world, buildings);
    CHECK(bytes != before);
    auto loaded = sim::read_game(bytes, buildings, nullptr);
    REQUIRE(loaded.ok());
    for (std::size_t p = 0; p < world.players().size(); ++p) {
        CHECK(loaded->players()[p].owner == world.players()[p].owner);
        CHECK(loaded->players()[p].gates_closed == (p == 2));
    }
    CHECK(save(*loaded, buildings) == bytes);

    const core::Bytes chunk = sim::encode_extension(world, buildings, nullptr, {});
    sim::World bare(400, 300);
    add_islands(bare, buildings, false);
    REQUIRE(sim::apply_extension(bare, buildings, chunk, {}).ok());
    CHECK(bare.players()[1].owner == world.players()[1].owner);
    CHECK(bare.players()[0].owner == 0);
}

TEST_CASE("save format: closed city gates come back (version 3 of PSTA)") {
    const auto buildings = make_buildings();
    for (const bool wide : {false, true}) {
        sim::World world = make_world(buildings, wide);
        world.player(2)->gates_closed = true;
        const core::Bytes bytes = save(world, buildings);
        auto loaded = sim::read_game(bytes, buildings, nullptr);
        REQUIRE(loaded.ok());
        for (std::size_t p = 0; p < world.players().size(); ++p) {
            CHECK(loaded->players()[p].gates_closed == (p == 2));
            CHECK(loaded->players()[p].scenario == world.players()[p].scenario);
        }
        CHECK(save(*loaded, buildings) == bytes);

        const core::Bytes chunk = sim::encode_extension(world, buildings, nullptr, {});
        sim::World bare(400, 300);
        add_islands(bare, buildings, wide);
        REQUIRE(sim::apply_extension(bare, buildings, chunk, {}).ok());
        CHECK(bare.players()[2].gates_closed);
        CHECK(!bare.players()[1].gates_closed);
    }
}

TEST_CASE("save format: the goods statistics of the cities come back (GLDG)") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings, false);
    REQUIRE(!world.cities().empty());
    sim::City& city = *world.city(world.cities()[0].island, world.cities()[0].index);
    for (std::uint32_t m = 0; m < 70; ++m) {
        city.book(19, sim::GoodsFlow::Produced, 10 + m);
        city.book(14, sim::GoodsFlow::Residents, 3 * m);
        city.ledger.close_minute(city.goods);
        sim::CitySample s;
        s.set(sim::CityFigure::Taxes, 100 + m);
        s.set(sim::CityFigure::Sales, -7);
        s.set_class(2, 40 * m);
        city.history.close_minute(s);
    }
    city.book(19, sim::GoodsFlow::Sold, 32);  // the running minute
    city.book(19, sim::GoodsFlow::SoldValue, 75);
    city.ledger.set_warned(14, true);
    const core::Bytes bytes = save(world, buildings);
    std::vector<std::string> warnings;
    auto loaded = sim::read_game(bytes, buildings, &warnings);
    REQUIRE(loaded.ok());
    CHECK(warnings.empty());
    const sim::City* back = loaded->city(city.island, city.index);
    REQUIRE(back != nullptr);
    CHECK(back->ledger.minutes_closed() == 70);
    CHECK(back->ledger.warned(14));
    CHECK(back->ledger.totals(19, 5).of(sim::GoodsFlow::Produced) ==
          city.ledger.totals(19, 5).of(sim::GoodsFlow::Produced));
    CHECK(back->ledger.last_minutes(14) == city.ledger.last_minutes(14));
    CHECK(back->ledger.last_quarters(19) == city.ledger.last_quarters(19));
    CHECK(back->history == city.history);
    CHECK(back->history.last_quarters().size() == 4);
    CHECK(save(*loaded, buildings) == bytes);
    CHECK(section_version(bytes, "GLDG") == 1);  // nothing withheld: the bytes of version 1

    // What producers withheld for a full warehouse needs version 2.
    city.book(19, sim::GoodsFlow::Withheld, 40);
    city.ledger.close_minute(city.goods);
    const core::Bytes withheld = save(world, buildings);
    CHECK(section_version(withheld, "GLDG") == 2);
    auto again = sim::read_game(withheld, buildings, &warnings);
    REQUIRE(again.ok());
    CHECK(warnings.empty());
    const sim::City* other = again->city(city.island, city.index);
    REQUIRE(other != nullptr);
    CHECK(other->ledger.totals(19, 1).of(sim::GoodsFlow::Withheld) == 40);
    CHECK(other->ledger == city.ledger);
    CHECK(save(*again, buildings) == withheld);
}

TEST_CASE("save format: public buildings of a kind of their own beyond the 16 bits come back (HOUS 2)") {
    const auto buildings = make_buildings();
    sim::World world = make_world(buildings, false);
    REQUIRE(!world.houses().empty());
    namespace pb = assets::public_building;
    world.houses()[0].services = pb::kChapel | pb::own(1);  // still 16 bits: version 1
    const core::Bytes narrow = save(world, buildings);
    CHECK(section_version(narrow, "HOUS") == 1);

    const assets::ServiceBits wide = pb::kChapel | pb::own(2) | pb::own(pb::kOwnCount - 1);
    world.houses()[0].services = wide;
    const core::Bytes bytes = save(world, buildings);
    CHECK(section_version(bytes, "HOUS") == 2);
    std::vector<std::string> warnings;
    auto loaded = sim::read_game(bytes, buildings, &warnings);
    REQUIRE(loaded.ok());
    CHECK(warnings.empty());
    CHECK(loaded->houses()[0].services == wide);
    CHECK(save(*loaded, buildings) == bytes);
}
