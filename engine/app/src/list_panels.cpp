// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ship list and the city list (SHIPLIST.GAD, STADTLST.GAD; ShipList_Refresh 0x00434020,
// CityList_Refresh 0x00434200; docs/re/ui.md): opened from the player status or with L and
// Shift+L, 13 rows a page; a click on a name goes there (a ship is also selected). Open1602 (FB-6):
// the mouse wheel scrolls row by row, a click on the heading orders the list (ships by name, idle
// first or damaged first; cities by name), and the list is gathered a few times a second rather
// than every frame.
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "banner_graphic.hpp"
#include "panel_common.hpp"

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

constexpr std::size_t kRows = 13;
constexpr std::uint64_t kCollectMs = 500;  ///< game time between two gatherings of an open list

/// The orders of a list: the original's (the ship or city table), then Open1602's.
enum class Order : std::uint8_t { Table, Name, Idle, Damaged };

class ListPanel final : public SelectionPanel {
public:
    ListPanel(assets::GadgetLayout layout, bool ships)
        : m_panel(std::move(layout), 1.0), m_ships(ships), m_base(ships ? 61000 : 63000) {
        const assets::GadgetLayout& l = m_panel.layout();
        if (const auto g = l.index_of(m_base + kArms)) {
            m_arms = l.gadgets[*g].gfx;
        }
        if (const auto g = l.index_of(m_base + kIcon)) {
            m_icons = l.gadgets[*g].gfx;
        }
    }

    void set_scale(double scale) override { m_panel.set_scale(scale); }
    ui::Panel& panel() override { return m_panel; }

    void refresh(const Selection& /*selection*/, const PanelContext& c) override {
        const std::uint64_t now = c.world().time_ms();
        if (m_stale || now < m_collected_ms || now - m_collected_ms >= kCollectMs) {
            collect(c);
            m_collected_ms = now;
            m_stale = false;
        }
        // The page is kept within the list (it shrinks when ships sink or cities change hands).
        m_first = std::min(m_first, last_first());
        // The original leaves the heading empty; Open1602 names the list and counts its rows. An
        // order chosen takes the place of both: the banner holds about 185 px before the arms.
        const std::string heading = panel_detail::help_line(c, m_ships ? 214 : 215);
        const std::string count = std::to_string(m_rows.size());
        m_panel.set_text(m_base + kHeading, m_order == Order::Table ? heading + " (" + count + ")"
                                                                    : ui::tr("list.heading.ordered",
                                                                             {heading, count, order_name(m_order)}));
        const render::BannerArt arms = render::BannerArt::at(m_arms);
        set_banner_graphic(m_panel, m_base + kArms, arms, c.banner(c.local));
        set_banner_graphic(m_panel, m_base + kArmsShadow, arms, c.banner(c.local));
        m_panel.set_visible(m_base + kPageDown, m_first + kRows < m_rows.size());
        m_panel.set_visible(m_base + kPageUp, m_first > 0);
        for (std::size_t k = 0; k < kRows; ++k) {
            const std::int32_t row = static_cast<std::int32_t>(k);
            const bool shown = m_first + k < m_rows.size();
            for (const std::int32_t id : {kBoard, kName, kIcon}) {
                m_panel.set_visible(m_base + id + row, shown);
            }
            if (!shown) {
                continue;
            }
            const Row& r = m_rows[m_first + k];
            m_panel.set_text(m_base + kName + row, r.name);
            if (r.owner) {
                // Cities: the small coat of arms of the owner (icons 4..7).
                set_banner_graphic(m_panel, m_base + kIcon + row, render::BannerArt::at(m_icons + kCityIcons),
                                   c.banner(*r.owner));
            } else {
                m_panel.set_graphic(m_base + kIcon + row, m_icons + r.icon, 0);
            }
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& /*selection*/, const PanelContext& /*c*/,
                 std::vector<sim::Command>& /*out*/) override {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        if (e.id == m_base + kPageDown && m_first + kRows < m_rows.size()) {
            m_first += kRows;
        } else if (e.id == m_base + kPageUp) {
            m_first -= std::min(m_first, kRows);
        } else if (e.id == m_base + kHeading) {
            // The next order; cities have no idle or damaged ones.
            const Order last = m_ships ? Order::Damaged : Order::Name;
            m_order = m_order == last ? Order::Table : static_cast<Order>(static_cast<std::uint8_t>(m_order) + 1);
            m_first = 0;
            m_stale = true;
        } else if (e.id >= m_base + kName && e.id < m_base + kName + static_cast<std::int32_t>(kRows)) {
            const std::size_t i = m_first + static_cast<std::size_t>(e.id - m_base - kName);
            if (i < m_rows.size()) {
                const Row& r = m_rows[i];
                Request q;
                q.kind = m_ships ? Request::Kind::SelectShip : Request::Kind::Centre;
                q.ship = r.ship;
                q.x = r.x;
                q.y = r.y;
                m_request = q;
            }
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& /*selection*/,
                                                  const PanelContext& c) const override {
        const std::int32_t k = id - m_base;
        if (k == kHeading) {
            return ui::tr(m_ships ? "list.order.help.ships" : "list.order.help.cities");
        }
        if (k >= kName && k < kName + static_cast<std::int32_t>(kRows)) {
            return panel_detail::help_line(c, m_ships ? 221 : 223);
        }
        if (k >= kIcon && k < kIcon + static_cast<std::int32_t>(kRows)) {
            const std::size_t i = m_first + static_cast<std::size_t>(k - kIcon);
            if (!m_ships) {
                return panel_detail::help_line(c, 222);
            }
            if (i < m_rows.size()) {
                // 218 on a trade route, 219 under way, 220 at anchor.
                constexpr std::array<std::size_t, 3> kLines = {218, 220, 219};
                return panel_detail::help_line(c, kLines[static_cast<std::size_t>(std::clamp(m_rows[i].icon, 0, 2))]);
            }
        }
        return std::nullopt;
    }

    /// Every opening starts at the first page (0x0043CE00), with the list as it is now.
    void opened() override {
        m_first = 0;
        m_stale = true;
    }

    /// A row per notch, within the list (FB-6): down to where the last row is the page's last.
    bool wheel(std::int32_t notches) override {
        const auto first = static_cast<std::int64_t>(m_first) - notches;
        // The page buttons may have left the last page further down; the wheel does not jump back.
        const auto last = std::max(static_cast<std::int64_t>(m_rows.size() > kRows ? m_rows.size() - kRows : 0),
                                   static_cast<std::int64_t>(m_first));
        m_first = static_cast<std::size_t>(std::clamp<std::int64_t>(first, 0, last));
        return true;
    }

    [[nodiscard]] std::optional<Request> take_request() override { return std::exchange(m_request, std::nullopt); }

private:
    static constexpr std::int32_t kHeading = 2;
    static constexpr std::int32_t kArmsShadow = 3;
    static constexpr std::int32_t kArms = 4;
    static constexpr std::int32_t kPageDown = 5;  ///< the next 13
    static constexpr std::int32_t kPageUp = 6;
    static constexpr std::int32_t kBoard = 11;
    static constexpr std::int32_t kName = 31;
    static constexpr std::int32_t kIcon = 51;
    static constexpr std::int32_t kCityIcons = 4;  ///< the cities' small coats of arms after the ships' icons

    /// The first row of the last page as the page buttons reach it.
    [[nodiscard]] std::size_t last_first() const noexcept {
        return m_rows.empty() ? 0 : (m_rows.size() - 1) / kRows * kRows;
    }

    [[nodiscard]] static std::string order_name(Order order) {
        switch (order) {
            case Order::Name:
                return ui::tr("list.order.name");
            case Order::Idle:
                return ui::tr("list.order.idle");
            case Order::Damaged:
                return ui::tr("list.order.damaged");
            case Order::Table:
                break;
        }
        return {};
    }

    struct Row {
        std::string name;
        std::int32_t icon = 0;               ///< ships: 0 trade route, 1 anchored, 2 under way
        double health = 1.0;                 ///< ships: 0..1
        std::optional<sim::PlayerId> owner;  ///< cities: whose coat of arms
        std::uint32_t ship = 0;
        double x = 0.0;  ///< world tile to go to
        double y = 0.0;
    };

    void collect(const PanelContext& c) {
        m_rows.clear();
        const sim::World& w = c.world();
        if (m_ships) {
            // All own ships that are not sinking, in the order of the ship table (0x0044F000).
            for (const sim::Ship& s : w.ships()) {
                if (s.owner != c.local || s.sinking) {
                    continue;
                }
                const sim::ShipMotion& m = s.motion;
                const bool moving = m.phase == sim::ShipMotion::Phase::Run || m.phase == sim::ShipMotion::Phase::Turn ||
                                    m.path_index < m.path.size();
                Row r;
                r.name = ship_name(s);
                r.icon = s.route.active && s.route.has_stops() ? 0 : (moving ? 2 : 1);
                r.ship = s.id;
                r.x = s.x;
                r.y = s.y;
                const assets::FigureType* t = c.figures.get(s.figure);
                if (t != nullptr && t->ship.max_health != 0 && s.health != 0) {
                    r.health = std::min(1.0, static_cast<double>(s.health) / t->ship.max_health);
                }
                m_rows.push_back(std::move(r));
            }
            sort_rows();
            return;
        }
        // Cities of humans and computers: the player's own and those of trade partners.
        for (const sim::City& city : w.cities()) {
            const sim::Player* owner = w.player(city.owner);
            if (owner == nullptr || city.island >= w.islands().size() ||
                (owner->role != sim::PlayerRole::Human && owner->role != sim::PlayerRole::Computer)) {
                continue;
            }
            if (city.owner != c.local && (c.local == sim::kNoPlayer || !w.diplomacy().may_trade(c.local, city.owner))) {
                continue;
            }
            const sim::WorldIsland& island = w.islands()[city.island];
            Row r;
            r.name = city_name(city);
            r.owner = city.owner;
            r.x = island.x + (city.warehouse_x ? *city.warehouse_x : island.map.width() / 2.0);
            r.y = island.y + (city.warehouse_y ? *city.warehouse_y : island.map.height() / 2.0);
            m_rows.push_back(std::move(r));
        }
        sort_rows();
    }

    /// The chosen order; rows alike keep the table's.
    void sort_rows() {
        switch (m_order) {
            case Order::Table:
                break;
            case Order::Name:
                std::stable_sort(m_rows.begin(), m_rows.end(),
                                 [](const Row& a, const Row& b) { return a.name < b.name; });
                break;
            case Order::Idle:
                // Anchored first, then under way, then on a trade route.
                std::stable_sort(m_rows.begin(), m_rows.end(), [](const Row& a, const Row& b) {
                    constexpr std::array<std::int32_t, 3> kRank = {2, 0, 1};
                    return kRank[static_cast<std::size_t>(std::clamp(a.icon, 0, 2))] <
                           kRank[static_cast<std::size_t>(std::clamp(b.icon, 0, 2))];
                });
                break;
            case Order::Damaged:
                std::stable_sort(m_rows.begin(), m_rows.end(),
                                 [](const Row& a, const Row& b) { return a.health < b.health; });
                break;
        }
    }

    ui::Panel m_panel;
    bool m_ships;
    std::int32_t m_base;
    std::int32_t m_arms = 548;
    std::int32_t m_icons = 622;
    std::size_t m_first = 0;
    std::vector<Row> m_rows;
    Order m_order = Order::Table;
    bool m_stale = true;               ///< gather the rows at the next refresh
    std::uint64_t m_collected_ms = 0;  ///< game time of the last gathering
    std::optional<Request> m_request;
};

}  // namespace

std::unique_ptr<SelectionPanel> make_list_panel(assets::GadgetLayout layout, bool ships) {
    return std::make_unique<ListPanel>(std::move(layout), ships);
}

}  // namespace o1602::app
