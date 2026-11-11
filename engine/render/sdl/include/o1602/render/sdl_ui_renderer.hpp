// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws the interface's draw lists (ui::Panel) with an SDL_Renderer: sprites of the interface's
// sheets, their shapes in one colour, text in the original fonts and colours, filled and
// checkered rectangles. Textures are made when first needed and kept (texts: the 512 most recently
// used, and all of the current frame).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/assets/palette.hpp"
#include "o1602/assets/sprite_sheet.hpp"
#include "o1602/render/text_cache.hpp"
#include "o1602/ui/font.hpp"
#include "o1602/ui/panel.hpp"
#include "o1602/ui/resources.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace o1602::render {

class SdlUiRenderer {
public:
    /// Loads the interface's sheets of one art size and the fonts of all three from the game
    /// directory (missing sheets draw nothing, a missing font is the large one); `data_fixes`: with
    /// the accents of the fonts' capitals in place (D-012).
    SdlUiRenderer(SDL_Renderer* renderer, const assets::GameDirectory& game, const assets::Palette& palette,
                  ui::ArtSize size, bool data_fixes = true);
    ~SdlUiRenderer();
    SdlUiRenderer(const SdlUiRenderer&) = delete;
    SdlUiRenderer& operator=(const SdlUiRenderer&) = delete;

    [[nodiscard]] ui::ArtSize art_size() const noexcept { return m_size; }
    /// The font slots that text drawn at `scale` (draw()) takes, for panels that measure or wrap
    /// it: those of ui::text_art_size(scale).
    [[nodiscard]] std::array<const ui::Font*, ui::kFontSlots> fonts(double scale) const noexcept;
    /// Size of a sprite of a sheet at the art's own scale, nothing if there is none. A sprite number
    /// of a package's icon (ui::package_icons()) is that icon in any sheet.
    [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>> sprite_size(std::int32_t block,
                                                                                   std::int32_t gfx) const;

    /// Draws the items with their region's top left at (x, y); `scale` is the region's scale
    /// (screen width / 1024), the art is stretched from its own size to it.
    void draw(const std::vector<ui::DrawItem>& items, std::int32_t x, std::int32_t y, double scale);
    /// A window's art made as wide as `rect`: its left two thirds, copies of its middle third and its
    /// right third at the right edge, so the frame closes there (status bar, mission window).
    void draw_widened(std::int32_t block, std::int32_t gfx, const ui::Rect& rect, double scale);
    /// Below a window's art where `rect` is taller than it (the side panel in a tall window): copies
    /// of the art's lower half, every other one upside down so that no seam shows, and the art's last
    /// `edge` rows (its frame) at the bottom of `rect`. Drawn over the art, which its layout draws.
    void draw_lengthened(std::int32_t block, std::int32_t gfx, const ui::Rect& rect, double scale,
                         std::int32_t edge = 0);
    /// A rectangle of a palette colour (backgrounds).
    void fill(const ui::Rect& rect, std::uint8_t colour);
    /// An RGBA image as a texture at (x, y), size (w, h) on screen (the minimap). The texture is
    /// kept under `key` and replaced when the image changes size or `update` is set.
    void image(int key, const std::vector<std::uint8_t>& rgba, std::int32_t width, std::int32_t height,
               const ui::Rect& at, bool update);
    /// After a frame is shown: its texts' textures may be given up again when the cache is full.
    /// Sprites not drawn for longest go too while they take more than a cap (FB-23).
    void next_frame();
    /// Retain only an opaque, fully contained control-panel background. Dynamic
    /// selection panels, minimap, tooltips and the world keep their normal draws.
    void opaque_panel(const std::vector<ui::DrawItem>& items,const ui::Rect& region,double scale,int extension,int edge);
    void cache_opaque_panel(bool enabled) noexcept { m_panel_enabled=enabled; m_panel_valid=false; }
    [[nodiscard]] std::uint64_t panel_hits() const noexcept { return m_panel_hits; }
    [[nodiscard]] std::uint64_t panel_builds() const noexcept { return m_panel_builds; }
    /// Opaque parchment and a complete draw list contained inside `region`.
    /// Identical lists reuse the picture; input and statistics still update normally.
    void opaque_window(const std::vector<ui::DrawItem>& items, const ui::Rect& region, double scale,
                       int background_key, const std::vector<std::uint8_t>& background, int width, int height);
    void cache_opaque_window(bool enabled) noexcept { m_window_enabled=enabled; m_window_valid=false; }
    [[nodiscard]] std::uint64_t window_hits() const noexcept { return m_window_hits; }
    [[nodiscard]] std::uint64_t window_builds() const noexcept { return m_window_builds; }
    /// The render device was reset and the textures lost their contents: all go and are made
    /// again when next drawn (an image from the pixels given then).
    void release_textures();
    /// The packages' icons changed (ui::set_package_icons(), ui::set_interface_sprites()): the
    /// sprites are made again when next drawn.
    void forget_package_icons();
    /// The renderer was made again (the device was lost): textures come from `renderer` from now
    /// on. release_textures() goes before the old renderer does.
    void set_renderer(SDL_Renderer* renderer) noexcept { m_renderer = renderer; }

private:
    struct Texture {
        SDL_Texture* texture = nullptr;
        std::int32_t w = 0;
        std::int32_t h = 0;
    };
    Texture sprite(std::int32_t block, std::int32_t gfx, int mode, std::int32_t colour,
                   const std::optional<ui::ColourTint>& tint = std::nullopt);
    /// Sprite `gfx` of a sheet recoloured (ui::ColourTint, render/variant_tint.hpp, render/shape_tint.hpp).
    [[nodiscard]] std::vector<std::uint8_t> tinted(const assets::SpriteSheet& sheet, std::int32_t gfx,
                                                   const ui::ColourTint& tint) const;
    /// The picture of a package's icon at the art's scale, or its shape in `colour` (mask, raster).
    /// A package's icon; a template icon (PackageIcon::tint) tinted in `player`, if given.
    Texture package_icon(const assets::PackageIcon& icon, int mode, std::int32_t colour,
                         std::optional<assets::Rgba> player);
    /// The font of a slot and size: the large one where that size has none, nullptr for none.
    [[nodiscard]] const ui::Font* font(std::int32_t slot, ui::ArtSize size) const noexcept;
    /// Text in text colour `colour`, its letters in palette colour `ink` unless that is -1 (DrawItem::ink).
    Texture text(const std::string& s, std::int32_t slot, ui::ArtSize size, std::int32_t colour, std::int32_t ink = -1);
    Texture make(const std::vector<std::uint8_t>& rgba, std::int32_t w, std::int32_t h);
    [[nodiscard]] double art_scale() const noexcept;

    SDL_Renderer* m_renderer;
    assets::Palette m_palette;
    ui::ArtSize m_size;
    std::array<std::optional<assets::SpriteSheet>, 5> m_sheets;
    /// Per art size (ui::ArtSize) the font slots.
    std::array<std::array<std::optional<ui::Font>, ui::kFontSlots>, 3> m_fonts;
    std::array<ui::Recolour, ui::kTextColours> m_colours{};
    /// Block, gfx, mode, colour, tint (0: none, else 0x01RRGGBB), its stride, variants, pattern, base (a byte each),
    /// shape.
    using SpriteKey = std::tuple<std::int32_t, std::int32_t, int, std::int32_t, std::uint32_t, std::int32_t,
                                 std::uint32_t, std::optional<std::int32_t>>;
    struct CachedSprite {
        Texture texture;
        std::uint64_t frame = 0;  ///< the frame it was last drawn in
    };
    std::map<SpriteKey, CachedSprite> m_sprites;
    std::size_t m_sprite_bytes = 0;  ///< RGBA bytes of the cached sprites
    std::uint64_t m_frame = 0;
    /// Releases the sprites drawn longest ago, never those of this frame, until they take at most
    /// `bytes`.
    void trim_sprites(std::size_t bytes);
    TextCache<Texture> m_texts;
    std::map<int, Texture> m_images;
    SDL_Texture* m_panel=nullptr;
    bool m_panel_valid=false,m_panel_failed=false;
#ifdef O1602_EXPERIMENTAL_VITA
    bool m_panel_enabled=true;
#else
    bool m_panel_enabled=false;
#endif
    int m_panel_w=0,m_panel_h=0,m_panel_extension=0,m_panel_edge=0;
    double m_panel_scale=0;
    ui::Rect m_panel_region{};
    std::vector<ui::DrawItem> m_panel_items;
    std::uint64_t m_panel_hits=0,m_panel_builds=0;
    SDL_Texture* m_window=nullptr;
    bool m_window_enabled=false,m_window_valid=false,m_window_failed=false;
    int m_window_w=0,m_window_h=0,m_window_key=0;
    double m_window_scale=0;
    ui::Rect m_window_region{};
    std::vector<ui::DrawItem> m_window_items;
    std::uint64_t m_window_hits=0,m_window_builds=0;
    Texture m_checker;  ///< made when first needed
};

}  // namespace o1602::render
