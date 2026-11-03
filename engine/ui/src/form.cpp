// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/form.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace o1602::ui {

namespace {

constexpr std::int32_t kBorder = 2;  ///< frame width, design units
constexpr std::int32_t kPadding = kTextPadding;
constexpr std::int32_t kBox = 18;       ///< the box of a check box
constexpr double kFallbackText = 18;    ///< text height without a font
constexpr std::int32_t kEntryGap = 12;  ///< between an entry's label and its value
constexpr std::int32_t kKnob = 14;      ///< a slider's knob, wide
constexpr std::int32_t kTrack = 6;      ///< a slider's track, high

/// Width and height of text in design units.
double text_width(const Font* font, std::string_view s) {
    return font != nullptr ? font->width(s) / font->design_scale() : 9.0 * static_cast<double>(s.size());
}

Form::Widget make(Form::Widget::Kind kind, Form::Id id, Rect r, std::string text = {}) {
    Form::Widget w;
    w.kind = kind;
    w.id = id;
    w.rect = r;
    w.text = std::move(text);
    return w;
}

}  // namespace

std::vector<std::string> wrap_lines(const Font* font, std::string_view text, double width) {
    std::vector<std::string> lines;
    std::string line;
    bool open = false;  // a line is begun (it may still be empty)
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = std::min(text.find(' ', start), text.size());
        std::string_view word = text.substr(start, end - start);
        for (;;) {
            if (!open) {
                // The word begins the line; wider than a whole line, it is cut where the line is
                // full (at least one character).
                if (word.empty() || text_width(font, word) <= width) {
                    line.assign(word);
                    open = true;
                    break;
                }
                std::size_t n = 1;
                while (n < word.size() && text_width(font, word.substr(0, n + 1)) <= width) {
                    ++n;
                }
                lines.emplace_back(word.substr(0, n));
                word.remove_prefix(n);
                if (word.empty()) {
                    break;  // the word's last piece filled a line
                }
                continue;
            }
            std::string candidate = line;
            candidate += ' ';
            candidate += word;
            if (text_width(font, candidate) <= width) {
                line = std::move(candidate);
                break;
            }
            if (text_width(font, word) > width) {
                // A word wider than any line starts right here, as much of it as still fits.
                std::size_t n = 0;
                while (n < word.size() && text_width(font, line + ' ' + std::string(word.substr(0, n + 1))) <= width) {
                    ++n;
                }
                if (n > 0) {
                    line += ' ';
                    line += word.substr(0, n);
                    word.remove_prefix(n);
                }
            }
            lines.push_back(std::exchange(line, {}));
            open = false;
            if (word.empty()) {
                break;
            }
        }
        if (end >= text.size()) {
            break;
        }
        start = end + 1;
    }
    if (open) {
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string fit_text(const Font* font, std::string s, double width, bool keep_end) {
    if (text_width(font, s) <= width) {
        return s;
    }
    // Binary search for the cut: shorter text is never wider. The font measures up to the first
    // control character, so the start may move up to it at most (the rest is then 0 wide).
    if (keep_end) {
        const auto control =
            std::find_if(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
        std::size_t lo = 0;
        std::size_t hi = font != nullptr ? static_cast<std::size_t>(control - s.begin()) : s.size();
        if (text_width(font, std::string_view(s).substr(hi)) > width) {
            return {};
        }
        while (lo < hi) {  // the first start whose rest fits
            const std::size_t mid = lo + (hi - lo) / 2;
            if (text_width(font, std::string_view(s).substr(mid)) <= width) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return s.substr(lo);
    }
    std::string probe;
    const auto fits = [&](std::size_t n) {
        probe.assign(s, 0, n);
        probe += "...";
        return text_width(font, probe) <= width;
    };
    std::size_t lo = 0;
    std::size_t hi = s.size();
    while (lo < hi) {  // the longest start that fits with the dots, 0 if none does
        const std::size_t mid = lo + (hi - lo + 1) / 2;
        if (fits(mid)) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    s.resize(lo);
    return s + "...";
}

std::int32_t text_bottom(const Font* font, std::int32_t y, std::int32_t height) {
    if (font == nullptr) {
        return y + static_cast<std::int32_t>((height + kFallbackText) / 2);
    }
    return y + static_cast<std::int32_t>(std::lround(height / 2.0 + font->ink_drop() / font->design_scale()));
}

void Form::panel(Rect r) {
    m_widgets.push_back(make(Widget::Kind::Panel, -1, r));
}

void Form::swatch(Rect r, std::int32_t colour) {
    Widget w = make(Widget::Kind::Swatch, -1, r);
    w.colour = colour;
    m_widgets.push_back(std::move(w));
}

void Form::label(Rect r, std::string text, Align align, Tone tone) {
    Widget w = make(Widget::Kind::Label, -1, r, std::move(text));
    w.align = align;
    w.tone = tone;
    m_widgets.push_back(std::move(w));
}

void Form::speech(Rect r, std::string speaker, std::int32_t ink, std::string text, Tone tone) {
    Widget w = make(Widget::Kind::Speech, -1, r, std::move(speaker));
    w.value = std::move(text);
    w.ink = ink;
    w.tone = tone;
    m_widgets.push_back(std::move(w));
}

void Form::entry(Rect r, std::string label, std::string value, Tone label_tone, Tone value_tone) {
    Widget w = make(Widget::Kind::Entry, -1, r, std::move(label));
    w.value = std::move(value);
    w.tone = label_tone;
    w.value_tone = value_tone;
    m_widgets.push_back(std::move(w));
}

void Form::paragraph(Rect r, std::string text, Tone tone, std::int32_t line, std::string fallback) {
    Widget w = make(Widget::Kind::Paragraph, -1, r, std::move(text));
    w.value = std::move(fallback);
    w.tone = tone;
    w.line = std::max(line, 1);
    m_widgets.push_back(std::move(w));
}

void Form::button(Id id, Rect r, std::string text, bool enabled, bool selected) {
    Widget w = make(Widget::Kind::Button, id, r, std::move(text));
    w.align = Align::Centre;
    w.enabled = enabled;
    w.on = selected;
    m_widgets.push_back(std::move(w));
}

void Form::cycle_button(Id id, Rect r, std::string text, bool enabled) {
    button(id, r, std::move(text), enabled);
    m_widgets.back().cycles = true;
}

void Form::field(Id id, Rect r, const std::string& text, bool secret) {
    Widget w = make(Widget::Kind::Field, id, r, secret ? std::string(text.size(), '*') : text);
    w.on = secret;
    m_widgets.push_back(std::move(w));
}

void Form::check(Id id, Rect r, std::string text, bool on, bool enabled) {
    Widget w = make(Widget::Kind::Check, id, r, std::move(text));
    w.on = on;
    w.enabled = enabled;
    m_widgets.push_back(std::move(w));
}

void Form::row(Id id, Rect r, bool selected) {
    Widget w = make(Widget::Kind::Row, id, r);
    w.on = selected;
    m_widgets.push_back(std::move(w));
}

void Form::slider(Id id, Rect r, double position, bool enabled) {
    Widget w = make(Widget::Kind::Slider, id, r);
    w.position = std::isfinite(position) ? std::clamp(position, 0.0, 1.0) : 0.0;
    w.enabled = enabled;
    m_widgets.push_back(std::move(w));
}

void Form::progress(Rect r, double done) {
    Widget w = make(Widget::Kind::Progress, -1, r);
    w.position = std::isfinite(done) ? std::clamp(done, 0.0, 1.0) : 0.0;
    m_widgets.push_back(std::move(w));
}

const Form::Widget* Form::held_slider() const noexcept {
    if (!m_pressed || *m_pressed < 0 || m_pressed_with != MouseButton::Left) {
        return nullptr;
    }
    for (const Widget& w : m_widgets) {
        if (w.id == *m_pressed && w.kind == Widget::Kind::Slider) {
            return &w;
        }
    }
    return nullptr;
}

double Form::slider_position(const Widget& slider, std::int32_t x) const noexcept {
    // The knob's middle follows the mouse, from half a knob in from either end.
    const double room = slider.rect.w - kKnob;
    if (room <= 0.0) {
        return 0.0;
    }
    return std::clamp((x / m_scale - slider.rect.x - kKnob / 2.0) / room, 0.0, 1.0);
}

std::optional<Form::Id> Form::hit(std::int32_t x, std::int32_t y, MouseButton button) const {
    // In design units; the last widget laid out is on top. The right button passes through the
    // widgets it does not click.
    const auto dx = static_cast<std::int32_t>(x / m_scale);
    const auto dy = static_cast<std::int32_t>(y / m_scale);
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it) {
        if (it->id >= 0 && it->enabled && it->rect.contains(dx, dy)) {
            if (button == MouseButton::Right && !it->cycles) {
                return std::nullopt;
            }
            return it->id;
        }
    }
    return std::nullopt;
}

std::optional<Form::Id> Form::mouse(std::int32_t x, std::int32_t y, bool down, MouseButton button) {
    m_hovered = hit(x, y, MouseButton::Left);
    m_released = false;
    if (m_pressed && button != m_pressed_with) {
        return std::nullopt;  // the other button while one is held
    }
    const std::optional<Id> under = hit(x, y, button);
    if (down) {
        if (!m_pressed) {
            m_pressed = under ? under : std::optional<Id>(-1);  // -1: pressed on nothing
            m_pressed_with = button;
        }
        if (const Widget* s = held_slider()) {
            m_slide = slider_position(*s, x);
        }
        return std::nullopt;
    }
    if (const Widget* s = held_slider()) {
        // A slider is let go wherever the mouse is: the knob stays where it was dragged.
        m_slide = slider_position(*s, x);
        m_pressed.reset();
        m_released = true;
        m_clicked_with = button;
        return s->id;
    }
    const std::optional<Id> pressed = std::exchange(m_pressed, std::nullopt);
    m_released = pressed.has_value();
    if (pressed && under && *pressed == *under) {
        m_clicked_with = button;
        return under;
    }
    return std::nullopt;
}

std::int32_t Form::colour_of(Tone tone) const noexcept {
    switch (tone) {
        case Tone::Heading:
            return m_style.heading;
        case Tone::Dim:
            return m_style.dim;
        case Tone::Warning:
            return m_style.warning;
        case Tone::Normal:
            break;
    }
    return m_style.text;
}

const Font* Form::font(const std::array<const Font*, kFontSlots>& fonts) const noexcept {
    return m_style.font >= 0 && m_style.font < static_cast<std::int32_t>(kFontSlots)
               ? fonts[static_cast<std::size_t>(m_style.font)]
               : nullptr;
}

std::vector<DrawItem> Form::draw(const std::array<const Font*, kFontSlots>& fonts) const {
    const Font* font = this->font(fonts);
    const double s = m_scale;
    const auto px = [s](std::int32_t v) { return to_pixels(v, s); };
    std::vector<DrawItem> out;
    const auto fill = [&](Rect r, std::int32_t colour, bool ground = false) {
        DrawItem d;
        d.kind = DrawItem::Kind::Fill;
        d.x = px(r.x);
        d.y = px(r.y);
        d.w = px(r.x + r.w) - d.x;
        d.h = px(r.y + r.h) - d.y;
        d.colour = colour;
        d.half = ground && m_style.translucent;
        out.push_back(d);
    };
    const auto framed = [&](Rect r, std::uint8_t inside) {
        if (m_style.translucent) {
            // Only the outline, the parchment shows through the ground.
            fill(Rect{r.x, r.y, r.w, kBorder}, m_style.frame);
            fill(Rect{r.x, r.y + r.h - kBorder, r.w, kBorder}, m_style.frame);
            fill(Rect{r.x, r.y + kBorder, kBorder, r.h - 2 * kBorder}, m_style.frame);
            fill(Rect{r.x + r.w - kBorder, r.y + kBorder, kBorder, r.h - 2 * kBorder}, m_style.frame);
        } else {
            fill(r, m_style.frame);
        }
        fill(Rect{r.x + kBorder, r.y + kBorder, r.w - 2 * kBorder, r.h - 2 * kBorder}, inside, true);
    };
    const auto text = [&](Rect r, std::string t, Align align, std::int32_t colour, bool keep_end = false) {
        const double room = r.w - 2.0 * kPadding;
        DrawItem d;
        d.kind = DrawItem::Kind::Text;
        d.block = m_style.font;
        d.colour = colour;
        d.align = align;
        d.text = fit_text(font, std::move(t), std::max(room, 0.0), keep_end);
        const std::int32_t x = align == Align::Left    ? r.x + kPadding
                               : align == Align::Right ? r.x + r.w - kPadding
                                                       : r.x + r.w / 2;
        d.x = px(x);
        d.y = px(text_bottom(font, r.y, r.h));
        out.push_back(std::move(d));
    };
    const auto raster = [&](Rect r) {
        DrawItem d;
        d.kind = DrawItem::Kind::Raster;
        d.x = px(r.x);
        d.y = px(r.y);
        d.w = px(r.x + r.w) - d.x;
        d.h = px(r.y + r.h) - d.y;
        out.push_back(d);
    };
    const bool held = m_pressed && *m_pressed >= 0;
    const Widget* sliding = held_slider();
    for (const Widget& w : m_widgets) {
        const bool hover = w.id >= 0 && m_hovered == w.id && w.enabled;
        switch (w.kind) {
            case Widget::Kind::Panel:
                framed(w.rect, m_style.ground);
                break;
            case Widget::Kind::Swatch:
                fill(w.rect, w.colour);
                break;
            case Widget::Kind::Label:
                text(w.rect, w.text, w.align, colour_of(w.tone));
                break;
            case Widget::Kind::Speech: {
                // Cut as one line; the name in its ink (all of it, when the cut reaches into the
                // name), the rest right behind it.
                text(w.rect, w.text + w.value, Align::Left, colour_of(w.tone));
                DrawItem& line = out.back();
                const bool whole_name = line.text.compare(0, w.text.size(), w.text) == 0;
                const std::size_t split = whole_name ? w.text.size() : line.text.size();
                DrawItem rest = line;
                rest.text = line.text.substr(split);
                line.text.resize(split);
                line.ink = w.ink;
                rest.x = line.x + static_cast<std::int32_t>(std::lround(text_width(font, line.text) * s));
                if (!rest.text.empty()) {
                    out.push_back(std::move(rest));
                }
                break;
            }
            case Widget::Kind::Entry: {
                // The value first, on the right; the label in what is left beside it.
                const std::string value = fit_text(font, w.value, std::max(w.rect.w - 2.0 * kPadding, 0.0), false);
                const auto used = static_cast<std::int32_t>(std::ceil(text_width(font, value))) + kEntryGap;
                text(w.rect, value, Align::Right, colour_of(w.value_tone));
                text(Rect{w.rect.x, w.rect.y, std::max(w.rect.w - used, 0), w.rect.h}, w.text, Align::Left,
                     colour_of(w.tone));
                break;
            }
            case Widget::Kind::Paragraph: {
                std::vector<std::string> lines;
                if (font != nullptr) {
                    const double room = std::max(0.0, (w.rect.w - 2.0 * kPadding) * font->design_scale());
                    lines = wrap_text(*font, w.text, static_cast<std::uint32_t>(room));
                    const auto fit = static_cast<std::size_t>(std::max(w.rect.h / w.line, 0));
                    if (lines.size() > fit && !w.value.empty()) {
                        lines = wrap_text(*font, w.value, static_cast<std::uint32_t>(room));
                    }
                } else {
                    lines = {w.text};
                }
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    const auto y = w.rect.y + static_cast<std::int32_t>(i) * w.line;
                    if (y + w.line > w.rect.y + w.rect.h) {
                        break;
                    }
                    text(Rect{w.rect.x, y, w.rect.w, w.line}, lines[i], Align::Left, colour_of(w.tone));
                }
                break;
            }
            case Widget::Kind::Button: {
                const bool pressed = hover && held && *m_pressed == w.id;
                framed(w.rect, w.on || pressed ? m_style.selected : hover ? m_style.hover : m_style.ground);
                // On a parchment a button that cannot be pressed is only faded; elsewhere rastered.
                const bool faded = !w.enabled && m_style.translucent;
                text(w.rect, w.text, Align::Centre, faded ? m_style.dim : w.on ? m_style.heading : m_style.text);
                if (!w.enabled && !faded) {
                    raster(w.rect);
                }
                break;
            }
            case Widget::Kind::Field: {
                const bool focused = m_focus == w.id;
                const bool all = focused && m_all_selected && !w.text.empty();
                framed(w.rect, all ? m_style.selected : m_style.field);
                text(w.rect, focused && !all ? w.text + "_" : w.text, Align::Left, m_style.text, focused);
                break;
            }
            case Widget::Kind::Check: {
                const Rect box{w.rect.x, w.rect.y + (w.rect.h - kBox) / 2, kBox, kBox};
                framed(box, hover ? m_style.hover : m_style.field);
                if (w.on) {
                    fill(Rect{box.x + 5, box.y + 5, kBox - 10, kBox - 10}, m_style.frame);
                }
                text(Rect{w.rect.x + kBox, w.rect.y, w.rect.w - kBox, w.rect.h}, w.text, Align::Left,
                     w.enabled ? m_style.text : m_style.dim);
                break;
            }
            case Widget::Kind::Row:
                if (w.on || hover) {
                    fill(w.rect, w.on ? m_style.selected : m_style.hover, true);
                }
                break;
            case Widget::Kind::Slider: {
                // A thin track across the box and the knob on it; held, the knob is where the
                // mouse drags it.
                const double position = sliding == &w ? m_slide : w.position;
                framed(Rect{w.rect.x, w.rect.y + (w.rect.h - kTrack) / 2, w.rect.w, kTrack}, m_style.field);
                const auto knob_x = w.rect.x + static_cast<std::int32_t>(std::lround(position * (w.rect.w - kKnob)));
                fill(Rect{knob_x, w.rect.y, kKnob, w.rect.h}, m_style.frame);
                if (!w.enabled) {
                    raster(w.rect);
                }
                break;
            }
            case Widget::Kind::Progress: {
                // The ground of a field, and the part done in gold, a gap inside its frame.
                framed(w.rect, m_style.field);
                constexpr std::int32_t inset = kBorder + 2;
                const std::int32_t room = std::max(w.rect.w - 2 * inset, 0);
                const auto done = static_cast<std::int32_t>(std::lround(w.position * room));
                if (done > 0) {
                    fill(Rect{w.rect.x + inset, w.rect.y + inset, done, std::max(w.rect.h - 2 * inset, 0)},
                         m_style.frame);
                }
                break;
            }
        }
    }
    return out;
}

}  // namespace o1602::ui
