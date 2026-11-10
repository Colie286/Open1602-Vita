// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/camera.hpp"

#include <algorithm>
#include <cmath>

namespace o1602::render {

namespace {

/// Size of the world in view space (rotations 1 and 3 swap width and height).
void view_size(const WorldView& view, std::int32_t& w, std::int32_t& h) {
    const bool swapped = (view.rotation & 1U) != 0;
    w = static_cast<std::int32_t>(swapped ? view.world_height : view.world_width);
    h = static_cast<std::int32_t>(swapped ? view.world_width : view.world_height);
}

TilePos view_to_world(const WorldView& view, std::int32_t vx, std::int32_t vy) {
    const auto W = static_cast<std::int32_t>(view.world_width);
    const auto H = static_cast<std::int32_t>(view.world_height);
    switch (view.rotation & 3U) {
        case 0:
            return {static_cast<std::uint32_t>(vx), static_cast<std::uint32_t>(vy)};
        case 1:
            return {static_cast<std::uint32_t>(vy), static_cast<std::uint32_t>(H - 1 - vx)};
        case 2:
            return {static_cast<std::uint32_t>(W - 1 - vx), static_cast<std::uint32_t>(H - 1 - vy)};
        default:
            return {static_cast<std::uint32_t>(W - 1 - vy), static_cast<std::uint32_t>(vx)};
    }
}

}  // namespace

std::optional<TilePos> world_tile_at(const WorldView& view, double px, double py) {
    std::int32_t vw = 0;
    std::int32_t vh = 0;
    view_size(view, vw, vh);
    const double half_w = tile_width(view.zoom) / 2.0;
    const double half_h = tile_height(view.zoom) / 2.0;
    // Offset from the centre of view tile (0, 0) at (vh * half_w, half_h), in half tiles:
    // s = vx - vy, t = vx + vy. The tile diamonds become unit squares after the 45 degree turn.
    const double s = px / half_w - vh;
    const double t = py / half_h - 1.0;
    const auto vx = static_cast<std::int32_t>(std::floor((s + t) / 2.0 + 0.5));
    const auto vy = static_cast<std::int32_t>(std::floor((t - s) / 2.0 + 0.5));
    if (vx < 0 || vy < 0 || vx >= vw || vy >= vh) {
        return std::nullopt;
    }
    return view_to_world(view, vx, vy);
}

std::optional<TilePos> world_ground_tile_at(const WorldView& view, double px, double py, std::int32_t max_elevation,
                                            const TileElevation& elevation) {
    std::int32_t vw = 0;
    std::int32_t vh = 0;
    view_size(view, vw, vh);
    const double half_w = tile_width(view.zoom) / 2.0;
    const double half_h = tile_height(view.zoom) / 2.0;
    // As world_tile_at. A tile raised by e pixels contains the point if its flat diamond contains
    // the point e pixels lower, so the candidates are the tiles on the vertical line from the point
    // down by max_elevation: walked from tile to tile, the depth (vx + vy) only grows, the last hit
    // is the front-most.
    const double s = px / half_w - vh;
    const double t0 = py / half_h - 1.0;
    const double t_end = t0 + std::max(max_elevation, 0) / half_h;
    const auto at = [&](double t) {
        return std::pair{static_cast<std::int32_t>(std::floor((s + t) / 2.0 + 0.5)),
                         static_cast<std::int32_t>(std::floor((t - s) / 2.0 + 0.5))};
    };
    std::optional<TilePos> best;
    for (double t = t0; t <= t_end;) {
        const auto [vx, vy] = at(t);
        if (vx >= 0 && vy >= 0 && vx < vw && vy < vh) {
            const TilePos tile = view_to_world(view, vx, vy);
            if (at(t0 + elevation(tile) / half_h) == std::pair{vx, vy}) {
                best = tile;
            }
        }
        // The next tile down the line: where vx or vy steps up.
        t = std::max(std::min(2.0 * vx + 1.0 - s, 2.0 * vy + 1.0 + s), t + 1e-9);
    }
    return best ? best : world_tile_at(view, px, py);
}

std::pair<double, double> world_point_at(const WorldView& view, double px, double py) {
    std::int32_t vw = 0;
    std::int32_t vh = 0;
    view_size(view, vw, vh);
    const double half_w = tile_width(view.zoom) / 2.0;
    const double half_h = tile_height(view.zoom) / 2.0;
    // As world_tile_at, without rounding: view tile (vx, vy) covers vx..vx+1.
    const double s = px / half_w - vh;
    const double t = py / half_h - 1.0;
    const double vx = (s + t) / 2.0 + 0.5;
    const double vy = (t - s) / 2.0 + 0.5;
    const auto W = static_cast<double>(view.world_width);
    const auto H = static_cast<double>(view.world_height);
    switch (view.rotation & 3U) {
        case 0:
            return {vx, vy};
        case 1:
            return {vy, H - vx};
        case 2:
            return {W - vx, H - vy};
        default:
            return {W - vy, vx};
    }
}

std::pair<double, double> world_pixel_of(const WorldView& view, double tx, double ty) {
    std::int32_t vw = 0;
    std::int32_t vh = 0;
    view_size(view, vw, vh);
    const auto W = static_cast<double>(view.world_width);
    const auto H = static_cast<double>(view.world_height);
    // The rotation of world_point_at undone, then its 45 degree turn.
    double vx = tx;
    double vy = ty;
    switch (view.rotation & 3U) {
        case 0:
            break;
        case 1:
            vx = H - ty;
            vy = tx;
            break;
        case 2:
            vx = W - tx;
            vy = H - ty;
            break;
        default:
            vx = ty;
            vy = W - tx;
            break;
    }
    const double half_w = tile_width(view.zoom) / 2.0;
    const double half_h = tile_height(view.zoom) / 2.0;
    return {(vx - vy + vh) * half_w, (vx + vy) * half_h};
}

ZoomChoice zoom_for_magnification(double magnification) noexcept {
    constexpr double kShrinkLimit = 0.75;
    const double m =
        std::isfinite(magnification) ? std::clamp(magnification, kMinMagnification, kMaxMagnification) : 1.0;
    if (m >= kShrinkLimit) {
        return {Zoom::Large, m};
    }
    if (m * 2.0 >= kShrinkLimit) {
        return {Zoom::Medium, m * 2.0};
    }
    return {Zoom::Small, m * 4.0};
}

bool whole_scale(double scale) noexcept {
    return scale >= 1.0 && std::abs(scale - std::round(scale)) < 1e-6;
}

void Camera::set_world_size(std::uint32_t width, std::uint32_t height) {
    m_view.world_width = std::max<std::uint32_t>(1, width);
    m_view.world_height = std::max<std::uint32_t>(1, height);
    clamp();
}

namespace {

/// The scales a camera accepts: below any level's sprites shrunk by the free zoom, up to the largest
/// magnification.
double valid_scale(double scale) {
    return std::isfinite(scale) ? std::clamp(scale, 0.5, kMaxMagnification) : 1.0;
}

}  // namespace

void Camera::set_screen(std::int32_t width, std::int32_t height, double scale) {
    m_width = std::max(1, width);
    m_height = std::max(1, height);
    m_scale = valid_scale(scale);
    clamp();
}

double Camera::magnification() const noexcept {
    return m_scale / static_cast<double>(1 << static_cast<int>(m_view.zoom));
}

Viewport Camera::viewport() const {
    const auto w = static_cast<std::int32_t>(std::ceil(m_width / m_scale));
    const auto h = static_cast<std::int32_t>(std::ceil(m_height / m_scale));
    return Viewport{static_cast<std::int32_t>(std::floor(m_center_x - m_width / (2.0 * m_scale))),
                    static_cast<std::int32_t>(std::floor(m_center_y - m_height / (2.0 * m_scale))), w + 1, h + 1};
}

void Camera::screen_origin(double& x, double& y) const {
    x = m_center_x * m_scale - m_width / 2.0;
    y = m_center_y * m_scale - m_height / 2.0;
    if (whole_scale(m_scale)) {
        x = std::round(x);
        y = std::round(y);
    }
}

RenderPlan Camera::render_plan(std::int64_t max_pixels, std::int32_t max_side) const {
    RenderPlan p;
    double ox = 0.0;
    double oy = 0.0;
    screen_origin(ox, oy);
    if (whole_scale(m_scale)) {
        p.scale = static_cast<std::int32_t>(std::lround(m_scale));
        p.origin_x = static_cast<std::int32_t>(ox);
        p.origin_y = static_cast<std::int32_t>(oy);
        p.width = m_width;
        p.height = m_height;
        return p;
    }
    // Drawn at the next whole scale and shrunk: every sprite pixel keeps hard edges, only the
    // screen pixels on its border mix the two colours (no uneven pixels, no blur).
    p.direct = false;
    p.scale = std::max(1, static_cast<std::int32_t>(std::ceil(m_scale)));
    const auto size = [&](std::int32_t k, std::int32_t& w, std::int32_t& h) {
        const double f = m_scale / k;
        // One texture pixel more on each side: the screen's edge may fall inside one.
        w = static_cast<std::int32_t>(std::ceil(m_width / f)) + 2;
        h = static_cast<std::int32_t>(std::ceil(m_height / f)) + 2;
    };
    for (;; --p.scale) {
        size(p.scale, p.width, p.height);
        const bool fits = (max_pixels <= 0 || std::int64_t{p.width} * p.height <= max_pixels) &&
                          (max_side <= 0 || std::max(p.width, p.height) <= max_side);
        if (fits || p.scale == 1) {
            break;
        }
    }
    p.dst_scale = m_scale / p.scale;
    // Texture pixel t = world pixel * k - origin lands at t * f + dst, which must be
    // world pixel * scale - screen origin.
    p.origin_x = static_cast<std::int32_t>(std::floor(ox / p.dst_scale));
    p.origin_y = static_cast<std::int32_t>(std::floor(oy / p.dst_scale));
    p.dst_x = p.origin_x * p.dst_scale - ox;
    p.dst_y = p.origin_y * p.dst_scale - oy;
    return p;
}

void Camera::center_on(TilePos tile) {
    std::int32_t px = 0;
    std::int32_t py = 0;
    world_tile_center(m_view, tile.x, tile.y, px, py);
    m_center_x = px;
    m_center_y = py;
    clamp();
}

void Camera::scroll(double dx, double dy) {
    m_center_x += dx / m_scale;
    m_center_y += dy / m_scale;
    clamp();
}

void Camera::screen_to_world(double sx, double sy, double& px, double& py) const {
    px = m_center_x + (sx - m_width / 2.0) / m_scale;
    py = m_center_y + (sy - m_height / 2.0) / m_scale;
}

void Camera::world_to_screen(double px, double py, double& sx, double& sy) const {
    sx = (px - m_center_x) * m_scale + m_width / 2.0;
    sy = (py - m_center_y) * m_scale + m_height / 2.0;
}

void Camera::place(double px, double py, double sx, double sy) {
    m_center_x = px - (sx - m_width / 2.0) / m_scale;
    m_center_y = py - (sy - m_height / 2.0) / m_scale;
}

std::optional<TilePos> Camera::tile_at_screen(double sx, double sy) const {
    double px = 0;
    double py = 0;
    screen_to_world(sx, sy, px, py);
    return world_tile_at(m_view, px, py);
}

std::optional<TilePos> Camera::ground_tile_at_screen(double sx, double sy, const TileElevation& elevation) const {
    double px = 0;
    double py = 0;
    screen_to_world(sx, sy, px, py);
    return world_ground_tile_at(m_view, px, py, max_tile_elevation(m_view.zoom), elevation);
}

std::pair<double, double> Camera::point_at_screen(double sx, double sy) const {
    double px = 0;
    double py = 0;
    screen_to_world(sx, sy, px, py);
    return world_point_at(m_view, px, py);
}

template <class Change>
void Camera::change_view(double sx, double sy, Change change) {
    // Remember the anchor as a tile plus the sub-tile offset of the point from the tile centre
    // (in tile units), so zooming keeps the exact spot, not only the tile.
    double px = 0;
    double py = 0;
    screen_to_world(sx, sy, px, py);
    const auto tile = world_tile_at(m_view, px, py);
    const WorldView old_view = m_view;
    change();
    if (!tile) {
        // Outside the map: scale the centre position proportionally.
        std::int32_t ow = 0;
        std::int32_t oh = 0;
        std::int32_t nw = 0;
        std::int32_t nh = 0;
        world_pixel_size(old_view, ow, oh);
        world_pixel_size(m_view, nw, nh);
        m_center_x = m_center_x * nw / ow;
        m_center_y = m_center_y * nh / oh;
        clamp();
        return;
    }
    std::int32_t ocx = 0;
    std::int32_t ocy = 0;
    world_tile_center(old_view, tile->x, tile->y, ocx, ocy);
    double fx = (px - ocx) / tile_width(old_view.zoom);
    double fy = (py - ocy) / tile_height(old_view.zoom);
    if (old_view.rotation != m_view.rotation) {
        fx = 0;  // a rotated view has no meaningful sub-tile offset
        fy = 0;
    }
    std::int32_t ncx = 0;
    std::int32_t ncy = 0;
    world_tile_center(m_view, tile->x, tile->y, ncx, ncy);
    place(ncx + fx * tile_width(m_view.zoom), ncy + fy * tile_height(m_view.zoom), sx, sy);
    clamp();
}

void Camera::set_zoom(Zoom zoom, double sx, double sy) {
    change_view(sx, sy, [&] { m_view.zoom = zoom; });
}

void Camera::set_rotation(std::uint8_t rotation, double sx, double sy) {
    change_view(sx, sy, [&] { m_view.rotation = static_cast<std::uint8_t>(rotation & 3U); });
}

TilePos tile_neighbour(const WorldView& view,TilePos tile,int sx,int sy) {
    if(!sx&&!sy)return tile;
    const auto [ox,oy]=world_pixel_of(view,tile.x+.5,tile.y+.5);
    double best=-1e30,best_cross=-1e30;int bx=0,by=0;
    for(const auto [dx,dy]:{std::pair{1,0},std::pair{0,1},std::pair{-1,0},std::pair{0,-1}}){
        const auto [x,y]=world_pixel_of(view,double(tile.x)+dx+.5,double(tile.y)+dy+.5);
        const double norm=std::hypot(x-ox,y-oy);
        const double dot=((x-ox)*sx+(y-oy)*sy)/norm;
        const double cross=(sx*(y-oy)-sy*(x-ox))/norm;
        if(dot>best+1e-8||(std::abs(dot-best)<1e-8&&cross>best_cross)){best=dot;best_cross=cross;bx=dx;by=dy;}
    }
    const auto x=static_cast<std::int64_t>(tile.x)+bx,y=static_cast<std::int64_t>(tile.y)+by;
    if(x<0||y<0||x>=view.world_width||y>=view.world_height)return tile;
    return {static_cast<std::uint32_t>(x),static_cast<std::uint32_t>(y)};
}

std::int32_t pixel_scale_for(float display_scale, std::int32_t max_scale) noexcept {
    const float s = std::isfinite(display_scale) ? std::clamp(display_scale, 1.0F, 64.0F) : 1.0F;
    return std::clamp(static_cast<std::int32_t>(std::lround(s)), 1, std::max(max_scale, 1));
}

void Camera::set_scale(double scale, double sx, double sy) {
    double px = 0;
    double py = 0;
    screen_to_world(sx, sy, px, py);
    m_scale = valid_scale(scale);
    place(px, py, sx, sy);
    clamp();
}

void Camera::set_magnification(double magnification, double sx, double sy) {
    const ZoomChoice choice = zoom_for_magnification(magnification);
    if (choice.zoom == m_view.zoom) {
        set_scale(choice.scale, sx, sy);
        return;
    }
    // The point under (sx, sy) in world tiles, the same at every level.
    const auto [tx, ty] = point_at_screen(sx, sy);
    m_view.zoom = choice.zoom;
    m_scale = valid_scale(choice.scale);
    const auto [px, py] = world_pixel_of(m_view, tx, ty);
    place(px, py, sx, sy);
    clamp();
}

void Camera::clamp() {
    // The screen centre stays on the map's diamond (not its bounding box), so the view never
    // drifts into the black corners beyond the end of the world: the centre in view tiles
    // (as world_point_at, before the rotation) is kept in 0..width, 0..height.
    std::int32_t vw = 0;
    std::int32_t vh = 0;
    view_size(m_view, vw, vh);
    const double half_w = tile_width(m_view.zoom) / 2.0;
    const double half_h = tile_height(m_view.zoom) / 2.0;
    const double s = m_center_x / half_w - vh;
    const double t = m_center_y / half_h - 1.0;
    const double vx = std::clamp((s + t) / 2.0 + 0.5, 0.0, static_cast<double>(vw));
    const double vy = std::clamp((t - s) / 2.0 + 0.5, 0.0, static_cast<double>(vh));
    m_center_x = (vx - vy + vh) * half_w;
    m_center_y = (vx + vy) * half_h;
}

}  // namespace o1602::render
