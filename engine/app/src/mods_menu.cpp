// SPDX-License-Identifier: GPL-3.0-or-later
#include "mods_menu.hpp"

#include <algorithm>
#include <string>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using State = ModsModel::Entry::State;
using ui::Align;
using ui::Rect;

constexpr Id kRow = 100;    // + row on the screen
constexpr Id kCheck = 200;  // + row on the screen
constexpr Id kUp = 1;
constexpr Id kDown = 2;
constexpr Id kAdopt = 3;
constexpr Id kBack = 4;

constexpr std::int32_t kLeft = 64;
constexpr std::int32_t kWidth = 896;
constexpr std::int32_t kListY = 186;
constexpr std::int32_t kRowH = 34;
constexpr std::int32_t kNameX = kLeft + 44;
constexpr std::int32_t kVersionX = kLeft + 500;
constexpr std::int32_t kStateX = kLeft + 640;

}  // namespace

std::string ModsMenu::state_text(State state) {
    switch (state) {
        case State::On:
            return ui::tr("mods.state_on");
        case State::Off:
            return ui::tr("mods.state_off");
        case State::Broken:
            return ui::tr("mods.state_broken");
        case State::Received:
            return ui::tr("mods.state_received");
    }
    return {};
}

std::vector<ui::DrawItem> ModsMenu::draw(const ModsModel& model,
                                         const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

std::optional<ModsMenu::Event> ModsMenu::mouse(const ModsModel& model, std::int32_t x, std::int32_t y, bool down,
                                               ui::MouseButton button) {
    layout(model);
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        return std::nullopt;
    }
    const std::size_t count = model.entries.size();
    if (*id == kBack) {
        return Event{Event::Kind::Close, 0};
    }
    if (*id == kUp) {
        m_first -= std::min(m_first, kRows);
    } else if (*id == kDown) {
        m_first = std::min(m_first + kRows, count > kRows ? count - kRows : 0);
    } else if (*id == kAdopt && m_selected && *m_selected < count) {
        return Event{Event::Kind::Adopt, *m_selected};
    } else if (*id >= kCheck && *id < kCheck + static_cast<Id>(kRows)) {
        const std::size_t index = m_first + static_cast<std::size_t>(*id - kCheck);
        if (index < count) {
            m_selected = index;
            return Event{Event::Kind::Toggle, index};
        }
    } else if (*id >= kRow && *id < kRow + static_cast<Id>(kRows)) {
        const std::size_t index = m_first + static_cast<std::size_t>(*id - kRow);
        if (index < count) {
            m_selected = index;
        }
    }
    return std::nullopt;
}

void ModsMenu::layout(const ModsModel& model) {
    const std::size_t count = model.entries.size();
    m_first = std::min(m_first, count > kRows ? count - kRows : 0);
    if (m_selected && *m_selected >= count) {
        m_selected.reset();
    }
    ui::Form& f = m_form;
    f.clear();
    f.panel(Rect{40, 40, 944, 688});
    f.label(Rect{kLeft, 54, kWidth, 40}, ui::tr("mods.title"), Align::Left, Tone::Heading);
    f.paragraph(Rect{kLeft, 98, kWidth, 48}, ui::tr("mods.hint"), Tone::Dim);
    f.label(Rect{kNameX, 150, 380, 30}, ui::tr("mods.col_name"), Align::Left, Tone::Heading);
    f.label(Rect{kVersionX, 150, 130, 30}, ui::tr("mods.col_version"), Align::Left, Tone::Heading);
    f.label(Rect{kStateX, 150, 250, 30}, ui::tr("mods.col_state"), Align::Left, Tone::Heading);
    if (count == 0) {
        f.paragraph(Rect{kNameX, kListY + 6, kWidth - 44, 66}, ui::tr("mods.none"), Tone::Dim);
    }
    for (std::size_t r = 0; r < kRows && m_first + r < count; ++r) {
        const std::size_t i = m_first + r;
        const ModsModel::Entry& e = model.entries[i];
        const std::int32_t y = kListY + static_cast<std::int32_t>(r) * kRowH;
        const auto k = static_cast<Id>(r);
        f.row(kRow + k, Rect{kLeft, y, kWidth, kRowH - 2}, m_selected == i);
        // Received packages are not turned on or off: the games that need them use them.
        if (e.state != State::Received) {
            f.check(kCheck + k, Rect{kLeft + 6, y + 2, 30, kRowH - 6}, {}, e.state == State::On);
        }
        const Tone tone = e.state == State::On ? Tone::Normal : e.state == State::Broken ? Tone::Warning : Tone::Dim;
        f.label(Rect{kNameX, y, kVersionX - kNameX - 12, kRowH - 2}, e.name, Align::Left, tone);
        f.label(Rect{kVersionX, y, kStateX - kVersionX - 12, kRowH - 2}, e.version, Align::Left, tone);
        f.label(Rect{kStateX, y, kLeft + kWidth - kStateX - 8, kRowH - 2}, state_text(e.state), Align::Left, tone);
    }
    const std::int32_t below = kListY + static_cast<std::int32_t>(kRows) * kRowH;
    if (count > kRows) {
        f.button(kUp, Rect{kLeft + kWidth - 92, below + 4, 44, 32}, "^", m_first > 0);
        f.button(kDown, Rect{kLeft + kWidth - 44, below + 4, 44, 32}, "v", m_first + kRows < count);
    }
    const ModsModel::Entry* chosen = m_selected ? &model.entries[*m_selected] : nullptr;
    if (chosen != nullptr && !chosen->description.empty()) {
        f.paragraph(Rect{kLeft, below + 42, kWidth, 88}, chosen->description);
    }
    if (!model.notice.empty()) {
        f.paragraph(Rect{kLeft, 610, kWidth, 44}, model.notice, Tone::Warning);
    }
    const bool adoptable = chosen != nullptr && chosen->state == State::Received && chosen->adoptable;
    f.button(kAdopt, Rect{kLeft, 664, 300, 44}, ui::tr("mods.adopt"), adoptable);
    f.button(kBack, Rect{kLeft + kWidth - 200, 664, 200, 44}, ui::tr("mods.back"));
}

}  // namespace o1602::app
