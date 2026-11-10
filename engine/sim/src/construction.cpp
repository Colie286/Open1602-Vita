// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/construction.hpp"

#include <algorithm>
#include <array>
#include <set>

#include "o1602/assets/savegame.hpp"
#include "o1602/sim/disasters.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::sim {

namespace {

// Unlock levels with a count limit (record Bauinfra).
constexpr std::uint8_t kInfraCastle = infra::kCastle;
constexpr std::uint8_t kInfraCathedral = infra::kCathedral;
constexpr std::uint8_t kInfraArch = infra::kArch;
constexpr std::uint8_t kInfraMonument = infra::kMonument;
/// A castle or cathedral makes its city proud: city + 0x200 += 25 * 32 (command 0x4B of 0x004084D0).
constexpr std::uint32_t kPrideBonus = 25 * 32;

constexpr std::int32_t kShipRange = 16;       // founding from a ship: circle of radius 16
constexpr std::int32_t kRoadSearch = 4;       // doors turn to roads within 4 tiles
constexpr std::uint8_t kMinKontorRadius = 8;  // territory of a Kontor
constexpr std::uint8_t kTowerRadius = 8;      // towers protect the land within 8 tiles
/// Real buildings, which a demolition rectangle leaves standing (0x00464AE0): not roads, walls,
/// fields, trees or ruins.
constexpr std::array<std::int32_t, 8> kRealBuildings = {kind::kTower,      kind::kTowerBeach, kind::kBuilding,
                                                        kind::kBeachHouse, kind::kMine,       kind::kHq,
                                                        kind::kHarbour,    kind::kMill};
constexpr std::uint32_t kMeadowId = id::kMeadow;

bool is_beach(std::int32_t k) noexcept {
    return k >= kind::kBeach && k <= kind::kBeachVariant;
}

/// Coast a Kontor stands at (0x00467E10).
bool kontor_coast(std::int32_t k) noexcept {
    return is_beach(k) || k == kind::kBeachRuin || k == kind::kPier;
}
/// Coast inside a shipyard (0x00468090).
bool shipyard_coast(std::int32_t k) noexcept {
    return k == kind::kWallBeach || kontor_coast(k);
}

bool registers_production(std::int32_t p) noexcept {
    return (p >= prod::kWorkshop && p <= prod::kKontor) || p == prod::kHaupt ||
           (p >= prod::kPublicFirst && p <= prod::kPublicLast);
}
bool is_public(std::int32_t p) noexcept {
    return p >= prod::kPublicFirst && p <= prod::kPublicLast;
}

struct Footprint {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t w = 1;
    std::uint32_t h = 1;
    [[nodiscard]] bool contains(std::int32_t tx, std::int32_t ty) const noexcept {
        return tx >= x && ty >= y && tx < x + static_cast<std::int32_t>(w) && ty < y + static_cast<std::int32_t>(h);
    }
};

Footprint footprint_of(const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::uint8_t rotation) {
    Footprint f{x, y, 1, 1};
    rotated_size(b, rotation, f.w, f.h);
    return f;
}

const assets::BuildingType* type_at(const IslandMap& map, const assets::BuildingTable& buildings, std::int32_t x,
                                    std::int32_t y) {
    if (!map.contains(x, y)) {
        return nullptr;
    }
    const MapCell& cell = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    return cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
}

std::int32_t kind_at(const IslandMap& map, const assets::BuildingTable& buildings, std::int32_t x, std::int32_t y) {
    const assets::BuildingType* b = type_at(map, buildings, x, y);
    return b != nullptr ? b->kind : -1;
}

/// Whether `old` is a piece of the wall `b` (its land, beach or river mouth pieces): the families
/// are told apart by KreuzBase.
bool same_wall_family(const assets::BuildingTable& buildings, const assets::BuildingType& b,
                      const assets::BuildingType& old) {
    if (old.kreuz_base == b.kreuz_base) {
        return true;
    }
    const assets::BuildingType* beach = b.beach_offset != 0 ? buildings.by_record(b.record + b.beach_offset) : nullptr;
    if (beach == nullptr) {
        return false;
    }
    if (old.kreuz_base == beach->kreuz_base) {
        return true;
    }
    const assets::BuildingType* mouth =
        beach->mouth_offset != 0 ? buildings.by_record(beach->record + beach->mouth_offset) : nullptr;
    return mouth != nullptr && old.kreuz_base == mouth->kreuz_base;
}

/// The gold a wall family costs, read from its first land piece (beach and river mouth pieces
/// stand for the land piece they were ordered as).
std::int32_t wall_family_cost(const assets::BuildingTable& buildings, const assets::BuildingType& t) {
    const assets::BuildingType* land =
        t.land_record != assets::BuildingType::kNoRecord ? buildings.by_record(t.land_record) : &t;
    if (land == nullptr) {
        return 0;
    }
    const assets::BuildingType* head = buildings.by_record(static_cast<std::uint32_t>(land->kreuz_base));
    return (head != nullptr ? head : land)->build_cost.money;
}

/// Whether a tile carrying `old` takes the new building (0x00464660).
bool tile_accepts(const assets::BuildingTable& buildings, const assets::BuildingType& b,
                  const assets::BuildingType& old) {
    const std::int32_t o = old.kind;
    const bool open = o == kind::kForest || o == kind::kGround || o == kind::kRuin;
    const auto beach_like = [&] { return is_beach(o) || o == kind::kBeachRuin; };
    switch (b.kind) {
        case kind::kRoad:
            return open || (o == kind::kRoad && old.kreuz_base != b.kreuz_base);
        case kind::kGate:
            // Only on roads, walls, towers and open land; never on the beach (table 0x004649A0).
            return o == kind::kRoad || o == kind::kWall || o == kind::kTower || open;
        case kind::kWall:
        case kind::kWallBeach:
            // Walls of another kind, on land or on the beach: a stone wall over a palisade, a
            // palisade over a stone wall (the original replaces it; seen 2026-10-27).
            // A dearer wall replaces a cheaper one (a stone wall a palisade), never the other way
            // round. The original also builds a palisade over a stone wall; open1602 does not.
            if ((o == kind::kWall || o == kind::kWallBeach) && !same_wall_family(buildings, b, old)) {
                return wall_family_cost(buildings, b) > wall_family_cost(buildings, old);
            }
            if ((o == kind::kWallBeach || o == kind::kTower || o == kind::kTowerBeach) &&
                old.kreuz_base == b.kreuz_base) {
                return true;
            }
            return open || beach_like();
        case kind::kTower:
            return (o >= kind::kWall && o <= kind::kTowerBeach) || open || o == kind::kBeach ||
                   (o >= kind::kBeachOuter && o <= kind::kBeachVariant) || o == kind::kBeachRuin;
        case kind::kSquare:
            return o == kind::kRoad || open;
        case kind::kBridge:
            return o == kind::kRiver || o == kind::kBridge;
        case kind::kBeachHouse:
            return o == kind::kBeach || o == kind::kBeachVariant;
        case kind::kPier:
            return o == kind::kBeach || (o >= kind::kBeachOuter && o <= kind::kBeachVariant) || o == kind::kBeachRuin ||
                   (o == kind::kPier && old.id != b.id);
        case kind::kMine:
            return o == kind::kSlope;
        case kind::kHq:
            return o == kind::kRoad || open || o == kind::kSquare;
        case kind::kHarbour:
            return o == kind::kRoad || o == kind::kWall || o == kind::kWallBeach || open || o == kind::kSquare ||
                   o == kind::kBeach || (o >= kind::kBeachOuter && o <= kind::kBeachVariant) || o == kind::kBeachRuin;
        case kind::kMill:
            return open || o == kind::kRiver;
        default:
            if (b.kind >= kind::kSlope && b.kind <= kind::kSlopeSpring) {
                return false;  // editor terrain
            }
            if (!open) {
                return false;
            }
            if (b.prod_kind == prod::kGrowing) {
                // Not on a field of the same crop, growing or ripe.
                const std::uint8_t crop = crop_ware(buildings, b);
                if (crop != 0 && crop_ware(buildings, old) == crop &&
                    (old.prod_kind == prod::kGrowing || old.prod_kind == prod::kRipe)) {
                    return false;
                }
            }
            return true;
    }
}

/// Coast or river rotation of a building whose long side lies along a line of tiles.
/// `outside`: the line next to the footprint (Kontor, 0x00467AF0), else its own edge line
/// (shipyard 0x00467E60, water mill 0x004680F0). Order: left (2), right (0), top (3), bottom (1);
/// `turn` is added to the rotation: the water mill's wheel faces the other way (0x004680F0 returns
/// left 0, right 2, top 1, bottom 3).
template <class Test, class Accept>
std::uint8_t edge_rotation(const IslandMap& map, const assets::BuildingTable& buildings, const assets::BuildingType& b,
                           std::int32_t x, std::int32_t y, bool outside, Test&& test, Accept&& accept,
                           std::uint8_t turn = 0) {
    const auto lng = static_cast<std::int32_t>(std::max(b.size_x, b.size_y));
    const auto shrt = static_cast<std::int32_t>(std::min(b.size_x, b.size_y));
    const auto line = [&](std::int32_t x0, std::int32_t y0, std::int32_t dx, std::int32_t dy) {
        for (std::int32_t i = 0; i < lng; ++i) {
            const std::int32_t tx = x0 + dx * i;
            const std::int32_t ty = y0 + dy * i;
            if (!map.contains(tx, ty) || !test(kind_at(map, buildings, tx, ty))) {
                return false;
            }
        }
        return true;
    };
    const std::int32_t o = outside ? 1 : 0;
    struct Side {
        std::int32_t x0, y0, dx, dy;
        std::uint8_t rotation;
    };
    const std::array<Side, 4> sides = {{
        {x - o, y, 0, 1, 2},
        {x + shrt - 1 + o, y, 0, 1, 0},
        {x, y - o, 1, 0, 3},
        {x, y + shrt - 1 + o, 1, 0, 1},
    }};
    for (const Side& s : sides) {
        const auto rotation = static_cast<std::uint8_t>((s.rotation + turn) & 3U);
        if (line(s.x0, s.y0, s.dx, s.dy) && accept(rotation, TilePos{s.x0, s.y0})) {
            return rotation;
        }
    }
    return 0xF;
}

/// Ore of the mine's kind in the mountain on the side the mine faces (0x0046C010).
bool mine_has_deposit(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                      const assets::BuildingType& mine, std::int32_t x, std::int32_t y, std::uint8_t rotation) {
    static constexpr std::array<TilePos, 4> kSide = {{{-1, 0}, {0, -1}, {1, 0}, {0, 1}}};
    const TilePos side{x + kSide[rotation & 3U].x, y + kSide[rotation & 3U].y};
    const auto mountain = building_at(world, buildings, island, side);
    if (!mountain) {
        return false;
    }
    for (const auto& d : world.islands()[island].header.deposits) {
        if (d.x == mountain->origin.x && d.y == mountain->origin.y && d.ware == mine.ware) {
            return true;
        }
    }
    return false;
}

/// City an order belongs to (0x0046AEC0): a tile of another owner's city makes it foreign;
/// otherwise the player's city with the most tiles under the footprint; none outside.
struct OrderCity {
    std::uint8_t city = MapCell::kNoCity;
    bool foreign = false;
};

OrderCity city_of_order(const World& world, const IslandMap& map, PlayerId player, const Footprint& f,
                        std::uint16_t island) {
    std::array<std::uint32_t, 256> count{};
    for (std::uint32_t j = 0; j < f.h; ++j) {
        for (std::uint32_t i = 0; i < f.w; ++i) {
            const std::int32_t tx = f.x + static_cast<std::int32_t>(i);
            const std::int32_t ty = f.y + static_cast<std::int32_t>(j);
            if (!map.contains(tx, ty)) {
                continue;
            }
            const MapCell& cell = map.top(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
            if (cell.city == MapCell::kNoCity) {
                continue;
            }
            const City* c = world.city(island, cell.city);
            if (c == nullptr) {
                continue;
            }
            if (c->owner != player) {
                return OrderCity{cell.city, true};
            }
            ++count[cell.city];
        }
    }
    OrderCity result;
    std::uint32_t best = 0;
    for (std::size_t c = 0; c < count.size(); ++c) {
        if (count[c] > best) {
            best = count[c];
            result.city = static_cast<std::uint8_t>(c);
        }
    }
    return result;
}

std::uint32_t cities_on_island(const World& world, std::uint16_t island) {
    return static_cast<std::uint32_t>(
        std::count_if(world.cities().begin(), world.cities().end(), [&](const City& c) { return c.island == island; }));
}

bool player_has_city(const World& world, std::uint16_t island, PlayerId player) {
    return std::any_of(world.cities().begin(), world.cities().end(),
                       [&](const City& c) { return c.island == island && c.owner == player; });
}

/// The soldiers over an island, by row: one pass over all soldiers per order instead of one per
/// placed item (TB-7). Built when the first item asks.
class UnitsOnIsland {
public:
    /// A soldier stands on the footprint (0x00442980). Open1602 also counts a soldier that is just
    /// stepping on (S-015).
    bool in_the_way(const World& world, const WorldIsland& island, const Footprint& f) {
        // Half a tile around the footprint, in the soldiers' fixed point units (ADR 0004).
        constexpr std::int64_t kHalf = kPosUnit / 2;
        if (!m_built) {
            const std::int64_t x0 = std::int64_t{island.x} * kPosUnit - kHalf;
            const std::int64_t y0 = std::int64_t{island.y} * kPosUnit - kHalf;
            const std::int64_t x1 = (std::int64_t{island.x} + island.map.width()) * kPosUnit + kHalf;
            const std::int64_t y1 = (std::int64_t{island.y} + island.map.height()) * kPosUnit + kHalf;
            for (const Soldier& s : world.soldiers()) {
                if (s.pos_x >= x0 && s.pos_y >= y0 && s.pos_x < x1 && s.pos_y < y1) {
                    m_units.emplace_back(s.pos_y, s.pos_x);
                }
            }
            std::sort(m_units.begin(), m_units.end());
            m_built = true;
        }
        const std::int64_t left = (std::int64_t{island.x} + f.x) * kPosUnit - kHalf;
        const std::int64_t top = (std::int64_t{island.y} + f.y) * kPosUnit - kHalf;
        const std::int64_t right = (std::int64_t{island.x} + f.x + f.w) * kPosUnit + kHalf;
        const std::int64_t bottom = (std::int64_t{island.y} + f.y + f.h) * kPosUnit + kHalf;
        for (auto it = std::lower_bound(m_units.begin(), m_units.end(), std::pair{top, left});
             it != m_units.end() && it->first < bottom; ++it) {
            if (it->second >= left && it->second < right) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::pair<std::int64_t, std::int64_t>> m_units;  ///< (y, x) of each soldier
    bool m_built = false;
};

/// Planning state shared by the items of an order.
struct Planner {
    const World& world;
    const assets::BuildingTable& buildings;
    const BuildOrder& order;
    const WorldIsland& island;
    const IslandMap& map;
    std::uint8_t city = MapCell::kNoCity;
    bool foreign = false;
    std::set<std::pair<std::int32_t, std::int32_t>> taken;  // tiles of accepted items
    mutable UnitsOnIsland units;

    /// Whether the footprint fits (0x00464450 with the fixes of Open1602).
    BuildRefusal can_place(const assets::BuildingType& b, std::uint8_t city_of_tiles, std::uint8_t rotation,
                           std::int32_t x, std::int32_t y) const {
        const Footprint f = footprint_of(b, x, y, rotation);
        if (x < 0 || y < 0 || static_cast<std::uint32_t>(x) + f.w > map.width() ||
            static_cast<std::uint32_t>(y) + f.h > map.height()) {
            return BuildRefusal::OutsideIsland;
        }
        if (city_of_tiles == MapCell::kNoCity && !b.place_outside) {
            return BuildRefusal::Territory;
        }
        if (b.kind == kind::kMine && b.ware != ware::kStone &&
            !mine_has_deposit(world, buildings, order.island, b, x, y, rotation)) {
            return BuildRefusal::NoDeposit;
        }
        // A market claims its own circle, which covers its footprint: like the original it needs only
        // one full edge in the city, the rest may be free land (never another city's). With S-008
        // brought back (OriginalBugs::building_radius) every building does, and the free land
        // under it joins the city.
        const bool edge_rule = b.prod_kind == prod::kMarket || world.original_bugs().building_radius;
        if (edge_rule && !full_edge_in_city(f, city_of_tiles)) {
            return BuildRefusal::Territory;
        }
        for (std::uint32_t j = 0; j < f.h; ++j) {
            for (std::uint32_t i = 0; i < f.w; ++i) {
                const std::int32_t tx = x + static_cast<std::int32_t>(i);
                const std::int32_t ty = y + static_cast<std::int32_t>(j);
                const MapCell& cell = map.top(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
                // S-008: every other building lies completely in the city (the original wanted one
                // full edge and annexed the rest).
                if (cell.city != city_of_tiles && !(edge_rule && cell.city == MapCell::kNoCity)) {
                    return BuildRefusal::Territory;
                }
                const assets::BuildingType* old = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
                if (old == nullptr || !tile_accepts(buildings, b, *old)) {
                    return BuildRefusal::Terrain;
                }
                if (taken.contains({tx, ty})) {
                    return BuildRefusal::Overlap;
                }
            }
        }
        static constexpr std::array<std::int32_t, 7> kNoUnitCheck = {
            kind::kRoad, kind::kGround, kind::kRuin, kind::kSquare, kind::kBridge, kind::kBeachRuin, kind::kPier};
        if (std::find(kNoUnitCheck.begin(), kNoUnitCheck.end(), b.kind) == kNoUnitCheck.end() &&
            units.in_the_way(world, island, f)) {
            return BuildRefusal::UnitInTheWay;
        }
        return BuildRefusal::None;
    }

    /// Whether one side of the footprint lies completely in the city (0x00464450).
    bool full_edge_in_city(const Footprint& f, std::uint8_t city_of_tiles) const {
        const auto in_city = [&](std::int32_t tx, std::int32_t ty) {
            return map.top(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty)).city == city_of_tiles;
        };
        const auto w = static_cast<std::int32_t>(f.w);
        const auto h = static_cast<std::int32_t>(f.h);
        bool top = true;
        bool bottom = true;
        for (std::int32_t i = 0; i < w; ++i) {
            top = top && in_city(f.x + i, f.y);
            bottom = bottom && in_city(f.x + i, f.y + h - 1);
        }
        bool left = true;
        bool right = true;
        for (std::int32_t j = 0; j < h; ++j) {
            left = left && in_city(f.x, f.y + j);
            right = right && in_city(f.x + w - 1, f.y + j);
        }
        return top || bottom || left || right;
    }

    /// Rotation by Kind (0x00468300); 0xF = impossible.
    std::uint8_t rotate(const assets::BuildingType& b, std::uint8_t rotation, std::int32_t x, std::int32_t y) const {
        if ((b.kind == kind::kBuilding || b.kind == kind::kHq) && b.coastal) {
            return edge_rotation(map, buildings, b, x, y, true, kontor_coast, [&](std::uint8_t r, TilePos coast) {
                const std::uint8_t c =
                    map.top(static_cast<std::uint32_t>(coast.x), static_cast<std::uint32_t>(coast.y)).city;
                return can_place(b, c, r, x, y) == BuildRefusal::None;
            });
        }
        if (b.kind == kind::kBuilding && order.orient_to_road) {
            return road_rotation(b, rotation, x, y);
        }
        if (b.kind == kind::kBeachHouse) {
            return map.contains(x, y) ? map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).rotation
                                      : std::uint8_t{0xF};
        }
        if (b.kind == kind::kMine) {
            return mine_rotation(x, y);
        }
        if (b.kind == kind::kHarbour) {
            return edge_rotation(map, buildings, b, x, y, false, shipyard_coast,
                                 [](std::uint8_t, TilePos) { return true; });
        }
        if (b.kind == kind::kMill) {
            return edge_rotation(
                map, buildings, b, x, y, false, [](std::int32_t k) { return k == kind::kRiver; },
                [](std::uint8_t, TilePos) { return true; }, 2);
        }
        return static_cast<std::uint8_t>(rotation & 3U);
    }

    /// Mines face the one rock next to their origin (0x004671B0).
    std::uint8_t mine_rotation(std::int32_t x, std::int32_t y) const {
        std::uint32_t mask = 0;
        mask |= kind_at(map, buildings, x, y - 1) == kind::kRock ? 1U : 0U;
        mask |= kind_at(map, buildings, x + 1, y) == kind::kRock ? 2U : 0U;
        mask |= kind_at(map, buildings, x, y + 1) == kind::kRock ? 4U : 0U;
        mask |= kind_at(map, buildings, x - 1, y) == kind::kRock ? 8U : 0U;
        switch (mask) {
            case 1:
                return 1;
            case 2:
                return 2;
            case 4:
                return 3;
            case 8:
                return 0;
            default:
                return 0xF;
        }
    }

    bool road(std::int32_t x, std::int32_t y) const {
        const assets::BuildingType* b = type_at(map, buildings, x, y);
        if (b == nullptr) {
            return false;
        }
        if (b->kind == kind::kRoad || b->kind == kind::kBridge) {
            return true;
        }
        // A gate counts with its centre tile (0x00466B40).
        const MapCell& cell = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        return b->kind == kind::kGate && cell.sprite == b->gfx + (b->size_x * b->size_y) / 2;
    }

    /// Buildings with a door turn to the nearest road within 4 tiles (0x004674B0); otherwise the
    /// player's rotation, turned by a quarter if only the other orientation fits.
    std::uint8_t road_rotation(const assets::BuildingType& b, std::uint8_t rotation, std::int32_t x,
                               std::int32_t y) const {
        const bool fits0 = can_place(b, city, 0, x, y) == BuildRefusal::None;
        const bool fits1 = can_place(b, city, 1, x, y) == BuildRefusal::None;
        const std::int32_t far = static_cast<std::int32_t>(map.width() + map.height());
        std::int32_t down = far;
        std::int32_t up = far;
        std::int32_t right = far;
        std::int32_t left = far;
        const auto w = static_cast<std::int32_t>(map.width());
        const auto h = static_cast<std::int32_t>(map.height());
        if (fits1 && b.door_on_edge) {
            std::uint32_t fw = 1;
            std::uint32_t fh = 1;
            rotated_size(b, 1, fw, fh);
            for (std::int32_t tx = x; tx < std::min(x + static_cast<std::int32_t>(fw), w); ++tx) {
                for (std::int32_t ty = y + static_cast<std::int32_t>(fh); ty < h; ++ty) {
                    if (road(tx, ty)) {
                        down = std::min(down, ty - y - static_cast<std::int32_t>(fh));
                        break;
                    }
                }
                for (std::int32_t ty = y - 1; ty >= 0; --ty) {
                    if (road(tx, ty)) {
                        up = std::min(up, y - 1 - ty);
                        break;
                    }
                }
            }
        }
        if (fits0 && b.door_on_edge) {
            std::uint32_t fw = 1;
            std::uint32_t fh = 1;
            rotated_size(b, 0, fw, fh);
            for (std::int32_t ty = y; ty < std::min(y + static_cast<std::int32_t>(fh), h); ++ty) {
                for (std::int32_t tx = x + static_cast<std::int32_t>(fw); tx < w; ++tx) {
                    if (road(tx, ty)) {
                        right = std::min(right, tx - x - static_cast<std::int32_t>(fw));
                        break;
                    }
                }
                for (std::int32_t tx = x - 1; tx >= 0; --tx) {
                    if (road(tx, ty)) {
                        left = std::min(left, x - 1 - tx);
                        break;
                    }
                }
            }
        }
        std::uint8_t horizontal = 0;
        if (left < right) {
            horizontal = 2;
            right = left;
        }
        std::uint8_t result = 1;
        std::int32_t best = down;
        if (up < down) {
            result = 3;
            best = up;
        }
        if (right < best) {
            result = horizontal;
            best = right;
        }
        if (best > kRoadSearch) {
            result = static_cast<std::uint8_t>(rotation & 3U);
            if (!fits0 && fits1) {
                result = static_cast<std::uint8_t>(result | 1U);
            } else if (fits0 && !fits1) {
                result = static_cast<std::uint8_t>(result & 2U);
            }
        }
        return result;
    }
};

/// The palm of a southern island for a tree (record + RandAnz * RandAdd).
const assets::BuildingType* palm_or_self(const assets::BuildingTable& buildings, const WorldIsland& island,
                                         const assets::BuildingType& b) {
    if ((island.header.climate & 1U) == 0 || crop_ware(buildings, b) != ware::kTree) {
        return &b;
    }
    const assets::BuildingType* palm = buildings.by_record(b.record + std::uint32_t{b.rand_count} * b.rand_add);
    return palm != nullptr ? palm : &b;
}

/// Ground under a wall or tower that makes it a beach variant (0x00466A80): beach walls and
/// towers, beach, fisher huts, beach ruins, piers, shipyards.
bool beach_for_variant(std::int32_t k) noexcept {
    return k == kind::kWallBeach || k == kind::kTowerBeach || (k >= kind::kBeach && k <= kind::kPier) ||
           k == kind::kHarbour;
}

struct Variant {
    const assets::BuildingType* type;
    std::uint8_t rotation;
};

/// Walls and towers built on the beach (0x004653A0): the record Strandoff further on, turned like
/// the beach tile (taken from the ground layer under beach terrain and ruins), and at a river mouth
/// another Mundoff further on.
Variant beach_variant(const assets::BuildingTable& buildings, const IslandMap& map, const assets::BuildingType& b,
                      std::int32_t x, std::int32_t y, std::uint8_t rotation) {
    if (b.beach_offset == 0 || !map.contains(x, y)) {
        return {&b, rotation};
    }
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    const MapCell& top = map.top(ux, uy);
    const assets::BuildingType* old = top.empty() ? nullptr : buildings.by_sprite(top.sprite);
    if (old == nullptr || !beach_for_variant(old->kind)) {
        return {&b, rotation};
    }
    const assets::BuildingType* beach = buildings.by_record(b.record + b.beach_offset);
    if (beach == nullptr) {
        return {&b, rotation};
    }
    const MapCell& ground = map.ground(ux, uy);
    const std::uint8_t turned = old->destroyed ? ground.rotation : top.rotation;
    const assets::BuildingType* g = ground.empty() ? nullptr : buildings.by_sprite(ground.sprite);
    if (beach->mouth_offset != 0 && g != nullptr && g->kind == kind::kBeachMouth) {
        if (const assets::BuildingType* mouth = buildings.by_record(beach->record + beach->mouth_offset)) {
            beach = mouth;
        }
    }
    return {beach, turned};
}

/// What a building cost: a beach variant costs what its land building does (the original pays
/// the record the player ordered).
const assets::BuildingType::BuildCost& ordered_cost(const assets::BuildingTable& buildings,
                                                    const assets::BuildingType& b) {
    if (b.land_record != assets::BuildingType::kNoRecord) {
        if (const assets::BuildingType* land = buildings.by_record(b.land_record)) {
            return land->build_cost;
        }
    }
    return b.build_cost;
}

/// Goods a city or ship can pay: less than a ton counts as none (0x0047AA30).
std::uint32_t usable(std::uint32_t stock) noexcept {
    return stock < assets::kStockUnitsPerTon ? 0 : stock;
}

}  // namespace

bool building_available(const Player& player, const assets::BuildingType& type) noexcept {
    if (!player.unlocked(type.infra)) {
        return false;
    }
    switch (type.infra) {
        case kInfraCastle:
            return player.castles_built == 0;
        case kInfraCathedral:
            return player.cathedrals_built == 0;
        case kInfraArch:
            return player.arches_allowed > player.arches_built;
        case kInfraMonument:
            return player.monuments_allowed > player.monuments_built;
        default:
            return true;
    }
}

const char* refusal_name(BuildRefusal r) noexcept {
    switch (r) {
        case BuildRefusal::None:
            return "ok";
        case BuildRefusal::UnknownBuilding:
            return "unknown building";
        case BuildRefusal::OutsideIsland:
            return "outside the island";
        case BuildRefusal::Terrain:
            return "terrain";
        case BuildRefusal::Territory:
            return "outside the city";
        case BuildRefusal::ForeignCity:
            return "another player's city";
        case BuildRefusal::CityLimit:
            return "no further city on this island";
        case BuildRefusal::SecondKontor:
            return "the city has a Kontor";
        case BuildRefusal::NoCoast:
            return "no coast";
        case BuildRefusal::NoDeposit:
            return "no ore";
        case BuildRefusal::NoRock:
            return "no rock face";
        case BuildRefusal::NoRiver:
            return "no river";
        case BuildRefusal::UnitInTheWay:
            return "a unit is in the way";
        case BuildRefusal::Locked:
            return "not unlocked";
        case BuildRefusal::Limit:
            return "limit reached";
        case BuildRefusal::Infertile:
            return "does not grow here";
        case BuildRefusal::Overlap:
            return "overlaps";
        case BuildRefusal::Money:
            return "not enough gold";
        case BuildRefusal::Tools:
            return "not enough tools";
        case BuildRefusal::Wood:
            return "not enough wood";
        case BuildRefusal::Bricks:
            return "not enough bricks";
        case BuildRefusal::Cannons:
            return "not enough cannons";
        case BuildRefusal::ShipTooFar:
            return "the ship is too far away";
    }
    return "?";
}

std::uint32_t island_fertility(const WorldIsland& island, std::uint8_t ware) noexcept {
    constexpr std::uint8_t kFirst = sim::ware::kCornField;
    constexpr std::uint8_t kLast = sim::ware::kTreasure;
    if (ware < kFirst || ware > kLast) {
        // Ore: by its deposits, a big vein 128, a small one 64.
        std::uint32_t f = 0;
        for (const auto& d : island.header.deposits) {
            if (d.ware == ware) {
                if (d.kind == 0) {
                    return 128;
                }
                f = std::max<std::uint32_t>(f, 64);
            }
        }
        return f;
    }
    if ((island.header.fertility >> (ware - kFirst) & 1U) != 0) {
        return 128;
    }
    // Without the bit, the crops of the climate still grow half: tobacco, sugar cane and wine in
    // the north, spices, cotton and cocoa in the south.
    const bool north = island.header.climate == climate::kNorth;
    const bool south = island.header.climate == climate::kSouth;
    if ((north &&
         (ware == sim::ware::kTobaccoField || ware == sim::ware::kSugarField || ware == sim::ware::kWineField)) ||
        (south &&
         (ware == sim::ware::kSpiceField || ware == sim::ware::kCottonField || ware == sim::ware::kCocoaField))) {
        return 64;
    }
    return 0;
}

bool tile_fertile(const World& world, const assets::BuildingTable& buildings, std::uint16_t island_index,
                  std::uint8_t ware, std::int32_t x, std::int32_t y) {
    const std::uint8_t strength =
        island_index < world.islands().size() ? world.islands()[island_index].drought.strength : 0;
    return tile_fertile_at(world, buildings, island_index, ware, x, y, strength);
}

bool tile_fertile_at(const World& world, const assets::BuildingTable& buildings, std::uint16_t island_index,
                     std::uint8_t ware, std::int32_t x, std::int32_t y, std::uint8_t drought) {
    // The five patterns (table 0x00499B98): 2, 10, 16, 23 and 32 of 32 tiles carry.
    static constexpr std::array<std::array<std::uint8_t, 32>, 5> kPatterns = {{
        {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0},
        {0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1},
        {1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1},
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    }};
    if (island_index >= world.islands().size()) {
        return false;
    }
    const WorldIsland& island = world.islands()[island_index];
    const IslandMap& map = island.map;
    if (!map.contains(x, y)) {
        return false;
    }
    // Weighted with the Randwachs of the original ground (island + 0xAFC).
    const MapCell& ground = map.ground(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    const assets::BuildingType* g = ground.empty() ? nullptr : buildings.by_sprite(ground.sprite);
    const std::uint32_t edge = g != nullptr ? g->edge_growth : 128;
    std::uint32_t f = std::min<std::uint32_t>(island_fertility(island, ware) * edge >> 7, 128);
    // A drought takes its share, except from grass, trees and fish.
    if (drought != 0 && ware != ware::kGrass && ware != ware::kTree && ware != ware::kFish) {
        f -= drought * f / 128;
    }
    // Pattern by value (table 0x0054697C): 0..18, 19..41, 42..75, 76..107, 108..128.
    const std::size_t pattern = f >= 108 ? 4 : f >= 76 ? 3 : f >= 42 ? 2 : f >= 19 ? 1 : 0;
    const auto at = static_cast<std::size_t>((static_cast<std::uint32_t>(x) & 3U) + static_cast<std::uint32_t>(y) * 4U +
                                             island.header.number + ware) &
                    31U;
    return kPatterns[pattern][at] != 0;
}

bool city_defenceless(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                      std::uint8_t city) {
    if (island >= world.islands().size()) {
        return false;
    }
    const IslandMap& map = world.islands()[island].map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const MapCell& c = map.top(x, y);
            if (c.empty() || c.city != city) {
                continue;
            }
            const assets::BuildingType* b = buildings.by_sprite(c.sprite);
            if (b == nullptr) {
                continue;
            }
            if (((b->prod_kind == prod::kMarket || b->prod_kind == prod::kKontor) && !b->destroyed) ||
                b->prod_kind == prod::kTower) {
                return false;
            }
        }
    }
    return true;
}

BuildPlan plan_build(const World& world, const assets::BuildingTable& buildings, const BuildOrder& order) {
    BuildPlan plan;
    plan_build(world, buildings, order, plan);
    return plan;
}

void plan_build(const World& world, const assets::BuildingTable& buildings, const BuildOrder& order, BuildPlan& plan) {
    std::vector<PlannedBuild> items = std::move(plan.items);
    items.assign(order.items.size(), PlannedBuild{});
    plan = BuildPlan{};
    plan.items = std::move(items);
    if (order.island >= world.islands().size() || order.items.empty()) {
        for (PlannedBuild& p : plan.items) {
            p.refusal = BuildRefusal::OutsideIsland;
        }
        return;
    }
    const WorldIsland& island = world.islands()[order.island];
    const Player* player = world.player(order.player);
    Planner planner{world, buildings, order, island, island.map, MapCell::kNoCity, false, {}, {}};

    // The city of the whole order: under the footprint of its first item.
    const BuildItem& first = order.items.front();
    if (const assets::BuildingType* t = buildings.by_id(first.building)) {
        const OrderCity oc = city_of_order(world, island.map, order.player,
                                           footprint_of(*t, first.x, first.y, first.rotation), order.island);
        planner.city = oc.foreign ? MapCell::kNoCity : oc.city;
        planner.foreign = oc.foreign;
        // Taking over a city: a Kontor over the destroyed Kontor of a defenceless city of another
        // human or computer player (Ausbau path, build_effects 4.3).
        const bool acting =
            player != nullptr && (player->role == PlayerRole::Human || player->role == PlayerRole::Computer);
        if (oc.foreign && acting && t->kind == kind::kHq && t->upgrade && order.items.size() == 1) {
            const auto old = building_at(world, buildings, order.island, {first.x, first.y});
            if (old && old->type->destroyed && old->type->kreuz_base == t->kreuz_base && old->city == oc.city &&
                city_defenceless(world, buildings, order.island, oc.city)) {
                planner.city = oc.city;
                planner.foreign = false;
                plan.takeover = true;
            }
        }
    }
    plan.city = planner.city;

    // What the order may spend: the city's warehouse (or the ship when founding) and the gold.
    const assets::BuildingTable::Materials& m = buildings.materials();
    BuildBill available;
    if (order.ship) {
        if (const Ship* found = world.ship(*order.ship)) {
            const Ship& ship = *found;
            available.tools = ship.load(m.tools);
            available.wood = ship.load(m.wood);
            available.bricks = ship.load(m.bricks);
            available.cannons = 0;  // the original pays no cannons from a ship
        }
    } else if (const City* c = world.city(order.island, planner.city)) {
        available.tools = usable(c->stock(m.tools));
        available.wood = usable(c->stock(m.wood));
        available.bricks = usable(c->stock(m.bricks));
        available.cannons = usable(c->stock(m.cannons));
    }
    available.money = player != nullptr ? player->money : 0;
    const bool limited_player =
        player != nullptr && (player->role == PlayerRole::Human || player->role == PlayerRole::Computer);

    BuildBill bill;
    BuildRefusal budget = BuildRefusal::None;  // sticky once something did not fit
    std::size_t placed = 0;
    std::array<std::size_t, 4> placed_limited{};  // castles, cathedrals, arches, monuments placed
    const auto counted = [&](std::uint8_t infra) {
        return player != nullptr && infra >= kInfraCastle && infra <= kInfraMonument;
    };
    const auto limit_of = [&](std::uint8_t infra) -> std::size_t {
        if (!counted(infra)) {
            return order.items.size();
        }
        const auto left = [](std::uint32_t allowed, std::uint32_t built) -> std::size_t {
            return allowed > built ? allowed - built : 0;
        };
        switch (infra) {
            case kInfraCastle:
                return player->castles_built > 0 ? 0 : 1;
            case kInfraCathedral:
                return player->cathedrals_built > 0 ? 0 : 1;
            case kInfraArch:
                return left(player->arches_allowed, player->arches_built);
            default:
                return left(player->monuments_allowed, player->monuments_built);
        }
    };
    for (std::size_t i = 0; i < order.items.size(); ++i) {
        const BuildItem& item = order.items[i];
        PlannedBuild& out = plan.items[i];
        out.x = item.x;
        out.y = item.y;
        out.rotation = static_cast<std::uint8_t>(item.rotation & 3U);
        out.anim_phase = static_cast<std::uint8_t>(item.anim_phase & 15U);
        const assets::BuildingType* type = buildings.by_id(item.building);
        if (type == nullptr) {
            out.refusal = BuildRefusal::UnknownBuilding;
            continue;
        }
        out.type = type;
        // Castle, cathedral: one each; monuments and arches by allowance (0x004084D0). Counted per
        // kind over the whole order, whatever else it holds.
        const std::size_t limit = limit_of(type->infra);
        if (planner.foreign) {
            out.refusal = BuildRefusal::ForeignCity;  // S-006: no building in other players' cities
            continue;
        }
        if (limited_player && !player->unlocked(type->infra)) {
            out.refusal = BuildRefusal::Locked;  // S-014: checked here, not only in the menu
            continue;
        }

        BuildRefusal refusal = BuildRefusal::None;
        // Upgrade in place (Ausbauflg): a building of the same family of the city is replaced and
        // keeps its origin and rotation. Open1602 accepts only buildings of the same Kind (D-006:
        // the gold mine shares the weavers' family in haeuser.cod); like the original it allows
        // steps down (S-036).
        if (type->upgrade) {
            if (const auto old = building_at(world, buildings, order.island, {item.x, item.y});
                old && old->type->kreuz_base == type->kreuz_base && old->type->id != type->id &&
                old->type->kind == type->kind && old->city == planner.city && planner.city != MapCell::kNoCity) {
                out.x = old->origin.x;
                out.y = old->origin.y;
                out.rotation = old->rotation;
                out.upgrade = true;
            }
        }
        if (!out.upgrade) {
            const std::uint8_t r = planner.rotate(*type, out.rotation, item.x, item.y);
            if (r == 0xF) {
                refusal = type->kind == kind::kMine   ? BuildRefusal::NoRock
                          : type->kind == kind::kMill ? BuildRefusal::NoRiver
                                                      : BuildRefusal::NoCoast;
            } else {
                out.rotation = r;
                // A Kontor's coast decides its city (none when founding).
                refusal = planner.can_place(*type, planner.city, r, item.x, item.y);
            }
            if (refusal == BuildRefusal::None) {
                if (planner.city == MapCell::kNoCity) {
                    if ((limited_player && player_has_city(world, order.island, order.player)) ||
                        cities_on_island(world, order.island) >= kMaxCitiesPerIsland) {
                        refusal = BuildRefusal::CityLimit;
                    }
                } else if (type->kind == kind::kHq) {
                    refusal = BuildRefusal::SecondKontor;
                }
            }
            if (refusal == BuildRefusal::None && order.ship) {
                // The ship lies within the circle of radius 16 around the footprint.
                const Ship* ship = world.ship(*order.ship);
                const Footprint f = footprint_of(*type, item.x, item.y, out.rotation);
                const std::int32_t sx =
                    ship != nullptr ? static_cast<std::int32_t>(ship->x) - static_cast<std::int32_t>(island.x) : -1000;
                const std::int32_t sy =
                    ship != nullptr ? static_cast<std::int32_t>(ship->y) - static_cast<std::int32_t>(island.y) : -1000;
                const std::int32_t dx = std::max({f.x - sx, 0, sx - (f.x + static_cast<std::int32_t>(f.w) - 1)});
                const std::int32_t dy = std::max({f.y - sy, 0, sy - (f.y + static_cast<std::int32_t>(f.h) - 1)});
                static const std::vector<std::uint8_t> kCircle = circle_half_widths(kShipRange);
                if (dy > kShipRange || dx > kCircle[static_cast<std::size_t>(dy)]) {
                    refusal = BuildRefusal::ShipTooFar;
                }
            }
        }
        if (refusal == BuildRefusal::None) {
            const assets::BuildingType* final_type = palm_or_self(buildings, island, *type);
            if (!out.upgrade) {
                const Variant v = beach_variant(buildings, island.map, *final_type, out.x, out.y, out.rotation);
                final_type = v.type;
                out.rotation = v.rotation;
            }
            out.type = final_type;
            if (final_type->prod_kind == prod::kGrowing) {
                // S-003: crops are checked when planted (the original planted them withered).
                // During a drought a field that will bear afterwards is planted withered and grows
                // once the drought is over, as in the original. Only crops that can wither are
                // checked: forest and pasture have no withered record and grow on every tile, as
                // in the original (harvest and drought skip them as well).
                const std::uint8_t crop = crop_ware(buildings, *final_type);
                const assets::BuildingType* ripe = ripe_record(buildings, *final_type);
                const assets::BuildingType* withered = ripe != nullptr ? withered_record(buildings, *ripe) : nullptr;
                if (crop != 0 && withered != nullptr) {
                    if (!tile_fertile_at(world, buildings, order.island, crop, out.x, out.y, 0)) {
                        // OriginalBugs::infertile_fields: planted withered and paid for, as the
                        // original does.
                        if (world.original_bugs().infertile_fields) {
                            out.type = withered;
                        } else {
                            refusal = BuildRefusal::Infertile;
                        }
                    } else if (!tile_fertile(world, buildings, order.island, crop, out.x, out.y)) {
                        out.type = withered;
                    }
                }
            }
        }
        if (refusal != BuildRefusal::None) {
            out.refusal = refusal;
            continue;
        }
        // Costs in order; once something does not fit, the rest of the order stays unbuilt.
        BuildBill next = bill;
        next.add(ordered_cost(buildings, *type));
        plan.shown.add(ordered_cost(buildings, *type));
        if (budget == BuildRefusal::None) {
            if (counted(type->infra) && placed_limited[type->infra - kInfraCastle] >= limit) {
                budget = BuildRefusal::Limit;
            } else if (next.tools > available.tools) {
                budget = BuildRefusal::Tools;
            } else if (next.wood > available.wood) {
                budget = BuildRefusal::Wood;
            } else if (next.bricks > available.bricks) {
                budget = BuildRefusal::Bricks;
            } else if (next.cannons > available.cannons) {
                budget = BuildRefusal::Cannons;
            } else if (next.money > available.money && next.money != 0) {
                budget = BuildRefusal::Money;
            }
        }
        if (budget != BuildRefusal::None) {
            out.refusal = budget;
            continue;
        }
        bill = next;
        ++placed;
        if (counted(type->infra)) {
            ++placed_limited[type->infra - kInfraCastle];
        }
        if (i + 1 == order.items.size()) {
            continue;  // no item after it to collide with
        }
        const Footprint f = footprint_of(*out.type, out.x, out.y, out.rotation);
        for (std::uint32_t j = 0; j < f.h; ++j) {
            for (std::uint32_t k = 0; k < f.w; ++k) {
                planner.taken.insert({f.x + static_cast<std::int32_t>(k), f.y + static_cast<std::int32_t>(j)});
            }
        }
    }
    // The building under the mouse shows its costs wherever it stands.
    if (order.items.size() == 1 && plan.items[0].type != nullptr) {
        plan.shown = BuildBill{};
        plan.shown.add(ordered_cost(buildings, *buildings.by_id(order.items[0].building)));
    }
    plan.bill = bill;
    plan.placed = placed;
}

namespace {

/// Which cities' land reaches each tile of an island (0x00482120): a bit per city number, from the
/// circles of the markets and Kontors of the city and within 8 tiles of the towers of its owner.
/// Buildings with their origin in `gone` count as removed.
std::vector<std::uint8_t> territory_coverage(const World& world, const assets::BuildingTable& buildings,
                                             std::uint16_t island, const std::vector<TilePos>& gone) {
    const IslandMap& m = world.islands()[island].map;
    const std::uint32_t w = m.width();
    const std::uint32_t h = m.height();
    std::vector<std::uint8_t> covered(std::size_t{w} * h, 0);
    const auto cover = [&](const Catchment& area, std::uint8_t radius, std::uint8_t bits) {
        const std::vector<std::uint8_t>& half = circle_table(radius);
        for (std::int32_t dy = 0; dy <= radius; ++dy) {
            const std::int32_t hw = half[static_cast<std::size_t>(dy)];
            for (const std::int32_t row : {area.centre.y - dy, area.centre.y + area.parity_y + dy}) {
                for (std::int32_t tx = area.centre.x - hw; tx <= area.centre.x + area.parity_x + hw; ++tx) {
                    if (m.contains(tx, row)) {
                        covered[static_cast<std::size_t>(row) * w + static_cast<std::size_t>(tx)] |= bits;
                    }
                }
            }
        }
    };
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const MapCell& cell = m.top(x, y);
            const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
            if (b == nullptr || cell.city == MapCell::kNoCity) {
                continue;
            }
            const SubTile sub = m.sub_tile(buildings, x, y);
            if (sub.origin_x != static_cast<std::int32_t>(x) || sub.origin_y != static_cast<std::int32_t>(y) ||
                std::find(gone.begin(), gone.end(), TilePos{sub.origin_x, sub.origin_y}) != gone.end()) {
                continue;
            }
            const bool market = b->prod_kind == prod::kMarket;
            const bool kontor = b->prod_kind == prod::kKontor;
            const bool tower = b->kind == kind::kTower || b->kind == kind::kTowerBeach;
            if (!market && !kontor && !tower) {
                continue;
            }
            const Catchment area = catchment_of(*b, cell.rotation, {sub.origin_x, sub.origin_y});
            if (tower) {
                // A tower protects the land of every city of its owner.
                const City* c = world.city(island, cell.city);
                std::uint8_t bits = 0;
                for (const City& other : world.cities()) {
                    if (c != nullptr && other.island == island && other.owner == c->owner &&
                        other.index < kCitySlotsPerIsland) {
                        bits = static_cast<std::uint8_t>(bits | (1U << other.index));
                    }
                }
                cover(area, kTowerRadius, bits);
            } else {
                cover(area, std::max(b->radius, kMinKontorRadius),
                      static_cast<std::uint8_t>(cell.city < kCitySlotsPerIsland ? 1U << cell.city : 0U));
            }
        }
    }
    return covered;
}

/// The city of the same owner whose land still reaches a tile its own city lost, or none.
std::uint8_t covering_city(const World& world, std::uint16_t island, std::uint8_t own_city, std::uint8_t bits) {
    const City* own = world.city(island, own_city);
    for (std::uint8_t c = 0; c < kCitySlotsPerIsland; ++c) {
        const City* other = world.city(island, c);
        if ((bits >> c & 1U) != 0 && other != nullptr && own != nullptr && other->owner == own->owner) {
            return c;
        }
    }
    return MapCell::kNoCity;
}

/// Changes of one island in the order of the original (0x00481450, 0x004653A0, 0x00482120).
class Builder {
public:
    Builder(World& world, const assets::BuildingTable& buildings, std::uint16_t island,
            std::vector<ConstructionEvent>* events)
        : m_world(world),
          m_buildings(buildings),
          m_island(island),
          m_events(events),
          m_growth_before(world.growth().entries.size()) {}

    IslandMap& map() { return m_world.island(m_island).map; }

    void emit(ConstructionEvent e) {
        e.island = m_island;
        if (m_events != nullptr) {
            m_events->push_back(e);
        }
    }

    /// Unregisters every building whose origin lies inside the footprint (0x00481450 phase 1).
    /// Returns the production state of a replaced building (for upgrades).
    std::optional<Production> unregister(const Footprint& f) {
        std::optional<Production> kept;
        std::set<std::pair<std::int32_t, std::int32_t>> done;
        for (std::uint32_t j = 0; j < f.h; ++j) {
            for (std::uint32_t i = 0; i < f.w; ++i) {
                const TilePos t{f.x + static_cast<std::int32_t>(i), f.y + static_cast<std::int32_t>(j)};
                const auto old = building_at(m_world, m_buildings, m_island, t);
                if (!old || !f.contains(old->origin.x, old->origin.y) ||
                    done.contains({old->origin.x, old->origin.y})) {
                    continue;
                }
                done.insert({old->origin.x, old->origin.y});
                if (auto p = remove(*old)) {
                    kept = p;
                }
            }
        }
        return kept;
    }

    /// Takes a building out of the lists; returns its production state if it had one.
    std::optional<Production> remove(const BuildingAt& old) {
        const auto ox = static_cast<std::uint16_t>(old.origin.x);
        const auto oy = static_cast<std::uint16_t>(old.origin.y);
        const std::int32_t p = old.type->prod_kind;
        std::optional<Production> state;
        forget_disaster(m_world, m_island, old.origin);  // its fire or plague goes with it
        if (const auto index = m_world.production_index(m_island, ox, oy)) {
            state = m_world.productions()[*index];
            m_world.remove_production(m_island, ox, oy);
            ConstructionEvent e;
            e.kind = ConstructionEvent::Kind::Removed;
            e.x = old.origin.x;
            e.y = old.origin.y;
            e.city = old.city;
            e.production = index;
            e.type = old.type;
            emit(e);
        } else {
            ConstructionEvent e;
            e.kind = ConstructionEvent::Kind::Removed;
            e.x = old.origin.x;
            e.y = old.origin.y;
            e.city = old.city;
            e.type = old.type;
            emit(e);
        }
        if (p == prod::kGrowing) {
            m_forgotten.push_back(old.origin);  // its growth entry goes in finish(), all in one pass
        }
        if (p == prod::kHouse) {
            if (House* h = m_world.house(m_island, ox, oy)) {
                if (City* c = m_world.city(m_island, h->city)) {
                    if (h->resident_class < c->inhabitants.size()) {
                        c->inhabitants[h->resident_class] -=
                            std::min(c->inhabitants[h->resident_class], h->inhabitants());
                    }
                    // A queued house leaves the upgrade queue.
                    for (auto& slot : c->upgrade_queue) {
                        if (slot.x == ox && slot.y == oy) {
                            slot = City::UpgradeSlot{};
                        }
                    }
                }
                services_of(h->city);
                m_world.remove_house(m_island, ox, oy);
            }
            services_of(old.city);
        }
        if (p == prod::kShipyard || p == prod::kMilitary) {
            std::erase_if(m_world.cost_buildings(),
                          [&](const CostBuilding& c) { return c.island == m_island && c.x == ox && c.y == oy; });
        }
        if (p == prod::kMarket || p == prod::kKontor || p == prod::kTower || old.type->kind == kind::kTower ||
            old.type->kind == kind::kTowerBeach) {
            m_territory = true;
            services_of(old.city);
            m_storage_cities.insert(old.city);
            if (p == prod::kKontor) {
                if (City* c = m_world.city(m_island, old.city);
                    c != nullptr && c->warehouse_x == ox && c->warehouse_y == oy) {
                    c->warehouse_x.reset();
                    c->warehouse_y.reset();
                }
            }
        }
        if (is_public(p)) {
            services_of(old.city);
        }
        return state;
    }

    /// Registers a new building of city `city` (0x00481450 phase 2).
    void register_building(const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::uint8_t city,
                           std::uint32_t rnd, const std::optional<Production>& kept) {
        City* c = m_world.city(m_island, city);
        if (c == nullptr) {
            return;  // outside a city nothing is registered
        }
        const auto ux = static_cast<std::uint16_t>(x);
        const auto uy = static_cast<std::uint16_t>(y);
        const std::int32_t p = b.prod_kind;
        if (registers_production(p)) {
            Production entry;
            entry.island = m_island;
            entry.x = ux;
            entry.y = uy;
            entry.enabled = true;
            if (kept) {
                // Fix S-032: an upgrade keeps what the building held (the original lost it).
                entry.enabled = kept->enabled;
                entry.raw = kept->raw;
                entry.raw2 = kept->raw2;
                entry.stock = kept->stock;
                entry.produced = kept->produced;
                entry.elapsed = kept->elapsed;
            }
            m_world.set_production(entry);
            if (p == prod::kMarket || p == prod::kKontor) {
                m_territory = true;
                services_of(city);
                m_storage_cities.insert(city);
                if (p == prod::kKontor && !c->warehouse_x) {
                    c->warehouse_x = ux;
                    c->warehouse_y = uy;
                }
                if (p == prod::kKontor) {
                    // 0x00481EE0: a Kontor that allows fewer wares than the city trades in
                    // switches all trade settings off (a Kontor rebuilt at a lower level).
                    trim_trade_settings(*c, b);
                }
            }
            if (is_public(p)) {
                services_of(city);
            }
        }
        if (p == prod::kGrowing && !b.withered) {  // a withered field waits for the end of the drought
            GrowthEntry g;
            g.island = m_island;
            g.x = ux;
            g.y = uy;
            g.growth_class = static_cast<std::uint8_t>(std::min<std::uint32_t>(b.growth_class + rnd % 3U, 31U));
            g.seen = m_world.growth().counter[g.growth_class];
            g.stage = 0;  // Fix S-033: the original took the stage of the tile it replaced
            m_world.growth().entries.push_back(g);
        }
        if (p == prod::kHouse) {
            House h;
            h.island = m_island;
            h.x = ux;
            h.y = uy;
            h.city = city;
            h.resident_class = b.resident_class;
            h.inhabitants64 = 64;  // one inhabitant
            h.timer_group = static_cast<std::uint8_t>(rnd & 15U);
            m_world.add_house(h);
            if (b.resident_class >= c->inhabitants.size()) {
                c->inhabitants.resize(std::size_t{b.resident_class} + 1, 0);
            }
            c->inhabitants[b.resident_class] += 1;
            services_of(city);
        }
        if (p == prod::kShipyard) {
            m_world.add_cost_building(CostBuilding{CostBuilding::Kind::Shipyard, m_island, ux, uy, false});
        }
        if (p == prod::kMilitary) {
            m_world.add_cost_building(CostBuilding{CostBuilding::Kind::Castle, m_island, ux, uy, true});
        }
        if (p == prod::kTower || b.kind == kind::kTower || b.kind == kind::kTowerBeach) {
            m_territory = true;
        }
    }

    /// Writes the tiles and fits roads and walls to their neighbours (0x004653A0).
    void write(const assets::BuildingType& placed, std::int32_t x, std::int32_t y, std::uint8_t rotation,
               std::uint8_t anim_phase, std::uint8_t city) {
        const City* c = m_world.city(m_island, city);
        const PlayerId owner = c != nullptr ? c->owner : kNoPlayer;
        const std::int32_t old_kind = kind_at(map(), m_buildings, x, y);
        const assets::BuildingType* b = &placed;
        // A beach wall alone on a beach corner is the corner piece (KreuzBase + 1).
        if (b->kind == kind::kWallBeach && (old_kind == kind::kBeachOuter || old_kind == kind::kBeachInner) &&
            wall_mask(x, y) == 0) {
            if (const assets::BuildingType* corner =
                    m_buildings.by_record(static_cast<std::uint32_t>(b->kreuz_base) + 1U)) {
                b = corner;
            }
        }
        (void)map().build(*b, x, y, rotation, anim_phase, owner, city);
        const Footprint f = footprint_of(*b, x, y, rotation);
        fit(x, y);
        fit_around(f);
        // A new Kontor lays a pier along its coast side (Strandflg, record 0x0061FA2C), not an
        // upgraded one. The piers stay when the Kontor goes, as in the original.
        if (b->coastal && old_kind != b->kind) {
            lay_piers(*b, f, rotation);
        }
    }

    void fit_around(const Footprint& f) {
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(f.w); ++i) {
            fit(f.x + i, f.y - 1);
            fit(f.x + i, f.y + static_cast<std::int32_t>(f.h));
        }
        for (std::int32_t j = 0; j < static_cast<std::int32_t>(f.h); ++j) {
            fit(f.x - 1, f.y + j);
            fit(f.x + static_cast<std::int32_t>(f.w), f.y + j);
        }
    }

    /// The line of tiles in front of the long side of a Kontor (the coast its rotation was chosen
    /// for, 0x00467AF0) becomes pier, turned like the Kontor; the tiles keep their city.
    void lay_piers(const assets::BuildingType& b, const Footprint& f, std::uint8_t rotation) {
        const assets::BuildingType* pier = first_of_kind(kind::kPier);
        if (pier == nullptr) {
            return;
        }
        IslandMap& m = map();
        const auto lng = static_cast<std::int32_t>(std::max(b.size_x, b.size_y));
        for (std::int32_t i = 0; i < lng; ++i) {
            std::int32_t tx = 0;
            std::int32_t ty = 0;
            switch (rotation & 3U) {
                case 0:
                    tx = f.x + static_cast<std::int32_t>(f.w);
                    ty = f.y + i;
                    break;
                case 1:
                    tx = f.x + i;
                    ty = f.y + static_cast<std::int32_t>(f.h);
                    break;
                case 2:
                    tx = f.x - 1;
                    ty = f.y + i;
                    break;
                default:
                    tx = f.x + i;
                    ty = f.y - 1;
                    break;
            }
            if (!m.contains(tx, ty)) {
                continue;
            }
            const MapCell cell = m.top(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
            (void)m.build(*pier, tx, ty, rotation, 0, cell.owner, cell.city);
            fit_around(Footprint{tx, ty, 1, 1});
        }
    }

    /// The first record of a Kind, like the special records the original keeps (0x00462D90).
    const assets::BuildingType* first_of_kind(std::int32_t k) const {
        const assets::BuildingType* first = nullptr;
        for (const assets::BuildingType& t : m_buildings.types()) {
            if (t.kind == k && (first == nullptr || t.record < first->record)) {
                first = &t;
            }
        }
        return first;
    }

    /// Walls, gates and towers around a tile (0x00466B10): N 8, E 4, S 2, W 1.
    std::uint32_t wall_mask(std::int32_t x, std::int32_t y) const {
        const auto wall_like = [&](std::int32_t tx, std::int32_t ty) {
            const std::int32_t k = kind_at(m_world.islands()[m_island].map, m_buildings, tx, ty);
            return k >= kind::kGate && k <= kind::kTowerBeach;
        };
        return (wall_like(x, y - 1) ? 8U : 0U) | (wall_like(x + 1, y) ? 4U : 0U) | (wall_like(x, y + 1) ? 2U : 0U) |
               (wall_like(x - 1, y) ? 1U : 0U);
    }

    /// Where the sea lies around a beach wall (0x00466E30, then 0x00466C00): 1 N, 2 NE, 3 E, 4 SE,
    /// 5 S, 6 SW, 7 W, 8 NW, 0 undecided (a straight coast).
    std::uint32_t coast_code(std::int32_t x, std::int32_t y) const {
        const IslandMap& m = m_world.islands()[m_island].map;
        const auto water = [&](std::int32_t tx, std::int32_t ty) {
            const std::int32_t k = kind_at(m, m_buildings, tx, ty);  // outside the island: land
            return k >= kind::kSea && k <= kind::kRiverMouth;
        };
        constexpr std::uint32_t W = 0x01, S = 0x02, E = 0x04, N = 0x08, NW = 0x10, SW = 0x20, SE = 0x40, NE = 0x80;
        const std::uint32_t mask = (water(x - 1, y) ? W : 0U) | (water(x, y + 1) ? S : 0U) |
                                   (water(x + 1, y) ? E : 0U) | (water(x, y - 1) ? N : 0U) |
                                   (water(x - 1, y - 1) ? NW : 0U) | (water(x - 1, y + 1) ? SW : 0U) |
                                   (water(x + 1, y + 1) ? SE : 0U) | (water(x + 1, y - 1) ? NE : 0U);
        switch (mask) {
            case N:
                return 1;
            case E:
                return 3;
            case S:
                return 5;
            case W:
                return 7;
            default:
                break;
        }
        // Outer corners: two sides and the diagonal between them, the two other diagonals next to
        // them may be water too.
        const auto corner = [&](std::uint32_t core, std::uint32_t optional) {
            return (mask & core) == core && (mask & ~(core | optional)) == 0;
        };
        if (corner(N | W | NW, SW | NE)) {
            return 8;
        }
        if (corner(W | S | SW, NW | SE)) {
            return 6;
        }
        if (corner(S | E | SE, SW | NE)) {
            return 4;
        }
        if (corner(E | N | NE, NW | SE)) {
            return 2;
        }
        // Inner corners: a single diagonal.
        switch (mask) {
            case NW:
                return 8;
            case SW:
                return 6;
            case SE:
                return 4;
            case NE:
                return 2;
            default:
                return 0;
        }
    }

    /// A road or wall tile takes the variant that matches its neighbours: straight, curve, T or
    /// cross of its KreuzBase family (masks N 8, E 4, S 2, W 1).
    void fit(std::int32_t x, std::int32_t y) {
        IslandMap& m = map();
        if (!m.contains(x, y)) {
            return;
        }
        const MapCell cell = m.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        const assets::BuildingType* b = cell.empty() ? nullptr : m_buildings.by_sprite(cell.sprite);
        if (b == nullptr || b->size_x != 1 || b->size_y != 1 || b->kreuz_base <= 0) {
            return;
        }
        if (b->kind == kind::kBridge) {
            fit_bridge(*b, x, y, cell);
            return;
        }
        const bool road = b->kind == kind::kRoad;
        const bool beach_wall = b->kind == kind::kWallBeach;
        const bool wall = b->kind == kind::kWall || beach_wall;
        if (!road && !wall) {
            return;
        }
        const auto connects = [&](std::int32_t tx, std::int32_t ty) {
            const assets::BuildingType* n = type_at(m, m_buildings, tx, ty);
            if (n == nullptr) {
                return false;
            }
            if (road) {
                if (n->kind == kind::kRoad || n->kind == kind::kBridge) {
                    return true;
                }
                const MapCell& nc = m.top(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
                return n->kind == kind::kGate && nc.sprite == n->gfx + (n->size_x * n->size_y) / 2;
            }
            return n->kind >= kind::kGate && n->kind <= kind::kTowerBeach;  // 0x00466B10
        };
        const std::uint32_t mask = (connects(x, y - 1) ? 8U : 0U) | (connects(x + 1, y) ? 4U : 0U) |
                                   (connects(x, y + 1) ? 2U : 0U) | (connects(x - 1, y) ? 1U : 0U);
        const auto base = static_cast<std::uint32_t>(b->kreuz_base);
        std::uint32_t variant = 0;  // 0 straight, 1 curve, 2 T, 3 cross
        std::uint8_t rotation = 0;
        switch (mask) {
            case 0:
                return;  // alone: stays as placed
            case 1:
            case 4:
            case 5:
                variant = 0;
                rotation = static_cast<std::uint8_t>(cell.rotation & 2U);
                break;
            case 2:
            case 8:
            case 10:
                variant = 0;
                rotation = static_cast<std::uint8_t>((cell.rotation & 2U) + 1U);
                break;
            case 3:
                variant = 1;
                rotation = 2;
                break;
            case 6:
                variant = 1;
                rotation = 1;
                break;
            case 9:
                variant = 1;
                rotation = 3;
                break;
            case 12:
                variant = 1;
                rotation = 0;
                break;
            case 7:
                variant = 2;
                rotation = 1;
                break;
            case 11:
                variant = 2;
                rotation = 2;
                break;
            case 13:
                variant = 2;
                rotation = 3;
                break;
            case 14:
                variant = 2;
                rotation = 0;
                break;
            default:
                variant = 3;
                rotation = 0;
                break;
        }
        if (wall && variant >= 2) {
            variant = 2;  // walls: three or more neighbours take the T piece, unturned
            rotation = 0;
        }
        if (beach_wall && variant == 0) {
            // A straight beach wall turns its back to the sea; on a straight coast it keeps the
            // turn of the beach (rotation: where the sea lies, 0 S, 1 W, 2 N, 3 E).
            const std::uint32_t coast = coast_code(x, y);
            if (mask == 1 || mask == 4 || mask == 5) {
                if (coast == 1 || coast == 2 || coast == 8) {
                    rotation = 2;
                } else if (coast >= 4 && coast <= 6) {
                    rotation = 0;
                }
            } else if (coast >= 2 && coast <= 4) {
                rotation = 3;
            } else if (coast >= 6 && coast <= 8) {
                rotation = 1;
            }
        }
        const assets::BuildingType* v = m_buildings.by_record(base + variant);
        if (v == nullptr || v->size_x != 1 || v->size_y != 1) {
            return;
        }
        (void)m.build(*v, x, y, rotation, cell.anim_phase, cell.owner, cell.city);
    }

    /// A bridge lies across its river: river or bridge to the west or east → even turn, to the
    /// north or south → odd turn, else as it is.
    void fit_bridge(const assets::BuildingType& b, std::int32_t x, std::int32_t y, const MapCell& cell) {
        IslandMap& m = map();
        const auto river = [&](std::int32_t tx, std::int32_t ty) {
            const std::int32_t k = kind_at(m, m_buildings, tx, ty);
            return k == kind::kRiver || k == kind::kRiverCorner || k == kind::kBridge;
        };
        const std::uint32_t mask = (river(x, y - 1) ? 8U : 0U) | (river(x + 1, y) ? 4U : 0U) |
                                   (river(x, y + 1) ? 2U : 0U) | (river(x - 1, y) ? 1U : 0U);
        std::uint8_t rotation = 0;
        if (mask == 1 || mask == 4 || mask == 5) {
            rotation = static_cast<std::uint8_t>(cell.rotation & 2U);
        } else if (mask == 2 || mask == 8 || mask == 10) {
            rotation = static_cast<std::uint8_t>((cell.rotation & 2U) + 1U);
        } else {
            return;
        }
        if (rotation != cell.rotation) {
            (void)m.build(b, x, y, rotation, cell.anim_phase, cell.owner, cell.city);
        }
    }

    /// Territory of a market or Kontor (0x00465170, 0x0046AC60): free land tiles in the circle.
    void claim(const assets::BuildingType& b, std::int32_t x, std::int32_t y, std::uint8_t rotation,
               std::uint8_t city) {
        std::uint8_t radius = b.radius;
        if (b.prod_kind == prod::kKontor) {
            radius = std::max(radius, kMinKontorRadius);
        } else if (b.prod_kind != prod::kMarket) {
            return;
        }
        const City* c = m_world.city(m_island, city);
        if (c == nullptr) {
            return;
        }
        IslandMap& m = map();
        const Catchment area = catchment_of(b, rotation, {x, y});
        const std::vector<std::uint8_t>& half = circle_table(radius);
        for (std::int32_t dy = 0; dy <= radius; ++dy) {
            const std::int32_t hw = half[static_cast<std::size_t>(dy)];
            for (const std::int32_t row : {area.centre.y - dy, area.centre.y + area.parity_y + dy}) {
                for (std::int32_t tx = area.centre.x - hw; tx <= area.centre.x + area.parity_x + hw; ++tx) {
                    if (!m.contains(tx, row)) {
                        continue;
                    }
                    const MapCell& cell = m.top(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(row));
                    if (cell.city != MapCell::kNoCity || kind_at(m, m_buildings, tx, row) == kind::kSea) {
                        continue;  // never another city's land, never the sea
                    }
                    m.set_city(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(row), city, c->owner);
                }
                if (dy == 0 && area.parity_y == 0) {
                    break;  // the centre row once
                }
            }
        }
    }

    /// Founds a city (0x00468CE0, 0x00468E10): the first free city number of the island.
    std::uint8_t found(PlayerId owner, std::int32_t x, std::int32_t y, const std::vector<std::string>& names,
                       std::size_t classes) {
        std::uint8_t index = 0;
        while (index < kMaxCitiesPerIsland && m_world.city(m_island, index) != nullptr) {
            ++index;
        }
        if (index >= kMaxCitiesPerIsland) {
            return MapCell::kNoCity;
        }
        City c;
        c.island = m_island;
        c.index = index;
        c.owner = owner;
        // The next name of Text.cod's [STAEDTE], round robin.
        if (!names.empty()) {
            const std::uint32_t n = m_world.city_name_index() % static_cast<std::uint32_t>(names.size());
            c.name = names[n];
            m_world.set_city_name_index((n + 1) % static_cast<std::uint32_t>(names.size()));
        }  // else the name stays empty, and the game shows one in its language (MD-10)
        c.inhabitants.assign(classes, 0);
        c.satisfaction.assign(classes, 0);
        c.tax_rate.assign(classes, 128);
        c.upgrade_queue.assign(classes + 1, City::UpgradeSlot{});
        c.food_ratio = 0;
        c.warehouse_x = static_cast<std::uint16_t>(x);
        c.warehouse_y = static_cast<std::uint16_t>(y);
        m_world.add_city(std::move(c));
        // 0x00468DD2: the city of a human or a computer shows its island to its founder; that the
        // original shows it to every player too (all eight discovery bits) is the game setting
        // Rules::reveal, which the simulation applies (World::reveal_islands). Natives and pirates
        // leave the island as it was.
        const Player* founder = m_world.player(owner);
        if (founder != nullptr && (founder->role == PlayerRole::Human || founder->role == PlayerRole::Computer)) {
            m_world.island(m_island).discover(owner);
        }
        ConstructionEvent e;
        e.kind = ConstructionEvent::Kind::CityFounded;
        e.x = x;
        e.y = y;
        e.city = index;
        emit(e);
        return index;
    }

    /// Restores the original ground under a removed building (0x00464B40): the ground layer, or
    /// meadow where it was forest (the forest ground itself when the data has no meadow).
    void restore_ground(const Footprint& f, std::uint8_t city) {
        IslandMap& m = map();
        const assets::BuildingType* meadow = m_buildings.by_id(kMeadowId);
        for (std::uint32_t j = 0; j < f.h; ++j) {
            for (std::uint32_t i = 0; i < f.w; ++i) {
                const std::int32_t tx = f.x + static_cast<std::int32_t>(i);
                const std::int32_t ty = f.y + static_cast<std::int32_t>(j);
                const MapCell top = m.top(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
                const MapCell& ground = m.ground(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
                const assets::BuildingType* g = ground.empty() ? nullptr : m_buildings.by_sprite(ground.sprite);
                if (g == nullptr || (g->kind == kind::kForest && meadow != nullptr)) {
                    if (meadow != nullptr) {
                        (void)m.build(*meadow, tx, ty, 0, 0, top.owner, city);
                    }
                    continue;
                }
                // The ground tile as it was (multi-tile terrain keeps its sub-tile).
                m.restore_ground(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
                m.set_city(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty), city, top.owner);
                if (g->prod_kind == prod::kGrowing) {
                    GrowthEntry e;
                    e.island = m_island;
                    e.x = static_cast<std::uint16_t>(tx);
                    e.y = static_cast<std::uint16_t>(ty);
                    e.growth_class = g->growth_class;
                    e.seen = m_world.growth().counter[g->growth_class];
                    e.stage = ground.anim_phase;
                    m_world.growth().entries.push_back(e);
                }
            }
        }
        for (std::uint32_t j = 0; j < f.h; ++j) {
            fit(f.x - 1, f.y + static_cast<std::int32_t>(j));
            fit(f.x + static_cast<std::int32_t>(f.w), f.y + static_cast<std::int32_t>(j));
        }
        for (std::uint32_t i = 0; i < f.w; ++i) {
            fit(f.x + static_cast<std::int32_t>(i), f.y - 1);
            fit(f.x + static_cast<std::int32_t>(i), f.y + static_cast<std::int32_t>(f.h));
        }
    }

    /// Destroys a building to ruins (0x00463F40): one ruin of the same size, else 1x1 ruins with
    /// a random variant; without a ruin record the ground comes back.
    void destroy(const BuildingAt& b) {
        const Footprint f{b.origin.x, b.origin.y, b.width, b.height};
        const std::uint8_t city = b.city;
        remove(b);
        IslandMap& m = map();
        const assets::BuildingType* ruin =
            b.type->ruin != 0xFF ? m_buildings.ruin_record(b.type->ruin, false) : nullptr;
        if (ruin == nullptr) {
            restore_ground(f, city);
            return;
        }
        const auto variant = [&](const assets::BuildingType& r) {
            const std::uint32_t pick =
                m_world.random(RandomStream::Construction).below(std::max<std::uint32_t>(r.rand_count, 1));
            const assets::BuildingType* v = m_buildings.by_record(r.record + pick * r.rand_add);
            return v != nullptr ? v : &r;
        };
        const auto on_beach = [&](std::int32_t tx, std::int32_t ty) {
            const MapCell& ground = m.ground(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
            const assets::BuildingType* g = ground.empty() ? nullptr : m_buildings.by_sprite(ground.sprite);
            return g != nullptr && is_beach(g->kind);
        };
        if (ruin->size_x == b.type->size_x && ruin->size_y == b.type->size_y) {
            // One ruin over the whole footprint, the beach variant by the origin's ground; it is
            // registered as a building of the city (0x00463FD0): a destroyed Kontor stays the
            // city's Kontor and holds its land (S-018), until another player builds over it.
            const assets::BuildingType* r = m_buildings.ruin_record(b.type->ruin, on_beach(b.origin.x, b.origin.y));
            if (r == nullptr || r->size_x != ruin->size_x || r->size_y != ruin->size_y) {
                r = ruin;
            }
            const assets::BuildingType* v = variant(*r);
            (void)m.build(*v, b.origin.x, b.origin.y, b.rotation, 0,
                          m.top(static_cast<std::uint32_t>(b.origin.x), static_cast<std::uint32_t>(b.origin.y)).owner,
                          city);
            register_building(*v, b.origin.x, b.origin.y, city, 0, std::nullopt);
            return;
        }
        for (std::uint32_t j = 0; j < f.h; ++j) {
            for (std::uint32_t i = 0; i < f.w; ++i) {
                const std::int32_t tx = f.x + static_cast<std::int32_t>(i);
                const std::int32_t ty = f.y + static_cast<std::int32_t>(j);
                const assets::BuildingType* r = m_buildings.ruin_record(b.type->ruin, on_beach(tx, ty));
                if (r == nullptr || r->size_x != 1 || r->size_y != 1) {
                    r = m_buildings.ruin_record(b.type->ruin, false);
                }
                if (r == nullptr || r->size_x != 1 || r->size_y != 1) {
                    continue;
                }
                const MapCell top = m.top(static_cast<std::uint32_t>(tx), static_cast<std::uint32_t>(ty));
                (void)m.build(*variant(*r), tx, ty, top.rotation, 0, top.owner, city);
            }
        }
    }

    /// Re-checks the territory of the island after markets, Kontors or towers changed
    /// (0x00482120): every city tile must lie in a circle of a market or Kontor of its city or
    /// within 8 tiles of a tower of its owner; otherwise it goes to another covering city of the
    /// same owner or becomes free, and what stands on it falls to ruins. A city without its
    /// Kontor is dissolved.
    void recheck_territory() {
        IslandMap& m = map();
        const std::uint32_t w = m.width();
        const std::uint32_t h = m.height();
        const std::vector<std::uint8_t> covered = territory_coverage(m_world, m_buildings, m_island, {});
        // Tiles their city no longer covers.
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const MapCell cell = m.top(x, y);
                if (cell.city == MapCell::kNoCity || cell.city >= 8) {
                    continue;
                }
                const std::uint8_t bits = covered[std::size_t{y} * w + x];
                if ((bits >> cell.city & 1U) != 0) {
                    continue;
                }
                lose_tile(x, y, covering_city(m_world, m_island, cell.city, bits));
            }
        }
        // Cities without their Kontor are dissolved (0x00468ED0).
        std::vector<std::uint8_t> dissolve;
        for (const City& c : m_world.cities()) {
            if (c.island == m_island && !c.warehouse_x) {
                dissolve.push_back(c.index);
            }
        }
        for (const std::uint8_t c : dissolve) {
            dissolve_city(c);
        }
    }

    /// A tile leaves its city: to `next` (same owner) or free; a building on it moves with it or
    /// falls to ruins.
    void lose_tile(std::uint32_t x, std::uint32_t y, std::uint8_t next) {
        IslandMap& m = map();
        const City* to = next != MapCell::kNoCity ? m_world.city(m_island, next) : nullptr;
        const auto b =
            building_at(m_world, m_buildings, m_island, {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
        if (to == nullptr && b && ruinable(*b->type)) {
            destroy(*b);
        }
        m.set_city(x, y, next, to != nullptr ? to->owner : kNoPlayer);
        if (to != nullptr && b) {
            if (House* house = m_world.house(m_island, static_cast<std::uint16_t>(b->origin.x),
                                             static_cast<std::uint16_t>(b->origin.y));
                house != nullptr && house->city != next) {
                move_house(*house, next);
            }
        }
    }

    void services_of(std::uint8_t city) { m_service_cities.insert(city); }

    void move_house(House& h, std::uint8_t next) {
        City* from = m_world.city(m_island, h.city);
        City* to = m_world.city(m_island, next);
        if (from != nullptr && h.resident_class < from->inhabitants.size()) {
            from->inhabitants[h.resident_class] -= std::min(from->inhabitants[h.resident_class], h.inhabitants());
        }
        if (to != nullptr) {
            if (h.resident_class >= to->inhabitants.size()) {
                to->inhabitants.resize(std::size_t{h.resident_class} + 1, 0);
            }
            to->inhabitants[h.resident_class] += h.inhabitants();
        }
        services_of(h.city);
        services_of(next);
        h.city = next;
    }

    /// What falls to ruins when its land is lost (0x00480B70): buildings, roads, piers and crop
    /// fields; not grass, trees, fish or terrain.
    bool ruinable(const assets::BuildingType& b) const { return territory_building(m_buildings, b); }

    void dissolve_city(std::uint8_t index) {
        IslandMap& m = map();
        for (std::uint32_t y = 0; y < m.height(); ++y) {
            for (std::uint32_t x = 0; x < m.width(); ++x) {
                if (m.top(x, y).city != index) {
                    continue;
                }
                const auto b = building_at(m_world, m_buildings, m_island,
                                           {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
                if (b && b->origin.x == static_cast<std::int32_t>(x) && b->origin.y == static_cast<std::int32_t>(y) &&
                    ruinable(*b->type)) {
                    destroy(*b);
                }
            }
        }
        for (std::uint32_t y = 0; y < m.height(); ++y) {
            for (std::uint32_t x = 0; x < m.width(); ++x) {
                if (m.top(x, y).city == index) {
                    m.set_city(x, y, MapCell::kNoCity, kNoPlayer);
                }
            }
        }
        // Houses, productions and cost buildings of the city are gone with their buildings.
        m_world.remove_city(m_island, index);
        ConstructionEvent e;
        e.kind = ConstructionEvent::Kind::CityDissolved;
        e.city = index;
        emit(e);
        services_of(index);
    }

    /// After the changes: territory, services and warehouse room.
    void finish() {
        // Growth entries of removed crops and trees; entries added by this change stay.
        forget_growth(m_world, m_island, m_forgotten, m_growth_before);
        m_forgotten.clear();
        if (m_territory) {
            recheck_territory();
        }
        if (m_territory && !m_service_cities.empty()) {
            // Land changed hands: houses and service buildings may have changed city.
            update_services(m_world, m_buildings, m_island);
        } else if (!m_service_cities.empty()) {
            // A house looks only at its own city's buildings: only these cities changed (TB-22).
            update_services(m_world, m_buildings, m_island, m_service_cities);
        }
        for (const std::uint8_t c : m_storage_cities) {
            ConstructionEvent e;
            e.kind = ConstructionEvent::Kind::StorageChanged;
            e.city = c;
            emit(e);
        }
    }

private:
    World& m_world;
    const assets::BuildingTable& m_buildings;
    std::uint16_t m_island;
    std::vector<ConstructionEvent>* m_events;
    bool m_territory = false;
    /// Cities whose houses' services must be recomputed in finish().
    std::set<std::uint8_t> m_service_cities;
    std::set<std::uint8_t> m_storage_cities;
    std::size_t m_growth_before;       ///< growth entries before this change
    std::vector<TilePos> m_forgotten;  ///< removed growing tiles
};

}  // namespace

void place_tiles(World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                 const assets::BuildingType& type, std::int32_t x, std::int32_t y, std::uint8_t rotation) {
    if (island >= world.islands().size()) {
        return;
    }
    const IslandMap& map = world.islands()[island].map;
    const std::uint8_t city = map.contains(x, y)
                                  ? map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).city
                                  : MapCell::kNoCity;
    const Variant v = beach_variant(buildings, map, type, x, y, rotation);
    Builder builder(world, buildings, island, nullptr);
    builder.write(*v.type, x, y, v.rotation, 0, city);
}

void fit_connection(World& world, const assets::BuildingTable& buildings, std::uint16_t island, std::int32_t x,
                    std::int32_t y) {
    if (island >= world.islands().size()) {
        return;
    }
    Builder builder(world, buildings, island, nullptr);
    builder.fit(x, y);
}

BuildPlan execute_build(World& world, const assets::BuildingTable& buildings, const BuildOrder& order,
                        const std::vector<std::string>& city_names, std::vector<ConstructionEvent>* events) {
    BuildPlan plan = plan_build(world, buildings, order);
    if (plan.placed == 0) {
        return plan;
    }
    Builder builder(world, buildings, order.island, events);
    std::uint8_t city = plan.city;
    Random& random = world.random(RandomStream::Construction);
    for (const PlannedBuild& item : plan.items) {
        if (!item.ok()) {
            continue;
        }
        const assets::BuildingType& b = *item.type;
        const std::uint32_t rnd = random.below(32);
        // Found the city with the first Kontor on the island.
        if (city == MapCell::kNoCity && b.kind == kind::kHq) {
            city = builder.found(order.player, item.x, item.y, city_names, buildings.resident_classes().size());
            if (city == MapCell::kNoCity) {
                continue;
            }
        }
        const Footprint f = footprint_of(b, item.x, item.y, item.rotation);
        builder.claim(b, item.x, item.y, item.rotation, city);
        const std::optional<Production> kept = builder.unregister(f);
        builder.register_building(b, item.x, item.y, city, rnd, item.upgrade ? kept : std::nullopt);
        // A treasure in the ground (deposit SCHATZ) goes to the builder, on every computer (M-008).
        std::optional<assets::IslandHeader::Deposit> treasure;
        auto& deposits = world.island(order.island).header.deposits;
        for (auto it = deposits.begin(); it != deposits.end(); ++it) {
            if (it->ware == ware::kTreasure && f.contains(it->x, it->y)) {
                if (Player* p = world.player(order.player)) {
                    p->money = money_plus(p->money, std::int64_t{it->amount});
                }
                treasure = *it;
                deposits.erase(it);
                break;
            }
        }
        builder.write(b, item.x, item.y, item.rotation, item.anim_phase, city);
        ConstructionEvent e;
        e.kind = ConstructionEvent::Kind::Placed;
        e.x = item.x;
        e.y = item.y;
        e.city = city;
        e.type = &b;
        e.production = world.production_index(order.island, static_cast<std::uint16_t>(item.x),
                                              static_cast<std::uint16_t>(item.y));
        builder.emit(e);
        if (treasure) {
            // 0x004084D0 shows "+1000" over the building and posts message 0x2E at the treasure.
            ConstructionEvent t = e;
            t.kind = ConstructionEvent::Kind::TreasureFound;
            t.production.reset();
            t.player = order.player;
            t.treasure = TilePos{treasure->x, treasure->y};
            t.money = treasure->amount;
            builder.emit(t);
        }
    }
    if (plan.takeover && city != MapCell::kNoCity) {
        // City_ChangeOwner: every tile of the city, and the city with its goods, go to the builder.
        IslandMap& map = world.island(order.island).map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                if (map.top(x, y).city == city) {
                    map.set_city(x, y, city, order.player);
                }
            }
        }
        if (City* c = world.city(order.island, city)) {
            c->owner = order.player;
            c->ledger.clear();  // the new owner's statistics start with the city
            c->history.clear();
        }
        ConstructionEvent e;
        e.kind = ConstructionEvent::Kind::CityTaken;
        e.city = city;
        builder.emit(e);
    }
    // Pay what was built: goods from the city (or the ship), gold from the player.
    const assets::BuildingTable::Materials& m = buildings.materials();
    if (Ship* found = order.ship ? world.ship(*order.ship) : nullptr) {
        Ship& ship = *found;
        const auto take = [&](std::uint8_t ware, std::uint32_t amount) {
            for (CargoSlot& c : ship.cargo) {
                if (amount == 0) {
                    break;
                }
                if (c.ware == ware) {
                    const std::uint32_t t = std::min(c.amount, amount);
                    c.amount -= t;
                    amount -= t;
                    if (c.amount == 0) {
                        c.ware = 0;
                    }
                }
            }
        };
        take(m.tools, plan.bill.tools);
        take(m.wood, plan.bill.wood);
        take(m.bricks, plan.bill.bricks);
    } else if (City* c = world.city(order.island, city)) {
        const auto take = [&](std::uint8_t ware, std::uint32_t amount) {
            if (ware < c->goods.size()) {
                const std::uint32_t taken = std::min(c->goods[ware], amount);
                c->goods[ware] -= taken;
                c->book(ware, GoodsFlow::Construction, taken);
            }
        };
        take(m.tools, plan.bill.tools);
        take(m.wood, plan.bill.wood);
        take(m.bricks, plan.bill.bricks);
        take(m.cannons, plan.bill.cannons);
    }
    if (Player* p = world.player(order.player)) {
        p->money = money_plus(p->money, -std::int64_t{plan.bill.money});
        // Counted limits; castle and cathedral raise the satisfaction of their city.
        const auto pride = [&] {
            if (City* c = world.city(order.island, city)) {
                c->satisfaction_bonus =
                    static_cast<std::uint16_t>(std::min<std::uint32_t>(0xFFFF, c->satisfaction_bonus + kPrideBonus));
            }
        };
        for (const PlannedBuild& item : plan.items) {
            if (!item.ok()) {
                continue;
            }
            switch (item.type->infra) {
                case kInfraCastle:
                    ++p->castles_built;
                    pride();
                    break;
                case kInfraCathedral:
                    ++p->cathedrals_built;
                    pride();
                    break;
                case kInfraArch:
                    // Op 0x4D 0x1000: the level holds while allowances are left.
                    add_count(p->arches_built, 1);
                    p->set_unlocked(kInfraArch, p->arches_allowed > p->arches_built);
                    break;
                case kInfraMonument:
                    add_count(p->monuments_built, 1);  // op 0x4D 0x2000
                    p->set_unlocked(kInfraMonument, p->monuments_allowed > p->monuments_built);
                    break;
                default:
                    break;
            }
        }
    }
    builder.finish();
    return plan;
}

std::vector<TilePos> plan_demolish(const World& world, const assets::BuildingTable& buildings,
                                   const DemolishOrder& order) {
    std::vector<TilePos> result;
    if (order.island >= world.islands().size()) {
        return result;
    }
    const IslandMap& map = world.islands()[order.island].map;
    const std::int32_t x0 = std::max(0, std::min(order.from.x, order.to.x));
    const std::int32_t y0 = std::max(0, std::min(order.from.y, order.to.y));
    const std::int32_t x1 = std::min(static_cast<std::int32_t>(map.width()) - 1, std::max(order.from.x, order.to.x));
    const std::int32_t y1 = std::min(static_cast<std::int32_t>(map.height()) - 1, std::max(order.from.y, order.to.y));
    const bool drag = x1 > x0 || y1 > y0;
    // Origins already looked at, one bit per tile of the island (MS-3: a rectangle over a whole
    // island visits every tile; a set of pairs made that several times slower).
    std::vector<bool> seen(drag ? std::size_t{map.width()} * map.height() : 0);
    for (std::int32_t y = y0; y <= y1; ++y) {
        for (std::int32_t x = x0; x <= x1; ++x) {
            const auto b = building_at(world, buildings, order.island, {x, y});
            if (!b) {
                continue;
            }
            // Whether the building is the player's depends only on the building: no need to note it.
            const assets::BuildingType& t = *b->type;
            if (t.kind == kind::kGround || b->city == MapCell::kNoCity) {
                continue;
            }
            const City* city = world.city(order.island, b->city);
            if (city == nullptr || city->owner != order.player) {
                continue;
            }
            if (drag) {
                if (b->origin.x < 0 || b->origin.y < 0 || static_cast<std::uint32_t>(b->origin.x) >= map.width() ||
                    static_cast<std::uint32_t>(b->origin.y) >= map.height()) {
                    continue;  // not a building of this island
                }
                const std::size_t bit =
                    static_cast<std::size_t>(b->origin.y) * map.width() + static_cast<std::size_t>(b->origin.x);
                if (seen[bit]) {
                    continue;
                }
                seen[bit] = true;
            }
            // Unchanged original terrain stays (trees and fields excepted).
            const MapCell& top =
                map.top(static_cast<std::uint32_t>(b->origin.x), static_cast<std::uint32_t>(b->origin.y));
            const MapCell& ground =
                map.ground(static_cast<std::uint32_t>(b->origin.x), static_cast<std::uint32_t>(b->origin.y));
            if (t.kind != kind::kForest && top.sprite == ground.sprite && top.rotation == ground.rotation) {
                continue;
            }
            if (t.kind == kind::kHq) {
                // The Kontor only as the last building of its city.
                std::size_t count = 0;
                for (const Production& p : world.productions()) {
                    if (p.used && p.island == order.island) {
                        const auto pb = building_at(world, buildings, order.island, {p.x, p.y});
                        count += pb && pb->city == b->city ? 1U : 0U;
                    }
                }
                for (const House& h : world.houses()) {
                    count += h.island == order.island && h.city == b->city ? 1U : 0U;
                }
                for (const CostBuilding& c : world.cost_buildings()) {
                    if (c.island == order.island) {
                        const auto cb = building_at(world, buildings, order.island, {c.x, c.y});
                        count += cb && cb->city == b->city ? 1U : 0U;
                    }
                }
                if (count >= 2) {
                    continue;
                }
            }
            if (drag) {
                // A rectangle leaves real buildings standing (0x00464AE0).
                if (std::find(kRealBuildings.begin(), kRealBuildings.end(), t.kind) != kRealBuildings.end()) {
                    continue;
                }
            }
            result.push_back(b->origin);
        }
    }
    return result;
}

DemolishLoss demolish_loss(const World& world, const assets::BuildingTable& buildings, const DemolishOrder& order) {
    DemolishLoss loss;
    const std::vector<TilePos> gone = plan_demolish(world, buildings, order);
    // Only markets, Kontors and towers hold land.
    const bool land = std::any_of(gone.begin(), gone.end(), [&](const TilePos& o) {
        const auto b = building_at(world, buildings, order.island, o);
        return b && (b->type->prod_kind == prod::kMarket || b->type->prod_kind == prod::kKontor ||
                     b->type->kind == kind::kTower || b->type->kind == kind::kTowerBeach);
    });
    if (!land) {
        return loss;
    }
    // The tiles recheck_territory would take from their city with nobody to give them to.
    const IslandMap& m = world.islands()[order.island].map;
    const std::vector<std::uint8_t> covered = territory_coverage(world, buildings, order.island, gone);
    std::set<std::pair<std::int32_t, std::int32_t>> counted;
    for (std::uint32_t y = 0; y < m.height(); ++y) {
        for (std::uint32_t x = 0; x < m.width(); ++x) {
            const MapCell& cell = m.top(x, y);
            if (cell.city == MapCell::kNoCity || cell.city >= 8) {
                continue;
            }
            const std::uint8_t bits = covered[std::size_t{y} * m.width() + x];
            if ((bits >> cell.city & 1U) != 0 ||
                covering_city(world, order.island, cell.city, bits) != MapCell::kNoCity) {
                continue;
            }
            const auto b = building_at(world, buildings, order.island,
                                       {static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
            if (!b || !territory_building(buildings, *b->type) ||
                std::find(gone.begin(), gone.end(), b->origin) != gone.end() ||
                !counted.insert({b->origin.x, b->origin.y}).second) {
                continue;
            }
            if (world.house(order.island, static_cast<std::uint16_t>(b->origin.x),
                            static_cast<std::uint16_t>(b->origin.y)) != nullptr) {
                ++loss.houses;
            } else if (std::find(kRealBuildings.begin(), kRealBuildings.end(), b->type->kind) != kRealBuildings.end()) {
                ++loss.buildings;
            }
        }
    }
    return loss;
}

DemolishResult execute_demolish(World& world, const assets::BuildingTable& buildings, const DemolishOrder& order,
                                std::vector<ConstructionEvent>* events, std::uint32_t refund_percent) {
    DemolishResult result;
    const std::vector<TilePos> origins = plan_demolish(world, buildings, order);
    if (origins.empty()) {
        return result;
    }
    refund_percent = std::min<std::uint32_t>(refund_percent, 100);
    const assets::BuildingTable::Materials& m = buildings.materials();
    Builder builder(world, buildings, order.island, events);
    for (const TilePos& o : origins) {
        const auto b = building_at(world, buildings, order.island, o);
        if (!b || b->origin.x != o.x || b->origin.y != o.y) {
            continue;
        }
        if (refund_percent != 0) {
            // Game setting: a share of the costs comes back (the original gives nothing). Goods
            // go into the city's warehouse while there is room; what does not fit is lost.
            const auto& cost = ordered_cost(buildings, *b->type);
            const auto share = [&](std::uint32_t amount) {
                return static_cast<std::uint32_t>(std::uint64_t{amount} * refund_percent / 100);
            };
            if (City* city = world.city(order.island, b->city)) {
                const auto give = [&](std::uint8_t ware, std::uint32_t amount, std::uint32_t& booked) {
                    if (amount == 0) {
                        return;
                    }
                    city->ensure_ware(ware);
                    std::uint32_t room = amount;
                    if (city->capacity != 0) {
                        room = city->capacity > city->goods[ware] ? city->capacity - city->goods[ware] : 0;
                    }
                    const std::uint32_t added = std::min(amount, room);
                    city->goods[ware] += added;
                    city->book(ware, GoodsFlow::Returned, added);
                    booked += added;
                };
                give(m.tools, share(cost.tools), result.refund.tools);
                give(m.wood, share(cost.wood), result.refund.wood);
                give(m.bricks, share(cost.bricks), result.refund.bricks);
                give(m.cannons, share(cost.cannons), result.refund.cannons);
            }
            if (Player* p = world.player(order.player); p != nullptr && cost.money > 0) {
                const auto gold = static_cast<std::int32_t>(std::int64_t{cost.money} * refund_percent / 100);
                p->money = money_plus(p->money, std::int64_t{gold});
                result.refund.money += gold;
            }
        }
        const Footprint f{o.x, o.y, b->width, b->height};
        builder.remove(*b);
        builder.restore_ground(f, b->city);
        ++result.removed;
    }
    builder.finish();
    return result;
}

bool destroy_building(World& world, const assets::BuildingTable& buildings, std::uint16_t island, TilePos origin,
                      std::vector<ConstructionEvent>* events) {
    if (island >= world.islands().size()) {
        return false;
    }
    const auto b = building_at(world, buildings, island, origin);
    if (!b || b->origin.x != origin.x || b->origin.y != origin.y) {
        return false;
    }
    const MapCell& cell =
        world.islands()[island].map.top(static_cast<std::uint32_t>(origin.x), static_cast<std::uint32_t>(origin.y));
    const bool natives = b->city != MapCell::kNoCity && world.acts_as(cell.owner, FactionBehaviour::Natives);
    Builder builder(world, buildings, island, events);
    builder.destroy(*b);
    if (natives && !world.original_bugs().native_land && native_village_gone(world, buildings, island, b->city)) {
        // Open1602: a village without its chief's house, market and huts is gone, and its land is
        // free for anybody's market; as in the original (OriginalBugs::native_land) the ruin of the
        // chief's house holds it like a destroyed Kontor.
        builder.dissolve_city(b->city);
    }
    builder.finish();
    return true;
}

bool native_village_gone(const World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                         std::uint8_t city) {
    if (island >= world.islands().size() || world.city(island, city) == nullptr ||
        !city_defenceless(world, buildings, island, city)) {
        return false;
    }
    const IslandMap& map = world.islands()[island].map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const MapCell& c = map.top(x, y);
            if (c.empty() || c.city != city) {
                continue;
            }
            const assets::BuildingType* t = buildings.by_sprite(c.sprite);
            if (t != nullptr && t->prod_kind == prod::kPirateHouse && !t->destroyed) {
                return false;  // a hut still stands
            }
        }
    }
    return true;
}

std::optional<BuildOrder> plant_fields_order(const World& world, const assets::BuildingTable& buildings,
                                             PlayerId player, std::uint16_t island, TilePos origin, std::uint32_t field,
                                             std::uint32_t seed) {
    if (island >= world.islands().size()) {
        return std::nullopt;
    }
    const IslandMap& map = world.islands()[island].map;
    if (!map.contains(origin.x, origin.y)) {
        return std::nullopt;
    }
    const MapCell& cell = map.top(static_cast<std::uint32_t>(origin.x), static_cast<std::uint32_t>(origin.y));
    const assets::BuildingType* farm = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
    const assets::BuildingType* crop = buildings.by_id(field);
    if (farm == nullptr || crop == nullptr || cell.owner != player || farm->radius == 0 ||
        farm->raw_ware < ware::kFirstField || farm->raw_ware > ware::kTree || farm->prod_kind == prod::kRipe ||
        farm->prod_kind == prod::kGrowing || crop_ware(buildings, *crop) != farm->raw_ware) {
        return std::nullopt;
    }
    const Catchment area = catchment_of(*farm, cell.rotation, origin);
    const std::vector<std::uint8_t>& half = circle_table(area.radius);
    const bool clears_forest = farm->raw_ware != ware::kTree;
    struct Spot {
        std::int32_t d = 0;
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    std::vector<Spot> spots;
    for (std::int32_t y = area.min().y; y < area.min().y + static_cast<std::int32_t>(area.height()); ++y) {
        for (std::int32_t x = area.min().x; x < area.min().x + static_cast<std::int32_t>(area.width()); ++x) {
            std::int32_t dx = x - area.centre.x;
            std::int32_t dy = y - area.centre.y;
            dx = dx > 0 ? dx - area.parity_x : -dx;
            dy = dy > 0 ? dy - area.parity_y : -dy;
            if (dy > area.radius || (area.radius > 1 && dx > half[static_cast<std::size_t>(dy)]) ||
                !map.contains(x, y)) {
                continue;
            }
            // Open ground, and forest cleared for a crop or pasture; a woodcutter keeps the trees
            // that stand. Roads, meadows, the fields of other farms and buildings stay as they are.
            const MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
            const bool forest = b != nullptr && b->kind == kind::kForest && crop_ware(buildings, *b) == ware::kTree;
            if (b == nullptr || (b->kind != kind::kGround && (!forest || !clears_forest))) {
                continue;
            }
            spots.push_back(Spot{dx * dx + dy * dy, x, y});
        }
    }
    std::stable_sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.d < b.d; });
    BuildOrder order;
    order.player = player;
    order.island = island;
    order.orient_to_road = false;
    // Trees come in RandAnz variants RandAdd records apart; like the build tool, one per tile
    // (a splitmix64 of seed, island and tile), so the order carries them for every computer.
    const auto variant = [&](std::int32_t x, std::int32_t y) {
        if (crop->kind != kind::kForest || crop->rand_count <= 1) {
            return field;
        }
        std::uint64_t z = (std::uint64_t{seed} << 32U) ^ (std::uint64_t{island} << 24U) ^
                          (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 12U) ^
                          static_cast<std::uint32_t>(y);
        z += 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        const auto pick = static_cast<std::uint32_t>(z ^ (z >> 31U)) % crop->rand_count;
        const assets::BuildingType* v = buildings.by_record(crop->record + pick * crop->rand_add);
        return v != nullptr ? v->id : field;
    };
    for (const Spot& s : spots) {
        order.items.push_back(BuildItem{variant(s.x, s.y), s.x, s.y, 0, 0});
    }
    return order;
}

}  // namespace o1602::sim
