// SPDX-License-Identifier: GPL-3.0-or-later
// Own worlds: the plan of islands and start areas.

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>

#include "../test_framework.hpp"

#include "o1602/sim/new_game.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/world_plan.hpp"

using namespace o1602;

namespace {

constexpr std::array<std::uint32_t, 5> kWidth = {30, 40, 50, 70, 100};
constexpr std::array<std::uint32_t, 5> kHeight = {30, 40, 52, 60, 90};

/// A catalogue like the game's: 20 templates per climate and class, every fifth with a volcano,
/// every third with a village, the big ones with ore mountains.
std::vector<sim::TemplateInfo> catalogue() {
    std::vector<sim::TemplateInfo> out;
    for (std::uint8_t climate = 0; climate < 2; ++climate) {
        for (std::uint8_t size = 0; size < 5; ++size) {
            for (std::uint16_t n = 0; n < 20; ++n) {
                sim::TemplateInfo t;
                t.climate = climate;
                t.size = size;
                t.number = n;
                t.width = static_cast<std::uint16_t>(kWidth[size]);
                t.height = static_cast<std::uint16_t>(kHeight[size]);
                t.volcano = n % 5 == 4;
                t.village = n % 3 == 0;
                t.big_mountains = size >= 2 ? 1 : 0;
                t.small_mountains = 2;
                out.push_back(t);
            }
        }
    }
    return out;
}

const std::vector<sim::TemplateInfo>& templates() {
    static const std::vector<sim::TemplateInfo> t = catalogue();
    return t;
}

struct Box {
    std::int64_t x, y, w, h;
};

Box box(const assets::ScenarioPlan::Island& i) {
    return Box{i.x, i.y, kWidth[i.size], kHeight[i.size]};
}

/// Checks every rule of a plan; returns the number of islands.
std::size_t check_plan(const sim::WorldPlan& p, const sim::WorldSettings& s) {
    CHECK(p.width == s.width);
    CHECK(p.height == s.height);
    REQUIRE(p.starts.size() == s.players);
    const auto& islands = p.plan.islands;
    std::set<std::tuple<int, int, int>> templates_used;
    std::map<std::pair<int, int>, std::size_t> per_class;
    for (std::size_t a = 0; a < islands.size(); ++a) {
        const Box ra = box(islands[a]);
        CHECK(islands[a].number == a);
        CHECK(islands[a].climate < 2);
        CHECK(ra.x >= 20);
        CHECK(ra.y >= 20);
        CHECK(ra.x + ra.w <= s.width - 20);
        CHECK(ra.y + ra.h <= s.height - 20);
        for (std::size_t b = a + 1; b < islands.size(); ++b) {
            const Box rb = box(islands[b]);
            const bool apart = ra.x + ra.w + 8 <= rb.x || rb.x + rb.w + 8 <= ra.x || ra.y + ra.h + 8 <= rb.y ||
                               rb.y + rb.h + 8 <= ra.y;
            CHECK(apart);
        }
        for (const sim::StartArea& st : p.starts) {
            const std::int64_t dx = std::max<std::int64_t>({ra.x - st.x, 0, st.x - (ra.x + ra.w - 1)});
            const std::int64_t dy = std::max<std::int64_t>({ra.y - st.y, 0, st.y - (ra.y + ra.h - 1)});
            CHECK(std::max(dx, dy) >= 5);
        }
        templates_used.insert({islands[a].climate, islands[a].size, islands[a].template_number});
        ++per_class[{islands[a].climate, islands[a].size}];
    }
    // Templates differ while the catalogue has enough: 20 per class here, 16 without a volcano.
    std::size_t expected = 0;
    for (const auto& [cls, count] : per_class) {
        expected += std::min<std::size_t>(count, 16);
    }
    CHECK(templates_used.size() >= expected);
    if (!p.shared_start) {
        for (const sim::StartArea& st : p.starts) {
            REQUIRE(st.home_island < islands.size());
            const auto& home = islands[st.home_island];
            CHECK((home.flags & sim::kPlanNoVillage) != 0);
            CHECK(home.size >= 2);
            std::array<int, 2> climates{};
            for (const auto& i : islands) {
                const Box r = box(i);
                const std::int64_t dx = r.x + r.w / 2 - st.x;
                const std::int64_t dy = r.y + r.h / 2 - st.y;
                if (dx * dx + dy * dy <= std::int64_t{st.reach} * st.reach) {
                    ++climates[i.climate];
                }
            }
            CHECK(climates[0] >= 1);  // both climates within reach of every start
            CHECK(climates[1] >= 1);
        }
    }
    return islands.size();
}

}  // namespace

TEST_CASE("world plan: the original's size and four players: about 18 islands, a home each, fair starts") {
    sim::WorldSettings s;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        s.seed = seed;
        std::vector<std::string> warnings;
        auto plan = sim::plan_world(s, templates(), &warnings);
        REQUIRE(plan.ok());
        CHECK(warnings.empty());
        const std::size_t n = check_plan(*plan, s);
        CHECK(n >= 17);
        CHECK(n <= 22);
        // Villages, ore, crops and treasures as the original's plans for 18 islands.
        CHECK(plan->plan.natives[0] + plan->plan.natives[1] == (n * 4 + 9) / 18);
        CHECK(plan->plan.ores[0].building == 22401);
        CHECK(plan->plan.ores[2].building == 22405);
        CHECK(plan->plan.ores[0].count >= 6);
        CHECK(plan->plan.treasures[0].count >= 10);
    }
}

TEST_CASE("world plan: the same settings and seed give the same plan, another seed another") {
    sim::WorldSettings s;
    s.seed = 77;
    auto a = sim::plan_world(s, templates());
    auto b = sim::plan_world(s, templates());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(a->plan.islands.size() == b->plan.islands.size());
    for (std::size_t i = 0; i < a->plan.islands.size(); ++i) {
        CHECK(a->plan.islands[i].x == b->plan.islands[i].x);
        CHECK(a->plan.islands[i].y == b->plan.islands[i].y);
        CHECK(a->plan.islands[i].template_number == b->plan.islands[i].template_number);
        CHECK(a->plan.islands[i].climate == b->plan.islands[i].climate);
    }
    s.seed = 78;
    auto c = sim::plan_world(s, templates());
    REQUIRE(c.ok());
    bool differs = c->plan.islands.size() != a->plan.islands.size();
    for (std::size_t i = 0; !differs && i < a->plan.islands.size(); ++i) {
        differs = a->plan.islands[i].x != c->plan.islands[i].x || a->plan.islands[i].y != c->plan.islands[i].y;
    }
    CHECK(differs);
}

TEST_CASE("world plan: any size from 200 to 4000 tiles and up to sim::kMaxPlayers players") {
    struct Case {
        std::uint32_t w, h;
        std::uint8_t players;
    };
    for (const Case c : {Case{200, 200, 1}, Case{300, 250, 2}, Case{500, 350, 3}, Case{800, 600, 6}, Case{1000, 700, 8},
                         Case{2000, 2000, 12}, Case{2000, 300, 4}, Case{4000, 4000, 12}}) {
        sim::WorldSettings s;
        s.width = c.w;
        s.height = c.h;
        s.players = c.players;
        s.seed = c.w + c.players;
        auto plan = sim::plan_world(s, templates());
        REQUIRE(plan.ok());
        const std::size_t n = check_plan(*plan, s);
        CHECK(n >= std::size_t{c.players} + 2);
    }
    sim::WorldSettings bad;
    bad.width = 150;
    CHECK(!sim::plan_world(bad, templates()).ok());
    bad.width = 500;
    bad.players = 0;
    CHECK(!sim::plan_world(bad, templates()).ok());
    bad.players = static_cast<std::uint8_t>(sim::kMaxPlayers + 1);  // add_players would not place them (LC-1)
    CHECK(!sim::plan_world(bad, templates()).ok());
    CHECK(!sim::plan_world(sim::WorldSettings{}, {}).ok());
}

TEST_CASE("world plan: more than four players get as much sea and land each as the original's four") {
    for (const std::uint8_t players : {std::uint8_t{1}, std::uint8_t{4}}) {
        const sim::WorldSettings s = sim::settings_for_players(players);
        CHECK(s.players == players);
        CHECK(s.width == 500);
        CHECK(s.height == 350);
    }
    const sim::WorldSettings eight = sim::settings_for_players(8);
    CHECK(eight.players == 8);
    CHECK(eight.width == 707);  // 500 * sqrt(2)
    CHECK(eight.height == 495);
    const sim::WorldSettings sixteen = sim::settings_for_players(16);
    CHECK(sixteen.width == 1000);
    CHECK(sixteen.height == 700);
    // The islands follow the area: about twice as many for twice the players.
    sim::WorldSettings four = sim::settings_for_players(4);
    auto few = sim::plan_world(four, templates());
    auto many = sim::plan_world(eight, templates());
    REQUIRE(few.ok());
    REQUIRE(many.ok());
    CHECK(many->plan.islands.size() > few->plan.islands.size() * 3 / 2);
}

TEST_CASE("world plan: settings change the amount of islands, their sizes, villages and volcanoes") {
    const auto plan_with = [](auto&& change) {
        sim::WorldSettings s;
        s.width = 1000;
        s.height = 700;
        s.seed = 5;
        change(s);
        auto p = sim::plan_world(s, templates());
        REQUIRE(p.ok());
        check_plan(*p, s);
        return std::move(p).value();
    };
    using Amount = sim::WorldSettings::Amount;
    const auto few = plan_with([](sim::WorldSettings& s) { s.islands = Amount::Few; });
    const auto many = plan_with([](sim::WorldSettings& s) { s.islands = Amount::Many; });
    CHECK(many.plan.islands.size() > few.plan.islands.size() * 3 / 2);
    const auto average_size = [](const sim::WorldPlan& p) {
        std::uint32_t sum = 0;
        for (const auto& i : p.plan.islands) {
            sum += i.size;
        }
        return sum * 100 / static_cast<std::uint32_t>(p.plan.islands.size());
    };
    const auto small = plan_with([](sim::WorldSettings& s) { s.sizes = sim::WorldSettings::Sizes::Small; });
    const auto large = plan_with([](sim::WorldSettings& s) { s.sizes = sim::WorldSettings::Sizes::Large; });
    CHECK(average_size(large) > average_size(small) + 60);
    const auto no_villages = plan_with([](sim::WorldSettings& s) { s.natives = Amount::None; });
    CHECK(no_villages.plan.natives[0] + no_villages.plan.natives[1] == 0);
    // Volcano islands: none at all, or at most one per player.
    const auto volcano_islands = [](const sim::WorldPlan& p) {
        std::size_t n = 0;
        for (const auto& i : p.plan.islands) {
            n += i.template_number % 5 == 4 ? 1U : 0U;
        }
        return n;
    };
    CHECK(volcano_islands(plan_with([](sim::WorldSettings& s) { s.volcanoes = Amount::None; })) == 0);
    CHECK(volcano_islands(plan_with([](sim::WorldSettings&) {})) <= 4);
    // Homes have no volcano and a big ore mountain.
    for (const sim::StartArea& st : plan_with([](sim::WorldSettings&) {}).starts) {
        CHECK(plan_with([](sim::WorldSettings&) {}).plan.islands[st.home_island].template_number % 5 != 4);
    }
}

TEST_CASE("world plan: separate start areas go to the players in an order drawn from the seed") {
    // The first player (the human of a single-player game) must not always get the same side.
    for (const std::uint8_t players : {std::uint8_t{2}, std::uint8_t{4}, std::uint8_t{8}}) {
        sim::WorldSettings s;
        s.players = players;
        std::set<std::pair<std::uint32_t, std::uint32_t>> first;
        std::set<std::pair<std::uint32_t, std::uint32_t>> areas;
        for (std::uint64_t seed = 1; seed <= 40; ++seed) {
            s.seed = seed;
            auto plan = sim::plan_world(s, templates());
            REQUIRE(plan.ok());
            REQUIRE(plan->starts.size() == players);
            first.emplace(plan->starts[0].x, plan->starts[0].y);
            for (const sim::StartArea& st : plan->starts) {
                areas.emplace(st.x, st.y);
            }
        }
        // The areas are the same for every seed (the layout's geometry), only who gets which varies.
        CHECK(areas.size() == players);
        CHECK(first.size() == players);
    }
    // The same seed gives every computer the same order (multiplayer).
    sim::WorldSettings s;
    s.seed = 5;
    auto a = sim::plan_world(s, templates());
    auto b = sim::plan_world(s, templates());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    for (std::size_t p = 0; p < a->starts.size(); ++p) {
        CHECK(a->starts[p].x == b->starts[p].x);
        CHECK(a->starts[p].y == b->starts[p].y);
        CHECK(a->starts[p].home_island == b->starts[p].home_island);
    }
}

TEST_CASE("world plan: together, all ships start at one place, as in the original") {
    sim::WorldSettings s;
    s.starts = sim::WorldSettings::Starts::Together;
    s.seed = 3;
    auto p = sim::plan_world(s, templates());
    REQUIRE(p.ok());
    CHECK(p->shared_start);
    check_plan(*p, s);
    for (const sim::StartArea& st : p->starts) {
        CHECK(std::abs(static_cast<std::int32_t>(st.x) - 250) <= 10);
        CHECK(std::abs(static_cast<std::int32_t>(st.y) - 175) <= 10);
        CHECK(st.home_island == 0xFF);
    }
}

TEST_CASE("world plan: players five on are computers behind the factions, each with a ship at its start") {
    // The seven slots of a level's file: four players, free trader, pirates, natives.
    sim::World world(600, 400);
    std::vector<sim::Player> players(7);
    players[1].role = sim::PlayerRole::Computer;
    players[1].money = 30000;
    sim::make_neutral(players[4], sim::faction::kTrader);
    sim::make_neutral(players[5], sim::faction::kPirates);
    sim::make_neutral(players[6], sim::faction::kNatives);
    world.set_players(players);
    world.add_ship(sim::make_ship("Pinasse", 10, 10, 21, 1));
    sim::WorldPlan plan;
    for (std::uint32_t n = 0; n < 6; ++n) {
        plan.starts.push_back(sim::StartArea{100 + 50 * n, 200, 0, 0});
    }

    REQUIRE(sim::add_players(world, plan, 6).ok());
    REQUIRE(world.players().size() == 9);
    CHECK(world.players()[5].faction == sim::faction::kPirates);  // the factions keep their slots
    for (std::size_t n = 4; n < 6; ++n) {
        const sim::PlayerId slot = sim::player_slot(n);
        CHECK(slot == 7 + (n - 4));
        const sim::Player& p = world.players()[slot];
        CHECK(p.role == sim::PlayerRole::Computer);
        CHECK(p.money == 30000);
        CHECK(p.banner == sim::kFirstExtraBanner + (n - 4));
        const auto ship = std::find_if(world.ships().begin(), world.ships().end(),
                                       [&](const sim::Ship& s) { return s.owner == slot; });
        REQUIRE(ship != world.ships().end());
        CHECK(ship->x == plan.starts[n].x);
        CHECK(ship->y == plan.starts[n].y);
        CHECK(ship->name.empty());  // named by the game, not after the copied ship
    }
    CHECK(world.diplomacy().size() == 9);

    // Four players need nothing; more than the starts or twelve do not fit.
    sim::World four(600, 400);
    four.set_players(players);
    CHECK(sim::add_players(four, plan, 4).ok());
    CHECK(four.players().size() == 7);
    CHECK(!sim::add_players(four, plan, 7).ok());
    CHECK(!sim::add_players(four, plan, 13).ok());
}

TEST_CASE("world plan: a fixed number of gold islands replaces the level's gold") {
    sim::WorldSettings s;
    s.seed = 3;
    auto by_level = sim::plan_world(s, templates());
    REQUIRE(by_level.ok());
    CHECK(by_level->gold_islands == 0);
    CHECK(by_level->plan.ores[2].count == 3);  // easy: three, as the original's plans
    s.gold_islands = 7;
    auto fixed = sim::plan_world(s, templates());
    REQUIRE(fixed.ok());
    CHECK(fixed->gold_islands == 7);
    CHECK(fixed->plan.ores[2].building == 22405);
    CHECK(fixed->plan.ores[2].count == 7);
    // Only the gold changes.
    CHECK(fixed->plan.ores[0].count == by_level->plan.ores[0].count);
    CHECK(fixed->plan.islands.size() == by_level->plan.islands.size());
}

namespace {

/// Shapes::Own: an island's rectangle, of its own sides or its template's class.
Box own_box(const sim::WorldPlan& p, const assets::ScenarioPlan::Island& i) {
    for (const auto& o : p.own_islands) {
        if (o.number == i.number) {
            return Box{i.x, i.y, o.width, o.height};
        }
    }
    return box(i);
}

}  // namespace

TEST_CASE("world plan: islands of their own size cover the land asked for, within the sides asked for") {
    using Landscape = sim::WorldSettings::Landscape;
    for (const std::uint8_t land : std::array<std::uint8_t, 3>{10, 25, 45}) {
        for (const auto spread : {Landscape::Spread::Even, Landscape::Spread::Mixed, Landscape::Spread::FewGiants}) {
            sim::WorldSettings s;
            s.shapes = sim::WorldSettings::Shapes::Own;
            s.landscape.land = land;
            s.landscape.min_side = 20;
            s.landscape.max_side = 160;
            s.landscape.spread = spread;
            s.seed = land * 7U + static_cast<std::uint64_t>(spread);
            auto plan = sim::plan_world(s, templates());
            REQUIRE(plan.ok());
            CHECK(plan->shapes == sim::WorldSettings::Shapes::Own);
            CHECK(plan->landscape == s.landscape);
            const auto& islands = plan->plan.islands;
            std::uint64_t area = 0;
            std::size_t own = 0;
            for (std::size_t a = 0; a < islands.size(); ++a) {
                const Box ra = own_box(*plan, islands[a]);
                area += static_cast<std::uint64_t>(ra.w * ra.h);
                CHECK(ra.x >= 20);
                CHECK(ra.y >= 20);
                CHECK(ra.x + ra.w <= s.width - 20);
                CHECK(ra.y + ra.h <= s.height - 20);
                const bool shaped = (islands[a].flags & sim::kPlanOwnShape) != 0;
                if (shaped) {
                    ++own;
                    CHECK(std::max(ra.w, ra.h) <= s.landscape.max_side);
                    CHECK(std::min(ra.w, ra.h) >= sim::shapes::kMinSide);
                }
                for (std::size_t b = a + 1; b < islands.size(); ++b) {
                    const Box rb = own_box(*plan, islands[b]);
                    CHECK((ra.x + ra.w + 4 <= rb.x || rb.x + rb.w + 4 <= ra.x || ra.y + ra.h + 4 <= rb.y ||
                           rb.y + rb.h + 4 <= ra.y));
                }
            }
            CHECK(own == plan->own_islands.size());
            CHECK(own == islands.size());  // villages and volcanoes too are on islands of their own shape
            // Volcanoes on islands of their own shape, at most one per player (Normal; fewer where
            // few islands have room for one), none at home.
            std::size_t volcanoes = 0;
            std::size_t villages = 0;
            for (const auto& o : plan->own_islands) {
                volcanoes += o.volcano ? 1U : 0U;
                villages += o.village ? 1U : 0U;
                CHECK(!(o.volcano && o.village));
                CHECK((!o.village || std::min(o.width, o.height) >= 40));
                for (const sim::StartArea& st : plan->starts) {
                    CHECK(!((o.volcano || o.village) && o.number == st.home_island));
                }
            }
            CHECK(villages > 0);
            CHECK(volcanoes > 0);
            CHECK(volcanoes <= s.players);
            CHECK(volcanoes == s.players || land == 10);
            // The land the rectangles hold: about what was asked; with much land and many small
            // islands, the water between them leaves less room.
            const std::uint64_t map = std::uint64_t{s.width} * s.height;
            const std::uint64_t land_tiles = area * s.landscape.fill() / 1000;
            CHECK(land_tiles * 100 >= map * land * (land >= 45 ? 70 : land >= 25 ? 80 : 90) / 100);
            CHECK(land_tiles * 100 <= map * land * 130 / 100);
            // Every start keeps a big home island without a village, both climates within reach.
            for (const sim::StartArea& st : plan->starts) {
                REQUIRE(st.home_island < islands.size());
                CHECK((islands[st.home_island].flags & sim::kPlanNoVillage) != 0);
            }
        }
    }
}

TEST_CASE("world plan: islands of their own size: the same seed the same plan, few giants are giants") {
    sim::WorldSettings s;
    s.shapes = sim::WorldSettings::Shapes::Own;
    s.landscape.spread = sim::WorldSettings::Landscape::Spread::FewGiants;
    s.landscape.min_side = 20;
    s.landscape.max_side = 200;
    s.landscape.land = 30;
    s.width = 1000;  // room for giants between the start areas
    s.height = 700;
    s.seed = 11;
    auto a = sim::plan_world(s, templates());
    auto b = sim::plan_world(s, templates());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(a->own_islands.size() == b->own_islands.size());
    std::size_t giants = 0;
    for (std::size_t i = 0; i < a->own_islands.size(); ++i) {
        CHECK(a->own_islands[i].number == b->own_islands[i].number);
        CHECK(a->own_islands[i].width == b->own_islands[i].width);
        CHECK(a->own_islands[i].height == b->own_islands[i].height);
        giants += std::max(a->own_islands[i].width, a->own_islands[i].height) >= 150 ? 1U : 0U;
    }
    CHECK(giants >= 1);
    CHECK(giants * 4 <= a->own_islands.size());
}

TEST_CASE("world plan: the map grows for islands far larger than the original's") {
    sim::WorldSettings s;
    CHECK(sim::fit_landscape(s).width == 500);  // templates: as chosen
    s.shapes = sim::WorldSettings::Shapes::Own;
    s.landscape.max_side = 100;
    CHECK(sim::fit_landscape(s).width == 500);  // room enough
    CHECK(sim::fit_landscape(s).height == 350);
    s.landscape.max_side = 300;
    const sim::WorldSettings big = sim::fit_landscape(s);
    CHECK(big.height >= 450);                        // one and a half times the largest island
    CHECK(big.width * 350 / 500 + 2 >= big.height);  // the same shape
    CHECK(big.height * 500 / 350 + 2 >= big.width);
    CHECK(std::uint64_t{big.width} * big.height * 100 >= std::uint64_t{300} * 300 * 16 * 99);  // room for giants
    s.landscape.max_side = 1000;
    CHECK(sim::fit_landscape(s).width == sim::WorldSettings::kMaxSide);

    // Islands of more than 255 tiles, which only Open1602's own chunks can hold.
    s.landscape.max_side = 400;
    s.landscape.min_side = 100;
    s.landscape.spread = sim::WorldSettings::Landscape::Spread::FewGiants;
    s.landscape.land = 30;
    s.seed = 5;
    auto plan = sim::plan_world(sim::fit_landscape(s), templates());
    REQUIRE(plan.ok());
    std::uint16_t largest = 0;
    for (const auto& own : plan->own_islands) {
        largest = std::max({largest, own.width, own.height});
        CHECK(std::max(own.width, own.height) <= 400);
    }
    CHECK(largest > 255);

    // Native villages on such islands too (the generator puts them within the first 255 tiles).
    s.landscape.min_side = 260;
    s.natives = sim::WorldSettings::Amount::Many;
    auto giants = sim::plan_world(sim::fit_landscape(s), templates());
    REQUIRE(giants.ok());
    CHECK(std::any_of(giants->own_islands.begin(), giants->own_islands.end(),
                      [](const auto& own) { return own.village && std::min(own.width, own.height) > 255; }));
}

TEST_CASE("world plan: the landscape's settings by name, as setup files and the command line give them") {
    using L = sim::WorldSettings::Landscape;
    L l;
    CHECK(sim::set_landscape(l, "land", "45"));
    CHECK(l.land == 45);
    CHECK(!sim::set_landscape(l, "land", "4"));
    CHECK(!sim::set_landscape(l, "land", "20%"));
    CHECK(l.land == 45);
    CHECK(sim::set_landscape(l, "max", "40"));
    CHECK(l.max_side == 40);
    CHECK(l.min_side == 30);
    CHECK(sim::set_landscape(l, "min", "200"));  // the largest follows
    CHECK(l.min_side == 200);
    CHECK(l.max_side == 200);
    CHECK(!sim::set_landscape(l, "max", "1001"));
    CHECK(sim::set_landscape(l, "spread", "giants"));
    CHECK(l.spread == L::Spread::FewGiants);
    CHECK(sim::set_landscape(l, "gap", "wide"));
    CHECK(l.gap == L::Gap::Wide);
    CHECK(sim::set_landscape(l, "coast", "rugged"));
    CHECK(l.coast == sim::shapes::Roughness::Rugged);
    CHECK(sim::set_landscape(l, "outline", "branched"));
    CHECK(l.outline == L::Outline::Branched);
    CHECK(sim::set_landscape(l, "forest", "little"));
    CHECK(l.forest == sim::shapes::Share::Little);
    CHECK(sim::set_landscape(l, "mountains", "much"));
    CHECK(l.mountains == sim::shapes::Share::Much);
    CHECK(!sim::set_landscape(l, "mountains", "many"));
    CHECK(!sim::set_landscape(l, "rivers", "many"));
    // Every preset has a name of its own and is a landscape the settings can give.
    for (const sim::LandscapePreset& p : sim::landscape_presets()) {
        CHECK(std::ranges::count(sim::landscape_presets(), p.name, &sim::LandscapePreset::name) == 1);
        CHECK(p.landscape.min_side <= p.landscape.max_side);
        CHECK(p.landscape.land >= L::kMinLand);
        CHECK(p.landscape.land <= L::kMaxLand);
    }
}
