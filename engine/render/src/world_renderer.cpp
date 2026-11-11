// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/world_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <numbers>

#include "o1602/assets/figure_looks.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/assets/package_flags.hpp"
#include "o1602/sim/disasters.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/ships.hpp"

namespace o1602::render {

namespace {

constexpr std::int32_t floor_div(std::int32_t a, std::int32_t b) {
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

struct RotatedWorld {
    std::uint32_t width;  ///< size in view space
    std::uint32_t height;
    std::uint8_t rotation;

    std::uint32_t world_width;  ///< size in world space
    std::uint32_t world_height;

    explicit RotatedWorld(const WorldView& view)
        : width((view.rotation & 1U) != 0 ? view.world_height : view.world_width),
          height((view.rotation & 1U) != 0 ? view.world_width : view.world_height),
          rotation(static_cast<std::uint8_t>(view.rotation & 3U)),
          world_width(view.world_width),
          world_height(view.world_height) {}

    /// View space -> world coordinates (same convention as the island renderer).
    void to_world(std::uint32_t vx, std::uint32_t vy, std::uint32_t& wx, std::uint32_t& wy) const {
        const std::uint32_t W = world_width;
        const std::uint32_t H = world_height;
        switch (rotation) {
            case 0:
                wx = vx;
                wy = vy;
                break;
            case 1:
                wx = vy;
                wy = H - 1 - vx;
                break;
            case 2:
                wx = W - 1 - vx;
                wy = H - 1 - vy;
                break;
            default:
                wx = W - 1 - vy;
                wy = vx;
                break;
        }
    }

    void to_view(std::uint32_t wx, std::uint32_t wy, std::uint32_t& vx, std::uint32_t& vy) const {
        const std::uint32_t W = world_width;
        const std::uint32_t H = world_height;
        switch (rotation) {
            case 0:
                vx = wx;
                vy = wy;
                break;
            case 1:
                vx = H - 1 - wy;
                vy = wx;
                break;
            case 2:
                vx = W - 1 - wx;
                vy = H - 1 - wy;
                break;
            default:
                vx = wy;
                vy = W - 1 - wx;
                break;
        }
    }
};

/// World point (continuous tile coordinates) -> world pixel position of the current view
/// (x = (fx - fy) * w/2, y = (fx + fy) * (h+1)/2 after the view rotation).
void project_point(const WorldView& view, double wx, double wy, double& sx, double& sy) {
    const RotatedWorld rw(view);
    const double W = rw.world_width;
    const double H = rw.world_height;
    double fx = wx;
    double fy = wy;
    switch (rw.rotation) {
        case 1:
            fx = H - wy;
            fy = wx;
            break;
        case 2:
            fx = W - wx;
            fy = H - wy;
            break;
        case 3:
            fx = wy;
            fy = W - wx;
            break;
        default:
            break;
    }
    sx = (fx - fy + static_cast<double>(rw.height)) * (tile_width(view.zoom) / 2.0);
    sy = (fx + fy) * (tile_height(view.zoom) / 2.0);
}

constexpr std::uint64_t kFlagFrameMs = 90;  // AnimSpeed of the flag figures
constexpr std::uint16_t kFigureUntergang = assets::figure::kSinking;
constexpr std::uint16_t kFigureFahneWeiss = assets::figure::kWhiteFlag;

/// Image `frame` of a banner's flag: the tinted generic flag or the banner's own sprites.
std::optional<std::uint32_t> flag_sprite(const WorldLayers& layers, sim::BannerId id, std::uint64_t frame) {
    const Banner* banner = layers.banners != nullptr ? layers.banners->get(id) : nullptr;
    if (banner == nullptr) {
        return std::nullopt;
    }
    // The tinted flag has as many images as its template, of which the atlas holds one block per
    // banner, a package's flag (flags.toml) kFlagFrames, for any banner; own sprites as many as the
    // banner says (FB-33).
    if (layers.banner_flags != nullptr && id < layers.banner_flags->size() && (*layers.banner_flags)[id]) {
        const auto& t = layers.banners->flag_template();
        const bool own =
            layers.package_flags != nullptr && id < layers.package_flags->size() && (*layers.package_flags)[id];
        const std::uint32_t frames = !own && t && t->frames > 0 ? t->frames : assets::kFlagFrames;
        return *(*layers.banner_flags)[id] + static_cast<std::uint32_t>(frame % frames);
    }
    if (banner->flag == FlagStyle::Sprites && banner->flag_frames > 0) {
        return layers.ship_base + banner->flag_gfx + static_cast<std::uint32_t>(frame % banner->flag_frames);
    }
    return std::nullopt;
}

/// Whether `island` stays hidden from `player`: an island the player has not discovered shows its
/// ground layer only, without buildings and figures. kNoPlayer sees all.
bool undiscovered(const sim::World& world, sim::PlayerId player, std::uint16_t island) {
    return player != sim::kNoPlayer && island < world.islands().size() &&
           !world.islands()[island].discovered_by(player);
}

/// The cell a tile shows to `player`: the top layer, or the ground layer on a hidden island. A tile
/// without ground keeps its top cell (the original's ground grid has no holes); nullptr at sea.
const sim::MapCell* shown_cell(const sim::World& world, sim::PlayerId player, const sim::World::Cell& wc) {
    if (wc.island == sim::World::kSea) {
        return nullptr;
    }
    const sim::IslandMap& map = world.islands()[wc.island].map;
    if (undiscovered(world, player, wc.island)) {
        const sim::MapCell& ground = map.ground(wc.local_x, wc.local_y);
        if (!ground.empty()) {
            return &ground;
        }
    }
    return &map.top(wc.local_x, wc.local_y);
}

}  // namespace

void world_pixel_size(const WorldView& view, std::int32_t& width, std::int32_t& height) {
    const RotatedWorld rw(view);
    const auto diag = static_cast<std::int32_t>(rw.width + rw.height);
    width = diag * (tile_width(view.zoom) / 2);
    height = diag * (tile_height(view.zoom) / 2);
}

std::int32_t tile_elevation(const sim::World& world, const assets::BuildingTable& buildings, sim::PlayerId local_player,
                            std::uint32_t x, std::uint32_t y, Zoom zoom) {
    const std::int32_t tw = tile_width(zoom);
    if (x >= world.width() || y >= world.height()) {
        return 0;
    }
    const sim::MapCell* cell = shown_cell(world, local_player, world.cell(x, y));
    if (cell == nullptr || cell->empty()) {
        const assets::BuildingType* sea = buildings.open_sea();
        return sea != nullptr ? sea->posoffs * tw / 64 : 0;
    }
    const assets::BuildingType* b = buildings.by_sprite(cell->sprite);
    return b != nullptr ? b->posoffs * tw / 64 : 0;
}

void world_tile_center(const WorldView& view, std::uint32_t x, std::uint32_t y, std::int32_t& px, std::int32_t& py) {
    const RotatedWorld rw(view);
    std::uint32_t vx = 0;
    std::uint32_t vy = 0;
    rw.to_view(x, y, vx, vy);
    const std::int32_t tw = tile_width(view.zoom);
    const std::int32_t th = tile_height(view.zoom);
    px = (static_cast<std::int32_t>(vx) - static_cast<std::int32_t>(vy) + static_cast<std::int32_t>(rw.height) - 1) *
             (tw / 2) +
         tw / 2;
    py = static_cast<std::int32_t>(vx + vy) * (th / 2) + th / 2;
}

std::uint32_t sea_sprite(const assets::BuildingType& sea, std::uint32_t frame, std::uint32_t x, std::uint32_t y) {
    std::uint32_t sprite = sea.gfx;
    if (sea.anim_count > 1) {
        sprite += ((frame + x * 3 + y) % sea.anim_count) * sea.anim_add;
    }
    return sprite;
}

std::vector<DrawCommand> build_world_draw_list(const sim::World& world, const assets::BuildingTable& buildings,
                                               const AnimationClock& clock, const WorldView& view,
                                               const Viewport& viewport, const SpriteExtent& extent) {
    std::vector<DrawCommand> commands;
    build_world_draw_list(world, buildings, clock, view, viewport, extent, commands);
    return commands;
}

TileLighting TileLighting::territory(sim::PlayerId player) {
    TileLighting l;
    l.m_kind = Kind::Territory;
    l.m_player = player;
    return l;
}

TileLighting TileLighting::circle(sim::PlayerId player, const sim::Catchment& area) {
    TileLighting l;
    l.m_kind = Kind::Circle;
    l.m_player = player;
    l.add_circle(area);
    return l;
}

TileLighting TileLighting::harbours_only(sim::PlayerId player) {
    TileLighting l;
    l.m_kind = Kind::Circle;  // without a circle: no land lit
    l.m_player = player;
    return l;
}

TileLighting& TileLighting::add_circle(const sim::Catchment& area) {
    m_kind = Kind::Circle;
    m_lit.emplace_back().set(area);
    return *this;
}

TileLighting& TileLighting::outline(const sim::Catchment& area) {
    m_outline.set(area);
    return *this;
}

TileLighting& TileLighting::harbour(const sim::World& world, const assets::BuildingTable& buildings,
                                    std::uint16_t island, std::uint32_t x, std::uint32_t y) {
    m_world = &world;
    m_buildings = &buildings;
    if (island != m_island) {
        m_harbours.clear();  // the circles are of one island
    }
    m_island = island;
    m_harbours.push_back(Harbour{x, y});
    return *this;
}

void TileLighting::Circle::set(const sim::Catchment& area) {
    m_area = area;
    m_half = sim::circle_half_widths(area.radius);
    if (area.radius <= 1) {
        std::fill(m_half.begin(), m_half.end(), area.radius);  // a square, as the figures' search
    }
}

bool TileLighting::Circle::contains(std::int64_t x, std::int64_t y) const noexcept {
    if (m_half.empty()) {
        return false;
    }
    // As the catchment of the figures (pathfinding.cpp): an even size doubles the centre row/column.
    std::int64_t dy = y - m_area.centre.y;
    if (dy > 0) {
        dy -= m_area.parity_y;
    }
    if (std::abs(dy) > m_area.radius) {
        return false;
    }
    std::int64_t dx = x - m_area.centre.x;
    if (dx > 0) {
        dx -= m_area.parity_x;
    }
    return std::abs(dx) <= m_half[static_cast<std::size_t>(std::abs(dy))];
}

bool TileLighting::in_circle(std::int64_t x, std::int64_t y, std::uint32_t width, std::uint32_t height) const noexcept {
    for (std::int64_t ty = y; ty < y + height; ++ty) {
        for (std::int64_t tx = x; tx < x + width; ++tx) {
            if (in_circle(tx, ty)) {
                return true;
            }
        }
    }
    return false;
}

bool TileLighting::dimmed(std::int64_t x, std::int64_t y, const sim::MapCell* cell) const noexcept {
    return dimmed(x, y, cell, Footprint{x, y, 1, 1});
}

bool TileLighting::dimmed(std::int64_t x, std::int64_t y, const sim::MapCell* cell,
                          const Footprint& footprint) const noexcept {
    if (m_kind == Kind::None) {
        return false;
    }
    // The harbour: water and the unclaimed coast, as far as the ships reach.
    if (m_world != nullptr && (cell == nullptr || cell->owner == sim::kNoPlayer || cell->owner == m_player) &&
        std::any_of(m_harbours.begin(), m_harbours.end(), [&](const Harbour& h) {
            return sim::building_in_reach(*m_world, *m_buildings, m_island, h.x, h.y, static_cast<std::int32_t>(x),
                                          static_cast<std::int32_t>(y));
        })) {
        return false;
    }
    if (cell == nullptr && m_water && m_kind == Kind::Circle && in_circle(x, y)) {
        return false;
    }
    if (cell == nullptr || cell->owner != m_player) {
        return true;
    }
    return m_kind == Kind::Circle && !in_circle(footprint.x, footprint.y, footprint.width, footprint.height);
}

void build_world_draw_list(const sim::World& world, const assets::BuildingTable& buildings, const AnimationClock& clock,
                           const WorldView& view, const Viewport& viewport, const SpriteExtent& extent,
                           std::vector<DrawCommand>& commands) {
    WorldLayers layers;
    layers.extent = extent;
    build_world_draw_list(world, buildings, clock, view, viewport, layers, commands);
}

bool ship_sprites(const sim::Ship& ship, const sim::World& world, const WorldView& view_in, const WorldLayers& layers,
                  std::uint64_t time_ms, ShipSprites& out) {
    const WorldView view = fit_view(view_in, world);
    if (layers.figures == nullptr || layers.atlas == nullptr) {
        return false;
    }
    const assets::FigureType* fig = layers.figures->get(ship.figure);
    // From SHIP.BSH, or a ship of a package drawn with its own image (own_figures.hpp).
    if (fig == nullptr || (fig->sheet != assets::FigureSheet::Ship && !assets::draws_package_image(*fig))) {
        return false;
    }
    // Below half health the next record (HANDELD1 ..., Gfx + 8) every frame; a ship of a package
    // with its own image keeps it, as the record after it shows the original's.
    const std::uint32_t max_health = fig->ship.max_health;
    if (ship.health != 0 && max_health != 0 && ship.health * 128U / max_health < 64U &&
        !assets::draws_package_image(*fig)) {
        const assets::FigureType* damaged = layers.figures->get(ship.figure + 1U);
        if (damaged != nullptr && damaged->sheet == assets::FigureSheet::Ship) {
            fig = damaged;
        }
    }
    const RotatedWorld rw(view);
    const double tw = tile_width(view.zoom);
    const double hw = tw / 2.0;
    const double hh = tile_height(view.zoom) / 2.0;

    // Figure position: tile centre, projected like the tiles.
    double screen_x = 0.0;
    double screen_y = 0.0;
    // Ships move between tile centres; one built without a position lies on its tile's centre.
    // Between two ticks the trail puts it on its way (ADR 0004).
    double fx = 0.0;
    double fy = 0.0;
    if (layers.ship_trail != nullptr) {
        layers.ship_trail->position(ship, fx, fy);
    } else {
        const bool placed = ship.pos_x != 0 || ship.pos_y != 0;
        fx = placed ? static_cast<double>(ship.pos_x) / sim::kShipUnit : ship.x + 0.5;
        fy = placed ? static_cast<double>(ship.pos_y) / sim::kShipUnit : ship.y + 0.5;
    }
    project_point(view, fx, fy, screen_x, screen_y);

    // The original keeps figures at Y = y + Posoffs[1] * w/64, each term truncated, and
    // sorts them by Y into the row loop. Open1602's tile rows start one pixel lower than
    // the original's, so the pictures go one pixel up.
    const std::int32_t itw = tile_width(view.zoom);
    const std::int32_t depth = static_cast<std::int32_t>(std::floor(screen_y)) + fig->posoffs[1] * itw / 64;
    out.depth = depth;
    out.row = floor_div(depth - 1, static_cast<std::int32_t>(hh));

    // Hull: sprite = Gfx + ((direction + 2 * view rotation) & 7) * Rotate, centred,
    // bottom at Y + Posoffs[0] * w/64 (field +0x0C of the figure record).
    const std::uint32_t dir = (ship.direction + 2U * rw.rotation) & 7U;
    // From SHIP.BSH, or from the terrain sheet for a ship drawn with a package's image (figure_looks.hpp).
    const std::uint32_t hull_base =
        assets::draws_package_image(*fig)
            ? layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Stadtfld)].value_or(0)
            : layers.ship_base;
    out.hull.sprite = hull_base + fig->gfx + dir * fig->rotate;
    // A package's ship drawn as a tint template: in the colour of its owner's banner.
    if (assets::draws_package_image(*fig) && layers.template_tint != nullptr && ship.owner != sim::kNoPlayer) {
        if (const sim::Player* owner = world.player(ship.owner)) {
            if (const auto tinted = layers.template_tint->sprite(owner->banner, fig->gfx + dir * fig->rotate)) {
                out.hull.sprite = *tinted;
            }
        }
    }
    const AtlasRect& hull = layers.atlas->rect(out.hull.sprite);
    out.hull.x = static_cast<std::int32_t>(std::floor(screen_x)) - hull.width / 2;
    out.hull.y = depth + fig->posoffs[0] * itw / 64 - 1;

    // Sinking: after the turn the hull goes down by one height unit
    // (half a tile width) per second and is cut at the waterline, its bottom at height 0; the
    // splash UNTERGANG plays where it went down.
    const sim::ShipMotion& m = ship.motion;
    std::int32_t drop = 0;
    out.has_sink = false;
    if (ship.sinking && m.phase == sim::ShipMotion::Phase::Sinking) {
        const std::uint64_t sink_us = std::uint64_t{fig->ship.sink_ms != 0 ? fig->ship.sink_ms : 3000U} * 1000U;
        const std::uint64_t start_us = m.duration_us > sink_us ? m.duration_us - sink_us : 0U;
        if (m.elapsed_us > start_us) {
            const std::uint64_t t_us = m.elapsed_us - start_us;
            drop = static_cast<std::int32_t>(static_cast<double>(t_us) / 1e6 * hw);
            const assets::FigureType* splash = layers.figures->get(kFigureUntergang);
            const auto base = layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Effekte)];
            if (splash != nullptr && base) {
                // 300 ms the last frame, then 36 frames of 110 ms, then the last frame again.
                const std::uint64_t t_ms = t_us / 1000U;
                const std::uint32_t frame =
                    t_ms < 300U ? 35U : std::min<std::uint32_t>(35U, static_cast<std::uint32_t>((t_ms - 300U) / 110U));
                out.sink.sprite = *base + splash->gfx + dir * 36U + frame;
                const AtlasRect& sr = layers.atlas->rect(out.sink.sprite);
                const std::int32_t sd = static_cast<std::int32_t>(std::floor(screen_y)) + splash->posoffs[1] * itw / 64;
                out.sink.x = static_cast<std::int32_t>(std::floor(screen_x)) - sr.width / 2;
                out.sink.y = sd + splash->posoffs[0] * itw / 64 - 1;
                out.has_sink = !sr.empty();
            }
        }
    }
    const std::int32_t waterline = out.hull.y;
    if (drop > 0) {
        out.hull.y += drop;
        out.hull.cut_bottom = static_cast<std::uint16_t>(std::min<std::int32_t>(drop, hull.height));
    }

    // Bow wave while the ship runs (not turning, waiting or sinking): BUGH, 12 frames
    // per direction at the ship's 90 ms, bottom on the ship's Y, drawn under the hull.
    out.has_wave = false;
    if (!ship.sinking && m.phase == sim::ShipMotion::Phase::Run && fig->bow_wave != 0) {
        if (const assets::FigureType* wave = layers.figures->get(fig->bow_wave)) {
            const assets::FigureAnim* a = wave->anim(0);
            const std::uint32_t frames = a != nullptr && a->count > 0 ? a->count : 12U;
            out.wave.sprite =
                layers.ship_base + wave->gfx + dir * wave->rotate + static_cast<std::uint32_t>(time_ms / 90U % frames);
            const AtlasRect& wr = layers.atlas->rect(out.wave.sprite);
            out.wave.x = static_cast<std::int32_t>(std::floor(screen_x)) - wr.width / 2;
            out.wave.y = depth + wave->posoffs[0] * itw / 64 - 1;
            out.has_wave = !wr.empty();
        }
    }

    // Smoke of a broadside: turned to the bearing; Fahnoffs[0]
    // across the ship to the side it fired to and Fahnoffs[1] along it, projected,
    // hanging down from Fahnoffs[2] above Y plus its Posoffs[0], like the flag. It goes under the
    // hull when that side faces away from the viewer, over hull and flag otherwise.
    out.has_smoke = false;
    if (layers.ship_smoke != nullptr && !ship.sinking) {
        const auto it = layers.ship_smoke->find(ship.id);
        const assets::FigureType* smoke =
            it != layers.ship_smoke->end() ? layers.figures->get(it->second.figure) : nullptr;
        if (smoke != nullptr && smoke->sheet == assets::FigureSheet::Ship) {
            const std::uint32_t side = it->second.side & 1U;
            const auto smoke_dir = static_cast<std::uint8_t>((ship.direction + 8U - 4U * side - 2U) & 7U);
            out.smoke.sprite =
                layers.ship_base + assets::figure_sprite(*smoke, 0, it->second.frame, smoke_dir, rw.rotation);
            const AtlasRect& sr = layers.atlas->rect(out.smoke.sprite);
            const double across = static_cast<double>((dir + 4U * side) & 7U) * std::numbers::pi / 4.0;
            const double along = static_cast<double>((dir + 4U * side + 2U) & 7U) * std::numbers::pi / 4.0;
            const double ox = smoke->fahnoffs[0] * std::sin(across) + smoke->fahnoffs[1] * std::sin(along);
            const double oy = smoke->fahnoffs[0] * std::cos(across) + smoke->fahnoffs[1] * std::cos(along);
            const double px = (ox - oy) * hw;
            const double py = (ox + oy) * hh;
            out.smoke.x = static_cast<std::int32_t>(std::floor(screen_x + px)) - sr.width / 2;
            const std::int32_t top = static_cast<std::int32_t>(std::floor(screen_y - py)) + fig->posoffs[1] * itw / 64 +
                                     smoke->posoffs[0] * itw / 64 - static_cast<std::int32_t>(smoke->fahnoffs[2] * hw) -
                                     1;
            out.smoke.y = top + sr.height;
            out.smoke_behind = (dir < 4U) == (side == 0U);
            out.has_smoke = !sr.empty();
        }
    }

    // Flag of the owner's banner (original: figure). Offset: Fahnoffs[1]
    // along the ship axis, rotated by (direction + 2) * 45 degrees, projected; lifted
    // by Fahnoffs[2] * w/2 from Y and hanging down from there. (The original also adds the flag
    // figure's Posoffs[0], which is 0 for all flags.)
    out.has_flag = false;
    const sim::Player* owner = world.player(ship.owner);
    auto flag = owner != nullptr ? flag_sprite(layers, owner->banner, time_ms / kFlagFrameMs) : std::nullopt;
    if (ship.held) {
        // A surrendered ship shows FAHNEWEISS instead.
        const assets::FigureType* white = layers.figures->get(kFigureFahneWeiss);
        flag = white != nullptr && white->sheet == assets::FigureSheet::Ship
                   ? std::optional<std::uint32_t>(layers.ship_base + white->gfx +
                                                  static_cast<std::uint32_t>(time_ms / kFlagFrameMs % 8U))
                   : std::nullopt;
    }
    if (flag) {
        out.flag.sprite = *flag;
        const AtlasRect& fr = layers.atlas->rect(out.flag.sprite);
        const double angle = static_cast<double>((dir + 2U) & 7U) * std::numbers::pi / 4.0;
        const double ox = fig->fahnoffs[1] * std::sin(angle);
        const double oy = fig->fahnoffs[1] * std::cos(angle);
        const double px = (ox - oy) * hw;
        const double py = (ox + oy) * hh;
        out.flag.x = static_cast<std::int32_t>(std::floor(screen_x + px)) - fr.width / 2;
        const std::int32_t top = static_cast<std::int32_t>(std::floor(screen_y - py)) + fig->posoffs[1] * itw / 64 -
                                 static_cast<std::int32_t>(fig->fahnoffs[2] * hw) - 1;
        out.flag.y = top + fr.height;
        if (drop > 0) {
            out.flag.y += drop;
            const std::int32_t below = out.flag.y - waterline;
            if (below >= fr.height) {
                return true;  // gone under
            }
            out.flag.cut_bottom = static_cast<std::uint16_t>(std::max(0, below));
        }
        out.has_flag = true;
    }
    return true;
}

namespace {

constexpr std::uint16_t kFigureNoAktiv = assets::figure::kIdle;
constexpr std::uint16_t kFigureBrandMarkt = assets::figure::kMarketFire;
constexpr std::uint16_t kFigureSensenmann = assets::figure::kReaper;
constexpr std::uint16_t kFigureShowEisen = assets::figure::kShowOre;
constexpr std::uint16_t kFigureShowGold = assets::figure::kShowGold;

const sim::CostBuilding* shipyard_at(const sim::World& world, std::uint16_t island, std::uint16_t x, std::uint16_t y) {
    const auto& yards = world.cost_buildings();
    const auto yard = std::find_if(yards.begin(), yards.end(), [&](const sim::CostBuilding& b) {
        return b.kind == sim::CostBuilding::Kind::Shipyard && b.island == island && b.x == x && b.y == y;
    });
    return yard != yards.end() ? &*yard : nullptr;
}

}  // namespace

/// Building centre plus the figure's Fahnoffs turned with the building
/// (the same for flags, smoke, icons and jugglers).
std::pair<std::int32_t, std::int32_t> text_figure_point(const WorldView& view, double x, double y, double z) {
    double sx = 0.0;
    double sy = 0.0;
    project_point(view, x, y, sx, sy);
    const std::int32_t tw = tile_width(view.zoom);
    // FIGUR 0 has no Posoffs: the line's bottom is the projected point minus the height.
    return {static_cast<std::int32_t>(std::floor(sx)),
            static_cast<std::int32_t>(std::floor(sy)) - static_cast<std::int32_t>(z * tw / 2.0) - 1};
}

void building_attach_point(const sim::WorldIsland& island, const assets::BuildingType& b, std::uint32_t x,
                           std::uint32_t y, std::uint8_t rotation, const assets::FigureType& fig, double& cx,
                           double& cy) {
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    sim::rotated_size(b, rotation, w, h);
    cx = island.x + x + w * 0.5;
    cy = island.y + y + h * 0.5;
    const double f0 = fig.fahnoffs[0];
    const double f1 = fig.fahnoffs[1];
    switch (rotation & 3U) {
        case 0:
            cx += f0;
            cy += f1;
            break;
        case 1:
            cx -= f1;
            cy += f0;
            break;
        case 2:
            cx -= f0;
            cy -= f1;
            break;
        default:
            cx += f1;
            cy -= f0;
            break;
    }
}

namespace {

void attach_point(const sim::WorldIsland& island, const assets::BuildingType& b, std::uint32_t x, std::uint32_t y,
                  std::uint8_t rotation, const assets::FigureType& fig, double& cx, double& cy) {
    building_attach_point(island, b, x, y, rotation, fig, cx, cy);
}

}  // namespace

std::vector<BuildingFigure> collect_building_figures(const sim::World& world, const assets::BuildingTable& buildings,
                                                     const assets::FigureTable& figures) {
    std::vector<BuildingFigure> out;
    for (std::size_t index = 0; index < world.islands().size(); ++index) {
        collect_island_building_figures(world, buildings, figures, static_cast<std::uint16_t>(index), out);
    }
    return out;
}

void collect_island_building_figures(const sim::World& world, const assets::BuildingTable& buildings,
                                     const assets::FigureTable& figures, std::uint16_t index,
                                     std::vector<BuildingFigure>& out) {
    {
        const sim::WorldIsland& island = world.islands()[index];
        const sim::IslandMap& map = island.map;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& cell = map.top(x, y);
                if (cell.empty() || cell.owner == sim::kNoPlayer) {
                    continue;  // the original only spawns figures for buildings inside a city
                }
                const sim::SubTile sub = map.sub_tile(buildings, x, y);
                if (sub.building == nullptr || sub.origin_x != static_cast<std::int32_t>(x) ||
                    sub.origin_y != static_cast<std::int32_t>(y)) {
                    continue;  // one set of figures per building, at its origin tile
                }
                const assets::BuildingType& b = *sub.building;
                const auto add = [&](BuildingFigureRole role, std::uint16_t number) {
                    // Figure 0 is a real definition in figuren.cod, but 0 here means "none" (the
                    // pirate warehouse names an undefined FAHNEKONTOR and gets no flag, D-002).
                    const assets::FigureType* fig = number == 0 ? nullptr : figures.get(number);
                    if (fig == nullptr) {
                        return;
                    }
                    BuildingFigure f;
                    f.role = role;
                    if (role == BuildingFigureRole::Status) {
                        // Symbols (kinds 0x16, 0x19-0x1B) stand on the building centre at the
                        // building's Einhoffs height, not at the figure's Fahnoffs.
                        attach_point(island, b, x, y, cell.rotation, assets::FigureType{}, f.x, f.y);
                        f.z = b.icon_height;
                    } else {
                        attach_point(island, b, x, y, cell.rotation, *fig, f.x, f.y);
                        f.z = fig->fahnoffs[2];
                    }
                    f.figure = number;
                    f.owner = cell.owner;
                    f.phase = static_cast<std::uint8_t>((island.x + x) * 7 + (island.y + y) * 13);
                    f.island = static_cast<std::uint16_t>(index);
                    f.origin_x = static_cast<std::uint16_t>(x);
                    f.origin_y = static_cast<std::uint16_t>(y);
                    out.push_back(f);
                };
                if (b.prod_kind == assets::prod::kMarket || b.prod_kind == assets::prod::kKontor ||
                    b.prod_kind == assets::prod::kTower) {
                    add(BuildingFigureRole::Flag, b.attached_figures[0]);
                    if (b.prod_kind == assets::prod::kMarket) {
                        add(BuildingFigureRole::MarketFire, kFigureBrandMarkt);
                    }
                } else if (b.prod_kind == assets::prod::kHouse) {
                    add(BuildingFigureRole::Disaster, kFigureSensenmann);
                } else if (b.prod_kind == assets::prod::kShipyard) {
                    // Shipyards of the local player: the same icon as kind 0x19.
                    add(BuildingFigureRole::Status, kFigureNoAktiv);
                } else if (sim::is_producer(b)) {
                    add(BuildingFigureRole::Status, kFigureNoAktiv);
                    add(BuildingFigureRole::Smoke, b.attached_figures[0]);
                }
            }
        }
        // Ore and gold symbols: on the origin of the ore mountain a deposit lies at;
        // whether the local player sees one is decided when drawing.
        for (const auto& d : island.header.deposits) {
            if ((d.ware != assets::ware::kOre && d.ware != assets::ware::kGold) || d.x >= map.width() ||
                d.y >= map.height()) {
                continue;
            }
            const sim::SubTile sub = map.sub_tile(buildings, d.x, d.y);
            if (sub.building == nullptr || sub.building->prod_kind != assets::prod::kOreMountain ||
                sub.origin_x != d.x || sub.origin_y != d.y) {
                continue;
            }
            const std::uint16_t number = d.ware == assets::ware::kGold ? kFigureShowGold : kFigureShowEisen;
            if (figures.get(number) == nullptr) {
                continue;
            }
            BuildingFigure f;
            f.role = BuildingFigureRole::Deposit;
            attach_point(island, *sub.building, d.x, d.y, map.top(d.x, d.y).rotation, assets::FigureType{}, f.x, f.y);
            f.z = sub.building->icon_height;
            f.figure = number;
            f.phase = static_cast<std::uint8_t>((island.x + d.x) * 7 + (island.y + d.y) * 13);
            f.island = static_cast<std::uint16_t>(index);
            f.origin_x = d.x;
            f.origin_y = d.y;
            out.push_back(f);
        }
    }
}

namespace {

/// Body of a display figure at world point (x, y) with height z: centred,
/// bottom at Y + Posoffs[0] * w/64 - trunc(z * w/2), where Y = y + Posoffs[1] * w/64 is also its
/// depth. The original truncates each term; Open1602's tile rows start one pixel lower
/// than the original's, so figures go one pixel up. For flags and smoke both
/// offsets cancel out (Posoffs -40, 40 / -20, 20 ...).
void place_display_figure(const WorldView& view, const AtlasRect& r, const assets::FigureType& fig, double x, double y,
                          double z, DrawCommand& c, FigureSprites& out) {
    double sx = 0.0;
    double sy = 0.0;
    project_point(view, x, y, sx, sy);
    const std::int32_t tw = tile_width(view.zoom);
    const std::int32_t depth = static_cast<std::int32_t>(std::floor(sy)) + fig.posoffs[1] * tw / 64;
    c.x = static_cast<std::int32_t>(std::floor(sx)) - r.width / 2;
    c.y = depth + fig.posoffs[0] * tw / 64 - static_cast<std::int32_t>(z * tw / 2.0) - 1;
    out.depth = depth;
    out.row = floor_div(depth - 1, tile_height(view.zoom) / 2);
}

}  // namespace

bool building_figure_sprites(const BuildingFigure& figure, const sim::World& world,
                             const assets::BuildingTable& buildings, const WorldView& view_in,
                             const WorldLayers& layers, std::uint64_t time_ms, FigureSprites& out) {
    out.count = 0;
    if (layers.figures == nullptr || layers.atlas == nullptr) {
        return false;
    }
    const assets::FigureType* fig = layers.figures->get(figure.figure);
    // The original makes no display figures on an island the local player has not discovered. A
    // deposit the player knows is the exception: by ship that needs the island discovered anyway,
    // with the island resources known from the start (Rules::reveal_resources) it does not.
    if (fig == nullptr ||
        (figure.role != BuildingFigureRole::Deposit && undiscovered(world, layers.local_player, figure.island))) {
        return false;
    }
    const WorldView view = fit_view(view_in, world);
    DrawCommand& c = out.parts[0];
    double z = figure.z;
    c = DrawCommand{};
    switch (figure.role) {
        case BuildingFigureRole::Flag: {
            // Flag figures have one 8-frame ANIM per original colour (FAHNEMARKT: AnimOffs 0, 8, ...
            // 40 = the same sprites as the ship flags), so the banner's flag images are used directly.
            const sim::Player* owner = world.player(figure.owner);
            if (owner == nullptr || fig->sheet != assets::FigureSheet::Ship) {
                return false;
            }
            const auto sprite = flag_sprite(layers, owner->banner, time_ms / kFlagFrameMs + figure.phase);
            if (!sprite) {
                return false;
            }
            c.sprite = *sprite;
            break;
        }
        case BuildingFigureRole::Deposit: {
            // Drawn only for the local player who knows this deposit (byte 3), also on an island
            // they have not discovered (see above).
            if (figure.island >= world.islands().size() || layers.local_player == sim::kNoPlayer) {
                return false;
            }
            const sim::WorldIsland& island = world.islands()[figure.island];
            const auto& deposits = island.header.deposits;
            const bool known = std::any_of(deposits.begin(), deposits.end(), [&](const auto& d) {
                return d.x == figure.origin_x && d.y == figure.origin_y && d.ware != 0 &&
                       sim::deposit_known(d, layers.local_player);
            });
            // Kind 0x1B draws from EFFEKTE.BSH like the other symbols (SHOWEISEN has no Blocknr).
            const auto base = layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Effekte)];
            if (!known || !base) {
                return false;
            }
            const assets::FigureAnim* a = fig->anim(0);
            std::uint32_t frame = 0;
            if (a != nullptr && a->count > 0) {
                const std::uint64_t speed = a->speed_ms > 0 ? a->speed_ms : 1;
                frame = static_cast<std::uint32_t>((time_ms / speed + figure.phase) % a->count);
            }
            c.sprite = *base + assets::figure_sprite(*fig, 0, frame, 0, view.rotation);
            break;
        }
        case BuildingFigureRole::Disaster:
        case BuildingFigureRole::MarketFire: {
            // The reaper over a house with the plague (ANIM 0), flames over a house on
            // fire (ANIM 1, 0.4 lower) and over a burning market of the local player.
            const auto base = layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Effekte)];
            if (!base) {
                return false;
            }
            std::uint8_t anim = 0;
            if (figure.role == BuildingFigureRole::Disaster) {
                const sim::House* house = world.house(figure.island, figure.origin_x, figure.origin_y);
                const std::uint16_t dis = house != nullptr ? (house->services & sim::house_service::kDisasterMask) : 0;
                if (dis == 0) {
                    return false;
                }
                anim = dis == 1 ? 0 : 1;
                if (anim == 1) {
                    z -= 0.4;
                }
            } else {
                const sim::Production* state = world.production(figure.island, figure.origin_x, figure.origin_y);
                if (state == nullptr || !state->burning() || figure.owner != layers.local_player) {
                    return false;
                }
                anim = 2;  // BRANDMARKT is NOAKTIV placed for markets: ANIM 2 are the flames (status 3)
            }
            const assets::FigureAnim* a = fig->anim(anim);
            std::uint32_t frame = 0;
            if (a != nullptr && a->count > 0) {
                const std::uint64_t speed = a->speed_ms > 0 ? a->speed_ms : 1;
                frame = static_cast<std::uint32_t>((time_ms / speed + figure.phase) % a->count);
            }
            c.sprite = *base + assets::figure_sprite(*fig, anim, frame, 0, view.rotation);
            break;
        }
        case BuildingFigureRole::Smoke:
        case BuildingFigureRole::Status: {
            const sim::Production* state = world.production(figure.island, figure.origin_x, figure.origin_y);
            // The draw kinds pick their sheet (kinds 0x16/0x17 always EFFEKTE.BSH), not
            // the figure's Blocknr - the smoke and icon figures have none.
            const auto base = layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Effekte)];
            if (!base) {
                return false;
            }
            std::uint8_t anim = 0;
            if (state == nullptr && figure.role == BuildingFigureRole::Status) {
                // A shipyard of the local player (kind 0x19): on fire -> ANIM 2, 0.4
                // lower; switched off -> ANIM 0; Maxnorohst cycles without material -> ANIM 1.
                if (figure.owner != layers.local_player || figure.owner == sim::kNoPlayer) {
                    return false;
                }
                const sim::CostBuilding* yard = shipyard_at(world, figure.island, figure.origin_x, figure.origin_y);
                if (yard == nullptr) {
                    return false;
                }
                const sim::Disaster* disaster =
                    sim::disaster_at(world, figure.island, figure.origin_x, figure.origin_y);
                if (disaster != nullptr && disaster->kind == sim::Disaster::Kind::Fire) {
                    anim = 2;
                    z -= 0.4;
                } else if (!yard->enabled) {
                    anim = 0;
                } else {
                    const sim::MapCell& cell = world.islands()[figure.island].map.top(figure.origin_x, figure.origin_y);
                    const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
                    if (b == nullptr || b->max_idle_cycles == 0 || yard->idle_cycles < b->max_idle_cycles) {
                        return false;
                    }
                    anim = 1;
                }
            } else if (state == nullptr) {
                return false;
            } else if (figure.role == BuildingFigureRole::Smoke) {
                if (!sim::smoke_visible(*state)) {
                    return false;
                }
                // Drawn every second pixel: 50 % translucent.
                c.mode = DrawCommand::Mode::Translucent;
                c.tint = assets::Rgba{255, 255, 255, kSmokeAlpha};
            } else {
                // Icon of the local player's buildings: anim 0 switched off, 1 no raw material.
                if (figure.owner != layers.local_player || figure.owner == sim::kNoPlayer) {
                    return false;
                }
                const sim::MapCell& cell = world.islands()[figure.island].map.top(figure.origin_x, figure.origin_y);
                const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
                const sim::ProductionStatus status =
                    b != nullptr ? sim::production_status(*state, *b) : sim::ProductionStatus::Ok;
                if (status == sim::ProductionStatus::Ok) {
                    return false;
                }
                anim = status == sim::ProductionStatus::Disabled ? 0 : status == sim::ProductionStatus::Burning ? 2 : 1;
            }
            // Endless animations: the original starts them at a random frame; here the
            // building's phase stands in for it so the picture is reproducible.
            const assets::FigureAnim* a = fig->anim(anim);
            std::uint32_t frame = 0;
            if (a != nullptr && a->count > 0) {
                const std::uint64_t speed = a->speed_ms > 0 ? a->speed_ms : 1;
                frame = static_cast<std::uint32_t>((time_ms / speed + figure.phase) % a->count);
            }
            c.sprite = *base + assets::figure_sprite(*fig, anim, frame, 0, view.rotation);
            break;
        }
    }
    const AtlasRect& r = layers.atlas->rect(c.sprite);
    if (r.empty()) {
        return false;
    }
    place_display_figure(view, r, *fig, figure.x, figure.y, z, c, out);
    out.count = 1;
    return true;
}

bool land_figure_sprites(const FigureInstance& figure, const sim::World& world, const WorldView& view_in,
                         const WorldLayers& layers, FigureSprites& out) {
    out.count = 0;
    if (layers.figures == nullptr || layers.atlas == nullptr) {
        return false;
    }
    const assets::FigureType* type = layers.figures->get(figure.figure);
    if (type == nullptr || undiscovered(world, layers.local_player, figure.island)) {
        return false;
    }
    // A package's image comes from the terrain sheet, whatever sheet the figure's kind draws from.
    const assets::FigureSheet sheet =
        assets::draws_package_image(*type) ? assets::FigureSheet::Stadtfld : figure.sheet.value_or(type->sheet);
    const auto base = layers.sheet_base[static_cast<std::size_t>(sheet)];
    if (!base) {
        return false;  // no graphics for this figure at this zoom level
    }
    // Units drawn per player colour: the variant of the owner's banner, or its tinted template.
    const assets::FigureType* body_type = type;
    std::optional<std::uint32_t> tinted_body;
    if (const auto variant = layers.figures->unit_of(figure.figure)) {
        const sim::Player* owner = world.player(figure.owner);
        const Banner* banner =
            owner != nullptr && layers.banners != nullptr ? layers.banners->get(owner->banner) : nullptr;
        const std::uint32_t image =
            assets::figure_sprite(*type, figure.anim, figure.frame, figure.direction, view_in.rotation);
        if (banner != nullptr && layers.template_tint != nullptr && sheet == assets::FigureSheet::Stadtfld &&
            layers.template_tint->sprite(owner->banner, image)) {
            // A package's tint template: in the colour of every banner, also of those with a drawn variant.
            tinted_body = layers.template_tint->sprite(owner->banner, image);
        } else if (banner != nullptr && banner->unit_variant && *banner->unit_variant < variant->count) {
            // A variant drawn from another sheet than the figure (one colour given a package's image) is not used.
            if (const assets::FigureType* v = layers.figures->get(variant->family + *banner->unit_variant);
                v != nullptr && assets::draws_package_image(*v) == assets::draws_package_image(*type)) {
                body_type = v;
            }
        } else if (banner != nullptr) {
            // Templates made from the four drawn colours: of SOLDAT.BSH, or of a package's image with a
            // block per colour (one image for all colours keeps its colours).
            const UnitTint* tint = sheet == assets::FigureSheet::Soldat     ? layers.unit_tint
                                   : sheet == assets::FigureSheet::Stadtfld ? layers.package_unit_tint
                                                                            : nullptr;
            if (tint != nullptr) {
                tinted_body = tint->sprite(owner->banner, image);
            }
        }
    }
    const WorldView view = fit_view(view_in, world);
    const std::int32_t tw = tile_width(view.zoom);
    double fx = 0.0;
    double fy = 0.0;
    project_point(view, figure.x, figure.y, fx, fy);
    // The original truncates the screen position; visible positions are not negative.
    const auto px = static_cast<std::int32_t>(std::floor(fx));
    // Y = y + Posoffs[1] * w/64: depth and reference for body and shadow.
    const std::int32_t p1 = type->posoffs[1] * tw / 64;
    const std::int32_t depth = static_cast<std::int32_t>(std::floor(fy)) + p1;
    // Body and shadow are lifted by the height, trunc(z * w/2) (17 px on land at full zoom), and by
    // one more pixel: Open1602's tile rows start one pixel below the original's, which puts the
    // centre of a tile exactly on the centre row of its diamond.
    const std::int32_t lift = static_cast<std::int32_t>(figure.z * tw / 2.0) + 1;

    // Shadow first (drawn below the body), centred on the lifted ground point.
    const assets::FigureType* shadow = layers.figures->get(type->shadow);
    const auto shadow_base = layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Schatten)];
    if (type->shadow != 0 && shadow != nullptr && shadow_base) {
        DrawCommand& c = out.parts[out.count];
        c.sprite =
            *shadow_base + assets::figure_sprite(*shadow, figure.anim, figure.frame, figure.direction, view.rotation);
        const AtlasRect& r = layers.atlas->rect(c.sprite);
        if (!r.empty()) {
            c.x = px - r.width / 2;
            c.y = depth - p1 - lift - r.height / 2 + r.height;
            c.mode = DrawCommand::Mode::Shadow;
            c.tint = assets::Rgba{0, 0, 0, kShadowAlpha};
            ++out.count;
        }
    }
    DrawCommand& body = out.parts[out.count];
    body = DrawCommand{};
    body.sprite = tinted_body ? *tinted_body
                              : *base + assets::figure_sprite(*body_type, figure.anim, figure.frame, figure.direction,
                                                              view.rotation);
    const AtlasRect& r = layers.atlas->rect(body.sprite);
    if (r.empty()) {
        out.count = 0;
        return false;
    }
    // Workers and animals in crops and young trees lose rows at the bottom, h * k / 256 of the
    // picture of this zoom level; the shadow goes up by as much; all of it hidden: nothing drawn
    // (case 0x0C).
    const std::int32_t cut = figure.crop >= kCropHidden ? r.height : r.height * figure.crop / 256;
    if (cut >= r.height) {
        out.count = 0;
        return false;
    }
    if (cut > 0) {
        for (std::uint8_t i = 0; i < out.count; ++i) {
            out.parts[i].y -= cut;  // the shadow
        }
        body.cut_bottom = static_cast<std::uint16_t>(cut);
    }
    body.x = px - r.width / 2;
    body.y = depth + type->posoffs[0] * tw / 64 - lift;
    ++out.count;
    // The height does not change the row (compares Y).
    out.depth = depth;
    out.row = floor_div(depth - 1, tile_height(view.zoom) / 2);
    return true;
}

bool effect_figure_sprites(const EffectFigure& figure, const sim::World& world, const WorldView& view_in,
                           const WorldLayers& layers, std::uint64_t time_ms, FigureSprites& out) {
    out.count = 0;
    if (layers.figures == nullptr || layers.atlas == nullptr) {
        return false;
    }
    const assets::FigureType* fig = layers.figures->get(figure.figure);
    if (fig == nullptr) {
        return false;
    }
    const auto base = layers.sheet_base[static_cast<std::size_t>(
        assets::draws_package_image(*fig) ? assets::FigureSheet::Stadtfld : figure.sheet)];
    if (!base || undiscovered(world, layers.local_player, figure.island)) {
        return false;
    }
    const WorldView view = fit_view(view_in, world);
    const double tw = tile_width(view.zoom);
    const assets::FigureAnim* a = fig->anim(figure.anim);
    std::uint32_t frame = 0;
    if (figure.frame) {
        frame = *figure.frame;
    } else if (a != nullptr && a->count > 0) {
        const std::uint64_t speed = a->speed_ms > 0 ? a->speed_ms : 1;
        frame = static_cast<std::uint32_t>(((time_ms + figure.phase) / speed) % a->count);
    }
    constexpr double kShadowHeight = 5.0;
    const assets::FigureType* shadow = fig->shadow != 0 ? layers.figures->get(fig->shadow) : nullptr;
    const auto shadow_base = layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Schatten)];
    if (shadow != nullptr && shadow_base && figure.z - figure.ground < kShadowHeight) {
        DrawCommand& c = out.parts[out.count];
        c = DrawCommand{};
        // The shadow figure's picture of the body's ANIM, frame and direction (called
        // with the shadow figure for kinds 0x1E and 0x21).
        c.sprite = *shadow_base + assets::figure_sprite(*shadow, figure.anim, frame, figure.direction, view.rotation);
        const AtlasRect& r = layers.atlas->rect(c.sprite);
        if (!r.empty()) {
            double sx = 0.0;
            double sy = 0.0;
            project_point(view, figure.x, figure.y, sx, sy);
            c.x = static_cast<std::int32_t>(std::floor(sx)) - r.width / 2;
            c.y = static_cast<std::int32_t>(std::floor(sy)) - static_cast<std::int32_t>(figure.ground * tw / 2.0) - 1 +
                  r.height / 2;
            c.mode = DrawCommand::Mode::Shadow;
            c.tint = assets::Rgba{0, 0, 0, kShadowAlpha};
            ++out.count;
        }
    }
    DrawCommand& c = out.parts[out.count];
    c = DrawCommand{};
    c.sprite = *base + assets::figure_sprite(*fig, figure.anim, frame, figure.direction, view.rotation);
    const AtlasRect& r = layers.atlas->rect(c.sprite);
    if (r.empty()) {
        out.count = 0;
        return false;
    }
    place_display_figure(view, r, *fig, figure.x, figure.y, figure.z, c, out);
    ++out.count;
    return true;
}

namespace {

/// The tile sprites of one pass (flat or Highflg) of a tile: the open sea, or the ground under a
/// building and the building.
struct TileSprites {
    std::array<DrawCommand, 2> parts{};
    std::uint8_t count = 0;
};

/// Which sprites a tile draws, shared by the draw list and picking so both see the same pictures.
struct TileDrawer {
    const sim::World& world;
    const assets::BuildingTable& buildings;
    const AnimationClock& clock;
    const ProductionPhases* phases;
    sim::PlayerId local_player;
    std::uint8_t rotation;
    std::int32_t tw;
    const assets::BuildingType* sea;
    std::uint32_t sea_frame;
    std::int32_t sea_elevation;

    TileDrawer(const sim::World& w, const assets::BuildingTable& b, const AnimationClock& c, const WorldView& view,
               const WorldLayers& layers)
        : world(w),
          buildings(b),
          clock(c),
          phases(layers.production_phases),
          local_player(layers.local_player),
          rotation(view.rotation),
          tw(tile_width(view.zoom)),
          sea(b.open_sea()),
          sea_frame(sea != nullptr ? c.frame(*sea) : 0),
          sea_elevation(sea != nullptr ? sea->posoffs * tw / 64 : 0) {}

    /// Tiles of production buildings take their frame from the building's state;
    /// `island` is kSea for tiles without such state (the ground layer).
    [[nodiscard]] DrawCommand cell(const sim::MapCell& cell, std::int32_t sx, std::int32_t row_bottom,
                                   std::uint16_t island, const sim::IslandMap* map, std::uint16_t lx,
                                   std::uint16_t ly) const {
        const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
        if (b == nullptr) {
            return DrawCommand{cell.sprite, sx, row_bottom};
        }
        std::uint32_t frame = 0;
        if (island != sim::World::kSea && has_production_animation(*b)) {
            const sim::SubTile sub = map->sub_tile(buildings, lx, ly);
            const sim::Production* state = world.production(island, static_cast<std::uint16_t>(sub.origin_x),
                                                            static_cast<std::uint16_t>(sub.origin_y));
            std::optional<std::uint8_t> phase;
            if (state != nullptr && phases != nullptr) {
                phase = phases->get(static_cast<std::size_t>(state - world.productions().data()));
            }
            frame = production_frame(*b, clock.frame(*b), state, phase);
        } else {
            frame = default_frame(cell, *b, clock.frame(*b));
        }
        return DrawCommand{building_sprite(cell, *b, frame, rotation), sx, row_bottom - b->posoffs * tw / 64};
    }

    /// Whether the tiles of `island` show only the ground layer: the local player has not
    /// discovered it (then the ground grid is drawn).
    [[nodiscard]] bool hidden(std::uint16_t island) const { return undiscovered(world, local_player, island); }

    /// The cell a tile shows (shown_cell).
    [[nodiscard]] const sim::MapCell* shown(const sim::World::Cell& wc) const {
        return shown_cell(world, local_player, wc);
    }

    /// What world tile (wx, wy) draws in the flat pass or the Highflg pass of its row; (sx,
    /// row_bottom) as in build_world_draw_list. The open sea is always flat.
    [[nodiscard]] TileSprites tile(std::uint32_t wx, std::uint32_t wy, const sim::World::Cell& wc, std::int32_t sx,
                                   std::int32_t row_bottom, bool high_pass) const {
        TileSprites out;
        const sim::IslandMap* map = wc.island != sim::World::kSea ? &world.islands()[wc.island].map : nullptr;
        const sim::MapCell* top = shown(wc);
        if (top == nullptr || top->empty()) {
            if (!high_pass && sea != nullptr) {
                out.parts[out.count++] =
                    DrawCommand{sea_sprite(*sea, sea_frame, wx, wy), sx, row_bottom - sea_elevation};
            }
            return out;
        }
        const assets::BuildingType* b = buildings.by_sprite(top->sprite);
        if ((b != nullptr && b->high) != high_pass) {
            return out;
        }
        if (top != &map->top(wc.local_x, wc.local_y)) {
            // The ground of a hidden island: one picture, no production state behind it.
            out.parts[out.count++] = cell(*top, sx, row_bottom, sim::World::kSea, map, 0, 0);
            return out;
        }
        if (b != nullptr && b->draw_ground) {
            const sim::MapCell& ground = map->ground(wc.local_x, wc.local_y);
            if (!ground.empty()) {
                out.parts[out.count++] = cell(ground, sx, row_bottom, sim::World::kSea, map, 0, 0);
            }
        }
        out.parts[out.count++] = cell(*top, sx, row_bottom, wc.island, map, wc.local_x, wc.local_y);
        return out;
    }
};

}  // namespace

void build_world_draw_list(const sim::World& world, const assets::BuildingTable& buildings, const AnimationClock& clock,
                           const WorldView& view, const Viewport& viewport, const WorldLayers& layers,
                           std::vector<DrawCommand>& commands) {
    DrawListScratch scratch;
    build_world_draw_list(world, buildings, clock, view, viewport, layers, commands, scratch);
}

void build_world_draw_list(const sim::World& world, const assets::BuildingTable& buildings, const AnimationClock& clock,
                           const WorldView& view_in, const Viewport& viewport, const WorldLayers& layers,
                           std::vector<DrawCommand>& commands, DrawListScratch& scratch) {
    const WorldView view = fit_view(view_in, world);
    const SpriteExtent& extent = layers.extent;
    commands.clear();
    if (viewport.width <= 0 || viewport.height <= 0) {
        return;
    }
    const RotatedWorld rw(view);
    const std::int32_t tw = tile_width(view.zoom);
    const std::int32_t th = tile_height(view.zoom);
    const std::int32_t half_w = tw / 2;
    const std::int32_t half_h = th / 2;
    const auto view_h = static_cast<std::int32_t>(rw.height);
    const auto view_w = static_cast<std::int32_t>(rw.width);
    const std::int32_t rows = view_w + view_h - 1;
    const std::int32_t max_elevation = tw;  // Posoffs is at most 64 * tw / 64 (land uses 20)

    // Rows: the bottom of row d is d * half_h + th - 1; its sprites extend upwards by at most
    // max_height + max_elevation pixels.
    const std::int32_t d_begin = std::max(0, floor_div(viewport.top - (th - 1), half_h));
    const std::int32_t d_end = std::min(
        rows - 1, floor_div(viewport.top + viewport.height + extent.height + max_elevation - (th - 1), half_h));
    if (d_begin > d_end) {
        return;
    }
    commands.reserve(static_cast<std::size_t>((viewport.width + extent.width) / half_w + 2) *
                     static_cast<std::size_t>(d_end - d_begin + 1) * 5 / 8);

    const TileDrawer drawer(world, buildings, clock, view, layers);
    const std::int32_t sea_elevation = drawer.sea_elevation;

    // Figures of the visible rows (ships, building flags, land figures), sorted by row and within
    // a row by depth, as the original sorts its one list by Y; equal depths keep this
    // order (the original's radix sort leaves them in an order that depends on the other keys).
    std::vector<FigureSprites>& figures = scratch.figures;
    figures.clear();
    const auto add_figure = [&](const FigureSprites& f) {
        if (f.row >= d_begin && f.row <= d_end) {
            figures.push_back(f);
            if (layers.dither != nullptr && layers.atlas != nullptr) {
                FigureSprites& added = figures.back();
                for (std::uint8_t i = 0; i < added.count; ++i) {
                    layers.dither->apply(added.parts[i], *layers.atlas);
                }
            }
        }
    };
    // Before any sprite is worked out (FB-3): a figure's pictures lie around its projected point,
    // at most a sprite extent and a tile wide to the sides, raised by the ground and the figure's
    // height; one whose point is further from the viewport is skipped. At far zoom this keeps the
    // row band across the whole world width out of the list.
    const double left_limit = viewport.left - extent.width - tw;
    const double right_limit = viewport.left + viewport.width + extent.width + tw;
    const double top_limit = viewport.top - tw;
    const double bottom_limit = viewport.top + viewport.height + extent.height + max_elevation + tw;
    const auto near_view = [&](double x, double y, double z) {
        double sx = 0.0;
        double sy = 0.0;
        project_point(view, x, y, sx, sy);
        return sx >= left_limit && sx <= right_limit && sy >= top_limit &&
               sy <= bottom_limit + std::max(z, 0.0) * tw / 2.0;
    };
    if (layers.figures != nullptr && layers.atlas != nullptr) {
        for (const sim::Ship& ship : world.ships()) {
            // The trail keeps a ship within a tile of its position.
            const bool placed = ship.pos_x != 0 || ship.pos_y != 0;
            const double px = placed ? static_cast<double>(ship.pos_x) / sim::kShipUnit : ship.x + 0.5;
            const double py = placed ? static_cast<double>(ship.pos_y) / sim::kShipUnit : ship.y + 0.5;
            if (!near_view(px, py, 2.0)) {
                continue;
            }
            ShipSprites s;
            if (ship_sprites(ship, world, view, layers, clock.figure_ms(), s)) {
                FigureSprites f;
                f.row = s.row;
                f.depth = s.depth;
                if (s.has_wave) {
                    f.parts[f.count++] = s.wave;
                }
                if (s.has_smoke && s.smoke_behind) {
                    f.parts[f.count++] = s.smoke;
                }
                f.parts[f.count++] = s.hull;
                if (s.has_flag) {
                    f.parts[f.count++] = s.flag;
                }
                if (s.has_smoke && !s.smoke_behind) {
                    f.parts[f.count++] = s.smoke;
                }
                if (s.has_sink) {
                    f.parts[f.count++] = s.sink;
                }
                add_figure(f);
            }
        }
        if (layers.building_figures != nullptr) {
            for (const BuildingFigure& bf : *layers.building_figures) {
                if (!near_view(bf.x, bf.y, bf.z)) {
                    continue;
                }
                FigureSprites f;
                if (building_figure_sprites(bf, world, buildings, view, layers, clock.figure_ms(), f)) {
                    add_figure(f);
                }
            }
        }
        for (const std::vector<FigureInstance>* set : layers.land_figures) {
            for (const FigureInstance& instance : *set) {
                if (!near_view(instance.x, instance.y, instance.z)) {
                    continue;
                }
                FigureSprites f;
                if (land_figure_sprites(instance, world, view, layers, f)) {
                    add_figure(f);
                }
            }
        }
        if (layers.effects != nullptr) {
            for (const EffectFigure& e : *layers.effects) {
                if (!near_view(e.x, e.y, e.z)) {
                    continue;
                }
                FigureSprites f;
                if (effect_figure_sprites(e, world, view, layers, clock.elapsed_ms(), f)) {
                    add_figure(f);
                }
            }
        }
        std::stable_sort(figures.begin(), figures.end(), [](const FigureSprites& a, const FigureSprites& b) {
            return a.row != b.row ? a.row < b.row : a.depth < b.depth;
        });
    }
    std::size_t next_figure = 0;

    // Ownership view: a translucent diamond on the tile surface, after the flat pass of the row.
    std::vector<DrawCommand>& overlays = scratch.overlays;
    overlays.clear();
    // Marked tiles go on top of everything, so buildings do not hide them; hatched ones lie on the
    // ground like the ownership view unless marked on top. A tile marked twice shows its last mark.
    const auto mark_key = [](std::uint16_t island, std::uint16_t x, std::uint16_t y) {
        return (std::uint64_t{island} << 32U) | (std::uint64_t{x} << 16U) | y;
    };
    auto& marks = scratch.marks;
    marks.clear();
    std::vector<DrawCommand>& marked = scratch.marked;
    marked.clear();
    if (layers.tile_marks != nullptr) {
        for (const TileMark& m : *layers.tile_marks) {
            marks.emplace_back(mark_key(m.island, m.x, m.y), m);
        }
        std::stable_sort(marks.begin(), marks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    const auto add_overlay = [&](const sim::MapCell& cell, std::int32_t sx, std::int32_t row_bottom,
                                 std::uint16_t island, std::uint16_t lx, std::uint16_t ly, bool hidden) {
        const assets::BuildingType* b = buildings.by_sprite(cell.sprite);
        const std::int32_t elevation = b != nullptr ? b->posoffs * tw / 64 : 0;
        // An undiscovered island does not give its owners away.
        const sim::Player* p = hidden ? nullptr : world.player(cell.owner);
        const Banner* banner = p != nullptr && layers.banners != nullptr ? layers.banners->get(p->banner) : nullptr;
        if (layers.owner_tint && banner != nullptr) {
            const assets::Rgba tint{banner->colour.r, banner->colour.g, banner->colour.b, kOverlayAlpha};
            overlays.push_back(DrawCommand{DrawCommand::kOverlayDiamond, sx, row_bottom - elevation, tint,
                                           static_cast<std::uint8_t>(tw)});
        }
        if (!marks.empty()) {
            const std::uint64_t key = mark_key(island, lx, ly);
            if (const auto it = std::upper_bound(marks.begin(), marks.end(), key,
                                                 [](std::uint64_t k, const auto& e) { return k < e.first; });
                it != marks.begin() && std::prev(it)->first == key) {
                const TileMark& m = std::prev(it)->second;
                (m.hatched && !m.on_top ? overlays : marked)
                    .push_back(DrawCommand{m.hatched ? DrawCommand::kOverlayHatch : DrawCommand::kOverlayDiamond, sx,
                                           row_bottom - elevation, m.colour, static_cast<std::uint8_t>(tw)});
            }
        }
    };
    // The border of a catchment circle: the edges of its tiles towards tiles outside, on top of
    // everything, so neither buildings nor the island's edge cut it off. The land of another player
    // gets the same border while building (History Edition): the edges of its tiles towards tiles
    // of another city, another island or the sea.
    const bool circle = layers.lighting != nullptr && layers.lighting->outlined();
    const bool borders = layers.lighting != nullptr && layers.lighting->foreign_borders_shown();
    std::vector<DrawCommand>& outline = scratch.outline;
    outline.clear();
    const auto add_edges = [&](std::int32_t vx, std::int32_t vy, std::int32_t sx, std::int32_t row_bottom,
                               const sim::MapCell* cell, const auto& inside) {
        const auto outside = [&](std::int32_t nx, std::int32_t ny) {
            if (nx < 0 || ny < 0 || nx >= view_w || ny >= view_h) {
                return true;
            }
            std::uint32_t ox = 0;
            std::uint32_t oy = 0;
            rw.to_world(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny), ox, oy);
            return !inside(ox, oy);
        };
        std::uint32_t edges = 0;
        edges |= outside(vx - 1, vy) ? std::uint32_t{DrawCommand::kTopLeft} : 0U;
        edges |= outside(vx, vy - 1) ? std::uint32_t{DrawCommand::kTopRight} : 0U;
        edges |= outside(vx + 1, vy) ? std::uint32_t{DrawCommand::kBottomRight} : 0U;
        edges |= outside(vx, vy + 1) ? std::uint32_t{DrawCommand::kBottomLeft} : 0U;
        if (edges == 0) {
            return;
        }
        const assets::BuildingType* b = cell != nullptr && !cell->empty() ? buildings.by_sprite(cell->sprite) : nullptr;
        const std::int32_t elevation = b != nullptr ? b->posoffs * tw / 64 : sea_elevation;
        DrawCommand c{DrawCommand::kOverlayOutline, sx, row_bottom - elevation, kCircleOutline,
                      static_cast<std::uint8_t>(tw)};
        c.edges = static_cast<std::uint8_t>(edges);
        outline.push_back(c);
    };
    const auto add_outline = [&](std::int32_t vx, std::int32_t vy, std::uint32_t wx, std::uint32_t wy, std::int32_t sx,
                                 std::int32_t row_bottom, const sim::MapCell* cell) {
        if (layers.lighting->in_outline(wx, wy)) {
            add_edges(vx, vy, sx, row_bottom, cell,
                      [&](std::uint32_t ox, std::uint32_t oy) { return layers.lighting->in_outline(ox, oy); });
        }
    };
    const auto add_border = [&](std::int32_t vx, std::int32_t vy, std::int32_t sx, std::int32_t row_bottom,
                                const sim::World::Cell& wc, const sim::MapCell* cell) {
        if (!layers.lighting->foreign(cell)) {
            return;
        }
        add_edges(vx, vy, sx, row_bottom, cell, [&](std::uint32_t ox, std::uint32_t oy) {
            const sim::World::Cell& other = world.cell(ox, oy);
            const sim::MapCell* o = other.island == wc.island ? drawer.shown(other) : nullptr;
            return o != nullptr && o->owner == cell->owner && o->city == cell->city;
        });
    };
    for (std::int32_t d = d_begin; d <= d_end; ++d) {
        const std::int32_t row_bottom = d * half_h + th - 1;
        // Left edge of tile vx in this row: X = (2 * vx - d + view_h - 1) * half_w.
        // Visible if X + max_width > left and X < left + width (conservative bounds).
        const std::int32_t k_lo = floor_div(viewport.left - extent.width, half_w) + d - view_h + 1;
        const std::int32_t k_hi = floor_div(viewport.left + viewport.width, half_w) + d - view_h + 1;
        const std::int32_t vx_lo = std::max({0, d - view_h + 1, -floor_div(-k_lo, 2)});
        const std::int32_t vx_hi = std::min({view_w - 1, d, floor_div(k_hi, 2)});
        auto& high_tiles = scratch.high_tiles;
        high_tiles.clear();
        for (int pass = 0; pass < 2; ++pass) {
            const bool high_pass = pass == 1;
            const auto count = high_pass ? static_cast<std::int32_t>(high_tiles.size()) : vx_hi - vx_lo + 1;
            for (std::int32_t column = 0; column < count; ++column) {
                const auto vx = high_pass ? high_tiles[static_cast<std::size_t>(column)] : vx_lo + column;
                const std::int32_t vy = d - vx;
                const std::int32_t sx = (vx - vy + view_h - 1) * half_w;
                std::uint32_t wx = 0;
                std::uint32_t wy = 0;
                rw.to_world(static_cast<std::uint32_t>(vx), static_cast<std::uint32_t>(vy), wx, wy);
                const sim::World::Cell& wc = world.cell(wx, wy);
                const sim::IslandMap* map = nullptr;
                // What the tile shows: on an island the local player has not discovered, its ground.
                const sim::MapCell* cell = drawer.shown(wc);
                const bool hidden = cell != nullptr && drawer.hidden(wc.island);
                if (!high_pass && cell != nullptr && !cell->empty()) {
                    const auto* type = buildings.by_sprite(cell->sprite);
                    if (type != nullptr && type->high)
                        high_tiles.push_back(vx);
                }
                if (wc.island != sim::World::kSea) {
                    map = &world.islands()[wc.island].map;
                }
                if (!high_pass && circle) {
                    add_outline(vx, vy, wx, wy, sx, row_bottom, cell);
                }
                if (!high_pass && borders && !hidden) {
                    add_border(vx, vy, sx, row_bottom, wc, cell);
                }
                const std::size_t first = commands.size();
                // A dimmed tile: everything drawn for it (ground and building) darker.
                const auto dim = [&] {
                    if (layers.lighting == nullptr || !layers.lighting->active()) {
                        return;
                    }
                    // Water, the island's shallows, surf, river mouths and the bare beach included,
                    // is never land to build on: the whole shore is shaded like the sea, whoever's
                    // land it lies in, instead of bright strips along the own coast.
                    const assets::BuildingType* t =
                        cell != nullptr && !cell->empty() ? buildings.by_sprite(cell->sprite) : nullptr;
                    const bool land = t != nullptr && (t->kind < sim::building_kind::kSea ||
                                                       t->kind > sim::building_kind::kBeachVariant);
                    // A building is lit or dark as a whole, whichever of its tiles is drawn.
                    TileLighting::Footprint footprint{wx, wy, 1, 1};
                    if (t != nullptr && t->tile_count() > 1 && !hidden) {
                        const sim::SubTile sub = map->sub_tile(buildings, wc.local_x, wc.local_y);
                        const sim::WorldIsland& isl = world.islands()[wc.island];
                        footprint.x = static_cast<std::int64_t>(isl.x) + sub.origin_x;
                        footprint.y = static_cast<std::int64_t>(isl.y) + sub.origin_y;
                        sim::rotated_size(*t, cell->rotation, footprint.width, footprint.height);
                    }
                    if (!layers.lighting->dimmed(wx, wy, land ? cell : nullptr, footprint)) {
                        return;
                    }
                    for (std::size_t k = first; k < commands.size(); ++k) {
                        if (commands[k].mode == DrawCommand::Mode::Normal) {
                            commands[k].mode = DrawCommand::Mode::Dimmed;
                            commands[k].tint = assets::Rgba{kDimBrightness, kDimBrightness, kDimBrightness, 255};
                        }
                    }
                };
                if (!high_pass && cell != nullptr && !cell->empty()) {
                    add_overlay(*cell, sx, row_bottom, wc.island, wc.local_x, wc.local_y, hidden);
                }
                const TileSprites drawn = drawer.tile(wx, wy, wc, sx, row_bottom, high_pass);
                if (drawn.count == 0) {
                    continue;
                }
                commands.insert(commands.end(), drawn.parts.begin(), drawn.parts.begin() + drawn.count);
                dim();
                if (layers.sprite_checker != nullptr && layers.sprite_checker->contains(wx, wy)) {
                    for (std::size_t k = first; k < commands.size(); ++k) {
                        DrawCommand& c = commands[k];
                        if (c.mode == DrawCommand::Mode::Normal || c.mode == DrawCommand::Mode::Dimmed) {
                            c.mode = DrawCommand::Mode::Checkered;
                            c.tint = layers.sprite_checker->colour;
                        }
                    }
                }
                if (layers.sprite_tints != nullptr && layers.sprite_tints->contains(wx, wy)) {
                    const assets::Rgba t = layers.sprite_tints->colour;
                    for (std::size_t k = first; k < commands.size(); ++k) {
                        DrawCommand& c = commands[k];
                        if (c.mode == DrawCommand::Mode::Normal) {
                            c.mode = DrawCommand::Mode::Tinted;
                            c.tint = t;
                        } else if (c.mode == DrawCommand::Mode::Dimmed) {
                            c.mode = DrawCommand::Mode::Tinted;
                            c.tint = assets::Rgba{static_cast<std::uint8_t>(c.tint.r * t.r / 255),
                                                  static_cast<std::uint8_t>(c.tint.g * t.g / 255),
                                                  static_cast<std::uint8_t>(c.tint.b * t.b / 255), t.a};
                        }
                    }
                }
            }
            if (!high_pass) {
                commands.insert(commands.end(), overlays.begin(), overlays.end());
                overlays.clear();
            }
            if (!high_pass) {  // figures of this row between the two passes
                for (; next_figure < figures.size() && figures[next_figure].row <= d; ++next_figure) {
                    const FigureSprites& f = figures[next_figure];
                    commands.insert(commands.end(), f.parts.begin(), f.parts.begin() + f.count);
                }
            }
        }
    }
    commands.insert(commands.end(), marked.begin(), marked.end());
    commands.insert(commands.end(), outline.begin(), outline.end());
}

void tiles_at_pixel(const sim::World& world, const assets::BuildingTable& buildings, const AnimationClock& clock,
                    const WorldView& view_in, const WorldLayers& layers, const SpriteHitMask& mask, std::int32_t px,
                    std::int32_t py, std::vector<TileHit>& out) {
    out.clear();
    if (layers.atlas == nullptr) {
        return;
    }
    const WorldView view = fit_view(view_in, world);
    const SpriteExtent& extent = layers.extent;
    const RotatedWorld rw(view);
    const std::int32_t tw = tile_width(view.zoom);
    const std::int32_t th = tile_height(view.zoom);
    const std::int32_t half_w = tw / 2;
    const std::int32_t half_h = th / 2;
    const auto view_h = static_cast<std::int32_t>(rw.height);
    const auto view_w = static_cast<std::int32_t>(rw.width);
    const std::int32_t rows = view_w + view_h - 1;
    const std::int32_t max_elevation = tw;
    // The tiles build_world_draw_list keeps for a viewport of this one pixel, front to back: rows
    // from the front, per row the Highflg pass before the flat one, per pass right to left.
    const std::int32_t d_begin = std::max(0, floor_div(py - (th - 1), half_h));
    const std::int32_t d_end = std::min(rows - 1, floor_div(py + 1 + extent.height + max_elevation - (th - 1), half_h));
    const TileDrawer drawer(world, buildings, clock, view, layers);
    for (std::int32_t d = d_end; d >= d_begin; --d) {
        const std::int32_t row_bottom = d * half_h + th - 1;
        const std::int32_t k_lo = floor_div(px - extent.width, half_w) + d - view_h + 1;
        const std::int32_t k_hi = floor_div(px + 1, half_w) + d - view_h + 1;
        const std::int32_t vx_lo = std::max({0, d - view_h + 1, -floor_div(-k_lo, 2)});
        const std::int32_t vx_hi = std::min({view_w - 1, d, floor_div(k_hi, 2)});
        for (int pass = 1; pass >= 0; --pass) {
            const bool high_pass = pass == 1;
            for (std::int32_t vx = vx_hi; vx >= vx_lo; --vx) {
                const std::int32_t vy = d - vx;
                const std::int32_t sx = (vx - vy + view_h - 1) * half_w;
                std::uint32_t wx = 0;
                std::uint32_t wy = 0;
                rw.to_world(static_cast<std::uint32_t>(vx), static_cast<std::uint32_t>(vy), wx, wy);
                const sim::World::Cell& wc = world.cell(wx, wy);
                const TileSprites drawn = drawer.tile(wx, wy, wc, sx, row_bottom, high_pass);
                for (std::uint8_t i = 0; i < drawn.count; ++i) {
                    if (command_hit(drawn.parts[i], *layers.atlas, mask, px, py)) {
                        out.push_back(TileHit{wx, wy, wc.island, wc.local_x, wc.local_y, d, high_pass});
                        break;
                    }
                }
            }
        }
    }
}

}  // namespace o1602::render
