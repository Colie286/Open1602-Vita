// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/island_events.hpp"

#include <array>

#include "o1602/sim/construction.hpp"
#include "o1602/sim/growth.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint64_t kTenth = 100;                     // the original counts game time in 1/10 s
constexpr std::uint64_t kDroughtMs = 6'000 * kTenth;      // 10 minutes
constexpr std::uint64_t kLongDroughtMs = 9'000 * kTenth;  // 15 minutes on "hard"
constexpr std::uint64_t kHalfHourTenths = 18'000;
constexpr std::uint8_t kWeakest = 12;  // weaker droughts are over

/// The drought tables (0x0049AD78 and the bytes before it): rows by difficulty, a drought where
/// the bit of the index is set. Difficulty 1 only meets one stray byte before the table (S-043).
constexpr std::array<std::uint32_t, 4> kDroughtTable = {
    0,
    1U << 28,
    (1U << 3) | (1U << 6) | (1U << 11) | (1U << 14) | (1U << 20) | (1U << 24) | (1U << 26) | (1U << 29),
    (1U << 2) | (1U << 5) | (1U << 11) | (1U << 14) | (1U << 17) | (1U << 19) | (1U << 23) | (1U << 24) | (1U << 29) |
        (1U << 31),
};

void emit(std::vector<IslandEvent>* events, IslandEvent::Kind kind, std::uint16_t island, std::uint32_t count = 0) {
    if (events != nullptr) {
        events->push_back(IslandEvent{kind, island, count});
    }
}

/// The ware whose fertility decides about a field: of its ripe record, or of the ripe record
/// before a withered one.
std::uint8_t field_ware(const assets::BuildingTable& buildings, const assets::BuildingType& b) {
    if (b.prod_kind == prod::kRipe) {
        return b.ware;
    }
    const assets::BuildingType* ripe = ripe_of_withered(buildings, b);
    return ripe != nullptr ? ripe->ware : 0;
}

/// While a drought lasts: ripe fields not fertile at its strength wither. Every row is looked at
/// in every third column from a random start (the original's start is rand() & 3, which favours
/// one of the three columns; Open1602 picks evenly).
std::uint32_t wither_scan(World& world, const assets::BuildingTable& buildings, std::uint16_t i,
                          const FieldReserved& reserved) {
    const IslandMap& map = world.islands()[i].map;
    const std::uint8_t strength = world.islands()[i].drought.strength;
    Random& random = world.random(RandomStream::Islands);
    std::uint32_t withered = 0;
    std::vector<TilePos> forgotten;
    const std::size_t first_new = world.growth().entries.size();
    const auto w = static_cast<std::int32_t>(map.width());
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::int32_t x = w - 1 - static_cast<std::int32_t>(random.below(3)); x >= 0; x -= 3) {
            const MapCell& cell = map.top(static_cast<std::uint32_t>(x), y);
            const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
            if (b == nullptr || b->prod_kind != prod::kRipe || withered_record(buildings, *b) == nullptr) {
                continue;
            }
            const TilePos tile{x, static_cast<std::int32_t>(y)};
            if ((reserved && reserved(i, tile)) || tile_fertile_at(world, buildings, i, b->ware, x, tile.y, strength)) {
                continue;
            }
            const auto ux = static_cast<std::uint16_t>(x);
            const auto uy = static_cast<std::uint16_t>(y);
            withered += wither_tile(world, buildings, i, ux, uy, &forgotten) ? 1U : 0U;
        }
    }
    forget_growth(world, i, forgotten, first_new);
    return withered;
}

/// After a drought: withered fields fertile at the current strength grow again.
std::uint32_t recovery_scan(World& world, const assets::BuildingTable& buildings, std::uint16_t i) {
    const IslandMap& map = world.islands()[i].map;
    const std::uint8_t strength = world.islands()[i].drought.strength;
    std::uint32_t recovered = 0;
    std::vector<TilePos> forgotten;
    const std::size_t first_new = world.growth().entries.size();
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = map.width(); x-- > 0;) {
            const MapCell& cell = map.top(x, y);
            const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
            if (b == nullptr || !b->withered) {
                continue;
            }
            const std::uint8_t ware = field_ware(buildings, *b);
            if (ware == 0 || !tile_fertile_at(world, buildings, i, ware, static_cast<std::int32_t>(x),
                                              static_cast<std::int32_t>(y), strength)) {
                continue;
            }
            recovered += recover_tile(world, buildings, i, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                                      &forgotten)
                             ? 1U
                             : 0U;
        }
    }
    forget_growth(world, i, forgotten, first_new);
    return recovered;
}

/// The drought roll of one island (0x0046B3E0): only once the next drought time has come.
void roll_drought(World& world, std::uint16_t i, std::uint8_t level, std::vector<IslandEvent>* events) {
    const std::uint64_t now = world.time_ms();
    if (level == 0 || now < world.next_drought_ms()) {
        return;
    }
    Random& random = world.random(RandomStream::Islands);
    if (!world.drought_seed()) {
        world.set_drought_seed(static_cast<std::uint8_t>(random.below(32)));
    }
    const std::uint64_t k =
        (random.below(7) + now / kTenth / kHalfHourTenths + world.islands()[i].header.number + *world.drought_seed()) &
        31U;
    if ((kDroughtTable[level] >> k & 1U) == 0) {
        return;
    }
    // Only islands of human or computer players get it; a computer city halves it.
    bool settled = false;
    std::uint64_t duration = level >= 3 ? kLongDroughtMs : kDroughtMs;
    std::array<const City*, kCitySlotsPerIsland> slots{};
    for (const City& c : world.cities()) {
        if (c.island == i && c.index < slots.size()) {
            slots[c.index] = &c;
        }
    }
    for (const City* c : slots) {
        const Player* p = c != nullptr ? world.player(c->owner) : nullptr;
        if (p == nullptr) {
            continue;
        }
        if (p->role == PlayerRole::Human) {
            settled = true;
        } else if (p->role == PlayerRole::Computer) {
            settled = true;
            duration /= 2;
            break;
        }
    }
    if (settled) {
        start_drought(world, i, kDroughtStrength, duration, events);
    }
    // The chance is used up even on an empty island.
    world.set_next_drought_ms(now + (27 + std::uint64_t{random.below(8)}) * 600 * kTenth);
}

}  // namespace

std::uint8_t drought_level(const World& world, const Rules& rules) noexcept {
    switch (rules.droughts) {
        case Droughts::Off:
            return 0;
        case Droughts::Rare:
            return 1;
        case Droughts::Normal:
            return 2;
        case Droughts::Frequent:
            return 3;
        case Droughts::Game:
            break;
    }
    const World::GameSettings& s = world.game_settings();
    if ((s.flags & World::GameSettings::kNoDroughts) != 0) {
        return 0;
    }
    return s.difficulty < kDroughtTable.size() ? s.difficulty : 3;
}

void start_drought(World& world, std::uint16_t island, std::uint8_t strength, std::uint64_t duration_ms,
                   std::vector<IslandEvent>* events) {
    if (island >= world.islands().size()) {
        return;
    }
    WorldIsland::Drought& d = world.island(island).drought;
    d.end_ms = world.time_ms() + duration_ms;
    d.strength = strength;
    emit(events, IslandEvent::Kind::DroughtStarted, island);
}

void advance_island_events(World& world, const assets::BuildingTable& buildings, const Rules& rules,
                           const FieldReserved& reserved, std::vector<IslandEvent>* events,
                           const std::vector<bool>* due) {
    const std::uint8_t level = drought_level(world, rules);
    for (std::uint16_t i = 0; i < world.islands().size(); ++i) {
        if (due != nullptr && (i >= due->size() || !(*due)[i])) {
            continue;
        }
        WorldIsland::Drought& d = world.island(i).drought;
        // A curse of the natives: a weaker drought, at once, replacing one that runs.
        if (d.curse_pending) {
            d.curse_pending = false;
            if (rules.droughts != Droughts::Off) {
                start_drought(world, i, kCurseStrength, kDroughtMs, events);
            }
        }
        if (d.strength == 0) {
            roll_drought(world, i, level, events);
        } else if (world.time_ms() < d.end_ms) {
            if (const std::uint32_t n = wither_scan(world, buildings, i, reserved); n != 0) {
                emit(events, IslandEvent::Kind::Withered, i, n);
            }
        } else {
            // Over: the strength halves every epoch, below 12 it is gone; each step brings back
            // what is fertile again.
            const auto s = static_cast<std::uint8_t>(d.strength * 64U >> 7);
            d.strength = s < kWeakest ? 0 : s;
            if (const std::uint32_t n = recovery_scan(world, buildings, i); n != 0) {
                emit(events, IslandEvent::Kind::Recovered, i, n);
            }
            if (d.strength == 0) {
                emit(events, IslandEvent::Kind::DroughtOver, i);
            }
        }
    }
}

}  // namespace o1602::sim
