// SPDX-License-Identifier: GPL-3.0-or-later
//
// The panels below the head of the side panel: the build menu (BAU.GAD and
// its pop-up of building icons) and the player status of the info mode (INFO.GAD). They only
// show and ask; what a click means for the game goes back to the caller as an action. Like the
// selection windows they fill their panels, which the side panel draws; no SDL here.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "selection_panels.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/gad.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/ui/build_menu.hpp"
#include "o1602/ui/panel.hpp"

namespace o1602::app {

/// The build menu.
class BuildPanel {
public:
    /// What a click asks for.
    struct Action {
        enum class Kind : std::uint8_t { Pick, Demolish, Rotate, OrientToRoad, Copy, Blueprint };
        Kind kind = Kind::Pick;
        std::uint32_t building = 0;  ///< Pick: HAUS Id
        int step = 0;                ///< Rotate: -1 left, +1 right; OrientToRoad: 1 on, 0 off
    };
    /// Whether the local player may pick a building (sim::building_available).
    using Available = std::function<bool(std::uint32_t id)>;
    /// Size of sprite `gfx` of sheet `block` at the art's own scale (render::SdlUiRenderer::sprite_size).
    using SpriteSize =
        std::function<std::optional<std::pair<std::int32_t, std::int32_t>>(std::int32_t block, std::int32_t gfx)>;

    BuildPanel(assets::GadgetLayout bau, const ui::BuildMenuDefinition& menu, const assets::TextTable* texts);

    void set_scale(double scale);
    /// The mouse relative to the side panel. `consumed` tells whether the panel took it (an open
    /// pop-up takes every press on the panel).
    std::optional<Action> mouse(std::int32_t x, std::int32_t y, bool down, const Available& available, bool& consumed);
    [[nodiscard]] bool popup_open() const noexcept { return m_popup.has_value(); }
    /// The category whose pop-up is open (its button), 0 if none.
    [[nodiscard]] std::int32_t popup_category() const noexcept { return m_popup ? m_popup_category : 0; }
    void close_popup() noexcept { m_popup.reset(); }

    /// The building of the open category (nothing: the tools, or nothing available there). Picks
    /// another available one if the remembered one is not (the original keeps it, S-014).
    [[nodiscard]] std::optional<std::uint32_t> current(const Available& available);
    /// The tools' category is open with the demolish tool picked.
    [[nodiscard]] bool demolish() const noexcept;
    /// The tools' category is open with the copy tool picked (Open1602).
    [[nodiscard]] bool copy() const noexcept;

    /// The tools under the demolish button (Open1602): its pop-up offers them instead of starting
    /// the demolish tool at once, as later Anno games do. Blueprint is a switch, not a tool: picked,
    /// it turns blueprint mode on or off (Action::Kind::Blueprint) and the menu goes back to the
    /// building (or the copy tool) in hand.
    enum class Tool : std::uint8_t { Demolish, Copy, Blueprint };
    /// Shows the tools' category with `tool` picked: a key started the tool (Delete, Ctrl+C).
    void show_tool(Tool tool);
    /// The tools' pop-up as a category of the menu: icons "open1602:<tool>", names in the
    /// language of the game (Windows-1252, as the original's; demolish by its own text).
    [[nodiscard]] ui::BuildCategory tools_category(std::int32_t button) const;
    /// Blueprint mode is on: the title says that what is placed becomes a blueprint.
    void set_blueprint(bool on) noexcept { m_blueprint = on; }
    /// Shows a building's category with it picked (a plantation's field).
    /// False if the menu has no such building.
    bool show(std::uint32_t id);

    /// Help text of the gadget under the mouse, nothing if none.
    [[nodiscard]] std::optional<std::string> help(const std::vector<std::string>& leiste) const;

    /// Title, costs, preview and production chain of the current building. `rotation`: view plus
    /// building rotation (the preview turns with both); `size` centres the preview in its box;
    /// `goods`: the game's, whose icons the production chain shows.
    void refresh(const assets::BuildingTable& buildings, const Available& available, std::uint8_t rotation,
                 const SpriteSize& size, const assets::Goods& goods);
    /// The panel, and the pop-up over it if one is open: drawn in this order.
    [[nodiscard]] const ui::Panel& panel() const noexcept { return m_bau; }
    [[nodiscard]] const ui::Panel* popup() const noexcept { return m_popup ? &*m_popup : nullptr; }

private:
    void open_popup(const ui::BuildCategory& category, const Available& available, std::int32_t x, std::int32_t y);
    [[nodiscard]] std::string name_of(const ui::BuildCategory& category, std::size_t index) const;

    ui::Panel m_bau;
    assets::GadgetLayout m_layout;  ///< the file as loaded (offsets before moving groups)
    const ui::BuildMenuDefinition* m_menu;
    const assets::TextTable* m_texts;
    std::int32_t m_gfx_bauhaus = 81;
    std::int32_t m_gfx_ware = 192;
    std::int32_t m_category = 0;
    std::int32_t m_building_category = 0;  ///< the last category of buildings shown
    bool m_blueprint = false;
    std::map<std::int32_t, std::size_t> m_picked;  ///< remembered position per category
    Tool m_tool = Tool::Demolish;                  ///< the tool picked last in the tools' pop-up
    ui::BuildCategory m_tools;                     ///< that pop-up's icons
    std::optional<ui::Panel> m_popup;
    std::int32_t m_popup_category = 0;
};

/// "Spielerstatus" of the info mode without a selection (INFO.GAD).
class InfoPanel {
public:
    InfoPanel(assets::GadgetLayout info, const assets::TextTable* texts);
    void set_scale(double scale) { m_panel.set_scale(scale); }
    /// The mouse; a click on a list button returns which list to open.
    std::optional<Selection::Kind> mouse(std::int32_t x, std::int32_t y, bool down);
    /// Whether "Bewertung" was clicked since the last call.
    [[nodiscard]] bool take_rating() noexcept { return std::exchange(m_rating_clicked, false); }
    /// Whether "Warenbilanz" (Open1602's own line) was clicked since the last call.
    [[nodiscard]] bool take_goods() noexcept { return std::exchange(m_goods_clicked, false); }
    /// "Punkte" (57027, the score's total) and whether "Bewertung" can be pressed.
    void set_points(std::int64_t points, bool rating);
    [[nodiscard]] std::optional<std::string> help(const std::vector<std::string>& leiste) const;
    /// The figures of the local player, its coat of arms in `banner`'s colour.
    void refresh(const sim::PlayerFigures& figures, const render::Banner* banner);
    [[nodiscard]] const ui::Panel& panel() const noexcept { return m_panel; }

private:
    ui::Panel m_panel;
    std::vector<std::string> m_game;  ///< Text.cod [GAME]
    std::int32_t m_arms = 0;          ///< sprite of the coat of arms of colour 0 (render::BannerArt)
    std::int32_t m_balance_icon = 0;  ///< sprite of the balance icon (green; red is the next)
    bool m_rating_clicked = false;
    bool m_goods_clicked = false;
};

}  // namespace o1602::app
