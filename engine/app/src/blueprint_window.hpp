// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window of a blueprint (Open1602, as in later Anno games): a click on one of the player's own
// blueprints opens it over the map. It shows what the blueprint and all blueprints of its city
// cost, builds the one or all of them, lets the city build them by itself as soon as it can pay
// (sim::BlueprintOrder::Kind::Auto) and removes the blueprint. The window only shows a
// BlueprintModel and tells what was clicked; the viewer turns that into commands.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "o1602/sim/construction.hpp"
#include "o1602/ui/form.hpp"

namespace o1602::app {

/// What the window shows; the viewer fills it every frame from the blueprint it was opened on.
struct BlueprintModel {
    bool valid = false;  ///< the blueprint is still there (else the window closes)
    std::string name;    ///< of the building
    std::string city;    ///< of the city it lies in
    sim::BuildBill cost;
    std::size_t city_count = 0;  ///< blueprints of the player in that city, this one included
    sim::BuildBill city_cost;
    bool automatic = false;  ///< the city builds them by itself
};

class BlueprintWindow {
public:
    enum class Result : std::uint8_t { None, Close, Build, BuildCity, Automatic, Remove };
    static constexpr std::int32_t kWidth = 420;
    static constexpr std::int32_t kHeight = 386;

    explicit BlueprintWindow(ui::FormStyle style = {}) : m_form(style) {}

    void set_style(const ui::FormStyle& style) { m_form.set_style(style); }
    void set_scale(double scale) { m_form.set_scale(scale); }
    [[nodiscard]] double scale() const noexcept { return m_form.scale(); }

    /// Everything to draw, relative to the window's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const BlueprintModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    /// The mouse in pixels relative to the window's top left.
    Result mouse(std::int32_t x, std::int32_t y, bool down, const BlueprintModel& model);

    /// What a bill reads as, the goods left out that it does not need: "120 Gold, 2 t Holz".
    [[nodiscard]] static std::string bill_text(const sim::BuildBill& bill);

private:
    void layout(const BlueprintModel& model);

    ui::Form m_form;
};

}  // namespace o1602::app
