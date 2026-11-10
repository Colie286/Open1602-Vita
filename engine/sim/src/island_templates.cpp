// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/island_templates.hpp"

#include <algorithm>
#include <cstdio>

namespace o1602::sim::templates {

std::string template_path(std::uint8_t climate, std::uint8_t size, std::uint16_t number, bool natives) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s/%s%02u.scp", (natives ? kNativeDir : kClimateDir)[climate & 1U],
                  kSizeName[std::min<std::size_t>(size, kSizeClasses - 1)], static_cast<unsigned>(number));
    return buf;
}

}  // namespace o1602::sim::templates
