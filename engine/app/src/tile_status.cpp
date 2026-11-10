// SPDX-License-Identifier: GPL-3.0-or-later
#include "tile_status.hpp"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "event_presenter.hpp"

#include "o1602/sim/game_data.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/text_format.hpp"

namespace o1602::app {

namespace {

// [GAME] lines.
constexpr std::size_t kGameSuited = 22;    // "Geeignet für: %s: %d%% - ..."
constexpr std::size_t kGameWithered = 37;  // "Verdorrtes Feld"
// [LEISTE] lines of the units.
constexpr std::size_t kLeisteShip = 224;
constexpr std::size_t kLeistePirateShip = 225;
constexpr std::size_t kLeisteTraderShip = 226;
constexpr std::size_t kLeisteNativeWarrior = 227;
// [LANDSCHAFT] line of the natives' houses on a pirate island.
constexpr std::size_t kLandscapeNativeHut = 2;

/// The list the original keeps after the build menu's (0x00498610), named from [LANDSCHAFT].
constexpr auto kLandscapeIds = sim::id::kLandscape;

std::string line(const assets::TextTable& texts, std::string_view section, std::size_t i) {
    const std::vector<std::string>& lines = texts.section(section);
    return i < lines.size() ? lines[i] : std::string();
}

std::string name_of(const assets::BuildingType* b, const assets::TextTable& texts) {
    return b != nullptr ? menu_name(b->id, texts) : std::string();
}

/// "Geeignet für: ..." with the climate's three crops (Text.cod GAME 22).
std::string crops_line(const sim::WorldIsland& island, const assets::TextTable& texts) {
    const std::vector<std::string>& raw = texts.section("ROHST");
    const auto crop = [&](std::uint8_t ware) {
        const std::size_t i = std::size_t{ware} - sim::ware::kCornField;  // [ROHST] starts with grain
        return i < raw.size() ? std::string_view(raw[i]) : std::string_view();
    };
    const auto crops = island_crops(island);
    const std::string pattern = line(texts, "GAME", kGameSuited);
    if (pattern.empty()) {
        return {};
    }
    return ui::format_text(pattern,
                           {crop(crops[0].first), std::int64_t{crops[0].second}, crop(crops[1].first),
                            std::int64_t{crops[1].second}, crop(crops[2].first), std::int64_t{crops[2].second}});
}

/// A raw material on the map (Kind ROHSTOFF, or the ripe record after a growing one).
std::string raw_material(const assets::BuildingType& ripe, const sim::WorldIsland& island, sim::PlayerId local,
                         const assets::TextTable& texts) {
    const std::uint8_t w = ripe.ware;
    if (w == sim::ware::kGrass || w == sim::ware::kTree) {
        // Only who has explored the island learns what it grows; nothing to say otherwise.
        return island.explored_by(local) && island.header.climate < sim::climate::kCount ? crops_line(island, texts)
                                                                                         : std::string();
    }
    if (w == 0) {
        // The record after a withered field is the next crop's growing one, without a ware.
        return line(texts, "GAME", kGameWithered);
    }
    // [ROHSTFELD]: Getreidefeld ... Erze, Wild, Fischgründe.
    return w >= sim::ware::kCornField ? line(texts, "ROHSTFELD", std::size_t{w} - sim::ware::kCornField)
                                      : std::string();
}

bool named_by_kind(const assets::BuildingType& b) {
    return b.kind == sim::kind::kRoad || b.kind == sim::kind::kSquare || b.kind == sim::kind::kPier;
}

}  // namespace

std::string menu_name(std::uint32_t id, const assets::TextTable& texts) {
    if (const ui::BuildCategory* cat = ui::build_menu().category_of(id)) {
        return cat->name(cat->index_of(id).value_or(0), texts.section(cat->names));
    }
    for (std::size_t i = 0; i < kLandscapeIds.size(); ++i) {
        if (kLandscapeIds[i] == id) {
            return line(texts, "LANDSCHAFT", i);
        }
    }
    return {};
}

std::string unit_status(const sim::World& world, const ui::UnitRef& unit, const assets::FigureTable& figures,
                        const assets::TextTable& texts) {
    const auto behaviour = [&](sim::PlayerId owner) {
        const sim::Player* p = world.player(owner);
        return p != nullptr ? world.behaviour(*p) : sim::FactionBehaviour::Settlers;
    };
    if (unit.kind == ui::UnitRef::Kind::Ship) {
        const sim::Ship* s = world.ship(unit.id);
        if (s == nullptr) {
            return {};
        }
        switch (behaviour(s->owner)) {
            case sim::FactionBehaviour::Pirates:
                return line(texts, "LEISTE", kLeistePirateShip);
            case sim::FactionBehaviour::Trader:
                return line(texts, "LEISTE", kLeisteTraderShip);
            case sim::FactionBehaviour::Settlers:
            case sim::FactionBehaviour::Natives:
                break;
        }
        return line(texts, "LEISTE", kLeisteShip);
    }
    const sim::Soldier* s = world.soldier(unit.id);
    if (s == nullptr) {
        return {};
    }
    if (behaviour(s->owner) == sim::FactionBehaviour::Natives) {
        return line(texts, "LEISTE", kLeisteNativeWarrior);
    }
    // [FIGKIND] by the figure's kind (FIGUR record +4).
    const assets::FigureType* t = figures.get(s->figure);
    return t != nullptr && t->kind >= 0 ? line(texts, "FIGKIND", static_cast<std::size_t>(t->kind)) : std::string();
}

std::string tile_status(const sim::WorldIsland& island, std::int32_t x, std::int32_t y, sim::PlayerId local,
                        const assets::BuildingTable& buildings, const assets::TextTable& texts) {
    if (!island.map.contains(x, y)) {
        return {};
    }
    // Who has not discovered the island sees its terrain only (island +0xAFC instead of +0xAF8).
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    const sim::MapCell& cell = island.discovered_by(local) ? island.map.top(ux, uy) : island.map.ground(ux, uy);
    const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
    if (b == nullptr) {
        return {};
    }
    switch (b->prod_kind) {
        case sim::prod::kRipe:
            return raw_material(*b, island, local, texts);
        case sim::prod::kGrowing: {
            const assets::BuildingType* ripe = buildings.by_record(b->record + 1);
            return ripe != nullptr ? raw_material(*ripe, island, local, texts) : std::string();
        }
        case sim::prod::kHouse:
            return line(texts, "WOHNHAUS", 0);
        case sim::prod::kPirateHouse:
            return b->native ? line(texts, "LANDSCHAFT", kLandscapeNativeHut) : line(texts, "WOHNHAUS", 0);
        case sim::prod::kTower:
            // On the beach: the tower the player built (one record before).
            return name_of(b->kind == sim::kind::kTowerBeach ? buildings.by_record(b->record - 1) : b, texts);
        case sim::prod::kWall:
            // On the beach: the wall the player built (three records before).
            return name_of(b->kind == sim::kind::kWallBeach ? buildings.by_record(b->record - 3) : b, texts);
        case sim::prod::kOreMountain:
        case sim::prod::kHaupt:
            return named_by_kind(*b) ? name_of(b, texts) : std::string();
        default:
            if (b->prod_kind >= sim::prod::kWorkshop && b->prod_kind <= sim::prod::kWall) {
                return name_of(b, texts);  // the workshops, farms, public buildings, ...
            }
            return named_by_kind(*b) ? name_of(b, texts) : std::string();  // roads, squares, piers
    }
}

}  // namespace o1602::app
