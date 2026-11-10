// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window "Warenbilanz" over the map, Open1602's own (docs/architecture/0012-goods-statistics.md):
// for every good of the player's cities, all of them or one island's, what is in store, what came
// in and went out per minute over a chosen time (1, 5, 15 or 60 game minutes) and how long the
// stock lasts at that balance. A click on a good shows where it came from and went to, and its
// last hour (or twelve hours) as a chart. Further tabs show the production buildings, the
// inhabitants, the trade at the Kontor and the finances (goods_tabs.cpp). Only the local player's cities: allies and
// trading partners never see each other's figures. The window only reads the world; the game goes on behind it. It is
// drawn as a parchment in the original's style: the owner lays the parchment (parchment.hpp) under what draw() returns.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/form.hpp"

namespace o1602::app {

/// The colours of the window: the form's (translucent, with the inks of ui/font.hpp) and the
/// palette colours of frames, bands and charts.
struct GoodsStyle {
    ui::FormStyle form;
    std::uint8_t dark = 0;    ///< the frame's outline and the ink of lines
    std::uint8_t light = 0;   ///< the frame's lit edge
    std::uint8_t shade = 0;   ///< the frame's shaded edge, separator lines
    std::uint8_t band = 0;    ///< every other row, the ground of bars (at half strength)
    std::uint8_t green = 0;   ///< goods coming in
    std::uint8_t red = 0;     ///< goods going out
    std::uint8_t stock = 0;   ///< the stock in the chart (at half strength)
    std::uint8_t shadow = 0;  ///< the window's shadow (at half strength)
    /// The lines of the charts of the inhabitants and the finances, one per series.
    std::array<std::uint8_t, 6> series{};
};

class GoodsWindow {
public:
    enum class Result : std::uint8_t { None, Close };
    /// The tabs, left to right.
    enum class Tab : std::uint8_t { Goods, Producers, Population, Trade, Finances };
    static constexpr std::size_t kTabs = 5;
    /// The window's size in design units (it is centred over the map).
    static constexpr std::int32_t kWidth = 744;
    static constexpr std::int32_t kHeight = 690;
    /// The time the figures are averaged over, game minutes; the default is the second.
    static constexpr std::array<std::uint32_t, 4> kWindows = {1, 5, 15, 60};
    /// Rows of the table shown at once.
    static constexpr std::size_t kRows = 16;

    /// The name the build menu gives a building (Text.cod), by its Id.
    using BuildingNames = std::function<std::string(std::uint32_t)>;

    /// `ware_names`: Text.cod [WARE] by ware number; `ware_icon`: the sprite of ware 0 in TOOLS
    /// (GFXWARE).
    GoodsWindow(GoodsStyle style, std::vector<std::string> ware_names, std::int32_t ware_icon,
                BuildingNames building_names = {});

    /// The names of the classes of inhabitants (Text.cod [GAME] 3 and on), for the tab "Bevölkerung".
    void set_class_names(std::vector<std::string> names) { m_class_names = std::move(names); }
    void set_scale(double scale) { m_form.set_scale(scale); }
    [[nodiscard]] double scale() const noexcept { return m_form.scale(); }

    /// Opens on the table (the island and time chosen before stay).
    void open() noexcept {
        m_detail.reset();
        m_scroll = 0;
    }
    /// Opens on the page of `ware` for the player's cities on `island` (a click on the good in a
    /// Kontor's stock).
    void open_ware(std::uint8_t ware, std::uint16_t island) noexcept {
        m_tab = Tab::Goods;
        m_detail = ware;
        m_island = island;
        m_scroll = 0;
    }
    /// Escape: from a good's page back to the table; false on the table (the window closes).
    bool back() noexcept {
        if (!m_detail) {
            return false;
        }
        m_detail.reset();
        return true;
    }
    /// The mouse wheel over the window: notches, up positive.
    void wheel(std::int32_t notches) noexcept;

    /// Everything to draw for `player`'s cities, relative to the window's top left, over the
    /// parchment. It lays the window out; mouse() answers what was drawn last.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const sim::World& world, const assets::BuildingTable& buildings,
                                                 const assets::FigureTable& figures, sim::PlayerId player,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The mouse in pixels relative to the window's top left.
    Result mouse(std::int32_t x, std::int32_t y, bool down, ui::MouseButton button = ui::MouseButton::Left);

    // The view, for the tests.
    [[nodiscard]] std::uint32_t minutes() const noexcept { return kWindows[m_window]; }
    [[nodiscard]] std::optional<std::uint16_t> island() const noexcept { return m_island; }
    [[nodiscard]] std::optional<std::uint8_t> detail() const noexcept { return m_detail; }
    [[nodiscard]] Tab tab() const noexcept { return m_tab; }
    [[nodiscard]] const ui::Form& form() const noexcept { return m_form; }

    /// Tenths of a ton per minute as text, e.g. "12.5" ("12,5" in German): `units` 1/32 t over
    /// `minutes`. With `sign` a "+" before what is above zero.
    [[nodiscard]] static std::string rate_text(std::int64_t units, std::uint32_t minutes, bool sign = false);
    /// How long a stock lasts: "12 min", "3 h"; empty if it does not fall.
    [[nodiscard]] static std::string lasts_text(std::optional<std::uint32_t> minutes);

private:
    class Painter;
    void layout_head(const sim::World& world, sim::PlayerId player, Painter& below, Painter& above);
    void layout_table(const sim::World& world, sim::PlayerId player, Painter& below, Painter& above);
    void layout_detail(const sim::World& world, const assets::BuildingTable& buildings, sim::PlayerId player,
                       std::uint8_t ware, Painter& below, Painter& above);
    // The other tabs (goods_tabs.cpp).
    void layout_tabs();
    void layout_producers(const sim::World& world, const assets::BuildingTable& buildings,
                          const assets::FigureTable& figures, sim::PlayerId player, Painter& below, Painter& above);
    void layout_population(const sim::World& world, const assets::BuildingTable& buildings, sim::PlayerId player,
                           Painter& below, Painter& above);
    void layout_trade(const sim::World& world, sim::PlayerId player, Painter& below, Painter& above);
    void layout_finances(const sim::World& world, const assets::BuildingTable& buildings,
                         const assets::FigureTable& figures, sim::PlayerId player, Painter& below, Painter& above);
    /// A line chart of the last hour (or twelve hours) at `top`, down to above the footer, with
    /// the buttons that choose the time; `series` by colour and legend text.
    struct Series {
        std::uint8_t colour = 0;
        std::string label;
        std::vector<std::int64_t> values;  ///< oldest first, at most one per slot
    };
    void layout_chart(std::int32_t top, const std::vector<Series>& series, Painter& below, Painter& above);
    /// The scroll bar of the table as last laid out (m_row_count rows, m_visible_rows of them shown).
    void layout_scroll_bar(Painter& below);
    /// The name of `ware`: Text.cod's for the original's goods, the package's for its own.
    [[nodiscard]] std::string ware_name(const sim::World& world, std::uint8_t ware) const;
    /// The name of class `k` of inhabitants: the package's, else Text.cod's for the original's.
    [[nodiscard]] std::string class_name(const assets::BuildingTable& buildings, std::size_t k) const;
    /// The sprite of the icon of `ware`: a package's good shows that of a good of the original.
    [[nodiscard]] std::int32_t ware_icon(const sim::World& world, std::uint8_t ware) const;

    GoodsStyle m_style;
    std::vector<std::string> m_ware_names;
    std::int32_t m_ware_icon = 0;
    BuildingNames m_building_names;
    std::vector<std::string> m_class_names;
    ui::Form m_form;

    Tab m_tab = Tab::Goods;
    std::optional<std::uint16_t> m_island;  ///< nothing: all of the player's islands
    std::size_t m_window = 1;               ///< index into kWindows
    bool m_all_goods = false;               ///< also goods without stock and movement
    std::size_t m_scroll = 0;               ///< first row of the table shown
    std::size_t m_row_count = 0;            ///< rows of the table as last laid out
    std::size_t m_visible_rows = kRows;     ///< rows of it shown at once
    std::optional<std::uint8_t> m_detail;   ///< the good whose page is shown
    bool m_long_chart = false;              ///< the page's chart over twelve hours instead of one
    std::vector<std::uint16_t> m_islands;   ///< the player's islands as last laid out
    std::vector<std::uint8_t> m_row_wares;  ///< the product of each row of the tab "Betriebe"
};

}  // namespace o1602::app
