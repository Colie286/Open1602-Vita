// SPDX-License-Identifier: GPL-3.0-or-later
//
// The goods statistics of a city (Open1602's own, docs/architecture/0012-goods-statistics.md):
// every movement of a good is booked where it happens, in 1/32 t (assets::kStockUnitsPerTon), and
// every game minute the running counts become a sample. The window "Warenbilanz" and the early
// warning of a shortage read it; nothing in the game depends on it. Integers and game time only
// (ADR 0004), and a fixed size per good, so that long games cost no more memory.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace o1602::sim {

/// Where goods came from or went to.
enum class GoodsFlow : std::uint8_t {
    Produced,      ///< in: a production cycle ended (workshops, farms, mines, hunters, fishers)
    Bought,        ///< in: another player's ship or the free trader sold at the Kontor
    ShippedIn,     ///< in: an own ship unloaded at the Kontor
    Returned,      ///< in: building material back from demolishing, weapons of a cancelled recruit
    Residents,     ///< out: the inhabitants consumed
    Workshops,     ///< out: a production cycle or a shipyard used it as raw material
    Military,      ///< out: weapons of recruited soldiers
    Construction,  ///< out: buildings and house upgrades
    Sold,          ///< out: another player's ship, the free trader or the natives bought at the Kontor
    ShippedOut,    ///< out: an own ship loaded at the Kontor
    Lost,          ///< out: did not fit into the full warehouse
    Demand,        ///< what the inhabitants asked for; not a movement (with Residents: the supply)
    // Not goods but gold, for the trade at the Kontor (the tab "Handel"): what the city earned with
    // Sold and paid for Bought, and the free trader's share of each.
    SoldValue,
    BoughtValue,
    TraderSoldValue,
    TraderBoughtValue,
};
inline constexpr std::size_t kGoodsFlows = 16;

[[nodiscard]] constexpr bool is_inflow(GoodsFlow f) noexcept {
    return f <= GoodsFlow::Returned;
}
[[nodiscard]] constexpr bool is_outflow(GoodsFlow f) noexcept {
    return f >= GoodsFlow::Residents && f <= GoodsFlow::Lost;
}

/// What the flows of a good add up to over some minutes, in 1/32 t.
struct FlowTotals {
    std::array<std::uint64_t, kGoodsFlows> flow{};
    std::uint32_t minutes = 0;  ///< the minutes summed (fewer than asked at the start of a game)

    [[nodiscard]] std::uint64_t of(GoodsFlow f) const noexcept { return flow[static_cast<std::size_t>(f)]; }
    [[nodiscard]] std::uint64_t in() const noexcept;
    [[nodiscard]] std::uint64_t out() const noexcept;
    /// In minus out.
    [[nodiscard]] std::int64_t net() const noexcept {
        return static_cast<std::int64_t>(in()) - static_cast<std::int64_t>(out());
    }
    FlowTotals& operator+=(const FlowTotals& other) noexcept;
};

/// What the window of a city shows, as numbers kept by minute (the tabs "Bevölkerung" and
/// "Finanzen"): the moving averages of the city window and its inhabitants.
enum class CityFigure : std::uint8_t {
    Taxes,         ///< tax income (consumption.hpp, tax_income)
    RunningCosts,  ///< running costs of its buildings
    Sales,         ///< City::sales / 480, the moving average per minute
    Purchases,     ///< City::purchases / 480
    Inhabitants,   ///< all of them, as the player status counts them (with the workers of farms)
    Class,         ///< + class: inhabitants of each class (BuildingTable::resident_classes)
};
/// Classes of inhabitants kept: the original's five and room for more.
inline constexpr std::size_t kCityClasses = 8;
inline constexpr std::size_t kCityFigures = static_cast<std::size_t>(CityFigure::Class) + kCityClasses;

struct CitySample {
    std::array<std::int32_t, kCityFigures> value{};

    [[nodiscard]] std::int32_t of(CityFigure f) const noexcept { return value[static_cast<std::size_t>(f)]; }
    [[nodiscard]] std::int32_t of_class(std::size_t c) const noexcept {
        return c < kCityClasses ? value[static_cast<std::size_t>(CityFigure::Class) + c] : 0;
    }
    void set(CityFigure f, std::int64_t v) noexcept;
    void set_class(std::size_t c, std::int64_t v) noexcept;
    bool operator==(const CitySample&) const = default;
};

/// A city's figures by game minute: the last hour one by one, the last twelve hours by quarters
/// (their average minute). Fixed size, integers only.
class CityHistory {
public:
    static constexpr std::size_t kMinutes = 60;
    static constexpr std::size_t kQuarterMinutes = 15;
    static constexpr std::size_t kQuarters = 48;

    void close_minute(const CitySample& sample);
    void clear() noexcept { *this = CityHistory{}; }
    [[nodiscard]] std::uint32_t minutes_closed() const noexcept { return m_state.closed; }
    /// The samples of the last hour, or the last twelve hours by quarters, oldest first.
    [[nodiscard]] std::vector<CitySample> last_minutes() const;
    [[nodiscard]] std::vector<CitySample> last_quarters() const;

    // For the savegame (save/cities.cpp).
    struct State {
        std::array<CitySample, kMinutes> minutes{};
        std::array<CitySample, kQuarters> quarters{};
        std::array<std::int64_t, kCityFigures> quarter{};  ///< sums of the running quarter
        std::uint32_t closed = 0;
        bool operator==(const State&) const = default;
    };
    [[nodiscard]] const State& state() const noexcept { return m_state; }
    void restore(State state) { m_state = std::move(state); }

    bool operator==(const CityHistory&) const = default;

private:
    State m_state;
};

class GoodsLedger {
public:
    static constexpr std::size_t kMinutes = 60;         ///< minutes kept one by one: the last hour
    static constexpr std::size_t kQuarterMinutes = 15;  ///< minutes of a coarse sample
    static constexpr std::size_t kQuarters = 48;        ///< coarse samples kept: the last 12 hours
    static constexpr std::uint16_t kMaxSample = 0xFFFF;

    /// One minute, or the average minute of a quarter of an hour: per flow in 1/32 t (at most
    /// kMaxSample, 2047 t), and the stock at its end.
    struct Sample {
        std::array<std::uint16_t, kGoodsFlows> flow{};
        std::uint32_t stock = 0;

        [[nodiscard]] std::uint32_t of(GoodsFlow f) const noexcept { return flow[static_cast<std::size_t>(f)]; }
        bool operator==(const Sample&) const = default;
    };

    /// What the ledger knows of one good.
    struct Ware {
        std::array<std::uint32_t, kGoodsFlows> current{};  ///< booked in the running minute
        std::array<std::uint32_t, kGoodsFlows> quarter{};  ///< the closed minutes of the running quarter
        std::array<Sample, kMinutes> minutes{};            ///< ring, by minute number
        std::array<Sample, kQuarters> quarters{};          ///< ring, by quarter number
        bool used = false;                                 ///< something was booked once (only these are saved)
        bool warned = false;  ///< the early warning of a shortage was given and not yet taken back

        bool operator==(const Ware&) const = default;
    };

    void add(std::uint8_t ware, GoodsFlow flow, std::uint32_t amount);
    /// The game minute ends: what was booked in it becomes the newest sample, with the stock
    /// (indexed by ware, 1/32 t) at its end. Every 15 minutes a quarter's sample follows.
    void close_minute(const std::vector<std::uint32_t>& stock);
    /// Forgets everything (the city changed hands).
    void clear() noexcept {
        m_wares.clear();
        m_closed = 0;
    }

    /// Minutes closed since the ledger began (or was cleared).
    [[nodiscard]] std::uint32_t minutes_closed() const noexcept { return m_closed; }
    /// The sums of the last `minutes` closed minutes of a ware: one by one up to kMinutes, beyond
    /// that from the quarters (rounded to whole quarters).
    [[nodiscard]] FlowTotals totals(std::uint8_t ware, std::uint32_t minutes) const;
    /// The samples of the last hour, oldest first; fewer at the start of a game.
    [[nodiscard]] std::vector<Sample> last_minutes(std::uint8_t ware) const;
    /// The samples of the last twelve hours by quarters, oldest first.
    [[nodiscard]] std::vector<Sample> last_quarters(std::uint8_t ware) const;
    /// Whether anything was ever booked for the ware.
    [[nodiscard]] bool used(std::uint8_t ware) const noexcept { return ware < m_wares.size() && m_wares[ware].used; }
    [[nodiscard]] bool warned(std::uint8_t ware) const noexcept {
        return ware < m_wares.size() && m_wares[ware].warned;
    }
    void set_warned(std::uint8_t ware, bool on);

    // For the savegame (save/cities.cpp).
    [[nodiscard]] const std::vector<Ware>& wares() const noexcept { return m_wares; }
    void restore(std::vector<Ware> wares, std::uint32_t closed) {
        m_wares = std::move(wares);
        m_closed = closed;
    }

    bool operator==(const GoodsLedger&) const = default;

private:
    Ware& ware(std::uint8_t ware);

    std::vector<Ware> m_wares;  ///< by ware number, as far as one was booked
    std::uint32_t m_closed = 0;
};

}  // namespace o1602::sim
