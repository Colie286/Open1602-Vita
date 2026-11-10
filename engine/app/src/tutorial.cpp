// SPDX-License-Identifier: GPL-3.0-or-later

#include "tutorial.hpp"

#include <algorithm>
#include <numeric>

#include "o1602/sim/construction.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/logistics.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::app {

namespace {

using View = TutorialView;
using Facts = TutorialFacts;

constexpr std::array<std::int32_t, Tutorial::kChapters> kFirstPage{0, 9, 19, 28, 36};
constexpr std::array<std::int32_t, Tutorial::kChapters> kLastPage{8, 18, 27, 35, 42};
constexpr std::array<std::uint16_t, Tutorial::kChapters> kSpeech{1023, 1046, 1073, 1114, 1098};

// Chapter 1: the buildings by HAUS Id and the build menu's categories they are in (the original's
// table in 0x00431500, docs/re/tutorial.md).
constexpr std::uint32_t kForester = sim::id::kForester;
constexpr std::uint32_t kFisher = sim::id::kFisher;
constexpr std::uint32_t kSheepFarm = sim::id::kSheepFarm;
constexpr std::int32_t kRoads = 31002;
constexpr std::int32_t kCrafts = 31005;
constexpr std::int32_t kFarms = 31006;
constexpr std::int32_t kHarbour = 31007;
constexpr std::int32_t kPublic = 31008;
constexpr std::int32_t kFirstCategoryButton = 31001;

// Chapter 1's steps (pages).
constexpr std::int32_t kStepForester = 9;
constexpr std::int32_t kStepFisher = 10;
constexpr std::int32_t kStepRoads = 11;
constexpr std::int32_t kStepHouses = 12;
constexpr std::int32_t kStepLookAtHouse = 13;
constexpr std::int32_t kStepSheepFarm = 14;
constexpr std::int32_t kStepWeaver = 15;
constexpr std::int32_t kStepWeaverRoad = 16;
constexpr std::int32_t kStepMarket = 17;
constexpr std::int32_t kStepDone = 18;
constexpr std::uint32_t kHousesWanted = 4;

constexpr std::uint8_t kRoadRadius = 70;  // the flood from the Kontor (0x004310C0)
constexpr std::uint32_t kTon = sim::kStockUnitsPerTon;
constexpr std::uint32_t kBricksLeft = 5 * kTon;
constexpr std::uint32_t kHalfHealth = 64;   // of 128
constexpr std::uint32_t kLowHealth = 0x3A;  // 45 %
constexpr std::uint8_t kCannonsWanted = 4;
constexpr std::uint16_t kBattleIsland = 0;  // chapter 4 fights on the first island

std::vector<std::uint8_t> one(std::uint8_t b) {
    return {b};
}

bool moving(const sim::Ship& ship) noexcept {
    const sim::ShipMotion& m = ship.motion;
    return m.phase == sim::ShipMotion::Phase::Run || m.phase == sim::ShipMotion::Phase::Turn ||
           m.path_index < m.path.size();
}

bool has_city_on(const sim::World& world, std::uint16_t island, sim::PlayerId player) {
    return std::any_of(world.cities().begin(), world.cities().end(),
                       [&](const sim::City& c) { return c.island == island && c.owner == player; });
}

/// Ship_ContextActions (0x0044E630) for a ship of the human that lies still.
std::uint8_t ship_actions(const sim::Simulation& simulation, const assets::BuildingTable& buildings,
                          const sim::Ship& ship) {
    const sim::World& w = simulation.world();
    std::uint8_t m = 0;
    if (simulation.fleet().island_at_ship(ship)) {
        m |= ship_action::kIsland;
        if (simulation.fleet().explore_state(ship, ship.owner) == sim::ExploreState::Done) {
            m |= ship_action::kExplored;
        }
    }
    if (const auto index = sim::kontor_in_reach(w, buildings, ship.x, ship.y)) {
        const sim::City& city = w.cities()[*index];
        const sim::Player* owner = w.player(city.owner);
        if (owner != nullptr && w.behaviour(*owner) == sim::FactionBehaviour::Pirates) {
            m = static_cast<std::uint8_t>((m & ~ship_action::kIsland) | ship_action::kPirates);
        } else if (city.owner == ship.owner || w.diplomacy().may_trade(ship.owner, city.owner)) {
            m |= ship_action::kTrade;
            if (city.owner == ship.owner) {
                m |= ship_action::kOwnCity;
            }
        }
    } else if (const auto site = sim::kontor_site_near(w, buildings, ship.x, ship.y)) {
        // Land within 4 tiles of a ship in an island's rectangle (0x00448EF0): an island without a
        // city of the owner can take a Kontor; otherwise the coast may be the owner's own.
        if (!has_city_on(w, site->island, ship.owner)) {
            m |= ship_action::kFound;
        } else if (w.islands()[site->island].map.top(site->x, site->y).owner == ship.owner) {
            m |= ship_action::kOwnCity;
        }
    }
    for (const sim::CostBuilding& b : w.cost_buildings()) {
        if (b.kind == sim::CostBuilding::Kind::Shipyard && b.island < w.islands().size() &&
            w.islands()[b.island].map.top(b.x, b.y).owner == ship.owner &&
            sim::building_in_reach(w, buildings, b.island, b.x, b.y, ship.x, ship.y)) {
            m = static_cast<std::uint8_t>((m & ~ship_action::kIsland) | ship_action::kShipyard);
            break;
        }
    }
    return m;
}

Facts::ShipState ship_state(const sim::Simulation& simulation, const assets::BuildingTable& buildings,
                            const assets::FigureTable& figures, std::uint32_t id, sim::PlayerId human) {
    Facts::ShipState s;
    const sim::World& w = simulation.world();
    const sim::Ship* ship = w.ship(id);
    if (ship == nullptr || ship->owner != human || ship->sinking) {
        return s;
    }
    s.exists = true;
    s.moving = moving(*ship);
    s.loaded = ship->total_load() != 0;
    s.actions = ship_actions(simulation, buildings, *ship);
    if (const auto island = simulation.fleet().island_at_ship(*ship)) {
        s.island_ore = sim::island_fertility(w.islands()[*island], sim::ware::kOre) != 0;
        s.island_city =
            std::any_of(w.cities().begin(), w.cities().end(), [&](const sim::City& c) { return c.island == *island; });
    }
    s.cloth = ship->load(sim::ware::kCloth);
    s.bricks = ship->load(sim::ware::kBricks);
    const auto health = ui::unit_health(w, figures, ui::UnitRef{ui::UnitRef::Kind::Ship, id});
    s.health = static_cast<std::uint32_t>(std::clamp(health.value_or(1.0), 0.0, 1.0) * 128.0);
    s.cannons = ship->cannons;
    if (ship->target.kind == sim::Target::Kind::Ship) {
        s.attacking = true;
        const sim::Ship* other = w.ship(ship->target.ship);
        s.attacking_partner = other != nullptr && other->owner == Tutorial::kPartner;
    }
    s.patrolling = ship->route.patrol();
    return s;
}

/// The largest city of a player (0x00480580): island and number.
std::optional<std::pair<std::uint16_t, std::uint8_t>> largest_city(const sim::World& world, sim::PlayerId player) {
    std::optional<std::pair<std::uint16_t, std::uint8_t>> best;
    std::uint64_t most = 0;
    for (const sim::City& c : world.cities()) {
        const std::uint64_t n = std::accumulate(c.inhabitants.begin(), c.inhabitants.end(), std::uint64_t{0});
        if (c.owner == player && (!best || n > most)) {
            best = std::make_pair(c.island, c.index);
            most = n;
        }
    }
    return best;
}

/// The human's buildings on the island of his town, and which of the forester's lodges, fisher's
/// huts and weaver's shops a market cart from the Kontor reaches on roads within 70 tiles.
Facts::Settlement settlement(const sim::World& world, const assets::BuildingTable& buildings, const sim::City& town,
                             sim::PlayerId human) {
    Facts::Settlement s;
    const sim::IslandMap& map = world.islands()[town.island].map;
    struct Site {
        std::uint32_t id;
        sim::TilePos origin;
        std::uint32_t width;
        std::uint32_t height;
    };
    std::vector<Site> sites;  // the buildings whose connection counts
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const sim::MapCell& cell = map.top(x, y);
            if (cell.empty() || cell.owner != human) {
                continue;
            }
            const sim::SubTile t = map.sub_tile(buildings, x, y);
            if (t.building == nullptr || t.origin_x != static_cast<std::int32_t>(x) ||
                t.origin_y != static_cast<std::int32_t>(y)) {
                continue;  // counted at its origin
            }
            const assets::BuildingType& b = *t.building;
            if (b.prod_kind == sim::prod::kHouse) {
                ++s.houses;
            } else if (b.prod_kind == sim::prod::kMarket) {
                ++s.markets;
            }
            std::uint32_t* count = b.id == kForester            ? &s.foresters
                                   : b.id == kFisher            ? &s.fishers
                                   : b.id == kSheepFarm         ? &s.sheep_farms
                                   : b.id == sim::id::kWebstube ? &s.weavers
                                                                : nullptr;
            if (count == nullptr) {
                continue;
            }
            ++*count;
            if (b.id != kSheepFarm) {
                std::uint32_t w = 1;
                std::uint32_t h = 1;
                sim::rotated_size(b, cell.rotation, w, h);
                sites.push_back(Site{b.id, sim::TilePos{t.origin_x, t.origin_y}, w, h});
            }
        }
    }
    if (sites.empty() || !town.warehouse_x || !town.warehouse_y) {
        return s;
    }
    const auto kontor =
        sim::building_at(world, buildings, town.island, sim::TilePos{*town.warehouse_x, *town.warehouse_y});
    if (!kontor) {
        return s;
    }
    sim::Catchment area = sim::catchment_of(*kontor->type, kontor->rotation, kontor->origin);
    area.radius = kRoadRadius;
    sim::PathFinder finder;
    finder.begin(map, buildings, area, sim::WalkClass::Roads, 0);
    for (const Site& site : sites) {
        for (std::uint32_t j = 0; j < site.height; ++j) {
            for (std::uint32_t i = 0; i < site.width; ++i) {
                finder.set(sim::TilePos{site.origin.x + static_cast<std::int32_t>(i),
                                        site.origin.y + static_cast<std::int32_t>(j)},
                           sim::PathFinder::Cell::Goal);
            }
        }
    }
    finder.open_footprint(kontor->origin, kontor->width, kontor->height);
    (void)finder.search(sim::centre_tile(*kontor), [](sim::TilePos) { return sim::PathFinder::Answer::Remember; });
    for (const Site& site : sites) {
        bool reached = false;
        for (std::uint32_t j = 0; j < site.height && !reached; ++j) {
            for (std::uint32_t i = 0; i < site.width && !reached; ++i) {
                reached = finder.reached(sim::TilePos{site.origin.x + static_cast<std::int32_t>(i),
                                                      site.origin.y + static_cast<std::int32_t>(j)});
            }
        }
        bool& connected = site.id == kForester ? s.foresters_connected
                          : site.id == kFisher ? s.fishers_connected
                                               : s.weavers_connected;
        connected = connected || reached;
    }
    return s;
}

/// What a partner's Kontor still buys or sells of a ware, 1/32 t.
std::uint32_t buys(const sim::City& city, std::uint8_t ware) {
    const sim::City::Trade* t = city.trade_of(ware);
    const std::uint32_t stock = city.stock(ware);
    return t != nullptr && t->buy && t->limit > stock ? t->limit - stock : 0;
}
std::uint32_t sells(const sim::City& city, std::uint8_t ware) {
    const sim::City::Trade* t = city.trade_of(ware);
    const std::uint32_t stock = city.stock(ware);
    return t != nullptr && t->sell && stock > t->keep ? stock - t->keep : 0;
}

/// The human's land units of a kind on the battle island, and how many attack another's unit.
Facts::Units units_of(const sim::World& world, const assets::FigureTable& figures, sim::PlayerId human,
                      std::int32_t kind) {
    Facts::Units u;
    for (const sim::Soldier& s : world.soldiers()) {
        const assets::FigureType* f = figures.get(s.figure);
        if (s.owner != human || s.island != kBattleIsland || s.dying() || f == nullptr || f->kind != kind) {
            continue;
        }
        ++u.total;
        if (s.target.kind == sim::Target::Kind::Soldier) {
            const sim::Soldier* enemy = world.soldier(s.target.ship);
            u.attacking += enemy != nullptr && enemy->owner != human ? 1U : 0U;
        }
    }
    return u;
}

/// Some of the units do not attack yet.
bool idle(const Facts::Units& u) noexcept {
    return u.attacking < u.total;
}

}  // namespace

Tutorial::Tutorial(std::uint8_t chapter)
    : m_chapter(std::min<std::uint8_t>(chapter, kChapters - 1)), m_page(kFirstPage[m_chapter]) {}

void Tutorial::start(const sim::World& world, sim::PlayerId human) {
    m_town = largest_city(world, human);
    m_partner_town = largest_city(world, kPartner);
    m_ships.clear();
    for (const sim::Ship& s : world.ships()) {
        if (s.owner == human) {
            m_ships.push_back(s.id);
        }
    }
}

void Tutorial::finish() noexcept {
    m_done = true;
    m_page = kLastPage[m_chapter];
}

void Tutorial::page_forward() noexcept {
    if (can_page_forward()) {
        ++m_page;
    }
}

std::int32_t Tutorial::first_page(std::uint8_t chapter) noexcept {
    return kFirstPage[std::min<std::uint8_t>(chapter, kChapters - 1)];
}

std::int32_t Tutorial::last_page(std::uint8_t chapter) noexcept {
    return kLastPage[std::min<std::uint8_t>(chapter, kChapters - 1)];
}

std::uint16_t Tutorial::speech(std::uint8_t chapter) noexcept {
    return kSpeech[std::min<std::uint8_t>(chapter, kChapters - 1)];
}

std::optional<std::uint8_t> Tutorial::next_chapter(std::uint8_t chapter) noexcept {
    if (chapter + 1 >= kChapters) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(chapter + 1);
}

TutorialFacts Tutorial::facts(const TutorialView& view, const sim::Simulation& simulation,
                              const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                              sim::PlayerId human) const {
    Facts f;
    const sim::World& w = simulation.world();
    auto ship = [&](std::size_t i) { return i < m_ships.size() ? m_ships[i] : 0U; };
    // A town founded after the start (chapter 0) counts from then on.
    const auto town_at = m_town ? m_town : largest_city(w, human);
    const sim::City* town = town_at ? w.city(town_at->first, town_at->second) : nullptr;
    const auto partner_at = m_partner_town ? m_partner_town : largest_city(w, kPartner);
    const sim::City* partner = partner_at ? w.city(partner_at->first, partner_at->second) : nullptr;
    switch (m_chapter) {
        case 0:
            if (view.window == View::Window::Ship) {
                f.ships[0] = ship_state(simulation, buildings, figures, view.ship, human);
            }
            break;
        case 1:
            if (town != nullptr) {
                f.settlement = settlement(w, buildings, *town, human);
            }
            break;
        case 2: {
            f.ships[0] = ship_state(simulation, buildings, figures, ship(0), human);
            const sim::Treaty trade = w.diplomacy().relation(human, kPartner).trade;
            f.treaty_offered = trade != sim::Treaty::None;
            f.treaty_signed = trade == sim::Treaty::Signed;
            f.town_cloth = town != nullptr ? town->stock(sim::ware::kCloth) : 0;
            if (partner != nullptr) {
                f.partner_cloth = partner->stock(sim::ware::kCloth);
                f.partner_buys_cloth = buys(*partner, sim::ware::kCloth);
                f.partner_sells_bricks = sells(*partner, sim::ware::kBricks);
            }
            break;
        }
        case 3:
            for (std::size_t i = 0; i < f.ships.size(); ++i) {
                f.ships[i] = ship_state(simulation, buildings, figures, ship(i), human);
            }
            f.partner_ships =
                static_cast<std::uint32_t>(std::count_if(w.ships().begin(), w.ships().end(), [](const sim::Ship& s) {
                    return s.owner == kPartner && !s.sinking;
                }));
            break;
        default:
            f.ships[0] = ship_state(simulation, buildings, figures, ship(0), human);
            f.riders = units_of(w, figures, human, sim::figtype::kRider);
            f.musketeers = units_of(w, figures, human, sim::figtype::kMusketeer);
            f.swordsmen = units_of(w, figures, human, sim::figtype::kSwordsman);
            f.partner_units = static_cast<std::uint32_t>(std::count_if(
                w.soldiers().begin(), w.soldiers().end(),
                [](const sim::Soldier& s) { return s.owner == kPartner && s.island == kBattleIsland && !s.dying(); }));
            for (const ui::UnitRef& r : view.units) {
                const sim::Soldier* s = r.kind == ui::UnitRef::Kind::Soldier ? w.soldier(r.id) : nullptr;
                const assets::FigureType* type = s != nullptr ? figures.get(s->figure) : nullptr;
                f.selected_riders += type != nullptr && type->kind == sim::figtype::kRider ? 1U : 0U;
            }
            break;
    }
    return f;
}

std::int32_t Tutorial::settle_step(const TutorialFacts::Settlement& s) const noexcept {
    if (s.foresters == 0) {
        return kStepForester;
    }
    if (s.fishers == 0) {
        return kStepFisher;
    }
    if (!s.foresters_connected || !s.fishers_connected) {
        return kStepRoads;
    }
    if (s.houses < kHousesWanted) {
        return kStepHouses;
    }
    if (!m_house_seen) {
        return kStepLookAtHouse;
    }
    if (s.sheep_farms == 0) {
        return kStepSheepFarm;
    }
    if (s.weavers == 0) {
        return kStepWeaver;
    }
    if (!s.weavers_connected) {
        return kStepWeaverRoad;
    }
    return s.markets == 0 ? kStepMarket : kStepDone;
}

std::vector<std::uint8_t> Tutorial::step(const TutorialView& view, const TutorialFacts& facts) {
    std::vector<std::uint8_t> out;
    if (m_done) {
        m_page = kLastPage[m_chapter];
        return one(bubble::kEnd);
    }
    if (can_page_forward()) {
        return {bubble::kWorldMap, bubble::kZoom, bubble::kCompass, bubble::kNext, bubble::kEnd};
    }
    // Chapter 1 shows the page of its step in every mode.
    const std::int32_t settle = m_chapter == 1 ? settle_step(facts.settlement) : 0;
    if (m_chapter == 1) {
        m_page = settle;
    }
    const bool battle = m_chapter >= 3;
    switch (view.mode) {
        case View::Mode::Military:
            if (m_chapter == 3) {
                sea_battle(view, facts, out);
            } else if (m_chapter == 4) {
                land_battle(view, facts, out);
            } else if (m_chapter == 1) {
                out.push_back(settle == kStepLookAtHouse ? bubble::kInfoMode : bubble::kBuildMode);
            } else {
                out.push_back(bubble::kInfoMode);
            }
            break;
        case View::Mode::Info:
            if (battle) {
                out.push_back(bubble::kMilitaryMode);
            } else if (m_chapter == 0) {
                info_discover(view, facts, out);
            } else if (m_chapter == 1) {
                info_settle(view, settle, out);
            } else {
                info_trade(view, facts, out);
            }
            break;
        case View::Mode::Build:
            if (battle) {
                out.push_back(bubble::kMilitaryMode);
            } else if (m_chapter == 1) {
                build_settle(view, settle, out);
            } else {
                out.push_back(bubble::kInfoMode);
            }
            if (view.short_of_wood) {
                out.push_back(bubble::kShortOfWood);
            }
            break;
        case View::Mode::Diplomacy:
            if (battle) {
                out.push_back(bubble::kMilitaryMode);
            } else if (m_chapter == 1) {
                // The original compares with page 14 here (U-038): page 13 asks for the info mode.
                out.push_back(settle == kStepLookAtHouse ? bubble::kInfoMode : bubble::kBuildMode);
            } else if (m_chapter == 2) {
                if (!facts.treaty_offered) {
                    out.push_back(bubble::kOfferTreaty);
                    m_page = 19;
                } else if (facts.treaty_signed) {
                    out.push_back(bubble::kRightMouse);
                    m_page = 20;
                }
            } else {
                out.push_back(bubble::kInfoMode);
            }
            break;
        case View::Mode::Settings:
            if (battle) {
                out.push_back(bubble::kMilitaryMode);
            } else if (m_chapter == 1) {
                out.push_back(settle == kStepLookAtHouse ? bubble::kRightMouse : bubble::kBuildMode);  // U-038
            } else {
                out.push_back(bubble::kInfoMode);
            }
            break;
    }
    return out;
}

void Tutorial::info_discover(const TutorialView& view, const TutorialFacts& facts, std::vector<std::uint8_t>& out) {
    const Facts::ShipState& ship = facts.ships[0];
    if (view.window != View::Window::Ship || !ship.exists) {
        out.push_back(bubble::kClickShip);
        return;
    }
    if (ship.island_city && ship.island_ore && !ship.loaded) {
        finish();  // the Kontor stands on an island with ore and the cargo is in it
        out.push_back(bubble::kEnd);
        return;
    }
    if (ship.moving) {
        return;
    }
    const std::uint8_t m = ship.actions;
    const bool ore_known = (m & ship_action::kExplored) != 0 && ship.island_ore;
    if (view.ship_page != View::ShipPage::Main) {
        if (view.ship_page == View::ShipPage::Transfer && ore_known) {
            if (ship.loaded) {
                out.push_back(bubble::kIntoKontor);
            }
            m_page = 7;
        } else {
            out.push_back(bubble::kRightMouse);
        }
        return;
    }
    if ((m & ship_action::kExplored) == 0) {
        if ((m & ship_action::kIsland) != 0) {
            out.push_back(bubble::kExplore);
            m_page = 2;
        } else if (m == 0) {
            out.push_back(bubble::kSailToIsland);
            m_page = 1;
        }
        return;
    }
    if (!ship.island_ore) {
        out.push_back(bubble::kNoOre);
        m_page = 3;
        return;
    }
    if ((m & ship_action::kOwnCity) != 0) {
        if ((m & ship_action::kTrade) != 0 && ship.loaded) {
            out.push_back(bubble::kTransferButton);
            m_page = 6;
        } else {
            out.push_back(bubble::kToKontor);
            m_page = 5;
        }
        return;
    }
    if ((m & ship_action::kFound) != 0) {
        out.push_back(bubble::kStatusLine);
        out.push_back(bubble::kFoundKontor);
        m_page = 4;
    }
}

void Tutorial::info_settle(const TutorialView& view, std::int32_t step, std::vector<std::uint8_t>& out) {
    const bool house = view.window == View::Window::OwnHouse;
    if (step == kStepLookAtHouse) {
        if (house) {
            out.push_back(bubble::kBuildMode);
            out.push_back(bubble::kNeedCloth);
            m_house_seen = true;
        } else {
            out.push_back(bubble::kClickHouse);
        }
    } else if (step == kStepSheepFarm || step == kStepWeaver) {
        if (house) {
            out.push_back(bubble::kNeedCloth);
        }
        out.push_back(bubble::kBuildMode);
    } else {
        out.push_back(bubble::kBuildMode);
    }
}

void Tutorial::build_settle(const TutorialView& view, std::int32_t step, std::vector<std::uint8_t>& out) {
    // The building to place next, its button in the category's pop-up and the category.
    struct Want {
        std::uint32_t building;
        std::int32_t category;
        std::uint8_t bubble;
    };
    std::optional<Want> want;
    switch (step) {
        case kStepForester:
            want = Want{kForester, kFarms, bubble::kForester};
            break;
        case kStepFisher:
            want = Want{kFisher, kHarbour, bubble::kFisher};
            break;
        case kStepRoads:
        case kStepWeaverRoad:
            want = Want{sim::id::kRoadDirt, kRoads, bubble::kRoad};
            break;
        case kStepHouses:
            want = Want{sim::id::kHouse, kPublic, bubble::kHouse};
            break;
        case kStepLookAtHouse:
            out.push_back(bubble::kInfoMode);
            break;
        case kStepSheepFarm:
            want = Want{kSheepFarm, kFarms, bubble::kSheepFarm};
            break;
        case kStepWeaver:
            want = Want{sim::id::kWebstube, kCrafts, bubble::kWeaver};
            break;
        case kStepMarket:
            want = Want{sim::id::kMarket, kPublic, bubble::kMarket};
            break;
        default:
            finish();  // the market place stands
            break;
    }
    if (want && view.menu_building != want->building) {
        if (!view.popup_open) {
            out.push_back(static_cast<std::uint8_t>(bubble::kFirstCategory + want->category - kFirstCategoryButton));
        } else if (view.popup_category == want->category) {
            out.push_back(want->bubble);
        }
    }
    if (view.holding_building) {
        out.push_back(bubble::kLetGo);
    }
}

void Tutorial::info_trade(const TutorialView& view, const TutorialFacts& facts, std::vector<std::uint8_t>& out) {
    const Facts::ShipState& ship = facts.ships[0];
    const bool at_kontor = !ship.moving && (ship.actions & ship_action::kTrade) != 0;
    const bool at_own = !ship.moving && (ship.actions & ship_action::kOwnCity) != 0;
    const View::Window window = view.window;
    const bool ship_window = window == View::Window::Ship;
    if (!facts.treaty_offered) {
        out.push_back(window == View::Window::None ? bubble::kDiplomacy : bubble::kRightMouse);
        m_page = 19;
    } else if (!facts.treaty_signed) {
        m_page = 19;
    } else if (!m_partner_seen) {
        if (window == View::Window::PartnerKontor) {
            m_partner_seen = true;
        } else {
            out.push_back(bubble::kPartnerKontor);
        }
        m_page = 20;
    } else if (ship.moving) {
        // Under way: nothing changes.
    } else if (!m_cloth_loaded) {
        if (window == View::Window::PartnerKontor) {
            out.push_back(bubble::kClickShip);
            m_page = 20;
        } else if (!ship_window) {
            out.push_back(bubble::kClickShip);
            m_page = 21;
        } else if (!at_own) {
            out.push_back(bubble::kToKontor);
            m_page = 21;
        } else if (view.ship_page == View::ShipPage::Transfer) {
            if (facts.town_cloth < kTon) {
                m_cloth_loaded = true;
                out.push_back(bubble::kSailToPartner);
                m_page = 23;
            } else {
                if (!view.consumer_goods) {
                    out.push_back(bubble::kConsumerGoods);
                } else {
                    if (view.kontor_ware != sim::ware::kCloth) {
                        out.push_back(bubble::kScrollGoods);
                    }
                    if (ship.cloth == 0) {
                        out.push_back(bubble::kOntoShip);
                    }
                }
                m_page = 22;
            }
        } else {
            out.push_back(bubble::kTransferButton);
            m_page = 21;
        }
    } else if (!m_bricks_bought) {
        if (!at_kontor || at_own) {
            out.push_back(bubble::kSailToPartner);
            m_page = 23;
        } else if (!ship_window) {
            out.push_back(bubble::kClickShip);
            m_page = 23;
        } else if (view.ship_page != View::ShipPage::Trade) {
            out.push_back(bubble::kTradeButton);
            m_page = 24;
        } else if (ship.cloth != 0 && facts.partner_buys_cloth >= kTon) {
            if (facts.partner_cloth < kTon) {
                out.push_back(bubble::kSell);
            }
            m_page = 24;
        } else if (ship.bricks != 0 && facts.partner_sells_bricks < kBricksLeft) {
            m_bricks_bought = true;
            m_page = 26;
        } else {
            if (ship.bricks == 0) {
                out.push_back(bubble::kBuy);
            }
            m_page = 25;
        }
    } else if (ship.bricks == 0) {
        finish();  // the bricks are in the own Kontor
    } else if (!at_own) {
        out.push_back(bubble::kSailHome);
        m_page = 26;
    } else {
        out.push_back(!ship_window                                 ? bubble::kClickShip
                      : view.ship_page == View::ShipPage::Transfer ? bubble::kIntoKontor
                                                                   : bubble::kTransferButton);
        m_page = 26;
    }
}

bool Tutorial::alone(const TutorialView& view, std::size_t ship) const {
    return view.units.size() == 1 && view.units.front().kind == ui::UnitRef::Kind::Ship && ship < m_ships.size() &&
           view.units.front().id == m_ships[ship];
}

void Tutorial::sea_battle(const TutorialView& view, const TutorialFacts& facts, std::vector<std::uint8_t>& out) {
    const Facts::ShipState& large = facts.ships[0];
    if (facts.partner_ships == 0) {
        if (facts.ships[1].patrolling || facts.ships[2].patrolling) {
            finish();
            out.push_back(bubble::kEnd);
        } else if (view.units.size() == 1 && !alone(view, 0)) {
            const bool point = view.picking_patrol;
            out.push_back(point ? bubble::kPatrolPoint : bubble::kPatrolButton);
            m_page = point ? 34 : 33;
        } else {
            out.push_back(bubble::kSelectSmall);
            m_page = 32;
        }
        return;
    }
    if (large.attacking || large.health < kHalfHealth) {
        // The large ship fights or has had enough: the small ones are to help once it is low.
        auto idle_small = [](const Facts::ShipState& s) { return s.exists && !s.attacking_partner; };
        if (large.health < kLowHealth && (idle_small(facts.ships[1]) || idle_small(facts.ships[2]))) {
            out.push_back(bubble::kAttackWithSmall);
            m_page = 31;
        }
    } else if (large.cannons < kCannonsWanted) {
        const bool selected = alone(view, 0);
        out.push_back(selected ? bubble::kCannons : bubble::kClickLargeShip);
        m_page = selected ? 29 : 28;
    } else {
        out.push_back(bubble::kAttackWithLarge);
        m_page = 30;
    }
}

void Tutorial::land_battle(const TutorialView& view, const TutorialFacts& facts, std::vector<std::uint8_t>& out) {
    const Facts::ShipState& ship = facts.ships[0];
    if (ship.loaded) {
        if (alone(view, 0)) {
            const bool coast = (ship.actions & ship_action::kFound) != 0;
            out.push_back(coast ? bubble::kUnloadSoldiers : bubble::kSailToCoast);
            m_page = coast ? 37 : 36;
        } else {
            out.push_back(bubble::kClickShip);
            m_page = 36;
        }
        return;
    }
    if (facts.riders.total > 0 && idle(facts.riders) && !m_cavalry_sent) {
        if (view.units.size() == 2 && facts.selected_riders == 2) {
            out.push_back(bubble::kAttackWithCavalry);
            m_page = 40;
        } else if (view.units.size() < 4) {
            out.push_back(bubble::kFrameSoldiers);
            m_page = 38;
        } else {
            out.push_back(bubble::kOnlyCavalry);
            m_page = 39;
        }
        return;
    }
    if (facts.partner_units == 0) {
        finish();
        out.push_back(bubble::kEnd);
        return;
    }
    m_cavalry_sent = true;
    if ((facts.musketeers.total > 0 && idle(facts.musketeers)) ||
        (facts.swordsmen.total > 0 && idle(facts.swordsmen))) {
        out.push_back(bubble::kAttackWithRest);
        m_page = 41;
    }
}

}  // namespace o1602::app
