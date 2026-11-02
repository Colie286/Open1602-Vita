// SPDX-License-Identifier: GPL-3.0-or-later
// Figure animations (ANIM), land figures with shadows, half-size sheets.

#include <algorithm>
#include <optional>
#include <span>
#include <utility>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/render/dither.hpp"
#include "o1602/render/figure_animation.hpp"
#include "o1602/render/flag_tint.hpp"
#include "o1602/render/land_figures.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// A deer like HIRSCH (89) with its shadow SCHATTENLANG (117), small numbers for the sheets.
assets::FigureTable make_deer() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 89\n  Gfx: 4\n  Blocknr: 5\n  Rotate: 8\n  Posoffs: 3, 9\n  Schatten: 117\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 135\n"
        "    Nummer: 1\n    Kind: 2\n    AnimOffs: 112\n    AnimAdd: 1\n    AnimAnz: 6\n    AnimSpeed: 130\n"
        "    AnimNr: 1, 1, 3, 1\n    AnimRept: 2\n    Rotate: 6\n"
        "    Nummer: 3\n    Kind: 3\n    AnimOffs: 64\n    AnimAdd: 1\n    AnimAnz: 3\n    AnimSpeed: 130\n"
        "    AnimNr: 1\n    Rotate: 6\n"
        "  EndObj\n"
        "  Nummer: 117\n  Gfx: 8\n  Blocknr: 6\n  Rotate: 8\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 1\n    AnimOffs: 0\n    AnimAdd: 1\n    AnimAnz: 8\n    AnimSpeed: 135\n"
        "  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

}  // namespace

TEST_CASE("figures: ANIM objects and the sprite formula") {
    const auto figures = make_deer();
    const assets::FigureType* deer = figures.get(89);
    REQUIRE(deer != nullptr);
    CHECK(deer->shadow == 117);
    CHECK(deer->sheet == assets::FigureSheet::Tiere);
    REQUIRE(deer->anim(0) != nullptr);
    CHECK(deer->anim(0)->rotate == 8);  // no Rotate: the figure's
    REQUIRE(deer->anim(1) != nullptr);
    const assets::FigureAnim& graze = *deer->anim(1);
    CHECK(graze.kind == assets::AnimKind::Random);
    CHECK(graze.offset == 112);
    CHECK(graze.count == 6);
    CHECK(graze.repeat == 2);
    CHECK(graze.speed_ms == 130);
    CHECK(graze.rotate == 6);
    CHECK((graze.next == std::vector<std::uint8_t>{1, 1, 3, 1}));
    CHECK(deer->anim(2) == nullptr);
    CHECK(deer->anim(0)->repeat == 1);

    // Gfx + AnimOffs + AnimAdd * frame + ((dir + 2 * rotation) & 7) * Rotate
    CHECK(assets::figure_sprite(*deer, 1, 2, 3, 0) == 4 + 112 + 2 + 3 * 6);
    CHECK(assets::figure_sprite(*deer, 1, 2, 3, 3) == 4 + 112 + 2 + 1 * 6);  // (3 + 6) & 7 = 1
    CHECK(assets::figure_sprite(*deer, 0, 5, 7, 0) == 4 + 5 + 7 * 8);
    // An animation the figure lacks: Gfx + direction * Rotate (shadows of grazing animals).
    const assets::FigureType* shadow = figures.get(117);
    REQUIRE(shadow != nullptr);
    CHECK(assets::figure_sprite(*shadow, 1, 4, 2, 0) == 8 + 2 * 8);
}

TEST_CASE("figures: an animation of more than 255 frames or runs still ends (FB-19)") {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 5\n  Gfx: 0\n  Blocknr: 5\n  Rotate: 8\n"
        "  Objekt: ANIM\n"
        "    Nummer: 0\n    Kind: 3\n    AnimAnz: 300\n    AnimSpeed: 1\n    AnimRept: 400\n    AnimNr: 1\n"
        "    Nummer: 1\n    Kind: 1\n    AnimAnz: 2\n    AnimSpeed: 1\n"
        "  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    const assets::FigureType& f = *table->get(5);
    CHECK(f.anim(0)->count == 255);
    CHECK(f.anim(0)->repeat == 255);
    render::AnimState s;
    render::advance_animation(s, f, 255 * 255);
    CHECK(s.anim == 1);  // JUMPTO after 255 runs of 255 frames
}

TEST_CASE("figures: animation stepping") {
    const auto figures = make_deer();
    const assets::FigureType& deer = *figures.get(89);

    render::AnimState s;
    s.anim = 0;  // ENDLESS, 8 frames of 135 ms
    render::advance_animation(s, deer, 134);
    CHECK(s.frame == 0);
    render::advance_animation(s, deer, 1);
    CHECK(s.frame == 1);
    render::advance_animation(s, deer, 135 * 7);
    CHECK(s.frame == 0);  // wrapped around
    CHECK(s.anim == 0);

    // JUMPTO: after one run (3 frames) continue with AnimNr[0] = 1.
    s = render::AnimState{};
    s.anim = 3;
    render::advance_animation(s, deer, 130 * 3);
    CHECK(s.anim == 1);
    CHECK(s.frame == 0);

    // RANDOM with AnimRept 2: two runs of 6 frames, then one of AnimNr (1 or 3).
    s = render::AnimState{};
    s.anim = 1;
    s.random = 12345;
    render::advance_animation(s, deer, 130 * 6);
    CHECK(s.anim == 1);
    CHECK(s.runs == 1);
    render::advance_animation(s, deer, 130 * 6);
    CHECK((s.anim == 1 || s.anim == 3));
    CHECK(s.runs == 0);

    // Same seed, same course.
    render::AnimState a = render::start_animation(deer, 1, 42);
    render::AnimState b = render::start_animation(deer, 1, 42);
    for (int i = 0; i < 100; ++i) {
        render::advance_animation(a, deer, 97);
        render::advance_animation(b, deer, 97);
    }
    CHECK(a.anim == b.anim);
    CHECK(a.frame == b.frame);
    CHECK(render::start_animation(deer, 1, 1).frame < 6);
}

TEST_CASE("figures: land figure with shadow, position and depth") {
    const auto figures = make_deer();
    // Atlas: sheet 0 terrain, sheet 1 "tiere" (200 sprites 10x12), sheet 2 "schatten" (80 sprites 14x6).
    const auto terrain = test::make_sheet({{4, 2}});
    const auto tiere = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(200, {10, 12}));
    const auto schatten = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(80, {14, 6}));
    const auto palette = test::make_palette();
    const assets::SpriteSheet* sheets[] = {&terrain, &tiere, &schatten};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 512);
    REQUIRE(atlas.ok());

    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Tiere)] = atlas->sheet_base(1);
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Schatten)] = atlas->sheet_base(2);

    sim::World world;
    // Made on land: height 0.56 (callers).
    const render::FigureInstance deer{10.5, 20.5, 89, 1, 2, 3, render::kLandHeight, 0, sim::kNoPlayer, std::nullopt};
    render::FigureSprites f;
    REQUIRE(render::land_figure_sprites(deer, world, render::WorldView{render::Zoom::Large, 0}, layers, f));
    REQUIRE(f.count == 2);
    // Point: x = (10.5 - 20.5 + 350) * 32 = 10880, y = 31 * 16 = 496; the original keeps the figure
    // at Y = y + Posoffs[1] = 505. Body and shadow are lifted by trunc(0.56 * 32) = 17
    // and one pixel for the tile rows of Open1602 starting one lower.
    const render::DrawCommand& shadow = f.parts[0];
    CHECK(shadow.mode == render::DrawCommand::Mode::Shadow);
    CHECK(shadow.tint.a == render::kShadowAlpha);
    CHECK(shadow.sprite == atlas->sheet_base(2) + 8 + 3 * 8);  // no ANIM 1: direction only
    CHECK(shadow.x == 10880 - 7);
    CHECK(shadow.y == 496 - 18 - 3 + 6);  // centred on the lifted ground point
    const render::DrawCommand& body = f.parts[1];
    CHECK(body.mode == render::DrawCommand::Mode::Normal);
    CHECK(body.sprite == atlas->sheet_base(1) + 4 + 112 + 2 + 3 * 6);
    CHECK(body.x == 10880 - 5);
    CHECK(body.y == 505 + 3 - 18);       // bottom at Y + Posoffs[0], lifted
    CHECK(f.row == (496 + 9 - 1) / 16);  // the height does not change the row
    CHECK(f.depth == 505);

    // Half of it in a crop: the bottom 6 of 12 rows are left out, the shadow goes up by as much;
    // all of it hidden: nothing drawn.
    render::FigureInstance in_crop = deer;
    in_crop.crop = 128;
    render::FigureSprites c;
    REQUIRE(render::land_figure_sprites(in_crop, world, render::WorldView{render::Zoom::Large, 0}, layers, c));
    CHECK(c.parts[1].cut_bottom == 6);
    CHECK(c.parts[1].y == body.y);
    CHECK(c.parts[0].y == shadow.y - 6);
    in_crop.crop = render::kCropHidden;
    CHECK(!render::land_figure_sprites(in_crop, world, render::WorldView{render::Zoom::Large, 0}, layers, c));

    // On an island the local player has not discovered, only figures without an island (deer)
    // show; kNoPlayer sees everything.
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, sim::IslandMap(4, 4), 9, 19}).ok());
    render::FigureInstance worker = deer;
    worker.island = 0;
    CHECK(render::land_figure_sprites(worker, world, render::WorldView{render::Zoom::Large, 0}, layers, c));
    layers.local_player = 0;
    CHECK(!render::land_figure_sprites(worker, world, render::WorldView{render::Zoom::Large, 0}, layers, c));
    CHECK(render::land_figure_sprites(deer, world, render::WorldView{render::Zoom::Large, 0}, layers, c));
    world.island(0).discover(1);
    CHECK(!render::land_figure_sprites(worker, world, render::WorldView{render::Zoom::Large, 0}, layers, c));
    world.island(0).discover(0);
    CHECK(render::land_figure_sprites(worker, world, render::WorldView{render::Zoom::Large, 0}, layers, c));

    // Without its sheet (e.g. SGFX without derived graphics) the figure is not drawn.
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Tiere)].reset();
    CHECK(!render::land_figure_sprites(deer, world, render::WorldView{render::Zoom::Large, 0}, layers, f));
}

TEST_CASE("figures: a display figure's shadow follows its picture and height (kind 0x1E)") {
    const auto figures = make_deer();
    const auto terrain = test::make_sheet({{4, 2}});
    const auto tiere = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(200, {10, 12}));
    const auto schatten = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(80, {14, 6}));
    const auto palette = test::make_palette();
    const assets::SpriteSheet* sheets[] = {&terrain, &tiere, &schatten};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 512);
    REQUIRE(atlas.ok());
    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Tiere)] = atlas->sheet_base(1);
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Schatten)] = atlas->sheet_base(2);

    // Like a juggler: standing at height 0.6 on the market, the shadow at the same height.
    render::EffectFigure e;
    e.x = 10.5;
    e.y = 20.5;
    e.z = 0.6;
    e.ground = 0.6;
    e.figure = 89;
    e.anim = 0;
    e.frame = 3;
    e.direction = 2;
    e.sheet = assets::FigureSheet::Tiere;
    sim::World world;
    render::FigureSprites f;
    REQUIRE(render::effect_figure_sprites(e, world, render::WorldView{render::Zoom::Large, 0}, layers, 0, f));
    REQUIRE(f.count == 2);
    // The shadow figure's ANIM 0 with the body's frame and direction, centred on the
    // point lifted by trunc(0.6 * 32) = 19 and the one row of Open1602.
    const render::DrawCommand& shadow = f.parts[0];
    CHECK(shadow.mode == render::DrawCommand::Mode::Shadow);
    CHECK(shadow.sprite == atlas->sheet_base(2) + 8 + 3 + 2 * 8);
    CHECK(shadow.x == 10880 - 7);
    CHECK(shadow.y == 496 - 20 - 3 + 6);
    const render::DrawCommand& body = f.parts[1];
    CHECK(body.sprite == atlas->sheet_base(1) + 4 + 3 + 2 * 8);
    CHECK(body.y == 505 + 3 - 20);  // bottom at Y + Posoffs[0], lifted by the same 20 pixels
}

TEST_CASE("figures: half-size sheets for zoom levels without graphics") {
    // 4 x 2 image: left 2x2 block fully opaque red 100/200, right block with one opaque pixel.
    std::vector<std::uint8_t> src(4 * 2 * 4, 0);
    const auto set = [&](std::uint32_t x, std::uint32_t y, std::uint8_t r) {
        const std::size_t p = (y * 4 + x) * 4;
        src[p] = r;
        src[p + 3] = 255;
    };
    set(0, 0, 100);
    set(1, 0, 200);
    set(0, 1, 100);
    set(1, 1, 200);
    set(3, 1, 50);
    std::vector<std::uint8_t> dst;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    render::halve_rgba(src, 4, 2, dst, w, h);
    CHECK(w == 2);
    CHECK(h == 1);
    CHECK(dst[0] == 150);  // average of the opaque pixels
    CHECK(dst[3] == 255);
    CHECK(dst[7] == 0);  // one opaque pixel of four is not enough

    // Odd sizes round up; the half-size source reports the reduced size.
    const auto sheet = test::make_sheet({{5, 3}});
    const auto palette = test::make_palette();
    const render::HalfSizeSource half(sheet, palette);
    std::uint32_t sw = 0;
    std::uint32_t sh = 0;
    half.size(0, sw, sh);
    CHECK(sw == 3);
    CHECK(sh == 2);
    std::vector<std::uint8_t> pixels;
    REQUIRE(half.pixels(0, pixels));
    CHECK(pixels.size() == 3U * 2U * 4U);
}

TEST_CASE("figures: shadows are drawn as translucent black shapes") {
    const auto sheet = test::make_sheet({{2, 1}}, 200);  // colour (200, 0, 0)
    const auto palette = test::make_palette();
    auto atlas = render::SpriteAtlas::build(sheet, palette, 64);
    REQUIRE(atlas.ok());
    render::RgbaImage target;
    target.width = 4;
    target.height = 1;
    target.pixels.assign(4 * 4, 100);
    render::DrawCommand shadow{0, 0, 1};
    shadow.mode = render::DrawCommand::Mode::Shadow;
    shadow.tint = assets::Rgba{0, 0, 0, 128};
    render::DrawCommand body{0, 2, 1};
    render::blit_draw_list({shadow, body}, *atlas, target, 0, 0);
    CHECK(target.pixels[0] == 100 * 127 / 255);  // darkened, not the sprite colour
    CHECK(target.pixels[1] == 100 * 127 / 255);
    CHECK(target.pixels[8] == 200);  // normal sprite
}

TEST_CASE("figures: classic graphics dither shadows and smoke like the original") {
    // Sheet 0 "schatten" with one 4 x 3 sprite, followed by its dithered copies.
    const auto sheet = test::make_sheet({{4, 3}}, 200);
    const auto palette = test::make_palette();
    const render::SheetSource source(sheet, palette);
    const render::DitheredSource dithered(source);
    REQUIRE(dithered.count() == 2);
    std::vector<std::uint8_t> rgba;
    REQUIRE(dithered.pixels(1, rgba));
    // Block 1 keeps the pixels with x + y odd: a checkerboard, the colours untouched.
    CHECK(rgba[3] == 0);
    CHECK(rgba[4 + 3] == 255);
    CHECK(rgba[4] == 200);
    CHECK(rgba[(4 + 0) * 4 + 3] == 255);  // (0, 1)
    CHECK(rgba[(4 + 1) * 4 + 3] == 0);    // (1, 1)

    const render::SpriteSource* sources[] = {&source, &dithered};
    auto atlas = render::SpriteAtlas::build(std::span<const render::SpriteSource* const>(sources), 64);
    REQUIRE(atlas.ok());
    render::Dither dither;
    dither.sheets.push_back(render::DitheredSheet{atlas->sheet_base(0), 1, atlas->sheet_base(1)});

    // A shadow whose top left corner is a hatched pixel takes block 1, in full black: the
    // original's even pixels are odd ones here, Open1602's rows lying one pixel lower.
    render::DrawCommand shadow{atlas->sheet_base(0), 10, 13};  // top left (10, 10)
    shadow.mode = render::DrawCommand::Mode::Shadow;
    shadow.tint = assets::Rgba{0, 0, 0, render::kShadowAlpha};
    dither.apply(shadow, *atlas);
    CHECK(shadow.sprite == atlas->sheet_base(1) + 1);
    CHECK(shadow.mode == render::DrawCommand::Mode::Shadow);
    CHECK(shadow.tint.a == 255);
    // One pixel further: the other block, so the checkerboard stays on the same world pixels.
    render::DrawCommand smoke{atlas->sheet_base(0), 11, 13};
    smoke.mode = render::DrawCommand::Mode::Translucent;
    smoke.tint = assets::Rgba{255, 255, 255, 128};
    dither.apply(smoke, *atlas);
    CHECK(smoke.sprite == atlas->sheet_base(1));
    CHECK(smoke.mode == render::DrawCommand::Mode::Normal);
    // Bodies and sprites of other sheets stay as they are.
    render::DrawCommand body{atlas->sheet_base(0), 10, 13};
    dither.apply(body, *atlas);
    CHECK(body.sprite == atlas->sheet_base(0));

    // Drawn: the shadow blackens exactly the pixels of its shape that are not hatched.
    render::RgbaImage target;
    target.width = 4;
    target.height = 3;
    target.pixels.assign(4 * 3 * 4, 100);
    render::blit_draw_list({shadow}, *atlas, target, 10, 10);
    for (std::int32_t y = 0; y < 3; ++y) {
        for (std::int32_t x = 0; x < 4; ++x) {
            const std::uint8_t expected = render::hatch_pixel(10 + x, 10 + y) ? 100 : 0;
            CHECK(target.pixels[static_cast<std::size_t>(y * 4 + x) * 4] == expected);
        }
    }
}

TEST_CASE("flags: generic flag tinted in any colour") {
    using render::tint_shade;
    const assets::Rgba teal{0, 160, 160, 255};
    CHECK(tint_shade(128, teal) == (assets::Rgba{0, 160, 160, 255}));  // neutral grey = the colour
    CHECK(tint_shade(64, teal) == (assets::Rgba{0, 80, 80, 255}));     // darker
    CHECK(tint_shade(0, teal) == (assets::Rgba{0, 0, 0, 255}));
    CHECK(tint_shade(255, teal) == (assets::Rgba{255, 255, 255, 255}));  // lighter, up to white

    // Only grey pixels are cloth; coloured pixels (pole, emblem) and transparent ones stay.
    std::vector<std::uint8_t> rgba = {128, 128, 128, 255,  // cloth
                                      90,  60,  30,  255,  // brown pole
                                      128, 128, 128, 0};   // transparent
    render::tint_rgba(rgba, teal);
    CHECK(rgba[0] == 0);
    CHECK(rgba[1] == 160);
    CHECK(rgba[4] == 90);
    CHECK(rgba[8] == 128);

    // Original flags become templates: every opaque pixel grey.
    std::vector<std::uint8_t> white = {255, 255, 255, 255, 100, 100, 100, 255, 0, 0, 0, 0};
    render::original_flag_to_template(white);
    CHECK(white[0] == 127);  // 0.48 * 255 + 5
    CHECK(white[1] == 127);
    CHECK(white[4] == 53);
    CHECK(white[11] == 0);
}

TEST_CASE("flags: one tinted block per banner in the atlas and in the draw list") {
    // Ship sheet: 4 flag images 6x4 (palette index 255 = white-ish red channel 255 in the test palette).
    const auto ships = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(4, {6, 4}), 255);
    const auto palette = test::make_palette();
    const render::SheetSource ship_source(ships, palette);
    const render::TintedFlagSource flags(ship_source, 0, 4, {{255, 0, 0, 255}, {0, 0, 255, 255}}, true);
    CHECK(flags.count() == 8);
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    flags.size(5, w, h);
    CHECK(w == 6);
    CHECK(h == 4);
    std::vector<std::uint8_t> px;
    REQUIRE(flags.pixels(5, px));  // second banner (blue), frame 1
    CHECK(px[0] == 0);
    CHECK(px[2] > 0);  // blue channel set
    CHECK(px[3] == 255);

    // Renderer: a ship of a banner with a tinted flag uses the banner's block.
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Posoffs: -19, 34\n"
        "  Fahnoffs: 0, 0, 2.0\nEndObj\n",
        types);
    auto figures = assets::FigureTable::from_cod(doc);
    REQUIRE(figures.ok());
    const auto hulls = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(8, {20, 10}));
    const render::SheetSource hull_source(hulls, palette);
    const render::SpriteSource* sources[] = {&hull_source, &flags};
    auto atlas = render::SpriteAtlas::build(std::span<const render::SpriteSource* const>(sources), 256);
    REQUIRE(atlas.ok());

    render::BannerTable banners;
    banners.add_colour("test:red", {255, 0, 0, 255});
    banners.add_colour("test:blue", {0, 0, 255, 255});
    banners.set_flag_template(render::FlagTemplate{0, 4, true});  // the four images of the sheet
    const std::vector<std::optional<std::uint32_t>> banner_flags = {atlas->sheet_base(1), atlas->sheet_base(1) + 4};
    render::WorldLayers layers;
    layers.figures = &*figures;
    layers.atlas = &*atlas;
    layers.ship_base = atlas->sheet_base(0);
    layers.banners = &banners;
    layers.banner_flags = &banner_flags;
    sim::World world;
    world.set_players(
        {sim::Player{sim::PlayerRole::Human, 0, 0, ""}, sim::Player{sim::PlayerRole::Computer, 1, 0, ""}});
    render::ShipSprites s;
    REQUIRE(render::ship_sprites(sim::make_ship("A", 10, 20, 21, 1, 0), world, render::WorldView{}, layers, 0, s));
    REQUIRE(s.has_flag);
    CHECK(s.flag.sprite == atlas->sheet_base(1) + 4);
    REQUIRE(render::ship_sprites(sim::make_ship("A", 10, 20, 21, 1, 0), world, render::WorldView{}, layers, 200, s));
    CHECK(s.flag.sprite == atlas->sheet_base(1) + 4 + 2);  // frame 200 / 90 = 2
    // The template's four images per block, not the banner's flag_frames (8): frame 450 / 90 = 5
    // is image 1 of the banner's block, not the first of the next (FB-33).
    REQUIRE(render::ship_sprites(sim::make_ship("A", 10, 20, 21, 1, 0), world, render::WorldView{}, layers, 450, s));
    CHECK(s.flag.sprite == atlas->sheet_base(1) + 4 + 1);
    // Without the atlas block (e.g. graphics loaded without banners) there is no flag.
    layers.banner_flags = nullptr;
    REQUIRE(render::ship_sprites(sim::make_ship("A", 10, 20, 21, 1, 0), world, render::WorldView{}, layers, 0, s));
    CHECK(!s.has_flag);
}

TEST_CASE("figures: workers and animals vanish into crops and trees") {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // ground
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"   // road
        "  @Nummer: 2\n  Id: 20501\n  Kind: 10\n  Gfx: 2\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"  // young tree
        "  @Nummer: 3\n  Id: 20502\n  Kind: 10\n  Gfx: 7\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"  // ripe tree
        "  @Nummer: 4\n  Id: 20503\n  Kind: 10\n  Gfx: 8\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"  // young grain
        "  @Nummer: 5\n  Id: 20504\n  Kind: 10\n  Gfx: 13\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 45\n  EndObj\n"  // ripe grain
        "  @Nummer: 6\n  Id: 20601\n  Kind: 14\n  Gfx: 14\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 11\n  EndObj\n"  // quarry
        "  @Nummer: 7\n  Id: 20602\n  Kind: 14\n  Gfx: 15\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n  EndObj\n"  // farm
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    const assets::BuildingTable& b = *table;
    sim::World world;
    sim::IslandMap map(9, 1);
    const std::uint32_t ids[] = {20101, 20401, 20501, 20501, 20502, 20503, 20504, 20601, 20602};
    for (std::int32_t x = 0; x < 9; ++x) {
        REQUIRE(map.place(*b.by_id(ids[x]), x, 0, 0, 0, 0).ok());
    }
    map.set_anim_phase(2, 0, 2);  // young trees at growth stages 2 and 4
    map.set_anim_phase(3, 0, 4);
    map.set_anim_phase(5, 0, 3);  // young grain at stage 3
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
    const auto at = [&](int x) { return render::crop_cut(world, b, 10 + x + 0.5, 10.5); };
    CHECK(at(0) == 0);                                 // ground
    CHECK(at(1) == 0);                                 // road
    CHECK(at(2) == 40);                                // tree, stage 2: the legs
    CHECK(at(3) == render::kCropHidden);               // tree, stage 4: all of it
    CHECK(at(4) == render::kCropHidden);               // ripe tree: the woodcutter at work vanishes
    CHECK(at(5) == 45);                                // grain, stage 3
    CHECK(at(6) == 55);                                // ripe grain
    CHECK(at(7) == 0);                                 // quarry: the stone cutter stays visible
    CHECK(at(8) == render::kCropHidden);               // inside any other building
    CHECK(render::crop_cut(world, b, 5.5, 5.5) == 0);  // open sea
}

TEST_CASE("figures: soldiers keep their animations from frame to frame and are found by id") {
    const auto figures = make_deer();
    const assets::FigureType& deer = *figures.get(89);
    const assets::BuildingTable buildings;
    sim::World world;
    const auto add = [&world](std::uint32_t id, std::int32_t x, std::uint8_t anim) {
        sim::Soldier s;
        s.id = id;
        s.pos_x = x * sim::kPosUnit;
        s.pos_y = 3 * sim::kPosUnit;
        s.figure = 89;
        s.anim = anim;
        world.add_soldier(s);
    };
    add(9, 1, 0);
    add(5, 2, 0);
    add(7, 4, 0);
    world.soldiers().back().motion.phase = sim::SoldierMotion::Phase::Healing;  // in a doctor's house

    render::SoldierFigures soldiers;
    soldiers.reset(world, buildings, figures);
    REQUIRE(soldiers.instances().size() == 2);
    REQUIRE(soldiers.find(5) != nullptr);
    CHECK(soldiers.find(5)->x == 2.0);
    CHECK(soldiers.find(9)->x == 1.0);
    CHECK(soldiers.find(7) == nullptr);
    CHECK(soldiers.find(6) == nullptr);
    // New units start somewhere in their animation; it runs on across a sync.
    CHECK(soldiers.find(5)->frame == std::optional<std::uint8_t>(render::start_animation(deer, 0, 5).frame));
    soldiers.advance(135 * 3, figures);
    const auto frame5 = soldiers.find(5)->frame;
    world.soldiers()[1].pos_x = 6 * sim::kPosUnit;
    soldiers.sync(world, buildings, figures);
    CHECK(soldiers.find(5)->x == 6.0);
    CHECK(soldiers.find(5)->frame == frame5);
    // A unit that changes its animation starts it from the first frame.
    world.soldiers()[0].anim = 3;
    soldiers.sync(world, buildings, figures);
    CHECK(soldiers.find(9)->anim == 3);
    CHECK(soldiers.find(9)->frame == std::optional<std::uint8_t>(0));
    CHECK(soldiers.find(5)->frame == frame5);
    // Gone and new units.
    world.soldiers().erase(world.soldiers().begin() + 1);
    add(12, 8, 0);
    soldiers.sync(world, buildings, figures);
    CHECK(soldiers.find(5) == nullptr);
    REQUIRE(soldiers.find(12) != nullptr);
    CHECK(soldiers.find(12)->x == 8.0);
    CHECK(soldiers.find(12)->frame == std::optional<std::uint8_t>(render::start_animation(deer, 0, 12).frame));
}

TEST_CASE("figures: soldiers out of view keep their place but their animations wait (FB-2)") {
    const auto figures = make_deer();
    const assets::BuildingTable buildings;
    sim::World world;
    for (const auto& [id, x] : {std::pair{3U, 2}, std::pair{4U, 40}}) {
        sim::Soldier s;
        s.id = id;
        s.pos_x = x * sim::kPosUnit;
        s.pos_y = 3 * sim::kPosUnit;
        s.figure = 89;
        world.add_soldier(s);
    }
    render::SoldierFigures soldiers;
    soldiers.reset(world, buildings, figures);
    const render::TileBox view{0.0, 0.0, 10.0, 10.0};
    soldiers.sync(world, buildings, figures, 1.0, view);
    const auto near = soldiers.find(3)->frame;
    const auto far = soldiers.find(4)->frame;
    soldiers.advance(135 * 3, figures);
    CHECK(soldiers.find(3)->frame != near);
    CHECK(soldiers.find(4)->frame == far);  // waits off screen
    REQUIRE(soldiers.find(4) != nullptr);   // still found, at its place
    CHECK(soldiers.find(4)->x == 40.0);
    // In view again, it runs on from where it waited.
    soldiers.sync(world, buildings, figures, 1.0);
    CHECK(soldiers.find(4)->frame == far);
    soldiers.advance(135 * 3, figures);
    CHECK(soldiers.find(4)->frame != far);
}

TEST_CASE("figures: soldiers are drawn on their way between two ticks") {
    const auto figures = make_deer();
    const assets::BuildingTable buildings;
    sim::World world;
    sim::Soldier s;
    s.id = 4;
    s.pos_x = 10 * sim::kPosUnit;
    s.pos_y = 3 * sim::kPosUnit;
    s.figure = 89;
    world.add_soldier(s);
    render::SoldierFigures soldiers;
    soldiers.reset(world, buildings, figures);
    // Before the tick the trail keeps where it was; the tick moves it half a tile.
    soldiers.capture(world);
    world.soldiers()[0].pos_x += sim::kPosUnit / 2;
    soldiers.sync(world, buildings, figures, 0.0);
    CHECK(soldiers.find(4)->x == 10.0);
    soldiers.sync(world, buildings, figures, 0.5);
    CHECK(soldiers.find(4)->x == 10.25);
    CHECK(soldiers.find(4)->y == 3.0);
    soldiers.sync(world, buildings, figures, 1.0);
    CHECK(soldiers.find(4)->x == 10.5);
    // A unit set down far away, and one that was not there before the tick, are drawn where they are.
    world.soldiers()[0].pos_x = 30 * sim::kPosUnit;
    s.id = 6;
    s.pos_x = 12 * sim::kPosUnit;
    world.add_soldier(s);
    soldiers.sync(world, buildings, figures, 0.5);
    CHECK(soldiers.find(4)->x == 30.0);
    CHECK(soldiers.find(6)->x == 12.0);
    // A reset forgets the trail.
    soldiers.capture(world);
    world.soldiers()[0].pos_x += sim::kPosUnit;
    soldiers.reset(world, buildings, figures);
    soldiers.sync(world, buildings, figures, 0.5);
    CHECK(soldiers.find(4)->x == 31.0);
}

TEST_CASE("figures: the draw list keeps every figure that reaches into the viewport and skips far ones (FB-3)") {
    const auto figures = make_deer();
    const auto terrain = test::make_sheet({{4, 2}});
    const auto tiere = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(200, {10, 12}));
    const auto schatten = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(80, {14, 6}));
    const auto palette = test::make_palette();
    const assets::SpriteSheet* sheets[] = {&terrain, &tiere, &schatten};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 512);
    REQUIRE(atlas.ok());
    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Tiere)] = atlas->sheet_base(1);
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Schatten)] = atlas->sheet_base(2);
    const assets::BuildingTable buildings;
    const render::AnimationClock clock(buildings);
    const sim::World world;
    std::vector<render::FigureInstance> deer;
    for (std::uint32_t y = 0; y < world.height(); y += 7) {
        for (std::uint32_t x = (y / 7) % 5; x < world.width(); x += 5) {
            deer.push_back(render::FigureInstance{x + 0.25, y + 0.75, 89, 1, 2, static_cast<std::uint8_t>(x % 8),
                                                  render::kLandHeight, 0, sim::kNoPlayer, std::nullopt});
        }
    }
    layers.land_figures = {&deer};
    const auto overlaps = [](const render::DrawCommand& c, const render::SpriteAtlas& a, const render::Viewport& v) {
        const render::AtlasRect& r = a.rect(c.sprite);
        return c.x < v.left + v.width && c.x + r.width > v.left && c.y < v.top + v.height && c.y + r.height > v.top;
    };
    for (std::uint8_t rotation = 0; rotation < 4; ++rotation) {
        render::WorldView view{render::Zoom::Large, rotation};
        std::int32_t w = 0;
        std::int32_t h = 0;
        render::world_pixel_size(view, w, h);
        for (const auto& [fx, fy] :
             {std::pair{0.5, 0.5}, std::pair{0.45, 0.2}, std::pair{0.25, 0.45}, std::pair{0.65, 0.6}}) {
            const render::Viewport v{static_cast<std::int32_t>(w * fx), static_cast<std::int32_t>(h * fy), 1280, 720};
            std::vector<render::DrawCommand> commands;
            render::build_world_draw_list(world, buildings, clock, view, v, layers, commands);
            std::size_t expected = 0;
            for (const render::FigureInstance& d : deer) {
                render::FigureSprites f;
                REQUIRE(render::land_figure_sprites(d, world, view, layers, f));
                bool seen = false;
                for (std::uint8_t i = 0; i < f.count; ++i) {
                    seen = seen || overlaps(f.parts[i], *atlas, v);
                }
                if (!seen) {
                    continue;
                }
                ++expected;
                const render::DrawCommand& body = f.parts[f.count - 1];
                const bool drawn = std::any_of(commands.begin(), commands.end(), [&](const render::DrawCommand& c) {
                    return c.sprite == body.sprite && c.x == body.x && c.y == body.y;
                });
                CHECK(drawn);
            }
            CHECK(expected > 0);
            // Nothing from the far ends of the row band: every figure command lies near the viewport.
            const std::int32_t tw = render::tile_width(view.zoom);
            for (const render::DrawCommand& c : commands) {
                if (c.sprite >= atlas->sheet_base(1)) {
                    CHECK(c.x > v.left - layers.extent.width - 3 * tw);
                    CHECK(c.x < v.left + v.width + layers.extent.width + 3 * tw);
                }
            }
        }
    }
}

TEST_CASE("figures: a walking figure's legs move at any frame rate (cart sliding at the slowest speed)") {
    // Off the road (Wegspeed 170) at 240 frames per second and game speed 1: 4 ms a frame. Each
    // frame alone is less than one millisecond of animation; together they make the full time.
    std::uint32_t rest = 0;
    std::uint64_t total = 0;
    for (int frame = 0; frame < 240; ++frame) {
        total += render::walking_pace(rest, 4, 170);
    }
    CHECK(total == 960U * 32U / 170U);
    CHECK(rest == 960U * 32U % 170U);
    // On the road (Wegspeed 32) the time passes unchanged.
    rest = 0;
    CHECK(render::walking_pace(rest, 7, 32) == 7);
    CHECK(rest == 0);
}
