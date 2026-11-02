// SPDX-License-Identifier: GPL-3.0-or-later
// The texts over production buildings: "Lager: N" and "Lager voll".

#include "../test_framework.hpp"
#include "stock_notes.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kWeaver = 20507;

/// Ground and a 2 x 1 workshop.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: +1\n  Id: 20507\n  Gfx: 10\n  Size: 2, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 14\n    Prodmenge: 1\n    Interval: 20\n"
        "  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

const assets::TextTable& texts() {
    static const assets::TextTable t = assets::TextTable::parse(
        "[GAME]\nLager: \nleerleerleer \n%d%% Steuer:\nPioniere\nSiedler\nBuerger\nKaufleute\nAristokraten\n"
        "Nahrung\nLeer\nPreis:\nAuslastung:\nFrachtraum:\nKanonen:\nGeschwindigkeit:\nEinwohner:\nNamenlos\n"
        "Lager voll\nBetriebs\n[END]\n");
    return t;
}

/// An island at (10, 20) with a weaver at (3, 4) of city 0 owned by `owner`, and a second one at
/// (6, 4) turned by 1 (1 x 2 then). Productions 0 and 1.
struct Town {
    assets::BuildingTable buildings = make_buildings();
    sim::World world;

    explicit Town(sim::PlayerId owner) {
        sim::IslandMap map{12, 8};
        for (std::int32_t y = 0; y < 8; ++y) {
            for (std::int32_t x = 0; x < 12; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0, owner, 0).ok());
            }
        }
        REQUIRE(map.place(*buildings.by_id(kWeaver), 3, 4, 0, 0, owner, 0).ok());
        REQUIRE(map.place(*buildings.by_id(kWeaver), 6, 4, 1, 0, owner, 0).ok());
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 20}).ok());
        sim::City city;
        city.island = 0;
        city.index = 0;
        city.owner = owner;
        world.add_city(city);
        for (const std::uint16_t x : {std::uint16_t{3}, std::uint16_t{6}}) {
            sim::Production p;
            p.island = 0;
            p.x = x;
            p.y = 4;
            world.set_production(p);
        }
    }
};

sim::ProductionEvent event(sim::ProductionEvent::Kind kind, std::uint32_t production, std::uint16_t amount = 0) {
    return sim::ProductionEvent{kind, production, amount, 14};
}

}  // namespace

TEST_CASE("stock notes: the texts come from Text.cod [GAME] 0 and 17") {
    using Kind = sim::ProductionEvent::Kind;
    CHECK(app::stock_note_text(texts(), event(Kind::Produced, 0, 3)) == "Lager: 3");
    CHECK(app::stock_note_text(texts(), event(Kind::StoreFull, 0)) == "Lager voll");
    CHECK(app::stock_note_text(texts(), event(Kind::Started, 0)).empty());
    CHECK(app::stock_note_text(texts(), event(Kind::NeedsRawGoods, 0)).empty());
    // Without the section (another Text.cod) nothing shows.
    CHECK(app::stock_note_text(assets::TextTable::parse(""), event(Kind::StoreFull, 0)).empty());
}

TEST_CASE("stock notes: over the centre of the local player's buildings, rising for 1.25 s") {
    using Kind = sim::ProductionEvent::Kind;
    Town town(0);
    app::StockNotes notes;
    const std::vector<sim::ProductionEvent> events = {event(Kind::Started, 0), event(Kind::Produced, 0, 1),
                                                      event(Kind::StoreFull, 1)};
    notes.add(town.world, town.buildings, texts(), events, 0, 1000.0);
    REQUIRE(notes.notes().size() == 2);
    const app::StockNote& a = notes.notes()[0];
    CHECK(a.text == "Lager: 1");
    CHECK(a.x == 10 + 3 + 1.0);  // 2 x 1: the middle of its two tiles
    CHECK(a.y == 20 + 4 + 0.5);
    const app::StockNote& b = notes.notes()[1];
    CHECK(b.text == "Lager voll");
    CHECK(b.x == 10 + 6 + 0.5);  // turned: 1 x 2
    CHECK(b.y == 20 + 4 + 1.0);

    // From 0.5 up by 2 half tile widths a second (0.04 per 20 ms step), gone after 2.5 of them.
    CHECK(a.height(1000.0) == app::kStockNoteStartHeight);
    CHECK(a.height(1500.0) == 1.5);
    notes.expire(2249.0);
    CHECK(notes.notes().size() == 2);
    notes.expire(2250.0);
    CHECK(notes.notes().empty());
}

TEST_CASE("stock notes: a cycle that fills the store says only \"Lager voll\"") {
    using Kind = sim::ProductionEvent::Kind;
    Town town(0);
    app::StockNotes notes;
    // finish_cycle: the new ton, then the full store of the same cycle.
    notes.add(town.world, town.buildings, texts(),
              std::vector<sim::ProductionEvent>{event(Kind::Produced, 0, 4), event(Kind::StoreFull, 0),
                                                event(Kind::Stopped, 0)},
              0, 0.0);
    REQUIRE(notes.notes().size() == 1);
    CHECK(notes.notes()[0].text == "Lager voll");
    // A ton of one building and the full store of another stay two texts.
    notes.clear();
    notes.add(town.world, town.buildings, texts(),
              std::vector<sim::ProductionEvent>{event(Kind::Produced, 0, 2), event(Kind::StoreFull, 1)}, 0, 0.0);
    REQUIRE(notes.notes().size() == 2);
    CHECK(notes.notes()[0].text == "Lager: 2");
    CHECK(notes.notes()[1].text == "Lager voll");
}

TEST_CASE("stock notes: nothing for other players' cities, nor for a spectator") {
    using Kind = sim::ProductionEvent::Kind;
    Town town(1);
    app::StockNotes notes;
    const std::vector<sim::ProductionEvent> events = {event(Kind::Produced, 0, 1), event(Kind::StoreFull, 1),
                                                      event(Kind::StoreFull, 7)};
    notes.add(town.world, town.buildings, texts(), events, 0, 0.0);
    CHECK(notes.notes().empty());
    notes.add(town.world, town.buildings, texts(), events, sim::kNoPlayer, 0.0);
    CHECK(notes.notes().empty());
    notes.add(town.world, town.buildings, texts(), events, 1, 0.0);
    CHECK(notes.notes().size() == 2);  // production 7 does not exist
}

TEST_CASE("stock notes: at most 176 at a time, like the original's slots") {
    using Kind = sim::ProductionEvent::Kind;
    Town town(0);
    app::StockNotes notes;
    const std::vector<sim::ProductionEvent> events(200, event(Kind::StoreFull, 0));
    notes.add(town.world, town.buildings, texts(), events, 0, 0.0);
    CHECK(notes.notes().size() == app::kMaxStockNotes);
}

TEST_CASE("stock notes: the line's bottom is the projected point minus the height") {
    render::WorldView view;
    view.zoom = render::Zoom::Large;
    view.rotation = 0;
    const auto [x0, y0] = render::text_figure_point(view, 100.5, 50.5, 0.0);
    const auto [x1, y1] = render::text_figure_point(view, 100.5, 50.5, 0.5);
    const auto [x2, y2] = render::text_figure_point(view, 100.5, 50.5, 3.0);
    CHECK(x1 == x0);
    CHECK(y0 - y1 == 16);  // 0.5 half tile widths of 64 pixels
    CHECK(y0 - y2 == 96);
    CHECK(x2 == x0);
}

TEST_CASE("stock notes: the gold of a treasure rises over the builder's building") {
    Town town(0);
    sim::ConstructionEvent found;
    found.kind = sim::ConstructionEvent::Kind::TreasureFound;
    found.island = 0;
    found.x = 6;  // the turned weaver, 1 x 2
    found.y = 4;
    found.player = 0;
    found.treasure = sim::TilePos{6, 5};
    found.money = 1000;
    sim::ConstructionEvent placed = found;
    placed.kind = sim::ConstructionEvent::Kind::Placed;
    sim::ConstructionEvent rival = found;
    rival.player = 1;

    app::StockNotes notes;
    const sim::ConstructionEvent events[] = {placed, rival, found};
    notes.add_treasures(town.world, town.buildings, events, 0, 100.0);
    REQUIRE(notes.notes().size() == 1);
    CHECK(notes.notes()[0].text == "+1000");
    CHECK(notes.notes()[0].x == 16.5);
    CHECK(notes.notes()[0].y == 25.0);
    CHECK(notes.notes()[0].start_ms == 100.0);
    notes.add_treasures(town.world, town.buildings, events, sim::kNoPlayer, 100.0);
    CHECK(notes.notes().size() == 1);
}
