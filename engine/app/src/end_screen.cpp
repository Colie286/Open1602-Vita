// SPDX-License-Identifier: GPL-3.0-or-later
#include "end_screen.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "banner_graphic.hpp"

#include "o1602/core/text.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

// ENDE.GAD (docs/re/ui.md "Endbildschirm").
constexpr std::int32_t kMainMenu = 36001;
constexpr std::int32_t kNewGame = 36002;
constexpr std::int32_t kBack = 36003;  // commented out in the shipped file
constexpr std::int32_t kTotal = 36004;
constexpr std::int32_t kArms = 36005;
constexpr std::int32_t kName = 36006;
constexpr std::int32_t kFigures = 36011;  // the status column, 11 lines
constexpr std::int32_t kPoints = 36031;   // the points column, 9 lines
constexpr std::int32_t kColours = 36051;  // the coats of arms to choose a player, 4
// Open1602's scroll arrows of the strip, copied from TRANSFER.GAD's goods arrows (45007, 45008).
constexpr std::int32_t kStripLeft = 36061;
constexpr std::int32_t kStripRight = 36062;
constexpr std::int32_t kTransferLeft = 45007;
constexpr std::int32_t kTransferRight = 45008;
constexpr std::int32_t kArrowGap = 6;  // between an arrow and the coat of arms beside it

// The hint of Open1602's way back into a won game, over the buttons.
constexpr std::int32_t kHintX = 512;
constexpr std::int32_t kHintY = 680;
constexpr std::int32_t kText = 4;  // text colour of the original (ui::text_colour)
// The ranks: under the name, and under each coat of arms of the strip (small font, its cell's bottom
// this far below the name's or the gadget's bottom).
constexpr std::int32_t kSmallFont = 1;
constexpr std::int32_t kRankBelowName = 19;
constexpr std::int32_t kRankBelowArms = 18;

std::int32_t slot_id(std::size_t slot) {
    return kColours + static_cast<std::int32_t>(slot);
}

/// ENDE.GAD with the scroll arrows of TRANSFER.GAD beside its coats of arms to choose a player,
/// centred on them; unchanged if either file lacks its gadgets.
assets::GadgetLayout with_strip_arrows(assets::GadgetLayout ende, const assets::GadgetLayout* transfer,
                                       std::size_t slots) {
    const assets::Gadget* first = ende.find(kColours);
    const assets::Gadget* last = slots > 0 ? ende.find(slot_id(slots - 1)) : nullptr;
    if (transfer == nullptr || first == nullptr || last == nullptr) {
        return ende;
    }
    const std::int32_t middle = first->pos[1] + first->size[1] / 2;
    const std::int32_t left_end = first->pos[0];
    const std::int32_t right_end = last->pos[0] + last->size[0];  // copied: the gadgets grow below
    for (const auto& [from, to] : {std::pair{kTransferLeft, kStripLeft}, std::pair{kTransferRight, kStripRight}}) {
        const assets::Gadget* g = transfer->find(from);
        if (g == nullptr) {
            return ende;
        }
        assets::Gadget arrow = *g;
        arrow.id = to;
        arrow.base = static_cast<std::int32_t>(ende.gadgets.size());
        arrow.pos = {to == kStripLeft ? left_end - kArrowGap - g->size[0] : right_end + kArrowGap,
                     middle - g->size[1] / 2};
        ende.gadgets.push_back(arrow);
    }
    return ende;
}

std::size_t slots_of(const assets::GadgetLayout& ende) {
    std::size_t n = 0;
    while (ende.find(slot_id(n)) != nullptr) {
        ++n;
    }
    return n;
}

}  // namespace

EndScreen::EndScreen(std::optional<assets::GadgetLayout> ende, const assets::GadgetLayout* transfer) {
    if (ende) {
        if (const assets::Gadget* g = ende->find(kArms)) {
            m_arms = g->gfx;
        }
        if (const assets::Gadget* g = ende->find(kColours)) {
            m_button_arms = g->gfx;
            m_button_press = g->press_offset;
        }
        m_slots = slots_of(*ende);
        assets::GadgetLayout layout = with_strip_arrows(std::move(*ende), transfer, m_slots);
        m_arrows = layout.find(kStripLeft) != nullptr;
        m_panel.emplace(std::move(layout), m_scale);
    }
}

void EndScreen::open(sim::EndScreenSummary summary, bool view_only) {
    m_summary = std::move(summary);
    m_view_only = view_only;
    m_by_colour = {};
    const std::vector<sim::EndScreenRow>& rows = m_summary.rows;
    bool other_colours = false;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const sim::BannerId banner = rows[i].banner;
        if (banner < m_by_colour.size() && !m_by_colour[banner]) {
            m_by_colour[banner] = i;
        }
        other_colours = other_colours || banner >= m_by_colour.size();
    }
    // The ranks by points, and the strip in their order (stable: the local player first among equals).
    m_order.resize(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        m_order[i] = i;
    }
    std::stable_sort(m_order.begin(), m_order.end(),
                     [&rows](std::size_t a, std::size_t b) { return rows[a].sheet.total > rows[b].sheet.total; });
    m_rank.assign(rows.size(), 0);
    for (std::size_t place = 0; place < m_order.size(); ++place) {
        const bool tie = place > 0 && rows[m_order[place]].sheet.total == rows[m_order[place - 1]].sheet.total;
        m_rank[m_order[place]] = tie ? m_rank[m_order[place - 1]] : place + 1;
    }
    // Open1602: the strip when the colours' coats of arms cannot show every player.
    m_strip = rows.size() > m_slots || other_colours;
    m_first = 0;
    if (!m_panel) {
        m_row = 0;
        return;
    }
    ui::Panel& p = *m_panel;
    p.set_visible(kMainMenu, !view_only);
    p.set_visible(kNewGame, !view_only);
    p.set_visible(kBack, view_only);
    if (m_strip) {
        update_strip();
    } else {
        // Screen_PlayerStatus: a coat of arms for every colour somebody plays, with two players or more.
        for (std::size_t c = 0; c < m_slots; ++c) {
            p.set_graphic(slot_id(c), m_button_arms + static_cast<std::int32_t>(c), m_button_press);
            p.set_visible(slot_id(c),
                          !view_only && c < m_by_colour.size() && m_by_colour[c].has_value() && rows.size() >= 2);
        }
        if (m_arrows) {
            p.set_visible(kStripLeft, false);
            p.set_visible(kStripRight, false);
        }
    }
    show(0);  // the local player is the first row
}

void EndScreen::scroll_strip(std::size_t first) {
    const std::size_t last_first = m_order.size() > m_slots ? m_order.size() - m_slots : 0;
    m_first = std::min(first, last_first);
    update_strip();
}

void EndScreen::update_strip() {
    if (!m_panel || !m_strip) {
        return;
    }
    ui::Panel& p = *m_panel;
    const bool shown = !m_view_only && m_order.size() >= 2;
    for (std::size_t s = 0; s < m_slots; ++s) {
        const std::size_t place = m_first + s;
        const bool used = shown && place < m_order.size();
        p.set_visible(slot_id(s), used);
        if (used) {
            const sim::EndScreenRow& r = m_summary.rows[m_order[place]];
            set_banner_graphic(p, slot_id(s), render::BannerArt::at(m_button_arms),
                               m_banners != nullptr ? m_banners->get(r.banner) : nullptr, m_button_press);
            p.set_selected(slot_id(s), m_order[place] == m_row);
        }
    }
    if (m_arrows) {
        const bool scrolls = shown && m_order.size() > m_slots;
        p.set_visible(kStripLeft, scrolls);
        p.set_visible(kStripRight, scrolls);
        p.set_disabled(kStripLeft, m_first == 0);
        p.set_disabled(kStripRight, m_first + m_slots >= m_order.size());
    }
}

void EndScreen::show(std::size_t row) {
    if (!m_panel || row >= m_summary.rows.size()) {
        return;
    }
    ui::Panel& p = *m_panel;
    m_row = row;
    const sim::EndScreenRow& r = m_summary.rows[row];
    const sim::ScoreSheet& s = r.sheet;
    const std::array<std::int64_t, 10> figures{s.cities,     s.inhabitants,       s.monuments,     s.money,
                                               s.islands,    s.soldiers_killed,   s.soldiers_lost, s.ships_sunk,
                                               s.ships_lost, s.opponents_defeated};
    for (std::size_t i = 0; i < figures.size(); ++i) {
        p.set_text(kFigures + static_cast<std::int32_t>(i), std::to_string(figures[i]));
    }
    p.set_text(kFigures + static_cast<std::int32_t>(figures.size()),
               std::to_string(s.bonus_percent) + "%");  // [GAME] 48 "%d%%"
    const std::array<std::int64_t, 9> points{s.city_points,  s.subjects_points, s.monument_points,
                                             s.money_points, s.land_points,     s.soldier_points,
                                             s.ship_points,  s.defeat_points,   s.bonus};
    for (std::size_t i = 0; i < points.size(); ++i) {
        p.set_text(kPoints + static_cast<std::int32_t>(i), std::to_string(points[i]));
    }
    p.set_text(kTotal, std::to_string(s.total));
    p.set_text(kName, r.name.empty() ? ui::tr("end.player", {std::to_string(r.player + 1)}) : r.name);
    // The coat of arms in the player's colour (the further colours: the white one recoloured).
    set_banner_graphic(p, kArms, render::BannerArt::at(m_arms),
                       m_banners != nullptr ? m_banners->get(r.banner) : nullptr);
    if (m_strip) {
        // Keep the shown player in the strip.
        const auto at = std::find(m_order.begin(), m_order.end(), row);
        const auto place = static_cast<std::size_t>(at - m_order.begin());
        if (place < m_first) {
            m_first = place;
        } else if (m_slots > 0 && place >= m_first + m_slots) {
            m_first = place + 1 - m_slots;
        }
        update_strip();
    } else {
        for (std::size_t c = 0; c < m_slots; ++c) {
            p.set_selected(slot_id(c), c < m_by_colour.size() && m_by_colour[c] == row);
        }
    }
}

void EndScreen::step(int direction) {
    const std::size_t n = m_summary.rows.size();
    if (n < 2 || m_view_only) {
        return;
    }
    if (m_strip) {
        const auto at = static_cast<std::size_t>(std::find(m_order.begin(), m_order.end(), m_row) - m_order.begin());
        show(m_order[direction < 0 ? (at + n - 1) % n : (at + 1) % n]);
        return;
    }
    show(direction < 0 ? (m_row + n - 1) % n : (m_row + 1) % n);
}

void EndScreen::set_scale(double scale) {
    m_scale = scale;
    if (m_panel) {
        m_panel->set_scale(scale);
    }
}

std::vector<ui::DrawItem> EndScreen::draw(const std::array<const ui::Font*, ui::kFontSlots>& fonts) const {
    std::vector<ui::DrawItem> items;
    if (m_panel) {
        items = m_panel->draw(fonts);
        draw_ranks(items);
    }
    if (!m_view_only && m_summary.outcome == sim::GameOutcome::Won) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = 0;
        d.colour = kText;
        d.align = ui::Align::Centre;
        d.x = ui::to_pixels(kHintX, m_scale);
        d.y = ui::to_pixels(kHintY, m_scale);
        d.text = ui::tr("end.continue_hint");
        items.push_back(std::move(d));
    }
    return items;
}

void EndScreen::draw_ranks(std::vector<ui::DrawItem>& out) const {
    if (!m_strip || m_view_only || m_order.size() < 2 || !m_panel) {
        return;
    }
    const auto text = [&](std::int32_t cx, std::int32_t bottom, std::string s) {
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = kSmallFont;
        d.colour = kText;
        d.align = ui::Align::Centre;
        d.x = ui::to_pixels(cx, m_scale);
        d.y = ui::to_pixels(bottom, m_scale);
        d.text = std::move(s);
        out.push_back(std::move(d));
    };
    const assets::GadgetLayout& layout = m_panel->layout();
    if (const assets::Gadget* name = layout.find(kName)) {
        text(name->pos[0], name->pos[1] + name->offset[1] + kRankBelowName,
             ui::tr("end.rank", {std::to_string(rank(m_row)), std::to_string(m_order.size())}));
    }
    for (std::size_t s = 0; s < m_slots && m_first + s < m_order.size(); ++s) {
        if (const auto pos = m_panel->position(slot_id(s))) {
            const assets::Gadget* g = layout.find(slot_id(s));
            text((*pos)[0] + g->size[0] / 2, (*pos)[1] + g->size[1] + kRankBelowArms,
                 ui::tr("end.place", {std::to_string(rank(m_order[m_first + s]))}));
        }
    }
}

std::optional<EndScreen::Choice> EndScreen::mouse(std::int32_t x, std::int32_t y, bool down) {
    if (!m_panel) {
        return std::nullopt;
    }
    std::optional<Choice> choice;
    for (const ui::PanelEvent& e : m_panel->mouse(x, y, down)) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            continue;
        }
        if (e.id == kMainMenu) {
            choice = Choice::MainMenu;
        } else if (e.id == kNewGame) {
            choice = Choice::NewGame;
        } else if (e.id == kBack) {
            choice = Choice::Resume;
        } else if (e.id == kStripLeft && m_first > 0) {
            scroll_strip(m_first - 1);
        } else if (e.id == kStripRight) {
            scroll_strip(m_first + 1);
        } else if (e.id >= kColours && e.id < slot_id(m_slots)) {
            const auto slot = static_cast<std::size_t>(e.id - kColours);
            if (m_strip) {
                if (m_first + slot < m_order.size()) {
                    show(m_order[m_first + slot]);
                }
            } else if (slot < m_by_colour.size()) {
                if (const auto row = m_by_colour[slot]) {
                    show(*row);
                }
            }
        }
    }
    return choice;
}

std::optional<EndScreen::Choice> EndScreen::back() const noexcept {
    if (m_view_only || m_summary.outcome == sim::GameOutcome::Won) {
        return Choice::Resume;
    }
    return std::nullopt;
}

}  // namespace o1602::app
