// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "o1602/sim/random.hpp"

namespace o1602::sim {

/// The saved state of one package's scripts (scripting.hpp, savegame section SCRP).
struct ScriptState {
    std::string package;             ///< the package's id
    Random random;                   ///< its math.random
    std::vector<std::uint8_t> data;  ///< its table `state` (script::Sandbox::save_table)
    friend bool operator==(const ScriptState&, const ScriptState&) = default;
};

}  // namespace o1602::sim
