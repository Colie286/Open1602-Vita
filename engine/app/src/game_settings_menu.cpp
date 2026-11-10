// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_settings_menu.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <string_view>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

constexpr Id kItem = 1;  // + SetupItem
constexpr Id kOriginal = 50;
constexpr Id kDefaults = 51;
constexpr Id kBack = 52;
constexpr Id kStart = 53;
constexpr Id kVictory = 54;       // the page of the victory conditions
constexpr Id kVictoryOff = 55;    // all of them off
constexpr Id kBugs = 56;          // the page of the original's bugs
constexpr Id kBugsOriginal = 57;  // all of them as in the original
constexpr Id kBugsFixed = 58;     // all of them fixed

// The frame and the bottom row, as the multiplayer menu's.
constexpr Rect kScreen{16, 16, 992, 736};
constexpr Rect kHeading{40, 28, 760, 40};
constexpr std::int32_t kBottomY = 690;
constexpr std::int32_t kHintY = 592;  ///< the hint's top when the groups end above it

// The endless game's left column (four groups) and right column (two long ones, sixteen rows) end
// where the hint still has two lines above the buttons.
constexpr std::int32_t kTop = 76;
constexpr std::int32_t kGap = 6;
constexpr std::int32_t kRowH = 28;     // room for two lines of hint or error below the endless game's items (FB-32)
constexpr std::int32_t kGroupW = 464;  ///< a column: its frame
constexpr std::int32_t kRowW = kGroupW - 28;  ///< a row in it: label, a gap of 4 and value
constexpr std::int32_t kTextPadding = 8;      ///< ui::Form's text from the edges of its box
constexpr std::int32_t kMinLabelW = 150;
constexpr std::int32_t kMinValueW = 170;

struct Group {
    std::string_view heading;
    SetupItem first;
    SetupItem last;  ///< inclusive
    std::int32_t x;  ///< the column; the groups of a column follow each other from the top
};

constexpr std::int32_t kLeft = 40;
constexpr std::int32_t kRight = kLeft + kGroupW + 16;

// Economy, limits and disasters on the left, the world and the opponents on the right.
constexpr Group kGroups[] = {
    {"setup.group.economy", SetupItem::Refund, SetupItem::BreadChain, kLeft},
    {"setup.group.limits", SetupItem::Ships, SetupItem::GameSpeed, kLeft},
    {"setup.group.disasters", SetupItem::Droughts, SetupItem::Volcanoes, kLeft},
    {"setup.group.world", SetupItem::Map, SetupItem::Resources, kRight},
    {"setup.group.opponents", SetupItem::ComputerPlayers, SetupItem::Reveal, kRight},
};

// The victory conditions, after the endless games of Anno 1503, 1701 and 1404: population and
// wealth on the left, power and the end of the game on the right.
constexpr Group kVictoryGroups[] = {
    {"setup.group.population", SetupItem::WinInhabitants, SetupItem::WinClassInhabitants, kLeft},
    {"setup.group.wealth", SetupItem::WinMoney, SetupItem::WinBalance, kLeft},
    {"setup.group.power", SetupItem::WinIslands, SetupItem::WinMonument, kRight},
    {"setup.group.end", SetupItem::WinTimeLimit, SetupItem::WinComputers, kRight},
};

// The bugs of the original a game can bring back, by what they touch; the computer players
// (ClassicAi) have their row with the opponents on the first page, the page's presets switch them too.
constexpr Group kBugGroups[] = {
    {"setup.group.disasters", SetupItem::BugFireRisk, SetupItem::BugRobberFlight, kLeft},
    {"setup.group.construction", SetupItem::BugGoldsheep, SetupItem::BugInfertileFields, kLeft},
    {"setup.group.ships", SetupItem::BugRemoteTrade, SetupItem::BugSmallYardWarships, kRight},
    {"setup.group.opponents", SetupItem::BugNativeLand, SetupItem::BugNativeLand, kRight},
};

std::string_view title_key(SetupScope scope) {
    switch (scope) {
        case SetupScope::MultiplayerEndless:
            return "setup.title.multiplayer";
        case SetupScope::Scenario:
            return "setup.title.scenario";
        case SetupScope::Savegame:
            return "setup.title.savegame";
        case SetupScope::Endless:
            break;
    }
    return "setup.title";
}

std::string_view hint_key(SetupScope scope) {
    switch (scope) {
        case SetupScope::MultiplayerEndless:
            return "setup.hint.multiplayer";
        case SetupScope::Scenario:
            return "setup.hint.scenario";
        case SetupScope::Savegame:
            return "setup.hint.savegame";
        case SetupScope::Endless:
            break;
    }
    return "setup.hint";
}

}  // namespace

bool GameSettingsMenu::back() noexcept {
    if (m_page != Page::Settings) {
        m_page = Page::Settings;
        return true;
    }
    return false;
}

std::vector<ui::DrawItem> GameSettingsMenu::draw(const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    m_label_font = m_font >= 0 && m_font < static_cast<std::int32_t>(fonts.size())
                       ? fonts[static_cast<std::size_t>(m_font)]
                       : nullptr;
    layout();
    return m_form.draw(fonts);
}

GameSettingsMenu::Result GameSettingsMenu::mouse(std::int32_t x, std::int32_t y, bool down, ui::MouseButton button) {
    layout();
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        return Result::None;
    }
    const bool editable = m_mode != Mode::View;
    if (*id >= kItem && *id < kItem + static_cast<Id>(kSetupItems)) {
        const auto item = static_cast<SetupItem>(*id - kItem);
        if (editable && setup_item_open(m_setup, m_scope, item)) {
            if (m_form.clicked_with() == ui::MouseButton::Right) {
                choose_previous(m_setup, item);
            } else {
                choose_next(m_setup, item);
            }
        }
    } else if (*id == kVictory) {
        m_page = Page::Victory;
    } else if (*id == kVictoryOff && editable) {
        m_setup.rules.victory = {};
    } else if (*id == kBugs) {
        m_page = Page::Bugs;
    } else if (*id == kBugsOriginal && editable) {
        m_setup.rules.bugs = sim::OriginalBugs::all();
    } else if (*id == kBugsFixed && editable) {
        m_setup.rules.bugs = {};
    } else if (*id == kOriginal && editable) {
        m_setup = keep_decided(EndlessSetup::original(m_scope));
    } else if (*id == kDefaults && editable) {
        m_setup = keep_decided(EndlessSetup::defaults(m_scope));
    } else if (*id == kBack) {
        return back() ? Result::None : Result::Back;
    } else if (*id == kStart && editable) {
        m_error.clear();
        return Result::Start;
    }
    return Result::None;
}

EndlessSetup GameSettingsMenu::keep_decided(EndlessSetup preset) const {
    // The presets are rules; the victory conditions, the game's goal, stay as chosen.
    preset.rules.victory = m_setup.rules.victory;
    if (m_scope == SetupScope::MultiplayerEndless) {
        // The players are the lobby's, whatever the preset says.
        preset = multiplayer_setup(preset, m_setup.world.players);
    }
    return preset;
}

void GameSettingsMenu::layout() {
    ui::Form& f = m_form;
    f.clear();
    f.panel(kScreen);
    const bool victory = m_page == Page::Victory;
    const bool bugs = m_page == Page::Bugs;
    f.label(kHeading,
            ui::tr(victory ? "setup.title.victory"
                   : bugs  ? "setup.title.bugs"
                           : title_key(m_scope)),
            Align::Left, Tone::Heading);
    const bool editable = m_mode != Mode::View;
    std::int32_t left_y = kTop;
    std::int32_t right_y = kTop;
    const std::span<const Group> groups = victory ? std::span<const Group>(kVictoryGroups)
                                          : bugs  ? std::span<const Group>(kBugGroups)
                                                  : std::span<const Group>(kGroups);
    const auto offered = [&](const Group& g) {
        std::vector<SetupItem> items;
        for (auto i = static_cast<std::int32_t>(g.first); i <= static_cast<std::int32_t>(g.last); ++i) {
            if (setup_item_offered(m_scope, static_cast<SetupItem>(i))) {
                items.push_back(static_cast<SetupItem>(i));
            }
        }
        return items;
    };
    // The labels' column as wide as the longest label of the page (all languages and packages),
    // the values the rest; without a font as wide as nine units a byte, as ui::Form counts then.
    const bool victory_row = m_page == Page::Settings && victory_offered(m_scope);
    double longest = 0.0;
    const auto measure = [&](const std::string& label) {
        const double w = m_label_font != nullptr ? m_label_font->width(label) / m_label_font->design_scale()
                                                 : 9.0 * static_cast<double>(label.size());
        longest = std::max(longest, w);
    };
    for (const Group& g : groups) {
        for (const SetupItem item : offered(g)) {
            measure(setup_label(item));
        }
    }
    if (m_page == Page::Settings) {
        measure(ui::tr("setup.bugs"));
        if (victory_row) {
            measure(ui::tr("setup.victory"));
        }
    }
    const std::int32_t label_w = std::clamp(static_cast<std::int32_t>(std::ceil(longest)) + 2 * kTextPadding,
                                            kMinLabelW, kRowW - 4 - kMinValueW);
    const std::int32_t value_w = kRowW - 4 - label_w;
    // A group's frame and heading; the top of its first row.
    const auto group = [&](std::string_view heading, std::int32_t rows, std::int32_t x) {
        std::int32_t& y0 = x < kScreen.x + kScreen.w / 2 ? left_y : right_y;
        f.panel(Rect{x, y0, kGroupW, 44 + rows * kRowH});
        f.label(Rect{x + 12, y0 + 6, kRowW, 32}, ui::tr(heading), Align::Left, Tone::Heading);
        const std::int32_t first = y0 + 40;
        y0 += 44 + rows * kRowH + kGap;
        return first;
    };
    // A row's label; the place of its value.
    const auto row = [&](std::int32_t x, std::int32_t y, const std::string& label, bool used) {
        f.label(Rect{x + 12, y, label_w, kRowH - 4}, label, Align::Left, used ? Tone::Normal : Tone::Dim);
        return Rect{x + 16 + label_w, y, value_w, kRowH - 6};
    };
    for (const Group& g : groups) {
        const std::vector<SetupItem> items = offered(g);
        if (items.empty()) {
            continue;
        }
        const std::int32_t top = group(g.heading, static_cast<std::int32_t>(items.size()), g.x);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const SetupItem item = items[i];
            const bool used = setup_item_used(m_setup, item);
            const std::int32_t y = top + static_cast<std::int32_t>(i) * kRowH;
            if (editable) {
                f.cycle_button(kItem + static_cast<Id>(item), row(g.x, y, setup_label(item), used),
                               setup_value(m_setup, item), setup_item_open(m_setup, m_scope, item));
            } else {
                // Only shown: the value may take the room the label leaves, not just a button's.
                const Tone tone = used ? Tone::Normal : Tone::Dim;
                f.entry(Rect{g.x + 12, y, kRowW, kRowH - 4}, setup_label(item), setup_value(m_setup, item), tone, tone);
            }
        }
    }
    if (m_page == Page::Settings) {
        // Below the disasters: the ways to the pages of the victory conditions and of the original's
        // bugs, each with what is chosen (to look at too, for a game that is only shown).
        std::int32_t top = group("setup.group.more", victory_row ? 2 : 1, kLeft);
        if (victory_row) {
            const Rect value = row(kLeft, top, ui::tr("setup.victory"), m_setup.rules.victory.active());
            f.button(kVictory, value, ui::tr("setup.victory.open", {victory_summary(m_setup.rules.victory)}));
            top += kRowH;
        }
        const Rect value = row(kLeft, top, ui::tr("setup.bugs"), page_bugs(m_setup.rules.bugs).count() != 0);
        f.button(kBugs, value, ui::tr("setup.bugs.open", {bugs_summary(m_setup.rules.bugs)}));
    }
    // The hint below the lower column; three lines of it fit above the buttons.
    const std::int32_t hint_y = std::max({kHintY, left_y, right_y});
    if (!m_error.empty()) {
        f.paragraph(Rect{40, hint_y, 944, kBottomY - 4 - hint_y}, m_error, Tone::Warning);
    } else {
        f.paragraph(Rect{40, hint_y, 944, kBottomY - 4 - hint_y},
                    ui::tr(victory ? (editable ? "setup.hint.victory" : "setup.hint.victory.view")
                           : bugs  ? (editable ? "setup.hint.bugs" : "setup.hint.bugs.view")
                                   : hint_key(m_scope)),
                    Tone::Dim);
    }
    f.button(kBack, Rect{40, kBottomY, 180, 44}, ui::tr("setup.back"));
    if (!editable) {
        return;
    }
    if (victory) {
        f.button(kVictoryOff, Rect{520, kBottomY, 240, 44}, ui::tr("setup.victory.off"), true,
                 !m_setup.rules.victory.active());
    } else if (bugs) {
        f.button(kBugsOriginal, Rect{260, kBottomY, 240, 44}, ui::tr("setup.bugs.all_original"), true,
                 page_bugs(m_setup.rules.bugs) == page_bugs(sim::OriginalBugs::all()));
        f.button(kBugsFixed, Rect{520, kBottomY, 240, 44}, ui::tr("setup.bugs.all_fixed"), true,
                 page_bugs(m_setup.rules.bugs) == sim::OriginalBugs{});
    } else {
        f.button(kOriginal, Rect{260, kBottomY, 240, 44}, ui::tr("setup.original"), true,
                 same_setup(m_setup, keep_decided(EndlessSetup::original(m_scope)), m_scope));
        f.button(kDefaults, Rect{520, kBottomY, 240, 44}, ui::tr("setup.defaults"), true,
                 same_setup(m_setup, keep_decided(EndlessSetup::defaults(m_scope)), m_scope));
    }
    f.button(kStart, Rect{804, kBottomY, 180, 44}, ui::tr(m_mode == Mode::Apply ? "setup.apply" : "setup.start"), true,
             true);
}

}  // namespace o1602::app
