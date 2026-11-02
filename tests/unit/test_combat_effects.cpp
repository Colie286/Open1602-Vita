// SPDX-License-Identifier: GPL-3.0-or-later
// Hits, cannon smoke and wrecks: pictures made from the events of
// the simulation.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../test_framework.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/render/combat_effects.hpp"
#include "o1602/render/sprite_atlas.hpp"

using namespace o1602;

namespace {

constexpr std::uint16_t kShip = 21;
constexpr std::uint16_t kShipSmoke = 30;
constexpr std::uint16_t kHit = 31;
constexpr std::uint16_t kTowerSmoke = 32;
constexpr std::uint16_t kTower = 40;

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Posoffs: -19, 34\n  Fahnoffs: 0, 0, 2.0\n"
        "  Shotfignr: 30\n  Hitfignr: 31\n"
        "  Nummer: 22\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Hitfignr: 65535\n"
        "  Nummer: 30\n  Gfx: 8\n  Blocknr: 2\n  Rotate: 2\n  Worktime: 0.12\n  Posoffs: 34, 34\n"
        "  Fahnoffs: 0.5, 0, 4.0\n"
        "  Objekt: ANIM\n    Nummer: 0\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 2\n    AnimSpeed: 60\n"
        "  EndObj\n"
        "  Nummer: 31\n  Gfx: 0\n  Rotate: 0\n  Worktime: 0.1\n  Posoffs: -17, 36\n  Fahnoffs: 0, 0, 0.3\n"
        "  Objekt: ANIM\n    Nummer: 0\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 23\n    AnimSpeed: 50\n"
        "  EndObj\n"
        "  Nummer: 32\n  Gfx: 8\n  Blocknr: 2\n  Rotate: 2\n  Worktime: 0.96\n  Fahnoffs: 0.5, 0, 2.0\n"
        "  Objekt: ANIM\n    Nummer: 0\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 16\n    AnimSpeed: 60\n"
        "  EndObj\n"
        "  Nummer: 40\n  Gfx: 0\n  Shotfignr: 32\n  Hitfignr: 31\n"
        // SHIPSUNK as figuren.cod has it.
        "  Nummer: 67\n  Gfx: 32\n  Rotate: 0\n  Worktime: 1.12\n  Posoffs: 35, -20\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 140\n"
        "    Nummer: 1\n    AnimOffs: 8\n    AnimAdd: 1\n    AnimAnz: 7\n    AnimSpeed: 140\n    AnimRept: 20\n"
        "    Nummer: 2\n    AnimOffs: 15\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 140\n"
        "    Nummer: 3\n    AnimOffs: 22\n    AnimAdd: 1\n    AnimAnz: 1\n    AnimSpeed: 500\n"
        "  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

void put_chunk(std::vector<std::uint8_t>& file, const char* name, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> header(16, 0);
    std::memcpy(header.data(), name, std::strlen(name));
    file.insert(file.end(), header.begin(), header.end());
    put_u32(file, static_cast<std::uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
}

/// BSH with solid sprites of the given sizes (palette index 1).
assets::SpriteSheet make_sheet(const std::vector<std::pair<std::uint32_t, std::uint32_t>>& sizes) {
    std::vector<std::vector<std::uint8_t>> records;
    for (const auto& [w, h] : sizes) {
        std::vector<std::uint8_t> rle;
        for (std::uint32_t y = 0; y < h; ++y) {
            rle.push_back(0);
            rle.push_back(static_cast<std::uint8_t>(w));
            rle.insert(rle.end(), w, 1);
            rle.push_back(y + 1 == h ? 0xFF : 0xFE);
        }
        std::vector<std::uint8_t> rec;
        put_u32(rec, w);
        put_u32(rec, h);
        put_u32(rec, 1);
        put_u32(rec, static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3}));
        rec.insert(rec.end(), rle.begin(), rle.end());
        rec.resize((rec.size() + 3) & ~std::size_t{3}, 0);
        records.push_back(std::move(rec));
    }
    std::vector<std::uint8_t> payload;
    auto offset = static_cast<std::uint32_t>(4 * records.size());
    for (const auto& rec : records) {
        put_u32(payload, offset);
        offset += static_cast<std::uint32_t>(rec.size());
    }
    for (const auto& rec : records) {
        payload.insert(payload.end(), rec.begin(), rec.end());
    }
    std::vector<std::uint8_t> file;
    put_chunk(file, "BSH", payload);
    auto chunks = assets::ChunkFile::from_bytes(file);
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(chunks).value());
    return std::move(sheet).value();
}

assets::Palette make_palette() {
    std::vector<std::uint8_t> payload;
    for (int i = 0; i < 256; ++i) {
        payload.insert(payload.end(), {static_cast<std::uint8_t>(i), 0, 0, 0});
    }
    std::vector<std::uint8_t> file;
    put_chunk(file, "COL", payload);
    auto chunks = assets::ChunkFile::from_bytes(file);
    return std::move(assets::Palette::from_chunk_file(*chunks)).value();
}

const render::EffectFigure* find_effect(const render::CombatEffects& fx, std::uint16_t figure) {
    const auto& e = fx.effects();
    const auto it = std::find_if(e.begin(), e.end(), [&](const auto& f) { return f.figure == figure; });
    return it != e.end() ? &*it : nullptr;
}

sim::MilitaryEvent hit_on(sim::Target target, std::uint16_t figure) {
    sim::MilitaryEvent e;
    e.kind = sim::MilitaryEvent::Kind::Hit;
    e.target = target;
    e.x = 10 * sim::kShipUnit + sim::kShipUnit / 2;
    e.y = 20 * sim::kShipUnit;
    e.figure = figure;
    return e;
}

}  // namespace

TEST_CASE("combat effects: a hit plays the shooter's Hitfignr for its Worktime") {
    const auto figures = make_figures();
    const assets::BuildingTable buildings;
    sim::World world;
    world.set_time_ms(1000);
    render::CombatEffects fx;
    const sim::MilitaryEvent events[] = {hit_on(sim::Target::other_ship(1), kHit)};
    fx.military(world, buildings, figures, events);
    fx.frame(world, figures);
    const render::EffectFigure* hit = find_effect(fx, kHit);
    REQUIRE(hit != nullptr);
    CHECK(hit->x == 10.5);
    CHECK(hit->y == 20.0);
    CHECK(std::abs(hit->z - 0.3) < 1e-9);  // on a ship: its Fahnoffs[2] above the water
    CHECK(hit->sheet == assets::FigureSheet::Effekte);
    CHECK(hit->frame == std::optional<std::uint8_t>(0));
    world.set_time_ms(1050);
    fx.frame(world, figures);
    REQUIRE(find_effect(fx, kHit) != nullptr);
    CHECK(find_effect(fx, kHit)->frame == std::optional<std::uint8_t>(1));
    // Between two ticks it runs a tick behind, like the units.
    fx.frame(world, figures, 0.0);
    CHECK(find_effect(fx, kHit)->frame == std::optional<std::uint8_t>(0));
    world.set_time_ms(1100);  // Worktime 0.1 s: gone
    fx.frame(world, figures);
    CHECK(fx.effects().empty());
}

TEST_CASE("combat effects: hits on buildings are higher, swordsmen leave none") {
    const auto figures = make_figures();
    const assets::BuildingTable buildings;
    sim::World world;
    render::CombatEffects fx;
    const sim::MilitaryEvent events[] = {hit_on(sim::Target::building(0, 1, 1), kHit),
                                         hit_on(sim::Target::soldier(3), 65535), hit_on(sim::Target::soldier(3), 0)};
    fx.military(world, buildings, figures, events);
    fx.frame(world, figures);
    REQUIRE(fx.effects().size() == 1);
    CHECK(std::abs(fx.effects()[0].z - 1.3) < 1e-9);
}

TEST_CASE("combat effects: a tower's smoke stands out from its centre towards its aim") {
    const auto figures = make_figures();
    const assets::BuildingTable buildings;
    sim::World world;
    world.towers().push_back(sim::Tower{1, 3, 4, 0, kTower, 6});  // same tile, other island
    world.towers().push_back(sim::Tower{0, 3, 4, 0, kTower, 2});
    render::CombatEffects fx;
    sim::MilitaryEvent shot;
    shot.kind = sim::MilitaryEvent::Kind::Shot;
    shot.unit = sim::Target::building(0, 3, 4);
    shot.x = 8 * sim::kShipUnit;
    shot.y = 8 * sim::kShipUnit;
    shot.figure = kTower;
    // A soldier's shot has no smoke figure.
    sim::MilitaryEvent musket = shot;
    musket.unit = sim::Target::soldier(5);
    // A shot from a tile without a tower (it was torn down) has none either.
    sim::MilitaryEvent gone = shot;
    gone.unit = sim::Target::building(0, 4, 3);
    const sim::MilitaryEvent events[] = {gone, shot, musket};
    fx.military(world, buildings, figures, events);
    fx.frame(world, figures);
    REQUIRE(fx.effects().size() == 1);
    const render::EffectFigure& smoke = fx.effects()[0];
    CHECK(smoke.figure == kTowerSmoke);
    CHECK(std::abs(smoke.x - 8.5) < 1e-9);  // direction 2: Fahnoffs[0] along +x
    CHECK(std::abs(smoke.y - 8.0) < 1e-9);
    CHECK(std::abs(smoke.z - 2.0) < 1e-9);
    CHECK(smoke.direction == 2);
    CHECK(smoke.sheet == assets::FigureSheet::Ship);
    world.set_time_ms(960);
    fx.frame(world, figures);
    CHECK(fx.effects().empty());
}

TEST_CASE("combat effects: the wreck runs through its four animations in about 22 s") {
    const auto figures = make_figures();
    const assets::FigureType* wreck = figures.get(render::kFigureShipSunk);
    REQUIRE(wreck != nullptr);
    const auto at = [&](std::uint64_t ms) { return render::wreck_frame(*wreck, ms); };
    CHECK(at(0)->anim == 0);
    CHECK(at(1119)->anim == 0);
    CHECK(at(1119)->frame == 7);
    CHECK(at(1120)->anim == 1);  // 7 frames x 140 ms, 20 times: 19.6 s
    CHECK(at(1120 + 19599)->anim == 1);
    CHECK(at(1120 + 19599)->frame == 6);
    CHECK(at(20720)->anim == 2);
    CHECK(at(21840)->anim == 3);
    CHECK(at(22339)->anim == 3);
    CHECK(!at(22340));

    // It appears where a sinking ship disappears.
    const assets::BuildingTable buildings;
    sim::World world;
    world.add_ship(sim::make_ship("Patria", 10, 20, kShip, 0));
    world.ships().back().sinking = true;
    render::CombatEffects fx;
    fx.frame(world, figures);
    CHECK(fx.effects().empty());
    world.ships().clear();
    world.set_time_ms(500);
    fx.frame(world, figures);
    const render::EffectFigure* w = find_effect(fx, render::kFigureShipSunk);
    REQUIRE(w != nullptr);
    CHECK(w->x == 10.5);
    CHECK(w->y == 20.5);
    CHECK(w->sheet == assets::FigureSheet::Effekte);
    world.set_time_ms(500 + 22340);
    fx.frame(world, figures);
    CHECK(fx.effects().empty());
    fx.reset();
}

TEST_CASE("combat effects: a broadside's smoke goes with the ship to the side it fired to") {
    const auto figures = make_figures();
    sim::World world;
    world.set_players({sim::Player{sim::PlayerRole::Human, 0, 0, ""}});
    world.add_ship(sim::make_ship("Patria", 10, 20, kShip, 0, 2));
    const std::uint32_t id = world.ships().back().id;
    world.set_time_ms(1000);
    render::CombatEffects fx;
    // Heading 2 = bearing 0 + 2: fired to the right.
    sim::ShipEvent fired{sim::ShipEvent::Kind::Fired, id, 0};
    fired.ware = 0;
    const sim::ShipEvent events[] = {fired};
    fx.ships(world, figures, events);
    fx.frame(world, figures);
    REQUIRE(fx.ship_smoke().count(id) == 1);
    CHECK(fx.ship_smoke().at(id).figure == kShipSmoke);
    CHECK(fx.ship_smoke().at(id).side == 0);
    CHECK(fx.ship_smoke().at(id).frame == 0);

    // Sheets: terrain, then ships: 8 hulls 20x10, 8 directions of 2 smoke frames 4x6.
    const auto terrain = make_sheet({{4, 2}});
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ship_sizes(8, {20, 10});
    ship_sizes.insert(ship_sizes.end(), 16, {4, 6});
    const auto ships = make_sheet(ship_sizes);
    const auto palette = make_palette();
    const assets::SpriteSheet* sheets[] = {&terrain, &ships};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 256);
    REQUIRE(atlas.ok());
    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.ship_base = atlas->sheet_base(1);
    layers.ship_smoke = &fx.ship_smoke();
    const render::WorldView view{render::Zoom::Large, 0};
    render::ShipSprites s;
    REQUIRE(render::ship_sprites(world.ships().back(), world, view, layers, 0, s));
    REQUIRE(s.has_smoke);
    // Turned to the bearing (direction 0), frame 0.
    CHECK(s.smoke.sprite == layers.ship_base + 8);
    // Fahnoffs[0] = 0.5 across the ship (+x for heading 2): 16 px right, 8 px up; hanging from
    // Fahnoffs[2] = 4.0 * 32 above Y = 530, lowered by its Posoffs 34.
    CHECK(s.smoke.x == 340 * 32 + 16 - 2);
    CHECK(s.smoke.y == 488 + 34 + 34 - 128 - 1 + 6);
    CHECK(s.smoke_behind);  // the right side of a ship heading 2 faces away

    // The next frame of the smoke; from the left side it goes over the hull.
    world.set_time_ms(1060);
    fired.ware = 4;  // heading 2 = bearing 4 + 6
    const sim::ShipEvent left[] = {fired};
    fx.ships(world, figures, left);
    world.set_time_ms(1120);
    fx.frame(world, figures);
    CHECK(fx.ship_smoke().at(id).side == 1);
    CHECK(fx.ship_smoke().at(id).frame == 1);
    REQUIRE(render::ship_sprites(world.ships().back(), world, view, layers, 0, s));
    REQUIRE(s.has_smoke);
    CHECK(s.smoke.sprite == layers.ship_base + 8 + 4 * 2 + 1);
    CHECK(!s.smoke_behind);

    // Worktime 0.12 s after the shot it is gone.
    world.set_time_ms(1180);
    fx.frame(world, figures);
    CHECK(fx.ship_smoke().empty());
    REQUIRE(render::ship_sprites(world.ships().back(), world, view, layers, 0, s));
    CHECK(!s.has_smoke);
}
