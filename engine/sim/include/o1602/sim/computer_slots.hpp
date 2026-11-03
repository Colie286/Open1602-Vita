// SPDX-License-Identifier: GPL-3.0-or-later
//
// The slots of a computer player's city record as data (item 5 of docs/modding/open-work.md): what
// a city has of each kind of building, by slot number, with what the computers know of each - the
// producer it builds (a Need), how the survey counts it and the public building it stands for. The
// original's 55 slots keep their numbers (the savegame's city records list them by number); their
// names are those of computers.toml (`bakery`, `chapel`). The rules of the stages that name a slot
// of the original (the weaving, the smelters, the towers) name it by its number (ai_state.hpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/sim/computer_orders.hpp"

namespace o1602::sim {

struct ComputerSlot {
    enum class Kind : std::uint8_t {
        None,      ///< a count of the city (the Kontor's level, its houses, its food producers)
        Producer,  ///< a producer the stage orders name (a Need), counted by the survey
        Tower,     ///< a tower: a Need the orders name as `tower`
        Public,    ///< a public building of the orders
    };

    std::string name;  ///< as computers.toml names it: lower case English, '_'
    std::uint8_t number = 0;
    Kind kind = Kind::None;
    /// The computer's task that builds it (ai_state.hpp's tasks), 0 for none.
    std::uint8_t task = 0;
    /// Producer and Tower: this building (0: the producer of `ware` from `raw` of the building
    /// table); Public: its building.
    std::uint32_t building = 0;

    // A producer's Need (Producer, Tower).
    std::uint8_t ware = 0;   ///< the ware it makes ...
    std::uint8_t raw = 0;    ///< ... from this raw material
    std::uint8_t field = 0;  ///< the crop of its fields (the farm money rule), 0 for none
    ProducerWait wait = ProducerWait::None;
    std::uint8_t attempts = 0;         ///< tried while its attempts are below this
    std::uint8_t fertile = 0;          ///< the island must grow this crop, 0 for no test
    bool from_start = false;           ///< mines: the search starts at the first point of segment 0
    std::uint8_t after = kNoProducer;  ///< the producer the city must have first

    // How the survey counts a building for it: its ware (0: not counted), raw material (0: any)
    // and building (0: any). A slot with a raw material or a building counts before one without.
    std::uint8_t count_ware = 0;
    std::uint8_t count_raw = 0;
    std::uint32_t count_building = 0;

    // A public building (Public).
    std::uint32_t flag = 0;       ///< its "covered" bit of the city record (city_flag)
    std::uint16_t service = 0;    ///< the services of houses that count as covered (house_service)
    std::int32_t min_houses = 0;  ///< houses a site must reach without the service
    bool roads_on_fail = false;   ///< lays roads where it found no site

    friend bool operator==(const ComputerSlot&, const ComputerSlot&) = default;
};

/// The original's slots by number: 54 named slots of 0x00 to 0x36 (0x2A is unused).
[[nodiscard]] const std::vector<ComputerSlot>& original_computer_slots();

/// The slot called `name` of `slots`, nullptr for none.
[[nodiscard]] const ComputerSlot* find_computer_slot(std::span<const ComputerSlot> slots,
                                                     std::string_view name) noexcept;

/// The slot of number `number` of `slots`, nullptr for none.
[[nodiscard]] const ComputerSlot* computer_slot(std::span<const ComputerSlot> slots, std::uint8_t number) noexcept;

}  // namespace o1602::sim
