// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/package_icons.hpp"

#include <utility>

#include "o1602/core/embedded_file.hpp"
#include "o1602/core/png_reader.hpp"

namespace o1602::ui::interface_icons {
extern const core::EmbeddedFile kEmbeddedFiles[];
}  // namespace o1602::ui::interface_icons

namespace o1602::ui {

namespace {

std::vector<assets::PackageIcon>& icons() {
    static std::vector<assets::PackageIcon> list;
    return list;
}

std::vector<assets::PackageArms>& arms() {
    static std::vector<assets::PackageArms> list;
    return list;
}

}  // namespace

std::vector<assets::PackageIcon> builtin_icons() {
    std::vector<assets::PackageIcon> list;
    for (const core::EmbeddedFile* f = interface_icons::kEmbeddedFiles; f->name != nullptr; ++f) {
        const auto size = core::png_size(std::span<const std::uint8_t>(f->data, f->size));
        if (!size) {
            continue;  // checked by the tests; a broken file is left out
        }
        assets::PackageIcon icon;
        icon.id = std::string("open1602:") + f->name;
        icon.sprite = assets::kFirstPackageIcon + static_cast<std::int32_t>(list.size());
        icon.width = size->width;
        icon.height = size->height;
        icon.data = f->data;
        icon.size = f->size;
        list.push_back(std::move(icon));
    }
    return list;
}

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

void set_package_arms(std::vector<assets::PackageArms> list) {
    arms() = std::move(list);
}

std::optional<std::int32_t> package_arms_sprite(std::string_view banner) noexcept {
    if (const assets::PackageArms* a = assets::find_package_arms(arms(), banner)) {
        return package_icon_sprite(a->icon);
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
