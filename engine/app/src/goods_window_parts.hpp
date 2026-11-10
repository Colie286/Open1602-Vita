// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the parts of the window "Warenbilanz" share (goods_window.cpp, goods_tabs.cpp): the ids of
// its widgets, its measures, the groups of goods and the painter of what is not a widget. Only for
// those two files.
#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "goods_window.hpp"

#include "o1602/assets/game_data.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app::goods_parts {

using Id = ui::Form::Id;
using sim::GoodsFlow;
using ui::Align;
using ui::Rect;

constexpr Id kIsland = 1;
constexpr Id kWindow = 2;
constexpr Id kAllGoods = 3;
constexpr Id kClose = 4;
constexpr Id kBack = 5;
constexpr Id kChartHour = 6;
constexpr Id kChartDay = 7;
constexpr Id kPageUp = 8;
constexpr Id kPageDown = 9;
constexpr Id kTab = 10;           // + GoodsWindow::Tab
constexpr Id kRow = 100;          // + ware
constexpr Id kProducerRow = 400;  // + row of the tab "Betriebe"

constexpr std::int32_t kFontNormal = 0;
constexpr std::int32_t kFontSmall = 1;   // column heads, axes, hints
constexpr std::int32_t kFontMedium = 3;  // the rows, with a shadow

constexpr std::int32_t kMargin = 14;
constexpr std::int32_t kSelectorX = 188;
constexpr std::int32_t kSelectorY = 44;
constexpr std::int32_t kSelectorW = 368;
constexpr std::int32_t kSelectorH = 28;
constexpr std::int32_t kHeaderY = 114;
constexpr std::int32_t kTableY = 140;
constexpr std::int32_t kRowH = 30;
constexpr std::int32_t kFooterY = GoodsWindow::kHeight - 40;
constexpr std::int32_t kScrollX = GoodsWindow::kWidth - 18;
constexpr std::int32_t kScrollW = 6;
/// The tabs, under the island.
constexpr std::int32_t kTabY = 84;
constexpr std::int32_t kTabW = (GoodsWindow::kWidth - 2 * kMargin - 4 * 4) / 5;
constexpr std::int32_t kTabH = 26;

/// The groups of the table, in the order of the original's Kontor window.
struct Group {
    std::string_view title;
    std::vector<std::uint8_t> wares;
};
inline const std::array<Group, 4>& groups() {
    namespace w = assets::ware;
    static const std::array<Group, 4> list = {{
        {"stats.group.consumer",
         {w::kFood, w::kTobaccoGoods, w::kSpices, w::kCocoa, w::kAlcohol, w::kCloth, w::kClothes, w::kJewellery}},
        {"stats.group.building", {w::kTools, w::kWood, w::kBricks}},
        {"stats.group.raw",
         {w::kOre, w::kGold, w::kWool, w::kSugar, w::kTobacco, w::kMeat, w::kGrain, w::kFlour, w::kIron}},
        {"stats.group.weapons", {w::kSwords, w::kMuskets, w::kCannons}},
    }};
    return list;
}

/// The groups for `player`'s cities: the original's goods, then any other good booked in them (one
/// that a mod adds) under "Further goods", by its number.
inline std::vector<Group> groups_of(const sim::World& world, sim::PlayerId player) {
    std::vector<Group> out(groups().begin(), groups().end());
    const auto known = [&](std::uint8_t ware) {
        return std::any_of(out.begin(), out.end(), [&](const Group& g) {
            return std::find(g.wares.begin(), g.wares.end(), ware) != g.wares.end();
        });
    };
    std::vector<std::uint8_t> further;
    for (const sim::City& c : world.cities()) {
        if (c.owner != player) {
            continue;
        }
        const std::size_t count = std::min<std::size_t>(c.ledger.wares().size(), 256);
        for (std::size_t w = 2; w < count; ++w) {
            const auto ware = static_cast<std::uint8_t>(w);
            if (c.ledger.used(ware) && !known(ware) &&
                std::find(further.begin(), further.end(), ware) == further.end()) {
                further.push_back(ware);
            }
        }
    }
    if (!further.empty()) {
        std::sort(further.begin(), further.end());
        out.push_back(Group{"stats.group.other", std::move(further)});
    }
    return out;
}

inline std::uint64_t tons(std::uint64_t units) {
    return units / assets::kStockUnitsPerTon;
}

}  // namespace o1602::app::goods_parts

namespace o1602::app {

/// Draw items in design units, turned into pixels of the form's scale.
class GoodsWindow::Painter {
public:
    using Rect = ui::Rect;
    using Align = ui::Align;

    Painter(double scale, const std::array<const ui::Font*, ui::kFontSlots>& fonts) : m_scale(scale), m_fonts(fonts) {}

    [[nodiscard]] std::vector<ui::DrawItem>& items() noexcept { return m_out; }

    /// A rectangle of a palette colour; `half`: at half strength over what lies below.
    void fill(Rect r, std::uint8_t colour, bool half = false) {
        if (r.w <= 0 || r.h <= 0) {
            return;
        }
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Fill;
        d.x = px(r.x);
        d.y = px(r.y);
        d.w = std::max(1, px(r.x + r.w) - d.x);
        d.h = std::max(1, px(r.y + r.h) - d.y);
        d.colour = colour;
        d.half = half;
        m_out.push_back(d);
    }
    /// An outline `width` thick.
    void outline(Rect r, std::uint8_t colour, std::int32_t width = 1) {
        fill(Rect{r.x, r.y, r.w, width}, colour);
        fill(Rect{r.x, r.y + r.h - width, r.w, width}, colour);
        fill(Rect{r.x, r.y + width, width, r.h - 2 * width}, colour);
        fill(Rect{r.x + r.w - width, r.y + width, width, r.h - 2 * width}, colour);
    }
    /// A triangle pointing left or right, its tip at (x, y).
    void arrow(std::int32_t x, std::int32_t y, bool left, std::int32_t size, std::uint8_t colour) {
        for (std::int32_t i = 0; i < size; ++i) {
            fill(Rect{left ? x + i : x - i, y - i, 1, 2 * i + 1}, colour);
        }
    }
    /// Text in the box, vertically centred and cut to its width.
    void text(Rect r, std::string t, Align align, std::int32_t colour, std::int32_t font = goods_parts::kFontNormal) {
        const ui::Font* f = m_fonts[static_cast<std::size_t>(font)];
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = font;
        d.colour = colour;
        d.align = align;
        d.text = ui::fit_text(f, std::move(t), std::max(r.w, 0), false);
        const std::int32_t x = align == Align::Left ? r.x : align == Align::Right ? r.x + r.w : r.x + r.w / 2;
        d.x = px(x);
        d.y = px(ui::text_bottom(f, r.y, r.h));
        m_out.push_back(std::move(d));
    }
    /// Text wrapped into lines `line` apart, as many as fit the box.
    void paragraph(Rect r, const std::string& t, std::int32_t colour, std::int32_t font, std::int32_t line) {
        const ui::Font* f = m_fonts[static_cast<std::size_t>(font)];
        const std::vector<std::string> lines =
            f != nullptr ? ui::wrap_text(*f, t, static_cast<std::uint32_t>(std::max(0.0, r.w * f->design_scale())))
                         : std::vector<std::string>{t};
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::int32_t y = r.y + static_cast<std::int32_t>(i) * line;
            if (y + line > r.y + r.h) {
                break;
            }
            text(Rect{r.x, y, r.w, line}, lines[i], Align::Left, colour, font);
        }
    }
    /// A sprite of TOOLS fitted into the box.
    void sprite(Rect r, std::int32_t gfx) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Sprite;
        d.block = 0;
        d.gfx = gfx;
        d.x = px(r.x);
        d.y = px(r.y);
        d.w = px(r.x + r.w) - d.x;
        d.h = px(r.y + r.h) - d.y;
        m_out.push_back(d);
    }

private:
    [[nodiscard]] std::int32_t px(std::int32_t v) const { return ui::to_pixels(v, m_scale); }

    std::vector<ui::DrawItem> m_out;
    double m_scale;
    const std::array<const ui::Font*, ui::kFontSlots>& m_fonts;
};

}  // namespace o1602::app
