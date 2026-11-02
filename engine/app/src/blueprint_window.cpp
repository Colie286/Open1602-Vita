// SPDX-License-Identifier: GPL-3.0-or-later
#include "blueprint_window.hpp"

#include <algorithm>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

constexpr Id kClose = 1;
constexpr Id kBuild = 2;
constexpr Id kBuildCity = 3;
constexpr Id kAutomatic = 4;
constexpr Id kRemove = 5;

constexpr std::int32_t kW = BlueprintWindow::kWidth;
constexpr std::int32_t kH = BlueprintWindow::kHeight;
constexpr std::int32_t kInner = kW - 32;

}  // namespace

std::string BlueprintWindow::bill_text(const sim::BuildBill& bill) {
    // An amount and its unit stay on one line: no-break spaces between them.
    const auto unbroken = [](std::string part) {
        std::replace(part.begin(), part.end(), ' ', '\xA0');
        return part;
    };
    std::string text = unbroken(ui::tr("blueprint.cost.gold", {std::to_string(bill.money)}));
    const auto goods = [&](std::uint32_t amount, const char* key) {
        if (amount != 0) {
            // Whole tons, rounded up: what is needed, not less.
            text += ", " + unbroken(ui::tr(key, {std::to_string((amount + 31) / 32)}));
        }
    };
    goods(bill.tools, "blueprint.cost.tools");
    goods(bill.wood, "blueprint.cost.wood");
    goods(bill.bricks, "blueprint.cost.bricks");
    goods(bill.cannons, "blueprint.cost.cannons");
    return text;
}

void BlueprintWindow::layout(const BlueprintModel& model) {
    ui::Form& f = m_form;
    f.clear();
    f.panel(Rect{0, 0, kW, kH});
    f.label(Rect{16, 8, kInner - 140, 34}, ui::tr("blueprint.title"), Align::Left, Tone::Heading);
    f.button(kClose, Rect{kW - 146, 8, 130, 34}, ui::tr("blueprint.close"));
    f.label(Rect{16, 48, kInner, 28}, model.name);
    // Costs wrap: with many goods they grow long.
    f.paragraph(Rect{16, 78, kInner, 44}, ui::tr("blueprint.cost") + ": " + bill_text(model.cost), Tone::Dim);
    f.button(kBuild, Rect{16, 126, kInner, 34}, ui::tr("blueprint.build"));
    f.label(Rect{16, 172, kInner, 28}, ui::tr("blueprint.city", {std::to_string(model.city_count), model.city}));
    f.paragraph(Rect{16, 198, kInner, 44}, bill_text(model.city_cost), Tone::Dim);
    f.button(kBuildCity, Rect{16, 246, kInner, 34}, ui::tr("blueprint.build_city"));
    f.check(kAutomatic, Rect{16, 288, kInner, 34}, ui::tr("blueprint.automatic"), model.automatic);
    f.button(kRemove, Rect{16, kH - 50, kInner, 34}, ui::tr("blueprint.remove"));
}

std::vector<ui::DrawItem> BlueprintWindow::draw(const BlueprintModel& model,
                                                const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

BlueprintWindow::Result BlueprintWindow::mouse(std::int32_t x, std::int32_t y, bool down, const BlueprintModel& model) {
    layout(model);
    const auto id = m_form.mouse(x, y, down);
    if (!id) {
        return Result::None;
    }
    switch (*id) {
        case kClose:
            return Result::Close;
        case kBuild:
            return Result::Build;
        case kBuildCity:
            return Result::BuildCity;
        case kAutomatic:
            return Result::Automatic;
        case kRemove:
            return Result::Remove;
        default:
            return Result::None;
    }
}

}  // namespace o1602::app
