// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "o1602/sim/random.hpp"

namespace o1602::sim {

/// The saved state of one package's scripts (scripting.hpp, savegame section SCRP).
struct ScriptState {
    std::string package;             ///< the package's id, or the scenario's ("<package>:<name>")
    Random random;                   ///< its math.random
    std::vector<std::uint8_t> data;  ///< its table `state` (script::Sandbox::save_table)
    /// Its table `campaign`, which goes on to the next part of a package's campaign; empty for none.
    std::vector<std::uint8_t> campaign;
    friend bool operator==(const ScriptState&, const ScriptState&) = default;
};

}  // namespace o1602::sim
