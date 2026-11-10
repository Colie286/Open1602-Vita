// SPDX-License-Identifier: GPL-3.0-or-later
//
// The mouse cursor of the window: the original's arrow and target (docs/re/ui.md "Mouse
// cursors"), made from RGBA images, or the system's own where an image is missing.
#pragma once

#include <array>
#include <cstdint>
#include <span>

struct SDL_Cursor;

namespace o1602::platform {

class MouseCursors {
public:
    enum class Shape : std::uint8_t {
        Arrow,   ///< the normal cursor
        Target,  ///< a window waits for a click on the map
        Hidden,  ///< a video fills the screen
    };

    MouseCursors() = default;
    ~MouseCursors();
    MouseCursors(const MouseCursors&) = delete;
    MouseCursors& operator=(const MouseCursors&) = delete;

    /// The image of a shape: width * height RGBA pixels, rows from the top. False if SDL refused
    /// it; the shape then keeps the system's cursor.
    bool set_image(Shape shape, std::uint32_t width, std::uint32_t height, std::uint32_t hot_x, std::uint32_t hot_y,
                   std::span<const std::uint8_t> rgba);
    /// Shows the shape; nothing happens while it is shown already.
    void show(Shape shape);

private:
    std::array<SDL_Cursor*, 2> m_cursors{};
    std::array<SDL_Cursor*, 2> m_system{};
    bool m_shown = false;
    Shape m_shape = Shape::Arrow;
};

}  // namespace o1602::platform
