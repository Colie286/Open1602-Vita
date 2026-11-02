// SPDX-License-Identifier: GPL-3.0-or-later
// Soldiers: savegame records, colour variants and tinted units.

#include <cstring>
#include <optional>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/figure_looks.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/render/flag_tint.hpp"
#include "o1602/render/package_sprites.hpp"
#include "o1602/render/sprite_atlas.hpp"
#include "o1602/render/unit_tint.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

TEST_CASE("units: SOLDAT3 records") {
    std::vector<std::uint8_t> rec(0x44, 0);
    rec[0] = 0x5C;  // x 604 half tiles = 302
    rec[1] = 0x02;
    rec[2] = 0x0C;  // y 268 = 134
    rec[3] = 0x01;
    rec[6] = 33;       // SPEER1
    rec[0x18] = 6;     // natives
    rec[0x19] = 1;     // ANIM 1
    rec[0x1B] = 0x0F;  // direction 7 (upper bits ignored)
    std::vector<std::uint8_t> file;
    test::put_chunk(file, "SOLDAT3", rec);
    auto chunks = assets::ChunkFile::from_bytes(file);
    REQUIRE(chunks.ok());
    auto state = assets::load_game_state(*chunks);
    REQUIRE(state.ok());
    REQUIRE(state->soldiers.size() == 1);
    const auto& s = state->soldiers[0];
    CHECK(s.x2 == 604);
    CHECK(s.y2 == 268);
    CHECK(s.figure == 33);
    CHECK(s.owner == 6);
    CHECK(s.anim == 1);
    CHECK(s.direction == 7);
}

TEST_CASE("units: colour variants of the original unit families") {
    const auto v = assets::FigureTable::unit_variant(7);  // KAVALERIE3 = yellow rider
    REQUIRE(v.has_value());
    CHECK(v->family == 5);
    CHECK(v->index == 2);
    CHECK(v->count == 4);
    CHECK(assets::FigureTable::unit_variant(1)->index == 0);
    CHECK(assets::FigureTable::unit_variant(16)->family == 13);
    CHECK(!assets::FigureTable::unit_variant(33).has_value());  // SPEER1: one image set for all
    CHECK(!assets::FigureTable::unit_variant(17).has_value());

    const auto banners = render::BannerTable::original();
    CHECK(banners.get(0)->unit_variant == std::optional<std::uint8_t>(0));
    CHECK(banners.get(3)->unit_variant == std::optional<std::uint8_t>(3));
    CHECK(!banners.get(4)->unit_variant.has_value());  // pirates
}

TEST_CASE("units: tint template from four drawn variants") {
    // 6 x 1 pixels: sash, plume (white variant white, red orange, blue grey helmet, yellow black),
    // boots (black only for yellow), grey steel (same everywhere), the plume colours again but away
    // from any cloth, transparent.
    const std::uint8_t red[] = {200, 40,  10,  255, 233, 113, 2, 255, 137, 86, 13, 255,
                                128, 128, 128, 255, 233, 113, 2, 255, 0,   0,  0,  0};
    const std::uint8_t blue[] = {0,   0,   200, 255, 128, 128, 128, 255, 137, 86, 13, 255,
                                 128, 128, 128, 255, 128, 128, 128, 255, 0,   0,  0,  0};
    const std::uint8_t yellow[] = {230, 200, 40,  255, 0, 4, 4, 255, 0, 4, 4, 255,
                                   128, 128, 128, 255, 0, 4, 4, 255, 0, 0, 0, 0};
    const std::uint8_t white[] = {230, 230, 230, 255, 250, 250, 250, 255, 137, 86, 13, 255,
                                  128, 128, 128, 255, 250, 250, 250, 255, 0,   0,  0,  0};
    const std::uint8_t* images[] = {red, blue, yellow, white};
    std::vector<render::RgbaView> views;
    for (const std::uint8_t* image : images) {
        views.push_back(render::RgbaView{image, 6, 1});
    }
    const std::vector<assets::Rgba> colours(render::BannerTable::kDefaultColours.begin(),
                                            render::BannerTable::kDefaultColours.begin() + 4);
    std::vector<std::uint8_t> out;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    render::unit_template(views, colours, render::UnitTemplateOptions{}, out, w, h);
    REQUIRE(w == 6);
    REQUIRE(h == 1);
    // Sash: three variants show their colour -> cloth, grey 0.48 * 230 + 5 = 115.
    CHECK(out[0] == 115);
    CHECK(out[1] == 115);
    CHECK(out[2] == 115);
    // Plume: only red shows its colour, but the white variant is white there and the pixel touches
    // the sash -> cloth (0.48 * 250 + 5 = 125).
    CHECK(out[4] == 125);
    CHECK(out[6] == 125);
    // Boots: as in the white variant.
    CHECK(out[8] == 137);
    CHECK(out[10] == 13);
    // Steel: pure grey, moved by one in blue.
    CHECK(out[12] == 128);
    CHECK(out[14] == 129);
    // The same plume colours without contact to other cloth: not cloth.
    CHECK(out[16] == 250);
    CHECK(out[18] == 251);
    CHECK(out[23] == 0);

    // Tinted green, the sash takes the colour, the rest stays.
    render::tint_rgba(out, assets::Rgba{30, 170, 40, 255});
    CHECK(out[1] > out[0]);
    CHECK(out[8] == 137);
}

TEST_CASE("units: riders keep one of the drawn horses") {
    // 3 x 3: top row the rider's sash (all variants show their colour), below the horse: brown for
    // red, grey for blue, dark brown for yellow, white for white.
    const auto image = [](const std::uint8_t rider[3], const std::uint8_t horse[3]) {
        std::vector<std::uint8_t> v;
        for (int p = 0; p < 9; ++p) {
            const std::uint8_t* c = p < 3 ? rider : horse;
            v.insert(v.end(), {c[0], c[1], c[2], 255});
        }
        return v;
    };
    const std::uint8_t sash[4][3] = {{200, 40, 10}, {0, 0, 200}, {230, 200, 40}, {230, 230, 230}};
    const std::uint8_t horse[4][3] = {{137, 86, 13}, {120, 120, 124}, {89, 42, 20}, {220, 220, 220}};
    std::vector<std::vector<std::uint8_t>> images;
    std::vector<render::RgbaView> views;
    for (int v = 0; v < 4; ++v) {
        images.push_back(image(sash[v], horse[v]));
    }
    for (const auto& i : images) {
        views.push_back(render::RgbaView{i.data(), 3, 3});
    }
    const std::vector<assets::Rgba> colours(render::BannerTable::kDefaultColours.begin(),
                                            render::BannerTable::kDefaultColours.begin() + 4);
    render::UnitTemplateOptions options;
    options.mounted = true;
    for (std::size_t body = 0; body < 4; ++body) {
        options.body = body;
        std::vector<std::uint8_t> out;
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        render::unit_template(views, colours, options, out, w, h);
        REQUIRE(out.size() == 36);
        CHECK(out[0] == 115);  // the rider's sash is cloth for every horse
        CHECK(out[1] == 115);
        CHECK(out[16] == horse[body][0]);  // the horse comes from the chosen variant
        CHECK(out[17] == horse[body][1]);
    }

    // Banners get a horse from their id, the same every time.
    render::BannerTable banners;
    const auto a = banners.add_colour("mod:green", {30, 170, 40, 255});
    const auto b = banners.add_colour("mod:green", {30, 170, 40, 255});
    CHECK(banners.get(a)->unit_mount == banners.get(b)->unit_mount);
    CHECK(banners.get(a)->unit_mount < 4);
}

TEST_CASE("units: each template is made once and gives the same images (FB-25)") {
    // One rider family, 4 variants of one 3 x 3 image: sash on top, horse below.
    class Sheet final : public render::SpriteSource {
    public:
        explicit Sheet(std::vector<std::vector<std::uint8_t>> images) : m_images(std::move(images)) {}
        [[nodiscard]] std::uint32_t count() const override { return static_cast<std::uint32_t>(m_images.size()); }
        void size(std::uint32_t, std::uint32_t& width, std::uint32_t& height) const override { width = height = 3; }
        bool pixels(std::uint32_t i, std::vector<std::uint8_t>& rgba) const override {
            ++reads;
            rgba = m_images[i];
            return true;
        }
        mutable int reads = 0;

    private:
        std::vector<std::vector<std::uint8_t>> m_images;
    };
    const std::uint8_t sash[4][3] = {{200, 40, 10}, {0, 0, 200}, {230, 200, 40}, {230, 230, 230}};
    const std::uint8_t horse[4][3] = {{137, 86, 13}, {120, 120, 124}, {89, 42, 20}, {220, 220, 220}};
    std::vector<std::vector<std::uint8_t>> images;
    for (int v = 0; v < 4; ++v) {
        std::vector<std::uint8_t> image;
        for (int p = 0; p < 9; ++p) {
            const std::uint8_t* c = p < 3 ? sash[v] : horse[v];
            image.insert(image.end(), {c[0], c[1], c[2], 255});
        }
        images.push_back(std::move(image));
    }
    const Sheet sheet(images);
    render::UnitImageGroup group;
    group.first = 0;
    group.stride = 1;
    group.variants = 4;
    group.base = 3;
    group.mounted = true;
    const std::vector<assets::Rgba> variants(render::BannerTable::kDefaultColours.begin(),
                                             render::BannerTable::kDefaultColours.begin() + 4);
    const render::UnitTintColour green{{30, 170, 40, 255}, 0};
    const render::UnitTintColour purple{{120, 40, 160, 255}, 1};
    const render::UnitTintColour teal{{20, 150, 150, 255}, 0};
    const render::TintedUnitSource source(sheet, {group}, variants, {green, purple, teal, green});
    std::vector<std::vector<std::uint8_t>> out(4);
    for (std::uint32_t i = 0; i < 4; ++i) {
        REQUIRE(source.pixels(i, out[i]));
    }
    CHECK(sheet.reads == 8);  // two templates (brown and grey horse), not four
    CHECK(out[0] == out[3]);
    CHECK(out[0] != out[2]);          // same horse, other colour
    CHECK(out[0][16] != out[1][16]);  // other horse
    // The same as made alone.
    for (std::uint32_t i = 0; i < 3; ++i) {
        const render::TintedUnitSource alone(sheet, {group}, variants, {i == 0 ? green : i == 1 ? purple : teal});
        std::vector<std::uint8_t> single;
        REQUIRE(alone.pixels(0, single));
        CHECK(single == out[i]);
    }
}

TEST_CASE("units: soldiers in the colour of their owner's banner") {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 1\n  Gfx: 0\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 2\n  Gfx: 10\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 3\n  Gfx: 20\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 4\n  Gfx: 30\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "EndObj\n",
        types);
    auto figures = assets::FigureTable::from_cod(doc);
    REQUIRE(figures.ok());

    // Atlas: terrain, soldier sheet (40 images 4x6) and a tinted block of 10 images for banner 7.
    const auto terrain = test::make_sheet({{4, 2}});
    const auto soldat = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(40, {4, 6}));
    const auto tinted = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(10, {4, 6}));
    const auto palette = test::make_palette();
    const assets::SpriteSheet* sheets[] = {&terrain, &soldat, &tinted};
    auto atlas = render::SpriteAtlas::build(sheets, palette, 256);
    REQUIRE(atlas.ok());

    auto banners = render::BannerTable::original();
    const sim::BannerId green = banners.add_colour("test:green", render::BannerTable::kDefaultColours[4]);
    render::UnitTint tint;
    tint.groups.push_back(render::UnitImageGroup{0, 10, 4, 3, 0});
    tint.block_size = 10;
    tint.banner_base.resize(banners.size());
    tint.banner_base[green] = atlas->sheet_base(2);
    CHECK(tint.sprite(green, 25) == std::optional<std::uint32_t>(atlas->sheet_base(2) + 5));  // any variant
    CHECK(!tint.sprite(1, 5).has_value());
    CHECK(!tint.sprite(green, 40).has_value());

    sim::World world;
    world.set_players(
        {sim::Player{sim::PlayerRole::Human, 2, 0, ""}, sim::Player{sim::PlayerRole::Computer, green, 0, ""}});
    render::WorldLayers layers;
    layers.figures = &*figures;
    layers.atlas = &*atlas;
    layers.banners = &banners;
    layers.unit_tint = &tint;
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Soldat)] = atlas->sheet_base(1);

    render::FigureInstance soldier;
    soldier.x = 10.0;
    soldier.y = 20.0;
    soldier.figure = 1;  // family SOLDAT1..4
    soldier.direction = 3;
    soldier.owner = 0;  // yellow banner: drawn variant 2
    soldier.sheet = assets::FigureSheet::Soldat;
    render::FigureSprites f;
    REQUIRE(render::land_figure_sprites(soldier, world, render::WorldView{}, layers, f));
    CHECK(f.parts[f.count - 1].sprite == atlas->sheet_base(1) + 20 + 3);
    soldier.owner = 1;  // green: tinted template
    REQUIRE(render::land_figure_sprites(soldier, world, render::WorldView{}, layers, f));
    CHECK(f.parts[f.count - 1].sprite == atlas->sheet_base(2) + 3);
    soldier.owner = sim::kNoPlayer;  // nobody: the stored figure
    REQUIRE(render::land_figure_sprites(soldier, world, render::WorldView{}, layers, f));
    CHECK(f.parts[f.count - 1].sprite == atlas->sheet_base(1) + 3);
}

TEST_CASE("units: a soldier drawn with a package's image keeps it in every colour") {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 1\n  Gfx: 0\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 2\n  Gfx: 10\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 3\n  Gfx: 20\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 4\n  Gfx: 30\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "EndObj\n",
        types);
    auto original = assets::FigureTable::from_cod(doc);
    REQUIRE(original.ok());
    // The swordsman in all four colours from the package's image at 40000 (figure_images.toml).
    assets::FigureTable figures = *original;
    const std::vector<assets::FigureLook> looks = {
        {1, 40000, true}, {2, 40000, true}, {3, 40000, true}, {4, 40000, true}};
    assets::apply_figure_looks(figures, *original, looks);

    // Atlas: terrain with the package's 8 sprites at 40000, then the soldier sheet.
    const auto terrain = test::make_sheet({{4, 2}});
    const auto package = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(8, {5, 7}));
    const auto soldat = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(40, {4, 6}));
    const auto palette = test::make_palette();
    const render::SheetSource terrain_source(terrain, palette);
    const render::SheetSource package_source(package, palette);
    const render::SheetSource soldat_source(soldat, palette);
    render::PlacedSources terrain_with_package;
    terrain_with_package.place(0, terrain_source);
    terrain_with_package.place(assets::kFirstPackageSprite, package_source);
    const render::SpriteSource* sources[] = {&terrain_with_package, &soldat_source};
    auto atlas = render::SpriteAtlas::build(sources, 256);
    REQUIRE(atlas.ok());

    auto banners = render::BannerTable::original();
    const sim::BannerId green = banners.add_colour("test:green", render::BannerTable::kDefaultColours[4]);
    render::UnitTint tint;  // no tinted templates: a package's unit is never tinted
    sim::World world;
    world.set_players(
        {sim::Player{sim::PlayerRole::Human, 2, 0, ""}, sim::Player{sim::PlayerRole::Computer, green, 0, ""}});
    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.banners = &banners;
    layers.unit_tint = &tint;
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Stadtfld)] = atlas->sheet_base(0);
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Soldat)] = atlas->sheet_base(1);

    render::FigureInstance soldier;
    soldier.x = 10.0;
    soldier.y = 20.0;
    soldier.figure = 1;
    soldier.direction = 3;
    soldier.sheet = assets::FigureSheet::Soldat;  // the soldiers' draw kind: the package's image all the same
    render::FigureSprites f;
    for (const sim::PlayerId owner : {sim::PlayerId{0}, sim::PlayerId{1}, sim::kNoPlayer}) {
        soldier.owner = owner;
        REQUIRE(render::land_figure_sprites(soldier, world, render::WorldView{}, layers, f));
        CHECK(f.parts[f.count - 1].sprite == atlas->sheet_base(0) + assets::kFirstPackageSprite + 3);
    }
}

TEST_CASE("units: a package's tint template takes on the colour of every banner") {
    // One 2 x 1 image: pure grey 128 (cloth) and a grey one step off in blue (fixed).
    class Sheet final : public render::SpriteSource {
    public:
        [[nodiscard]] std::uint32_t count() const override { return 3; }
        void size(std::uint32_t, std::uint32_t& width, std::uint32_t& height) const override {
            width = 2;
            height = 1;
        }
        bool pixels(std::uint32_t, std::vector<std::uint8_t>& rgba) const override {
            rgba = {128, 128, 128, 255, 120, 120, 121, 255};
            return true;
        }
    };
    const Sheet sheet;
    render::UnitImageGroup group;
    group.first = 2;
    group.stride = 1;
    group.variants = 1;
    const auto banners = render::BannerTable::original();
    std::vector<assets::Rgba> colours;
    for (std::size_t b = 0; b < render::BannerTable::kDefaultColours.size(); ++b) {
        colours.push_back(render::BannerTable::kDefaultColours[b]);
    }
    REQUIRE(colours.size() >= 12);  // up to 12 players
    const render::TintedTemplateSource source(sheet, {group}, colours);
    REQUIRE(source.count() == colours.size());
    for (std::uint32_t i = 0; i < source.count(); ++i) {
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        source.size(i, w, h);
        CHECK(w == 2);
        std::vector<std::uint8_t> rgba;
        REQUIRE(source.pixels(i, rgba));
        CHECK(rgba[0] == colours[i].r);  // grey 128: exactly the banner's colour
        CHECK(rgba[1] == colours[i].g);
        CHECK(rgba[2] == colours[i].b);
        CHECK(rgba[4] == 120);  // not pure grey: as drawn
        CHECK(rgba[6] == 121);
    }
    std::vector<std::uint8_t> none;
    CHECK(!source.pixels(source.count(), none));
}

TEST_CASE("units: a soldier drawn with a tint template wears the colour of every banner") {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 1\n  Gfx: 0\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 2\n  Gfx: 10\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 3\n  Gfx: 20\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "  Nummer: 4\n  Gfx: 30\n  Rotate: 1\n  Posoffs: 0, 3\n"
        "EndObj\n",
        types);
    auto original = assets::FigureTable::from_cod(doc);
    REQUIRE(original.ok());
    assets::FigureTable figures = *original;
    const std::vector<assets::FigureLook> looks = {
        {1, 40000, true}, {2, 40000, true}, {3, 40000, true}, {4, 40000, true}};
    assets::apply_figure_looks(figures, *original, looks);

    const auto terrain = test::make_sheet({{4, 2}});
    const auto package = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(8, {5, 7}));
    const auto tinted = test::make_sheet(std::vector<std::pair<std::uint32_t, std::uint32_t>>(8 * 16, {5, 7}));
    const auto palette = test::make_palette();
    const render::SheetSource terrain_source(terrain, palette);
    const render::SheetSource package_source(package, palette);
    const render::SheetSource tinted_source(tinted, palette);  // stands for the tinted blocks
    render::PlacedSources terrain_with_package;
    terrain_with_package.place(0, terrain_source);
    terrain_with_package.place(assets::kFirstPackageSprite, package_source);
    const render::SpriteSource* sources[] = {&terrain_with_package, &tinted_source};
    auto atlas = render::SpriteAtlas::build(sources, 256);
    REQUIRE(atlas.ok());

    auto banners = render::BannerTable::original();
    const sim::BannerId green = banners.add_colour("test:green", render::BannerTable::kDefaultColours[4]);
    render::UnitTint templates;
    templates.groups.push_back(render::UnitImageGroup{assets::kFirstPackageSprite, 8, 1, 0, 0, false, 0});
    templates.block_size = 8;
    for (std::size_t b = 0; b < banners.banners().size(); ++b) {
        templates.banner_base.emplace_back(atlas->sheet_base(1) + static_cast<std::uint32_t>(b) * 8U);
    }
    sim::World world;
    world.set_players(
        {sim::Player{sim::PlayerRole::Human, 2, 0, ""}, sim::Player{sim::PlayerRole::Computer, green, 0, ""}});
    render::WorldLayers layers;
    layers.figures = &figures;
    layers.atlas = &*atlas;
    layers.banners = &banners;
    layers.template_tint = &templates;
    layers.sheet_base[static_cast<std::size_t>(assets::FigureSheet::Stadtfld)] = atlas->sheet_base(0);

    render::FigureInstance soldier;
    soldier.x = 10.0;
    soldier.y = 20.0;
    soldier.figure = 1;
    soldier.direction = 3;
    render::FigureSprites f;
    // Banner 2 has a drawn variant, the green one none: both wear the template in their colour.
    soldier.owner = 0;
    REQUIRE(render::land_figure_sprites(soldier, world, render::WorldView{}, layers, f));
    CHECK(f.parts[f.count - 1].sprite == atlas->sheet_base(1) + 2 * 8 + 3);
    soldier.owner = 1;
    REQUIRE(render::land_figure_sprites(soldier, world, render::WorldView{}, layers, f));
    CHECK(f.parts[f.count - 1].sprite == atlas->sheet_base(1) + green * 8U + 3);
    // Without an owner: as drawn.
    soldier.owner = sim::kNoPlayer;
    REQUIRE(render::land_figure_sprites(soldier, world, render::WorldView{}, layers, f));
    CHECK(f.parts[f.count - 1].sprite == atlas->sheet_base(0) + assets::kFirstPackageSprite + 3);
}
