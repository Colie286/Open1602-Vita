// SPDX-License-Identifier: GPL-3.0-or-later
#include "selection_panels.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "panel_common.hpp"
#include "production_utilisation.hpp"

#include "o1602/assets/resident_classes.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/localization.hpp"
#include "o1602/ui/package_icons.hpp"
#include "o1602/ui/text_format.hpp"

namespace o1602::app {

std::string PanelContext::text(std::string_view section, std::size_t line) const {
    if (section == "WARE" || section == "WAREM") {
        // The goods of packages have their names with them (docs/modding/goods.md).
        if (const assets::OwnGood* g = line <= 255 ? world().goods().own(static_cast<std::uint32_t>(line)) : nullptr) {
            return g->name(ui::current_language());
        }
    }
    if (texts == nullptr) {
        return {};
    }
    const auto& lines = texts->section(section);
    return line < lines.size() ? lines[line] : std::string();
}

std::string PanelContext::class_name(std::size_t resident_class) const {
    const auto& classes = buildings.resident_classes();
    if (resident_class < classes.size()) {
        if (std::string name = assets::class_name(classes[resident_class], ui::current_language()); !name.empty()) {
            return name;
        }
    }
    return resident_class < assets::kResidentClasses ? text("GAME", 3 + resident_class)
                                                     : std::to_string(resident_class + 1);
}

namespace {

using namespace panel_detail;

// LEISTE lines.
constexpr std::size_t kHelpCityName = 11;
constexpr std::size_t kHelpProduct = 146;
constexpr std::size_t kHelpStop = 147;    // + 1: switch on again
constexpr std::size_t kHelpPickup = 149;  // + 1: allow again
// [GAME] lines.
constexpr std::size_t kGameUtilisation = 11;
constexpr std::size_t kGameRunningCost = 18;
constexpr std::size_t kGameStock = 21;
constexpr std::size_t kGamePercent = 48;
constexpr std::size_t kGameTons = 49;

/// The building at a selection, nothing if it is gone.
const assets::BuildingType* building_at(const Selection& s, const PanelContext& c) {
    if (s.kind != Selection::Kind::Building || s.island >= c.world().islands().size()) {
        return nullptr;
    }
    const sim::IslandMap& map = c.world().islands()[s.island].map;
    if (!map.contains(s.x, s.y)) {
        return nullptr;
    }
    const sim::MapCell& top = map.top(s.x, s.y);
    return top.empty() ? nullptr : c.buildings.by_sprite(top.sprite);
}

/// The name of a building as the build menu calls it (Haus_GetMenuName 0x0042D230).
std::string building_name(const assets::BuildingType& b, const PanelContext& c) {
    if (const ui::BuildCategory* cat = ui::build_menu().category_of(b.id)) {
        return c.texts != nullptr ? cat->name(cat->index_of(b.id).value_or(0), c.texts->section(cat->names))
                                  : std::string();
    }
    return {};
}

std::int64_t utilisation(const sim::Production& p, const assets::BuildingType& b, const PanelContext& c) {
    return production_utilisation(p, b, c.figures);
}

/// What remains of a raw material beside the share the running cycle already took (0x0047D540).
std::int64_t free_stock(std::uint16_t stock, std::uint32_t per_cycle, std::uint8_t rate) {
    const std::int64_t used = (std::int64_t{per_cycle} * rate) >> 7;
    return std::max<std::int64_t>(0, std::int64_t{stock} - used);
}

// ---- Production buildings ------------------------------------------------------------------------

/// Gadget Ids of the three production windows (PROD, PLANTAGE, BERGWERK); 0 = not in this one.
struct ProductionIds {
    std::int32_t title = 0;
    std::int32_t name = 0;
    std::int32_t util_label = 0;
    std::int32_t util_value = 0;
    std::int32_t cost_label = 0;
    std::int32_t cost_value = 0;
    std::int32_t stop = 0;
    std::int32_t pickup = 0;
    // One raw material: icon, amount, frame, arrow; the product: icon, amount, frame.
    std::array<std::int32_t, 4> raw{};
    std::array<std::int32_t, 3> product{};
    // Two raw materials (PROD only): second icon, amount, frame, "+"; first icon, amount, frame,
    // arrow; product icon, amount, frame.
    std::array<std::int32_t, 4> raw2_b{};
    std::array<std::int32_t, 4> raw_b{};
    std::array<std::int32_t, 3> product_b{};
};

constexpr ProductionIds kProd{37002,
                              37008,
                              37003,
                              37004,
                              37005,
                              37006,
                              37009,
                              37010,
                              {37021, 37022, 37023, 37024},
                              {37025, 37026, 37027},
                              {37031, 37032, 37033, 37034},
                              {37035, 37036, 37037, 37038},
                              {37039, 37040, 37041}};
constexpr ProductionIds kPlantage{41002, 41008, 41003, 41004, 41005, 41006, 41009, 41010, {}, {41021, 41022, 41023}};
constexpr ProductionIds kMine{46002, 46006, 0, 0, 46003, 46004, 46007, 46008, {}, {46021, 46022, 46023}};

class ProductionPanel final : public SimplePanel {
public:
    ProductionPanel(assets::GadgetLayout layout, const ProductionIds& ids, std::int32_t gfx_ware)
        : SimplePanel(std::move(layout)), m_ids(ids), m_gfx_ware(gfx_ware) {}

    void refresh(const Selection& s, const PanelContext& c) override {
        const assets::BuildingType* b = building_at(s, c);
        const sim::Production* p = c.world().production(s.island, s.x, s.y);
        if (b == nullptr || p == nullptr) {
            return;
        }
        const sim::City* city = city_at(s, c);
        m_panel.set_text(m_ids.title, city != nullptr ? city_name(*city) : std::string());
        m_panel.set_text(m_ids.name, building_name(*b, c));
        m_panel.set_text(m_ids.util_label, c.text("GAME", kGameUtilisation));
        m_panel.set_text(m_ids.util_value, ui::format_text(c.text("GAME", kGamePercent), {utilisation(*p, *b, c)}));
        m_panel.set_text(m_ids.cost_label, c.text("GAME", kGameRunningCost));
        m_panel.set_text(m_ids.cost_value, std::to_string(p->enabled ? b->running_cost : b->idle_cost));
        m_panel.set_selected(m_ids.stop, !p->enabled);
        m_panel.set_selected(m_ids.pickup, p->no_pickup());

        const std::string tons = c.text("GAME", kGameTons);
        const auto show = [&](const auto& group, bool on) {
            for (const std::int32_t id : group) {
                m_panel.set_visible(id, on);
            }
        };
        const auto ware = [&](std::int32_t icon, std::int32_t amount, std::uint8_t w, std::int64_t stock) {
            m_panel.set_graphic(icon, ui::ware_sprite(m_gfx_ware, c.world().goods(), w), 0);
            m_panel.set_text(amount, ui::format_text(tons, {stock >> 5}));
        };
        const bool two = b->raw2_ware != 0 && b->raw2_amount != 0 && m_ids.raw2_b[0] != 0;
        const bool raw = b->raw_ware != 0 && m_ids.raw[0] != 0;
        show(m_ids.raw, raw && !two);
        show(m_ids.product, !two);
        show(m_ids.raw2_b, two);
        show(m_ids.raw_b, two);
        show(m_ids.product_b, two);
        const std::int64_t raw_free = free_stock(p->raw, b->raw_amount, p->rate);
        const std::int64_t raw2_free = free_stock(p->raw2, b->raw2_amount, p->rate);
        if (two) {
            ware(m_ids.raw2_b[0], m_ids.raw2_b[1], b->raw2_ware, raw2_free);
            ware(m_ids.raw_b[0], m_ids.raw_b[1], b->raw_ware, raw_free);
            ware(m_ids.product_b[0], m_ids.product_b[1], b->ware, p->stock);
        } else {
            if (raw) {
                ware(m_ids.raw[0], m_ids.raw[1], b->raw_ware, raw_free);
            }
            ware(m_ids.product[0], m_ids.product[1], b->ware, p->stock);
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& s, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const std::int32_t id = e.id;
        const sim::Production* p = c.world().production(s.island, s.x, s.y);
        if (p == nullptr) {
            return;
        }
        sim::ProductionSwitch sw;
        sw.player = c.local;
        sw.island = s.island;
        sw.x = s.x;
        sw.y = s.y;
        if (id == m_ids.stop) {
            sw.op = sim::ProductionSwitch::Op::Enable;
            sw.on = !p->enabled;
            out.emplace_back(sw);
        } else if (id == m_ids.pickup) {
            sw.op = sim::ProductionSwitch::Op::Pickup;
            sw.on = p->no_pickup();
            out.emplace_back(sw);
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& s,
                                                  const PanelContext& c) const override {
        const sim::Production* p = c.world().production(s.island, s.x, s.y);
        const assets::BuildingType* b = building_at(s, c);
        if (p == nullptr || b == nullptr) {
            return std::nullopt;
        }
        if (id == m_ids.title) {
            return help_line(c, kHelpCityName);
        }
        if (id == m_ids.stop) {
            return help_line(c, kHelpStop + (p->enabled ? 0 : 1));
        }
        if (id == m_ids.pickup) {
            return help_line(c, kHelpPickup + (p->no_pickup() ? 1 : 0));
        }
        if (id == m_ids.product[2] || id == m_ids.product_b[2]) {
            return help_line(c, kHelpProduct);
        }
        // An input frame: "Wollevorrat (reicht für weitere 1.5t Stoffe)" - whole tons in the
        // building against the input of one ton of product.
        const auto stock_text = [&](std::uint8_t in, std::uint16_t stock, std::uint32_t per_cycle) {
            const std::int64_t free = free_stock(stock, per_cycle, p->rate);
            const double enough =
                per_cycle != 0 ? static_cast<double>(free - free % 32) / static_cast<double>(per_cycle) : 0.0;
            return ui::format_text(c.text("GAME", kGameStock), {std::string_view(c.text("WARE", in)), enough,
                                                                std::string_view(c.text("WARE", b->ware))});
        };
        if (id == m_ids.raw[2] || id == m_ids.raw_b[2]) {
            return stock_text(b->raw_ware, p->raw, b->raw_amount);
        }
        if (id == m_ids.raw2_b[2]) {
            return stock_text(b->raw2_ware, p->raw2, b->raw2_amount);
        }
        return std::nullopt;
    }

private:
    ProductionIds m_ids;
    std::int32_t m_gfx_ware;
};

// ---- Public buildings ----------------------------------------------------------------------------

class InfraPanel final : public SimplePanel {
public:
    InfraPanel(assets::GadgetLayout layout, std::int32_t gfx_bauhaus)
        : SimplePanel(std::move(layout)), m_gfx_bauhaus(gfx_bauhaus) {}

    void refresh(const Selection& s, const PanelContext& c) override {
        const assets::BuildingType* b = building_at(s, c);
        if (b == nullptr) {
            return;
        }
        const sim::City* city = city_at(s, c);
        m_panel.set_text(62002, city != nullptr ? city_name(*city) : std::string());
        m_panel.set_text(62006, building_name(*b, c));
        m_panel.set_text(62003, c.text("GAME", kGameRunningCost));
        m_panel.set_text(62004, std::to_string(b->running_cost));
        // The building's icon of the build menu (Haus_GetMenuIcon 0x0042D3A0).
        const ui::BuildCategory* cat = ui::build_menu().category_of(b->id);
        if (const ui::BuildButton* button =
                cat != nullptr ? cat->button_at(cat->index_of(b->id).value_or(0)) : nullptr) {
            m_panel.set_graphic(62021, button->sprite(m_gfx_bauhaus), 0);
            m_panel.set_visible(62021, true);
        } else {
            m_panel.set_visible(62021, false);
        }
    }
    void clicked(const ui::PanelEvent&, const Selection&, const PanelContext&, std::vector<sim::Command>&) override {}
    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection&,
                                                  const PanelContext& c) const override {
        return id == 62002 ? std::optional<std::string>(help_line(c, kHelpCityName)) : std::nullopt;
    }

private:
    std::int32_t m_gfx_bauhaus;
};

// ---- Own Kontor or market: city, trade lists, stock -----------------------------------------------

namespace city_ids {
// STADT.GAD
constexpr std::int32_t kTitle = 43002;
constexpr std::int32_t kSell = 43003;
constexpr std::int32_t kBuy = 43004;
constexpr std::int32_t kKontorPicture = 43005;
constexpr std::int32_t kStock = 43006;
constexpr std::int32_t kMaterialLock = 43025;
constexpr std::int32_t kTrader = 43026;
constexpr std::int32_t kBalanceIcon = 43024;
// KONTOR.GAD
constexpr std::int32_t kKTitle = 34002;
constexpr std::int32_t kKMode = 34003;
constexpr std::int32_t kKLimit = 34005;
constexpr std::int32_t kKStockBar = 34007;
constexpr std::int32_t kKLimitMarker = 34008;
constexpr std::int32_t kKName = 34012;
constexpr std::int32_t kKPrice = 34013;
constexpr std::int32_t kKStockLabel = 34014;
constexpr std::int32_t kKStockValue = 34015;
constexpr std::int32_t kKStop = 34016;
[[maybe_unused]] constexpr std::int32_t kKPriceTrack = 34021;
constexpr std::int32_t kKPriceDown = 34022;
constexpr std::int32_t kKPriceUp = 34023;
constexpr std::int32_t kKPriceSlider = 34024;
constexpr std::int32_t kKIcon = 34031;   // + slot, 11
constexpr std::int32_t kKBar = 34051;    // + slot
constexpr std::int32_t kKFrame = 34071;  // + slot
constexpr std::int32_t kKSlots = 11;
}  // namespace city_ids

constexpr std::int32_t kPriceSteps = 1024;

/// STADT (the city), KONTOR (the buy or sell list, with the LAGER layout as the ware picker) and
/// LAGER (the stock) (CityPanel_Refresh 0x00434640, KontorPanel_Refresh 0x00434B40,
/// StockPanel_Refresh 0x00434810).
class CityPanel final : public SelectionPanel {
public:
    CityPanel(assets::GadgetLayout stadt, assets::GadgetLayout kontor, assets::GadgetLayout lager)
        : m_stadt(std::move(stadt), 1.0), m_kontor(std::move(kontor), 1.0), m_lager(std::move(lager)) {
        const assets::GadgetLayout& k = m_kontor.layout();
        m_gfx_ware = k.constant("GFXWARE").value_or(192);
        m_gfx_empty = k.constant("GFXWARELEER").value_or(188) + kEmptyHold;
        if (const assets::Gadget* g = m_stadt.layout().find(city_ids::kBalanceIcon)) {
            m_balance_icon = g->gfx;
        }
        if (const assets::Gadget* g = m_stadt.layout().find(city_ids::kTrader)) {
            m_cart_icon = g->gfx;
        }
        // No handler in the original: the Kontor picture.
        m_stadt.set_disabled(city_ids::kKontorPicture, true);
    }

    void set_scale(double scale) override {
        m_stadt.set_scale(scale);
        m_kontor.set_scale(scale);
        m_lager.panel().set_scale(scale);
    }
    ui::Panel& panel() override {
        switch (m_page) {
            case Page::Kontor:
                return m_picker ? m_lager.panel() : m_kontor;
            case Page::Stock:
                return m_lager.panel();
            default:
                return m_stadt;
        }
    }

    void refresh(const Selection& s, const PanelContext& c) override {
        // A new selection starts with the city, also the same Kontor selected anew after it was
        // deselected; the same Kontor or market place clicked again shows the stock.
        switch (m_pages.update(s)) {
            case SelectionPages::Change::Opened:
                m_page = Page::City;
                m_picker = false;
                m_slot = 0;
                break;
            case SelectionPages::Change::Again:
                m_page = Page::Stock;
                m_picker = false;
                m_slot = 0;
                break;
            case SelectionPages::Change::None:
                break;
        }
        const sim::City* city = city_at(s, c);
        if (city == nullptr) {
            return;
        }
        switch (m_page) {
            case Page::City:
                refresh_city(*city, c);
                break;
            case Page::Kontor:
                if (m_picker) {
                    m_lager.refresh(*city, c.world().goods(), true);
                } else {
                    refresh_kontor(*city, c);
                }
                break;
            case Page::Stock:
                m_lager.refresh(*city, c.world().goods(), false, c.goods_trend);
                break;
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& s, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        const sim::City* city = city_at(s, c);
        if (city == nullptr) {
            return;
        }
        if (m_page == Page::City) {
            if (e.kind != ui::PanelEvent::Kind::Clicked) {
                return;
            }
            if (e.id == city_ids::kTitle) {
                c.start_rename(NameTarget::of(*city), out);  // 0x0043CE00 → 0x00409910(2, …)
            } else if (e.id == city_ids::kSell || e.id == city_ids::kBuy) {
                m_page = Page::Kontor;
                m_sell = e.id == city_ids::kSell;
                m_picker = false;
                m_slot = 0;
            } else if (e.id == city_ids::kStock) {
                m_page = Page::Stock;
            } else if (e.id == city_ids::kTrader) {
                // 0x0043CE00: with a cart on the island the view jumps to it and selects it, else
                // the next click on the map sends one out from the Kontor (0x004894E0 with 4).
                if (const sim::Ship* cart = sim::trade_cart_of(c.world(), c.local, city->island)) {
                    m_request = Request{Request::Kind::SelectShip, cart->id};
                } else {
                    m_cart_pick = true;
                }
            } else if (e.id == city_ids::kMaterialLock) {
                sim::CityOrder o;
                o.player = c.local;
                o.island = city->island;
                o.city = city->index;
                o.op = sim::CityOrder::Op::MaterialLock;
                o.on = !city->material_lock;
                out.emplace_back(o);
            }
            return;
        }
        if (m_page == Page::Kontor && m_picker) {
            // The ware picker: a ware goes into the list (and out of the other one), the empty
            // frame closes the picker.
            if (e.kind != ui::PanelEvent::Kind::Clicked) {
                return;
            }
            if (m_lager.turn_page(e.id)) {
                return;
            }
            const std::optional<std::uint8_t> picked = m_lager.ware_of(e.id);
            if (picked && *picked != 0) {
                out.emplace_back(setting(*city, c, *picked, mode()));
                // The new ware's place in the list, which follows the order of the goods.
                std::int32_t before = 0;
                for (const std::uint8_t v : c.world().goods().trade_wares()) {
                    if (v == *picked) {
                        break;
                    }
                    before += listed(*city, v) ? 1 : 0;
                }
                m_slot = before;
            }
            if (picked) {
                m_picker = false;
            }
            return;
        }
        if (m_page == Page::Stock) {
            // Open1602's own: a good of the stock opens its page in the window "Warenbilanz".
            if (e.kind == ui::PanelEvent::Kind::Clicked && m_lager.turn_page(e.id)) {
                return;
            }
            const std::optional<std::uint8_t> w = m_lager.ware_of(e.id);
            if (e.kind == ui::PanelEvent::Kind::Clicked && w && *w != 0 && city->owner == c.local) {
                Request r;
                r.kind = Request::Kind::Goods;
                r.ware = *w;
                r.island = city->island;
                m_request = r;
            }
            return;
        }
        if (m_page != Page::Kontor) {
            return;
        }
        if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == city_ids::kKTitle) {
            c.start_rename(NameTarget::of(*city), out);  // 0x0043CE00, gadget 34002
            return;
        }
        const std::vector<std::uint8_t> list = wares(*city, c.world().goods());
        const std::int32_t slots = slot_count(*city, c);
        if (e.kind == ui::PanelEvent::Kind::Clicked && e.id >= city_ids::kKFrame &&
            e.id < city_ids::kKFrame + city_ids::kKSlots) {
            m_slot = e.id - city_ids::kKFrame;
            if (m_slot >= static_cast<std::int32_t>(list.size()) && m_slot < slots) {
                m_picker = true;  // an empty slot: choose a ware
            }
            return;
        }
        if (m_slot >= static_cast<std::int32_t>(list.size())) {
            return;
        }
        const std::uint8_t w = list[static_cast<std::size_t>(m_slot)];
        const sim::City::Trade* t = city->trade_of(w);
        if (t == nullptr) {
            return;
        }
        const std::uint16_t base = c.world().goods().price(w);
        const std::uint16_t min = static_cast<std::uint16_t>(base * 300U >> 10U);
        const std::uint16_t price = m_sell ? t->sell_price : t->buy_price;
        sim::TradeSetting o = setting(*city, c, w, mode());
        if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == city_ids::kKStop) {
            o.mode = sim::TradeSetting::Mode::None;
            out.emplace_back(o);
        } else if (e.kind == ui::PanelEvent::Kind::Clicked &&
                   (e.id == city_ids::kKPriceDown || e.id == city_ids::kKPriceUp)) {
            const int step = e.id == city_ids::kKPriceUp ? 1 : -1;
            o.price = static_cast<std::uint16_t>(std::clamp<int>(price + step, min, base));
            out.emplace_back(o);
        } else if (e.kind == ui::PanelEvent::Kind::Slid && e.id == city_ids::kKPriceSlider) {
            // ((v + 300) * base) >> 10 over v = 0..724 (0x0043D9xx); sent only when it changes.
            const std::int32_t v = m_kontor.slider(e.id, 0, 724);
            const auto p = static_cast<std::uint16_t>(((v + 300) * base) >> 10);
            if (p != price) {
                o.price = p;
                out.emplace_back(o);
            }
        } else if (e.kind == ui::PanelEvent::Kind::Slid && e.id == city_ids::kKLimit) {
            const auto cap = static_cast<std::int32_t>(city->capacity);
            const auto v = static_cast<std::uint32_t>(m_kontor.slider(e.id, 0, std::max(cap, 1)));
            if (v != (m_sell ? t->keep : t->limit)) {
                o.amount = v;
                out.emplace_back(o);
            }
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& s,
                                                  const PanelContext& c) const override {
        const sim::City* city = city_at(s, c);
        if (city == nullptr) {
            return std::nullopt;
        }
        const auto line = [&](std::size_t n) { return std::optional<std::string>(help_line(c, n)); };
        const auto stock_text = [&](std::uint8_t w) {
            const std::uint32_t t = city->stock(w) >> 5U;
            return t >= 1 ? ui::format_text(c.text("GAME", 50), {std::int64_t{t}, std::string_view(c.text("WARE", w))})
                          : c.text("WARE", w);
        };
        const std::size_t m = m_sell ? 1 : 0;
        if (m_page == Page::City) {
            switch (id) {
                case city_ids::kTitle:
                    return line(kHelpCityName);
                case city_ids::kSell:
                    return line(151);
                case city_ids::kBuy:
                    return line(152);
                case city_ids::kStock:
                    return line(153);
                case city_ids::kMaterialLock:
                    return line(154 + (city->material_lock ? 1 : 0));
                case city_ids::kTrader:
                    // "Fahrenden Händler anfordern" / "Zum fahrenden Händler springen".
                    return line(sim::trade_cart_of(c.world(), c.local, city->island) != nullptr ? 157 : 156);
                default:
                    return std::nullopt;
            }
        }
        if (m_page == Page::Stock || m_picker) {
            const std::optional<std::uint8_t> w = m_lager.ware_of(id);
            if (w && *w != 0 && m_page == Page::Stock) {
                const std::string trend = StockList::trend_text(*city, *w);
                return stock_text(*w) + " - " +
                       ui::tr("stats.kontor_help",
                              {trend.empty() ? std::string("0") : trend, std::to_string(StockList::kTrendMinutes)});
            }
            if (w && *w != 0) {
                return stock_text(*w);
            }
            return std::nullopt;
        }
        const std::vector<std::uint8_t> list = wares(*city, c.world().goods());
        if (id >= city_ids::kKFrame && id < city_ids::kKFrame + city_ids::kKSlots) {
            const auto k = static_cast<std::size_t>(id - city_ids::kKFrame);
            return k < list.size() ? std::optional<std::string>(stock_text(list[k])) : line(41 + m);
        }
        switch (id) {
            case city_ids::kKTitle:
                return line(kHelpCityName);
            case city_ids::kKStop:
                return line(35 + m);
            case city_ids::kKPriceDown:
                return line(37 + m);
            case city_ids::kKPriceUp:
                return line(39 + m);
            case city_ids::kKPriceSlider:
                return line(43 + m);
            case city_ids::kKLimit:
                if (m_slot < static_cast<std::int32_t>(list.size())) {
                    const std::uint8_t w = list[static_cast<std::size_t>(m_slot)];
                    const sim::City::Trade* t = city->trade_of(w);
                    const std::uint32_t limit = t == nullptr ? 0 : (m_sell ? t->keep : t->limit);
                    return ui::format_text(c.text("GAME", 59 + m),
                                           {std::string_view(c.text("WARE", w)), std::int64_t{limit >> 5U}});
                }
                return std::nullopt;
            default:
                return std::nullopt;
        }
    }

    std::optional<Request> take_request() override { return std::exchange(m_request, std::nullopt); }

    /// After "Fahrenden Händler anfordern": where the new trade cart drives.
    [[nodiscard]] bool picking() const override { return m_cart_pick; }
    void picked(std::uint32_t x, std::uint32_t y, const Selection& s, const PanelContext& c,
                std::vector<sim::Command>& out) override {
        m_cart_pick = false;
        if (const sim::City* city = city_at(s, c)) {
            out.emplace_back(sim::CartRequest{c.local, city->island, city->index, static_cast<std::uint16_t>(x),
                                              static_cast<std::uint16_t>(y)});
        }
    }
    bool back() override { return std::exchange(m_cart_pick, false); }

private:
    enum class Page : std::uint8_t { City, Kontor, Stock };

    [[nodiscard]] sim::TradeSetting::Mode mode() const noexcept {
        return m_sell ? sim::TradeSetting::Mode::Sell : sim::TradeSetting::Mode::Buy;
    }
    [[nodiscard]] bool listed(const sim::City& city, std::uint8_t w) const {
        const sim::City::Trade* t = city.trade_of(w);
        return t != nullptr && (m_sell ? t->sell : t->buy);
    }
    /// The wares of the list, in the order of the goods (the original's by ware number).
    [[nodiscard]] std::vector<std::uint8_t> wares(const sim::City& city, const assets::Goods& goods) const {
        std::vector<std::uint8_t> list;
        for (const std::uint8_t w : goods.trade_wares()) {
            if (listed(city, w)) {
                list.push_back(w);
            }
        }
        return list;
    }
    /// Places of the list: the Kontor's Maxware (sell) or its second value (buy).
    [[nodiscard]] std::int32_t slot_count(const sim::City& city, const PanelContext& c) const {
        const assets::BuildingType* k = sim::kontor_of(c.world(), c.buildings, city);
        const std::int32_t n = k == nullptr ? 0 : (m_sell ? k->max_wares : k->max_wares2);
        return std::clamp(n, 0, city_ids::kKSlots);
    }
    static sim::TradeSetting setting(const sim::City& city, const PanelContext& c, std::uint8_t w,
                                     sim::TradeSetting::Mode m) {
        sim::TradeSetting o;
        o.player = c.local;
        o.island = city.island;
        o.city = city.index;
        o.ware = w;
        o.mode = m;
        return o;
    }

    void refresh_city(const sim::City& city, const PanelContext& c) {
        // Only the buildings of the city's island (FB-4).
        const std::int64_t people = sim::city_einwohner(city, sim::city_workers(c.world(), c.buildings, city));
        const std::int64_t tax = sim::tax_income(city, c.buildings);
        const std::int64_t cost = sim::running_cost(c.world(), c.buildings, city);
        const std::int64_t sales = city.sales / 480;
        const std::int64_t purchases = city.purchases / 480;
        const std::int64_t balance = tax - cost + sales - purchases;
        // The name: a click on it types a new one, also on the Kontor page.
        m_stadt.set_text(city_ids::kTitle, c.shown_name(NameTarget::of(city), city_name(city)));
        m_stadt.set_no_select(city_ids::kTitle, !can_rename(c.world(), c.local, NameTarget::of(city)));
        const std::array<std::pair<std::size_t, std::int64_t>, 6> rows{
            {{15, people}, {19, tax}, {18, cost}, {64, sales}, {65, purchases}, {80, balance}}};
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto label = static_cast<std::int32_t>(43007 + 3 * i);
            m_stadt.set_text(label, c.text("GAME", rows[i].first));
            m_stadt.set_text(label + 1, std::to_string(rows[i].second));
        }
        m_stadt.set_graphic(city_ids::kBalanceIcon, m_balance_icon + (balance < 0 ? 1 : 0), 0);
        m_stadt.set_selected(city_ids::kMaterialLock, city.material_lock);
        // The trade cart: requested or jumped to (two pictures, 0x00448B70 finds the cart).
        const bool cart = sim::trade_cart_of(c.world(), c.local, city.island) != nullptr;
        m_stadt.set_graphic(city_ids::kTrader, m_cart_icon + (cart ? kCartIconJump : 0), 0);
        m_stadt.set_selected(city_ids::kTrader, m_cart_pick);
    }

    void refresh_kontor(const sim::City& city, const PanelContext& c) {
        m_kontor.set_text(city_ids::kKTitle, c.shown_name(NameTarget::of(city), city_name(city)));
        m_kontor.set_no_select(city_ids::kKTitle, !can_rename(c.world(), c.local, NameTarget::of(city)));
        m_kontor.set_selected(city_ids::kKMode, m_sell);
        const std::vector<std::uint8_t> list = wares(city, c.world().goods());
        const std::int32_t slots = slot_count(city, c);
        const auto cap = static_cast<std::int32_t>(std::max<std::uint32_t>(city.capacity, 1));
        if (m_slot >= std::max<std::int32_t>(slots, static_cast<std::int32_t>(list.size()))) {
            m_slot = 0;  // the list shrank
        }
        for (std::int32_t k = 0; k < city_ids::kKSlots; ++k) {
            const bool ware = k < static_cast<std::int32_t>(list.size());
            const bool slot = ware || k < slots;
            m_kontor.set_visible(city_ids::kKIcon + k, slot);
            m_kontor.set_visible(city_ids::kKFrame + k, slot);
            m_kontor.set_visible(city_ids::kKBar + k, ware);
            m_kontor.set_selected(city_ids::kKFrame + k, k == m_slot);
            if (ware) {
                const std::uint8_t w = list[static_cast<std::size_t>(k)];
                m_kontor.set_graphic(city_ids::kKIcon + k, ui::ware_sprite(m_gfx_ware, c.world().goods(), w), 0);
                m_kontor.set_slider(city_ids::kKBar + k, 1, static_cast<std::int32_t>(city.stock(w)), cap);
            } else if (slot) {
                m_kontor.set_graphic(city_ids::kKIcon + k, m_gfx_empty, 0);
            }
        }
        const bool chosen = m_slot < static_cast<std::int32_t>(list.size());
        for (const std::int32_t id : {city_ids::kKName, city_ids::kKPrice}) {
            m_kontor.set_visible(id, chosen);
        }
        // The original fades all of 0x84D5 .. 0x84E8 while no ware is chosen (0x004073C0).
        for (std::int32_t id = city_ids::kKLimit; id <= city_ids::kKPriceSlider; ++id) {
            m_kontor.set_faded(id, !chosen);
        }
        m_kontor.set_text(city_ids::kKStockLabel, c.text("GAME", 0));
        if (!chosen) {
            m_kontor.set_slider(city_ids::kKPriceSlider, 0, kPriceSteps / 2, kPriceSteps);
            m_kontor.set_slider(city_ids::kKLimit, 0, 0, cap);
            m_kontor.set_slider(city_ids::kKLimitMarker, 0, 0, cap);
            m_kontor.set_slider(city_ids::kKStockBar, 0, 0, cap);
            m_kontor.set_text(city_ids::kKStockValue, ui::format_text(c.text("GAME", 49), {std::int64_t{0}}));
            return;
        }
        const std::uint8_t w = list[static_cast<std::size_t>(m_slot)];
        const sim::City::Trade* t = city.trade_of(w);
        const std::uint16_t base = c.world().goods().price(w);
        const auto min = static_cast<std::int32_t>(base * 300U >> 10U);
        const std::int32_t price = t == nullptr ? 0 : (m_sell ? t->sell_price : t->buy_price);
        const std::uint32_t limit = t == nullptr ? 0 : (m_sell ? t->keep : t->limit);
        m_kontor.set_text(city_ids::kKName, c.text("WARE", w));
        m_kontor.set_text(city_ids::kKPrice, std::to_string(price));
        const std::int32_t span = std::max(1, base - min);
        // A slider the mouse holds shows the mouse, not the game (its command comes a tick later).
        if (m_kontor.held() != city_ids::kKPriceSlider) {
            m_kontor.set_slider(city_ids::kKPriceSlider, 0,
                                std::clamp((price - min) * kPriceSteps / span, 0, kPriceSteps), kPriceSteps);
        }
        if (m_kontor.held() != city_ids::kKLimit) {
            m_kontor.set_slider(city_ids::kKLimit, 0, static_cast<std::int32_t>(limit), cap);
        }
        m_kontor.set_slider(city_ids::kKLimitMarker, 0, static_cast<std::int32_t>(limit), cap);
        m_kontor.set_slider(city_ids::kKStockBar, 0, static_cast<std::int32_t>(city.stock(w)), cap);
        m_kontor.set_text(city_ids::kKStockValue,
                          ui::format_text(c.text("GAME", 49), {std::int64_t{city.stock(w) >> 5U}}));
    }

    ui::Panel m_stadt;
    ui::Panel m_kontor;
    StockList m_lager;
    std::optional<Request> m_request;
    std::int32_t m_gfx_ware = 192;
    std::int32_t m_gfx_empty = 192;
    std::int32_t m_balance_icon = 0;
    std::int32_t m_cart_icon = 0;
    /// The picture of the trade cart button while the city has one, after "anfordern"'s.
    static constexpr std::int32_t kCartIconJump = 2;
    bool m_cart_pick = false;
    Page m_page = Page::City;
    bool m_sell = true;
    bool m_picker = false;
    std::int32_t m_slot = 0;
    SelectionPages m_pages;
};

// ---- Houses --------------------------------------------------------------------------------------

/// SIEDLER: the class of a house, its city's supply and the public buildings it lacks
/// (HousePanel_Refresh 0x00438A30).
class SettlerPanel final : public SimplePanel {
public:
    explicit SettlerPanel(assets::GadgetLayout layout) : SimplePanel(std::move(layout)) {
        const assets::GadgetLayout& l = m_panel.layout();
        m_gfx_ware = l.constant("GFXWARE").value_or(192);
        if (const assets::Gadget* g = l.find(kFace)) {
            m_face = g->gfx;
        }
        for (std::size_t k = 0; k < kServices; ++k) {
            if (const assets::Gadget* g = l.find(kService + static_cast<std::int32_t>(k))) {
                m_service_gfx[k] = g->gfx;
                m_service_x[k] = g->pos[0];
                m_service_w[k] = g->size[0];
            }
        }
    }

    void refresh(const Selection& s, const PanelContext& c) override {
        const assets::BuildingType* b = building_at(s, c);
        const sim::House* house = c.world().house(s.island, s.x, s.y);
        const sim::City* city = city_at(s, c);
        const auto& classes = c.buildings.resident_classes();
        if (b == nullptr || house == nullptr || city == nullptr || house->resident_class >= classes.size()) {
            return;
        }
        const std::uint8_t cls = house->resident_class;
        const assets::ResidentClass& rc = classes[cls];
        const int state = growth_state(*city, *house);
        m_panel.set_text(kTitle, city_name(*city));
        m_panel.set_graphic(kFace, m_face + 4 * rc.look + (1 - state), 0);
        m_panel.set_text(kClassName, c.class_name(cls));
        m_panel.set_text(kClassCount, std::to_string(cls < city->inhabitants.size() ? city->inhabitants[cls] : 0));
        const std::uint8_t tax = tax_of(*city, b->resident_class);
        m_panel.set_text(kTaxLabel, ui::format_text(c.text("GAME", 2), {std::int64_t{tax / 4}}));
        m_panel.set_text(kTaxIncome, std::to_string(class_tax(*city, rc, cls, tax)));
        if (m_panel.held() != kTaxSlider) {
            m_panel.set_slider(kTaxSlider, 0, tax - sim::CityOrder::kMinTax, 128);
        }

        // Goods: those of the class that are short, then (while the house grows) what the next
        // class will ask for.
        const auto ratio = [&](std::uint8_t w) -> std::int32_t {
            return w < city->supply.size() ? city->supply[w].ratio : 128;
        };
        const auto wants = [&](const assets::ResidentClass& r, std::uint8_t w) {
            return std::any_of(r.needs.begin(), r.needs.end(),
                               [&](const assets::ResidentClass::Need& n) { return n.ware == w && n.amount != 0; });
        };
        // The goods any class asks for besides food, by ware number: for the original's classes
        // tobacco goods to jewellery, as the original's panel goes through them.
        std::vector<std::uint8_t> consumer;
        for (const assets::ResidentClass& r : classes) {
            for (const assets::ResidentClass::Need& n : r.needs) {
                consumer.push_back(n.ware);
            }
        }
        std::sort(consumer.begin(), consumer.end());
        consumer.erase(std::unique(consumer.begin(), consumer.end()), consumer.end());
        m_demand.clear();
        for (const std::uint8_t w : consumer) {
            if (wants(rc, w) && ratio(w) < 128) {
                m_demand.push_back({w, ratio(w)});
            }
        }
        const sim::Player* owner = c.world().player(city->owner);
        if (cls + 1U < classes.size() && state > 0) {
            for (const std::uint8_t w : consumer) {
                if (!wants(classes[cls + 1U], w) || wants(rc, w) || !ware_unlocked(w, owner)) {
                    continue;
                }
                if (ratio(w) < 128) {
                    m_demand.push_back({w, ratio(w)});
                } else if (w < city->supply.size() && city->supply[w].demand == 0 && city->stock(w) < 32) {
                    m_demand.push_back({w, 0});
                }
            }
        }
        for (std::size_t k = 0; k < kDemandSlots; ++k) {
            const auto id = static_cast<std::int32_t>(k);
            const bool on = k < m_demand.size();
            m_panel.set_visible(kDemandIcon + id, on);
            m_panel.set_visible(kDemandBar + id, on);
            m_panel.set_visible(kDemandFrame + id, on);
            if (on) {
                m_panel.set_graphic(kDemandIcon + id, ui::ware_sprite(m_gfx_ware, c.world().goods(), m_demand[k].first),
                                    0);
                m_panel.set_slider(kDemandBar + id, 1, m_demand[k].second, 128);
            }
        }
        // Overall supply of the goods the class wants against what it asks for at this tax.
        std::int32_t sum = 0;
        std::int32_t count = 0;
        for (const auto& n : rc.needs) {
            if (n.amount != 0) {
                sum += ratio(n.ware);
                ++count;
            }
        }
        const std::int32_t avg = count == 0 ? 128 : sum / count;
        const std::uint32_t idx = ((128U - (city->satisfaction_bonus >> 5U)) * tax) >> 7U;
        const auto threshold =
            static_cast<std::int32_t>(std::min<std::uint32_t>(128, (sim::tax_curve(idx) * rc.share) >> 7U));
        m_panel.set_slider(kGoodsBar, 1, avg, 128);
        m_panel.set_slider(kGoodsMarker, 1, threshold, 128);
        m_panel.set_text(kFoodLabel, c.text("GAME", 8));
        m_panel.set_slider(kFoodBar, 0, ratio(kFood) & ~7, 128);
        m_panel.set_slider(kFoodMarker, 0, 88, 128);

        // Public buildings the house lacks: for its class, or (growing) for the next.
        std::uint32_t mask = required(c, cls);
        if (cls + 1U < classes.size() && state > 0) {
            // The levels INFRA_MARKT (1) to INFRA_HOCHSCHULE (8) lie in the mask's low bits as the
            // panel's icons do (original): the next class's buildings not yet unlocked are left out.
            const std::uint32_t unlocked = owner != nullptr ? owner->infra_mask : 0;
            mask = required(c, cls + 1U) & unlocked;
        }
        const std::uint32_t missing = mask & ~sim::panel_services(*house);
        m_missing.clear();
        for (std::size_t k = 0; k < kServices; ++k) {
            if ((missing & kServiceOrder[k].bit) != 0) {
                m_missing.push_back(k);
            }
        }
        m_lacks_now = state < 1;
        std::int32_t x = m_service_x[0];
        for (std::size_t k = 0; k < kServices; ++k) {
            const auto id = kService + static_cast<std::int32_t>(k);
            const bool on = k < m_missing.size();
            m_panel.set_visible(id, on);
            m_panel.set_visible(kServiceHover + static_cast<std::int32_t>(k), on);
            if (!on) {
                continue;
            }
            // Packed from the left by the widths of the pictures; drawn red (selected).
            const std::size_t which = kServiceOrder[m_missing[k]].gadget;
            const assets::Gadget* g = m_panel.layout().find(id);
            m_panel.set_graphic(id, m_service_gfx[which], 8);
            m_panel.set_selected(id, true);
            if (g != nullptr) {
                m_panel.set_offset(id, x - m_service_x[k], 0);
            }
            x += m_service_w[which];
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& s, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        const assets::BuildingType* b = building_at(s, c);
        const sim::City* city = city_at(s, c);
        if (b == nullptr || city == nullptr) {
            return;
        }
        const std::uint8_t tax = tax_of(*city, b->resident_class);
        std::int32_t rate = 0;
        if (e.kind == ui::PanelEvent::Kind::Slid && e.id == kTaxSlider) {
            rate = m_panel.slider(kTaxSlider, 0, 128) + sim::CityOrder::kMinTax;
        } else if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == kTaxDown) {
            rate = tax - 4;
        } else if (e.kind == ui::PanelEvent::Kind::Clicked && e.id == kTaxUp) {
            rate = tax + 4;
        } else {
            return;
        }
        rate = std::clamp<std::int32_t>(rate, sim::CityOrder::kMinTax, sim::CityOrder::kMaxTax);
        if (rate == tax) {
            return;  // the slider reports every frame: a command only for a change
        }
        sim::CityOrder o;
        o.player = c.local;
        o.island = city->island;
        o.city = city->index;
        o.op = sim::CityOrder::Op::TaxRate;
        o.resident_class = b->resident_class;
        o.rate = static_cast<std::uint8_t>(rate);
        out.emplace_back(o);
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& s,
                                                  const PanelContext& c) const override {
        const auto line = [&](std::size_t n) { return std::optional<std::string>(help_line(c, n)); };
        switch (id) {
            case kTitle:
                return line(kHelpCityName);
            case kTaxSlider:
                return line(57);
            case kTaxDown:
                return line(55);
            case kTaxUp:
                return line(56);
            case kTaxLabel:
                return line(58);
            case kClassCount:
                return line(53);
            case kClassName:
                return line(54);
            case kGoodsHover:
                return line(60);
            case kFoodLabel:
                return line(61);
            case kFace: {
                const sim::House* house = c.world().house(s.island, s.x, s.y);
                const sim::City* city = city_at(s, c);
                if (house == nullptr || city == nullptr) {
                    return std::nullopt;
                }
                return line(static_cast<std::size_t>(78 + (1 - growth_state(*city, *house))));
            }
            default:
                break;
        }
        if (id >= kDemandFrame && id < kDemandFrame + static_cast<std::int32_t>(kDemandSlots)) {
            const auto k = static_cast<std::size_t>(id - kDemandFrame);
            const sim::City* city = city_at(s, c);
            if (k >= m_demand.size() || city == nullptr) {
                return std::nullopt;
            }
            const std::uint8_t w = m_demand[k].first;
            const std::string name = c.text("WAREM", w);
            if (w >= city->supply.size() || city->supply[w].demand == 0) {
                return ui::format_text(c.text("GAME", 63), {std::string_view(name)});
            }
            const std::int64_t p = std::int64_t{m_demand[k].second} * 100 / 128;
            return p == 0 ? ui::format_text(c.text("GAME", 62), {std::string_view(name)})
                          : ui::format_text(c.text("GAME", 61), {std::string_view(name), p});
        }
        if (id >= kServiceHover && id < kServiceHover + static_cast<std::int32_t>(kServices)) {
            const auto k = static_cast<std::size_t>(id - kServiceHover);
            if (k >= m_missing.size()) {
                return std::nullopt;
            }
            return line(kServiceOrder[m_missing[k]].help + (m_lacks_now ? 8U : 0U));
        }
        return std::nullopt;
    }

private:
    static constexpr std::int32_t kTitle = 36002;
    static constexpr std::int32_t kTaxSlider = 36004;
    static constexpr std::int32_t kTaxDown = 36005;
    static constexpr std::int32_t kTaxUp = 36006;
    static constexpr std::int32_t kGoodsBar = 36008;
    static constexpr std::int32_t kGoodsMarker = 36009;
    static constexpr std::int32_t kFace = 36010;
    static constexpr std::int32_t kTaxLabel = 36011;
    static constexpr std::int32_t kTaxIncome = 36012;
    static constexpr std::int32_t kClassCount = 36013;
    static constexpr std::int32_t kClassName = 36014;
    static constexpr std::int32_t kGoodsHover = 36015;
    static constexpr std::int32_t kFoodBar = 36017;
    static constexpr std::int32_t kFoodMarker = 36018;
    static constexpr std::int32_t kFoodLabel = 36019;
    static constexpr std::int32_t kService = 36021;
    static constexpr std::int32_t kDemandIcon = 36031;
    static constexpr std::int32_t kDemandBar = 36051;
    static constexpr std::int32_t kDemandFrame = 36071;
    static constexpr std::int32_t kServiceHover = 36091;
    static constexpr std::size_t kDemandSlots = 6;
    static constexpr std::size_t kServices = 8;
    static constexpr std::uint8_t kFood = 14;

    /// The public buildings in the order of the icons: coverage bit, which of the eight gadgets
    /// carries its picture (sprites 339 Kapelle, 340 Markt, 341 Wirtshaus, 342 Schule, 343 Theater,
    /// 344 Badehaus, 345 Hochschule, 346 Kirche) and its LEISTE line ("verlangen nach").
    struct Service {
        std::uint32_t bit;
        std::size_t gadget;
        std::size_t help;
    };
    static constexpr std::array<Service, kServices> kServiceOrder{{
        {sim::panel_service::kMarkt, 1, 64},
        {sim::panel_service::kKapelle, 0, 62},
        {sim::panel_service::kKirche, 7, 63},
        {sim::panel_service::kWirtshaus, 2, 65},
        {sim::panel_service::kSchule, 3, 66},
        {sim::panel_service::kHochschule, 6, 67},
        {sim::panel_service::kBadehaus, 5, 69},
        {sim::panel_service::kTheater, 4, 68},
    }};

    /// What a class needs of public buildings (Class_RequiredServices 0x0047F320).
    /// A class a package added needs what the original's class it looks like needs.
    static std::uint32_t required(const PanelContext& c, std::uint32_t cls) {
        return cls <= 255
                   ? sim::required_panel_services(sim::service_class(c.buildings, static_cast<std::uint8_t>(cls)))
                   : 0;
    }
    /// Goods a next class asks for only once their level is reached (sim::infra::ware_level).
    static bool ware_unlocked(std::uint8_t w, const sim::Player* owner) {
        return owner != nullptr && owner->unlocked(sim::infra::ware_level(w));
    }
    /// Sign of the growth rate: 1 grows, 0 stays, -1 shrinks a little, -2 a lot (0x0047B3B0).
    static int growth_state(const sim::City& city, const sim::House& house) {
        const std::int32_t rate = sim::growth_rate(city, house);
        return rate > 0 ? 1 : rate == 0 ? 0 : rate >= -64 ? -1 : -2;
    }
    static std::uint8_t tax_of(const sim::City& city, std::uint8_t cls) {
        return cls < city.tax_rate.size() ? city.tax_rate[cls] : std::uint8_t{128};
    }
    /// Taxes of one class per minute (City_ClassTax 0x0047F370).
    static std::int64_t class_tax(const sim::City& city, const assets::ResidentClass& rc, std::uint8_t cls,
                                  std::uint8_t tax) {
        const std::int64_t n = cls < city.inhabitants.size() ? city.inhabitants[cls] : 0;
        return (std::int64_t{rc.tax} * n * 6 / 32) * tax / 128;
    }

    std::int32_t m_gfx_ware = 192;
    std::int32_t m_face = 319;
    std::array<std::int32_t, kServices> m_service_gfx{};
    std::array<std::int32_t, kServices> m_service_x{};
    std::array<std::int32_t, kServices> m_service_w{};
    std::vector<std::pair<std::uint8_t, std::int32_t>> m_demand;  ///< ware, supply 0..128
    std::vector<std::size_t> m_missing;                           ///< into kServiceOrder
    bool m_lacks_now = false;
};

// ---- Shipyards -----------------------------------------------------------------------------------

const sim::CostBuilding* cost_building_at(const Selection& s, const PanelContext& c, sim::CostBuilding::Kind kind) {
    for (const sim::CostBuilding& b : c.world().cost_buildings()) {
        if (b.kind == kind && b.island == s.island && b.x == s.x && b.y == s.y) {
            return &b;
        }
    }
    return nullptr;
}

/// WERFT (the yard and its order) and BAUSHIP (the choice of the ship type)
/// (ShipyardPanel_Refresh 0x00435B40, ShipTypePanel_Refresh 0x00435EF0).
class ShipyardPanel final : public SelectionPanel {
public:
    ShipyardPanel(assets::GadgetLayout werft, assets::GadgetLayout bauship)
        : m_werft(std::move(werft), 1.0), m_bauship(std::move(bauship), 1.0) {
        m_gfx_ware = m_werft.layout().constant("GFXWARE").value_or(192);
        for (std::int32_t k = 0; k < 4; ++k) {
            if (const assets::Gadget* g = m_bauship.layout().find(kType + k)) {
                m_type_x[static_cast<std::size_t>(k)] = g->offset[0];
            }
        }
    }
    void set_scale(double scale) override {
        m_werft.set_scale(scale);
        m_bauship.set_scale(scale);
    }
    ui::Panel& panel() override { return m_choose ? m_bauship : m_werft; }

    void refresh(const Selection& s, const PanelContext& c) override {
        if (m_pages.update(s) != SelectionPages::Change::None) {
            m_choose = false;
        }
        const assets::BuildingType* b = building_at(s, c);
        const sim::CostBuilding* yard = cost_building_at(s, c, sim::CostBuilding::Kind::Shipyard);
        const sim::City* city = city_at(s, c);
        if (b == nullptr || yard == nullptr) {
            return;
        }
        const std::string tons = c.text("GAME", kGameTons);
        const auto ware_row = [&](ui::Panel& p, std::int32_t icon, std::int32_t amount, std::uint8_t w,
                                  std::uint32_t stock) {
            p.set_graphic(icon, ui::ware_sprite(m_gfx_ware, c.world().goods(), w), 0);
            p.set_text(amount, ui::format_text(tons, {std::int64_t{stock >> 5U}}));
        };
        if (m_choose) {
            m_bauship.set_text(60002, city != nullptr ? city_name(*city) : std::string());
            // A small yard (three tiles wide) builds the small types only, moved to the middle.
            const bool small = b->size_x == 3;
            for (std::int32_t k = 0; k < 4; ++k) {
                const bool big = k == 1 || k == 3;
                m_bauship.set_visible(kType + k, !(small && big));
                m_bauship.set_offset(kType + k, m_type_x[static_cast<std::size_t>(k)] + (small && !big ? 40 : 0), 0);
            }
            // The original keeps a large type chosen at a large yard; its order then builds it in the
            // small one too (S-005, brought back by OriginalBugs::small_yard_warships).
            if (small && (m_type == 1 || m_type == 3) && !c.world().original_bugs().small_yard_warships) {
                m_type = static_cast<std::uint8_t>(m_type - 1);
            }
            for (std::int32_t k = 0; k < 4; ++k) {
                m_bauship.set_selected(kType + k, k == m_type);
            }
            if (const assets::FigureType* f = c.figures.get(sim::figure::ship_figure(m_type))) {
                m_bauship.set_text(60015, ui::format_text(tons, {std::int64_t{f->ship.cargo_slots} * 50}));
                m_bauship.set_text(60017, std::to_string(f->ship.max_cannons));
                m_bauship.set_text(60019, ui::format_text(c.text("GAME", 47), {std::int64_t{f->speed / 20}}));
                const std::uint32_t wood = f->ship.build_amount;
                m_bauship.set_text(60020, std::to_string(wood >> 5U));
                const std::uint64_t cloth =
                    b->raw_amount != 0 ? std::uint64_t{b->raw2_amount} * wood / (std::uint64_t{b->raw_amount} * 32) : 0;
                m_bauship.set_text(60021, std::to_string(cloth));
            }
            ware_row(m_bauship, 60031, 60041, b->raw_ware, yard->wood);
            ware_row(m_bauship, 60032, 60042, b->raw2_ware, yard->cloth);
            return;
        }
        m_werft.set_text(39002, city != nullptr ? city_name(*city) : std::string());
        m_werft.set_text(39006, building_name(*b, c));
        m_werft.set_text(39003, c.text("GAME", kGameRunningCost));
        m_werft.set_text(39004, std::to_string(yard->enabled ? b->running_cost : b->idle_cost));
        ware_row(m_werft, 39021, 39031, b->raw_ware, yard->wood);
        ware_row(m_werft, 39022, 39032, b->raw2_ware, yard->cloth);
        const bool order = yard->order != sim::CostBuilding::kNoOrder;
        for (const std::int32_t id : {39011, 39012, 39014, 39015}) {
            m_werft.set_visible(id, order);
        }
        m_werft.set_visible(39016, !(order));
        m_werft.set_visible(39017, order);
        m_werft.set_selected(39018, !yard->enabled);
        if (order) {
            const assets::FigureType* f = c.figures.get(sim::figure::ship_figure(yard->order));
            const std::uint32_t need = f != nullptr ? std::max<std::uint32_t>(f->ship.build_amount, 1) : 1;
            m_werft.set_slider(
                39012, 1,
                static_cast<std::int32_t>(std::min<std::uint64_t>(std::uint64_t{yard->progress} * 1024 / need, 1024)),
                1024);
            for (const std::int32_t id : {39014, 39015}) {
                m_werft.set_graphic(id, yard->order, 0);
            }
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection& s, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const sim::CostBuilding* yard = cost_building_at(s, c, sim::CostBuilding::Kind::Shipyard);
        if (yard == nullptr) {
            return;
        }
        sim::ShipyardOrder o;
        o.player = c.local;
        o.island = s.island;
        o.x = s.x;
        o.y = s.y;
        if (m_choose) {
            if (e.id >= kType && e.id < kType + 4) {
                m_type = static_cast<std::uint8_t>(e.id - kType);
            } else if (e.id == 60022) {
                o.op = sim::ShipyardOrder::Op::Build;
                o.ship_type = m_type;
                out.emplace_back(o);
                m_choose = false;
            }
            return;
        }
        switch (e.id) {
            case 39016:  // order a ship: the type choice, the first type picked
                m_choose = true;
                m_type = 0;
                break;
            case 39017:
                o.op = sim::ShipyardOrder::Op::Cancel;
                out.emplace_back(o);
                break;
            case 39018:
                o.op = sim::ShipyardOrder::Op::Enable;
                o.on = !yard->enabled;
                out.emplace_back(o);
                break;
            default:
                break;
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& s,
                                                  const PanelContext& c) const override {
        const auto line = [&](std::size_t n) { return std::optional<std::string>(help_line(c, n)); };
        const sim::CostBuilding* yard = cost_building_at(s, c, sim::CostBuilding::Kind::Shipyard);
        if (m_choose) {
            if (id >= kType && id < kType + 4) {
                return line(static_cast<std::size_t>(95 + id - kType));
            }
            switch (id) {
                case 60002:
                    return line(kHelpCityName);
                case 60015:
                    return line(89);
                case 60017:
                    return line(90);
                case 60019:
                    return line(91);
                case 60020:
                    return line(93);
                case 60021:
                    return line(92);
                case 60022:
                    return line(87);
                case 60051:
                    return line(101);
                case 60052:
                    return line(102);
                default:
                    return std::nullopt;
            }
        }
        switch (id) {
            case 39002:
                return line(kHelpCityName);
            case 39013:
                return line(94);
            case 39015:
                return yard != nullptr && yard->order < 4 ? line(95U + yard->order) : std::nullopt;
            case 39016:
                return line(87);
            case 39017:
                return line(88);
            case 39018:
                return line(yard != nullptr && yard->enabled ? 99 : 100);
            case 39041:
                return line(101);
            case 39042:
                return line(102);
            default:
                return std::nullopt;
        }
    }

    bool back() override {
        if (!m_choose) {
            return false;
        }
        m_choose = false;
        return true;
    }

private:
    static constexpr std::int32_t kType = 60003;
    ui::Panel m_werft;
    ui::Panel m_bauship;
    std::int32_t m_gfx_ware = 192;
    std::array<std::int32_t, 4> m_type_x{};
    bool m_choose = false;
    std::uint8_t m_type = 0;
    SelectionPages m_pages;
};

// ---- Castles -------------------------------------------------------------------------------------

/// MILITAR: training places, the unit to train, its pay and weapons (CastlePanel_Refresh 0x004385E0).
class CastlePanel final : public SimplePanel {
public:
    explicit CastlePanel(assets::GadgetLayout layout) : SimplePanel(std::move(layout)) {
        const assets::GadgetLayout& l = m_panel.layout();
        m_gfx_ware = l.constant("GFXWARE").value_or(192);
        m_gfx_empty = l.constant("GFXWARELEER").value_or(188) + kEmptyHold;
        if (const assets::Gadget* g = l.find(40007)) {
            m_weapon_icon = g->gfx;
        }
    }

    void refresh(const Selection& s, const PanelContext& c) override {
        const assets::BuildingType* b = building_at(s, c);
        const sim::CostBuilding* castle = cost_building_at(s, c, sim::CostBuilding::Kind::Castle);
        const sim::City* city = city_at(s, c);
        const sim::Player* owner = city != nullptr ? c.world().player(city->owner) : nullptr;
        if (b == nullptr || castle == nullptr || owner == nullptr) {
            return;
        }
        m_panel.set_text(40002, city_name(*city));
        const std::int32_t places = std::min<std::int32_t>(b->max_wares, 9);
        const std::int32_t colour = sim::four_colour_index(owner->banner);
        for (std::int32_t k = 0; k < 9; ++k) {
            const bool place = k < places;
            const bool used = place && static_cast<std::size_t>(k) < castle->slots.size();
            m_panel.set_visible(40091 + k, place);
            m_panel.set_visible(40031 + k, place);
            m_panel.set_visible(40071 + k, used);
            m_panel.set_visible(40051 + k, used);
            m_panel.set_selected(40091 + k, k == m_slot);
            if (!place) {
                continue;
            }
            if (!used) {
                m_panel.set_graphic(40031 + k, m_gfx_empty, 0);
                m_panel.set_disabled(40031 + k, false);
                continue;
            }
            const sim::CastleSlot& slot = castle->slots[static_cast<std::size_t>(k)];
            const assets::FigureType* f = c.figures.get(slot.figure);
            // The soldier icons are the wares of the units (wSOLDAT1 ...), in their colours.
            m_panel.set_graphic(40031 + k, m_gfx_ware + sim::unit_ware(slot.figure), 0);
            m_panel.set_disabled(40031 + k, slot.training);
            m_panel.set_text(40071 + k, std::to_string(std::min<std::uint32_t>(slot.progress, 320) * 10 / 320));
            const std::uint32_t max = f != nullptr ? std::max<std::uint32_t>(f->combat.max_health, 1) : 1;
            m_panel.set_slider(40051 + k, 1, static_cast<std::int32_t>(std::min(slot.health, max)),
                               static_cast<std::int32_t>(max));
        }
        for (std::int32_t u = 0; u < 4; ++u) {
            m_panel.set_selected(40021 + u, u == m_unit);
        }
        const assets::FigureType* unit = c.figures.get(unit_figure(colour));
        if (unit != nullptr) {
            m_panel.set_text(40006, std::to_string(c.figures.unit_upkeep(unit->kind)));
            m_panel.set_graphic(40007, m_weapon_icon + unit->combat.weapon_ware - 11, 0);
            m_panel.set_text(40008, std::to_string(unit->combat.weapon_amount >> 5U));
        }
        for (std::int32_t w = 0; w < 3; ++w) {
            m_panel.set_graphic(40111 + w, m_gfx_ware + 11 + w, 0);
            m_panel.set_text(40121 + w,
                             ui::format_text(c.text("GAME", kGameTons),
                                             {std::int64_t{castle->weapons[static_cast<std::size_t>(w)] >> 5U}}));
        }
        const bool free = static_cast<std::int32_t>(castle->slots.size()) < places;
        m_panel.set_disabled(40009, !free);
        const bool chosen = static_cast<std::size_t>(m_slot) < castle->slots.size();
        m_panel.set_visible(40003, chosen);
        m_panel.set_visible(40004, !(!chosen || castle->slots[static_cast<std::size_t>(std::max(m_slot, 0))].training));
    }

    void clicked(const ui::PanelEvent& e, const Selection& s, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const sim::CostBuilding* castle = cost_building_at(s, c, sim::CostBuilding::Kind::Castle);
        const sim::City* city = city_at(s, c);
        const sim::Player* owner = city != nullptr ? c.world().player(city->owner) : nullptr;
        if (castle == nullptr || owner == nullptr) {
            return;
        }
        if (e.id >= 40091 && e.id < 40100) {
            m_slot = e.id - 40091;
            return;
        }
        if (e.id >= 40021 && e.id <= 40024) {
            m_unit = e.id - 40021;
            return;
        }
        sim::CastleOrder o;
        o.player = c.local;
        o.island = s.island;
        o.x = s.x;
        o.y = s.y;
        o.slot = static_cast<std::uint8_t>(std::max(m_slot, 0));
        switch (e.id) {
            case 40009:
                o.op = sim::CastleOrder::Op::Recruit;
                o.figure = unit_figure(sim::four_colour_index(owner->banner));
                out.emplace_back(o);
                break;
            case 40003:
                o.op = sim::CastleOrder::Op::Cancel;
                out.emplace_back(o);
                break;
            case 40004:
                m_picking = true;  // the unit's destination: the next click on the map
                m_all = c.shift;   // Open1602: with Shift every trained unit goes
                break;
            default:
                break;
        }
    }

    [[nodiscard]] bool picking() const override { return m_picking; }
    void picked(std::uint32_t x, std::uint32_t y, const Selection& s, const PanelContext& c,
                std::vector<sim::Command>& out) override {
        m_picking = false;
        sim::CastleOrder o;
        o.player = c.local;
        o.island = s.island;
        o.x = s.x;
        o.y = s.y;
        o.op = m_all || c.shift ? sim::CastleOrder::Op::ReleaseAll : sim::CastleOrder::Op::Release;
        o.slot = static_cast<std::uint8_t>(std::max(m_slot, 0));
        o.spot = sim::Target::spot(2 * x + 1, 2 * y + 1);
        out.emplace_back(o);
    }
    bool back() override {
        const bool was = m_picking;
        m_picking = false;
        return was;
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& s,
                                                  const PanelContext& c) const override {
        const auto line = [&](std::size_t n) { return std::optional<std::string>(help_line(c, n)); };
        switch (id) {
            case 40003:
                return line(85);
            case 40004:
                return ui::tr("castle.release", {help_line(c, 84)});
            case 40006:
                return line(83);
            case 40009:
                return line(86);
            default:
                break;
        }
        if (id >= 40021 && id <= 40024) {
            return line(82);
        }
        const sim::CostBuilding* castle = cost_building_at(s, c, sim::CostBuilding::Kind::Castle);
        if (castle == nullptr) {
            return std::nullopt;
        }
        if (id == 40008) {
            const sim::City* city = city_at(s, c);
            const sim::Player* owner = city != nullptr ? c.world().player(city->owner) : nullptr;
            const assets::FigureType* unit =
                owner != nullptr ? c.figures.get(unit_figure(sim::four_colour_index(owner->banner))) : nullptr;
            if (unit == nullptr) {
                return std::nullopt;
            }
            return ui::format_text(c.text("GAME", 73), {std::int64_t{unit->combat.weapon_amount >> 5U},
                                                        std::string_view(c.text("WARE", unit->combat.weapon_ware))});
        }
        if (id >= 40091 && id < 40100) {
            const auto k = static_cast<std::size_t>(id - 40091);
            if (k >= castle->slots.size()) {
                return std::nullopt;
            }
            const sim::CastleSlot& slot = castle->slots[k];
            std::string text = slot.progress >= 320
                                   ? c.text("GAME", 75)
                                   : ui::format_text(c.text("GAME", 74), {std::int64_t{slot.progress * 100 / 320}});
            if (!slot.armed) {
                text += c.text("GAME", 76);
            }
            return text;
        }
        if (id >= 40131 && id <= 40133) {
            const auto w = static_cast<std::uint8_t>(11 + id - 40131);
            return ui::format_text(c.text("GAME", 50),
                                   {std::int64_t{castle->weapons[static_cast<std::size_t>(w - 11)] >> 5U},
                                    std::string_view(c.text("WARE", w))});
        }
        return std::nullopt;
    }

private:
    /// SOLDAT1, KAVALERIE1, MUSKETIER1, KANONIER1 (the chosen family) of a colour.
    [[nodiscard]] std::uint16_t unit_figure(std::int32_t colour) const noexcept {
        return sim::figure::unit_figure(static_cast<std::size_t>(m_unit), colour);
    }
    std::int32_t m_gfx_ware = 192;
    std::int32_t m_gfx_empty = 192;
    std::int32_t m_weapon_icon = 432;
    std::int32_t m_slot = 0;
    std::int32_t m_unit = 0;
    bool m_picking = false;
    bool m_all = false;  ///< the pick sends every trained unit (Shift on the send-out arrow)
};

// ---- A foreign Kontor --------------------------------------------------------------------------

/// HANDEL: what a foreign Kontor with a trade treaty wants and offers (ForeignKontorPanel_Refresh
/// 0x004357D0).
class ForeignKontorPanel final : public SelectionPanel {
public:
    explicit ForeignKontorPanel(assets::GadgetLayout layout) : m_lists(std::move(layout)) {}
    void set_scale(double scale) override { m_lists.panel().set_scale(scale); }
    ui::Panel& panel() override { return m_lists.panel(); }

    void refresh(const Selection& s, const PanelContext& c) override {
        if (const sim::City* city = city_at(s, c)) {
            m_lists.refresh(*city, c);
        }
    }
    void clicked(const ui::PanelEvent&, const Selection&, const PanelContext&, std::vector<sim::Command>&) override {}

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection& s,
                                                  const PanelContext& c) const override {
        const sim::City* city = city_at(s, c);
        return city != nullptr ? m_lists.help(id, *city, c) : std::nullopt;
    }

private:
    TradeLists m_lists;
};

}  // namespace

// ---- The set -------------------------------------------------------------------------------------

SelectionPanels::SelectionPanels(const assets::GameDirectory& game) {
    const auto load = [&](const char* file) -> std::optional<assets::GadgetLayout> {
        auto l = assets::load_gad(game, file);
        return l ? std::optional<assets::GadgetLayout>(std::move(l).value()) : std::nullopt;
    };
    if (auto l = load("PROD.GAD")) {
        const std::int32_t ware = l->constant("GFXWARE").value_or(192);
        m_panels["PROD"] = std::make_unique<ProductionPanel>(std::move(*l), kProd, ware);
    }
    if (auto l = load("PLANTAGE.GAD")) {
        const std::int32_t ware = l->constant("GFXWARE").value_or(192);
        m_panels["PLANTAGE"] = std::make_unique<ProductionPanel>(std::move(*l), kPlantage, ware);
    }
    if (auto l = load("BERGWERK.GAD")) {
        const std::int32_t ware = l->constant("GFXWARE").value_or(192);
        m_panels["BERGWERK"] = std::make_unique<ProductionPanel>(std::move(*l), kMine, ware);
    }
    if (auto stadt = load("STADT.GAD")) {
        auto kontor = load("KONTOR.GAD");
        auto lager = load("LAGER.GAD");
        if (kontor && lager) {
            m_panels["STADT"] = std::make_unique<CityPanel>(std::move(*stadt), std::move(*kontor), std::move(*lager));
        }
    }
    {
        auto ship = load("SHIP.GAD");
        auto transfer = load("TRANSFER.GAD");
        auto trade = load("TRADE.GAD");
        auto route = load("ROUTE.GAD");
        auto lager = load("LAGER.GAD");
        auto handel = load("HANDEL.GAD");
        auto pirat = load("PIRAT.GAD");
        auto piratord = load("PIRATORD.GAD");
        if (ship && transfer && trade && route && lager && handel && pirat && piratord) {
            m_panels["SHIP"] = make_ship_panel(ShipLayouts{
                std::move(*ship), std::move(*transfer), std::move(*trade), std::move(*route), std::move(*lager),
                std::move(*handel), std::move(*pirat), std::move(*piratord), load("MUSIK.GAD"), load("TRADER.GAD")});
        }
    }
    if (auto werft = load("WERFT.GAD")) {
        if (auto bauship = load("BAUSHIP.GAD")) {
            m_panels["WERFT"] = std::make_unique<ShipyardPanel>(std::move(*werft), std::move(*bauship));
        }
    }
    if (auto l = load("MILITAR.GAD")) {
        m_panels["MILITAR"] = std::make_unique<CastlePanel>(std::move(*l));
    }
    if (auto l = load("HANDEL.GAD")) {
        m_panels["HANDEL"] = std::make_unique<ForeignKontorPanel>(std::move(*l));
    }
    if (auto l = load("SKSHIP.GAD")) {
        m_panels["SKSHIP"] = make_ship_unit_panel(std::move(*l));
    }
    if (auto l = load("SKSOLDAT.GAD")) {
        m_panels["SKSOLDAT"] = make_soldier_unit_panel(std::move(*l));
    }
    if (auto l = load("MKSHIP.GAD")) {
        m_panels["MKSHIP"] = make_group_panel(std::move(*l), true);
    }
    if (auto l = load("MKSOLDAT.GAD")) {
        m_panels["MKSOLDAT"] = make_group_panel(std::move(*l), false);
    }
    if (auto l = load("SIEDLER.GAD")) {
        m_panels["SIEDLER"] = std::make_unique<SettlerPanel>(std::move(*l));
    }
    if (auto l = load("SHIPLIST.GAD")) {
        m_panels["SHIPLIST"] = make_list_panel(std::move(*l), true);
    }
    if (auto l = load("STADTLST.GAD")) {
        m_panels["STADTLST"] = make_list_panel(std::move(*l), false);
    }
    if (auto vertrag = load("VERTRAG.GAD")) {
        if (auto tribut = load("TRIBUT.GAD")) {
            m_panels["VERTRAG"] = make_diplomacy_panel(std::move(*vertrag), std::move(*tribut));
        }
    }
    if (auto l = load("INFRA.GAD")) {
        const std::int32_t bauhaus = l->constant("GFXBAUHAUS").value_or(81);
        m_panels["INFRA"] = std::make_unique<InfraPanel>(std::move(*l), bauhaus);
    }
}

void SelectionPanels::set_scale(double scale) {
    for (auto& [name, panel] : m_panels) {
        panel->set_scale(scale);
    }
}

SelectionPanel* SelectionPanels::military_panel_for(const PanelContext& c) {
    if (c.units == nullptr || c.units->empty()) {
        return nullptr;
    }
    const auto it =
        m_panels.find(c.units->kind() == ui::UnitRef::Kind::Ship ? (c.units->size() == 1 ? "SKSHIP" : "MKSHIP")
                                                                 : (c.units->size() == 1 ? "SKSOLDAT" : "MKSOLDAT"));
    return it != m_panels.end() ? it->second.get() : nullptr;
}

SelectionPanel* SelectionPanels::panel_for(const Selection& s, const PanelContext& c) {
    const auto get = [&](const char* name) -> SelectionPanel* {
        const auto it = m_panels.find(name);
        return it != m_panels.end() ? it->second.get() : nullptr;
    };
    if (s.kind == Selection::Kind::Ship) {
        return c.world().ship(s.ship) != nullptr ? get("SHIP") : nullptr;
    }
    if (s.kind == Selection::Kind::ShipList) {
        return get("SHIPLIST");
    }
    if (s.kind == Selection::Kind::CityList) {
        return get("STADTLST");
    }
    if (s.kind == Selection::Kind::Diplomacy) {
        return get("VERTRAG");
    }
    if (s.kind != Selection::Kind::Building) {
        return nullptr;
    }
    const assets::BuildingType* b = building_at(s, c);
    if (b == nullptr) {
        return nullptr;
    }
    const sim::MapCell& top = c.world().islands()[s.island].map.top(s.x, s.y);
    if (top.owner != c.local) {
        // Foreign buildings: only Kontor and market, and only with a trade treaty.
        const sim::City* city = city_at(s, c);
        if ((b->prod_kind == sim::prod::kMarket || b->prod_kind == sim::prod::kKontor) && city != nullptr &&
            c.local != sim::kNoPlayer && c.world().diplomacy().may_trade(c.local, city->owner)) {
            return get("HANDEL");
        }
        return nullptr;
    }
    // Panel_UpdateSelection 0x0043AA00: the building's HAUS_PRODTYP Kind decides.
    switch (b->prod_kind) {
        case sim::prod::kWorkshop:
            return c.world().production(s.island, s.x, s.y) != nullptr ? get("PROD") : nullptr;
        case sim::prod::kFarm:
        case sim::prod::kPasture:
        case sim::prod::kHunter:
        case sim::prod::kFisher:
            return c.world().production(s.island, s.x, s.y) != nullptr ? get("PLANTAGE") : nullptr;
        case sim::prod::kMine:
            return c.world().production(s.island, s.x, s.y) != nullptr ? get("BERGWERK") : nullptr;
        default:
            break;
    }
    if (b->prod_kind >= sim::prod::kPublicFirst && b->prod_kind <= sim::prod::kPublicLast) {
        return get("INFRA");
    }
    if (b->prod_kind == sim::prod::kShipyard && cost_building_at(s, c, sim::CostBuilding::Kind::Shipyard) != nullptr) {
        return get("WERFT");
    }
    if (b->prod_kind == sim::prod::kMilitary && cost_building_at(s, c, sim::CostBuilding::Kind::Castle) != nullptr &&
        city_at(s, c) != nullptr) {
        return get("MILITAR");
    }
    if (b->prod_kind == sim::prod::kHouse && c.world().house(s.island, s.x, s.y) != nullptr &&
        city_at(s, c) != nullptr) {
        return get("SIEDLER");
    }
    if ((b->prod_kind == sim::prod::kMarket || b->prod_kind == sim::prod::kKontor) && city_at(s, c) != nullptr) {
        return get("STADT");  // market or Kontor
    }
    return nullptr;
}

}  // namespace o1602::app
