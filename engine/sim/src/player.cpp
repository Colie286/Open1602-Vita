// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/player.hpp"

namespace o1602::sim {

const char* role_name(PlayerRole role) noexcept {
    switch (role) {
        case PlayerRole::Human:
            return "human";
        case PlayerRole::Computer:
            return "computer";
        case PlayerRole::Neutral:
            return "neutral";
    }
    return "?";
}

}  // namespace o1602::sim
