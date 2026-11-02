// SPDX-License-Identifier: GPL-3.0-or-later
// Scores, high score lists, campaign progress and the end screen's figures.

#include <cstring>

#include "../test_framework.hpp"

#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/high_scores.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/score.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

/// Two classes with Hiscore 1 and 3.
assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n  Hiscore: 1\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n  Hiscore: 3\n"
        "EndObj\n"
        "Objekt: HAUS\n  Nummer: 0\n  Id: 20101\n  Gfx: 0\nEndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

void add_city(sim::World& world, std::uint8_t index, sim::PlayerId owner, std::vector<std::uint32_t> inhabitants) {
    sim::City c;
    c.island = 0;
    c.index = index;
    c.owner = owner;
    c.inhabitants = std::move(inhabitants);
    world.add_city(c);
}

assets::HighScore line(std::string name, std::int64_t points, std::uint32_t game = 0) {
    assets::HighScore h;
    h.name = std::move(name);
    h.points = points;
    h.game_id = game;
    return h;
}

}  // namespace

TEST_CASE("scores: the curves of the original") {
    // City points per Einwohner / 20 (checked against's table).
    const std::array<std::pair<std::size_t, int>, 11> city{{{0, 12},
                                                            {15, 14},
                                                            {25, 19},
                                                            {50, 32},
                                                            {100, 51},
                                                            {200, 76},
                                                            {400, 102},
                                                            {450, 130},
                                                            {500, 160},
                                                            {750, 206},
                                                            {1000, 254}}};
    for (const auto& [i, v] : city) {
        CHECK(sim::city_curve(i) == v);
    }
    CHECK(sim::city_curve(5000) == 254);  // beyond 20,000 Einwohner
    for (std::size_t n = 0; n <= 20; ++n) {
        CHECK(sim::fight_curve(n) == static_cast<std::int32_t>(100 * n));
    }
    const std::array<std::int32_t, 6> from80{5000, 5033, 5066, 5099, 5133, 5166};
    for (std::size_t i = 0; i < from80.size(); ++i) {
        CHECK(sim::fight_curve(80 + i) == from80[i]);
    }
    CHECK(sim::fight_curve(200) == 10000);
    CHECK(sim::fight_curve(999) == 10000);
}

TEST_CASE("scores: every part of the sheet") {
    const assets::BuildingTable buildings = make_buildings();
    sim::World world(100, 100);
    std::vector<sim::Player> players(3);
    players[1].role = sim::PlayerRole::Computer;
    world.set_players(players);
    // An island of 16 x 16 tiles; city 0 holds the sampled tiles (0,0) and (8,0).
    sim::IslandMap map(16, 16);
    sim::MapCell cell;
    cell.city = 0;
    map.set_top_cell(0, 0, cell);
    map.set_top_cell(8, 0, cell);
    map.set_top_cell(1, 1, cell);  // not sampled
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 0, 0}).ok());
    add_city(world, 0, 0, {1000, 0});
    add_city(world, 1, 0, {400, 100});

    sim::Player& p = *world.player(0);
    p.money = 60'000;
    p.monuments_allowed = 2;
    p.arches_allowed = 1;
    p.soldiers_killed = 10;
    p.soldiers_lost = 10;
    p.ships_sunk = 3;
    p.ships_lost = 1;
    world.set_time_ms(36'000'000);  // ten hours

    const sim::ScoreSheet s = sim::score_sheet(world, buildings, 0);
    CHECK(s.money_points == 1000);
    CHECK(s.inhabitants == 1500);
    CHECK(s.subjects_points == (1400 * 1) / 50 + (100 * 3) / 50);
    CHECK(s.cities == 2);
    CHECK(s.city_points == 500 + 19 * 500 / 64);  // 1000 Einwohner, then 500
    CHECK(s.islands == 1);
    CHECK(s.land_points == 2 * 64 * 10000 / 256);
    CHECK(s.monuments == 2);
    CHECK(s.monument_points == 2 * 90'000'000 / 360'000);
    CHECK(s.defeat_points == 1500);
    CHECK(s.soldier_points == 200 * 64 / 128);  // two wins on the curve, half the fights won
    CHECK(s.ship_points == 200 * 96 / 128);
    CHECK(!s.won);
    CHECK(s.bonus == 0);
    const std::int64_t sum = s.money_points + s.subjects_points + s.city_points + s.land_points + s.monument_points +
                             s.defeat_points + s.soldier_points + s.ship_points;
    CHECK(s.total == sum);
    CHECK(sim::player_score(world, buildings, 2) == 0);  // nothing
    CHECK(sim::player_score(world, buildings, 9) == 0);  // no such player

    // Monuments count from the tenth minute on; a winner's points count twice.
    world.set_time_ms(600'000);
    CHECK(sim::score_sheet(world, buildings, 0).monument_points == 0);
    world.player(0)->won = true;
    const sim::ScoreSheet w = sim::score_sheet(world, buildings, 0);
    CHECK(w.bonus_percent == 100);
    CHECK(w.bonus == w.subtotal());
    CHECK(w.total == 2 * w.subtotal());
}

TEST_CASE("scores: city sizes as the original rates them") {
    const assets::BuildingTable buildings = make_buildings();
    const std::array<std::pair<std::uint32_t, std::int64_t>, 7> sizes{
        {{100, 18}, {500, 148}, {1000, 500}, {2000, 1593}, {5000, 6406}, {10000, 25000}, {20000, 79375}}};
    for (const auto& [e, points] : sizes) {
        sim::World world(100, 100);
        world.set_players(std::vector<sim::Player>(1));
        add_city(world, 0, 0, {e});
        CHECK(sim::score_sheet(world, buildings, 0).city_points == points);
    }
}

TEST_CASE("scores: a growing city wants a monument every 30 minutes") {
    sim::World world(100, 100);
    world.set_players(std::vector<sim::Player>(1));
    add_city(world, 0, 0, {0, 0, 150, 50});  // 200 Bürger and higher
    sim::City& c = *world.city(0, 0);
    std::vector<sim::CityEvent> events;

    c.house_growth = 5;
    c.growing_since_ms = 0;
    world.set_time_ms(30 * 60'000 - 1);
    sim::allow_monument(world, c, &events);
    CHECK(world.player(0)->monuments_allowed == 0);  // not yet
    CHECK(!world.player(0)->unlocked(14));
    world.set_time_ms(30 * 60'000);
    sim::allow_monument(world, c, &events);
    CHECK(world.player(0)->monuments_allowed == 1);
    CHECK(world.player(0)->unlocked(14));  // op 0x55: the level no city reaches
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == sim::CityEvent::Kind::Monument);
    CHECK(c.growing_since_ms == 30 * 60'000);  // the next in another 30 minutes

    world.set_time_ms(60 * 60'000);
    c.house_growth = 0;  // not growing
    sim::allow_monument(world, c, &events);
    CHECK(world.player(0)->monuments_allowed == 1);
    c.house_growth = 1;
    c.inhabitants = {1000, 1000, 199};  // too few of the upper classes
    sim::allow_monument(world, c, &events);
    CHECK(world.player(0)->monuments_allowed == 1);
}

TEST_CASE("scores: the game's name, stars and id come from its source") {
    sim::World world(100, 100);
    CHECK(sim::game_identity(world).name.empty());  // no source

    assets::ChunkWriter w;
    const char name[] = "Das Atoll";
    w.add("NAME", std::span(reinterpret_cast<const std::uint8_t*>(name), sizeof(name)));
    const std::array<std::uint8_t, 4> stars{3, 0, 0, 0};
    w.add("SZENE_RANKING", stars);
    const std::array<std::uint8_t, 4> number{5, 0, 0, 0};
    w.add("SZENE_MISSNR", number);
    std::vector<std::uint8_t> timers(0x298, 0);
    const std::uint32_t game = 713262714U;
    std::memcpy(timers.data() + 0xAC, &game, 4);  // little-endian on every supported platform
    w.add("TIMERS", timers);
    auto file = assets::ChunkFile::from_bytes(w.take());
    REQUIRE(file.ok());
    world.set_source(std::make_shared<const assets::ChunkFile>(std::move(file).value()));

    const sim::GameIdentity id = sim::game_identity(world);
    CHECK(id.name == "Das Atoll");
    CHECK(id.ranking == 3);
    CHECK(!id.custom);
    CHECK(id.mission_number == 5);
    CHECK(id.campaign == sim::GameIdentity::kNone);
    CHECK(id.game_id == game);
}

TEST_CASE("scores: the end screen lists humans and computers, the local player first") {
    const assets::BuildingTable buildings = make_buildings();
    sim::World world(100, 100);
    std::vector<sim::Player> players(5);
    players[0].name = "Merlin";
    players[1].role = sim::PlayerRole::Computer;
    players[1].name = "Ming";
    players[2].role = sim::PlayerRole::Human;
    players[2].status = sim::PlayerStatus::Absent;
    sim::make_neutral(players[3], sim::faction::kPirates);
    players[4].role = sim::PlayerRole::Computer;
    players[4].status = sim::PlayerStatus::Defeated;
    world.set_players(players);
    add_city(world, 0, 0, {10, 0});
    add_city(world, 1, 1, {100, 0});
    add_city(world, 2, 1, {0, 10});
    sim::Ship ship;
    ship.owner = 1;
    world.add_ship(ship);

    const sim::EndScreenSummary s = sim::end_screen_summary(world, buildings, 0, sim::GameOutcome::Lost);
    CHECK(s.outcome == sim::GameOutcome::Lost);
    REQUIRE(s.rows.size() == 3);  // not the absent slot, not the pirates
    CHECK(s.rows[0].player == 0);
    CHECK(s.rows[0].name == "Merlin");
    CHECK(s.rows[0].sheet.inhabitants == 10);
    CHECK(s.rows[1].player == 1);
    CHECK(s.rows[1].sheet.cities == 2);
    CHECK(s.rows[1].sheet.inhabitants == 110);
    CHECK(s.rows[1].sheet.total == sim::player_score(world, buildings, 1));
    CHECK(s.rows[2].player == 4);
    CHECK(s.rows[2].status == sim::PlayerStatus::Defeated);

    const sim::EndScreenSummary other = sim::end_screen_summary(world, buildings, 4, sim::GameOutcome::Lost);
    REQUIRE(other.rows.size() == 3);
    CHECK(other.rows[0].player == 4);
    CHECK(other.rows[1].player == 0);  // then in the order of the slots
    CHECK(other.rows[2].player == 1);
}

TEST_CASE("high scores: best first, above equal points, one line per game and player") {
    std::vector<assets::HighScore> list;
    CHECK(assets::insert_high_score(list, line("A", 100)) == 0U);
    REQUIRE(list.size() == assets::kHighScoreLines);  // padded with empty lines
    CHECK(assets::insert_high_score(list, line("B", 300)) == 0U);
    CHECK(assets::insert_high_score(list, line("C", 100)) == 1U);  // above the equal one
    CHECK(list[0].name == "B");
    CHECK(list[1].name == "C");
    CHECK(list[2].name == "A");
    CHECK(list[3].name.empty());

    // The same game and player: a better result moves the line up, a worse one leaves it.
    CHECK(assets::insert_high_score(list, line("D", 200, 7)) == 1U);
    CHECK(assets::insert_high_score(list, line("D", 150, 7)) == 1U);
    CHECK(list[1].points == 200);
    CHECK(list[2].name == "C");
    CHECK(assets::insert_high_score(list, line("D", 400, 7)) == 0U);
    CHECK(list[1].name == "B");
    CHECK(list[2].name == "C");
    CHECK(list[3].name == "A");
    CHECK(list[4].name.empty());                                     // the old line is gone, nothing else moved out
    CHECK(assets::insert_high_score(list, line("D", 50, 8)) == 4U);  // another game

    for (int i = 0; i < 20; ++i) {
        (void)assets::insert_high_score(list, line("E" + std::to_string(i), 1000 + i));
    }
    CHECK(list.size() == assets::kHighScoreLines);
    CHECK(list.front().points == 1019);
    CHECK(!assets::insert_high_score(list, line("F", 1)).has_value());  // not good enough

    // Names longer than the original's 32 bytes are cut.
    std::vector<assets::HighScore> one;
    (void)assets::insert_high_score(one, line(std::string(40, 'x'), 5));
    CHECK(one[0].name.size() == assets::kHighScoreNameLength);
}

TEST_CASE("high scores: written as the original's .hss and read back") {
    std::vector<assets::HighScore> list{line("Merlin", 381, 713262714U), line("Ming \"der\" Gro\xdf", 12)};
    list[0].stars = 2;
    const std::string text = assets::write_high_scores(list);
    CHECK(text.find("Objekt:   HISCORE") != std::string::npos);
    CHECK(text.find("    Entry:    0, \"Merlin\", 381, 2, 713262714\n") != std::string::npos);
    CHECK(text.find("    Entry:   11, \"\", 0, 0, 0\n  \n  EndObj;\n\n") != std::string::npos);
    const auto back = assets::parse_high_scores(text);
    REQUIRE(back.size() == assets::kHighScoreLines);  // padded with empty lines
    CHECK(back[0].name == "Merlin");
    CHECK(back[0].points == 381);
    CHECK(back[0].stars == 2);
    CHECK(back[0].game_id == 713262714U);
    CHECK(back[1].name == "Ming 'der' Gro\xdf");  // a quote would end the name
    CHECK(back[2].name.empty());

    // Empty lines of a read list count as no points.
    std::vector<assets::HighScore> again = back;
    CHECK(assets::insert_high_score(again, line("New", 0)) == 2U);
    CHECK(again.size() == assets::kHighScoreLines);
}

TEST_CASE("campaign progress: wins open the next mission") {
    assets::CampaignProgress p;
    CHECK(p.numbered == 2);
    CHECK(p.record_win(0xFF, 0, 2));
    CHECK(p.numbered == 3);
    CHECK(!p.record_win(0xFF, 0, 2));  // an earlier mission again
    CHECK(p.record_win(0xFF, 0, 5));   // a later one the menu offered
    CHECK(p.numbered == 6);
    CHECK(!p.record_win(0xFF, 0, 1));  // the endless game opens nothing
    p.numbered = 17;
    CHECK(!p.record_win(0xFF, 0, 17));  // at most 17
    CHECK(p.numbered == 17);

    // A campaign part: the next one, by the part's number (SZENE_RANKING).
    CHECK(p.record_win(2, 1, 0xFF));
    CHECK(p.campaigns.at(2) == 2);
    CHECK(!p.record_win(2, 0, 0xFF));
    CHECK(p.campaigns.at(2) == 2);
    // A numbered mission below the progress falls through to its campaign, if any.
    CHECK(p.record_win(3, 0, 4));
    CHECK(p.campaigns.at(3) == 1);
    CHECK(p.numbered == 17);

    const assets::CampaignProgress back = assets::parse_progress(assets::write_progress(p));
    CHECK(back.numbered == 17);
    CHECK(back.campaigns == p.campaigns);

    const assets::CampaignProgress odd = assets::parse_progress("# x\nnumbered 99\ncampaign -1 3\njunk\n");
    CHECK(odd.numbered == 17);
    CHECK(odd.campaigns.empty());
}

TEST_CASE("campaign progress: the last part of campaign 5 ends the campaigns") {
    const assets::CampaignProgress p;
    CHECK(p.ends_campaigns(5, 3, 0xFF));
    CHECK(p.ends_campaigns(5, 4, 0xFF));
    CHECK(!p.ends_campaigns(5, 2, 0xFF));
    CHECK(!p.ends_campaigns(4, 3, 0xFF));
    CHECK(!p.ends_campaigns(0xFF, 0, 5));
    // A numbered mission the progress still counts goes by the numbered rule.
    CHECK(!p.ends_campaigns(5, 3, 2));
}
