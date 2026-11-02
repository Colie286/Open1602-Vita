// SPDX-License-Identifier: GPL-3.0-or-later
#include "template_window.hpp"

#include <algorithm>

#include "blueprint_window.hpp"
#include "templates.hpp"

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

constexpr Id kClose = 1;
constexpr Id kField = 2;
constexpr Id kSave = 3;
constexpr Id kCancel = 4;
constexpr Id kPrevious = 5;
constexpr Id kNext = 6;
// Per row of the list: the row's base plus one of these.
constexpr Id kRowBase = 100;
constexpr Id kRowStep = 10;
constexpr Id kTake = 1;
constexpr Id kRename = 2;
constexpr Id kRemove = 3;
constexpr Id kRowField = 4;

constexpr std::int32_t kW = TemplateWindow::kWidth;
constexpr std::int32_t kInner = kW - 32;
constexpr std::int32_t kTop = 52;       ///< the first row below the heading
constexpr std::int32_t kRowH = 112;     ///< a template's row
constexpr std::int32_t kViewSide = 88;  ///< its view from above
constexpr std::int32_t kSaveH = 214;

Id row_id(std::size_t row, Id what) {
    return kRowBase + static_cast<Id>(row) * kRowStep + what;
}

}  // namespace

std::string TemplateWindow::summary(const TemplateEntry& entry) {
    return ui::tr("template.summary",
                  {std::to_string(entry.width), std::to_string(entry.height), std::to_string(entry.buildings)}) +
           ", " + BlueprintWindow::bill_text(entry.cost);
}

void TemplateWindow::open_list() {
    m_mode = Mode::List;
    m_renaming.reset();
    m_removing.reset();
    m_form.set_focus(std::nullopt);
}

void TemplateWindow::open_save(std::string_view name) {
    m_mode = Mode::Save;
    m_text = clean_template_name(name);
    m_renaming.reset();
    m_removing.reset();
    m_form.set_focus(kField);
    m_form.select_all();  // what is typed replaces the name offered
}

void TemplateWindow::close() noexcept {
    m_mode = Mode::Closed;
    m_renaming.reset();
    m_removing.reset();
    m_form.set_focus(std::nullopt);
}

std::int32_t TemplateWindow::height(const TemplatesModel& model) const {
    if (m_mode == Mode::Save) {
        return kSaveH;
    }
    const std::size_t rows = std::clamp<std::size_t>(model.entries.size(), 1, kRows);
    const bool pages = model.entries.size() > kRows;
    return kTop + static_cast<std::int32_t>(rows) * kRowH + (pages ? 50 : 8);
}

void TemplateWindow::view(const TemplateEntry& entry, Rect box) {
    m_form.swatch(box, m_colours.ground);
    if (entry.width <= 0 || entry.height <= 0) {
        return;
    }
    // Whole design units per tile, as large as the box allows; the piece in its middle.
    const std::int32_t cell = std::max(1, std::min((box.w - 4) / entry.width, (box.h - 4) / entry.height));
    const std::int32_t ox = box.x + (box.w - cell * entry.width) / 2;
    const std::int32_t oy = box.y + (box.h - cell * entry.height) / 2;
    for (const TemplateEntry::Piece& p : entry.view) {
        const Rect r{ox + p.x * cell, oy + p.y * cell, std::max(1, p.w * cell - (cell > 3 ? 1 : 0)),
                     std::max(1, p.h * cell - (cell > 3 ? 1 : 0))};
        if (r.x + r.w <= box.x + box.w && r.y + r.h <= box.y + box.h) {
            m_form.swatch(r, m_colours.pieces[static_cast<std::size_t>(p.kind)]);
        }
    }
}

void TemplateWindow::layout(const TemplatesModel& model) {
    ui::Form& f = m_form;
    f.clear();
    const std::int32_t h = height(model);
    f.panel(Rect{0, 0, kW, h});
    if (m_mode == Mode::Save) {
        f.label(Rect{16, 8, kInner, 34}, ui::tr("template.save.title"), Align::Left, Tone::Heading);
        f.label(Rect{16, 50, kInner, 28}, ui::tr("template.name"));
        f.field(kField, Rect{16, 80, kInner, 34}, m_text);
        f.paragraph(Rect{16, 120, kInner, 44}, summary(model.piece), Tone::Dim);
        const std::int32_t half = (kInner - 8) / 2;
        f.button(kSave, Rect{16, kSaveH - 50, half, 34}, ui::tr("template.save"), !clean_template_name(m_text).empty());
        f.button(kCancel, Rect{16 + half + 8, kSaveH - 50, half, 34}, ui::tr("template.cancel"));
        return;
    }
    f.label(Rect{16, 8, kInner - 140, 34}, ui::tr("template.title"), Align::Left, Tone::Heading);
    f.button(kClose, Rect{kW - 146, 8, 130, 34}, ui::tr("template.close"));
    if (model.entries.empty()) {
        f.paragraph(Rect{16, kTop, kInner, kRowH - 8}, ui::tr("template.empty"), Tone::Dim);
        return;
    }
    const std::size_t pages = (model.entries.size() + kRows - 1) / kRows;
    m_page = std::min(m_page, pages - 1);
    for (std::size_t row = 0; row < kRows; ++row) {
        const std::size_t index = m_page * kRows + row;
        if (index >= model.entries.size()) {
            break;
        }
        const TemplateEntry& e = model.entries[index];
        const std::int32_t y = kTop + static_cast<std::int32_t>(row) * kRowH;
        view(e, Rect{16, y, kViewSide, kViewSide});
        const std::int32_t tx = 16 + kViewSide + 10;
        const std::int32_t tw = kW - 16 - tx;
        if (m_renaming == index) {
            f.field(row_id(row, kRowField), Rect{tx, y, tw, 30}, m_text);
        } else {
            f.label(Rect{tx, y, tw, 30}, e.name);
        }
        f.paragraph(Rect{tx, y + 30, tw, 40}, summary(e), Tone::Dim, 20);
        const std::int32_t bw = (tw - 12) / 3;
        const std::int32_t by = y + 72;
        f.button(row_id(row, kTake), Rect{tx, by, bw, 30}, ui::tr("template.take"));
        f.button(row_id(row, kRename), Rect{tx + bw + 6, by, bw, 30},
                 ui::tr(m_renaming == index ? "template.rename.done" : "template.rename"));
        f.button(row_id(row, kRemove), Rect{tx + 2 * (bw + 6), by, bw, 30},
                 ui::tr(m_removing == index ? "template.remove.sure" : "template.remove"));
    }
    if (pages > 1) {
        const std::int32_t y = h - 44;
        f.button(kPrevious, Rect{16, y, 120, 32}, ui::tr("template.previous"), m_page > 0);
        f.label(Rect{146, y, kInner - 260, 32},
                ui::tr("template.page", {std::to_string(m_page + 1), std::to_string(pages)}), Align::Centre, Tone::Dim);
        f.button(kNext, Rect{kW - 136, y, 120, 32}, ui::tr("template.next"), m_page + 1 < pages);
    }
}

std::vector<ui::DrawItem> TemplateWindow::draw(const TemplatesModel& model,
                                               const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

std::optional<TemplateRequest> TemplateWindow::commit() {
    const std::string name = clean_template_name(m_text);
    if (m_mode == Mode::Save) {
        if (name.empty()) {
            return std::nullopt;
        }
        close();
        return TemplateRequest{TemplateRequest::Kind::Save, 0, name};
    }
    if (m_renaming) {
        const std::size_t index = *m_renaming;
        m_renaming.reset();
        m_form.set_focus(std::nullopt);
        if (!name.empty()) {
            return TemplateRequest{TemplateRequest::Kind::Rename, index, name};
        }
    }
    return std::nullopt;
}

std::optional<TemplateRequest> TemplateWindow::mouse(std::int32_t x, std::int32_t y, bool down,
                                                     const TemplatesModel& model) {
    if (m_mode == Mode::Closed) {
        return std::nullopt;
    }
    layout(model);
    const auto id = m_form.mouse(x, y, down);
    if (!id) {
        return std::nullopt;
    }
    switch (*id) {
        case kClose:
        case kCancel:
            close();
            return std::nullopt;
        case kSave:
            return commit();
        case kPrevious:
            m_page = m_page > 0 ? m_page - 1 : 0;
            m_removing.reset();
            return std::nullopt;
        case kNext:
            ++m_page;
            m_removing.reset();
            return std::nullopt;
        case kField:
            return std::nullopt;
        default:
            break;
    }
    if (*id < kRowBase) {
        return std::nullopt;
    }
    const auto row = static_cast<std::size_t>((*id - kRowBase) / kRowStep);
    const Id what = (*id - kRowBase) % kRowStep;
    const std::size_t index = m_page * kRows + row;
    if (index >= model.entries.size()) {
        return std::nullopt;
    }
    if (what != kRemove) {
        m_removing.reset();
    }
    switch (what) {
        case kTake:
            return TemplateRequest{TemplateRequest::Kind::Take, index, {}};
        case kRename:
            if (m_renaming == index) {
                return commit();
            }
            m_renaming = index;
            m_text = model.entries[index].name;
            m_form.set_focus(row_id(row, kRowField));
            m_form.select_all();
            return std::nullopt;
        case kRemove:
            m_renaming.reset();
            m_form.set_focus(std::nullopt);
            if (m_removing == index) {
                m_removing.reset();
                return TemplateRequest{TemplateRequest::Kind::Remove, index, {}};
            }
            m_removing = index;  // a second click deletes it
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

void TemplateWindow::type(std::string_view text) {
    if (!typing()) {
        return;
    }
    if (!text.empty() && m_form.take_all_selected()) {
        m_text.clear();  // typed over the whole name (Ctrl+A, or the name offered)
    }
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && m_text.size() < kMaxTemplateName) {
            m_text.push_back(c);
        }
    }
}

std::optional<TemplateRequest> TemplateWindow::key(int key) {
    if (!typing()) {
        return std::nullopt;
    }
    if (key == 1) {
        m_form.select_all();
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
            return std::nullopt;
        case 13:
            return commit();
        case 27:
            if (m_mode == Mode::Save) {
                close();
            } else {
                m_renaming.reset();
                m_form.set_focus(std::nullopt);
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

}  // namespace o1602::app
