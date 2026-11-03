// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/units.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include "o1602/editor/islands.hpp"
#include "o1602/sim/land_paths.hpp"
#include "o1602/sim/sea_paths.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/soldier.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;

namespace {

bool on_map(const sim::World& world, std::int64_t x, std::int64_t y) {
    return x >= 0 && y >= 0 && x < std::int64_t{world.width()} && y < std::int64_t{world.height()};
}

bool owner_ok(const sim::World& world, sim::PlayerId owner) {
    const sim::Player* p = world.player(owner);
    return p != nullptr && (p->role == sim::PlayerRole::Human || p->role == sim::PlayerRole::Computer);
}

/// Every order that names `gone` cleared.
void forget(sim::Target& t, sim::Target::Kind kind, std::uint32_t gone) {
    if (t.kind == kind && t.ship == gone) {
        t = sim::Target{};
    }
}

void forget_everywhere(sim::World& world, sim::Target::Kind kind, std::uint32_t gone) {
    const auto forget_queue = [&](std::vector<sim::Target>& queue) {
        std::erase_if(queue, [&](const sim::Target& t) { return t.kind == kind && t.ship == gone; });
    };
    for (sim::Ship& s : world.ships()) {
        forget(s.target, kind, gone);
        forget(s.order, kind, gone);
        forget(s.previous, kind, gone);
        forget_queue(s.queue);
    }
    for (sim::Soldier& s : world.soldiers()) {
        forget(s.target, kind, gone);
        forget(s.home, kind, gone);
        for (sim::Target& p : s.patrol.points) {
            forget(p, kind, gone);
        }
        forget_queue(s.queue);
    }
}

}  // namespace

UnitRefusal ship_fits(const sim::World& world, const assets::BuildingTable& buildings, std::int64_t x, std::int64_t y) {
    if (!on_map(world, x, y)) {
        return UnitRefusal::Outside;
    }
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    const sim::World::Cell& cell = world.cell(ux, uy);
    if (cell.island != sim::World::kSea) {
        // Water inside an island's rectangle: what its tile shows (sim::SeaMap).
        const sim::MapCell& c = world.islands()[cell.island].map.top(cell.local_x, cell.local_y);
        const assets::BuildingType* b = c.empty() ? nullptr : buildings.by_sprite(c.sprite);
        if (b == nullptr || !sim::sailable_building(*b)) {
            return UnitRefusal::NoWater;
        }
    }
    for (const sim::Ship& s : world.ships()) {
        if (!s.cart() && std::abs(std::int64_t{s.x} - x) <= 1 && std::abs(std::int64_t{s.y} - y) <= 1) {
            return UnitRefusal::Taken;
        }
    }
    return UnitRefusal::None;
}

UnitRefusal unit_fits(const sim::World& world, const assets::BuildingTable& buildings, std::int64_t x, std::int64_t y) {
    if (!on_map(world, x, y)) {
        return UnitRefusal::Outside;
    }
    sim::LandMap land;
    land.rebuild(world, buildings);
    const auto ix = static_cast<std::int32_t>(x);
    const auto iy = static_cast<std::int32_t>(y);
    const assets::BuildingType* ground = land.building(ix, iy);
    if (land.island_at(ix, iy) == sim::World::kSea || (ground != nullptr && !sim::soldiers_walk_on(*ground))) {
        return UnitRefusal::NoGround;
    }
    for (const sim::Soldier& s : world.soldiers()) {
        if (s.pos_x / sim::kPosUnit == ix && s.pos_y / sim::kPosUnit == iy) {
            return UnitRefusal::Taken;
        }
    }
    return UnitRefusal::None;
}

core::Result<std::unique_ptr<Operation>> place_ship(const sim::World& world, const assets::BuildingTable& buildings,
                                                    const assets::FigureTable& figures, sim::PlayerId owner,
                                                    std::uint16_t figure, std::int64_t x, std::int64_t y,
                                                    std::uint8_t direction) {
    const assets::FigureType* type = figures.get(figure);
    UnitRefusal refused = UnitRefusal::None;
    if (type == nullptr || !figures.ship_type_of(figure)) {
        refused = UnitRefusal::NoType;
    } else if (!owner_ok(world, owner)) {
        refused = UnitRefusal::NoOwner;
    } else {
        refused = ship_fits(world, buildings, x, y);
    }
    if (refused != UnitRefusal::None) {
        return make_error(ErrorCode::InvalidFormat, refusal_name(refused));
    }
    sim::World after = world;
    sim::Ship s = sim::make_ship(std::string(), static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), figure,
                                 owner, direction);
    s.health = type->ship.max_health;
    s.cargo.resize(std::max<std::size_t>(1, type->ship.cargo_slots));
    after.add_ship(std::move(s));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "ship_place"));
}

core::Result<std::unique_ptr<Operation>> place_unit(const sim::World& world, const assets::BuildingTable& buildings,
                                                    const assets::FigureTable& figures, sim::PlayerId owner,
                                                    std::uint16_t family, std::int64_t x, std::int64_t y,
                                                    std::uint8_t direction) {
    const auto unit = figures.unit_of(family);
    const assets::FigureType* type = figures.get(family);
    UnitRefusal refused = UnitRefusal::None;
    if (!unit || unit->family != family || type == nullptr) {
        refused = UnitRefusal::NoType;
    } else if (!owner_ok(world, owner)) {
        refused = UnitRefusal::NoOwner;
    } else {
        refused = unit_fits(world, buildings, x, y);
    }
    if (refused != UnitRefusal::None) {
        return make_error(ErrorCode::InvalidFormat, refusal_name(refused));
    }
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    sim::World after = world;
    sim::Soldier s = sim::make_soldier(ux, uy, family, owner, world.cell(ux, uy).island);
    s.direction = static_cast<std::uint8_t>(direction & 7U);
    s.health = type->combat.max_health;
    s.anim = 1;  // standing
    after.add_soldier(std::move(s));
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "unit_place"));
}

core::Result<std::unique_ptr<Operation>> remove_ship(const sim::World& world, std::uint32_t id) {
    if (world.ship(id) == nullptr) {
        return make_error(ErrorCode::NotFound, "no such ship");
    }
    sim::World after = world;
    after.erase_ships([id](const sim::Ship& s) { return s.id == id; });
    forget_everywhere(after, sim::Target::Kind::Ship, id);
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "ship_remove"));
}

core::Result<std::unique_ptr<Operation>> remove_unit(const sim::World& world, std::uint32_t id) {
    if (world.soldier(id) == nullptr) {
        return make_error(ErrorCode::NotFound, "no such unit");
    }
    sim::World after = world;
    after.erase_soldiers([id](const sim::Soldier& s) { return s.id == id; });
    forget_everywhere(after, sim::Target::Kind::Soldier, id);
    return std::unique_ptr<Operation>(std::make_unique<WorldEdit>(std::move(after), "unit_remove"));
}

const char* refusal_name(UnitRefusal refusal) noexcept {
    switch (refusal) {
        case UnitRefusal::None:
            return "none";
        case UnitRefusal::Outside:
            return "outside the map";
        case UnitRefusal::NoWater:
            return "no water a ship sails on";
        case UnitRefusal::NoGround:
            return "no ground a unit walks on";
        case UnitRefusal::Taken:
            return "taken by another";
        case UnitRefusal::NoOwner:
            return "no player to own it";
        case UnitRefusal::NoType:
            return "no such type";
    }
    return "unknown";
}

}  // namespace o1602::editor
