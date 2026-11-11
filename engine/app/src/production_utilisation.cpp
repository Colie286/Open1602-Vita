// SPDX-License-Identifier: GPL-3.0-or-later
#include "production_utilisation.hpp"

#include <algorithm>

#include "o1602/sim/production_cycle.hpp"

namespace o1602::app {

std::int64_t production_utilisation(const sim::Production& p, const assets::BuildingType& b,
                                    const assets::FigureTable& figures) {
    const std::int64_t window = p.elapsed;
    std::int64_t n = 0;
    switch (b.prod_kind) {
        case 1:  // HANDWERK
        case 2:  // PLANTAGE
        case 4:  // WEIDETIER
        case 5:  // JAGDHAUS
        case 6:  // FISCHEREI
            // Farms, hunters and fishers with the walk of their worker for the raw material.
            if (const std::uint32_t cycle = sim::full_speed_seconds(b, &figures); cycle != 0) {
                n = window * 32 / cycle;
            }
            break;
        default:
            return 100;
    }
    return n == 0 ? 100 : std::min<std::int64_t>(100, std::int64_t{p.produced} * 100 / n);
}

}  // namespace o1602::app
