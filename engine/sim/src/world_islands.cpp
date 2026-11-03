// SPDX-License-Identifier: GPL-3.0-or-later
//
// Islands changed by the editor (ADR 0013): moved, turned, copied and taken out. The simulation
// never does any of it; the editor keeps the world as it was for undo.
#include <algorithm>
#include <string>
#include <utility>

#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_generator.hpp"

namespace o1602::sim {

using core::ErrorCode;
using core::make_error;

namespace {

/// The footprint of the building whose origin is at (x, y) of `map`: its width and height as
/// placed, 1 x 1 where no building begins.
std::pair<std::uint32_t, std::uint32_t> footprint(const IslandMap& map, const assets::BuildingTable* buildings,
                                                  std::uint32_t x, std::uint32_t y) {
    if (buildings == nullptr || !map.contains(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y))) {
        return {1, 1};
    }
    const SubTile t = map.sub_tile(*buildings, x, y);
    if (t.building == nullptr) {
        return {1, 1};
    }
    const bool turned = (map.top(x, y).rotation & 1U) != 0;
    const std::uint32_t w = std::max<std::uint32_t>(1, t.building->size_x);
    const std::uint32_t h = std::max<std::uint32_t>(1, t.building->size_y);
    return turned ? std::pair{h, w} : std::pair{w, h};
}

const char* use_text(World::IslandUse use) {
    switch (use) {
        case World::IslandUse::None:
            return "";
        case World::IslandUse::Cities:
            return "a city lies on it";
        case World::IslandUse::Buildings:
            return "buildings of a game stand on it";
        case World::IslandUse::Units:
            return "units stand on it";
        case World::IslandUse::RunningGame:
            return "a game is running in the world";
    }
    return "";
}

}  // namespace

World::IslandUse World::island_use(std::size_t index) const {
    if (!m_simulation_state.empty()) {
        return IslandUse::RunningGame;
    }
    const auto on = [index](const auto& list) {
        return std::any_of(list.begin(), list.end(), [index](const auto& e) { return e.island == index; });
    };
    if (on(m_cities)) {
        return IslandUse::Cities;
    }
    if (on(m_houses) || on(m_cost_buildings) || on(m_disasters) || on(m_building_damage) || on(m_blueprints) ||
        on(m_blueprint_auto) || std::any_of(m_productions.begin(), m_productions.end(), [index](const Production& p) {
            return p.used && p.island == index;
        })) {
        return IslandUse::Buildings;
    }
    if (on(m_soldiers) || on(m_towers)) {
        return IslandUse::Units;
    }
    if (index < m_islands.size()) {
        const IslandMap& map = m_islands[index].map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                if (map.top(x, y).city != MapCell::kNoCity) {
                    return IslandUse::Cities;  // land of a city that is gone: a game's buildings
                }
            }
        }
    }
    return IslandUse::None;
}

core::Status World::place_islands(const assets::BuildingTable* buildings) {
    std::fill(m_cells.begin(), m_cells.end(), Cell{});
    std::vector<bool> taken;
    for (std::size_t i = 0; i < m_islands.size(); ++i) {
        const WorldIsland& island = m_islands[i];
        if (auto status = claim_cells(island, buildings, taken); !status) {
            return status;
        }
        const std::uint32_t w = island.map.width();
        for (std::uint32_t ly = 0; ly < island.map.height(); ++ly) {
            for (std::uint32_t lx = 0; lx < w; ++lx) {
                if (taken[std::size_t{ly} * w + lx]) {
                    m_cells[std::size_t{island.y + ly} * m_width + island.x + lx] = Cell{
                        static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(lx), static_cast<std::uint16_t>(ly)};
                }
            }
        }
    }
    return core::Ok{};
}

core::Status World::move_island(std::size_t index, std::uint32_t x, std::uint32_t y,
                                const assets::BuildingTable* buildings) {
    if (index >= m_islands.size()) {
        return make_error(ErrorCode::OutOfRange, "no island " + std::to_string(index));
    }
    if (const IslandUse use = island_use(index); use == IslandUse::Units || use == IslandUse::RunningGame) {
        return make_error(ErrorCode::Unsupported, std::string("the island cannot move: ") + use_text(use));
    }
    WorldIsland& island = m_islands[index];
    if (std::uint64_t{x} + island.map.width() > m_width || std::uint64_t{y} + island.map.height() > m_height) {
        return make_error(ErrorCode::OutOfRange, "the island would leave the world map");
    }
    const std::uint32_t old_x = island.x;
    const std::uint32_t old_y = island.y;
    const std::uint32_t w = island.map.width();
    const std::uint32_t h = island.map.height();
    // The ships in its rectangle (at its coast, in its bays) go with it.
    const auto inside = [&](const Ship& ship) {
        return ship.x >= old_x && ship.y >= old_y && ship.x < old_x + w && ship.y < old_y + h;
    };
    std::vector<bool> riding(m_ships.size(), false);
    for (std::size_t i = 0; i < m_ships.size(); ++i) {
        riding[i] = inside(m_ships[i]);
    }
    const std::vector<Cell> old_cells = m_cells;
    island.x = x;
    island.y = y;
    const auto undo = [&] {
        island.x = old_x;
        island.y = old_y;
        m_cells = old_cells;
    };
    if (auto status = place_islands(buildings); !status) {
        undo();
        return status;
    }
    // No other ship may end up on its land.
    for (std::size_t i = 0; i < m_ships.size(); ++i) {
        const Ship& ship = m_ships[i];
        if (riding[i] || !contains(ship.x, ship.y)) {
            continue;
        }
        const Cell& c = cell(ship.x, ship.y);
        if (c.island != index) {
            continue;
        }
        const MapCell& tile = island.map.top(c.local_x, c.local_y);
        const assets::BuildingType* b =
            buildings != nullptr && !tile.empty() ? buildings->by_sprite(tile.sprite) : nullptr;
        if (b == nullptr || !sailable_building(*b)) {
            undo();
            return make_error(ErrorCode::InvalidFormat, "the island would lie on a ship");
        }
    }
    const std::int64_t dx = std::int64_t{x} - old_x;
    const std::int64_t dy = std::int64_t{y} - old_y;
    constexpr std::int64_t kUnit = 4096;  // a ship's position is in 1/4096 tile
    // Those that go with it stay on water (another island's land may lie where its sea was).
    for (std::size_t i = 0; i < m_ships.size(); ++i) {
        if (!riding[i]) {
            continue;
        }
        const auto nx = static_cast<std::uint32_t>(m_ships[i].x + dx);
        const auto ny = static_cast<std::uint32_t>(m_ships[i].y + dy);
        const Cell& c = cell(nx, ny);
        if (c.island == kSea) {
            continue;
        }
        const MapCell& tile = m_islands[c.island].map.top(c.local_x, c.local_y);
        const assets::BuildingType* b =
            buildings != nullptr && !tile.empty() ? buildings->by_sprite(tile.sprite) : nullptr;
        if (b == nullptr || !sailable_building(*b)) {
            undo();
            return make_error(ErrorCode::InvalidFormat, "a ship of the island would lie on land");
        }
    }
    for (std::size_t i = 0; i < m_ships.size(); ++i) {
        if (!riding[i]) {
            continue;
        }
        Ship& ship = m_ships[i];
        ship.x = static_cast<std::uint16_t>(ship.x + dx);
        ship.y = static_cast<std::uint16_t>(ship.y + dy);
        ship.pos_x = static_cast<std::int32_t>(ship.pos_x + dx * kUnit);
        ship.pos_y = static_cast<std::int32_t>(ship.pos_y + dy * kUnit);
        // Its course led from the old place: it plans anew.
        ship.motion = ShipMotion{};
        ship.motion.from_x = ship.pos_x;
        ship.motion.from_y = ship.pos_y;
    }
    island.header.pos_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(x, 0xFFFF));
    island.header.pos_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(y, 0xFFFF));
    return core::Ok{};
}

core::Status World::rotate_island(std::size_t index, const assets::BuildingTable* buildings) {
    if (index >= m_islands.size()) {
        return make_error(ErrorCode::OutOfRange, "no island " + std::to_string(index));
    }
    if (const IslandUse use = island_use(index); use != IslandUse::None) {
        return make_error(ErrorCode::Unsupported, std::string("the island cannot turn: ") + use_text(use));
    }
    const WorldIsland& old = m_islands[index];
    const IslandMap& from = old.map;
    const std::uint32_t w = from.width();
    const std::uint32_t h = from.height();
    // A quarter clockwise: the tile (x, y) goes to (h - 1 - y, x), and every picture turns one step
    // (IslandMap::origin_of: rotation r + 1 lays a building's tiles out as the turn moves them).
    const auto turn = [h](std::uint32_t x, std::uint32_t y) {
        return std::pair<std::uint32_t, std::uint32_t>{h - 1 - y, x};
    };
    // The origin of a building of footprint fw x fh at (x, y) after the turn: the corner that
    // becomes the top left.
    const auto turn_origin = [&](std::uint32_t x, std::uint32_t y) {
        if (x >= w || y >= h) {
            return std::pair<std::uint32_t, std::uint32_t>{x, y};  // nothing of the island: as it was
        }
        const std::uint32_t fh = footprint(from, buildings, x, y).second;
        return std::pair<std::uint32_t, std::uint32_t>{h - y - fh, x};
    };
    WorldIsland island = old;
    island.map = IslandMap(h, w);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto [nx, ny] = turn(x, y);
            MapCell top = from.top(x, y);
            MapCell ground = from.ground(x, y);
            top.rotation = static_cast<std::uint8_t>((top.rotation + 1U) & 3U);
            ground.rotation = static_cast<std::uint8_t>((ground.rotation + 1U) & 3U);
            island.map.set_top_cell(nx, ny, top);
            island.map.set_ground_cell(nx, ny, ground);
        }
    }
    island.header.width = static_cast<std::uint16_t>(h);
    island.header.height = static_cast<std::uint16_t>(w);
    for (assets::IslandHeader::Deposit& d : island.header.deposits) {
        const auto [nx, ny] = turn_origin(d.x, d.y);
        d.x = static_cast<std::uint16_t>(nx);
        d.y = static_cast<std::uint16_t>(ny);
    }
    for (Crater& c : island.craters) {
        const auto [nx, ny] = turn_origin(c.x, c.y);
        c.x = static_cast<std::uint16_t>(nx);
        c.y = static_cast<std::uint16_t>(ny);
    }
    for (assets::IslandHeader::Crater& c : island.header.craters) {  // the copy loaded with the island
        const auto [nx, ny] = turn_origin(c.x, c.y);
        c.x = static_cast<std::uint8_t>(std::min<std::uint32_t>(nx, 0xFF));
        c.y = static_cast<std::uint8_t>(std::min<std::uint32_t>(ny, 0xFF));
    }
    for (DeerSpot& spot : island.deer_spots) {
        if (spot.used && spot.x < w && spot.y < h) {
            const auto [nx, ny] = turn(spot.x, spot.y);
            spot.x = static_cast<std::uint16_t>(nx);
            spot.y = static_cast<std::uint16_t>(ny);
        }
    }
    // About the same centre, inside the map.
    const auto centre = [](std::uint32_t pos, std::uint32_t side, std::uint32_t new_side, std::uint32_t room) {
        const std::int64_t want = std::int64_t{pos} + (std::int64_t{side} - std::int64_t{new_side}) / 2;
        return static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(want, 0, std::max<std::int64_t>(0, std::int64_t{room} - new_side)));
    };
    island.x = centre(old.x, w, h, m_width);
    island.y = centre(old.y, h, w, m_height);
    if (std::uint64_t{island.x} + h > m_width || std::uint64_t{island.y} + w > m_height) {
        return make_error(ErrorCode::OutOfRange, "the turned island does not fit on the world map");
    }
    island.header.pos_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(island.x, 0xFFFF));
    island.header.pos_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(island.y, 0xFFFF));

    // In place of the old one; the cells anew, and back as it was if it no longer fits.
    WorldIsland kept = std::move(m_islands[index]);
    m_islands[index] = std::move(island);
    const std::vector<Cell> old_cells = m_cells;
    if (auto status = place_islands(buildings); !status) {
        m_islands[index] = std::move(kept);
        m_cells = old_cells;
        return status;
    }
    for (Animal& a : m_animals) {
        if (a.island == index && a.x < w && a.y < h) {
            const auto [nx, ny] = turn(a.x, a.y);
            a.x = static_cast<std::uint16_t>(nx);
            a.y = static_cast<std::uint16_t>(ny);
        }
    }
    for (GrowthEntry& e : m_growth.entries) {
        if (e.island == index && e.x < w && e.y < h) {
            const auto [nx, ny] = turn(e.x, e.y);
            e.x = static_cast<std::uint16_t>(nx);
            e.y = static_cast<std::uint16_t>(ny);
        }
    }
    std::vector<Eruption> eruptions;
    for (const Eruption& e : m_volcanoes.eruptions) {
        if (e.crater.island != index) {
            eruptions.push_back(e);  // one of the island's would throw its lava from the old place
        }
    }
    m_volcanoes.eruptions = std::move(eruptions);
    std::erase_if(m_volcanoes.lava, [index](const LavaLump& l) { return l.island == index; });
    if (m_volcanoes.last && m_volcanoes.last->island == index) {
        m_volcanoes.last.reset();
    }
    return core::Ok{};
}

core::Status World::grow_island(std::size_t index, std::uint32_t left, std::uint32_t top, std::uint32_t right,
                                std::uint32_t bottom, const assets::BuildingType& sea,
                                const assets::BuildingTable* buildings) {
    if (index >= m_islands.size()) {
        return make_error(ErrorCode::OutOfRange, "no island " + std::to_string(index));
    }
    if (const IslandUse use = island_use(index); use != IslandUse::None) {
        return make_error(ErrorCode::Unsupported, std::string("the island cannot grow: ") + use_text(use));
    }
    const WorldIsland& old = m_islands[index];
    const std::uint32_t w = old.map.width();
    const std::uint32_t h = old.map.height();
    const std::uint64_t nw = std::uint64_t{w} + left + right;
    const std::uint64_t nh = std::uint64_t{h} + top + bottom;
    if (left > old.x || top > old.y || std::uint64_t{old.x} - left + nw > m_width ||
        std::uint64_t{old.y} - top + nh > m_height) {
        return make_error(ErrorCode::OutOfRange, "the island would leave the world map");
    }
    if (nw > kMaxIslandSide || nh > kMaxIslandSide) {
        return make_error(ErrorCode::OutOfRange, "the island would grow too large");
    }
    WorldIsland island = old;
    island.x = old.x - left;
    island.y = old.y - top;
    island.map = IslandMap(static_cast<std::uint32_t>(nw), static_cast<std::uint32_t>(nh));
    const std::uint32_t phases = std::max<std::uint32_t>(sea.anim_count, 1);
    for (std::uint32_t y = 0; y < nh; ++y) {
        for (std::uint32_t x = 0; x < nw; ++x) {
            MapCell top_cell;
            MapCell ground;
            if (x >= left && y >= top && x - left < w && y - top < h) {
                top_cell = old.map.top(x - left, y - top);
                ground = old.map.ground(x - left, y - top);
            } else {
                // The world's open sea goes on: its phase is that of its place (as the generator's).
                top_cell.sprite = static_cast<std::uint16_t>(sea.gfx);
                top_cell.anim_phase = static_cast<std::uint8_t>((3U * (island.x + x) + island.y + y) % phases & 0xFU);
                ground = top_cell;
            }
            island.map.set_top_cell(x, y, top_cell);
            island.map.set_ground_cell(x, y, ground);
        }
    }
    island.header.width = static_cast<std::uint16_t>(nw);
    island.header.height = static_cast<std::uint16_t>(nh);
    island.header.pos_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(island.x, 0xFFFF));
    island.header.pos_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(island.y, 0xFFFF));
    for (assets::IslandHeader::Deposit& d : island.header.deposits) {
        d.x = static_cast<std::uint16_t>(d.x + left);
        d.y = static_cast<std::uint16_t>(d.y + top);
    }
    for (Crater& c : island.craters) {
        c.x = static_cast<std::uint16_t>(c.x + left);
        c.y = static_cast<std::uint16_t>(c.y + top);
    }
    for (assets::IslandHeader::Crater& c : island.header.craters) {  // the copy loaded with the island
        c.x = static_cast<std::uint8_t>(std::min<std::uint32_t>(c.x + left, 0xFF));
        c.y = static_cast<std::uint8_t>(std::min<std::uint32_t>(c.y + top, 0xFF));
    }
    for (DeerSpot& spot : island.deer_spots) {
        if (spot.used) {
            spot.x = static_cast<std::uint16_t>(spot.x + left);
            spot.y = static_cast<std::uint16_t>(spot.y + top);
        }
    }
    WorldIsland kept = std::move(m_islands[index]);
    m_islands[index] = std::move(island);
    const std::vector<Cell> old_cells = m_cells;
    if (auto status = place_islands(buildings); !status) {
        m_islands[index] = std::move(kept);
        m_cells = old_cells;
        return status;
    }
    for (Animal& a : m_animals) {
        if (a.island == index) {
            a.x = static_cast<std::uint16_t>(a.x + left);
            a.y = static_cast<std::uint16_t>(a.y + top);
        }
    }
    for (GrowthEntry& e : m_growth.entries) {
        if (e.island == index) {
            e.x = static_cast<std::uint16_t>(e.x + left);
            e.y = static_cast<std::uint16_t>(e.y + top);
        }
    }
    const auto shift = [&](CraterRef& c) {
        if (c.island == index) {
            c.x = static_cast<std::uint16_t>(c.x + left);
            c.y = static_cast<std::uint16_t>(c.y + top);
        }
    };
    for (Eruption& e : m_volcanoes.eruptions) {
        shift(e.crater);
    }
    if (m_volcanoes.last) {
        shift(*m_volcanoes.last);
    }
    std::erase_if(m_volcanoes.lava, [index](const LavaLump& l) { return l.island == index; });
    return core::Ok{};
}

core::Status World::copy_island(std::size_t index, std::uint32_t x, std::uint32_t y, std::uint16_t number,
                                const assets::BuildingTable* buildings) {
    if (index >= m_islands.size()) {
        return make_error(ErrorCode::OutOfRange, "no island " + std::to_string(index));
    }
    if (const IslandUse use = island_use(index); use != IslandUse::None) {
        return make_error(ErrorCode::Unsupported, std::string("the island cannot be copied: ") + use_text(use));
    }
    if (std::any_of(m_islands.begin(), m_islands.end(),
                    [number](const WorldIsland& i) { return i.header.number == number; })) {
        return make_error(ErrorCode::InvalidFormat, "an island has the number " + std::to_string(number));
    }
    WorldIsland copy = m_islands[index];
    copy.header.number = number;
    copy.x = x;
    copy.y = y;
    copy.header.pos_x = static_cast<std::uint16_t>(std::min<std::uint32_t>(x, 0xFFFF));
    copy.header.pos_y = static_cast<std::uint16_t>(std::min<std::uint32_t>(y, 0xFFFF));
    copy.header.explored = 0;
    copy.header.discovered = 0;
    for (assets::IslandHeader::Deposit& d : copy.header.deposits) {
        d.mask = 0;
    }
    copy.discovered.clear();
    copy.explored.clear();
    copy.drought = {};
    copy.volcano_erupted = false;
    for (Crater& c : copy.craters) {
        c.steps_left = 0;
    }
    // The copy's tiles look as the original's: the same blocks keep their revisions, which say
    // that the tiles are the same (IslandMap::block_revision()).
    const auto new_index = static_cast<std::uint16_t>(m_islands.size());
    if (auto status = add_island(std::move(copy), buildings); !status) {
        return status;
    }
    const std::size_t animals = m_animals.size();
    for (std::size_t i = 0; i < animals; ++i) {
        if (m_animals[i].island == index) {
            Animal a = m_animals[i];
            a.island = new_index;
            m_animals.push_back(a);
        }
    }
    const std::size_t growing = m_growth.entries.size();
    for (std::size_t i = 0; i < growing; ++i) {
        if (m_growth.entries[i].island == index) {
            GrowthEntry e = m_growth.entries[i];
            e.island = new_index;
            m_growth.entries.push_back(e);
        }
    }
    return core::Ok{};
}

core::Status World::remove_island(std::size_t index, const assets::BuildingTable* buildings) {
    if (index >= m_islands.size()) {
        return make_error(ErrorCode::OutOfRange, "no island " + std::to_string(index));
    }
    if (const IslandUse use = island_use(index); use != IslandUse::None) {
        return make_error(ErrorCode::Unsupported, std::string("the island cannot be taken out: ") + use_text(use));
    }
    const auto i = static_cast<std::uint16_t>(index);
    // Names an island after the one taken out: one place forward.
    const auto shift = [i](std::uint16_t& island) {
        if (island != kSea && island > i) {
            --island;
        }
    };
    const auto shift_target = [&](Target& t) {
        if (t.kind == Target::Kind::City || t.kind == Target::Kind::Building) {
            shift(t.island);
        }
    };
    const auto shift_all = [&](auto& list) {
        for (auto& e : list) {
            shift(e.island);
        }
    };
    m_islands.erase(m_islands.begin() + static_cast<std::ptrdiff_t>(index));
    if (auto status = place_islands(buildings); !status) {
        return status;  // cannot be: the islands left were placed before
    }
    std::erase_if(m_animals, [i](const Animal& a) { return a.island == i; });
    std::erase_if(m_growth.entries, [i](const GrowthEntry& e) { return e.island == i; });
    std::erase_if(m_volcanoes.eruptions, [i](const Eruption& e) { return e.crater.island == i; });
    std::erase_if(m_volcanoes.lava, [i](const LavaLump& l) { return l.island == i; });
    if (m_volcanoes.last && m_volcanoes.last->island == i) {
        m_volcanoes.last.reset();
    }
    shift_all(m_cities);
    index_cities();
    shift_all(m_animals);
    shift_all(m_growth.entries);
    shift_all(m_towers);
    shift_all(m_building_damage);
    shift_all(m_disasters);
    shift_all(m_cost_buildings);
    shift_all(m_blueprints);
    shift_all(m_blueprint_auto);
    shift_all(m_volcanoes.lava);
    for (Eruption& e : m_volcanoes.eruptions) {
        shift(e.crater.island);
    }
    if (m_volcanoes.last) {
        shift(m_volcanoes.last->island);
    }
    std::vector<House> houses = std::move(m_houses);
    shift_all(houses);
    set_houses(std::move(houses));
    std::vector<Production> productions = std::move(m_productions);
    shift_all(productions);
    set_productions(std::move(productions), std::move(m_free_productions));
    for (Soldier& s : m_soldiers) {
        shift(s.island);
        shift_target(s.home);
        shift_target(s.target);
        for (Target& t : s.patrol.points) {
            shift_target(t);
        }
        for (Target& t : s.queue) {
            shift_target(t);
        }
    }
    for (PendingHit& hit : m_hits) {
        shift_target(hit.shooter);
        shift_target(hit.target);
    }
    for (Ship& ship : m_ships) {
        for (RouteStop& stop : ship.route.stops) {
            shift_target(stop.target);
        }
        shift_target(ship.target);
        shift_target(ship.order);
        shift_target(ship.previous);
        for (Target& t : ship.queue) {
            shift_target(t);
        }
        if (ship.near_island == i) {
            ship.near_island = kSea;
        }
        shift(ship.near_island);
    }
    for (Mission& mission : m_missions) {
        for (Mission::IslandGoal& goal : mission.islands) {
            std::erase(goal.islands, i);
            for (std::uint16_t& island : goal.islands) {
                shift(island);
            }
        }
    }
    return core::Ok{};
}

core::Status World::resize(std::uint32_t width, std::uint32_t height, const assets::BuildingTable* buildings) {
    if (width == 0 || height == 0 || width > kMaxSide || height > kMaxSide) {
        return make_error(ErrorCode::OutOfRange,
                          "a map of " + std::to_string(width) + " x " + std::to_string(height) + " tiles");
    }
    if (!m_simulation_state.empty()) {
        return make_error(ErrorCode::InvalidFormat, "the size of a running game's map stays");
    }
    const auto fits = [&](std::uint64_t x, std::uint64_t y, std::uint64_t w, std::uint64_t h) {
        return x + w <= width && y + h <= height;
    };
    for (const WorldIsland& island : m_islands) {
        if (!fits(island.x, island.y, island.map.width(), island.map.height())) {
            return make_error(ErrorCode::OutOfRange, "an island would lie outside the map");
        }
    }
    for (const Ship& ship : m_ships) {
        if (!fits(ship.x, ship.y, 1, 1)) {
            return make_error(ErrorCode::OutOfRange, "a ship would lie outside the map");
        }
    }
    for (const Soldier& soldier : m_soldiers) {
        if (soldier.pos_x < 0 || soldier.pos_y < 0 ||
            !fits(static_cast<std::uint64_t>(soldier.pos_x) / 4096, static_cast<std::uint64_t>(soldier.pos_y) / 4096, 1,
                  1)) {
            return make_error(ErrorCode::OutOfRange, "a land unit would stand outside the map");
        }
    }
    for (const ScenarioMarker& marker : m_markers) {
        if (!fits(marker.x, marker.y, marker.width, marker.height)) {
            return make_error(ErrorCode::OutOfRange, "the marker " + marker.name + " would lie outside the map");
        }
    }
    if (m_planned_islands && !check_planned_islands(*m_planned_islands, width, height).empty()) {
        return make_error(ErrorCode::OutOfRange, "an island of the world plan would lie outside the map");
    }
    m_width = width;
    m_height = height;
    m_cells.assign(std::size_t{width} * height, Cell{});
    return place_islands(buildings);
}

std::optional<std::uint16_t> World::free_island_number() const {
    std::vector<bool> used(std::size_t{0xFFFF} + 1, false);
    for (const WorldIsland& island : m_islands) {
        used[island.header.number] = true;
    }
    for (std::size_t n = 0; n < used.size(); ++n) {
        if (!used[n]) {
            return static_cast<std::uint16_t>(n);
        }
    }
    return std::nullopt;
}

}  // namespace o1602::sim
