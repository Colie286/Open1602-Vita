// SPDX-License-Identifier: GPL-3.0-or-later
// "Insel erkunden": a ship explores the crops and deposits of the island it stands in
// (docs/re/ships.md#exploring). Part of sim::Fleet.

#include "o1602/sim/game_data.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint8_t kExploreSteps = 11;      // ship + 0x1A4 after the click
constexpr std::uint32_t kExploreStepMs = 1500;  // timer type 2, 15 tenths
constexpr std::uint8_t kCropsStep = 10;         // 3.0 s after the click
constexpr std::uint8_t kFirstOreStep = 7;       // 7.5 s
constexpr std::uint8_t kSecondOreStep = 5;      // 10.5 s
constexpr std::uint8_t kGoldStep = 2;           // 15.0 s

bool moving(const Ship& ship) noexcept {
    return ship.motion.phase == ShipMotion::Phase::Run || ship.motion.phase == ShipMotion::Phase::Turn;
}

}  // namespace

std::optional<std::uint16_t> Fleet::island_at_ship(const Ship& ship) const {
    // 0x0046A570: the rectangle itself, without the margin of 5 tiles that discovering uses.
    for (std::size_t i = 0; i < m_world->islands().size(); ++i) {
        const WorldIsland& isl = m_world->islands()[i];
        if (ship.x >= isl.x && ship.x < isl.x + isl.map.width() && ship.y >= isl.y &&
            ship.y < isl.y + isl.map.height()) {
            return static_cast<std::uint16_t>(i);
        }
    }
    return std::nullopt;
}

ExploreState Fleet::explore_state(const Ship& ship, PlayerId player) const {
    if (ship.sinking || ship.cart() || (player != kNoPlayer && ship.owner != player)) {
        return ExploreState::NotAllowed;
    }
    if (ship.explore_steps != 0) {
        return ExploreState::Running;
    }
    if (moving(ship)) {
        return ExploreState::Moving;
    }
    const auto island = island_at_ship(ship);
    if (!island) {
        return ExploreState::NoIsland;
    }
    // 0x0046B260: done when the crops are known and no ore or gold deposit is unknown. Unlike the
    // original, a shipyard in reach or a pirate Kontor does not hide the button (U-013).
    const WorldIsland& isl = m_world->islands()[*island];
    if (!isl.explored_by(ship.owner)) {
        return ExploreState::Possible;
    }
    for (const auto& d : isl.header.deposits) {
        if ((d.ware == ware::kOre || d.ware == ware::kGold) && !deposit_known(d, ship.owner)) {
            return ExploreState::Possible;
        }
    }
    return ExploreState::Done;
}

bool Fleet::explore(const ExploreOrder& order) {
    Ship* ship = m_world->ship(order.ship);
    // A second click while exploring is refused (the original starts a second timer, U-012).
    if (ship == nullptr || explore_state(*ship, order.player) != ExploreState::Possible) {
        return false;
    }
    ship->explore_steps = kExploreSteps;
    ship->explore_next_ms = m_world->time_ms() + kExploreStepMs;
    return true;
}

void Fleet::stop_exploring(Ship& ship) {
    if (ship.explore_steps == 0) {
        return;
    }
    ship.explore_steps = 0;
    m_events.push_back(ShipEvent{ShipEvent::Kind::ExploreFinished, ship.id, ship.owner, 0xFFFF});
}

void Fleet::explore_tick() {
    for (Ship& ship : m_world->ships()) {
        if (ship.explore_steps == 0) {
            continue;
        }
        if (moving(ship) || ship.sinking) {
            stop_exploring(ship);  // under way again (0x00452370)
            continue;
        }
        while (ship.explore_steps != 0 && m_world->time_ms() >= ship.explore_next_ms) {
            explore_step(ship);
        }
    }
}

void Fleet::explore_step(Ship& ship) {
    const auto found = island_at_ship(ship);
    if (!found) {
        stop_exploring(ship);  // the original ends without resetting the counter (U-011)
        return;
    }
    WorldIsland& isl = m_world->island(*found);
    const PlayerId p = ship.owner;
    const std::uint8_t n = ship.explore_steps;
    if (n == kCropsStep && !isl.explored_by(p)) {
        // 0x0046ABF0: message 0x2A at the centre of the island.
        isl.explore(p);
        ShipEvent e{ShipEvent::Kind::CropsExplored, ship.id, p, *found};
        e.x = static_cast<std::uint16_t>(isl.map.width() / 2);
        e.y = static_cast<std::uint16_t>(isl.map.height() / 2);
        m_events.push_back(e);
    } else if (n == kFirstOreStep || n == kSecondOreStep || n == kGoldStep) {
        // 0x0046C100: deposits from the end of the list; one ore deposit, or up to two gold veins.
        const std::uint8_t ware = n == kGoldStep ? ware::kGold : ware::kOre;
        std::uint32_t left = n == kGoldStep ? 2U : 1U;
        auto& deposits = isl.header.deposits;
        for (auto it = deposits.rbegin(); it != deposits.rend() && left != 0; ++it) {
            if (it->ware != ware || deposit_known(*it, p) || p >= 32) {
                continue;
            }
            it->mask |= 1U << p;
            --left;
            ShipEvent e{ShipEvent::Kind::DepositFound, ship.id, p, *found};
            e.ware = ware;
            e.x = it->x;
            e.y = it->y;
            m_events.push_back(e);
        }
    }
    if (n <= 1) {
        ship.explore_steps = 0;
        m_events.push_back(ShipEvent{ShipEvent::Kind::ExploreFinished, ship.id, p, *found});
        return;
    }
    ship.explore_steps = static_cast<std::uint8_t>(n - 1);
    ship.explore_next_ms += kExploreStepMs;
}

}  // namespace o1602::sim
