// SPDX-License-Identifier: GPL-3.0-or-later
// The islands of a world plan (world_generator.hpp) on a synthetic game
// directory: island templates of every climate and size class, small enough to write in a moment.

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/sim/island_shapes.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/validate.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_generator.hpp"
#include "o1602/sim/world_plan.hpp"

using namespace o1602;

namespace {

// Wares and buildings the plans name (the ids of the original's plans, world_plan.cpp).
constexpr std::uint8_t kIron = 2;
constexpr std::uint8_t kGold = 3;
constexpr std::uint8_t kTreasure = 58;
constexpr std::uint8_t kFirstCrop = 45;  // fertility bit 0
constexpr std::uint8_t kTobacco = 46;
constexpr std::uint8_t kSugar = 48;
constexpr std::uint8_t kWine = 50;
constexpr std::uint8_t kCocoa = 51;
constexpr std::uint32_t kIronMine = 22401;
constexpr std::uint32_t kGoldMine = 22405;
constexpr std::uint32_t kGoldsmith = 20533;
// Plantations of tobacco, spices, sugar, cotton, wine and cocoa (wares 46..51).
constexpr std::array<std::uint32_t, 6> kCrops = {21336, 21342, 21340, 21332, 21344, 21338};
constexpr std::uint32_t kBaseFertility = 0x1181;  // grain, grass, trees, fish on every island

// Island tiles (haeuser.cod id - 20000).
constexpr std::uint16_t kGroundTile = 101;
constexpr std::uint16_t kVillageTile = 533;  // 20533, a building of the fake table
constexpr std::uint16_t kBigMountainTile = 1101;
constexpr std::uint16_t kSmallMountainTile = 1102;

// Template files per climate and size class, as the original draws them.
constexpr std::array<std::array<std::uint16_t, 5>, 2> kTemplateCount = {{{51, 48, 36, 67, 5}, {49, 45, 48, 63, 6}}};
constexpr std::array<const char*, 2> kClimateDir = {"Nord", "Sued"};
constexpr std::array<const char*, 2> kNativeDir = {"NordNat", "SuedNat"};
constexpr std::array<const char*, 5> kSizeName = {"lit", "mit", "med", "big", "lar"};

/// Ground, forest, a big and a small ore mountain (2 x 2), the mines, the goldsmith and the six
/// plantations; and the tiles of islands of their own shape (island_shapes.hpp).
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    std::string text =
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // ground
        "  @Nummer: 1\n  Id: 20102\n  Kind: 10\n  Gfx: 1\n  Size: 1, 1\n"  // forest
        "  @Nummer: 2\n  Id: 21101\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n"  // big ore mountain
        "  Objekt: HAUS_PRODTYP\n    Kind: 12\n    Ware: 55\n    Erzbergnr: 0\n  EndObj\n"
        "  @Nummer: 3\n  Id: 21102\n  Kind: 14\n  Gfx: 6\n  Size: 2, 2\n"  // small ore mountain
        "  Objekt: HAUS_PRODTYP\n    Kind: 12\n    Ware: 55\n    Erzbergnr: 1\n  EndObj\n"
        "  @Nummer: 4\n  Id: 22401\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n"  // iron mine
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Ware: 2\n  EndObj\n"
        "  @Nummer: 5\n  Id: 22405\n  Kind: 14\n  Gfx: 14\n  Size: 2, 2\n"  // gold mine
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Ware: 3\n  EndObj\n"
        "  @Nummer: 6\n  Id: 20533\n  Kind: 14\n  Gfx: 18\n  Size: 1, 1\n"  // goldsmith
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Ware: 21\n  EndObj\n";
    for (std::size_t i = 0; i < kCrops.size(); ++i) {
        text += "  @Nummer: " + std::to_string(7 + i) + "\n  Id: " + std::to_string(kCrops[i]) +
                "\n  Kind: 14\n  Gfx: " + std::to_string(19 + i) + "\n  Size: 1, 1\n" +
                "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: " + std::to_string(kTobacco + i) + "\n  EndObj\n";
    }
    std::uint32_t number = 7 + static_cast<std::uint32_t>(kCrops.size());
    std::uint32_t gfx = 100;
    const auto add = [&](std::span<const std::uint32_t> ids, int kind, std::uint32_t size, int ore) {
        for (const std::uint32_t id : ids) {
            text += "  @Nummer: " + std::to_string(number++) + "\n  Id: " + std::to_string(id) +
                    "\n  Kind: " + std::to_string(kind) + "\n  Gfx: " + std::to_string(gfx) +
                    "\n  Size: " + std::to_string(size) + ", " + std::to_string(size) + "\n";
            if (ore >= 0) {
                text += "  Objekt: HAUS_PRODTYP\n    Kind: 12\n    Ware: 55\n    Erzbergnr: " + std::to_string(ore) +
                        "\n  EndObj\n";
            }
            gfx += size * size;
        }
    };
    namespace ids = sim::shapes::ids;
    add(ids::kOpenSea, 19, 1, -1);
    add(ids::kSurf, 22, 1, -1);
    add(ids::kSurfCorner, 21, 1, -1);
    add(ids::kSurfBay, 21, 1, -1);
    add(ids::kBeach, 27, 1, -1);
    add(ids::kOuterCorner, 25, 1, -1);
    add(ids::kInnerCorner, 26, 1, -1);
    std::vector<std::uint32_t> grounds;
    for (const auto& g : ids::kGroundMix) {
        grounds.push_back(g.id);
    }
    if (std::find(grounds.begin(), grounds.end(), ids::kGround) == grounds.end()) {
        grounds.push_back(ids::kGround);
    }
    add(grounds, 11, 1, -1);
    add(ids::kSlope, 31, 1, -1);
    add(std::array<std::uint32_t, 1>{ids::kSlopeCorner}, 32, 1, -1);
    add(std::array<std::uint32_t, 1>{ids::kSpring}, 33, 1, -1);
    add(ids::kRiver, 16, 1, -1);
    add(ids::kRiverBend, 17, ids::kRiverBendSize, -1);
    add(std::array<std::uint32_t, 1>{ids::kRiverMouth}, 24, 1, -1);
    add(std::array<std::uint32_t, 1>{ids::kMouthSurf}, 22, 1, -1);
    add(ids::kForestNorth, 10, 1, -1);
    add(ids::kForestSouth, 10, 1, -1);
    add(ids::kBigMountain, 15, ids::kBigMountainSize, 0);
    add(std::array<std::uint32_t, 1>{ids::kVolcanoMountain}, 15, ids::kBigMountainSize, 0);
    add(ids::kSmallMountain, 15, ids::kSmallMountainSize, 1);
    text += "EndObj\n";
    const auto doc = assets::cod::parse(text, types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

const assets::BuildingTable& buildings() {
    static const assets::BuildingTable table = make_buildings();
    return table;
}

std::uint32_t side(std::uint8_t size) {
    // The narrowest width of each size class: the loader takes the class of an INSEL5 from its width.
    static constexpr std::uint32_t kSides[] = {16, 33, 43, 56, 76};  // lit .. lar
    return kSides[size];
}

struct Spot {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    auto operator<=>(const Spot&) const = default;
};

/// Origins of the two ore mountains of a template (they move with the template number).
std::array<Spot, 2> mountain_spots(std::uint8_t size, std::uint16_t number) {
    const auto s = static_cast<std::uint8_t>(side(size));
    return {Spot{static_cast<std::uint8_t>(3 + number % 4), 3},
            Spot{static_cast<std::uint8_t>(s - 6), static_cast<std::uint8_t>(s - 6 - number % 3)}};
}

std::string template_path(std::uint8_t climate, std::uint8_t size, std::uint16_t number, bool natives) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s/%s%02u.scp", (natives ? kNativeDir : kClimateDir)[climate], kSizeName[size],
                  static_cast<unsigned>(number));
    return buf;
}

void put_u16(std::vector<std::uint8_t>& d, std::size_t o, std::uint32_t v) {
    d[o] = static_cast<std::uint8_t>(v);
    d[o + 1] = static_cast<std::uint8_t>(v >> 8);
}

void put_u32(std::vector<std::uint8_t>& d, std::size_t o, std::uint32_t v) {
    put_u16(d, o, v);
    put_u16(d, o + 2, v >> 16);
}

void add_tile(std::vector<std::uint8_t>& tiles, std::uint16_t id, std::uint32_t x, std::uint32_t y) {
    // City 7 and owner 7: terrain of nobody.
    const std::uint32_t bits = (7U << 14) | (7U << 22);
    tiles.insert(tiles.end(), {static_cast<std::uint8_t>(id), static_cast<std::uint8_t>(id >> 8),
                               static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)});
    o1602::test::put_u32(tiles, bits);
}

/// A template (.scp): INSEL5 and one INSELHAUS layer of ground with, if wanted, the two mountains.
std::vector<std::uint8_t> template_file(std::uint8_t climate, std::uint8_t size, std::uint16_t number, bool mountains) {
    const std::uint32_t s = side(size);
    std::vector<std::uint8_t> header(116, 0);
    header[1] = static_cast<std::uint8_t>(s);
    header[2] = static_cast<std::uint8_t>(s);
    put_u16(header, 0x60, number);
    put_u16(header, 0x62, size);
    header[0x64] = climate;
    header[0x65] = 1;  // stored complete
    std::vector<std::uint8_t> tiles;
    for (std::uint32_t y = 0; y < s; ++y) {
        for (std::uint32_t x = 0; x < s; ++x) {
            add_tile(tiles, kGroundTile, x, y);
        }
    }
    if (mountains) {
        const auto spots = mountain_spots(size, number);
        add_tile(tiles, kBigMountainTile, spots[0].x, spots[0].y);
        add_tile(tiles, kSmallMountainTile, spots[1].x, spots[1].y);
    }
    std::vector<std::uint8_t> file;
    o1602::test::put_chunk(file, "INSEL5", header);
    o1602::test::put_chunk(file, "INSELHAUS", tiles);
    return file;
}

/// Where the native templates' growth record lies, within their village.
struct TilePos {
    std::uint8_t x;
    std::uint8_t y;
};
constexpr TilePos kVillageRecord{6, 5};

/// A native template: as large as its template, only the village part (as NordNat and SuedNat
/// are): a block of 6 x 5 tiles of buildings, and a growth record (ROHWACHS2) on its tile (6, 5).
std::vector<std::uint8_t> village_file(std::uint8_t climate, std::uint8_t size, std::uint16_t number) {
    std::vector<std::uint8_t> file = template_file(climate, size, number, false);
    std::vector<std::uint8_t> header(file.begin() + 20, file.begin() + 20 + 116);
    std::vector<std::uint8_t> tiles;
    for (std::uint32_t y = 4; y < 9; ++y) {
        for (std::uint32_t x = 5; x < 11; ++x) {
            add_tile(tiles, kVillageTile, x, y);
        }
    }
    std::vector<std::uint8_t> out;
    o1602::test::put_chunk(out, "INSEL5", header);
    o1602::test::put_chunk(out, "INSELHAUS", tiles);
    o1602::test::put_chunk(out, "ROHWACHS2", {0, kVillageRecord.x, kVillageRecord.y, 0, 0, 0, 0, 0});
    return out;
}

/// What a synthetic game directory holds.
struct Contents {
    /// Template classes written (climate, size); all if empty.
    std::set<std::pair<std::uint8_t, std::uint8_t>> classes;
    bool mountains = true;
    /// A native version (same size) for every template whose number is a multiple of this, 0 = none.
    std::uint16_t natives_every = 3;
};

/// A game directory of its own under the system's temporary folder, removed again at the end:
/// what GameDirectory::open looks for, and the island templates.
struct FakeGame {
    std::filesystem::path root;
    assets::GameDirectory game;

    explicit FakeGame(const Contents& contents) {
        root = test::temp_path("o1602_worldgen_" + std::to_string(reinterpret_cast<std::uintptr_t>(this) & 0xFFFFFU));
        std::filesystem::remove_all(root);
        for (const char* dir : {"toolgfx", "gfx", "Nord", "Sued", "NordNat", "SuedNat"}) {
            std::filesystem::create_directories(root / dir);
        }
        for (const char* file : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col", "gfx/stadtfld.bsh"}) {
            std::ofstream(root / file, std::ios::binary) << "x";
        }
        for (std::uint8_t climate = 0; climate < 2; ++climate) {
            for (std::uint8_t size = 0; size < 5; ++size) {
                if (!contents.classes.empty() && contents.classes.count({climate, size}) == 0) {
                    continue;
                }
                for (std::uint16_t n = 0; n < kTemplateCount[climate][size]; ++n) {
                    const auto bytes = template_file(climate, size, n, contents.mountains);
                    write(template_path(climate, size, n, false), bytes);
                    if (contents.natives_every != 0 && n % contents.natives_every == 0) {
                        write(template_path(climate, size, n, true), village_file(climate, size, n));
                    }
                }
            }
        }
        auto opened = assets::GameDirectory::open(root);
        REQUIRE(opened.ok());
        game = std::move(opened).value();
    }
    ~FakeGame() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    FakeGame(const FakeGame&) = delete;
    FakeGame& operator=(const FakeGame&) = delete;

    void write(const std::string& relative, const std::vector<std::uint8_t>& bytes) const {
        std::ofstream out(root / relative, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
};

/// Every template of every class, shared by the tests that need no special directory.
const FakeGame& full_game() {
    static const FakeGame game{Contents{}};
    return game;
}

/// SZENE as the original stores it (0x494 bytes, scenario.cpp load_scenario_plan).
std::vector<std::uint8_t> encode_plan(const assets::ScenarioPlan& p) {
    std::vector<std::uint8_t> d(0x494, 0);
    put_u32(d, 0x40, p.natives[0]);
    put_u32(d, 0x44, p.natives[1]);
    put_u32(d, 0x60, p.max_crops);
    put_u32(d, 0x64, static_cast<std::uint32_t>(p.islands.size()));
    put_u32(d, 0x68, p.gold_min_size);
    put_u32(d, 0x6C, p.gold_max_size);
    const auto resource = [&](std::size_t o, const assets::ScenarioPlan::Resource& r) {
        if (r.building != 0) {
            put_u16(d, o, r.building - 20000U);
            put_u16(d, o + 2, r.count);
            put_u32(d, o + 4, r.type);
        }
    };
    for (std::size_t k = 0; k < p.ores.size(); ++k) {
        resource(0x94 + 8 * k, p.ores[k]);
    }
    for (std::size_t k = 0; k < p.crops.size(); ++k) {
        resource(0xB4 + 8 * k, p.crops[k]);
    }
    for (std::size_t k = 0; k < p.treasures.size(); ++k) {
        resource(0x114 + 8 * k, p.treasures[k]);
    }
    for (std::size_t i = 0; i < p.islands.size(); ++i) {
        const auto& e = p.islands[i];
        const std::size_t o = 0x174 + 16 * i;
        d[o] = e.climate;
        d[o + 1] = e.size;
        d[o + 2] = e.flags;
        d[o + 3] = e.number;
        put_u16(d, o + 4, e.template_number);
        put_u32(d, o + 8, e.x);
        put_u32(d, o + 12, e.y);
    }
    return d;
}

/// A saved rand() table: 512 numbers of 0..32767.
std::vector<std::uint8_t> random_table() {
    std::vector<std::uint8_t> d;
    std::uint32_t state = 1602;
    for (int i = 0; i < 512; ++i) {
        state = state * 214013U + 2531011U;
        const std::uint32_t v = (state >> 16) & 0x7FFFU;
        d.push_back(static_cast<std::uint8_t>(v));
        d.push_back(static_cast<std::uint8_t>(v >> 8));
    }
    return d;
}

/// A scenario of the endless kind: the plan, the table (if wanted) and a chunk of its own that the
/// generator must keep.
assets::ChunkFile scenario(const assets::ScenarioPlan& plan, bool table = true) {
    std::vector<std::uint8_t> bytes;
    o1602::test::put_chunk(bytes, "SZENE", encode_plan(plan));
    if (table) {
        o1602::test::put_chunk(bytes, "RANDTAB", random_table());
    }
    o1602::test::put_chunk(bytes, "O1602TEST", {1, 6, 0, 2});
    auto file = assets::ChunkFile::from_bytes(bytes);
    REQUIRE(file.ok());
    return std::move(file).value();
}

assets::ScenarioPlan::Island island(std::uint8_t number, std::uint8_t climate, std::uint8_t size, std::uint32_t x,
                                    std::uint32_t y) {
    assets::ScenarioPlan::Island e;
    e.number = number;
    e.climate = climate;
    e.size = size;
    e.x = x;
    e.y = y;
    return e;
}

/// The original's resource lists: big and small iron, gold, every crop twice, two treasures.
void add_resources(assets::ScenarioPlan& p) {
    p.ores[0] = {kIronMine, 2, 0};
    p.ores[1] = {kIronMine, 3, 1};
    p.ores[2] = {kGoldMine, 2, 1};
    for (std::size_t i = 0; i < kCrops.size(); ++i) {
        p.crops[i] = {kCrops[i], 2, 0};
    }
    p.treasures[0] = {kGoldsmith, 2, 0};
    p.max_crops = 2;
    p.gold_min_size = 1;
    p.gold_max_size = 3;
}

/// Sixteen islands of all classes on the original's map, two of them climate by position (one at
/// the top, one at the bottom), listed against the order of their numbers.
assets::ScenarioPlan standard_plan() {
    assets::ScenarioPlan p;
    p.natives = {1, 1};
    add_resources(p);
    struct Entry {
        std::uint8_t climate;
        std::uint8_t size;
    };
    const std::array<Entry, 16> entries = {{{2, 0},
                                            {0, 0},
                                            {0, 0},
                                            {1, 0},
                                            {1, 1},
                                            {0, 1},
                                            {0, 2},
                                            {1, 2},
                                            {0, 3},
                                            {1, 3},
                                            {0, 4},
                                            {1, 4},
                                            {0, 1},
                                            {1, 1},
                                            {0, 2},
                                            {2, 0}}};
    for (std::size_t i = entries.size(); i-- > 0;) {
        // Rows at y 20, 120, 220 and 300: the first island lies in the north, the last in the south.
        const auto x = static_cast<std::uint32_t>(20 + (i % 4) * 110);
        const auto y = static_cast<std::uint32_t>(i < 12 ? 20 + (i / 4) * 100 : 300);
        p.islands.push_back(island(static_cast<std::uint8_t>(i), entries[i].climate, entries[i].size, x, y));
    }
    return p;
}

bool has_crop(std::uint32_t fertility, std::uint8_t ware) {
    return (fertility & (1U << (ware - kFirstCrop))) != 0;
}

std::uint32_t crop_count(std::uint32_t fertility) {
    std::uint32_t n = 0;
    for (std::uint8_t w = kTobacco; w <= kCocoa; ++w) {
        n += has_crop(fertility, w) ? 1U : 0U;
    }
    return n;
}

std::size_t deposits_of(const sim::GeneratedWorld& w, std::uint8_t ware) {
    std::size_t n = 0;
    for (const auto& g : w.islands) {
        n += static_cast<std::size_t>(
            std::count_if(g.deposits.begin(), g.deposits.end(), [&](const auto& d) { return d.ware == ware; }));
    }
    return n;
}

bool same_world(const sim::GeneratedWorld& a, const sim::GeneratedWorld& b) {
    return a.draws == b.draws && a.lost == b.lost && a.guaranteed == b.guaranteed &&
           std::equal(a.file.data().begin(), a.file.data().end(), b.file.data().begin(), b.file.data().end());
}

/// The rules every generated island keeps, whatever the draws. Own worlds may give sugar and wine
/// to one island when it is the only northern one near a start.
void check_islands(const sim::GeneratedWorld& w, const assets::ScenarioPlan& plan, bool own_world = false) {
    std::map<std::uint8_t, assets::ScenarioPlan::Island> planned;
    for (const auto& e : plan.islands) {
        planned[e.number] = e;
    }
    std::set<std::tuple<int, int, int>> templates;
    std::size_t own_shapes = 0;
    for (std::size_t i = 0; i < w.islands.size(); ++i) {
        const sim::GeneratedIsland& g = w.islands[i];
        if (i > 0) {
            CHECK(w.islands[i - 1].number < g.number);  // in the order of the island numbers
        }
        REQUIRE(planned.count(g.number) == 1);
        const auto& e = planned[g.number];
        CHECK(g.size == e.size);
        if (e.climate < 2) {
            CHECK(g.climate == e.climate);
        }
        CHECK(g.climate < 2);
        // An island of its own shape has no template; its mountains lie where it drew them.
        const bool own_shape = g.template_number == assets::IslandHeader::kNoTemplate;
        CHECK((own_shape || g.template_number < kTemplateCount[g.climate][g.size]));
        if (own_shape) {
            ++own_shapes;
        } else {
            templates.insert({g.climate, g.size, g.template_number});
        }
        CHECK((g.fertility & kBaseFertility) == kBaseFertility);
        // Crops of the island's climate only, sugar and wine not together.
        for (std::uint8_t ware = kTobacco; ware <= kCocoa; ++ware) {
            const bool north_crop = ware == kTobacco || ware == kSugar || ware == kWine;
            if (has_crop(g.fertility, ware)) {
                CHECK(north_crop == (g.climate == 0));
            }
        }
        CHECK((own_world || !(has_crop(g.fertility, kSugar) && has_crop(g.fertility, kWine))));
        // At most two deposits (an island of its own size one for every 2000 tiles), each on
        // a mountain of its own; treasures on open ground of middle islands, in own worlds also of
        // big ones.
        CHECK(g.deposits.size() <= std::max<std::size_t>(2, own_shape ? std::size_t{g.width} * g.height / 2000 : 2));
        std::set<Spot> used;
        const auto mountains = mountain_spots(g.size, g.template_number);
        for (const auto& d : g.deposits) {
            CHECK(used.insert(Spot{d.x, d.y}).second);
            if (d.ware == kIron || d.ware == kGold) {
                CHECK((own_shape || std::find(mountains.begin(), mountains.end(), Spot{d.x, d.y}) != mountains.end()));
                CHECK(d.amount > 0);
            } else {
                CHECK(d.ware == kTreasure);
                CHECK((g.size == 1 || g.size == 2 || (own_world && g.size >= 3)));
                const bool on_mountain = std::any_of(mountains.begin(), mountains.end(), [&](const Spot& m) {
                    return d.x >= m.x && d.x < m.x + 2 && d.y >= m.y && d.y < m.y + 2;
                });
                CHECK(!on_mountain);
                CHECK((d.x >= 1 && d.x + 1U < g.width && d.y >= 1 && d.y + 1U < g.height));
            }
        }
    }
    // Templates differ within a class (every class has more templates than the plan islands).
    CHECK(templates.size() + own_shapes == w.islands.size());
}

}  // namespace

TEST_CASE("world generator: table draws are Rnd() % n from the start index, wrapping at 512") {
    std::array<std::uint16_t, 512> table{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<std::uint16_t>(i * 7 + 3);
    }
    sim::PlanRandom random(table, 510);
    CHECK(random.draws() == 0);
    CHECK(random.below(1000) == (510 * 7 + 3) % 1000);
    CHECK(random.below(10) == (511 * 7 + 3) % 10);
    CHECK(random.below(100) == 3);  // index 0 after 511
    CHECK(random.below(5) == 10 % 5);
    CHECK(random.draws() == 4);
    CHECK(random.below(1) == 0);
    CHECK(random.draws() == 5);

    // The start index is taken modulo 512.
    sim::PlanRandom a(table, 3);
    sim::PlanRandom b(table, 512 + 3);
    for (std::uint32_t n = 1; n < 40; ++n) {
        CHECK(a.below(n) == b.below(n));
    }

    // A number above 0x7FFF is a negative Rnd(); the result stays in 0..n-1.
    table.fill(0xFFFF);
    sim::PlanRandom negative(table, 0);
    const std::uint32_t r = negative.below(10);
    CHECK(r < 10);
    CHECK(negative.below(0) == 0);  // nothing to choose from
}

TEST_CASE("world generator: seeded draws repeat for the same seed and stay below n") {
    sim::PlanRandom a(std::uint64_t{0x1602});
    sim::PlanRandom b(std::uint64_t{0x1602});
    sim::PlanRandom c(std::uint64_t{0x1603});
    bool differs = false;
    for (std::uint32_t i = 0; i < 200; ++i) {
        const std::uint32_t n = 1 + i % 37;
        const std::uint32_t va = a.below(n);
        CHECK(va < n);
        CHECK(va == b.below(n));
        differs = differs || va != c.below(n);
    }
    CHECK(differs);
    CHECK(a.draws() == 200);
}

TEST_CASE("world generator: plans_islands only for a plan with islands the file does not store") {
    CHECK(sim::plans_islands(scenario(standard_plan())));

    // No SZENE, or one without islands.
    std::vector<std::uint8_t> bytes;
    o1602::test::put_chunk(bytes, "O1602TEST", {1});
    CHECK(!sim::plans_islands(*assets::ChunkFile::from_bytes(bytes)));
    CHECK(!sim::plans_islands(scenario(assets::ScenarioPlan{})));

    // A scenario that stores some islands itself still has the others made.
    bytes.clear();
    o1602::test::put_chunk(bytes, "SZENE", encode_plan(standard_plan()));
    o1602::test::put_chunk(bytes, "INSEL5", std::vector<std::uint8_t>(116, 0));
    CHECK(sim::plans_islands(*assets::ChunkFile::from_bytes(bytes)));
    // One that stores every planned island (a savegame that kept its plan) has nothing to make.
    bytes.clear();
    o1602::test::put_chunk(bytes, "SZENE", encode_plan(standard_plan()));
    for (const auto& e : standard_plan().islands) {
        std::vector<std::uint8_t> header(116, 0);
        header[0] = e.number;
        o1602::test::put_chunk(bytes, "INSEL5", header);
    }
    CHECK(!sim::plans_islands(*assets::ChunkFile::from_bytes(bytes)));

    // A file without a plan cannot be generated.
    bytes.clear();
    o1602::test::put_chunk(bytes, "O1602TEST", {1});
    CHECK(!sim::generate_islands(*assets::ChunkFile::from_bytes(bytes), full_game().game, buildings(), {}).ok());
}

TEST_CASE("world generator: a scenario's stored islands are kept and the planned ones made beside them") {
    // As "Eine einsame Siedlung": the home island stored in the file, the others in its plan.
    const auto& fake = full_game();
    const auto plan = standard_plan();
    auto full = sim::generate_islands(scenario(plan), fake.game, buildings(), sim::WorldStart{});
    REQUIRE(full.ok());
    // The file stores island 5 as the generator made it; the plan still lists all of them.
    std::vector<std::uint8_t> bytes;
    o1602::test::put_chunk(bytes, "SZENE", encode_plan(plan));
    o1602::test::put_chunk(bytes, "RANDTAB", random_table());
    bool in_island = false;
    std::vector<std::uint8_t> stored_header;
    for (const assets::Chunk& c : full->file.chunks()) {
        const auto payload = full->file.payload(c);
        if (c.name == "INSEL5") {
            in_island = !payload.empty() && payload[0] == 5;
            if (in_island) {
                stored_header.assign(payload.begin(), payload.end());
            }
        }
        if (in_island && (c.name == "INSEL5" || c.name == "INSELHAUS")) {
            o1602::test::put_chunk(bytes, c.name.c_str(), std::vector<std::uint8_t>(payload.begin(), payload.end()));
        }
    }
    REQUIRE(!stored_header.empty());
    o1602::test::put_chunk(bytes, "O1602TEST", {1, 6, 0, 2});
    const auto file = assets::ChunkFile::from_bytes(bytes);
    REQUIRE(file.ok());
    REQUIRE(sim::plans_islands(*file));

    auto w = sim::generate_islands(*file, fake.game, buildings(), sim::WorldStart{});
    REQUIRE(w.ok());
    CHECK(w->islands.size() == plan.islands.size() - 1);
    CHECK(std::none_of(w->islands.begin(), w->islands.end(), [](const auto& g) { return g.number == 5; }));
    // Every island in the file, in the order of the numbers, the stored one unchanged.
    auto islands = assets::load_islands(w->file);
    REQUIRE(islands.ok());
    REQUIRE(islands->size() == plan.islands.size());
    for (std::size_t i = 0; i < islands->size(); ++i) {
        CHECK((*islands)[i].header.number == i);
    }
    CHECK(!(*islands)[5].layers.empty());
    const auto out = std::find_if(w->file.chunks().begin(), w->file.chunks().end(), [&](const assets::Chunk& c) {
        return c.name == "INSEL5" && w->file.payload(c)[0] == 5;
    });
    REQUIRE(out != w->file.chunks().end());
    const auto payload = w->file.payload(*out);
    CHECK(std::equal(payload.begin(), payload.end(), stored_header.begin(), stored_header.end()));
}

TEST_CASE("world generator: every planned island gets an unused template of its climate and size") {
    const auto& fake = full_game();
    auto plan = standard_plan();
    // A fixed template is kept, and no other island of its class draws it.
    plan.islands[3].template_number = 7;
    const auto fixed = plan.islands[3];
    const auto file = scenario(plan);
    std::vector<std::string> warnings;
    auto w = sim::generate_islands(file, fake.game, buildings(), sim::WorldStart{}, &warnings);
    REQUIRE(w.ok());
    CHECK(warnings.empty());
    REQUIRE(w->islands.size() == plan.islands.size());
    check_islands(*w, plan);
    CHECK(w->draws > 0);
    // The mountains and the open ground of the templates take ore and treasures.
    CHECK(deposits_of(*w, kIron) > 0);
    CHECK(deposits_of(*w, kGold) > 0);
    CHECK(deposits_of(*w, kTreasure) > 0);
    for (const auto& g : w->islands) {
        CHECK(fake.game.resolve(template_path(g.climate, g.size, g.template_number, false)).has_value());
        if (g.number == fixed.number) {
            CHECK(g.template_number == 7);
        }
    }
    // Climate by position: the north at the top of the map, the south at the bottom.
    CHECK(w->islands.front().number == 0);
    CHECK(w->islands.front().climate == 0);
    CHECK(w->islands.back().number == 15);
    CHECK(w->islands.back().climate == 1);
    // One village per climate, on islands that have a native template.
    std::array<std::uint32_t, 2> villages{};
    for (const auto& g : w->islands) {
        if (g.natives) {
            ++villages[g.climate];
            CHECK(fake.game.resolve(template_path(g.climate, g.size, g.template_number, true)).has_value());
        }
    }
    CHECK(villages[0] == 1);
    CHECK(villages[1] == 1);

    // The scenario with these islands: an INSEL5 per island pointing to its template, the plan and
    // the table gone, the scenario's other chunks kept.
    CHECK(!sim::plans_islands(w->file));
    CHECK(w->file.find("SZENE") == nullptr);
    CHECK(w->file.find("RANDTAB") == nullptr);
    REQUIRE(w->file.find("O1602TEST") != nullptr);
    CHECK(w->file.payload(*w->file.find("O1602TEST")).size() == 4);
    auto headers = assets::load_islands(w->file);
    REQUIRE(headers.ok());
    REQUIRE(headers->size() == w->islands.size());
    std::map<std::uint8_t, assets::ScenarioPlan::Island> planned;
    for (const auto& e : plan.islands) {
        planned[e.number] = e;
    }
    for (std::size_t i = 0; i < headers->size(); ++i) {
        const assets::IslandHeader& h = (*headers)[i].header;
        const sim::GeneratedIsland& g = w->islands[i];
        CHECK(h.number == g.number);
        CHECK(h.pos_x == planned[g.number].x);
        CHECK(h.pos_y == planned[g.number].y);
        CHECK(h.width == side(g.size));
        CHECK(h.template_path() == template_path(g.climate, g.size, g.template_number, false));
        CHECK(h.fertility == g.fertility);
        REQUIRE(h.deposits.size() == g.deposits.size());
        for (std::size_t k = 0; k < h.deposits.size(); ++k) {
            CHECK(h.deposits[k].ware == g.deposits[k].ware);
            CHECK(h.deposits[k].x == g.deposits[k].x);
            CHECK(h.deposits[k].y == g.deposits[k].y);
            CHECK(h.deposits[k].amount == g.deposits[k].amount);
        }
    }
}

TEST_CASE("world generator: the table start decides the world, the same start gives the same bytes") {
    const auto& fake = full_game();
    const auto plan = standard_plan();
    const auto file = scenario(plan);
    sim::WorldStart start;
    start.seed = 5;
    auto a = sim::generate_islands(file, fake.game, buildings(), start);
    auto b = sim::generate_islands(file, fake.game, buildings(), start);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(same_world(*a, *b));
    // The table has 512 starts: seed 5 + 512 is seed 5.
    start.seed = 5 + 512;
    auto c = sim::generate_islands(file, fake.game, buildings(), start);
    REQUIRE(c.ok());
    CHECK(same_world(*a, *c));
    // Other starts give other worlds (most of them other templates).
    std::size_t other = 0;
    for (std::uint64_t seed = 6; seed < 16; ++seed) {
        start.seed = seed;
        auto d = sim::generate_islands(file, fake.game, buildings(), start);
        REQUIRE(d.ok());
        check_islands(*d, plan);
        other += same_world(*a, *d) ? 0U : 1U;
    }
    CHECK(other == 10);
}

TEST_CASE("world generator: seeded draws: the same seed the same world; no table falls back to the seed") {
    const auto& fake = full_game();
    const auto plan = standard_plan();
    const auto with_table = scenario(plan);
    sim::WorldStart start;
    start.draws = sim::WorldStart::Draws::Seeded;
    start.seed = 0x1602'0000'1234ULL;
    auto a = sim::generate_islands(with_table, fake.game, buildings(), start);
    auto b = sim::generate_islands(with_table, fake.game, buildings(), start);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(same_world(*a, *b));
    check_islands(*a, plan);
    // Seeded draws use all 64 bits: seed + 512 is another world.
    start.seed += 512;
    auto c = sim::generate_islands(with_table, fake.game, buildings(), start);
    REQUIRE(c.ok());
    CHECK(!same_world(*a, *c));

    // A scenario without RANDTAB draws from the seed even when the table is asked for.
    const auto without_table = scenario(plan, false);
    start.seed = 0x1602'0000'1234ULL;
    auto seeded = sim::generate_islands(without_table, fake.game, buildings(), start);
    start.draws = sim::WorldStart::Draws::Table;
    auto fallback = sim::generate_islands(without_table, fake.game, buildings(), start);
    REQUIRE(seeded.ok());
    REQUIRE(fallback.ok());
    CHECK(same_world(*seeded, *fallback));
    CHECK(same_world(*a, *seeded));  // the table of the first file was not used either
}

TEST_CASE("world generator: an island kept free of villages never gets one") {
    const auto& fake = full_game();
    // Four north islands whose templates all have a native version, three villages to place: the
    // three islands without the flag get them.
    assets::ScenarioPlan plan;
    plan.natives = {3, 0};
    for (std::uint8_t i = 0; i < 4; ++i) {
        plan.islands.push_back(island(i, 0, 1, 20 + 60U * i, 40));
        plan.islands.back().template_number = static_cast<std::uint16_t>(3 * i);
    }
    plan.islands[2].flags = sim::kPlanNoVillage;
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
        sim::WorldStart start;
        start.seed = seed;
        auto w = sim::generate_islands(scenario(plan), fake.game, buildings(), start);
        REQUIRE(w.ok());
        REQUIRE(w->islands.size() == 4);
        for (const auto& g : w->islands) {
            CHECK(g.natives == (g.number != 2));
        }
    }
    // Templates without a native version get no village, however many the plan asks for.
    for (auto& e : plan.islands) {
        e.template_number = static_cast<std::uint16_t>(e.template_number + 1);
        e.flags = 0;
    }
    auto w = sim::generate_islands(scenario(plan), fake.game, buildings(), sim::WorldStart{});
    REQUIRE(w.ok());
    for (const auto& g : w->islands) {
        CHECK(!g.natives);
    }
}

TEST_CASE("world generator: a missing template is reported and its island left out") {
    // Only the little northern templates exist.
    const FakeGame fake(Contents{{{0, 0}}, true, 0});
    assets::ScenarioPlan plan;
    plan.natives = {1, 1};
    add_resources(plan);
    plan.islands = {island(0, 0, 0, 20, 20), island(1, 1, 3, 100, 20), island(2, 0, 0, 200, 20)};
    std::vector<std::string> warnings;
    auto w = sim::generate_islands(scenario(plan), fake.game, buildings(), sim::WorldStart{}, &warnings);
    REQUIRE(w.ok());
    REQUIRE(w->islands.size() == 2);
    CHECK(w->islands[0].number == 0);
    CHECK(w->islands[1].number == 2);
    const bool reported = std::any_of(warnings.begin(), warnings.end(), [](const std::string& s) {
        return s.find("island template not found") != std::string::npos && s.find("Sued/big") != std::string::npos;
    });
    CHECK(reported);
    check_islands(*w, plan);
    // What is left loads as any scenario.
    std::vector<std::string> load_warnings;
    auto world = sim::load_world(w->file, buildings(), fake.game, &load_warnings);
    REQUIRE(world.ok());
    CHECK(world->islands().size() == 2);

    // A directory without a template of the plan's classes: no islands, each one reported.
    const FakeGame empty(Contents{{{1, 4}}, true, 0});
    warnings.clear();
    plan.islands = {island(0, 0, 0, 20, 20), island(1, 0, 1, 100, 20)};
    auto none = sim::generate_islands(scenario(plan), empty.game, buildings(), sim::WorldStart{}, &warnings);
    REQUIRE(none.ok());
    CHECK(none->islands.empty());
    CHECK(warnings.size() >= 2);
}

TEST_CASE("world generator: guarantee_resources places the crop the main passes left out") {
    // One northern island that may grow one crop; the plan asks for tobacco and sugar.
    const FakeGame fake(Contents{{{0, 1}}, false, 0});
    assets::ScenarioPlan plan;
    plan.max_crops = 1;
    plan.crops[0] = {kCrops[0], 1, 0};  // tobacco
    plan.crops[1] = {kCrops[2], 1, 0};  // sugar
    plan.islands = {island(0, 0, 1, 40, 40)};
    sim::WorldStart start;
    auto off = sim::generate_islands(scenario(plan), fake.game, buildings(), start);
    REQUIRE(off.ok());
    REQUIRE(off->islands.size() == 1);
    CHECK(crop_count(off->islands[0].fertility) == 1);
    CHECK(off->guaranteed == 0);

    start.guarantee_resources = true;
    auto on = sim::generate_islands(scenario(plan), fake.game, buildings(), start);
    REQUIRE(on.ok());
    REQUIRE(on->islands.size() == 1);
    CHECK(has_crop(on->islands[0].fertility, kTobacco));
    CHECK(has_crop(on->islands[0].fertility, kSugar));
    CHECK(crop_count(on->islands[0].fertility) == 2);
    CHECK(on->guaranteed == 1);
    CHECK(on->draws > off->draws);

    // Nothing to place where the main passes found everything a place.
    plan.max_crops = 2;
    auto full = sim::generate_islands(scenario(plan), fake.game, buildings(), start);
    REQUIRE(full.ok());
    CHECK(crop_count(full->islands[0].fertility) == 2);
    CHECK(full->guaranteed == 0);
}

TEST_CASE("world generator: units without a place count as lost; guarantee_resources places iron and gold") {
    // Little islands only: no pass of the original puts small iron, gold or a treasure there.
    const FakeGame fake(Contents{{{0, 0}, {0, 1}}, true, 0});
    assets::ScenarioPlan plan;
    plan.ores[0] = {kIronMine, 1, 1};
    plan.ores[1] = {kGoldMine, 1, 1};
    plan.treasures[0] = {kGoldsmith, 1, 0};
    plan.gold_min_size = 1;
    plan.gold_max_size = 4;
    plan.islands = {island(0, 0, 0, 20, 20), island(1, 0, 0, 100, 20), island(2, 0, 0, 200, 20)};
    sim::WorldStart start;
    auto off = sim::generate_islands(scenario(plan), fake.game, buildings(), start);
    REQUIRE(off.ok());
    CHECK(off->lost == 3);
    CHECK(off->guaranteed == 0);
    CHECK(deposits_of(*off, kIron) + deposits_of(*off, kGold) + deposits_of(*off, kTreasure) == 0);

    // The guarantee finds the iron a mountain; gold never goes to a little island.
    start.guarantee_resources = true;
    auto on = sim::generate_islands(scenario(plan), fake.game, buildings(), start);
    REQUIRE(on.ok());
    CHECK(on->lost == 3);  // the original's passes lost as much as before
    CHECK(on->guaranteed == 1);
    CHECK(deposits_of(*on, kIron) == 1);
    CHECK(deposits_of(*on, kGold) == 0);
    check_islands(*on, plan);

    // With a middle island but gold asked for on the largest class only: the main passes lose
    // it, the guarantee puts it on the middle island.
    plan.ores[0] = {};
    plan.treasures[0] = {};
    plan.gold_min_size = 4;
    plan.gold_max_size = 4;
    plan.islands.push_back(island(3, 0, 1, 300, 20));
    start.guarantee_resources = false;
    auto lost_gold = sim::generate_islands(scenario(plan), fake.game, buildings(), start);
    REQUIRE(lost_gold.ok());
    CHECK(lost_gold->lost == 1);
    CHECK(deposits_of(*lost_gold, kGold) == 0);
    start.guarantee_resources = true;
    auto gold = sim::generate_islands(scenario(plan), fake.game, buildings(), start);
    REQUIRE(gold.ok());
    CHECK(gold->guaranteed == 1);
    REQUIRE(deposits_of(*gold, kGold) == 1);
    for (const auto& g : gold->islands) {
        for (const auto& d : g.deposits) {
            CHECK(g.number == 3);
            CHECK(d.ware == kGold);
        }
    }
    check_islands(*gold, plan);
}

TEST_CASE("world generator: the islands load with load_world and pass validate") {
    const auto& fake = full_game();
    const auto plan = standard_plan();
    const auto file = scenario(plan);
    for (const auto draws : {sim::WorldStart::Draws::Table, sim::WorldStart::Draws::Seeded}) {
        sim::WorldStart start;
        start.draws = draws;
        start.seed = 77;
        start.guarantee_resources = draws == sim::WorldStart::Draws::Seeded;
        auto generated = sim::generate_islands(file, fake.game, buildings(), start);
        REQUIRE(generated.ok());
        std::vector<std::string> warnings;
        auto world = sim::load_world(generated->file, buildings(), fake.game, &warnings, start);
        REQUIRE(world.ok());
        CHECK(warnings.empty());
        CHECK(sim::validate(*world).ok());
        REQUIRE(world->islands().size() == generated->islands.size());
        for (std::size_t i = 0; i < generated->islands.size(); ++i) {
            const sim::WorldIsland& wi = world->islands()[i];
            const sim::GeneratedIsland& g = generated->islands[i];
            CHECK(wi.header.number == g.number);
            CHECK(wi.map.width() == side(g.size));
            CHECK(wi.header.fertility == g.fertility);
            CHECK(wi.header.deposits.size() == g.deposits.size());
        }
        // load_world makes the islands itself from the plan: the same world.
        auto direct = sim::load_world(file, buildings(), fake.game, nullptr, start);
        REQUIRE(direct.ok());
        REQUIRE(direct->islands().size() == world->islands().size());
        for (std::size_t i = 0; i < world->islands().size(); ++i) {
            CHECK(direct->islands()[i].header.raw == world->islands()[i].header.raw);
            CHECK(direct->islands()[i].x == world->islands()[i].x);
            CHECK(direct->islands()[i].y == world->islands()[i].y);
        }
    }
}

TEST_CASE("world generator: a plan written into a SZENE chunk reads back, its other bytes stay") {
    assets::ScenarioPlan plan = standard_plan();
    std::vector<std::uint8_t> base = encode_plan(plan);
    base[0x10] = 0xAB;  // a byte the plan does not hold
    plan.islands[3].x = 333;
    plan.islands.pop_back();
    plan.treasures[1] = {kGoldsmith, 4, 0};
    const std::vector<std::uint8_t> chunk = assets::write_scenario_plan(base, plan);
    CHECK(chunk.size() == 0x494);
    CHECK(chunk[0x10] == 0xAB);
    CHECK(chunk[0x174 + 16 * 15] == 0);  // the island dropped from the list is gone
    std::vector<std::uint8_t> bytes;
    o1602::test::put_chunk(bytes, "SZENE", chunk);
    auto file = assets::ChunkFile::from_bytes(bytes);
    REQUIRE(file.ok());
    auto read = assets::load_scenario_plan(*file);
    REQUIRE(read.ok());
    REQUIRE(read->has_value());
    CHECK(**read == plan);
    CHECK(assets::write_scenario_plan({}, plan).size() == 0x494);
}

TEST_CASE("world generator: a plan kept for the editor is saved and its islands are made at the start") {
    const auto& fake = full_game();
    const auto file = scenario(standard_plan());
    auto planned_plan = assets::load_scenario_plan(file);
    REQUIRE(planned_plan.ok());
    sim::WorldStart start;
    start.seed = 77;
    start.keep_plan = true;
    std::vector<std::string> warnings;
    auto kept = sim::load_world(file, buildings(), fake.game, &warnings, start);
    REQUIRE(kept.ok());
    CHECK(kept->islands().empty());
    REQUIRE(kept->planned_islands().has_value());
    CHECK(kept->planned_islands()->plan == **planned_plan);
    CHECK(kept->planned_islands()->table == assets::load_random_table(file));
    CHECK(sim::check_planned_islands(*kept->planned_islands(), kept->width(), kept->height()).empty());

    // PLAN keeps it in the own savegame, the same bytes again after reading.
    sim::ScenarioInfo info;
    info.names = {{"en", "Atoll"}};
    kept->set_scenario_info(info);
    kept->set_markers({{"south", sim::ScenarioMarker::Shape::Area, 0, 300, 500, 50}});
    auto bytes = sim::write_game(*kept, buildings());
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, buildings());
    REQUIRE(loaded.ok());
    CHECK(loaded->planned_islands() == kept->planned_islands());
    auto again = sim::write_game(*loaded, buildings());
    REQUIRE(again.ok());
    CHECK(*again == *bytes);

    // The start makes the islands as the original's file would have them, and keeps the scenario's parts.
    start.keep_plan = false;
    auto direct = sim::load_world(file, buildings(), fake.game, nullptr, start);
    REQUIRE(direct.ok());
    auto rolled = sim::roll_planned_islands(*loaded, fake.game, buildings(), start);
    REQUIRE(rolled.ok());
    CHECK(!rolled->planned_islands().has_value());
    REQUIRE(rolled->islands().size() == direct->islands().size());
    for (std::size_t i = 0; i < direct->islands().size(); ++i) {
        CHECK(rolled->islands()[i].header.raw == direct->islands()[i].header.raw);
        CHECK(rolled->islands()[i].x == direct->islands()[i].x);
    }
    CHECK(rolled->scenario_info() == info);
    CHECK(rolled->markers() == kept->markers());
    CHECK(sim::validate(*rolled).ok());

    // A plan changed in the editor: the island lies where the plan now says (open sea there).
    sim::PlannedIslands moved = *loaded->planned_islands();
    moved.plan.islands[0].x = 450;
    moved.plan.islands[0].y = 230;
    loaded->set_planned_islands(moved);
    auto changed = sim::roll_planned_islands(*loaded, fake.game, buildings(), start);
    REQUIRE(changed.ok());
    const auto at = std::find_if(changed->islands().begin(), changed->islands().end(), [&](const sim::WorldIsland& w) {
        return w.header.number == moved.plan.islands[0].number;
    });
    REQUIRE(at != changed->islands().end());
    CHECK(at->x == 450);
    CHECK(at->y == 230);

    // Without a plan, or with an island outside the world, nothing is made.
    sim::World plain(500, 350);
    CHECK(!sim::roll_planned_islands(plain, fake.game, buildings(), start).ok());
    moved.plan.islands[0].x = 500;
    loaded->set_planned_islands(moved);
    CHECK(!sim::roll_planned_islands(*loaded, fake.game, buildings(), start).ok());
}

TEST_CASE("world generator: a world plan names what is wrong with it") {
    sim::PlannedIslands planned;
    planned.plan.islands = {island(1, 0, 0, 10, 10), island(1, 3, 5, 600, 10)};
    const auto problems = sim::check_planned_islands(planned, 500, 350);
    REQUIRE(problems.size() == 4);
    CHECK(problems[0] == "island 1 twice");
    CHECK(problems[1] == "island 1: no climate 3");
    CHECK(problems[2] == "island 1: no size class 5");
    CHECK(problems[3] == "island 1: outside the world");
    planned.plan.islands.assign(51, island(0, 0, 0, 0, 0));
    for (std::size_t i = 0; i < planned.plan.islands.size(); ++i) {
        planned.plan.islands[i].number = static_cast<std::uint8_t>(i);
    }
    REQUIRE(sim::check_planned_islands(planned, 500, 350).size() == 1);
    CHECK(sim::check_planned_islands(planned, 500, 350)[0] == "more than 50 islands");
}

TEST_CASE("world generator: an own world without middle islands puts treasures on big islands, no home") {
    const auto& fake = full_game();
    const auto templates = sim::island_templates(fake.game, buildings());
    REQUIRE(!templates.empty());
    sim::WorldSettings settings;
    settings.sizes = sim::WorldSettings::Sizes::Large;
    settings.seed = 3;
    auto plan = sim::plan_world(settings, templates);
    REQUIRE(plan.ok());
    std::set<std::uint8_t> homes;
    for (const sim::StartArea& st : plan->starts) {
        homes.insert(st.home_island);
    }
    // Only the little and the big islands stay.
    std::erase_if(plan->plan.islands, [](const auto& e) { return e.size == 1 || e.size == 2; });
    REQUIRE(std::count_if(plan->plan.islands.begin(), plan->plan.islands.end(),
                          [&](const auto& e) { return e.size >= 3 && !homes.contains(e.number); }) > 0);
    plan->plan.treasures = {};
    plan->plan.treasures[0] = {kGoldsmith, 2, 0};
    sim::WorldStart start;
    start.seed = 3;
    auto w = sim::generate_islands(scenario(standard_plan()), *plan, fake.game, buildings(), start);
    REQUIRE(w.ok());
    CHECK(deposits_of(*w, kTreasure) > 0);
    for (const auto& g : w->islands) {
        if (std::any_of(g.deposits.begin(), g.deposits.end(), [](const auto& d) { return d.ware == kTreasure; })) {
            CHECK(g.size >= 3);
            CHECK(!homes.contains(g.number));
        }
    }

    // The original's plans keep to the middle islands: the same islands give no treasure.
    assets::ScenarioPlan original = plan->plan;
    for (auto& e : original.islands) {
        e.template_number = 0xFFFF;
    }
    auto o = sim::generate_islands(scenario(original), fake.game, buildings(), sim::WorldStart{});
    REQUIRE(o.ok());
    CHECK(deposits_of(*o, kTreasure) == 0);
}

TEST_CASE("world generator: an own world gives every start home iron, all crops and gold within reach") {
    const auto& fake = full_game();
    const auto templates = sim::island_templates(fake.game, buildings());
    REQUIRE(!templates.empty());
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        sim::WorldSettings settings;
        settings.seed = seed;
        std::vector<std::string> warnings;
        auto plan = sim::plan_world(settings, templates, &warnings);
        REQUIRE(plan.ok());
        REQUIRE(!plan->shared_start);
        // The scenario's own plan is replaced by the world plan.
        const auto file = scenario(standard_plan());
        sim::WorldStart start;
        start.seed = seed;
        auto w = sim::generate_islands(file, *plan, fake.game, buildings(), start, &warnings);
        REQUIRE(w.ok());
        CHECK(warnings.empty());
        REQUIRE(w->islands.size() == plan->plan.islands.size());
        check_islands(*w, plan->plan, true);
        std::map<std::uint8_t, assets::ScenarioPlan::Island> planned;
        for (const auto& e : plan->plan.islands) {
            planned[e.number] = e;
        }
        for (const auto& g : w->islands) {
            CHECK(g.template_number == planned[g.number].template_number);
        }
        for (const sim::StartArea& st : plan->starts) {
            const auto home = std::find_if(w->islands.begin(), w->islands.end(),
                                           [&](const sim::GeneratedIsland& g) { return g.number == st.home_island; });
            REQUIRE(home != w->islands.end());
            CHECK(!home->natives);
            CHECK(std::any_of(home->deposits.begin(), home->deposits.end(),
                              [](const auto& d) { return d.ware == kIron; }));
            std::uint32_t crops = 0;
            bool gold = false;
            for (const auto& g : w->islands) {
                const auto& e = planned[g.number];
                const std::int64_t dx = std::int64_t{e.x} + side(g.size) / 2 - st.x;
                const std::int64_t dy = std::int64_t{e.y} + side(g.size) / 2 - st.y;
                if (dx * dx + dy * dy > std::int64_t{st.reach} * st.reach && g.number != st.home_island) {
                    continue;
                }
                crops |= g.fertility;
                gold = gold ||
                       std::any_of(g.deposits.begin(), g.deposits.end(), [](const auto& d) { return d.ware == kGold; });
            }
            CHECK(crop_count(crops) == 6);
            CHECK(gold);
        }
        // The same seed, the same world; and it loads.
        auto again = sim::generate_islands(file, *plan, fake.game, buildings(), start);
        REQUIRE(again.ok());
        CHECK(same_world(*w, *again));
        auto world = sim::load_world(w->file, buildings(), fake.game, &warnings, start);
        REQUIRE(world.ok());
        CHECK(warnings.empty());
        CHECK(world->islands().size() == w->islands.size());
        CHECK(sim::validate(*world).ok());
    }
}

TEST_CASE("world generator: islands of their own shape instead of templates, with villages of the native templates") {
    const auto& fake = full_game();
    const auto templates = sim::island_templates(fake.game, buildings());
    REQUIRE(!templates.empty());
    for (const auto shapes : {sim::WorldSettings::Shapes::Own, sim::WorldSettings::Shapes::Mixed}) {
        std::size_t own = 0;
        std::size_t kept = 0;
        std::size_t volcanoes = 0;
        std::size_t villages = 0;
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            sim::WorldSettings settings;
            settings.seed = seed;
            settings.shapes = shapes;
            std::vector<std::string> warnings;
            auto plan = sim::plan_world(settings, templates, &warnings);
            REQUIRE(plan.ok());
            CHECK(plan->shapes == shapes);
            sim::WorldStart start;
            start.seed = seed;
            const auto file = scenario(standard_plan());
            auto w = sim::generate_islands(file, *plan, fake.game, buildings(), start, &warnings);
            REQUIRE(w.ok());
            CHECK(warnings.empty());
            REQUIRE(w->islands.size() == plan->plan.islands.size());
            check_islands(*w, plan->plan, true);
            for (const auto& g : w->islands) {
                const bool shaped = g.template_number == assets::IslandHeader::kNoTemplate;
                if (shapes == sim::WorldSettings::Shapes::Mixed) {
                    CHECK(!(shaped && g.natives));  // a village keeps its template
                }
                if (shapes == sim::WorldSettings::Shapes::Own) {
                    // Free sizes: every island of its own shape; the villages the plan asked for
                    // are moved onto them from native templates.
                    const auto planned =
                        std::find_if(plan->plan.islands.begin(), plan->plan.islands.end(),
                                     [&](const assets::ScenarioPlan::Island& p) { return p.number == g.number; });
                    REQUIRE(planned != plan->plan.islands.end());
                    const auto own_island = std::find_if(plan->own_islands.begin(), plan->own_islands.end(),
                                                         [&](const auto& o) { return o.number == g.number; });
                    REQUIRE(own_island != plan->own_islands.end());
                    CHECK(g.natives == own_island->village);
                    villages += g.natives ? 1U : 0U;
                    CHECK(shaped == ((planned->flags & sim::kPlanOwnShape) != 0));
                }
                own += shaped ? 1U : 0U;
                kept += shaped ? 0U : 1U;
            }
            // Every start still has iron at home, all crops and gold within reach.
            for (const sim::StartArea& st : plan->starts) {
                const auto home =
                    std::find_if(w->islands.begin(), w->islands.end(),
                                 [&](const sim::GeneratedIsland& g) { return g.number == st.home_island; });
                REQUIRE(home != w->islands.end());
                CHECK(std::any_of(home->deposits.begin(), home->deposits.end(),
                                  [](const auto& d) { return d.ware == kIron; }));
            }
            // The same seed, the same world; the islands load with all their tiles.
            auto again = sim::generate_islands(file, *plan, fake.game, buildings(), start);
            REQUIRE(again.ok());
            CHECK(same_world(*w, *again));
            auto world = sim::load_world(w->file, buildings(), fake.game, &warnings, start);
            REQUIRE(world.ok());
            CHECK(warnings.empty());
            CHECK(world->islands().size() == w->islands.size());
            CHECK(sim::validate(*world).ok());
            for (const sim::WorldIsland& island : world->islands()) {
                if (island.header.template_number == assets::IslandHeader::kNoTemplate) {
                    CHECK(island.header.stored_complete);
                    // A volcano where the plan asked for one (or the template had one), else none.
                    const auto own_island =
                        std::find_if(plan->own_islands.begin(), plan->own_islands.end(),
                                     [&](const auto& o) { return o.number == island.header.number; });
                    if (own_island != plan->own_islands.end()) {
                        CHECK(island.craters.size() == (own_island->volcano ? 1U : 0U));
                    }
                    volcanoes += island.craters.size();
                    if (own_island != plan->own_islands.end() && own_island->village) {
                        // The village's block of 6 x 5 tiles moved onto the island, on land; its
                        // record moved with it.
                        std::uint32_t x0 = island.map.width();
                        std::uint32_t y0 = island.map.height();
                        std::size_t count = 0;
                        for (std::uint32_t y = 0; y < island.map.height(); ++y) {
                            for (std::uint32_t x = 0; x < island.map.width(); ++x) {
                                const assets::BuildingType* b = buildings().by_sprite(island.map.top(x, y).sprite);
                                if (b != nullptr && b->id == 20533) {
                                    x0 = std::min(x0, x);
                                    y0 = std::min(y0, y);
                                    ++count;
                                }
                            }
                        }
                        CHECK(count == 30);
                        bool moved = false;
                        for (const assets::Chunk& c : w->file.chunks()) {
                            if (c.name != "ROHWACHS2") {
                                continue;
                            }
                            const auto data = w->file.payload(c);
                            for (std::size_t o = 0; o + 8 <= data.size(); o += 8) {
                                moved = moved || (data[o] == island.header.number && data[o + 1] == x0 + 1 &&
                                                  data[o + 2] == y0 + 1);
                            }
                        }
                        CHECK(moved);
                    }
                    std::size_t sea = 0;
                    for (std::uint32_t y = 0; y < island.map.height(); ++y) {
                        for (std::uint32_t x = 0; x < island.map.width(); ++x) {
                            const assets::BuildingType* b = buildings().by_sprite(island.map.ground(x, y).sprite);
                            REQUIRE(b != nullptr);
                            sea += b->kind == 19 || b->kind == 21 || b->kind == 22 ? 1U : 0U;
                        }
                    }
                    CHECK(sea > 0);
                    CHECK(sea < std::size_t{island.map.width()} * island.map.height());
                }
            }
        }
        CHECK(own > 0);
        if (shapes == sim::WorldSettings::Shapes::Mixed) {
            CHECK(kept > 0);
        } else {
            CHECK(volcanoes > 0);  // the plan's islands of their own shape have volcanoes
            CHECK(villages > 0);   // and native villages
        }
    }
}

TEST_CASE("world generator: a fixed number of gold islands, the starts' first") {
    const auto& fake = full_game();
    const auto templates = sim::island_templates(fake.game, buildings());
    REQUIRE(!templates.empty());
    const auto has_gold = [](const sim::GeneratedIsland& g) {
        return std::any_of(g.deposits.begin(), g.deposits.end(), [](const auto& d) { return d.ware == kGold; });
    };
    for (const std::uint8_t wanted : std::array<std::uint8_t, 4>{1, 2, 4, 7}) {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            sim::WorldSettings settings;
            settings.seed = seed;
            settings.gold_islands = wanted;
            std::vector<std::string> warnings;
            auto plan = sim::plan_world(settings, templates, &warnings);
            REQUIRE(plan.ok());
            sim::WorldStart start;
            start.seed = seed;
            auto w = sim::generate_islands(scenario(standard_plan()), *plan, fake.game, buildings(), start, &warnings);
            REQUIRE(w.ok());
            CHECK(warnings.empty());
            std::map<std::uint8_t, assets::ScenarioPlan::Island> planned;
            for (const auto& e : plan->plan.islands) {
                planned[e.number] = e;
            }
            std::size_t gold_islands = 0;
            for (const auto& g : w->islands) {
                const auto veins =
                    std::count_if(g.deposits.begin(), g.deposits.end(), [](const auto& d) { return d.ware == kGold; });
                CHECK(veins <= 1);  // one vein each
                gold_islands += veins == 1 ? 1U : 0U;
            }
            CHECK(gold_islands == wanted);
            // As many starts as the number lasts have gold within reach.
            std::size_t starts_with_gold = 0;
            for (const sim::StartArea& st : plan->starts) {
                bool gold = false;
                for (const auto& g : w->islands) {
                    const auto& e = planned[g.number];
                    const std::int64_t dx = std::int64_t{e.x} + side(g.size) / 2 - st.x;
                    const std::int64_t dy = std::int64_t{e.y} + side(g.size) / 2 - st.y;
                    const bool near =
                        dx * dx + dy * dy <= std::int64_t{st.reach} * st.reach || g.number == st.home_island;
                    gold = gold || (near && has_gold(g));
                }
                starts_with_gold += gold ? 1U : 0U;
            }
            CHECK(starts_with_gold >= std::min<std::size_t>(wanted, plan->starts.size()));
            // The same seed, the same world.
            auto again = sim::generate_islands(scenario(standard_plan()), *plan, fake.game, buildings(), start);
            REQUIRE(again.ok());
            CHECK(same_world(*w, *again));
        }
    }

    // All ships at one place: the number holds as well.
    sim::WorldSettings together;
    together.seed = 5;
    together.starts = sim::WorldSettings::Starts::Together;
    together.gold_islands = 5;
    auto plan = sim::plan_world(together, templates);
    REQUIRE(plan.ok());
    auto w = sim::generate_islands(scenario(standard_plan()), *plan, fake.game, buildings(), {});
    REQUIRE(w.ok());
    CHECK(std::count_if(w->islands.begin(), w->islands.end(), has_gold) == 5);
}

TEST_CASE("world generator: more gold islands than free mountains are reported, not invented") {
    const auto& fake = full_game();
    const auto templates = sim::island_templates(fake.game, buildings());
    sim::WorldSettings settings;
    settings.seed = 2;
    settings.gold_islands = sim::WorldSettings::kMaxGoldIslands;
    auto plan = sim::plan_world(settings, templates);
    REQUIRE(plan.ok());
    std::vector<std::string> warnings;
    auto w = sim::generate_islands(scenario(standard_plan()), *plan, fake.game, buildings(), {}, &warnings);
    REQUIRE(w.ok());
    CHECK(std::any_of(warnings.begin(), warnings.end(),
                      [](const std::string& t) { return t.find("gold islands") != std::string::npos; }));
    std::size_t gold_islands = 0;
    for (const auto& g : w->islands) {
        gold_islands +=
            std::any_of(g.deposits.begin(), g.deposits.end(), [](const auto& d) { return d.ware == kGold; }) ? 1U : 0U;
    }
    CHECK(gold_islands > 0);
    CHECK(gold_islands <= w->islands.size());
}

TEST_CASE("world generator: large islands of their own size get veins by their size, all over them") {
    const auto& fake = full_game();
    const auto templates = sim::island_templates(fake.game, buildings());
    REQUIRE(!templates.empty());
    sim::WorldSettings settings;
    settings.seed = 5;
    settings.shapes = sim::WorldSettings::Shapes::Own;
    settings.landscape.land = 25;
    settings.landscape.min_side = 120;
    settings.landscape.max_side = 160;  // within the scenario's 500 x 350
    std::vector<std::string> warnings;
    auto plan = sim::plan_world(settings, templates, &warnings);
    REQUIRE(plan.ok());
    sim::WorldStart start;
    start.seed = 5;
    auto w = sim::generate_islands(scenario(standard_plan()), *plan, fake.game, buildings(), start, &warnings);
    REQUIRE(w.ok());
    check_islands(*w, plan->plan, true);
    std::size_t most = 0;
    bool south = false;
    for (const auto& g : w->islands) {
        most = std::max(most, g.deposits.size());
        if (g.deposits.size() >= 4) {
            // Not only the first mountains from the north.
            south = south || std::any_of(g.deposits.begin(), g.deposits.end(),
                                         [&](const auto& d) { return d.y > g.height / 2; });
        }
    }
    CHECK(most > 2);
    CHECK(south);
    // They load with all their deposits (Open1602's chunk holds those beyond the record's two),
    // and the original's savegame cannot take them.
    warnings.clear();
    auto world = sim::load_world(w->file, buildings(), fake.game, &warnings, start);
    CHECK(warnings.empty());
    REQUIRE(world.ok());
    REQUIRE(world->islands().size() == w->islands.size());
    for (std::size_t i = 0; i < w->islands.size(); ++i) {
        const auto& loaded = world->islands()[i].header.deposits;
        const auto& made = w->islands[i].deposits;
        REQUIRE(loaded.size() == made.size());
        for (std::size_t k = 0; k < made.size(); ++k) {
            CHECK(loaded[k].ware == made[k].ware);
            CHECK(loaded[k].x == made[k].x);
            CHECK(loaded[k].y == made[k].y);
            CHECK(loaded[k].kind == made[k].kind);
            CHECK(loaded[k].amount == made[k].amount);
        }
    }
    CHECK(sim::validate(*world).ok());
}

TEST_CASE("world generator: a native village on an island larger than the original's, within its first 255 tiles") {
    const auto& fake = full_game();
    const auto templates = sim::island_templates(fake.game, buildings());
    sim::WorldSettings settings;
    settings.seed = 5;
    settings.shapes = sim::WorldSettings::Shapes::Own;
    settings.natives = sim::WorldSettings::Amount::Many;
    settings.landscape.min_side = 260;
    settings.landscape.max_side = 300;
    settings.landscape.land = 30;
    std::vector<std::string> warnings;
    auto plan = sim::plan_world(sim::fit_landscape(settings), templates, &warnings);
    REQUIRE(plan.ok());
    sim::WorldStart start;
    start.seed = 5;
    // The scenario of the world's size, as a new game makes it.
    auto own = sim::own_world_scenario(scenario(standard_plan()), *plan, settings.players);
    REQUIRE(own.ok());
    auto w = sim::generate_islands(*own, *plan, fake.game, buildings(), start, &warnings);
    REQUIRE(w.ok());
    auto world = sim::load_world(w->file, buildings(), fake.game, &warnings, start);
    REQUIRE(world.ok());
    std::size_t villages = 0;
    for (const sim::WorldIsland& island : world->islands()) {
        if (island.map.width() <= 255 && island.map.height() <= 255) {
            continue;
        }
        for (std::uint32_t y = 0; y < island.map.height(); ++y) {
            for (std::uint32_t x = 0; x < island.map.width(); ++x) {
                const assets::BuildingType* b = buildings().by_sprite(island.map.top(x, y).sprite);
                if (b != nullptr && b->id == 20533) {
                    CHECK(x < 255);
                    CHECK(y < 255);
                    ++villages;
                }
            }
        }
    }
    CHECK(villages >= 30);  // one village of 6 x 5 tiles at least
    CHECK(std::none_of(warnings.begin(), warnings.end(),
                       [](const std::string& text) { return text.find("no room for a native village") != text.npos; }));
}
