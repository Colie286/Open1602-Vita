// SPDX-License-Identifier: GPL-3.0-or-later
// Production state: building animation frames, smoke and status icons.

#include <algorithm>
#include <cmath>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/pool.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// A bakery (HANDWERK with smoke RAUCHBAECK 48), a forester (PLANTAGE with LagAniFlg) and a market.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Gfx: 0\n  Size: 2, 2\n  AnimAnz: 8\n  AnimAdd: 4\n  AnimTime: 100\n"
        "  Einhoffs: 2.5\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Rauchfignr: 48\n    Maxlager: 4\n    Maxnorohst: 5\n"
        "    Ware: 10\n    Rohstoff: 5\n    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
        "  @Nummer: +1\n  Id: 22201\n  Gfx: 40\n  Size: 1, 1\n  AnimAnz: 4\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Maxlager: 10\n    LagAniFlg: 1\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20809\n  Gfx: 50\n  Size: 1, 1\n  AnimAnz: 3\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Rauchfignr: 59\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// RAUCHBAECK (48) and NOAKTIV (46) like figuren.cod: no Blocknr, drawn from EFFEKTE.BSH.
assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 46\n  Gfx: 10\n  Rotate: 0\n  Posoffs: -35, 35\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 4\n    AnimSpeed: 50\n"
        "    Nummer: 1\n    Kind: 1\n    AnimOffs: 4\n    AnimAdd: 1\n    AnimAnz: 4\n    AnimSpeed: 50\n"
        "    Nummer: 2\n    Kind: 1\n    AnimOffs: 8\n    AnimAdd: 1\n    AnimAnz: 4\n    AnimSpeed: 50\n"
        "  EndObj\n"
        "  Nummer: 48\n  Gfx: 20\n  Rotate: 0\n  Posoffs: -20, 20\n  Fahnoffs: -0.5, 0.25, 2.0\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 5\n    AnimSpeed: 80\n"
        "  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

sim::Production make(std::uint16_t island, std::uint16_t x, std::uint16_t y, bool enabled, std::uint8_t rate,
                     std::uint8_t idle, std::uint16_t stock) {
    sim::Production p;
    p.island = island;
    p.x = x;
    p.y = y;
    p.enabled = enabled;
    p.rate = rate;
    p.idle_cycles = idle;
    p.stock = stock;
    return p;
}

}  // namespace

TEST_CASE("production: HAUS_PRODTYP fields for the production state") {
    const auto buildings = make_buildings();
    const assets::BuildingType& bakery = *buildings.by_id(20503);
    CHECK(bakery.max_stock == 4 * assets::kStockUnitsPerTon);
    CHECK(bakery.max_idle_cycles == 5);
    CHECK(bakery.icon_height == 2.5);
    CHECK(bakery.ware == 10);
    CHECK(bakery.raw_ware == 5);
    CHECK(bakery.raw_amount == 32);
    CHECK(bakery.produce_amount == 32);
    CHECK(bakery.interval == 20);
    CHECK(!bakery.stock_animation);
    CHECK(sim::is_producer(bakery));
    CHECK(buildings.by_id(22201)->stock_animation);
    CHECK(!sim::is_producer(*buildings.by_id(20809)));  // market: kind 7
    CHECK(render::has_production_animation(*buildings.by_id(20809)));
}

TEST_CASE("production: animation frames follow the building's state") {
    const auto buildings = make_buildings();
    const assets::BuildingType& bakery = *buildings.by_id(20503);
    sim::Production p;
    p.anim_phase = 3;
    // Working: runs from its phase with the global frame; idle or switched off: stands still.
    p.rate = sim::Production::kFullRate;
    CHECK(render::production_frame(bakery, 10, &p) == (3 + 10) % 8);
    p.rate = 0;
    CHECK(render::production_frame(bakery, 10, &p) == 3);
    p.rate = sim::Production::kFullRate;
    p.enabled = false;
    CHECK(render::production_frame(bakery, 10, &p) == 3);
    // Without state the original uses the global frame alone.
    CHECK(render::production_frame(bakery, 10, nullptr) == 10 % 8);

    // LagAniFlg: the frame shows the fill level, (AnimAnz - 1) * stock / Maxlager, rounded.
    const assets::BuildingType& forester = *buildings.by_id(22201);
    p = sim::Production{};
    p.stock = 0;
    CHECK(render::production_frame(forester, 7, &p) == 0);
    p.stock = 5 * 32;  // half of 10 t: 1.5 -> 2
    CHECK(render::production_frame(forester, 7, &p) == 2);
    p.stock = 10 * 32;
    CHECK(render::production_frame(forester, 7, &p) == 3);
    p.stock = 40 * 32;  // above Maxlager: stays on the last frame
    CHECK(render::production_frame(forester, 7, &p) == 3);
    CHECK(render::production_frame(forester, 7, nullptr) == 0);

    // Market: the stalls fill with the progress of the cycle, (AnimAnz - 1) * progress / 512.
    const assets::BuildingType& market = *buildings.by_id(20809);
    p.produced = 0;
    CHECK(render::production_frame(market, 1, &p) == 0);
    p.produced = 256;
    CHECK(render::production_frame(market, 1, &p) == 1);
    p.produced = 514;  // seen in a savegame
    CHECK(render::production_frame(market, 1, &p) == 2);
}

TEST_CASE("production: status of a building and smoke") {
    const auto buildings = make_buildings();
    const assets::BuildingType& bakery = *buildings.by_id(20503);
    sim::Production p;
    CHECK(sim::production_status(p, bakery) == sim::ProductionStatus::Ok);
    p.idle_cycles = 5;
    CHECK(sim::production_status(p, bakery) == sim::ProductionStatus::NoRawGoods);
    p.enabled = false;
    CHECK(sim::production_status(p, bakery) == sim::ProductionStatus::Disabled);
    CHECK(!sim::smoke_visible(p));
    p.enabled = true;
    CHECK(!sim::smoke_visible(p));
    p.rate = sim::Production::kFullRate;
    CHECK(sim::smoke_visible(p));
}

TEST_CASE("production: world lookup by island and origin tile") {
    sim::World world;
    world.set_production(make(2, 10, 11, true, 128, 0, 32));
    world.set_production(make(2, 11, 10, true, 0, 0, 0));
    REQUIRE(world.production(2, 10, 11) != nullptr);
    CHECK(world.production(2, 10, 11)->stock == 32);
    CHECK(!world.production(2, 11, 10)->working());
    CHECK(world.production(1, 10, 11) == nullptr);
    world.set_production(make(2, 10, 11, false, 0, 0, 0));  // replaces
    CHECK(world.productions().size() == 2);
    CHECK(!world.production(2, 10, 11)->enabled);
}

TEST_CASE("production: smoke and status icons on buildings") {
    const auto buildings = make_buildings();
    const auto figures = make_figures();
    sim::World world;
    world.set_players({sim::Player{}, sim::Player{sim::PlayerRole::Computer, 1, 0, ""}});
    sim::WorldIsland island{assets::IslandHeader{}, sim::IslandMap(8, 8), 5, 5};
    REQUIRE(island.map.place(*buildings.by_id(20503), 1, 1, 0, 0, 0).ok());  // player 0
    REQUIRE(island.map.place(*buildings.by_id(20503), 4, 4, 1, 0, 1).ok());  // player 1, rotated
    REQUIRE(island.map.place(*buildings.by_id(20503), 4, 0, 0, 0).ok());     // no city: nothing
    island.discover(0);  // the local player's own island: its figures show
    REQUIRE(world.add_island(std::move(island)).ok());

    const auto all = render::collect_building_figures(world, buildings, figures);
    // Per bakery in a city: status icon and smoke.
    REQUIRE(all.size() == 4);
    CHECK(all[0].role == render::BuildingFigureRole::Status);
    CHECK(all[0].figure == 46);
    CHECK(all[0].x == 7.0);  // centre of the 2x2 footprint
    CHECK(all[0].y == 7.0);
    CHECK(all[0].z == 2.5);  // hovers at the bakery's Einhoffs height
    CHECK(all[1].role == render::BuildingFigureRole::Smoke);
    CHECK(all[1].x == 6.5);   // 5 + 1 + 1 - 0.5
    CHECK(all[1].y == 7.25);  // 5 + 1 + 1 + 0.25
    CHECK(all[1].z == 2.0);
    CHECK(all[3].x == 9.75);  // rotation 1: x - Fahnoffs[1], y + Fahnoffs[0]
    CHECK(all[3].y == 9.5);
    CHECK(all[3].owner == 1);

    // Effects sheet with 40 sprites 6x8.
    const auto terrain = test::make_sheet({{4, 2}});
    const auto effekte = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(40, {6, 8}));
    const auto palette = test::make_palette();
    const assets::SpriteSheet* sheets[] = {&terrain, &effekte};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 256);
    REQUIRE(atlas.ok());
    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Effekte)] = atlas->sheet_base(1);
    layers.local_player = 0;
    const render::WorldView view{render::Zoom::Large, 0};
    render::FigureSprites s;

    // Without production state nothing is shown.
    CHECK(!render::building_figure_sprites(all[1], world, buildings, view, layers, 0, s));
    world.set_production(make(0, 1, 1, true, 0, 0, 0));
    world.set_production(make(0, 4, 4, true, 0, 5, 0));
    // Idle bakery: no smoke; status OK: no icon.
    CHECK(!render::building_figure_sprites(all[1], world, buildings, view, layers, 0, s));
    CHECK(!render::building_figure_sprites(all[0], world, buildings, view, layers, 0, s));

    world.set_production(make(0, 1, 1, true, 128, 0, 0));
    render::BuildingFigure smoke = all[1];
    smoke.phase = 0;
    REQUIRE(render::building_figure_sprites(smoke, world, buildings, view, layers, 170, s));
    REQUIRE(s.count == 1);
    CHECK(s.parts[0].sprite == atlas->sheet_base(1) + 20 + 2);  // frame 170 / 80
    CHECK(s.parts[0].mode == render::DrawCommand::Mode::Translucent);
    CHECK(s.parts[0].tint.a == render::kSmokeAlpha);
    // Point: x = (6.5 - 7.25 + 350) * 32 = 11176, y = 13.75 * 16 = 220; Posoffs cancel out,
    // bottom 2.0 * 32 above the point and one pixel up; depth y + 20.
    CHECK(s.parts[0].x == 11176 - 3);
    CHECK(s.parts[0].y == 220 - 64 - 1);
    CHECK(s.row == (220 + 20 - 1) / 16);

    // Status icon: only for the local player's buildings; anim 0 off, 1 short of raw material.
    world.set_production(make(0, 1, 1, false, 0, 0, 0));
    render::BuildingFigure icon = all[0];
    icon.phase = 0;
    REQUIRE(render::building_figure_sprites(icon, world, buildings, view, layers, 0, s));
    CHECK(s.parts[0].sprite == atlas->sheet_base(1) + 10);
    CHECK(s.parts[0].mode == render::DrawCommand::Mode::Normal);
    // Point: x = (7 - 7 + 350) * 32 = 11200, y = 14 * 16 = 224; bottom 2.5 * 32 above it and one
    // pixel up (matches a screenshot of the original).
    CHECK(s.parts[0].x == 11200 - 3);
    CHECK(s.parts[0].y == 224 - 80 - 1);
    world.set_production(make(0, 1, 1, true, 0, 7, 0));
    REQUIRE(render::building_figure_sprites(icon, world, buildings, view, layers, 0, s));
    CHECK(s.parts[0].sprite == atlas->sheet_base(1) + 10 + 4);
    // Player 1's bakery is short of raw material too, but player 1 is not at this computer.
    CHECK(!render::building_figure_sprites(all[2], world, buildings, view, layers, 0, s));
    // A shipyard has no production state; its icon (kind 0x19) shows while it is
    // switched off, as a screenshot of the original shows.
    render::BuildingFigure yard = icon;
    yard.origin_x = 4;
    yard.origin_y = 0;
    CHECK(!render::building_figure_sprites(yard, world, buildings, view, layers, 0, s));
    world.add_cost_building(sim::CostBuilding{sim::CostBuilding::Kind::Shipyard, 0, 4, 0, false});
    REQUIRE(render::building_figure_sprites(yard, world, buildings, view, layers, 0, s));
    CHECK(s.parts[0].sprite == atlas->sheet_base(1) + 10);  // ANIM 0: switched off
    const std::int32_t off_y = s.parts[0].y;
    world.cost_buildings().back().enabled = true;
    CHECK(!render::building_figure_sprites(yard, world, buildings, view, layers, 0, s));
    // Short of material: ANIM 1 once it has gone Maxnorohst (5) cycles without.
    world.cost_buildings().back().idle_cycles = 4;
    CHECK(!render::building_figure_sprites(yard, world, buildings, view, layers, 0, s));
    world.cost_buildings().back().idle_cycles = 5;
    REQUIRE(render::building_figure_sprites(yard, world, buildings, view, layers, 0, s));
    CHECK(s.parts[0].sprite == atlas->sheet_base(1) + 10 + 4);
    CHECK(s.parts[0].y == off_y);
    // On fire: ANIM 2, 0.4 tile heights lower, before the other two states.
    world.cost_buildings().back().enabled = false;
    world.disasters().push_back(sim::Disaster{sim::Disaster::Kind::Fire, 0, 4, 0, 0, 20, 0});
    REQUIRE(render::building_figure_sprites(yard, world, buildings, view, layers, 0, s));
    CHECK(s.parts[0].sprite == atlas->sheet_base(1) + 10 + 8);
    CHECK(s.parts[0].y > off_y);
    world.disasters().clear();
    world.cost_buildings().back().idle_cycles = 0;

    layers.local_player = sim::kNoPlayer;
    CHECK(!render::building_figure_sprites(icon, world, buildings, view, layers, 0, s));
    CHECK(!render::building_figure_sprites(yard, world, buildings, view, layers, 0, s));
}

namespace {

/// A workshop: 1 t raw material per cycle, 1 t product, 20 s, store 4 t, kind HANDWERK.
assets::BuildingType workshop() {
    assets::BuildingType b;
    b.prod_kind = 1;
    b.ware = 10;
    b.raw_ware = 5;
    b.raw_amount = 32;
    b.produce_amount = 32;
    b.interval = 20;
    b.max_stock = 4 * 32;
    b.max_idle_cycles = 5;
    return b;
}

std::size_t count(const std::vector<sim::ProductionEvent>& events, sim::ProductionEvent::Kind kind) {
    return static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [&](const sim::ProductionEvent& e) { return e.kind == kind; }));
}

}  // namespace

TEST_CASE("production cycle: raw material in, goods out") {
    const assets::BuildingType b = workshop();
    sim::Production p;
    p.raw = 64;  // 2 t
    std::vector<sim::ProductionEvent> events;

    // Start: enough raw material for a full cycle -> rate 128, 20 s.
    sim::finish_cycle(p, b, 7, &events);
    CHECK(p.rate == 128);
    CHECK(p.countdown == 20);
    CHECK(p.produced == 32);
    CHECK(p.elapsed == 20);
    CHECK(p.raw == 64);  // taken when the cycle ends
    CHECK(count(events, sim::ProductionEvent::Kind::Started) == 1);
    CHECK(events[0].production == 7);
    // The original asks for more while there is raw material for at most two cycles.
    CHECK(count(events, sim::ProductionEvent::Kind::NeedsRawGoods) == 1);

    // End of cycle 1: 1 t used, 1 t made; the next cycle starts with the last ton and asks for more.
    events.clear();
    sim::finish_cycle(p, b, 7, &events);
    CHECK(p.raw == 32);
    CHECK(p.stock == 32);
    CHECK(p.rate == 128);
    CHECK(count(events, sim::ProductionEvent::Kind::Produced) == 1);
    REQUIRE(count(events, sim::ProductionEvent::Kind::NeedsRawGoods) == 1);
    CHECK(events.back().ware == 5);

    // End of cycle 2: no raw material left -> idle 11 s.
    events.clear();
    sim::finish_cycle(p, b, 7, &events);
    CHECK(p.raw == 0);
    CHECK(p.stock == 64);
    CHECK(p.rate == 0);
    CHECK(p.countdown == sim::kIdleWaitSeconds);
    CHECK(p.idle_cycles == 1);
    CHECK(count(events, sim::ProductionEvent::Kind::Stopped) == 1);

    // Half the raw material of a cycle still works at half rate; less does not.
    p.raw = 16;
    sim::finish_cycle(p, b, 7, &events);
    CHECK(p.rate == 64);
    CHECK(p.countdown == 10);
    CHECK(p.idle_cycles == 0);
    p.raw = 15;
    p.rate = 0;
    sim::finish_cycle(p, b, 7, &events);
    CHECK(p.rate == 0);
    CHECK(p.idle_cycles == 1);

    // Full store: no new cycle.
    p.raw = 64;
    p.stock = 4 * 32;
    events.clear();
    sim::finish_cycle(p, b, 7, &events);
    CHECK(p.rate == 0);
    CHECK(count(events, sim::ProductionEvent::Kind::StoreFull) == 1);
    CHECK(p.idle_cycles == 1);  // a full store is no shortage

    // The statistics window is halved beyond 239 s.
    p.stock = 0;
    p.elapsed = 230;
    p.produced = 100;
    sim::finish_cycle(p, b, 7, &events);
    CHECK(p.elapsed == (230 + 20) / 2);
    CHECK(p.produced == (100 + 32) / 2);
}

TEST_CASE("simulation: fixed ticks, production once per game second, speed and pause") {
    const auto buildings = make_buildings();
    sim::World world;
    sim::WorldIsland island{assets::IslandHeader{}, sim::IslandMap(8, 8), 5, 5};
    REQUIRE(island.map.place(*buildings.by_id(20503), 1, 1, 0, 0, 0).ok());
    REQUIRE(world.add_island(std::move(island)).ok());
    sim::Production p = make(0, 1, 1, true, 0, 0, 0);
    p.countdown = 3;
    world.set_production(p);

    const assets::FigureTable no_figures;
    sim::Simulation simulation(world, buildings, no_figures);
    simulation.advance(999);
    CHECK(world.productions()[0].countdown == 3);  // not a second yet
    CHECK(simulation.game_time_ms() == 950);       // whole ticks only
    simulation.advance(51);
    CHECK(world.productions()[0].countdown == 2);
    simulation.set_speed(2);
    simulation.advance(500);  // one game second at double speed
    CHECK(world.productions()[0].countdown == 1);
    simulation.set_speed(0);  // paused
    simulation.advance(10'000);
    CHECK(world.productions()[0].countdown == 1);
    simulation.set_speed(1);
    simulation.advance(1000);
    // Countdown over, no raw material: the bakery idles 11 s and asks for flour.
    CHECK(world.productions()[0].countdown == sim::kIdleWaitSeconds);
    CHECK(world.productions()[0].idle_cycles == 1);
    const auto events = simulation.take_production_events();
    CHECK(count(events, sim::ProductionEvent::Kind::NeedsRawGoods) == 1);
    CHECK(simulation.take_production_events().empty());

    // A stalled computer does not fast-forward the game.
    const auto before = simulation.game_time_ms();
    simulation.advance(60'000);
    CHECK(simulation.game_time_ms() == before + sim::Simulation::kTickMs);
    simulation.set_speed(100);
    CHECK(simulation.speed() == sim::Rules{}.max_game_speed);
}

TEST_CASE("simulation: a slow computer runs a budget of ticks per frame, the game slower (review point 20)") {
    const auto buildings = make_buildings();
    sim::World world;
    const assets::FigureTable no_figures;
    sim::Simulation simulation(world, buildings, no_figures);
    constexpr std::uint64_t kBudget = sim::Simulation::kMaxTicksPerAdvance;
    simulation.set_speed(8);
    // A frame of 400 ms at speed 8 is no stall: it asks for 64 ticks and gets the budget.
    simulation.advance(400);
    CHECK(simulation.game_time_ms() == kBudget * sim::Simulation::kTickMs);
    CHECK(simulation.behind());
    // The rest follows in the next frame; more than one budget owed is dropped.
    simulation.advance(0);
    CHECK(simulation.game_time_ms() == 2 * kBudget * sim::Simulation::kTickMs);
    CHECK(!simulation.behind());
    simulation.advance(2000);  // 320 ticks asked: one budget now, one more owed, the rest dropped
    simulation.advance(0);
    simulation.advance(0);
    CHECK(simulation.game_time_ms() == 4 * kBudget * sim::Simulation::kTickMs);
    CHECK(!simulation.behind());

    // The caller may end a frame early (its time is up); the rest is owed.
    std::uint32_t asked = 0;
    const std::uint64_t before = simulation.game_time_ms();
    simulation.set_speed(1);
    simulation.advance(500, [&] { return ++asked <= 3; });
    CHECK(simulation.game_time_ms() == before + 3 * sim::Simulation::kTickMs);
    CHECK(simulation.behind());
    simulation.advance(20);  // 7 ticks owed and 20 ms more
    CHECK(simulation.game_time_ms() == before + 10 * sim::Simulation::kTickMs);
    CHECK(std::abs(simulation.tick_fraction() - 0.4) < 1e-9);
}

TEST_CASE("sim: pool with stable handles") {
    sim::Pool<int> pool;
    const sim::Handle a = pool.add(1);
    const sim::Handle b = pool.add(2);
    const sim::Handle c = pool.add(3);
    CHECK(pool.size() == 3);
    pool.remove(b);
    CHECK(!pool.contains(b));
    CHECK(pool.get(b) == nullptr);
    const sim::Handle d = pool.add(4);  // reuses b's slot with a new generation
    CHECK(d.index == b.index);
    CHECK(!(d == b));
    CHECK(pool.get(b) == nullptr);
    CHECK(*pool.get(d) == 4);
    std::vector<int> order;
    pool.for_each([&](sim::Handle, int v) { order.push_back(v); });
    CHECK((order == std::vector<int>{1, 4, 3}));
    pool.remove(a);
    pool.remove(c);
    CHECK(pool.add(5).index == a.index);  // lowest free slot first
    CHECK(pool.size() == 2);
}

TEST_CASE("simulation: the switches of the production window") {
    const auto buildings = make_buildings();
    sim::World world;
    sim::WorldIsland island{assets::IslandHeader{}, sim::IslandMap(8, 8), 5, 5};
    REQUIRE(island.map.place(*buildings.by_id(20503), 1, 1, 0, 0, 0).ok());
    REQUIRE(world.add_island(std::move(island)).ok());
    sim::Production p = make(0, 1, 1, false, 0, 3, 0);
    p.countdown = 7;
    p.elapsed = 20;
    world.set_production(p);
    const assets::FigureTable no_figures;
    sim::Simulation simulation(world, buildings, no_figures);

    sim::ProductionSwitch sw;
    sw.player = 0;
    sw.island = 0;
    sw.x = 1;
    sw.y = 1;
    sw.on = true;
    CHECK(simulation.execute(sw).accepted);
    const sim::Production& now = world.productions()[0];
    CHECK(now.enabled);
    // An idle building starts in the next second; its waiting does not count.
    CHECK(now.countdown == 0);
    CHECK(now.elapsed == 13);
    CHECK(now.idle_cycles == 0);
    CHECK(!simulation.execute(sw).accepted);  // already on

    sw.op = sim::ProductionSwitch::Op::Pickup;
    sw.on = false;
    CHECK(simulation.execute(sw).accepted);
    CHECK(world.productions()[0].no_pickup());
    CHECK(!simulation.execute(sw).accepted);
    sw.on = true;
    CHECK(simulation.execute(sw).accepted);
    CHECK(!world.productions()[0].no_pickup());
    // Somebody else's building: refused.
    sw.player = 1;
    sw.on = false;
    CHECK(!simulation.execute(sw).accepted);
}

TEST_CASE("production: a known deposit shows on an island the player has not discovered, others do not") {
    // With the island resources known from the start the deposits of every island are known
    // before a ship has come near; their symbols show, the island's buildings still do not.
    const auto buildings = make_buildings();
    const auto figures = make_figures();
    sim::World world;
    world.set_players({sim::Player{}, sim::Player{sim::PlayerRole::Computer, 1, 0, ""}});
    sim::WorldIsland island{assets::IslandHeader{}, sim::IslandMap(8, 8), 5, 5};
    assets::IslandHeader::Deposit ore;
    ore.ware = assets::ware::kOre;
    ore.x = 2;
    ore.y = 3;
    island.header.deposits.push_back(ore);
    REQUIRE(world.add_island(std::move(island)).ok());

    const auto effekte = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(40, {6, 8}));
    const auto palette = test::make_palette();
    const assets::SpriteSheet* sheets[] = {&effekte};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 256);
    REQUIRE(atlas.ok());
    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Effekte)] = atlas->sheet_base(0);
    layers.local_player = 0;
    const render::WorldView view{render::Zoom::Large, 0};
    render::FigureSprites s;

    render::BuildingFigure symbol;
    symbol.role = render::BuildingFigureRole::Deposit;
    symbol.figure = 46;  // any figure of the table: the symbol's own is not what is tested
    symbol.island = 0;
    symbol.origin_x = 2;
    symbol.origin_y = 3;
    render::BuildingFigure icon = symbol;  // a building's icon on the same island
    icon.role = render::BuildingFigureRole::Status;
    REQUIRE(!world.islands()[0].discovered_by(0));
    CHECK(!render::building_figure_sprites(symbol, world, buildings, view, layers, 0, s));  // not known
    world.explore_islands();
    CHECK(render::building_figure_sprites(symbol, world, buildings, view, layers, 0, s));
    CHECK(!render::building_figure_sprites(icon, world, buildings, view, layers, 0, s));  // still hidden
}
