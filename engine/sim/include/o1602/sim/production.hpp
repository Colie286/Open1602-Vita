// SPDX-License-Identifier: GPL-3.0-or-later
//
// State of the production buildings (workshops, farms, mines, hunters, fishers, markets). Until the
// economy simulation exists the state comes from the savegame (PRODLIST2) and stays as loaded; the
// renderer already uses it for building animations, smoke and status icons (docs/re/production.md).
#pragma once

#include <cstdint>

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/game_data.hpp"

namespace o1602::sim {

/// HAUS_PRODTYP Kind values with production state (the original keeps a PRODLIST entry for them).
enum class ProductionKind : std::int32_t {
    Handwerk = prod::kWorkshop,  ///< workshops (bakery, smelter, ...)
    Plantage = prod::kFarm,      ///< farms and plantations
    Bergwerk = prod::kMine,      ///< mines
    Weidetier = prod::kPasture,  ///< cattle and sheep farms
    Jagdhaus = prod::kHunter,    ///< hunters
    Fischerei = prod::kFisher,   ///< fishers
    Markt = prod::kMarket,       ///< market place
    Kontor = prod::kKontor,      ///< warehouse (Kontor)
};

/// Ware number of markets and warehouses (ALLWARE): they hold every good.
inline constexpr std::uint8_t kAllWares = ware::kAll;

/// Workshops, farms, mines, hunters and fishers: kinds 1..6. These have smoke and status icons.
[[nodiscard]] constexpr bool is_producer(const assets::BuildingType& b) noexcept {
    return b.prod_kind >= static_cast<std::int32_t>(ProductionKind::Handwerk) &&
           b.prod_kind <= static_cast<std::int32_t>(ProductionKind::Fischerei);
}

/// State of one production building (the original's PRODLIST entry, 20 bytes at 0x0054A3B8).
/// Goods are counted in 1/32 t (assets::kStockUnitsPerTon), times in game seconds.
struct Production {
    std::uint16_t island = 0;  ///< index into World::islands()
    std::uint16_t x = 0;       ///< origin tile of the building on the island
    std::uint16_t y = 0;
    /// False for a free slot: the building is gone and the slot waits for a new one (entries keep
    /// their index, so figures and events can refer to them).
    bool used = true;
    bool enabled = true;           ///< switched on by the player
    std::uint8_t rate = 0;         ///< working rate of the running cycle, kFullRate = full speed, 0 = idle
    std::uint8_t anim_phase = 0;   ///< animation offset of the building (0..15, presentation only)
    std::uint8_t idle_cycles = 0;  ///< cycles without raw material (0..15)
    std::uint8_t flags = 0;        ///< further bits of the original (+0x0F bits 4..7), meaning open
    std::uint16_t countdown = 0;   ///< game seconds until the running cycle ends
    std::uint16_t raw = 0;         ///< raw material in the building (Rohstoff)
    std::uint16_t raw2 = 0;        ///< second raw material (Workstoff)
    std::uint16_t stock = 0;       ///< produced goods waiting for a carrier
    /// Goods produced in the statistics window (markets: goods delivered); with `elapsed` it gives
    /// the productivity the building window shows. Both are halved when the window exceeds 239 s.
    std::uint16_t produced = 0;
    std::uint16_t elapsed = 0;  ///< length of the statistics window
    /// Part of `stock` promised to carriers and carts on their way (entry +0x06, not saved: 0
    /// after loading).
    std::uint16_t reserved = 0;

    static constexpr std::uint8_t kFullRate = 128;
    /// `flags` bit: the player forbids market carts to collect here (+0x0F bit 4, 0x0047CD60).
    static constexpr std::uint8_t kNoPickupFlag = 0x10;
    /// `flags` bit: the building burns (+0x0F bit 7, not saved): no new cycle, no smoke.
    static constexpr std::uint8_t kBurningFlag = 0x80;
    static constexpr std::uint16_t kProgressDone = 512;  ///< market: `produced` of a full cycle

    [[nodiscard]] constexpr bool working() const noexcept { return rate != 0; }
    [[nodiscard]] constexpr bool no_pickup() const noexcept { return (flags & kNoPickupFlag) != 0; }
    [[nodiscard]] constexpr bool burning() const noexcept { return (flags & kBurningFlag) != 0; }
    /// Stock nobody has reserved.
    [[nodiscard]] constexpr std::uint16_t available() const noexcept {
        return stock > reserved ? static_cast<std::uint16_t>(stock - reserved) : 0;
    }
};

/// What the icon above a building of the local player shows (0x0047CDD0).
enum class ProductionStatus : std::uint8_t {
    Ok,          ///< no icon
    Disabled,    ///< switched off by the player
    NoRawGoods,  ///< idle for at least Maxnorohst cycles
    Burning,     ///< on fire (status 3)
};

[[nodiscard]] constexpr ProductionStatus production_status(const Production& p,
                                                           const assets::BuildingType& b) noexcept {
    if (p.burning()) {
        return ProductionStatus::Burning;
    }
    if (!p.enabled) {
        return ProductionStatus::Disabled;
    }
    return p.idle_cycles >= b.max_idle_cycles ? ProductionStatus::NoRawGoods : ProductionStatus::Ok;
}

/// Smoke rises from the chimney while the building works (0x0047CF30).
[[nodiscard]] constexpr bool smoke_visible(const Production& p) noexcept {
    return p.enabled && p.working();
}

}  // namespace o1602::sim
