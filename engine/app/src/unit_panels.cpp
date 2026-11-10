// SPDX-License-Identifier: GPL-3.0-or-later
//
// The windows of the military mode (docs/re/ui.md#kampfmodus, MilitaryPanel_Refresh 0x00439BA0): one
// ship (SKSHIP), one land unit (SKSOLDAT), a group of ships (MKSHIP) or of land units (MKSOLDAT).
// They work on the selection (filters, formations) and give orders; scuttling and disbanding are
// asked about by the map view (ADR 0010).
#include <algorithm>
#include <string_view>

#include "panel_common.hpp"

#include "o1602/sim/ships.hpp"
#include "o1602/ui/package_icons.hpp"
#include "o1602/ui/text_format.hpp"

namespace o1602::app {

namespace {

using namespace panel_detail;

constexpr std::uint8_t kCannonWare = sim::ware::kCannons;

const sim::Ship* the_ship(const PanelContext& c) {
    if (c.units == nullptr || c.units->size() != 1 || c.units->kind() != ui::UnitRef::Kind::Ship) {
        return nullptr;
    }
    return c.world().ship(c.units->units().front().id);
}

const sim::Soldier* the_soldier(const PanelContext& c) {
    if (c.units == nullptr || c.units->size() != 1 || c.units->kind() != ui::UnitRef::Kind::Soldier) {
        return nullptr;
    }
    return c.world().soldier(c.units->units().front().id);
}

/// SKSHIP: cannons, health, stop, patrol, white flag, goods overboard, holds.
class ShipUnitPanel final : public SimplePanel {
public:
    explicit ShipUnitPanel(assets::GadgetLayout layout) : SimplePanel(std::move(layout)) {
        m_gfx_ware = m_panel.layout().constant("GFXWARE").value_or(192);
        m_gfx_empty = m_panel.layout().constant("GFXWARELEER").value_or(188) + kEmptyHold;
    }

    void refresh(const Selection&, const PanelContext& c) override {
        const sim::Ship* ship = the_ship(c);
        if (ship == nullptr) {
            return;
        }
        const bool own = ship->owner == c.local;
        // Open1602: the name can be typed here too, as in the ship window.
        m_panel.set_text(kName, c.shown_name(NameTarget::of(*ship), ship_name(*ship)));
        m_panel.set_no_select(kName, !can_rename(c.world(), c.local, NameTarget::of(*ship)));
        const bool moving = !ship->target.empty() || ship->motion.path_index < ship->motion.path.size();
        for (const std::int32_t id :
             {kCannonsDown, kCannons, kHealthFrame, kHealth, kHealthHover, kPatrol, kWhiteFlag, kDump}) {
            m_panel.set_visible(id, own);
        }
        m_panel.set_visible(kStop, own && moving);
        m_panel.set_disabled(kCannonsDown, ship->cannons == 0);
        m_panel.set_text(kCannons, std::to_string(ship->cannons));
        const double health = ui::unit_health(c.world(), c.figures, c.units->units().front()).value_or(1.0);
        m_panel.set_slider(kHealth, 0, static_cast<std::int32_t>(health * 1000.0), 1000);
        m_panel.set_selected(kPatrol, ship->route.patrol() || m_picking);
        m_panel.set_selected(kWhiteFlag, ship->held);
        const std::size_t n = sim::usable_holds(*ship, c.figures);
        for (std::size_t h = 0; h < 8; ++h) {
            const auto k = static_cast<std::int32_t>(h);
            const bool hold = own && h < n;
            m_panel.set_visible(kHoldIcon + k, hold);
            m_panel.set_visible(kHoldButton + k, hold);
            const bool filled = hold && ship->cargo[h].ware != 0 && ship->cargo[h].amount != 0;
            m_panel.set_visible(kHoldBar + k, filled);
            if (hold) {
                m_panel.set_graphic(
                    kHoldIcon + k,
                    filled ? ui::ware_sprite(m_gfx_ware, c.world().goods(), ship->cargo[h].ware) : m_gfx_empty, 0);
            }
            if (filled) {
                m_panel.set_slider(
                    kHoldBar + k, 1,
                    static_cast<std::int32_t>(std::min(ship->cargo[h].amount, sim::hold_capacity(*ship))),
                    static_cast<std::int32_t>(sim::hold_capacity(*ship)));
            }
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection&, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        const sim::Ship* ship = the_ship(c);
        if (ship == nullptr || ship->owner != c.local || e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        const auto command = [&](sim::ShipCommand::Op op) {
            sim::ShipCommand o;
            o.player = c.local;
            o.ship = ship->id;
            o.op = op;
            return o;
        };
        switch (e.id) {
            case kName:
                c.start_rename(NameTarget::of(*ship), out);
                break;
            case kStop:
                out.emplace_back(sim::ShipOrder{c.local, ship->id, sim::Target{}});
                break;
            case kPatrol:
                if (ship->route.patrol()) {
                    out.emplace_back(sim::ShipOrder{c.local, ship->id, sim::Target{}});
                } else {
                    m_picking = !m_picking;  // the next click on the map is the far end
                }
                break;
            case kWhiteFlag: {
                sim::ShipCommand o = command(sim::ShipCommand::Op::WhiteFlag);
                o.on = !ship->held;
                out.emplace_back(o);
                break;
            }
            case kDump:
                out.emplace_back(command(sim::ShipCommand::Op::Dump));
                break;
            case kCannonsDown:
                if (ship->cannons > 0) {
                    out.emplace_back(command(sim::ShipCommand::Op::UnmountCannon));
                }
                break;
            default:
                if (e.id >= kHoldButton && e.id < kHoldButton + 8) {
                    const auto h = static_cast<std::size_t>(e.id - kHoldButton);
                    if (h >= ship->cargo.size()) {
                        break;
                    }
                    const sim::CargoSlot& slot = ship->cargo[h];
                    if (sim::is_soldier_ware(slot.ware) && slot.amount != 0) {
                        sim::UnloadOrder o;
                        o.player = c.local;
                        o.ship = ship->id;
                        o.hold = static_cast<std::uint8_t>(h);
                        out.emplace_back(o);
                    } else if (slot.ware == kCannonWare && slot.amount >= 32) {
                        out.emplace_back(mount_cannons(*ship, h, c));
                    }
                }
                break;
        }
    }

    [[nodiscard]] bool picking() const override { return m_picking; }
    void picked(std::uint32_t x, std::uint32_t y, const Selection&, const PanelContext& c,
                std::vector<sim::Command>& out) override {
        m_picking = false;
        if (const sim::Ship* ship = the_ship(c); ship != nullptr && ship->owner == c.local) {
            sim::ShipCommand o;
            o.player = c.local;
            o.ship = ship->id;
            o.op = sim::ShipCommand::Op::Patrol;
            o.x = static_cast<std::uint16_t>(x);
            o.y = static_cast<std::uint16_t>(y);
            out.emplace_back(o);
        }
    }
    bool back() override { return std::exchange(m_picking, false); }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection&,
                                                  const PanelContext& c) const override {
        const sim::Ship* ship = the_ship(c);
        if (ship == nullptr) {
            return std::nullopt;
        }
        switch (id) {
            case kCannonsDown:
                return help_line(c, 119);
            case kCannons:
                return ship->cannons > 0 ? ui::format_text(c.text("GAME", 23), {std::int64_t{ship->cannons}})
                                         : c.text("GAME", 24);
            case kHealthHover:
                return help_line(c, 121);
            case kStop:
                return help_line(c, 127);
            case kPatrol:
                return help_line(c, ship->route.patrol() ? 113 : 112);
            case kWhiteFlag:
                return help_line(c, ship->held ? 114 : 115);
            case kDump:
                return help_line(c, 126);
            default:
                break;
        }
        if (id >= kHoldButton && id < kHoldButton + 8) {
            const auto h = static_cast<std::size_t>(id - kHoldButton);
            if (h < ship->cargo.size()) {
                return hold_help(*ship, h, c, true);
            }
        }
        return std::nullopt;
    }

private:
    static constexpr std::int32_t kName = 51002;
    static constexpr std::int32_t kCannonsDown = 51011;
    static constexpr std::int32_t kCannons = 51012;
    static constexpr std::int32_t kHealthFrame = 51013;
    static constexpr std::int32_t kHealth = 51014;
    static constexpr std::int32_t kHealthHover = 51015;
    static constexpr std::int32_t kStop = 51021;
    static constexpr std::int32_t kPatrol = 51022;
    static constexpr std::int32_t kWhiteFlag = 51023;
    static constexpr std::int32_t kDump = 51024;
    static constexpr std::int32_t kHoldIcon = 51051;
    static constexpr std::int32_t kHoldBar = 51071;
    static constexpr std::int32_t kHoldButton = 51091;
    std::int32_t m_gfx_ware = 192;
    std::int32_t m_gfx_empty = 192;
    bool m_picking = false;
};

/// SKSOLDAT: the unit's kind, stop, patrol, disband with its price.
class SoldierUnitPanel final : public SimplePanel {
public:
    explicit SoldierUnitPanel(assets::GadgetLayout layout) : SimplePanel(std::move(layout)) {}

    void refresh(const Selection&, const PanelContext& c) override {
        const sim::Soldier* s = the_soldier(c);
        if (s == nullptr) {
            return;
        }
        const bool own = s->owner == c.local;
        const assets::FigureType* t = c.figures.get(s->figure);
        // The kind of unit (FIGKIND); the natives' warriors have none there (LEISTE 227, 0x0040A3D0).
        const std::int32_t kind = t != nullptr ? t->kind : 0;
        m_panel.set_text(
            kName, kind >= 1 && kind <= 4 ? c.text("FIGKIND", static_cast<std::size_t>(kind)) : help_line(c, 227));
        m_panel.set_visible(kStop, own && !s->target.empty());
        for (const std::int32_t id : {kPatrol, kDisband, kCost, kCoin}) {
            m_panel.set_visible(id, own);
        }
        m_panel.set_selected(kPatrol, s->patrol.active || m_picking);
        m_panel.set_text(kCost, std::to_string(t != nullptr ? c.figures.unit_cost(t->kind) : 0));
    }

    void clicked(const ui::PanelEvent& e, const Selection&, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        const sim::Soldier* s = the_soldier(c);
        if (s == nullptr || s->owner != c.local || e.kind != ui::PanelEvent::Kind::Clicked) {
            return;
        }
        sim::SoldierCommand o;
        o.player = c.local;
        o.soldier = s->id;
        switch (e.id) {
            case kStop:
                o.op = sim::SoldierCommand::Op::Stop;
                out.emplace_back(o);
                break;
            case kPatrol:
                if (s->patrol.active) {
                    o.op = sim::SoldierCommand::Op::Stop;
                    out.emplace_back(o);
                } else {
                    m_picking = !m_picking;
                }
                break;
            case kDisband:
                o.op = sim::SoldierCommand::Op::Disband;
                out.emplace_back(o);
                break;
            default:
                break;
        }
    }

    [[nodiscard]] bool picking() const override { return m_picking; }
    void picked(std::uint32_t x, std::uint32_t y, const Selection&, const PanelContext& c,
                std::vector<sim::Command>& out) override {
        m_picking = false;
        const sim::Soldier* s = the_soldier(c);
        if (s == nullptr || s->owner != c.local) {
            return;
        }
        // Two waypoints, where it stands and the click, and back and forth (0x00440BD0).
        sim::SoldierCommand o;
        o.player = c.local;
        o.soldier = s->id;
        o.op = sim::SoldierCommand::Op::SetWaypoint;
        o.index = 0;
        o.point = sim::Target::spot(static_cast<std::uint32_t>(s->cell_x()), static_cast<std::uint32_t>(s->cell_y()));
        out.emplace_back(o);
        o.index = 1;
        o.point = sim::Target::tile_spot(x, y);
        out.emplace_back(o);
        o.op = sim::SoldierCommand::Op::Patrol;
        o.on = true;
        out.emplace_back(o);
    }
    bool back() override { return std::exchange(m_picking, false); }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection&,
                                                  const PanelContext& c) const override {
        const sim::Soldier* s = the_soldier(c);
        if (s == nullptr) {
            return std::nullopt;
        }
        switch (id) {
            case kStop:
                return help_line(c, 135);
            case kPatrol:
                return help_line(c, s->patrol.active ? 134 : 133);
            case kDisband:
                return help_line(c, 239);
            default:
                return std::nullopt;
        }
    }

private:
    static constexpr std::int32_t kName = 65002;
    static constexpr std::int32_t kStop = 65021;
    static constexpr std::int32_t kPatrol = 65022;
    static constexpr std::int32_t kDisband = 65023;
    static constexpr std::int32_t kCost = 65024;
    static constexpr std::int32_t kCoin = 65025;
    bool m_picking = false;
};

/// MKSHIP and MKSOLDAT: formations, scatter, stop all; the fleet's white flag; the land units'
/// filters by kind and by wounds (Ctrl: remove instead of keep only).
class GroupPanel final : public SimplePanel {
public:
    GroupPanel(assets::GadgetLayout layout, bool ships) : SimplePanel(std::move(layout)), m_ships(ships) {
        m_base = ships ? 50000 : 52000;
    }

    void refresh(const Selection&, const PanelContext& c) override {
        if (c.units == nullptr) {
            return;
        }
        m_panel.set_visible(m_base + 21, c.units->any_moving(c.world()));
        if (m_ships) {
            m_panel.set_selected(50022, c.units->any_white_flag(c.world()));
            return;
        }
        const std::uint32_t mask = c.units->kind_mask(c.world(), c.figures);
        for (std::int32_t k = 0; k < 4; ++k) {
            m_panel.set_visible(52031 + k, (mask & (1U << static_cast<std::uint32_t>(k + 1))) != 0);
        }
    }

    void clicked(const ui::PanelEvent& e, const Selection&, const PanelContext& c,
                 std::vector<sim::Command>& out) override {
        if (c.units == nullptr || e.kind != ui::PanelEvent::Kind::Clicked || c.units->owner(c.world()) != c.local) {
            return;
        }
        ui::UnitSelection& units = *c.units;
        const auto form_up = [&] {
            // Around where the group stands (0x0044C6D0).
            if (const auto centre = units.centre(c.world())) {
                for (sim::Command& o : units.move_orders(c.world(), c.local, centre->first, centre->second, false)) {
                    out.push_back(std::move(o));
                }
            }
        };
        const std::int32_t id = e.id - m_base;
        if (id >= 11 && id <= 13) {
            units.formation(c.world(), c.figures, static_cast<std::size_t>(id - 11));
            form_up();
        } else if (id == 14) {
            units.scatter(c.world());
            form_up();
        } else if (id == 21) {
            for (sim::Command& o : units.stop_orders(c.world(), c.local)) {
                out.push_back(std::move(o));
            }
        } else if (m_ships && e.id == 50022) {
            const bool on = !units.any_white_flag(c.world());
            for (const ui::UnitRef& r : units.units()) {
                sim::ShipCommand o;
                o.player = c.local;
                o.ship = r.id;
                o.op = sim::ShipCommand::Op::WhiteFlag;
                o.on = on;
                out.emplace_back(o);
            }
        } else if (!m_ships && id >= 31 && id <= 34) {
            // Keep only this kind, with Ctrl remove it (0x0044C3D0).
            units.filter_kinds(c.world(), c.figures, 1U << static_cast<std::uint32_t>(id - 30), !c.ctrl);
        } else if (!m_ships && id == 35) {
            units.filter_wounded(c.world(), c.figures, !c.ctrl);
        }
    }

    [[nodiscard]] std::optional<std::string> help(std::int32_t id, const Selection&,
                                                  const PanelContext& c) const override {
        const std::int32_t k = id - m_base;
        if (k >= 11 && k <= 14) {
            return help_line(c, static_cast<std::size_t>((m_ships ? 129 : 137) + k - 11));
        }
        if (k == 21) {
            return help_line(c, m_ships ? 128 : 136);
        }
        if (m_ships && id == 50022) {
            return help_line(c, c.units != nullptr && c.units->any_white_flag(c.world()) ? 114 : 115);
        }
        if (!m_ships && k >= 31 && k <= 35) {
            return help_line(c, static_cast<std::size_t>(141 + k - 31));
        }
        return std::nullopt;
    }

private:
    bool m_ships = true;
    std::int32_t m_base = 50000;
};

}  // namespace

std::unique_ptr<SelectionPanel> make_ship_unit_panel(assets::GadgetLayout layout) {
    return std::make_unique<ShipUnitPanel>(std::move(layout));
}
std::unique_ptr<SelectionPanel> make_soldier_unit_panel(assets::GadgetLayout layout) {
    return std::make_unique<SoldierUnitPanel>(std::move(layout));
}
std::unique_ptr<SelectionPanel> make_group_panel(assets::GadgetLayout layout, bool ships) {
    return std::make_unique<GroupPanel>(std::move(layout), ships);
}

}  // namespace o1602::app
