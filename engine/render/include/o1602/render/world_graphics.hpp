// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loads the sprite sheets of one zoom level into a single atlas: terrain plus the figure sheets
// (ships, animals, shadows, ...). Shared by the tools and the viewer.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/core/result.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/render/dither.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/sprite_hit_mask.hpp"
#include "o1602/render/unit_tint.hpp"

namespace o1602::render {

/// Path of a sheet of a zoom level, e.g. "mgfx/tiere.bsh".
[[nodiscard]] std::string sheet_path(assets::FigureSheet sheet, Zoom zoom);

struct GraphicsOptions {
    /// Some editions of the original have no animals, carriers or soldiers at the smallest zoom
    /// level (SGFX without TIERE.BSH, TRAEGER.BSH, SOLDAT.BSH, ...; the GOG edition has them). If
    /// set, missing sheets are made from the next larger level at half size, so these figures stay
    /// visible when zoomed out.
    bool derive_missing_sheets = true;
    std::uint32_t page_size = 2048;
    /// Checked while the sprites are decoded: once set, loading stops with an error (the game is
    /// quitting and waits for the thread that loads).
    const std::atomic<bool>* cancel = nullptr;
    /// Also the dithered copies of SCHATTEN.BSH and EFFEKTE.BSH (WorldGraphics::dither), for the
    /// classic graphics: a few hundred sprites more, an atlas page more at the larger zoom levels.
    bool dither = false;
    /// The images of the content packages (assets/package_graphics.hpp): their sprites join the
    /// terrain's at their sprite numbers. An image that cannot be read stays empty, with a warning.
    std::vector<assets::PackageImage> package_images{};
    /// Sprites of the terrain sheet (with the packages' images) drawn as tint templates, as first
    /// sprite and count: the units of packages with `tint = true` (WorldGraphics::template_tint).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> template_sprites{};
};

struct WorldGraphics {
    SpriteAtlas atlas;
    /// First atlas index of each sheet, nothing if the sheet is not available at this zoom level.
    std::array<std::optional<std::uint32_t>, assets::kFigureSheetCount> base{};
    /// Sheets made from the next larger zoom level.
    std::array<bool, assets::kFigureSheetCount> derived{};
    /// Per banner (BannerTable index): atlas index of the first image of its tinted flag, nothing
    /// for banners without a tinted flag.
    std::vector<std::optional<std::uint32_t>> banner_flags;
    /// Units in banner colours without drawn variants (needs the soldier sheet and the figures).
    UnitTint unit_tint;
    /// The same for package units whose image has a block per colour (terrain sheet).
    UnitTint package_unit_tint;
    /// Package units drawn as tint templates (GraphicsOptions::template_sprites), for every banner.
    UnitTint template_tint;
    /// Shadows (SCHATTEN.BSH) and smoke (EFFEKTE.BSH) as the original draws them, for the classic
    /// graphics (WorldLayers::dither); empty unless GraphicsOptions::dither.
    Dither dither;

    [[nodiscard]] std::optional<std::uint32_t> sheet_base(assets::FigureSheet sheet) const noexcept {
        return base[static_cast<std::size_t>(sheet)];
    }
};

/// Terrain (STADTFLD) is required; every sheet in `figure_sheets` is optional and simply missing
/// from WorldGraphics::base if neither it nor (with derive_missing_sheets) its larger version exists.
/// With `banners`, the generic flag is tinted once per banner with FlagStyle::Tinted (needs the
/// ship sheet). With `banners` and `figures`, the units drawn per colour (FigureTable::kUnitFamilies)
/// are tinted once per banner without unit_variant (needs the soldier sheet).
core::Result<WorldGraphics> load_world_graphics(const assets::GameDirectory& game, const assets::Palette& palette,
                                                Zoom zoom, std::span<const assets::FigureSheet> figure_sheets,
                                                const GraphicsOptions& options = {},
                                                const BannerTable* banners = nullptr,
                                                const assets::FigureTable* figures = nullptr);

/// A zoom level's graphics as far as they are made without the GPU: the atlas with its pixels and
/// the masks of the figures for picking units. Reads only files and its arguments, so it may run
/// on another thread while the game goes on (GraphicsPreloader).
struct PreparedGraphics {
    WorldGraphics graphics;
    SpriteHitMask hit_mask;  ///< visible pixels of all sprites, for picking buildings and units
    std::uint32_t ms = 0;    ///< time it took
};

/// load_world_graphics plus the figures' hit mask.
core::Result<PreparedGraphics> prepare_world_graphics(const assets::GameDirectory& game, const assets::Palette& palette,
                                                      Zoom zoom, std::span<const assets::FigureSheet> figure_sheets,
                                                      const GraphicsOptions& options = {},
                                                      const BannerTable* banners = nullptr,
                                                      const assets::FigureTable* figures = nullptr);

/// The sheets the world renderer uses: ships, animals, shadows, effects (smoke, status icons),
/// soldiers, carriers and carts (TRAEGER.BSH), farm workers (MAEHER.BSH), fish, jugglers and the
/// group numbers over selected units (NUMBERS.BSH).
inline constexpr std::array<assets::FigureSheet, 10> kWorldFigureSheets = {
    assets::FigureSheet::Ship,    assets::FigureSheet::Tiere,  assets::FigureSheet::Schatten,
    assets::FigureSheet::Effekte, assets::FigureSheet::Soldat, assets::FigureSheet::Traeger,
    assets::FigureSheet::Maeher,  assets::FigureSheet::Fische, assets::FigureSheet::Gaukler,
    assets::FigureSheet::Numbers};

}  // namespace o1602::render
