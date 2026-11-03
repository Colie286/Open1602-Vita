// SPDX-License-Identifier: GPL-3.0-or-later
// The SDL backend of the world's sprites (engine/render/sdl/sdl_sprite_renderer.cpp): an atlas
// uploaded to a software renderer, draw lists drawn and the pixels read back.

#include <array>
#include <vector>

#include "../test_framework.hpp"
#include "../unit/test_support.hpp"
#include "sdl_support.hpp"

#include "o1602/platform/window.hpp"
#include "o1602/render/sdl_sprite_renderer.hpp"
#include "o1602/render/sprite_atlas.hpp"

using namespace o1602;

namespace {

const assets::Rgba kBackground{0, 0, 200, 255};
// test::make_palette: colour i is (i, 0, 0).
const assets::Rgba kRed50{50, 0, 0, 255};
const assets::Rgba kRed90{90, 0, 0, 255};

/// Sprite 0: 4 x 3 pixels of colour 50; sprite 1: 2 x 2 pixels of colour 90.
render::SpriteAtlas test_atlas() {
    const auto first = test::make_sheet({{4, 3}}, 50);
    const auto second = test::make_sheet({{2, 2}}, 90);
    const std::array<const assets::SpriteSheet*, 2> sheets{&first, &second};
    auto atlas = render::SpriteAtlas::build(sheets, test::make_palette(), 64);
    REQUIRE(atlas.ok());
    REQUIRE(atlas->sprite_count() == 2);
    return std::move(atlas).value();
}

render::DrawCommand sprite(std::uint32_t index, std::int32_t x, std::int32_t y) {
    render::DrawCommand c;
    c.sprite = index;
    c.x = x;
    c.y = y;
    return c;
}

/// Whether the rectangle [x0, x1) x [y0, y1) has the colour and its one pixel wide border not.
bool box(const test::Canvas& canvas, int x0, int y0, int x1, int y1, assets::Rgba colour) {
    for (int y = y0 - 1; y <= y1; ++y) {
        for (int x = x0 - 1; x <= x1; ++x) {
            const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if ((canvas.at(x, y) == colour) != inside) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("sdl sprite renderer: nothing is drawn before an atlas is uploaded") {
    test::Canvas canvas(32, 32);
    render::SdlSpriteRenderer renderer(canvas.renderer());
    canvas.clear(kBackground);
    CHECK(renderer.draw({sprite(0, 10, 10)}, 0, 0, 1) == 0);
    canvas.read();
    CHECK(canvas.at(10, 8) == kBackground);
}

TEST_CASE("sdl sprite renderer: a sprite lands with its bottom left corner at the command's position") {
    test::Canvas canvas(32, 32);
    const auto atlas = test_atlas();
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(atlas).ok());
    canvas.clear(kBackground);
    CHECK(renderer.draw({sprite(0, 10, 20), sprite(1, 2, 5)}, 0, 0, 1) == 2);
    canvas.read();
    CHECK(box(canvas, 10, 17, 14, 20, kRed50));
    CHECK(box(canvas, 2, 3, 4, 5, kRed90));
}

TEST_CASE("sdl sprite renderer: the scale multiplies positions and sizes, the origin moves the screen") {
    test::Canvas canvas(48, 48);
    const auto atlas = test_atlas();
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(atlas).ok());
    canvas.clear(kBackground);
    // Screen = (x, y) * 2 - origin: left 2 * 10 - 4 = 16, top (20 - 3) * 2 - 6 = 28, 8 x 6 pixels.
    CHECK(renderer.draw({sprite(0, 10, 20)}, 4, 6, 2) == 1);
    canvas.read();
    CHECK(box(canvas, 16, 28, 24, 34, kRed50));
}

TEST_CASE("sdl sprite renderer: later commands are drawn over earlier ones, missing sprites are skipped") {
    test::Canvas canvas(32, 32);
    const auto atlas = test_atlas();
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(atlas).ok());
    canvas.clear(kBackground);
    CHECK(renderer.draw({sprite(0, 10, 20), sprite(1, 11, 19), sprite(7, 0, 10)}, 0, 0, 1) == 2);
    canvas.read();
    CHECK(canvas.at(10, 17) == kRed50);
    CHECK(canvas.at(11, 17) == kRed90);
    CHECK(canvas.at(12, 18) == kRed90);
    CHECK(canvas.at(13, 19) == kRed50);
    CHECK(canvas.at(0, 9) == kBackground);
}

TEST_CASE("sdl sprite renderer: rows cut off at the bottom are not drawn") {
    test::Canvas canvas(32, 32);
    const auto atlas = test_atlas();
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(atlas).ok());
    canvas.clear(kBackground);
    auto c = sprite(0, 10, 20);
    c.cut_bottom = 1;
    CHECK(renderer.draw({c}, 0, 0, 1) == 1);
    canvas.read();
    CHECK(box(canvas, 10, 17, 14, 19, kRed50));
}

TEST_CASE("sdl sprite renderer: shadows, translucent and dimmed sprites blend with what is below") {
    test::Canvas canvas(32, 32);
    const auto atlas = test_atlas();
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(atlas).ok());
    canvas.clear(kBackground);
    auto shadow = sprite(0, 2, 5);
    shadow.mode = render::DrawCommand::Mode::Shadow;
    shadow.tint = assets::Rgba{0, 0, 0, 128};
    auto smoke = sprite(0, 10, 5);
    smoke.mode = render::DrawCommand::Mode::Translucent;
    smoke.tint = assets::Rgba{0, 0, 0, 128};
    auto dimmed = sprite(0, 20, 5);
    dimmed.mode = render::DrawCommand::Mode::Dimmed;
    dimmed.tint = assets::Rgba{128, 128, 128, 255};
    CHECK(renderer.draw({shadow, smoke, dimmed}, 0, 0, 1) == 3);
    canvas.read();
    CHECK(test::near(canvas.at(2, 3), assets::Rgba{0, 0, 100, 255}));    // half the background
    CHECK(test::near(canvas.at(10, 3), assets::Rgba{25, 0, 100, 255}));  // half sprite, half background
    CHECK(test::near(canvas.at(20, 3), assets::Rgba{25, 0, 0, 255}));    // the sprite at half brightness
}

TEST_CASE("sdl sprite renderer: a checkered sprite keeps its shape and is partly in the colour") {
    test::Canvas canvas(32, 32);
    const auto atlas = test_atlas();
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(atlas).ok());
    canvas.clear(kBackground);
    auto ghost = sprite(0, 4, 8);  // 4 x 3 pixels at (4, 5) - (8, 8)
    ghost.mode = render::DrawCommand::Mode::Checkered;
    ghost.tint = assets::Rgba{255, 255, 0, 255};
    CHECK(renderer.draw({ghost}, 0, 0, 1) == 1);
    canvas.read();
    // Where the renderer has render targets, every second pixel is yellow; otherwise the whole
    // sprite is tinted towards it. Either way nothing around it changes.
    int changed = 0;
    for (int y = 5; y < 8; ++y) {
        for (int x = 4; x < 8; ++x) {
            CHECK(canvas.at(x, y) != kBackground);
            changed += canvas.at(x, y) != kRed50 ? 1 : 0;
        }
    }
    CHECK(changed > 0);
    CHECK(canvas.at(3, 6) == kBackground);
    CHECK(canvas.at(8, 6) == kBackground);
    CHECK(canvas.at(5, 4) == kBackground);
    CHECK(canvas.at(5, 8) == kBackground);
}

TEST_CASE("sdl sprite renderer: an overlay diamond fills the tile in its colour, not its corners") {
    test::Canvas canvas(64, 40);
    const auto atlas = test_atlas();
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(atlas).ok());
    canvas.clear(kBackground);
    render::DrawCommand c;
    c.sprite = render::DrawCommand::kOverlayDiamond;
    c.x = 0;
    c.y = 32;
    c.tint = assets::Rgba{0, 255, 0, 255};
    c.tint_size = 64;
    CHECK(renderer.draw({c}, 0, 0, 1) == 0);  // overlays are not counted as sprites
    canvas.read();
    // The diamond of a 64 x 32 tile with its box from (0, 0) to (64, 32).
    CHECK(canvas.at(32, 16) == (assets::Rgba{0, 255, 0, 255}));
    CHECK(canvas.at(5, 16) == (assets::Rgba{0, 255, 0, 255}));
    CHECK(canvas.at(32, 2) == (assets::Rgba{0, 255, 0, 255}));
    CHECK(canvas.at(2, 2) == kBackground);
    CHECK(canvas.at(61, 29) == kBackground);
    CHECK(canvas.at(32, 34) == kBackground);
}

TEST_CASE("sdl sprite renderer: a hatched diamond lights about every second pixel of the tile") {
    test::Canvas canvas(64, 40);
    const auto atlas = test_atlas();
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(atlas).ok());
    canvas.clear(kBackground);
    render::DrawCommand c;
    c.sprite = render::DrawCommand::kOverlayHatch;
    c.x = 0;
    c.y = 32;  // bottom of the diamond's box
    c.tint = assets::Rgba{255, 255, 0, 255};
    c.tint_size = 64;
    CHECK(renderer.draw({c}, 0, 0, 1) == 0);
    canvas.read();
    // SDL's software renderer samples textured triangles a little off the pixel centres, so the
    // checkerboard's phase is not exact here (the CPU path, blit_draw_list, is tested for it).
    int lit = 0;
    for (int x = 8; x < 56; ++x) {
        const bool on = canvas.at(x, 16) == (assets::Rgba{255, 255, 0, 255});
        CHECK(on || canvas.at(x, 16) == kBackground);
        lit += on ? 1 : 0;
    }
    CHECK(lit >= 20);
    CHECK(lit <= 28);
    CHECK(canvas.at(2, 2) == kBackground);
    CHECK(canvas.at(61, 29) == kBackground);
    CHECK(canvas.at(32, 34) == kBackground);
}

TEST_CASE("sdl sprite renderer: uploading another atlas replaces the previous one") {
    test::Canvas canvas(32, 32);
    const auto first = test_atlas();
    const auto sheet = test::make_sheet({{3, 3}}, 120);
    auto second = render::SpriteAtlas::build(sheet, test::make_palette(), 64);
    REQUIRE(second.ok());
    render::SdlSpriteRenderer renderer(canvas.renderer());
    REQUIRE(renderer.upload(first).ok());
    REQUIRE(renderer.upload(*second).ok());
    canvas.clear(kBackground);
    CHECK(renderer.draw({sprite(0, 4, 10), sprite(1, 20, 10)}, 0, 0, 1) == 1);
    canvas.read();
    CHECK(box(canvas, 4, 7, 7, 10, assets::Rgba{120, 0, 0, 255}));
    CHECK(canvas.at(20, 9) == kBackground);
}

TEST_CASE("sdl sprite renderer: after the window's renderer is made again a new one draws the sprites") {
    test::use_dummy_drivers();
    auto sdl = platform::SdlSystem::init();
    REQUIRE(sdl.ok());
    auto window = platform::Window::create(platform::WindowConfig{.width = 32, .height = 32, .vsync = false});
    REQUIRE(window.ok());
    const auto atlas = test_atlas();
    test::Pixels pixels;
    const auto draws_sprites = [&](render::SdlSpriteRenderer& renderer) {
        SDL_Renderer* r = (*window)->sdl_renderer();
        test::clear(r, kBackground);
        CHECK(renderer.draw({sprite(0, 10, 20), sprite(1, 2, 5)}, 0, 0, 1) == 2);
        pixels.read(r);
        return pixels.at(10, 17) == kRed50 && pixels.at(13, 19) == kRed50 && pixels.at(14, 19) == kBackground &&
               pixels.at(2, 3) == kRed90 && pixels.at(3, 4) == kRed90 && pixels.at(4, 4) == kBackground;
    };
    auto renderer = std::make_unique<render::SdlSpriteRenderer>((*window)->sdl_renderer());
    REQUIRE(renderer->upload(atlas).ok());
    CHECK(draws_sprites(*renderer));

    // As the game does when the device is lost: the textures go first, then the renderer.
    renderer.reset();
    REQUIRE((*window)->recreate_renderer().ok());
    renderer = std::make_unique<render::SdlSpriteRenderer>((*window)->sdl_renderer());
    REQUIRE(renderer->upload(atlas).ok());
    CHECK(draws_sprites(*renderer));
}
