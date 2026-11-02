// SPDX-License-Identifier: GPL-3.0-or-later
#include "chat_window.hpp"

#include <algorithm>
#include <utility>

#include "multiplayer_texts.hpp"

#include "o1602/net/protocol.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

constexpr Id kField = 1;
constexpr Id kClose = 2;
constexpr Id kAll = 3;
constexpr Id kPartner = 100;  ///< + index in ChatModel::partners

constexpr std::int32_t kW = ChatWindow::kWidth;
constexpr std::int32_t kRowsTop = 52;
constexpr std::int32_t kRowH = 40;
constexpr std::int32_t kButtonsX = 64;
constexpr std::int32_t kButtonW = 126;
constexpr std::int32_t kButtonGap = 6;
constexpr std::size_t kPerRow = 4;
constexpr std::int32_t kLineH = 24;
constexpr std::int32_t kHistoryH = ChatWindow::kHistoryLines * kLineH + 12;
constexpr std::int32_t kFieldH = 36;
constexpr std::int32_t kTextW = kW - 40;  ///< a line of the history or over the map

std::string tr(MpText id) {
    return mp_text(id);
}

/// The text of a line as the chat shows it, and how.
std::pair<std::string, Tone> shown(const ChatLine& line) {
    if (line.name.empty()) {
        return {line.text, Tone::Dim};
    }
    return {chat_prefix(line) + line.text, line.whisper ? Tone::Heading : Tone::Normal};
}

const ui::Font* form_font(const ui::FormStyle& style, const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    return style.font >= 0 && style.font < static_cast<std::int32_t>(ui::kFontSlots)
               ? fonts[static_cast<std::size_t>(style.font)]
               : nullptr;
}

}  // namespace

void ChatWindow::open() {
    m_open = true;
    m_form.set_focus(kField);
}

std::int32_t ChatWindow::history_top(const ChatModel& model) {
    if (!model.may_write || model.partners.empty()) {
        return kRowsTop;
    }
    const std::size_t buttons = model.partners.size() + 1;  // "All" first
    const auto rows = static_cast<std::int32_t>((buttons + kPerRow - 1) / kPerRow);
    return kRowsTop + rows * kRowH + 6;
}

std::int32_t ChatWindow::height(const ChatModel& model) const {
    return history_top(model) + kHistoryH + 8 + kFieldH + 12;
}

void ChatWindow::prune(const ChatModel& model) {
    std::erase_if(m_to, [&](sim::PlayerId seat) {
        return std::none_of(model.partners.begin(), model.partners.end(),
                            [&](const ChatPartner& p) { return p.seat == seat; });
    });
}

void ChatWindow::layout(const ChatModel& model, const std::deque<ChatLine>* lines,
                        const std::array<const ui::Font*, ui::kFontSlots>* fonts) {
    prune(model);
    ui::Form& f = m_form;
    f.clear();
    const std::int32_t h = height(model);
    f.panel(Rect{0, 0, kW, h});
    f.label(Rect{16, 8, 300, 34}, tr(MpText::Chat), Align::Left, Tone::Heading);
    f.button(kClose, Rect{kW - 146, 8, 130, 34}, tr(MpText::ChatClose));

    // To whom: everyone, or any choice of the other persons.
    if (model.may_write && !model.partners.empty()) {
        f.label(Rect{16, kRowsTop, kButtonsX - 16, 34}, tr(MpText::ChatToLabel), Align::Left, Tone::Dim);
        const auto cell = [](std::size_t i) {
            return Rect{kButtonsX + static_cast<std::int32_t>(i % kPerRow) * (kButtonW + kButtonGap),
                        kRowsTop + static_cast<std::int32_t>(i / kPerRow) * kRowH, kButtonW, 34};
        };
        f.button(kAll, cell(0), tr(MpText::ChatAll), true, m_to.empty());
        for (std::size_t i = 0; i < model.partners.size(); ++i) {
            const ChatPartner& p = model.partners[i];
            const Rect r = cell(i + 1);
            const bool on = std::find(m_to.begin(), m_to.end(), p.seat) != m_to.end();
            f.button(kPartner + static_cast<Id>(i), r, {}, true, on);
            f.swatch(Rect{r.x + 8, r.y + 8, 14, r.h - 16}, banner_colour(p.banner));
            f.label(Rect{r.x + 24, r.y, r.w - 28, r.h}, p.name, Align::Left, on ? Tone::Heading : Tone::Normal);
        }
    }

    // The lines so far, the newest at the bottom, long ones wrapped.
    const std::int32_t top = history_top(model);
    f.panel(Rect{12, top, kW - 24, kHistoryH});
    if (lines != nullptr && fonts != nullptr) {
        const ui::Font* font = form_font(m_style, *fonts);
        std::vector<std::pair<std::string, Tone>> rows;  // newest first
        for (auto it = lines->rbegin(); it != lines->rend() && rows.size() < kHistoryLines; ++it) {
            auto [text, tone] = shown(*it);
            std::vector<std::string> wrapped{text};
            if (font != nullptr) {
                wrapped = ui::wrap_text(*font, text, static_cast<std::uint32_t>(kTextW * font->design_scale()));
            }
            for (auto w = wrapped.rbegin(); w != wrapped.rend() && rows.size() < kHistoryLines; ++w) {
                rows.emplace_back(std::move(*w), tone);
            }
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto y = top + 6 + static_cast<std::int32_t>(rows.size() - 1 - i) * kLineH;
            f.label(Rect{16, y, kW - 32, kLineH}, rows[i].first, Align::Left, rows[i].second);
        }
    }

    // The line being typed, with to whom it goes.
    const std::int32_t y = top + kHistoryH + 8;
    const Rect field{12, y, kW - 24, kFieldH};
    if (!model.may_write) {
        f.label(field, tr(MpText::ChatReadOnly), Align::Left, Tone::Dim);
        f.set_focus(std::nullopt);
        return;
    }
    f.field(kField, field, m_text);
    f.set_focus(kField);
    if (m_text.empty()) {
        std::string names;
        for (const ChatPartner& p : model.partners) {
            if (std::find(m_to.begin(), m_to.end(), p.seat) != m_to.end()) {
                names += (names.empty() ? "" : ", ") + p.name;
            }
        }
        f.label(field, names.empty() ? tr(MpText::ChatHintAll) : mp_text(MpText::ChatHintPrivate, names), Align::Left,
                Tone::Dim);
    }
}

std::vector<ui::DrawItem> ChatWindow::draw(const ChatModel& model, const std::deque<ChatLine>& lines,
                                           const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model, &lines, &fonts);
    return m_form.draw(fonts);
}

std::vector<ui::DrawItem> ChatWindow::draw_recent(const std::deque<ChatLine>& lines, std::uint64_t now_ms,
                                                  const std::array<const ui::Font*, ui::kFontSlots>& fonts) const {
    std::vector<const ChatLine*> recent;  // newest first
    for (auto it = lines.rbegin(); it != lines.rend() && recent.size() < kRecentLines; ++it) {
        if (it->time_ms > now_ms || now_ms - it->time_ms >= kRecentMs) {
            break;
        }
        recent.push_back(&*it);
    }
    std::vector<ui::DrawItem> out;
    if (recent.empty()) {
        return out;
    }
    const ui::Font* font = form_font(m_style, fonts);
    const double s = m_form.scale();
    const auto px = [s](std::int32_t v) { return ui::to_pixels(v, s); };
    // A dark ground at half strength under the lines, so that they read over any land.
    const auto n = static_cast<std::int32_t>(recent.size());
    ui::DrawItem ground;
    ground.kind = ui::DrawItem::Kind::Fill;
    ground.y = px(-(n * kLineH + 8));
    ground.w = px(kW);
    ground.h = -ground.y;
    ground.colour = m_style.field;
    ground.half = true;
    out.push_back(ground);
    for (std::int32_t i = 0; i < n; ++i) {
        const auto [text, tone] = shown(*recent[static_cast<std::size_t>(i)]);
        ui::DrawItem d;
        d.kind = ui::DrawItem::Kind::Text;
        d.block = m_style.font;
        d.colour = tone == Tone::Dim ? m_style.dim : tone == Tone::Heading ? m_style.heading : m_style.text;
        d.text = ui::fit_text(font, text, kTextW, false);
        d.x = px(10);
        d.y = px(ui::text_bottom(font, -(i + 1) * kLineH - 4, kLineH));
        out.push_back(std::move(d));
    }
    return out;
}

ChatWindow::Result ChatWindow::mouse(std::int32_t x, std::int32_t y, bool down, const ChatModel& model) {
    layout(model, nullptr, nullptr);
    const auto id = m_form.mouse(x, y, down);
    if (model.may_write) {
        m_form.set_focus(kField);  // a click on the window keeps the line being typed
    }
    if (!id) {
        return Result::None;
    }
    if (*id == kClose) {
        close();
        return Result::Close;
    }
    if (*id == kAll) {
        m_to.clear();
    } else if (*id >= kPartner && *id < kPartner + static_cast<Id>(model.partners.size())) {
        const sim::PlayerId seat = model.partners[static_cast<std::size_t>(*id - kPartner)].seat;
        if (const auto it = std::find(m_to.begin(), m_to.end(), seat); it != m_to.end()) {
            m_to.erase(it);
        } else {
            m_to.insert(std::upper_bound(m_to.begin(), m_to.end(), seat), seat);
        }
    }
    return Result::None;
}

void ChatWindow::type(std::string_view text) {
    if (!text.empty() && m_form.take_all_selected()) {
        m_text.clear();  // typed over the whole line (Ctrl+A)
    }
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && m_text.size() < net::kMaxChatLength) {
            m_text.push_back(c);
        }
    }
}

std::optional<ChatSay> ChatWindow::key(const ChatModel& model, int key) {
    prune(model);
    if (key == 1) {
        m_form.select_all();  // Ctrl+A
        return std::nullopt;
    }
    const bool all = m_form.take_all_selected();
    switch (key) {
        case 8:
            if (all) {
                m_text.clear();
            } else if (!m_text.empty()) {
                m_text.pop_back();
            }
            break;
        case 9: {
            // Tab: everyone, then each other person alone, then everyone again.
            if (model.partners.empty()) {
                break;
            }
            std::size_t next = 0;  // the first partner
            if (m_to.size() == 1) {
                const auto it = std::find_if(model.partners.begin(), model.partners.end(),
                                             [&](const ChatPartner& p) { return p.seat == m_to.front(); });
                next = static_cast<std::size_t>(it - model.partners.begin()) + 1;
            } else if (!m_to.empty()) {
                next = model.partners.size();  // several chosen: back to everyone
            }
            m_to.clear();
            if (next < model.partners.size()) {
                m_to.push_back(model.partners[next].seat);
            }
            break;
        }
        case 13:
            if (m_text.empty() || !model.may_write) {
                close();
                break;
            }
            return ChatSay{std::exchange(m_text, {}), m_to};
        case 27:
            close();
            break;
        default:
            break;
    }
    return std::nullopt;
}

}  // namespace o1602::app
