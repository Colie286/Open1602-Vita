// SPDX-License-Identifier: GPL-3.0-or-later
// Corrections of the original's data (ADR 0006; D-001, D-002, D-012, D-014).

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/data_fixes.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/ui/font.hpp"

using namespace o1602;

namespace {

/// The bakery, the pirate Kontor and a Kontor as haeuser.cod has them; `fixed`: as a mod might have
/// corrected them already.
assets::BuildingTable make_buildings(bool fixed) {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const std::string sound = fixed ? "  Bausample: 21\n" : "";
    const std::string flag = fixed ? "    Rauchfignr: 63\n" : "    Rauchfignr: 0\n";
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20503\n  Gfx: 0\n  Size: 2, 2\n" +
            sound +
            "  @Nummer: +1\n  Id: 22101\n  Gfx: 10\n  Size: 2, 3\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Rauchfignr: 60\n  EndObj\n"
            "  @Nummer: +1\n  Id: 22221\n  Gfx: 20\n  Size: 2, 3\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 8\n" +
            flag +
            "  EndObj\n"
            "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// FORM_QUAD as figuren.cod has it: the right-hand row (15, 16, 17) reads (2, -1), (2, 0), (2, -1).
assets::FigureTable make_figures(std::string_view slot17) {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}, {"FORMATION", 2}};
    std::string cod = "Objekt: FIGUR\n  Nummer: 1\n  Gfx: 0\nEndObj\nObjekt: FORMATION\n  Nummer: 2\n";
    const char* slots[] = {"0, 0",   "-1, 0", "1, 0",  "0, -1",  "0, 1",  "-1, -1", "1, -1", "1, 1", "-1, 1",
                           "-2, -1", "-2, 0", "-2, 1", "-1, -2", "0, -2", "1, -2",  "2, -1", "2, 0"};
    for (std::size_t i = 0; i < std::size(slots); ++i) {
        cod += "  Offs: " + std::to_string(i) + ", " + slots[i] + "\n";
    }
    cod += "  Offs: 17, " + std::string(slot17) + "\n  Offs: 18, -1, 2\n  Offs: 19, 0, 2\n  Offs: 20, 1, 2\nEndObj\n";
    auto table = assets::FigureTable::from_cod(assets::cod::parse(cod, types));
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A BSH of glyphs drawn as text: one string per row, '#' a pixel of palette index 1.
assets::SpriteSheet sheet_of(const std::vector<std::vector<std::string>>& glyphs) {
    std::vector<std::vector<std::uint8_t>> records;
    for (const auto& rows : glyphs) {
        const auto w = static_cast<std::uint32_t>(rows.front().size());
        std::vector<std::uint8_t> rle;
        for (std::size_t y = 0; y < rows.size(); ++y) {
            std::size_t x = 0;
            while (x < w) {
                const std::size_t start = rows[y].find('#', x);
                if (start == std::string::npos) {
                    break;
                }
                const std::size_t end = std::min(rows[y].find('.', start), std::size_t{w});
                rle.push_back(static_cast<std::uint8_t>(start - x));
                rle.push_back(static_cast<std::uint8_t>(end - start));
                rle.insert(rle.end(), end - start, 1);
                x = end;
            }
            rle.push_back(y + 1 == rows.size() ? 0xFF : 0xFE);
        }
        std::vector<std::uint8_t> rec;
        test::put_u32(rec, w);
        test::put_u32(rec, static_cast<std::uint32_t>(rows.size()));
        test::put_u32(rec, 1);
        test::put_u32(rec, static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3}));
        rec.insert(rec.end(), rle.begin(), rle.end());
        rec.resize((rec.size() + 3) & ~std::size_t{3}, 0);
        records.push_back(std::move(rec));
    }
    std::vector<std::uint8_t> payload;
    auto offset = static_cast<std::uint32_t>(4 * records.size());
    for (const auto& rec : records) {
        test::put_u32(payload, offset);
        offset += static_cast<std::uint32_t>(rec.size());
    }
    for (const auto& rec : records) {
        payload.insert(payload.end(), rec.begin(), rec.end());
    }
    std::vector<std::uint8_t> file;
    test::put_chunk(file, "BSH", payload);
    auto sheet = assets::SpriteSheet::from_chunk_file(std::move(assets::ChunkFile::from_bytes(file)).value());
    REQUIRE(sheet.ok());
    return std::move(sheet).value();
}

/// The rows of `text` drawn into a canvas as tall as the font's cell and what reaches above it.
std::vector<std::string> drawn(const ui::Font& font, std::string_view text) {
    assets::IndexedImage canvas;
    canvas.width = font.width(text) + 2;
    canvas.height = font.height() + font.above();
    canvas.indices.assign(std::size_t{canvas.width} * canvas.height, 0);
    canvas.opaque.assign(canvas.indices.size(), 0);
    ui::Recolour identity{};
    for (std::size_t i = 0; i < identity.size(); ++i) {
        identity[i] = static_cast<std::uint8_t>(i);
    }
    font.draw(canvas, text, 0, static_cast<std::int32_t>(canvas.height), identity);
    std::vector<std::string> rows(canvas.height, std::string(canvas.width, '.'));
    for (std::uint32_t y = 0; y < canvas.height; ++y) {
        for (std::uint32_t x = 0; x < canvas.width; ++x) {
            if (canvas.opaque[std::size_t{y} * canvas.width + x] != 0) {
                rows[y][x] = '#';
            }
        }
    }
    return rows;
}

}  // namespace

TEST_CASE("data fixes: the bakery's sound and the pirate Kontor's flag (D-001, D-002)") {
    auto buildings = make_buildings(false);
    CHECK(buildings.by_id(20503)->build_sound == 0);                              // WAV_BAECKER is undefined
    CHECK(buildings.by_id(assets::id::kPirateKontor)->attached_figures[0] == 0);  // FAHNEKONTOR too
    CHECK((assets::fixes::fix_buildings(buildings) == std::vector<std::string_view>{"D-001", "D-002"}));
    CHECK(buildings.by_id(20503)->build_sound == assets::fixes::kBakerySound);
    CHECK(buildings.by_id(assets::id::kPirateKontor)->attached_figures[0] == 60);  // FAHNEKONTOR1
    CHECK(buildings.by_id(assets::id::kKontor1)->attached_figures[0] == 60);       // unchanged
    CHECK(assets::fixes::fix_buildings(buildings).empty());                        // once is enough

    // Data that has its own values keeps them.
    auto modded = make_buildings(true);
    CHECK(assets::fixes::fix_buildings(modded).empty());
    CHECK(modded.by_id(20503)->build_sound == 21);
    CHECK(modded.by_id(assets::id::kPirateKontor)->attached_figures[0] == 63);
}

TEST_CASE("data fixes: the block formation has a place for each of its 21 units (D-014)") {
    auto figures = make_figures("2, -1");
    const auto places = [&] {
        std::vector<std::pair<std::int32_t, std::int32_t>> p;
        for (const auto& s : figures.formations()[2]) {
            p.emplace_back(s.x, s.y);
        }
        std::sort(p.begin(), p.end());
        return static_cast<std::size_t>(std::unique(p.begin(), p.end()) - p.begin());
    };
    CHECK(places() == 20);  // slot 17 stands on slot 15
    CHECK((assets::fixes::fix_figures(figures) == std::vector<std::string_view>{"D-014"}));
    CHECK(places() == 21);
    CHECK(figures.formations()[2][17].x == 2);
    CHECK(figures.formations()[2][17].y == 1);
    CHECK(assets::fixes::fix_figures(figures).empty());

    auto other = make_figures("2, 2");  // a mod's own place stays
    CHECK(assets::fixes::fix_figures(other).empty());
    CHECK(other.formations()[2][17].y == 2);
}

TEST_CASE("data fixes: the accents of capitals go back above the letter (D-012)") {
    // Glyphs 32 .. 0xDD, all blank but 'A', À, Á, Ç and Ä. À keeps the upper row of its accent in
    // the cell's last row; Á has none there; Ç's cedilla belongs below; Ä fills its cell to the bottom.
    std::vector<std::vector<std::string>> glyphs(0xDE - 32, std::vector<std::string>(6, "...."));
    glyphs['A' - 32] = {"....", ".##.", "#..#", "####", "#..#", "...."};
    glyphs[0xC0 - 32] = {"..#.", ".##.", "#..#", "####", "....", ".#.."};
    glyphs[0xC1 - 32] = {"..#.", ".##.", "#..#", "####", "#..#", "...."};
    glyphs[0xC4 - 32] = {"#..#", ".##.", "#..#", "####", "#..#", "#..#"};
    glyphs[0xC7 - 32] = {"....", ".##.", "#...", ".##.", "....", "..#."};
    auto font = ui::Font::from_sheet(sheet_of(glyphs));
    REQUIRE(font.ok());
    CHECK(font->height() == 6);
    CHECK(font->above() == 0);
    CHECK(
        (drawn(*font, "\xC0") == std::vector<std::string>{"..#...", ".##...", "#..#..", "####..", "......", ".#...."}));

    CHECK(font->fix_accents() == 1);  // À only
    CHECK(font->height() == 6);       // the cell and the lines stay
    CHECK(font->above() == 1);
    CHECK((drawn(*font, "\xC0") ==
           std::vector<std::string>{".#....", "..#...", ".##...", "#..#..", "####..", "......", "......"}));
    CHECK((drawn(*font, "\xC7") ==
           std::vector<std::string>{"......", "......", ".##...", "#.....", ".##...", "......", "..#..."}));
    CHECK(drawn(*font, "A")[6] == "......");  // the other letters stand where they stood
    CHECK(drawn(*font, "A")[5] == "#..#..");
    CHECK(font->fix_accents() == 0);
}

TEST_CASE("building table: the records the game places by Id are required (MD-1)") {
    // The bakery and the Kontors only: the meadow is the first one missing.
    CHECK(make_buildings(false).missing() == assets::id::kMeadow);

    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto with = [&](bool ruin) {
        auto table = assets::BuildingTable::from_cod(assets::cod::parse(
            std::string("Objekt: HAUS\n"
                        "  @Nummer: 0\n  Id: 22722\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
                        "  @Nummer: 1\n  Id: 21108\n  Kind: 9\n  Gfx: 1\n  Size: 4, 4\n") +
                (ruin ? "  @Nummer: 2\n  Id: 22601\n  Kind: 11\n  Gfx: 20\n  Size: 1, 1\n" : "") + "EndObj\n",
            types));
        REQUIRE(table.ok());
        return std::move(table).value();
    };
    CHECK(with(false).missing() == assets::id::kRuin);
    CHECK(!with(true).missing().has_value());
}

TEST_CASE("game data: the Kontors of the four levels are a table, not id arithmetic (MD-4)") {
    for (std::uint32_t level = 1; level <= 4; ++level) {
        CHECK(assets::id::kontor(level) == assets::id::kKontor1 + 2 * (level - 1));
        CHECK(assets::id::kontor_level(assets::id::kontor(level)) == level);
    }
    CHECK(assets::id::kontor(0) == assets::id::kKontor1);
    CHECK(assets::id::kontor(9) == assets::id::kKontors[3]);
    // As the original's range check: the records between two levels count as the lower one.
    for (std::uint32_t building = assets::id::kKontor1; building <= assets::id::kKontor1 + 6; ++building) {
        CHECK(assets::id::kontor_level(building) == (building - assets::id::kKontor1) / 2 + 1);
    }
    CHECK(assets::id::kontor_level(assets::id::kKontor1 - 1) == 0);
    CHECK(assets::id::kontor_level(assets::id::kKontor1 + 7) == 0);
}

TEST_CASE("game data: unit and ship figures come from the tables, not from id arithmetic (MD-6)") {
    CHECK(assets::figure::unit_figure(0, 0) == 1);   // SOLDAT1, red
    CHECK(assets::figure::unit_figure(1, 2) == 7);   // KAVALERIE, yellow
    CHECK(assets::figure::unit_figure(3, 3) == 16);  // KANONIER, white
    CHECK(assets::figure::unit_figure(4, 0) == 0);
    CHECK(assets::figure::unit_figure(0, 4) == 0);
    CHECK(assets::figure::ship_figure(0) == 21);
    CHECK(assets::figure::ship_figure(3) == 27);
    CHECK(assets::figure::ship_figure(4) == 0);
    CHECK(assets::unit_ware(1) == assets::ware::kFirstSoldier);
    CHECK(assets::unit_of_ware(assets::ware::kLastSoldier) == 20);
    for (std::size_t family = 0; family < assets::figure::kUnitFamilies.size(); ++family) {
        for (std::int32_t colour = 0; colour < assets::figure::kUnitColours; ++colour) {
            CHECK(assets::is_soldier_ware(assets::unit_ware(assets::figure::unit_figure(family, colour))));
        }
    }
}
