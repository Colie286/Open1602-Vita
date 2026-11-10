// SPDX-License-Identifier: GPL-3.0-or-later
//
// The windows of what the player clicked on the map (docs/re/ui.md#auswahlfenster): production
// buildings, public buildings, houses, the own Kontor, shipyards and castles, ships. Each window
// reads the game every frame and turns clicks into commands; it changes nothing itself.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "name_edit.hpp"
#include "selection.hpp"
#include "tutorial.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/ui/panel.hpp"
#include "o1602/ui/unit_selection.hpp"

namespace o1602::app {

/// What the windows read.
struct PanelContext {
    const sim::Simulation& simulation;
    const assets::BuildingTable& buildings;
    const assets::FigureTable& figures;
    const assets::TextTable* texts;
    sim::PlayerId local = sim::kNoPlayer;
    /// The selected ships and land units (the military mode's windows change it: filters,
    /// formations).
    ui::UnitSelection* units = nullptr;
    /// How players are shown (coats of arms in their colour); without it the white ones.
    const render::BannerTable* banners = nullptr;
    /// The name being typed (a click on the name of an own ship or city starts it); without it the
    /// windows only show names.
    NameEdit* names = nullptr;
    /// What a window shows as the name of `target`: the world's, or the one being typed.
    [[nodiscard]] std::string shown_name(const NameTarget& target, std::string_view world_name) const {
        return names != nullptr ? names->shown(target, world_name) : std::string(world_name);
    }
    /// A click on the name of `target`: typing it starts if the local player may rename it.
    void start_rename(const NameTarget& target, std::vector<sim::Command>& out) const {
        if (names != nullptr) {
            if (auto kept = names->start(world(), local, target)) {
                out.push_back(std::move(*kept));
            }
        }
    }
    /// The goods trend under the stock of a Kontor (DisplaySettings::goods_trend).
    bool goods_trend = false;
    /// Ctrl is held (the unit windows' filters then remove a kind instead of keeping it).
    bool ctrl = false;
    /// Shift is held (a click on the cannons in a ship's hold mounts as many as fit).
    bool shift = false;
    /// Real time in milliseconds, for what blinks (the diplomacy window's open offers).
    std::uint64_t now_ms = 0;
    [[nodiscard]] const sim::World& world() const noexcept { return simulation.world(); }
    /// The banner of a player, nothing if there is no such player or table.
    [[nodiscard]] const render::Banner* banner(sim::PlayerId p) const {
        const sim::Player* player = world().player(p);
        return banners != nullptr && player != nullptr ? banners->get(player->banner) : nullptr;
    }
    /// A line of a Text.cod section, empty if there is none.
    [[nodiscard]] std::string text(std::string_view section, std::size_t line) const;
    /// The name of class `resident_class` of inhabitants: the one a package gave it, else Text.cod's
    /// ([GAME] 3 and on, for the original's five).
    [[nodiscard]] std::string class_name(std::size_t resident_class) const;
};

/// One window: one or more layouts (sub-pages) and what fills them.
class SelectionPanel {
public:
    SelectionPanel() = default;
    virtual ~SelectionPanel() = default;
    SelectionPanel(const SelectionPanel&) = delete;
    SelectionPanel& operator=(const SelectionPanel&) = delete;

    virtual void set_scale(double scale) = 0;
    /// The layout shown now.
    virtual ui::Panel& panel() = 0;
    /// Fills the gadgets from the game for this frame.
    virtual void refresh(const Selection& selection, const PanelContext& context) = 0;
    /// A press, click or slide; commands go to `out`.
    virtual void clicked(const ui::PanelEvent& event, const Selection& selection, const PanelContext& context,
                         std::vector<sim::Command>& out) = 0;
    /// Help text of the gadget under the mouse.
    [[nodiscard]] virtual std::optional<std::string> help(std::int32_t id, const Selection& selection,
                                                          const PanelContext& context) const = 0;
    /// Whether the window waits for a click on the map (a unit's destination, a route stop).
    [[nodiscard]] virtual bool picking() const { return false; }
    /// The click on the map while picking, world tile.
    virtual void picked(std::uint32_t /*x*/, std::uint32_t /*y*/, const Selection& /*selection*/,
                        const PanelContext& /*context*/, std::vector<sim::Command>& /*out*/) {}
    /// Escape: back one page or out of picking; false if there is nothing to go back from.
    virtual bool back() { return false; }
    /// Whether a right click goes back a page (as back()) instead of out to the player status: on
    /// pages that lead somewhere, such as a ship's trade route editor (Open1602).
    [[nodiscard]] virtual bool steps_back() const { return false; }
    /// The window was opened anew (lists start at their first page).
    virtual void opened() {}
    /// The mouse wheel over the window (`notches` up positive); false if the window does not scroll.
    virtual bool wheel(std::int32_t /*notches*/) { return false; }
    /// What the window asks of the interface beyond commands (once).
    struct Request {
        /// FoundKontor: the building tool places a Kontor; SelectShip: select `ship` and look at it;
        /// Centre: look at world tile (x, y); Goods: the page of `ware` in the window "Warenbilanz",
        /// for the cities on `island`.
        enum class Kind : std::uint8_t { FoundKontor, SelectShip, Centre, Goods };
        Kind kind = Kind::FoundKontor;
        std::uint32_t ship = 0;  ///< FoundKontor: the ship whose cargo pays
        std::uint8_t ware = 0;
        std::uint16_t island = 0;
        double x = 0.0;
        double y = 0.0;
    };
    [[nodiscard]] virtual std::optional<Request> take_request() { return std::nullopt; }
    /// What the tutorial reads of the window (the ship window's page and the ware it shows).
    virtual void describe(TutorialView& /*view*/) const {}
};

/// A window of one layout.
class SimplePanel : public SelectionPanel {
public:
    explicit SimplePanel(assets::GadgetLayout layout) : m_panel(std::move(layout), 1.0) {}
    void set_scale(double scale) override { m_panel.set_scale(scale); }
    ui::Panel& panel() override { return m_panel; }

protected:
    ui::Panel m_panel;
};

/// All windows of the selections, loaded once.
class SelectionPanels {
public:
    SelectionPanels(const assets::GameDirectory& game);

    /// The window for a selection, nothing if it has none (the info mode then shows the player
    /// status).
    [[nodiscard]] SelectionPanel* panel_for(const Selection& selection, const PanelContext& context);
    /// The window of the military mode for the selected units (Panel_UpdateSelection 0x0043AA00):
    /// one ship, one land unit, a group of ships or of land units; nothing without a selection.
    [[nodiscard]] SelectionPanel* military_panel_for(const PanelContext& context);
    void set_scale(double scale);

private:
    std::map<std::string, std::unique_ptr<SelectionPanel>> m_panels;
};

}  // namespace o1602::app
