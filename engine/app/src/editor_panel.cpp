// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor_panel.hpp"

#include <algorithm>
#include <cctype>

#include "o1602/sim/limits.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using ui::Align;
using ui::Rect;

// The page of the document.
constexpr Id kNew = 1;
constexpr Id kOpen = 2;
constexpr Id kSave = 3;
constexpr Id kSaveAs = 4;
constexpr Id kUndo = 5;
constexpr Id kRedo = 6;
constexpr Id kClose = 7;
constexpr Id kView = 8;
constexpr Id kCheck = 9;
// The other pages.
constexpr Id kCancel = 10;
constexpr Id kCreate = 11;
constexpr Id kSize = 12;
constexpr Id kIslands = 13;
constexpr Id kPlayers = 14;
constexpr Id kReroll = 15;
constexpr Id kField = 16;
constexpr Id kStore = 17;
constexpr Id kPrevious = 18;
constexpr Id kNext = 19;
constexpr Id kBack = 20;
constexpr Id kFirstOverlay = 30;  ///< + EditorOverlay
constexpr Id kFirstRow = 100;     ///< + the row on the page

constexpr std::int32_t kX = 12;
constexpr std::int32_t kW = EditorPanel::kWidth - 2 * kX;
constexpr std::int32_t kButtonH = 30;
constexpr std::int32_t kGap = 6;
constexpr std::int32_t kLine = 24;
constexpr std::int32_t kRowH = 24;
constexpr std::int32_t kNote = 100;  ///< a note of up to five lines

constexpr std::array<const char*, kEditorOverlays> kOverlayTexts = {"editor.view.owners", "editor.view.island_numbers",
                                                                    "editor.view.coordinates"};

constexpr std::array<const char*, 4> kIslandTexts = {"editor.new.islands.none", "editor.new.islands.few",
                                                     "editor.new.islands.normal", "editor.new.islands.many"};

std::string count(std::size_t n) {
    return std::to_string(n);
}

/// The next value of a cycle of `n` values, forward or back.
std::size_t step(std::size_t value, std::size_t n, bool forward) {
    return forward ? (value + 1) % n : (value + n - 1) % n;
}

}  // namespace

std::string EditorPanel::clean_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        // What Windows refuses in a file's name, and the control characters.
        if (u < 0x20 || u == 0x7F || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos) {
            continue;
        }
        out.push_back(c);
    }
    const auto first = out.find_first_not_of(" .");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = out.find_last_not_of(" .");
    out = out.substr(first, last - first + 1);
    if (out.size() > kMaxName) {
        out.resize(kMaxName);
    }
    return out;
}

bool EditorPanel::name_taken(const EditorModel& model, std::string_view name) {
    const auto lower = [](std::string_view s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };
    const std::string wanted = lower(name);
    return std::any_of(model.own_names.begin(), model.own_names.end(),
                       [&](const std::string& n) { return lower(n) == wanted; });
}

void EditorPanel::show_document() noexcept {
    m_page = Page::Document;
    m_form.set_focus(std::nullopt);
}

void EditorPanel::ask_name(std::string_view name) {
    m_page = Page::SaveAs;
    m_text = clean_name(name);
    m_form.set_focus(kField);
    m_form.select_all();  // what is typed replaces the name offered
}

bool EditorPanel::show_selection() noexcept {
    if (m_page == Page::New || m_page == Page::Open || m_page == Page::SaveAs) {
        return false;
    }
    m_page = Page::Selection;
    m_form.set_focus(std::nullopt);
    return true;
}

void EditorPanel::ask_new(const EditorNewMap& map) {
    m_page = Page::New;
    m_new = map;
    m_new.players = std::clamp<std::uint8_t>(m_new.players, 1, static_cast<std::uint8_t>(sim::kMaxPlayers));
    m_form.set_focus(std::nullopt);
}

bool EditorPanel::back() noexcept {
    if (m_page == Page::Document) {
        return false;
    }
    show_document();
    return true;
}

void EditorPanel::layout(const EditorModel& model) {
    m_form.clear();
    m_form.panel(Rect{kX - 6, m_top, kWidth - 2 * (kX - 6), m_height});
    switch (m_page) {
        case Page::Document:
            layout_document(model);
            break;
        case Page::New:
            layout_new();
            break;
        case Page::Open:
            layout_open(model);
            break;
        case Page::SaveAs:
            layout_save(model);
            break;
        case Page::Selection:
            layout_selection(model);
            break;
        case Page::View:
            layout_view(model);
            break;
        case Page::Check:
            layout_check(model);
            break;
    }
}

void EditorPanel::layout_document(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 4;
    f.label(Rect{kX, y, kW, 24}, ui::tr("editor.title"), Align::Left, Tone::Heading);
    y += 26;
    std::string file = model.file.empty() ? ui::tr("editor.untitled") : model.file;
    if (model.modified) {
        file += " *";
    }
    f.label(Rect{kX, y, kW, kLine}, file);
    y += kLine;
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.map_size"),
            ui::tr("editor.size", {count(model.width), count(model.height)}), Tone::Dim);
    y += kLine;
    if (model.planned_islands > 0) {
        f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.islands"),
                ui::tr("editor.islands_planned", {count(model.planned_islands)}), Tone::Dim);
    } else {
        f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.islands"), count(model.islands), Tone::Dim);
    }
    y += kLine;
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.players"), count(model.players), Tone::Dim);
    y += kLine + 4;
    // The buttons share what is left above two lines of the notice: as high as the game's in a
    // large window, lower in a small one, so that the last stays in the panel.
    constexpr std::int32_t kButtons = 9;
    constexpr std::int32_t kNoticeRoom = 2 * 20 + 6;
    const std::int32_t bottom = m_top + m_height - 4;
    const std::int32_t pitch = std::clamp((bottom - kNoticeRoom - y) / kButtons, 22, kButtonH + kGap);
    const std::int32_t gap = std::min(kGap, std::max(2, pitch / 8));
    const auto full = [&](Id id, const char* text, bool enabled = true) {
        f.button(id, Rect{kX, y, kW, pitch - gap}, ui::tr(text), enabled);
        y += pitch;
    };
    full(kNew, "editor.new");
    full(kOpen, "editor.open");
    full(kSave, "editor.save");
    full(kSaveAs, "editor.save_as");
    full(kUndo, "editor.undo", model.can_undo);
    full(kRedo, "editor.redo", model.can_redo);
    full(kView, "editor.view");
    full(kCheck, "editor.check");
    full(kClose, "editor.close");
    if (!model.notice.empty()) {
        f.paragraph(Rect{kX, y + 2, kW, std::max(0, bottom - y - 2)}, model.notice,
                    model.notice_is_error ? Tone::Warning : Tone::Dim, 20);
    }
}

void EditorPanel::layout_new() {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.new.title"), Align::Left, Tone::Heading);
    y += 34;
    const auto value = [&](Id id, const char* label, std::string text, bool enabled = true) {
        f.label(Rect{kX, y, kW, kLine}, ui::tr(label), Align::Left, enabled ? Tone::Normal : Tone::Dim);
        y += kLine;
        f.cycle_button(id, Rect{kX, y, kW, kButtonH}, std::move(text), enabled);
        y += kButtonH + kGap + 2;
    };
    std::string size = ui::tr("editor.size", {count(m_new.width()), count(m_new.height())});
    if (m_new.size == 0) {
        size = ui::tr("editor.new.size_original", {size});
    }
    value(kSize, "editor.new.size", std::move(size));
    value(kIslands, "editor.new.islands", ui::tr(kIslandTexts[static_cast<std::size_t>(m_new.islands)]));
    const bool islands = m_new.islands != EditorNewMap::Islands::None;
    value(kPlayers, "editor.new.players", count(m_new.players), islands);
    f.entry(Rect{kX, y, kW, kLine}, ui::tr("editor.new.seed"), std::to_string(m_new.seed),
            islands ? Tone::Normal : Tone::Dim, Tone::Dim);
    y += kLine + 2;
    f.button(kReroll, Rect{kX, y, kW, kButtonH}, ui::tr("editor.new.reroll"), islands);
    y += kButtonH + kGap;
    f.paragraph(Rect{kX, y, kW, kNote}, ui::tr(islands ? "editor.new.note_islands" : "editor.new.note_sea"), Tone::Dim,
                20);
    y += kNote + 4;
    const std::int32_t half = (kW - kGap) / 2;
    f.button(kCreate, Rect{kX, y, half, kButtonH}, ui::tr("editor.new.create"));
    f.button(kCancel, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_open(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.open.title"), Align::Left, Tone::Heading);
    y += 32;
    if (model.files.empty()) {
        f.paragraph(Rect{kX, y, kW, 60}, ui::tr("editor.open.empty"), Tone::Dim, 20);
        y += 64;
    } else {
        const std::size_t n = model.files.size();
        m_first = std::min(m_first, n > kRows ? n - kRows : 0);
        std::string group;  // every page begins with the heading of its first file's group
        std::size_t shown = 0;
        for (std::size_t i = m_first; i < n && shown < kRows; ++i) {
            const EditorFile& file = model.files[i];
            if (file.group != group) {
                group = file.group;
                f.label(Rect{kX, y, kW, kRowH}, group, Align::Left, Tone::Heading);
                y += kRowH;
            }
            const auto row = static_cast<Id>(i - m_first);
            f.row(kFirstRow + row, Rect{kX, y, kW, kRowH - 2}, false);
            f.label(Rect{kX + 4, y, kW - 8, kRowH - 2}, file.name);
            y += kRowH;
            ++shown;
        }
        y += 4;
        const std::int32_t half = (kW - kGap) / 2;
        f.button(kPrevious, Rect{kX, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
        f.button(kNext, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.next"), m_first + kRows < n);
        y += kButtonH + kGap;
    }
    f.button(kCancel, Rect{kX, y, kW, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_save(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.save.title"), Align::Left, Tone::Heading);
    y += 34;
    f.label(Rect{kX, y, kW, kLine}, ui::tr("editor.save.name"));
    y += kLine;
    f.field(kField, Rect{kX, y, kW, 32}, m_text);
    y += 38;
    const std::string name = clean_name(m_text);
    const bool taken = !name.empty() && name_taken(model, name);
    f.paragraph(Rect{kX, y, kW, kNote}, ui::tr(taken ? "editor.save.taken" : "editor.save.where"),
                taken ? Tone::Warning : Tone::Dim, 20);
    y += kNote + 4;
    const std::int32_t half = (kW - kGap) / 2;
    f.button(kStore, Rect{kX, y, half, kButtonH}, ui::tr(taken ? "editor.save.replace" : "editor.save"), !name.empty());
    f.button(kCancel, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.cancel"));
}

void EditorPanel::layout_selection(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.selection.title"), Align::Left, Tone::Heading);
    y += 30;
    if (model.selection.empty()) {
        f.paragraph(Rect{kX, y, kW, 3 * 20}, ui::tr("editor.selection.none"), Tone::Dim, 20);
        y += 3 * 20 + 4;
    } else {
        f.paragraph(Rect{kX, y, kW, 2 * kLine}, model.selection, Tone::Normal, kLine);
        y += 2 * kLine + 4;
        const std::int32_t bottom = m_top + m_height - kButtonH - 2 * kGap;
        for (const EditorProperty& p : model.properties) {
            if (y + kLine > bottom) {
                break;
            }
            f.entry(Rect{kX, y, kW, kLine}, p.label, p.value, Tone::Dim);
            y += kLine;
        }
        y += kGap;
    }
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_view(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.view.title"), Align::Left, Tone::Heading);
    y += 34;
    for (std::size_t i = 0; i < kEditorOverlays; ++i) {
        f.check(kFirstOverlay + static_cast<Id>(i), Rect{kX, y, kW, kButtonH}, ui::tr(kOverlayTexts[i]),
                model.overlays[i]);
        y += kButtonH + kGap;
    }
    y += kGap;
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

void EditorPanel::layout_check(const EditorModel& model) {
    ui::Form& f = m_form;
    std::int32_t y = m_top + 6;
    f.label(Rect{kX, y, kW, 28}, ui::tr("editor.check.title"), Align::Left, Tone::Heading);
    y += 32;
    // Each line gets three lines of text, a heading two; as many as the room holds above the
    // buttons, the rest on the next pages.
    constexpr std::int32_t kItem = 3 * 20;
    constexpr std::int32_t kHeading = 2 * 20 + 2;
    const std::int32_t bottom = m_top + m_height - 2 * (kButtonH + kGap) - 6;
    const std::size_t n = model.check.size();
    m_first = std::min(m_first, n == 0 ? 0 : n - 1);
    std::size_t i = m_first;
    for (; i < n; ++i) {
        const EditorCheckLine& line = model.check[i];
        using Kind = EditorCheckLine::Kind;
        const std::int32_t h = line.kind == Kind::Heading ? kHeading : kItem;
        if (y + h > bottom && i > m_first) {
            break;
        }
        if (line.kind == Kind::Heading) {
            f.paragraph(Rect{kX, y, kW, kHeading - 2}, line.text, Tone::Heading, 20);
        } else {
            const Tone tone = line.kind == Kind::Error || line.kind == Kind::Warning ? Tone::Warning
                              : line.kind == Kind::Fine                              ? Tone::Normal
                                                                                     : Tone::Dim;
            f.paragraph(Rect{kX + 8, y, kW - 8, kItem}, line.text, tone, 20);
        }
        y += h;
    }
    m_page_rows = std::max<std::size_t>(1, i - m_first);
    y += 4;
    if (m_first > 0 || i < n) {
        const std::int32_t half = (kW - kGap) / 2;
        f.button(kPrevious, Rect{kX, y, half, kButtonH}, ui::tr("editor.previous"), m_first > 0);
        f.button(kNext, Rect{kX + half + kGap, y, half, kButtonH}, ui::tr("editor.next"), i < n);
        y += kButtonH + kGap;
    }
    f.button(kBack, Rect{kX, y, kW, kButtonH}, ui::tr("editor.back"));
}

std::vector<ui::DrawItem> EditorPanel::draw(const EditorModel& model,
                                            const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

std::optional<EditorRequest> EditorPanel::commit_name() {
    std::string name = clean_name(m_text);
    if (name.empty()) {
        return std::nullopt;
    }
    show_document();
    EditorRequest r{EditorRequest::Kind::SaveAs};
    r.name = std::move(name);
    return r;
}

std::optional<EditorRequest> EditorPanel::mouse(std::int32_t x, std::int32_t y, bool down, const EditorModel& model,
                                                ui::MouseButton button) {
    layout(model);
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        return std::nullopt;
    }
    const bool forward = m_form.clicked_with() == ui::MouseButton::Left;
    switch (*id) {
        case kNew:
            ask_new(m_new);
            return std::nullopt;
        case kOpen:
            m_page = Page::Open;
            m_first = 0;
            return EditorRequest{EditorRequest::Kind::ListFiles};
        case kSave:
            return EditorRequest{EditorRequest::Kind::Save};
        case kSaveAs:
            ask_name(model.file);
            return std::nullopt;
        case kUndo:
            return EditorRequest{EditorRequest::Kind::Undo};
        case kRedo:
            return EditorRequest{EditorRequest::Kind::Redo};
        case kClose:
            return EditorRequest{EditorRequest::Kind::Close};
        case kView:
            m_page = Page::View;
            return std::nullopt;
        case kCheck:
            m_page = Page::Check;
            m_first = 0;
            return EditorRequest{EditorRequest::Kind::Check};
        case kBack:
            show_document();
            return std::nullopt;
        case kCancel:
            show_document();
            return std::nullopt;
        case kSize:
            m_new.size = step(m_new.size, EditorNewMap::kSizes.size(), forward);
            return std::nullopt;
        case kIslands:
            m_new.islands = static_cast<EditorNewMap::Islands>(
                step(static_cast<std::size_t>(m_new.islands), kIslandTexts.size(), forward));
            return std::nullopt;
        case kPlayers:
            m_new.players = static_cast<std::uint8_t>(step(m_new.players - 1U, sim::kMaxPlayers, forward) + 1U);
            return std::nullopt;
        case kReroll:
            // Another world: the next number of a simple generator (Knuth's MMIX constants).
            m_new.seed = m_new.seed * 6364136223846793005ULL + 1442695040888963407ULL;
            return std::nullopt;
        case kCreate: {
            show_document();
            EditorRequest r{EditorRequest::Kind::New};
            r.map = m_new;
            return r;
        }
        case kStore:
            return commit_name();
        case kPrevious: {
            // The check list goes back by the lines its page shows (they differ in height).
            const std::size_t rows = m_page == Page::Check ? m_page_rows : kRows;
            m_first = m_first > rows ? m_first - rows : 0;
            return std::nullopt;
        }
        case kNext:
            m_first += m_page == Page::Check ? m_page_rows : kRows;
            return std::nullopt;
        default:
            break;
    }
    if (*id >= kFirstOverlay && *id < kFirstOverlay + static_cast<Id>(kEditorOverlays)) {
        EditorRequest r{EditorRequest::Kind::Overlay};
        r.index = static_cast<std::size_t>(*id - kFirstOverlay);
        return r;
    }
    if (*id >= kFirstRow && m_page == Page::Open) {
        const std::size_t index = m_first + static_cast<std::size_t>(*id - kFirstRow);
        if (index < model.files.size()) {
            show_document();
            EditorRequest r{EditorRequest::Kind::Open};
            r.index = index;
            return r;
        }
    }
    return std::nullopt;
}

void EditorPanel::type(std::string_view text) {
    if (!typing()) {
        return;
    }
    if (!text.empty() && m_form.take_all_selected()) {
        m_text.clear();  // typed over the whole name (Ctrl+A, or the name offered)
    }
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F && m_text.size() < kMaxName) {
            m_text.push_back(c);
        }
    }
}

std::optional<EditorRequest> EditorPanel::key(int key) {
    if (!typing()) {
        if (key == 27) {
            (void)back();
        }
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
            return commit_name();
        case 27:
            show_document();
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

}  // namespace o1602::app
