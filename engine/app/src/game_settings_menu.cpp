// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_settings_menu.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
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

constexpr Id kOriginal = 50;
constexpr Id kDefaults = 51;
constexpr Id kBack = 52;
constexpr Id kStart = 53;
constexpr Id kVictory = 54;            // the page of the victory conditions
constexpr Id kVictoryOff = 55;         // all of them off
constexpr Id kBugs = 56;               // the page of the original's bugs
constexpr Id kBugsOriginal = 57;       // all of them as in the original
constexpr Id kBugsFixed = 58;          // all of them fixed
constexpr Id kIslands = 59;            // the page of the islands
constexpr Id kReroll = 60;             // another world for the preview
constexpr Id kDisasters = 61;          // the page of the disasters
constexpr Id kDisastersOriginal = 62;  // all of them as in the original
constexpr Id kItem = 100;              // + SetupItem

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

// Economy, limits and nature on the left, the world and the opponents on the right; the disasters
// have a page of their own, its way below the nature.
constexpr Group kGroups[] = {
    {"setup.group.economy", SetupItem::Refund, SetupItem::TradeCarts, kLeft},
    {"setup.group.limits", SetupItem::Ships, SetupItem::GameSpeed, kLeft},
    {"setup.group.nature", SetupItem::FertileFields, SetupItem::FertileFields, kLeft},
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
    // Not a bug, but a trick of the original: its cheats (with the code, from the start, or never).
    {"setup.group.cheats", SetupItem::Cheats, SetupItem::Cheats, kRight},
};

// The disasters: how often they come on the left, what they do to the game's speed on the right.
constexpr Group kDisasterGroups[] = {
    {"setup.group.disasters", SetupItem::Droughts, SetupItem::Fires, kLeft},
    {"setup.group.game_speed", SetupItem::EventSpeed, SetupItem::EventSpeed, kRight},
};

// The islands: what kind of world, the land and its sizes, and what is on the islands; the
// preview on the right.
constexpr Group kIslandGroups[] = {
    {"setup.group.island_shapes", SetupItem::IslandPreset, SetupItem::Outline, kLeft},
    {"setup.group.island_sizes", SetupItem::LandShare, SetupItem::IslandGap, kLeft},
    {"setup.group.island_nature", SetupItem::Forest, SetupItem::BigOreVeins, kLeft},
};

// The preview: its frame in the right column, the picture inside it at most this large.
constexpr std::int32_t kPreviewW = kGroupW - 24;
constexpr std::int32_t kPreviewH = 330;
constexpr std::int32_t kCell = 2;  ///< design units of one cell of the picture

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

GameSettingsMenu::~GameSettingsMenu() {
    // A preview being made still uses the maker: wait for it.
    if (m_pending.valid()) {
        m_pending.wait();
    }
}

void GameSettingsMenu::wait_preview() {
    // The preview being made may be of settings since changed: then the next one starts when it
    // is done. Wait until the preview is of the settings as they are.
    update_preview();
    while (m_pending.valid()) {
        m_pending.wait();
        update_preview();
    }
}

void GameSettingsMenu::update_preview() {
    if (m_pending.valid() && m_pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto made = m_pending.get();
        if (made) {
            m_preview = std::move(made).value();
            m_preview_error.clear();
            picture(*m_preview);
        } else {
            m_preview.reset();
            m_preview_error = made.error().message;
        }
    }
    if (!m_maker || m_page != Page::Islands || m_pending.valid()) {
        return;  // the next one when this one is made
    }
    // The world follows from the world's settings, the seed and the level.
    const std::string key = write_setup(m_setup) + std::to_string(m_setup.world_seed) + "/" + std::to_string(m_level) +
                            (m_setup.own_world ? "/own" : "/original");
    if (key == m_preview_key) {
        return;
    }
    m_preview_key = key;
    m_pending = std::async(std::launch::async, m_maker, m_setup, m_level);
}

void GameSettingsMenu::picture(const sim::WorldPreview& p) {
    m_runs.clear();
    m_picture_w = 0;
    m_picture_h = 0;
    if (p.width == 0 || p.height == 0) {
        return;
    }
    // As large as fits, the map's shape kept; in cells of kCell units.
    const double scale = std::min(static_cast<double>(kPreviewW) / p.width, static_cast<double>(kPreviewH) / p.height);
    const auto cols = std::max<std::int32_t>(1, static_cast<std::int32_t>(p.width * scale) / kCell);
    const auto rows = std::max<std::int32_t>(1, static_cast<std::int32_t>(p.height * scale) / kCell);
    m_picture_w = cols * kCell;
    m_picture_h = rows * kCell;
    // A cell shows the most telling of its tiles, so that a small island or a village stays in sight.
    using Tile = sim::WorldPreview::Tile;
    constexpr std::array<std::uint8_t, sim::WorldPreview::kTiles> kWeight = {0, 1, 3, 2, 4, 5, 6};
    for (std::int32_t r = 0; r < rows; ++r) {
        const auto y0 = static_cast<std::uint32_t>(std::int64_t{r} * p.height / rows);
        const auto y1 = std::max(y0 + 1, static_cast<std::uint32_t>(std::int64_t{r + 1} * p.height / rows));
        Run run;
        run.y = r * kCell;
        for (std::int32_t c = 0; c < cols; ++c) {
            const auto x0 = static_cast<std::uint32_t>(std::int64_t{c} * p.width / cols);
            const auto x1 = std::max(x0 + 1, static_cast<std::uint32_t>(std::int64_t{c + 1} * p.width / cols));
            Tile best = Tile::Sea;
            for (std::uint32_t y = y0; y < y1 && y < p.height; ++y) {
                for (std::uint32_t x = x0; x < x1 && x < p.width; ++x) {
                    const Tile t = p.at(x, y);
                    if (kWeight[static_cast<std::size_t>(t)] > kWeight[static_cast<std::size_t>(best)]) {
                        best = t;
                    }
                }
            }
            const std::uint8_t colour = m_colours[static_cast<std::size_t>(best)];
            if (run.w > 0 && run.colour == colour) {
                run.w += kCell;
            } else {
                if (run.w > 0) {
                    m_runs.push_back(run);
                }
                run.x = c * kCell;
                run.w = kCell;
                run.colour = colour;
            }
        }
        m_runs.push_back(run);
    }
    // The starts as small squares.
    for (const auto& st : p.starts) {
        const auto x = static_cast<std::int32_t>(std::int64_t{st.x} * m_picture_w / p.width);
        const auto y = static_cast<std::int32_t>(std::int64_t{st.y} * m_picture_h / p.height);
        for (std::int32_t dy = -3; dy <= 3; ++dy) {
            const std::int32_t yy = y + dy;
            if (yy >= 0 && yy < m_picture_h) {
                m_runs.push_back(Run{std::max(0, x - 3), yy, std::min(7, m_picture_w - std::max(0, x - 3)),
                                     m_colours[sim::WorldPreview::kTiles]});
            }
        }
    }
}

void GameSettingsMenu::layout_preview(std::int32_t x, std::int32_t y) {
    ui::Form& f = m_form;
    const std::int32_t h = 12 + kPreviewH + 8 + 26 + 8 + 44 + 12;
    f.panel(Rect{x, y, kGroupW, h});
    const Rect area{x + 12, y + 12, kPreviewW, kPreviewH};
    update_preview();
    if (m_preview && m_picture_w > 0) {
        const std::int32_t left = area.x + (area.w - m_picture_w) / 2;
        const std::int32_t top = area.y + (area.h - m_picture_h) / 2;
        for (const Run& r : m_runs) {
            f.swatch(Rect{left + r.x, top + r.y, r.w, r.y + kCell <= m_picture_h ? kCell : 1}, r.colour);
        }
    }
    const std::int32_t text_y = area.y + area.h + 8;
    std::string status;
    if (!m_maker) {
        status.clear();
    } else if (m_pending.valid()) {
        status = ui::tr("setup.preview.busy");
    } else if (!m_preview_error.empty()) {
        status = ui::tr("setup.preview.failed", {m_preview_error});
    } else if (m_preview) {
        const std::uint64_t area_tiles = std::uint64_t{m_preview->width} * m_preview->height;
        status = ui::tr("setup.preview.stats",
                        {std::to_string(m_preview->islands), std::to_string((m_preview->land * 100 + area_tiles / 2) /
                                                                            std::max<std::uint64_t>(area_tiles, 1))});
    }
    f.label(Rect{x + 12, text_y, kPreviewW, 26}, status, Align::Left,
            m_preview_error.empty() ? Tone::Normal : Tone::Warning);
    const std::int32_t button_y = text_y + 34;
    // A multiplayer game's world number as its "world number" field shows it.
    char seed[24];
    std::snprintf(seed, sizeof(seed), m_scope == SetupScope::MultiplayerEndless ? "%llu" : "%llX",
                  static_cast<unsigned long long>(m_setup.world_seed));
    f.label(Rect{x + 12, button_y, kPreviewW - 200, 44}, ui::tr("setup.preview.seed", {seed}), Align::Left, Tone::Dim);
    f.button(kReroll, Rect{x + kGroupW - 12 - 190, button_y, 190, 44}, ui::tr("setup.preview.reroll"),
             m_mode != Mode::View && m_reroll);
}

std::uint64_t GameSettingsMenu::new_seed() const {
    std::random_device device;
    if (m_scope == SetupScope::MultiplayerEndless) {
        return device() % kMultiplayerSeeds;
    }
    return (std::uint64_t{device()} << 32U) | device();
}

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
    } else if (*id == kDisasters) {
        m_page = Page::Disasters;
    } else if (*id == kDisastersOriginal && editable) {
        m_setup.rules = original_disasters(m_setup.rules);
    } else if (*id == kIslands) {
        m_page = Page::Islands;
        if (m_setup.world_seed == 0 && editable && m_scope != SetupScope::MultiplayerEndless) {
            // The preview's world is the game's: a number for it now. A multiplayer game has its
            // number already (0 is one of them).
            m_setup.world_seed = new_seed();
        }
    } else if (*id == kReroll && editable && m_reroll) {
        m_setup.world_seed = new_seed();
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
    const bool islands = m_page == Page::Islands;
    const bool disasters = m_page == Page::Disasters;
    f.label(kHeading,
            ui::tr(victory     ? "setup.title.victory"
                   : bugs      ? "setup.title.bugs"
                   : islands   ? "setup.title.islands"
                   : disasters ? "setup.title.disasters"
                               : title_key(m_scope)),
            Align::Left, Tone::Heading);
    const bool editable = m_mode != Mode::View;
    std::int32_t left_y = kTop;
    std::int32_t right_y = kTop;
    const std::span<const Group> groups = victory     ? std::span<const Group>(kVictoryGroups)
                                          : bugs      ? std::span<const Group>(kBugGroups)
                                          : islands   ? std::span<const Group>(kIslandGroups)
                                          : disasters ? std::span<const Group>(kDisasterGroups)
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
    // The way to the islands' page, below the world's items.
    const bool islands_row = m_page == Page::Settings && setup_item_offered(m_scope, SetupItem::IslandPreset);
    // The way to the disasters' page, below the nature's items.
    const bool disasters_row = m_page == Page::Settings && setup_item_offered(m_scope, SetupItem::Plagues);
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
        if (islands_row) {
            measure(ui::tr("setup.island_page"));
        }
        if (disasters_row) {
            measure(ui::tr("setup.disasters"));
        }
    }
    const std::int32_t label_w = std::clamp(static_cast<std::int32_t>(std::ceil(longest)) + 2 * kTextPadding,
                                            kMinLabelW, kRowW - 4 - kMinValueW);
    const std::int32_t value_w = kRowW - 4 - label_w;
    // A group's frame and heading; the top of its first row.
    // Without a heading (the ways to the other pages) the frame holds just the rows.
    const auto group = [&](std::string_view heading, std::int32_t rows, std::int32_t x) {
        std::int32_t& y0 = x < kScreen.x + kScreen.w / 2 ? left_y : right_y;
        const std::int32_t head = heading.empty() ? 8 : 44;
        f.panel(Rect{x, y0, kGroupW, head + rows * kRowH});
        if (!heading.empty()) {
            f.label(Rect{x + 12, y0 + 6, kRowW, 32}, ui::tr(heading), Align::Left, Tone::Heading);
        }
        const std::int32_t first = y0 + head - 4;
        y0 += head + rows * kRowH + kGap;
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
        const bool world = g.first == SetupItem::Map && islands_row;
        const bool nature = g.first == SetupItem::FertileFields && disasters_row;
        const std::int32_t extra = world || nature ? 1 : 0;
        const std::int32_t top = group(g.heading, static_cast<std::int32_t>(items.size()) + extra, g.x);
        const std::int32_t extra_y = top + static_cast<std::int32_t>(items.size()) * kRowH;
        if (world) {
            const Rect value = row(g.x, extra_y, ui::tr("setup.island_page"), m_setup.own_world);
            f.button(kIslands, value, ui::tr("setup.island_page.open", {islands_summary(m_setup)}));
        } else if (nature) {
            const Rect value = row(g.x, extra_y, ui::tr("setup.disasters"), true);
            f.button(kDisasters, value, ui::tr("setup.disasters.open", {disasters_summary(m_setup.rules)}));
        }
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
        // Below the nature: the ways to the pages of the victory conditions and of the original's
        // bugs, each with what is chosen (to look at too, for a game that is only shown).
        std::int32_t top = group({}, victory_row ? 2 : 1, kLeft);
        if (victory_row) {
            const Rect value = row(kLeft, top, ui::tr("setup.victory"), m_setup.rules.victory.active());
            f.button(kVictory, value, ui::tr("setup.victory.open", {victory_summary(m_setup.rules.victory)}));
            top += kRowH;
        }
        const Rect value = row(kLeft, top, ui::tr("setup.bugs"), page_bugs(m_setup.rules.bugs).count() != 0);
        f.button(kBugs, value, ui::tr("setup.bugs.open", {bugs_summary(m_setup.rules.bugs)}));
    }
    if (islands) {
        layout_preview(kRight, kTop);
        right_y = kTop + 12 + kPreviewH + 8 + 26 + 8 + 44 + 12 + kGap;
    }
    // The hint below the lower column; three lines of it fit above the buttons.
    const std::int32_t hint_y = std::max({kHintY, left_y, right_y});
    if (!m_error.empty()) {
        f.paragraph(Rect{40, hint_y, 944, kBottomY - 4 - hint_y}, m_error, Tone::Warning);
    } else {
        f.paragraph(Rect{40, hint_y, 944, kBottomY - 4 - hint_y},
                    ui::tr(victory     ? (editable ? "setup.hint.victory" : "setup.hint.victory.view")
                           : bugs      ? (editable ? "setup.hint.bugs" : "setup.hint.bugs.view")
                           : islands   ? (!m_setup.own_world ? "setup.hint.islands.original"
                                          : editable         ? "setup.hint.islands"
                                                             : "setup.hint.islands.view")
                           : disasters ? (editable ? "setup.hint.disasters" : "setup.hint.disasters.view")
                                       : hint_key(m_scope)),
                    Tone::Dim);
    }
    f.button(kBack, Rect{40, kBottomY, 180, 44}, ui::tr("setup.back"));
    if (!editable) {
        return;
    }
    if (islands) {
        // The kinds of world are the page's first item.
    } else if (victory) {
        f.button(kVictoryOff, Rect{520, kBottomY, 240, 44}, ui::tr("setup.victory.off"), true,
                 !m_setup.rules.victory.active());
    } else if (disasters) {
        f.button(kDisastersOriginal, Rect{520, kBottomY, 240, 44}, ui::tr("setup.disasters.all_original"), true,
                 original_disasters(m_setup.rules) == m_setup.rules);
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
