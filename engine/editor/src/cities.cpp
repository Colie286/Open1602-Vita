// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/editor/cities.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "o1602/editor/islands.hpp"
#include "o1602/sim/logistics.hpp"

namespace o1602::editor {

using core::ErrorCode;
using core::make_error;

namespace {

core::Result<std::unique_ptr<Operation>> no_city(std::uint16_t island, std::uint8_t city) {
    return make_error(ErrorCode::NotFound,
                      "no city " + std::to_string(city) + " on island " + std::to_string(island + 1));
}

std::unique_ptr<Operation> edit(sim::World after, const char* name) {
    return std::make_unique<WorldEdit>(std::move(after), name);
}

}  // namespace

core::Result<std::unique_ptr<Operation>> rename_city(const sim::World& world, std::uint16_t island, std::uint8_t city,
                                                     std::string_view name) {
    if (world.city(island, city) == nullptr) {
        return no_city(island, city);
    }
    if (!sim::valid_name(name, kMaxCityName)) {
        return make_error(ErrorCode::InvalidFormat, "a city cannot be named so");
    }
    sim::World after = world;
    after.city(island, city)->name = std::string(name);
    return edit(std::move(after), "city_rename");
}

std::uint32_t city_room(const sim::World& world, const assets::BuildingTable& buildings, std::uint16_t island,
                        std::uint8_t city) {
    const auto capacities = sim::city_capacities(world, buildings);
    const auto it = capacities.find(std::uint32_t{island} << 8 | city);
    return it == capacities.end() ? 0 : it->second;
}

core::Result<std::unique_ptr<Operation>> set_city_goods(const sim::World& world, const assets::BuildingTable& buildings,
                                                        std::uint16_t island, std::uint8_t city, std::uint8_t ware,
                                                        std::uint32_t amount) {
    if (world.city(island, city) == nullptr) {
        return no_city(island, city);
    }
    if (!world.goods().is_trade(ware)) {
        return make_error(ErrorCode::InvalidFormat, "ware " + std::to_string(ware) + " is no trade good");
    }
    sim::World after = world;
    sim::City& c = *after.city(island, city);
    c.ensure_ware(ware);
    c.goods[ware] = std::min(amount, city_room(world, buildings, island, city));
    return edit(std::move(after), "city_goods");
}

core::Result<std::unique_ptr<Operation>> change_city_owner(const sim::World& world, std::uint16_t island,
                                                           std::uint8_t city, sim::PlayerId owner) {
    const sim::City* c = world.city(island, city);
    if (c == nullptr) {
        return no_city(island, city);
    }
    const sim::Player* p = world.player(owner);
    if (p == nullptr) {
        return make_error(ErrorCode::NotFound, "no player " + std::to_string(owner));
    }
    if (p->role != sim::PlayerRole::Human && p->role != sim::PlayerRole::Computer) {
        return make_error(ErrorCode::Unsupported, "only a human or a computer takes a city");
    }
    if (c->owner == owner) {
        return make_error(ErrorCode::Unsupported, "the city is the player's already");
    }
    const bool has_one = std::any_of(world.cities().begin(), world.cities().end(), [&](const sim::City& other) {
        return other.island == island && other.owner == owner;
    });
    if (has_one) {
        return make_error(ErrorCode::Unsupported, "the player has a city on the island already");
    }
    sim::World after = world;
    sim::IslandMap& map = after.island(island).map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            if (map.top(x, y).city == city) {
                map.set_city(x, y, city, owner);
            }
        }
    }
    sim::City& changed = *after.city(island, city);
    changed.owner = owner;
    changed.ledger.clear();  // the new owner's statistics start with the city
    changed.history.clear();
    return edit(std::move(after), "city_owner");
}

}  // namespace o1602::editor
