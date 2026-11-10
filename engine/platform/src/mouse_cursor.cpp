// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/platform/mouse_cursor.hpp"

#include <SDL3/SDL.h>
#include <cstddef>

namespace o1602::platform {

MouseCursors::~MouseCursors() {
    for (std::array<SDL_Cursor*, 2>* list : {&m_cursors, &m_system}) {
        for (SDL_Cursor*& c : *list) {
            if (c != nullptr) {
                SDL_DestroyCursor(c);
                c = nullptr;
            }
        }
    }
}

bool MouseCursors::set_image(Shape shape, std::uint32_t width, std::uint32_t height, std::uint32_t hot_x,
                             std::uint32_t hot_y, std::span<const std::uint8_t> rgba) {
    const auto i = static_cast<std::size_t>(shape);
    if (i >= m_cursors.size() || width == 0 || height == 0 || rgba.size() != std::size_t{width} * height * 4) {
        return false;
    }
    // SDL copies the pixels into the cursor; the surface only lends them.
    SDL_Surface* surface =
        SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32,
                              const_cast<std::uint8_t*>(rgba.data()), static_cast<int>(width * 4));
    if (surface == nullptr) {
        return false;
    }
    // Twice and three times as large for displays that scale the window (FB-24): SDL takes the
    // image that fits the display's scale. The pixels are doubled, as the art is pixel art.
    for (const int factor : {2, 3}) {
        if (SDL_Surface* large = SDL_ScaleSurface(surface, static_cast<int>(width) * factor,
                                                  static_cast<int>(height) * factor, SDL_SCALEMODE_NEAREST)) {
            SDL_AddSurfaceAlternateImage(surface, large);
            SDL_DestroySurface(large);  // the surface keeps its own reference
        }
    }
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, static_cast<int>(hot_x), static_cast<int>(hot_y));
    SDL_DestroySurface(surface);
    if (cursor == nullptr) {
        return false;
    }
    if (m_cursors[i] != nullptr) {
        SDL_DestroyCursor(m_cursors[i]);
    }
    m_cursors[i] = cursor;
    if (m_shown && m_shape == shape) {
        SDL_SetCursor(cursor);
    }
    return true;
}

void MouseCursors::show(Shape shape) {
    if (m_shown && m_shape == shape) {
        return;
    }
    m_shown = true;
    m_shape = shape;
    if (shape == Shape::Hidden) {
        SDL_HideCursor();
        return;
    }
    const auto i = static_cast<std::size_t>(shape);
    SDL_Cursor* cursor = m_cursors[i];
    if (cursor == nullptr) {
        if (m_system[i] == nullptr) {
            m_system[i] = SDL_CreateSystemCursor(shape == Shape::Target ? SDL_SYSTEM_CURSOR_CROSSHAIR
                                                                        : SDL_SYSTEM_CURSOR_DEFAULT);
        }
        cursor = m_system[i];
    }
    if (cursor != nullptr) {
        SDL_SetCursor(cursor);
    }
    SDL_ShowCursor();
}

}  // namespace o1602::platform
