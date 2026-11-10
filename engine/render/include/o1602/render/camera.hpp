// SPDX-License-Identifier: GPL-3.0-or-later
//
// Map camera: which part of the world is on screen. Pure logic without platform dependency, so
// the viewer and later the game share it and the tests cover it.
//
// Zoom and rotation changes keep the tile under a chosen screen point (centre or mouse cursor)
// in place, which the original only does for the screen centre.
//
// Free zoom: the map is drawn at any magnification, not only the original's three zoom levels.
// The magnification is screen pixels per sprite pixel of the large level; the camera picks the
// sprites of the level that shows it best and scales them by the rest (scale()). Whole scales draw
// the sprites directly; any other scale draws them at the next whole scale into a texture first,
// which is then shrunk smoothly onto the screen (render_plan()), so the pixel art stays sharp.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "o1602/render/world_renderer.hpp"

namespace o1602::render {

struct TilePos {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    friend bool operator==(const TilePos&, const TilePos&) = default;
};

/// World tile whose diamond contains the world pixel (px, py), or nothing outside the map.
[[nodiscard]] std::optional<TilePos> world_tile_at(const WorldView& view, double px, double py);
/// Height of a world tile's diamond above its flat place, in world pixels (tile_elevation).
using TileElevation = std::function<std::int32_t(TilePos)>;

/// World tile whose diamond, drawn `elevation` pixels higher (0..max_elevation), contains the world
/// pixel (px, py): the front-most of them, as it is drawn over those behind it. The land is raised
/// above the sea (Posoffs), so the tile under the mouse is not the flat one below it. Without such
/// a tile (below a raised tile, where its coast falls to a lower one) the flat one, nothing outside
/// the map.
[[nodiscard]] std::optional<TilePos> world_ground_tile_at(const WorldView& view, double px, double py,
                                                          std::int32_t max_elevation, const TileElevation& elevation);
/// The same point in world tiles as fractions (tile (x, y) covers x..x+1, y..y+1), also outside
/// the map: for the frame of the visible part on the minimap.
[[nodiscard]] std::pair<double, double> world_point_at(const WorldView& view, double px, double py);

/// World pixel of a point in world tiles (the inverse of world_point_at).
[[nodiscard]] std::pair<double, double> world_pixel_of(const WorldView& view, double tx, double ty);

/// A single four-connected neighbour in the requested screen direction. Uses
/// world_pixel_of for the current rotation, with clockwise ties at cardinal axes.
/// A blocked map edge keeps the same tile rather than choosing a different edge.
[[nodiscard]] TilePos tile_neighbour(const WorldView&, TilePos, int screen_x, int screen_y);

/// The integer pixel scale for a display scale (Window::display_scale): rounded, 1..max_scale.
[[nodiscard]] std::int32_t pixel_scale_for(float display_scale, std::int32_t max_scale) noexcept;

/// The smallest and largest magnification of the free zoom (screen pixels per sprite pixel of the
/// large level): the small level shrunk to 0.8, the large level eight times.
inline constexpr double kMinMagnification = 0.2;
inline constexpr double kMaxMagnification = 8.0;

/// Zoom level and scale that show a magnification: the most detailed level whose sprites need
/// not be shrunk below 0.75 (smaller scales blur away the detail the level has in addition).
struct ZoomChoice {
    Zoom zoom = Zoom::Large;
    double scale = 1.0;
};
[[nodiscard]] ZoomChoice zoom_for_magnification(double magnification) noexcept;

/// Whether a scale is a whole number (the sprites are drawn directly then).
[[nodiscard]] bool whole_scale(double scale) noexcept;

/// How a frame of the map is drawn: sprites at the whole scale `scale` with the integer origin
/// (screen = world pixel * scale - origin) into a texture of `width` x `height` (or straight onto
/// the screen if `direct`), then that texture stretched to `dst_*` on the screen.
struct RenderPlan {
    bool direct = true;
    std::int32_t scale = 1;
    std::int32_t origin_x = 0;
    std::int32_t origin_y = 0;
    std::int32_t width = 0;  ///< of the texture (direct: of the screen)
    std::int32_t height = 0;
    double dst_x = 0.0;  ///< where the texture's top left lands on the screen
    double dst_y = 0.0;
    double dst_scale = 1.0;  ///< screen pixels per texture pixel
};

class Camera {
public:
    Camera() = default;

    /// Size of the world in tiles (default: the original 500 x 350).
    void set_world_size(std::uint32_t width, std::uint32_t height);

    /// Screen size in physical pixels and the scale (1 = one sprite pixel per screen pixel).
    void set_screen(std::int32_t width, std::int32_t height, double scale);

    [[nodiscard]] const WorldView& view() const noexcept { return m_view; }
    /// Screen pixels per sprite pixel of the zoom level shown.
    [[nodiscard]] double scale() const noexcept { return m_scale; }
    /// Screen pixels per sprite pixel of the large zoom level.
    [[nodiscard]] double magnification() const noexcept;
    [[nodiscard]] std::int32_t screen_width() const noexcept { return m_width; }
    [[nodiscard]] std::int32_t screen_height() const noexcept { return m_height; }

    /// Visible part of the world in world pixels of the current zoom level.
    [[nodiscard]] Viewport viewport() const;

    /// Offset for drawing and picking: screen position = world pixel * scale() - origin. Rounded at
    /// a whole scale, so that the sprites land on whole pixels.
    void screen_origin(double& x, double& y) const;
    /// How to draw the frame. A texture larger than `max_pixels` pixels or `max_side` on a side
    /// (0: no limit) is drawn at the next smaller whole scale instead (stretched, a little softer).
    [[nodiscard]] RenderPlan render_plan(std::int64_t max_pixels, std::int32_t max_side) const;

    void center_on(TilePos tile);
    /// Scrolls by a distance in screen pixels.
    void scroll(double dx, double dy);

    /// Changes zoom level / rotation / scale, keeping the tile under screen point (sx, sy).
    void set_zoom(Zoom zoom, double sx, double sy);
    void set_rotation(std::uint8_t rotation, double sx, double sy);
    void set_scale(double scale, double sx, double sy);
    /// Free zoom: shows the magnification (kept within kMin/kMaxMagnification) with the level
    /// zoom_for_magnification chooses, keeping the point under (sx, sy) where it is.
    void set_magnification(double magnification, double sx, double sy);

    /// World tile under a screen point.
    [[nodiscard]] std::optional<TilePos> tile_at_screen(double sx, double sy) const;
    /// World tile whose raised diamond is under a screen point (world_ground_tile_at): the tile the
    /// player sees there.
    [[nodiscard]] std::optional<TilePos> ground_tile_at_screen(double sx, double sy,
                                                               const TileElevation& elevation) const;
    /// World point (in fractional tiles) under a screen point, also beyond the map's edge.
    [[nodiscard]] std::pair<double, double> point_at_screen(double sx, double sy) const;
    /// Screen point of a world pixel (units drawn over the map).
    void world_to_screen(double px, double py, double& sx, double& sy) const;

private:
    void screen_to_world(double sx, double sy, double& px, double& py) const;
    /// Moves the camera so that world pixel (px, py) appears at screen point (sx, sy).
    void place(double px, double py, double sx, double sy);
    /// Keeps the screen centre inside the map.
    void clamp();
    /// Applies a view change, keeping whatever is under (sx, sy) in place.
    template <class Change>
    void change_view(double sx, double sy, Change change);

    WorldView m_view{};
    double m_center_x = 0.0;  ///< world pixel at the screen centre
    double m_center_y = 0.0;
    std::int32_t m_width = 640;
    std::int32_t m_height = 480;
    double m_scale = 1.0;
};

}  // namespace o1602::render
