// SPDX-License-Identifier: GPL-3.0-or-later
#include "production_utilisation.hpp"

#include <algorithm>

namespace o1602::app {

std::int64_t production_utilisation(const sim::Production& p, const assets::BuildingType& b,
                                    const assets::FigureTable& figures) {
    const std::int64_t window = p.elapsed;
    std::int64_t n = 0;
    switch (b.prod_kind) {
        case 1:  // HANDWERK
        case 4:  // WEIDETIER
            n = b.interval != 0 ? window * 32 / b.interval : 0;
            break;
        case 2:    // PLANTAGE
        case 5:    // JAGDHAUS
        case 6: {  // FISCHEREI
            const double reach = b.radius <= 1 ? 2.2 : b.radius == 2 ? 3.0 : b.radius == 3 ? 4.0 : 5.0;
            const assets::FigureType* worker = figures.get(b.worker_figure);
            const double speed = worker != nullptr ? worker->speed * 0.0001 : 0.0;
            const double work = worker != nullptr ? worker->work_time_ms / 1000.0 : 0.0;
            const auto walk = speed > 0.0 ? static_cast<std::int64_t>(reach / (speed * 50.0) + work) : 0;
            const std::int64_t cycle = ((walk * b.raw_amount) >> 5) + b.interval;
            n = cycle != 0 ? window * 32 / cycle : 0;
            break;
        }
        default:
            return 100;
    }
    return n == 0 ? 100 : std::min<std::int64_t>(100, std::int64_t{p.produced} * 100 / n);
}

}  // namespace o1602::app
