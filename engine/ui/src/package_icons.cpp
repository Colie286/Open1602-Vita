// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/package_icons.hpp"

#include <utility>

namespace o1602::ui {

namespace {

std::vector<assets::PackageIcon>& icons() {
    static std::vector<assets::PackageIcon> list;
    return list;
}

}  // namespace

void set_package_icons(std::vector<assets::PackageIcon> list) {
    icons() = std::move(list);
}

std::span<const assets::PackageIcon> package_icons() noexcept {
    return icons();
}

std::optional<std::int32_t> package_icon_sprite(std::string_view id) noexcept {
    if (const assets::PackageIcon* icon = assets::find_package_icon(icons(), id)) {
        return icon->sprite;
    }
    return std::nullopt;
}

std::int32_t ware_sprite(std::int32_t gfx_ware, const assets::Goods& goods, std::uint32_t w) noexcept {
    if (const std::string_view picture = goods.picture(w); !picture.empty()) {
        if (const auto sprite = package_icon_sprite(picture)) {
            return *sprite;
        }
    }
    return gfx_ware + goods.icon(w);
}

}  // namespace o1602::ui
