// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/savegame.hpp"

#include <algorithm>
#include <span>

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::size_t kCitySize = 0xA8;
constexpr std::size_t kWarehouseSize = 0x3EC;
constexpr std::size_t kWarehouseSlots = 23;
constexpr std::size_t kWareSlotSize = 20;
constexpr std::size_t kWareSlotsOffset = 0x2C;
constexpr std::uint8_t kFirstWare = 2;
constexpr std::size_t kProductionSize = 0x18;
constexpr std::size_t kAnimalSize = 8;
constexpr std::size_t kSoldierSize = 0x44;
constexpr std::size_t kGrowthSize = 8;
constexpr std::size_t kConsumptionSize = 0x104;
constexpr std::size_t kHouseSize = 16;
constexpr std::size_t kShipyardSize = 0x18;
constexpr std::size_t kCastleSize = 0x70;
constexpr std::size_t kTowerSize = 0x24;
constexpr std::size_t kTimersHouseEpochs = 0x44;
constexpr std::size_t kTimersHouseMs = 0x198;
constexpr std::size_t kTimersGameTime = 0xA0;
constexpr std::size_t kConsumptionGoods = 16;
constexpr std::size_t kTimersCounters = 0x64;
constexpr std::size_t kTimersGrowthMs = 0x218;

std::uint32_t u32(std::span<const std::uint8_t> d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1]) << 8) |
           (static_cast<std::uint32_t>(d[o + 2]) << 16) | (static_cast<std::uint32_t>(d[o + 3]) << 24);
}

/// Payloads of all chunks with this name (savegames: once, scenarios: once per island).
template <class Fn>
core::Status for_each_record(const ChunkFile& file, const char* name, std::size_t size, Fn&& fn) {
    for (const Chunk& chunk : file.chunks()) {
        if (chunk.name != name) {
            continue;
        }
        const auto data = file.payload(chunk);
        if (data.size() % size != 0) {
            return make_error(ErrorCode::InvalidFormat,
                              std::string(name) + " size is not a multiple of " + std::to_string(size));
        }
        for (std::size_t o = 0; o < data.size(); o += size) {
            fn(data.subspan(o, size));
        }
    }
    return core::Ok{};
}

}  // namespace

core::Result<GameState> load_game_state(const ChunkFile& file) {
    GameState state;
    auto status = for_each_record(file, "STADT4", kCitySize, [&](std::span<const std::uint8_t> r) {
        CityRecord c;
        c.island = r[0];
        c.index = r[1];
        c.owner = r[2];
        const auto name = r.subspan(0x87);
        c.name.assign(name.begin(), std::find(name.begin(), name.end(), std::uint8_t{0}));
        for (std::size_t k = 0; k < 5; ++k) {
            c.inhabitants[k] = u32(r, 0x5C + 4 * k);
            c.satisfaction[k] = r[0x78 + k];
            c.tax[k] = r[0x7F + k];
        }
        c.food_ratio = r[0x86];
        for (std::size_t k = 0; k < c.goodwill.size(); ++k) {
            c.goodwill[k] = u32(r, 0x18 + 4 * k);
        }
        c.material_lock = (r[4] & 1U) != 0;
        c.population_message = r[5];
        c.purchases = u32(r, 0x08);
        c.sales = u32(r, 0x0C);
        state.cities.push_back(std::move(c));
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "KONTOR2", kWarehouseSize, [&](std::span<const std::uint8_t> r) {
        const std::uint32_t head = u32(r, 0);
        WarehouseRecord w;
        w.island = static_cast<std::uint8_t>(head);
        w.x = static_cast<std::uint8_t>(head >> 8);
        w.y = static_cast<std::uint8_t>(head >> 16);
        w.city = static_cast<std::uint8_t>((head >> 24) & 0xFU);
        for (std::size_t i = 0; i < kWarehouseSlots; ++i) {
            const std::size_t o = kWareSlotsOffset + i * kWareSlotSize;
            WareSlot s;
            s.ware = static_cast<std::uint8_t>(kFirstWare + i);
            const std::uint32_t id = u32(r, o + 16) & 0xFFFFU;
            s.producer_id = id == 0 ? 0 : static_cast<std::uint16_t>(id + 20000);
            for (std::size_t k = 0; k < s.raw.size(); ++k) {
                s.raw[k] = u32(r, o + 4 * k);
            }
            w.wares.push_back(s);
        }
        state.warehouses.push_back(std::move(w));
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "PRODLIST2", kProductionSize, [&](std::span<const std::uint8_t> r) {
        ProductionRecord p;
        p.island = r[0];
        p.x = r[1];
        p.y = r[2];
        const auto u16 = [&](std::size_t o) { return static_cast<std::uint16_t>(r[o] | (r[o + 1] << 8)); };
        p.enabled = (r[0x14] & 0x01U) != 0;
        p.anim_phase = static_cast<std::uint8_t>((r[0x14] >> 2) & 0x0FU);
        p.stock = u16(5);
        p.countdown = u16(8);
        p.raw2 = u16(0x0A);
        p.raw = u16(0x0C);
        p.rate = r[0x0F];
        p.produced = u16(0x10);
        p.elapsed = u16(0x12);
        p.idle_cycles = static_cast<std::uint8_t>(r[0x15] & 0x0FU);
        p.flags = static_cast<std::uint8_t>((((r[0x14] >> 6) & 1U) << 4) | (((r[0x14] >> 1) & 1U) << 6));
        std::copy(r.begin() + 3, r.end(), p.raw_bytes.begin());
        state.production.push_back(p);
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "HIRSCH2", kAnimalSize, [&](std::span<const std::uint8_t> r) {
        state.animals.push_back(AnimalRecord{r[0], r[1], r[2], u32(r, 4)});
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "SOLDAT3", kSoldierSize, [&](std::span<const std::uint8_t> r) {
        const auto u16 = [&](std::size_t o) { return static_cast<std::uint16_t>(r[o] | (r[o + 1] << 8)); };
        SoldierRecord s;
        s.x2 = u16(0);
        s.y2 = u16(2);
        s.health = u16(4);
        s.figure = u16(6);
        s.slot = u16(8);
        s.home = u32(r, 0x0A);
        s.target = u32(r, 0x12);
        s.island = r[0x17];
        s.owner = r[0x18];
        s.anim = r[0x19];
        s.radius = r[0x1A];
        s.direction = static_cast<std::uint8_t>(r[0x1B] & 7U);
        s.waypoint = r[0x1C];
        s.patrol = static_cast<std::uint8_t>(r[0x1D] & 3U);
        s.waypoints = {u32(r, 0x1E), u32(r, 0x22)};
        std::copy(r.begin(), r.end(), s.raw.begin());
        state.soldiers.push_back(s);
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "TURM", kTowerSize, [&](std::span<const std::uint8_t> r) {
        const auto u16 = [&](std::size_t o) { return static_cast<std::uint16_t>(r[o] | (r[o + 1] << 8)); };
        TowerRecord t;
        t.x = u16(0);
        t.y = u16(2);
        t.health = u16(4);
        t.figure = u16(6);
        t.slot = u16(8);
        t.building = u32(r, 0x0A);
        t.island = r[0x17];
        t.owner = r[0x18];
        t.anim = r[0x19];
        t.direction = static_cast<std::uint8_t>(r[0x1B] & 7U);
        state.towers.push_back(t);
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "MARKT2", kConsumptionSize, [&](std::span<const std::uint8_t> r) {
        ConsumptionRecord c;
        const std::uint32_t head = u32(r, 0);
        c.island = static_cast<std::uint8_t>(head);
        c.city = static_cast<std::uint8_t>((head >> 8) & 0xFU);
        for (std::size_t e = 0; e < kConsumptionGoods; ++e) {
            const std::size_t o = 4 + 16 * e;
            ConsumptionRecord::Good g;
            const auto id = static_cast<std::uint16_t>(r[o + 0xC] | (r[o + 0xD] << 8));
            if (id == 0) {
                continue;
            }
            g.producer_id = static_cast<std::uint16_t>(id + 20000);
            g.demand = u32(r, o);
            g.supplied = u32(r, o + 4);
            g.ratio = r[o + 0xE];
            c.goods.push_back(g);
        }
        state.consumption.push_back(std::move(c));
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "SIEDLER", kHouseSize, [&](std::span<const std::uint8_t> r) {
        const std::uint32_t d0 = u32(r, 0);
        const std::uint32_t d1 = u32(r, 4);
        const std::uint32_t d2 = u32(r, 8);
        HouseRecord h;
        h.island = static_cast<std::uint8_t>(d0);
        h.x = static_cast<std::uint8_t>(d0 >> 8);
        h.y = static_cast<std::uint8_t>(d0 >> 16);
        h.epoch = static_cast<std::uint8_t>((d0 >> 24) & 7U);
        h.queue_epoch = static_cast<std::uint8_t>(d1 & 7U);
        h.resident_class = static_cast<std::uint8_t>(d1 >> 8);
        h.market_distance = static_cast<std::uint8_t>((d1 >> 16) & 0xFU);
        h.inhabitants64 = static_cast<std::uint16_t>(d2);
        h.timer_group = static_cast<std::uint8_t>((d2 >> 16) & 0xFU);
        h.services_complete = ((d2 >> 21) & 1U) != 0;
        h.market = ((d2 >> 22) & 1U) != 0;
        // dword2 bits 30, 23, 24, 28, 25, 26, 29, 31, 27 -> runtime word bits 2..10.
        constexpr std::array<std::uint8_t, 9> kServiceBits = {30, 23, 24, 28, 25, 26, 29, 31, 27};
        for (std::size_t i = 0; i < kServiceBits.size(); ++i) {
            if (((d2 >> kServiceBits[i]) & 1U) != 0) {
                h.services = static_cast<std::uint16_t>(h.services | (1U << (i + 2)));
            }
        }
        std::copy(r.begin(), r.end(), h.raw.begin());
        state.houses.push_back(h);
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "WERFT", kShipyardSize, [&](std::span<const std::uint8_t> r) {
        if (r[0] != 0xFF) {
            const auto u16 = [&](std::size_t o) { return static_cast<std::uint16_t>(r[o] | (r[o + 1] << 8)); };
            state.shipyards.push_back(CostRecord{r[0], r[1], r[2], ((u32(r, 0) >> 27) & 1U) != 0, r[6], u16(8),
                                                 u16(0x0A), u16(4), u16(0x0C)});
        }
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "MILITAR", kCastleSize, [&](std::span<const std::uint8_t> r) {
        if (r[0] != 0xFF) {
            const auto u16 = [&](std::size_t o) { return static_cast<std::uint16_t>(r[o] | (r[o + 1] << 8)); };
            CostRecord c{r[0], r[1], r[2], true};
            c.countdown = u16(4);
            c.weapons = {u16(8), u16(0x0A), u16(0x0C)};
            for (std::size_t k = 0; k < c.slots.size(); ++k) {
                const std::uint32_t d0 = u32(r, 0x10 + 8 * k);
                const std::uint32_t d1 = u32(r, 0x14 + 8 * k);
                c.slots[k] = {static_cast<std::uint16_t>(d0 & 0xFFFU), ((d0 >> 12) & 1U) != 0, ((d0 >> 13) & 1U) != 0,
                              static_cast<std::uint16_t>(d1 & 0xFFFU), static_cast<std::uint16_t>(d1 >> 16)};
            }
            state.castles.push_back(c);
        }
    });
    if (!status) {
        return status.error();
    }
    status = for_each_record(file, "ROHWACHS2", kGrowthSize, [&](std::span<const std::uint8_t> r) {
        if (r[0] != 0xFF) {
            state.growth.push_back(GrowthRecord{r[0], r[1], r[2], r[3], r[4], r[5]});
        }
    });
    if (!status) {
        return status.error();
    }
    if (const Chunk* timers = file.find("TIMERS")) {
        const auto data = file.payload(*timers);
        if (data.size() >= kTimersGrowthMs + 32 * 4) {
            GrowthClocks clocks;
            for (std::size_t i = 0; i < 32; ++i) {
                clocks.counter[i] = static_cast<std::uint8_t>(data[kTimersCounters + i] & 7U);
                clocks.time_ms[i] = u32(data, kTimersGrowthMs + 4 * i);
            }
            state.growth_clocks = clocks;
        }
        if (data.size() >= kTimersHouseMs + 16 * 4) {
            HouseClocks clocks;
            for (std::size_t i = 0; i < 16; ++i) {
                clocks.epoch[i] = static_cast<std::uint8_t>(data[kTimersHouseEpochs + i] & 7U);
                clocks.time_ms[i] = u32(data, kTimersHouseMs + 4 * i);
            }
            state.house_clocks = clocks;
        }
        if (data.size() >= kTimersGameTime + 4) {
            state.game_time = u32(data, kTimersGameTime);
        }
        if (data.size() >= 2) {
            state.island_phase = static_cast<std::uint8_t>(data[1] & 7U);
        }
        constexpr std::size_t kTimersCityName = 0xB0;
        if (data.size() >= kTimersCityName + 4) {
            state.city_name_index = u32(data, kTimersCityName);
        }
        constexpr std::size_t kTimersDifficulty = 0xA6;
        constexpr std::size_t kTimersSettings = 0xA8;
        constexpr std::size_t kTimersNextDrought = 0xB4;
        if (data.size() >= kTimersNextDrought + 4) {
            constexpr std::size_t kTimersTutorial = 0xA5;
            state.tutorial = data[kTimersTutorial];
            state.difficulty = data[kTimersDifficulty];
            state.settings = u32(data, kTimersSettings);
            // A byte the loader turns into "large veins never run out" (0x00484D30); no file sets it.
            constexpr std::size_t kTimersEndlessVeins = 0xA4;
            constexpr std::uint32_t kEndlessVeinsBit = 1U << 3;
            if (data[kTimersEndlessVeins] != 0) {
                state.settings = *state.settings | kEndlessVeinsBit;
            }
            constexpr std::size_t kTimersPirates = 0xB8;
            if (data.size() >= kTimersPirates + 4) {
                state.pirate_interval = u32(data, kTimersPirates);
            }
            state.next_drought = u32(data, kTimersNextDrought);
        }
        constexpr std::size_t kTimersRunningVolcano = 0xC4;
        constexpr std::size_t kTimersLastVolcano = 0xC8;
        constexpr std::size_t kTimersNextVolcano = 0xCC;
        constexpr std::size_t kTimersCraters = 0xD0;
        constexpr std::uint8_t kRefIslandTile = 0x33;
        const auto crater_ref = [&](std::size_t o) -> std::optional<GameState::CraterRef> {
            // The low byte tells whether the ref is set (0x0048C0F0 tests it alone).
            if (data[o] != kRefIslandTile) {
                return std::nullopt;
            }
            return GameState::CraterRef{data[o + 1], data[o + 2], data[o + 3]};
        };
        if (data.size() >= kTimersCraters + 4) {
            state.running_volcano = crater_ref(kTimersRunningVolcano);
            state.last_volcano = crater_ref(kTimersLastVolcano);
            state.next_volcano = u32(data, kTimersNextVolcano);
            state.crater_count = u32(data, kTimersCraters);
        }
    }
    return state;
}

}  // namespace o1602::assets
