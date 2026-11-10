// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/ui/panel.hpp"

#include <algorithm>
#include <cmath>

#include "o1602/ui/form.hpp"
#include "o1602/ui/package_icons.hpp"

namespace o1602::ui {

namespace {

using assets::GadgetKind;

std::int32_t trunc_scaled(double v) noexcept {
    return static_cast<std::int32_t>(std::trunc(v));
}

}  // namespace

ScreenRegions screen_regions(std::int32_t width, std::int32_t height) noexcept {
    return screen_regions(width, height, static_cast<double>(width) / kDesignWidth);
}

ScreenRegions screen_regions(std::int32_t width, std::int32_t height, double scale) noexcept {
    ScreenRegions r;
    r.scale = scale;
    const std::int32_t panel = trunc_scaled(256.5 * r.scale);
    const std::int32_t bar = trunc_scaled(34.5 * r.scale);
    const std::int32_t tutor = trunc_scaled(140.5 * r.scale);
    r.side_panel = {width - panel, 0, panel, height};
    r.status_bar = {0, height - bar, width - panel, bar};
    r.tutor = {0, height - bar - tutor, width - panel, tutor};
    r.full = {0, 0, width, height};
    r.game_view = {0, 0, width - panel, height - bar};
    return r;
}

std::int32_t to_pixels(std::int32_t v, double scale) noexcept {
    return trunc_scaled((v + 0.5) * scale);
}

Panel::Panel(assets::GadgetLayout layout, double scale)
    : m_layout(std::move(layout)), m_state(m_layout.gadgets.size()), m_scale(scale) {}

void Panel::set_text(std::int32_t id, std::string text) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].text = std::move(text);
    }
}

void Panel::set_fit(std::int32_t id, bool on, std::optional<std::int32_t> room) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].fit = on;
        m_state[*i].fit_room = room;
    }
}

void Panel::set_selected(std::int32_t id, bool on) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].selected = on;
    }
}

bool Panel::selected(std::int32_t id) const {
    const auto i = m_layout.index_of(id);
    return i && m_state[*i].selected;
}

void Panel::set_hidden(std::int32_t id, bool on) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].hidden = on;
    }
}

void Panel::set_rows(std::int32_t id, std::int32_t first, std::int32_t count) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].first_row = std::max(first, 0);
        m_state[*i].rows = std::max(count, 0);
    }
}

void Panel::set_no_select(std::int32_t id, bool on) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].no_select = on;
    }
}

void Panel::set_visible(std::int32_t id, bool on) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].hidden = !on;
        m_state[*i].no_select = !on;
    }
}

void Panel::set_disabled(std::int32_t id, bool on) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].disabled = on;
    }
}

void Panel::set_faded(std::int32_t id, bool on) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].faded = on;
    }
}

std::int32_t Panel::slider(std::int32_t id, std::size_t axis, std::int32_t range) const {
    const auto i = m_layout.index_of(id);
    if (!i || axis > 1) {
        return 0;
    }
    const std::int32_t size = m_layout.gadgets[*i].slider_size[axis];
    return size <= 0 ? 0 : (m_state[*i].slider[axis] * range + size / 2) / size;
}

void Panel::set_slider(std::int32_t id, std::size_t axis, std::int32_t value, std::int32_t range) {
    const auto i = m_layout.index_of(id);
    if (!i || axis > 1 || range <= 0) {
        return;
    }
    const std::int32_t size = m_layout.gadgets[*i].slider_size[axis];
    m_state[*i].slider[axis] = std::clamp((value * size + range / 2) / range, 0, std::max(0, size));
}

void Panel::set_graphic(std::int32_t id, std::int32_t gfx, std::int32_t press_offset, std::optional<ColourTint> tint) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].graphic = std::make_pair(gfx, press_offset);
        m_state[*i].tint = tint;
    }
}

void Panel::set_offset(std::int32_t id, std::int32_t x, std::int32_t y) {
    if (const auto i = m_layout.index_of(id)) {
        m_layout.gadgets[*i].offset = {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)};
    }
}

void Panel::set_position(std::int32_t id, std::int32_t x, std::int32_t y) {
    if (const auto i = m_layout.index_of(id)) {
        m_layout.gadgets[*i].pos = {x, y};
    }
}

void Panel::set_size(std::int32_t id, std::int32_t w, std::int32_t h) {
    if (const auto i = m_layout.index_of(id)) {
        m_layout.gadgets[*i].size = {w, h};
    }
}

std::optional<std::array<std::int32_t, 2>> Panel::position(std::int32_t id) const {
    if (const auto i = m_layout.index_of(id)) {
        return m_layout.gadgets[*i].pos;
    }
    return std::nullopt;
}

void Panel::set_scroll(std::int32_t id, std::int32_t line) {
    if (const auto i = m_layout.index_of(id)) {
        m_state[*i].scroll = std::max(0, line);
    }
}

std::int32_t Panel::max_scroll(std::int32_t id, const std::array<const Font*, kFontSlots>& fonts) const {
    const auto i = m_layout.index_of(id);
    const auto flow = i ? flow_lines(*i, fonts) : std::nullopt;
    if (!flow) {
        return 0;
    }
    return std::max(0, static_cast<std::int32_t>(flow->first.size()) - flow->second);
}

std::int32_t Panel::flow_rows(std::int32_t id) const {
    const auto i = m_layout.index_of(id);
    if (!i) {
        return 0;
    }
    const assets::Gadget& g = m_layout.gadgets[*i];
    return g.kind == GadgetKind::TextFlow && g.offset[1] > 0 ? g.size[1] / g.offset[1] : 0;
}

std::optional<std::pair<std::vector<std::string>, std::int32_t>> Panel::flow_lines(
    std::size_t index, const std::array<const Font*, kFontSlots>& fonts) const {
    // 0x00407D7D: width Size.x, line height Posoffs.y, lines Size.y / Posoffs.y.
    const assets::Gadget& g = m_layout.gadgets[index];
    const State& s = m_state[index];
    const Font* font = g.block >= 0 && g.block < static_cast<std::int32_t>(kFontSlots)
                           ? fonts[static_cast<std::size_t>(g.block)]
                           : nullptr;
    if (g.kind != GadgetKind::TextFlow || font == nullptr || g.offset[1] <= 0 || s.text.empty()) {
        return std::nullopt;
    }
    // The width in the font's own pixels: the renderer stretches glyphs to the screen itself, so
    // the panel's scale must not widen the lines (they ran past the window).
    auto lines = wrap_text(*font, s.text,
                           static_cast<std::uint32_t>(std::max(0, trunc_scaled(g.size[0] * font->design_scale()))));
    return std::make_pair(std::move(lines), g.size[1] / g.offset[1]);
}

std::optional<std::size_t> Panel::hit(std::int32_t x, std::int32_t y) const {
    // The last gadget under the mouse is on top; hidden ones still answer (0x00406660).
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < m_layout.gadgets.size(); ++i) {
        const assets::Gadget& g = m_layout.gadgets[i];
        if (g.kind == GadgetKind::Unused || g.no_select || m_state[i].no_select) {
            continue;
        }
        if (x >= g.pos[0] && y >= g.pos[1] && x < g.pos[0] + g.size[0] && y < g.pos[1] + g.size[1]) {
            found = i;
        }
    }
    return found;
}

void Panel::slide(std::size_t index, std::int32_t gx, std::int32_t gy) {
    // 0x00406A00: x from the left, y from the bottom.
    const assets::Gadget& g = m_layout.gadgets[index];
    State& s = m_state[index];
    s.slider[0] = std::clamp(gx - g.pos[0] - g.slider_offset[0], 0, std::max<std::int32_t>(0, g.slider_size[0]));
    s.slider[1] = g.slider_size[1] -
                  std::clamp(gy - g.pos[1] - g.slider_offset[1], 0, std::max<std::int32_t>(0, g.slider_size[1]));
}

std::vector<PanelEvent> Panel::mouse(std::int32_t x, std::int32_t y, bool down) {
    std::vector<PanelEvent> events;
    const std::int32_t gx = m_scale > 0.0 ? trunc_scaled(x / m_scale) : x;
    const std::int32_t gy = m_scale > 0.0 ? trunc_scaled(y / m_scale) : y;
    const std::optional<std::size_t> over = hit(gx, gy);
    m_hovered = over;
    const auto select = [&](std::size_t i) {
        const assets::Gadget& g = m_layout.gadgets[i];
        if (g.radio) {
            for (std::size_t k = 0; k < m_layout.gadgets.size(); ++k) {
                if (m_layout.gadgets[k].radio && m_layout.gadgets[k].base == g.base) {
                    m_state[k].selected = false;
                }
            }
        }
        m_state[i].selected = true;
    };
    if (down && !m_down) {
        m_down = true;
        m_pressed = over;
        if (over) {
            const assets::Gadget& g = m_layout.gadgets[*over];
            if (g.menu) {
                events.push_back(PanelEvent{PanelEvent::Kind::Pressed, g.id});
            }
            if (g.select_on_move) {
                select(*over);
            }
            if (g.slider) {
                slide(*over, gx, gy);
                events.push_back(PanelEvent{PanelEvent::Kind::Slid, g.id});
            }
        }
    } else if (down) {
        if (m_pressed && m_layout.gadgets[*m_pressed].slider) {
            slide(*m_pressed, gx, gy);
            events.push_back(PanelEvent{PanelEvent::Kind::Slid, m_layout.gadgets[*m_pressed].id});
        } else if (over && over != m_pressed && m_layout.gadgets[*over].select_on_move) {
            // Dragged onto another select-on-drag gadget: it is the one now.
            m_pressed = over;
            select(*over);
        }
    } else if (m_down) {
        m_down = false;
        if (m_pressed && over == m_pressed) {
            const assets::Gadget& g = m_layout.gadgets[*over];
            if (g.toggle) {
                m_state[*over].selected = !m_state[*over].selected;
            } else if (g.radio) {
                select(*over);
            }
            events.push_back(PanelEvent{PanelEvent::Kind::Clicked, g.id});
        }
        m_pressed.reset();
    }
    return events;
}

std::optional<std::int32_t> Panel::hovered() const {
    if (!m_hovered) {
        return std::nullopt;
    }
    return m_layout.gadgets[*m_hovered].id;
}

std::optional<std::int32_t> Panel::held() const {
    if (!m_down || !m_pressed) {
        return std::nullopt;
    }
    return m_layout.gadgets[*m_pressed].id;
}

std::uint8_t Panel::state_of(std::size_t index) const {
    return (m_down && m_pressed == index) || m_state[index].selected ? 1 : 0;
}

void Panel::draw_gadget(std::vector<DrawItem>& out, std::size_t index, std::uint8_t state,
                        const std::array<const Font*, kFontSlots>& fonts) const {
    const assets::Gadget& g = m_layout.gadgets[index];
    const State& s = m_state[index];
    const double k = m_scale;
    // Draw position: trunc((Pos + Posoffs + 0.5) * scale) plus the slider's offset (0x00407AED).
    std::int32_t x = to_pixels(g.pos[0] + g.offset[0], k);
    std::int32_t y = to_pixels(g.pos[1] + g.offset[1], k);
    // Only along the slider's own axes (Slidhorflg 0x40, Slidverflg 0x80 at +0x19).
    if (g.slider_x) {
        x += trunc_scaled((s.slider[0] + g.slider_offset[0]) * k);
    }
    if (g.slider_y) {
        y += trunc_scaled((g.slider_size[1] - s.slider[1] + g.slider_offset[1]) * k);
    }
    const std::int32_t colour = g.colour[state];
    // The graphic set by the code (set_graphic) replaces the layout's for every graphic kind: the
    // build menu's preview and its shadow (GraphicFill) show the same building.
    const std::int32_t gfx = s.graphic ? s.graphic->first + s.graphic->second * state : g.gfx + g.press_offset * state;
    DrawItem item;
    item.x = x;
    item.y = y;
    item.block = g.block;
    item.colour = colour;
    item.half = g.half;
    item.faded = s.faded;
    switch (g.kind) {
        case GadgetKind::Unused:
        case GadgetKind::Area:
            break;
        case GadgetKind::Graphic:
            item.kind = DrawItem::Kind::Sprite;
            item.gfx = gfx;
            item.tint = s.graphic ? s.tint : std::nullopt;
            item.first_row = s.first_row;
            item.rows = s.rows;
            if (const assets::PackageIcon* icon = assets::package_icon_at(package_icons(), gfx);
                icon != nullptr && (icon->width > static_cast<std::uint32_t>(std::max(g.size[0], 1)) ||
                                    icon->height > static_cast<std::uint32_t>(std::max(g.size[1], 1)))) {
                // A package's icon larger than its place: made smaller to fit it.
                item.w = trunc_scaled(g.size[0] * k);
                item.h = trunc_scaled(g.size[1] * k);
            }
            out.push_back(item);
            break;
        case GadgetKind::GraphicRaster:
            item.kind = DrawItem::Kind::SpriteRaster;
            item.gfx = s.graphic ? s.graphic->first : g.gfx;
            out.push_back(item);
            break;
        case GadgetKind::GraphicFill:
            item.kind = DrawItem::Kind::SpriteMask;
            item.gfx = s.graphic ? s.graphic->first : g.gfx;
            out.push_back(item);
            break;
        case GadgetKind::TextLeft:
        case GadgetKind::TextCentre:
        case GadgetKind::TextRight:
            if (!s.text.empty()) {
                item.kind = DrawItem::Kind::Text;
                item.align = g.kind == GadgetKind::TextLeft     ? Align::Left
                             : g.kind == GadgetKind::TextCentre ? Align::Centre
                                                                : Align::Right;
                item.text = s.text;
                const Font* font = g.block >= 0 && g.block < static_cast<std::int32_t>(kFontSlots)
                                       ? fonts[static_cast<std::size_t>(g.block)]
                                       : nullptr;
                // Without a font, or in a gadget without a width, there is nothing to measure against.
                if (s.fit && font != nullptr && (s.fit_room || g.size[0] > 0)) {
                    // The text starts Posoffs.x into the gadget.
                    const std::int32_t room = s.fit_room.value_or(g.size[0]) - g.offset[0];
                    item.text = fit_text(font, s.text, std::max(room, 0), false);
                }
                out.push_back(item);
            }
            break;
        case GadgetKind::Fill:
            item.kind = DrawItem::Kind::Fill;
            item.x = to_pixels(g.pos[0], k);
            item.y = to_pixels(g.pos[1], k);
            item.w = trunc_scaled(g.size[0] * k);
            item.h = trunc_scaled(g.size[1] * k);
            out.push_back(item);
            break;
        case GadgetKind::Bar: {
            // 0x00407F7C: horizontal bars follow the slider, vertical ones grow upwards.
            item.kind = DrawItem::Kind::Fill;
            item.x = to_pixels(g.pos[0] + g.offset[0], k);
            item.y = to_pixels(g.pos[1] + g.offset[1], k);
            item.w = trunc_scaled(g.size[0] * k);
            item.h = trunc_scaled(g.size[1] * k);
            if (g.slider_x) {
                item.w = trunc_scaled((s.slider[0] + g.slider_offset[0]) * k);
            }
            if (g.slider_y) {
                const std::int32_t h = trunc_scaled(s.slider[1] * k);
                item.y += item.h - h;
                item.h = h;
            }
            out.push_back(item);
            break;
        }
        case GadgetKind::TextFlow: {
            const auto flow = flow_lines(index, fonts);
            if (!flow) {
                break;
            }
            const auto& [lines, shown] = *flow;
            for (std::int32_t n = 0; n < shown; ++n) {
                const auto line = static_cast<std::size_t>(s.scroll + n);
                if (line >= lines.size()) {
                    break;
                }
                DrawItem t = item;
                t.kind = DrawItem::Kind::Text;
                t.x = to_pixels(g.pos[0], k);
                t.y = to_pixels(g.pos[1] + (n + 1) * g.offset[1], k);
                t.text = lines[line];
                out.push_back(std::move(t));
            }
            break;
        }
    }
    if (s.disabled) {
        DrawItem r = item;
        if (g.kind == GadgetKind::Graphic) {
            r.kind = DrawItem::Kind::SpriteRaster;
            r.gfx = gfx;
            r.colour = 0;
        } else {
            r.kind = DrawItem::Kind::Raster;
            r.x = to_pixels(g.pos[0], k);
            r.y = to_pixels(g.pos[1], k);
            r.w = trunc_scaled(g.size[0] * k);
            r.h = trunc_scaled(g.size[1] * k);
        }
        r.text.clear();
        out.push_back(std::move(r));
    }
}

std::vector<DrawItem> Panel::draw(const std::array<const Font*, kFontSlots>& fonts) const {
    std::vector<DrawItem> out;
    for (std::size_t i = 0; i < m_layout.gadgets.size(); ++i) {
        const assets::Gadget& g = m_layout.gadgets[i];
        if (g.kind == GadgetKind::Unused || g.hidden || m_state[i].hidden) {
            continue;
        }
        const std::uint8_t state = state_of(i);
        if (g.draw_base && g.base >= 0 && static_cast<std::size_t>(g.base) < m_layout.gadgets.size() &&
            static_cast<std::size_t>(g.base) != i && (!g.base_when_selected || state != 0)) {
            draw_gadget(out, static_cast<std::size_t>(g.base), state, fonts);
        }
        draw_gadget(out, i, state, fonts);
    }
    return out;
}

}  // namespace o1602::ui
