// SPDX-License-Identifier: GPL-3.0-or-later
// What the local player is told of the game's events (engine/app/src/event_presenter.hpp).

#include <filesystem>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "event_presenter.hpp"

#include "o1602/audio/game_audio.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;

namespace {

constexpr sim::PlayerId kMe = 0;
constexpr sim::PlayerId kRival = 1;

/// Two players, an island at (100, 50) with a city of each.
struct Game {
    sim::World world{300, 200};
    app::EventPresenter presenter{
        [](const sim::Player& p, sim::PlayerId id) { return "player " + std::to_string(id) + " " + p.name; }};

    Game() {
        std::vector<sim::Player> players(2);
        players[kMe].role = sim::PlayerRole::Human;
        players[kRival].role = sim::PlayerRole::Computer;
        players[kRival].banner = 1;
        world.set_players(players);
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, sim::IslandMap(20, 10), 100, 50}).ok());
        for (const sim::PlayerId owner : {kMe, kRival}) {
            sim::City c;
            c.island = 0;
            c.index = owner;
            c.owner = owner;
            c.name = owner == kMe ? "Mine" : "Theirs";
            world.add_city(c);
        }
    }
};

const app::Announcement* announcement(const app::Notice& n) {
    return std::get_if<app::Announcement>(&n);
}

}  // namespace

TEST_CASE("event presenter: disasters slow the game down only as the game's settings say") {
    // Plague, fire, drought and eruption; any other message (attacks) slows down unless kept.
    for (const app::MessageId disaster : {0x01U, 0x02U, 0x03U, 0x5EU}) {
        CHECK(app::slows_down(app::EventSpeed::Normal, disaster));
        CHECK(!app::slows_down(app::EventSpeed::Attacks, disaster));
        CHECK(!app::slows_down(app::EventSpeed::Keep, disaster));
    }
    constexpr app::MessageId kOther = 0x10;
    CHECK(app::slows_down(app::EventSpeed::Normal, kOther));
    CHECK(app::slows_down(app::EventSpeed::Attacks, kOther));
    CHECK(!app::slows_down(app::EventSpeed::Keep, kOther));
}

TEST_CASE("event presenter: city, disaster, drought and diplomacy messages for the local player only") {
    Game g;
    sim::CityEvent hunger;
    hunger.kind = sim::CityEvent::Kind::Hunger;
    hunger.owner = kMe;
    sim::CityEvent theirs = hunger;
    theirs.owner = kRival;
    theirs.city = 1;
    sim::CityEvent shortage;
    shortage.kind = sim::CityEvent::Kind::Shortage;
    shortage.owner = kMe;
    sim::CityEvent locked = hunger;
    locked.kind = sim::CityEvent::Kind::MaterialLocked;
    sim::CityEvent their_lock = locked;
    their_lock.owner = kRival;
    their_lock.city = 1;
    const auto city = g.presenter.city_events(g.world, kMe, {hunger, theirs, shortage, locked, their_lock});
    REQUIRE(city.size() == 2);
    CHECK(announcement(city[0])->message == 0x33);
    CHECK(announcement(city[1])->message == 0x48);  // "Das Volk bekommt kein Baumaterial"
    // Every message points somewhere (Space jumps there): a city without a Kontor at its island's middle.
    CHECK((announcement(city[0])->tile == std::make_pair(110.0, 55.0)));

    // Shortages, wishes and population marks (MELDUNG 0x11-0x44).
    sim::CityEvent spices = shortage;
    spices.ware = sim::ware::kFood + 2;
    sim::CityEvent tavern = hunger;
    tavern.kind = sim::CityEvent::Kind::Unlocked;
    tavern.infra = 4;
    sim::CityEvent gallows = tavern;
    gallows.infra = 10;
    sim::CityEvent grown = hunger;
    grown.kind = sim::CityEvent::Kind::Population;
    grown.message = 0x12;
    sim::CityEvent rival_small = grown;
    rival_small.owner = kRival;
    rival_small.city = 1;
    sim::CityEvent rival_big = rival_small;
    rival_big.message = 0x15;
    const auto people = g.presenter.city_events(g.world, kMe, {spices, tavern, gallows, grown, rival_small, rival_big});
    REQUIRE(people.size() == 4);
    CHECK(announcement(people[0])->message == 0x36);  // "Es fehlt an Gewürzen"
    CHECK(announcement(people[1])->message == 0x3C);  // "Euer Volk wünscht ein Wirtshaus"
    CHECK(announcement(people[2])->message == 0x12);  // 200 inhabitants
    CHECK(announcement(people[3])->message == 0x21);  // a rival's city passed 1000
    CHECK(announcement(people[3])->about == kRival);  // in the rival's colour
    CHECK(!announcement(people[2])->about);           // the own city in the own colour
    CHECK(g.presenter.city_events(g.world, sim::kNoPlayer, {rival_big}).empty());
    // Another city of the same rival at the same size says nothing more, a larger size does (FB-30).
    sim::CityEvent rival_other = rival_big;
    rival_other.city = 3;
    CHECK(g.presenter.city_events(g.world, kMe, {rival_other}).empty());
    rival_other.message = 0x16;
    CHECK(g.presenter.city_events(g.world, kMe, {rival_other}).size() == 1);
    g.presenter.reset_game();  // another game tells them again
    CHECK(g.presenter.city_events(g.world, kMe, {rival_big}).size() == 1);
    // An event of an island the world does not have is left out, not read beyond the islands.
    sim::VolcanoEvent far;
    far.kind = sim::VolcanoEvent::Kind::Erupted;
    far.message = true;
    far.island = 7;
    CHECK(g.presenter.volcano_events(g.world, kMe, {far}).empty());

    sim::DisasterEvent fire;
    fire.island = 0;
    fire.x = 3;
    fire.y = 4;
    fire.owner = kMe;
    sim::DisasterEvent spread = fire;
    spread.spread = true;
    sim::DisasterEvent plague = fire;
    plague.disaster = sim::Disaster::Kind::Plague;
    const auto disasters = g.presenter.disaster_events(g.world, kMe, {fire, spread, plague});
    REQUIRE(disasters.size() == 2);
    CHECK(announcement(disasters[0])->message == 0x02);
    CHECK((announcement(disasters[0])->tile == std::make_pair(103.0, 54.0)));  // in world tiles
    CHECK(announcement(disasters[1])->message == 0x01);

    sim::IslandEvent drought;
    drought.island = 0;
    const auto islands = g.presenter.island_events(g.world, kMe, {drought});
    REQUIRE(islands.size() == 1);
    CHECK(announcement(islands[0])->message == 0x03);
    CHECK(announcement(islands[0])->tile.has_value());

    sim::DiplomacyNotice offer;
    offer.kind = sim::DiplomacyNotice::Kind::TradeOffered;
    offer.to = kMe;
    offer.from = kRival;
    sim::DiplomacyNotice other = offer;
    other.to = kRival;
    other.from = kMe;
    const auto diplomacy = g.presenter.diplomacy_notices(g.world, kMe, {offer, other});
    REQUIRE(diplomacy.size() == 1);
    CHECK(announcement(diplomacy[0])->message == 0x51);
    // The icon in the colour of who made the offer.
    CHECK(announcement(diplomacy[0])->about == std::optional<sim::PlayerId>(kRival));
    // It points at the rival's city, at its Kontor once it has one.
    g.world.city(0, kRival)->warehouse_x = 4;
    g.world.city(0, kRival)->warehouse_y = 6;
    const auto at_kontor = g.presenter.diplomacy_notices(g.world, kMe, {offer});
    REQUIRE(at_kontor.size() == 1);
    CHECK((announcement(at_kontor[0])->tile == std::make_pair(104.0, 56.0)));

    // A computer's answer to a founding points at its city on that island.
    sim::DiplomacyNotice tolerated;
    tolerated.kind = sim::DiplomacyNotice::Kind::FoundingTolerated;
    tolerated.to = kMe;
    tolerated.from = kRival;
    tolerated.island = 0;
    tolerated.city = kRival;
    sim::DiplomacyNotice protest = tolerated;
    protest.kind = sim::DiplomacyNotice::Kind::FoundingProtest;
    const auto answers = g.presenter.diplomacy_notices(g.world, kMe, {tolerated, protest});
    REQUIRE(answers.size() == 2);
    CHECK(announcement(answers[0])->message == 0x4B);
    CHECK(announcement(answers[1])->message == 0x49);
    CHECK((announcement(answers[0])->tile == std::make_pair(104.0, 56.0)));
}

TEST_CASE("event presenter: war and peace between two others go to the status bar, if wanted") {
    Game g;
    std::vector<sim::Player> players = g.world.players();
    players.push_back(players[kRival]);
    players[2].banner = 2;
    players.push_back(sim::Player{});
    players[3].role = sim::PlayerRole::Neutral;  // the pirates
    g.world.set_players(players);
    sim::DiplomacyNotice war;
    war.kind = sim::DiplomacyNotice::Kind::PeaceBroken;
    war.from = kRival;
    war.to = 2;
    sim::DiplomacyNotice peace = war;
    peace.kind = sim::DiplomacyNotice::Kind::PeaceSigned;
    sim::DiplomacyNotice offer = war;
    offer.kind = sim::DiplomacyNotice::Kind::PeaceOffered;
    sim::DiplomacyNotice pirates = war;
    pirates.to = 3;
    const auto told = g.presenter.diplomacy_notices(g.world, kMe, {war, peace, offer, pirates});
    REQUIRE(told.size() == 2);
    const app::Announcement* a = announcement(told[0]);
    CHECK(a->message == 0x4D);
    CHECK(!a->voice);
    CHECK(a->about == std::optional<sim::PlayerId>(kRival));
    CHECK(a->tile.has_value());  // the rival's city
    CHECK(a->text == "War: Blue player against Yellow player");
    CHECK(announcement(told[1])->text == "Peace: Blue player and Yellow player");
    // The war on the local player is the original's message.
    war.to = kMe;
    const auto mine = g.presenter.diplomacy_notices(g.world, kMe, {war});
    REQUIRE(mine.size() == 1);
    CHECK(announcement(mine[0])->voice);
    CHECK(announcement(mine[0])->text.empty());
    // Switched off.
    g.presenter.set_rival_wars(false);
    CHECK(g.presenter.diplomacy_notices(g.world, kMe, {peace}).empty());
}

TEST_CASE("event presenter: a rival's new city shows in the rival's colour, an own one says nothing") {
    Game g;
    sim::ConstructionEvent founded;
    founded.kind = sim::ConstructionEvent::Kind::CityFounded;
    founded.island = 0;
    founded.city = 1;  // the rival's
    sim::ConstructionEvent own = founded;
    own.city = 0;
    const auto notices = g.presenter.construction_events(g.world, kMe, {own, founded});
    REQUIRE(notices.size() == 1);
    const app::Announcement* a = announcement(notices[0]);
    CHECK(a->message == 0x0E);
    CHECK(a->about == std::optional<sim::PlayerId>(kRival));
    CHECK((a->tile == std::make_pair(110.0, 55.0)));  // the island's middle
}

TEST_CASE("event presenter: an own defeat plays its video directly, the defeat videos alternate") {
    Game g;
    sim::GameEvent defeated;
    defeated.kind = sim::GameEvent::Kind::PlayerDefeated;
    defeated.player = kMe;
    const auto first = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    REQUIRE(first.size() == 3);
    CHECK(std::get<app::ScoreUpdate>(first[0]).defeated == kMe);
    CHECK(announcement(first[1])->message == 0x53);
    CHECK(std::get<app::DirectVideo>(first[2]).number == 1010);
    const auto second = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    CHECK(std::get<app::DirectVideo>(second[2]).number == 2020);
    defeated.reason = sim::GameEvent::Reason::Bankrupt;
    const auto bankrupt = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    CHECK(std::get<app::DirectVideo>(bankrupt[2]).number == 998);

    // Somebody else's defeat: a message only.
    defeated.player = kRival;
    const auto rival = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    REQUIRE(rival.size() == 2);
    CHECK(announcement(rival[1])->message == 0x59);
    CHECK(announcement(rival[1])->about == kRival);  // the icon in the leaving player's colour
}

TEST_CASE("event presenter: a rival's defeat announces the culprit's triumphal arch instead (op 3 sub 9)") {
    Game g;
    sim::GameEvent defeated;
    defeated.kind = sim::GameEvent::Kind::PlayerDefeated;
    defeated.player = kRival;
    defeated.reason = sim::GameEvent::Reason::NoPossessions;
    // Nobody credited: no message.
    CHECK(g.presenter.game_events(g.world, kMe, {defeated}, 1000).size() == 1);
    // Credited to me without an arch: "Ein Konkurrent wurde besiegt!".
    defeated.by = kMe;
    const auto plain = g.presenter.game_events(g.world, kMe, {defeated}, 1000);
    REQUIRE(plain.size() == 2);
    CHECK(announcement(plain[1])->message == 0x5A);
    CHECK(announcement(plain[1])->about == kRival);  // in the colour of the defeated
    // With the arch: only its message.
    sim::GameEvent arch;
    arch.kind = sim::GameEvent::Kind::ArchAllowed;
    arch.player = kMe;
    arch.by = kRival;
    const auto with_arch = g.presenter.game_events(g.world, kMe, {defeated, arch}, 1000);
    REQUIRE(with_arch.size() == 2);
    CHECK(announcement(with_arch[1])->message == 0x32);
}

TEST_CASE("event presenter: the multiplayer victory speaks to the winner on track 1, to the others on track 2") {
    Game g;
    sim::GameEvent won;
    won.kind = sim::GameEvent::Kind::MissionAccomplished;
    won.player = kMe;
    won.message = audio::kMessageMultiplayerWin;
    const auto mine = g.presenter.game_events(g.world, kMe, {won}, 0);
    REQUIRE(mine.size() == 1);
    CHECK(announcement(mine[0])->tracks == 0x03);
    CHECK(announcement(mine[0])->message == audio::kMessageMultiplayerWin);  // banner 0: red

    // The rival won the race: the game is over for this player.
    sim::Mission race;
    race.success_message = audio::kMessageMultiplayerWin;
    g.world.set_missions({race});
    g.world.player(kRival)->mission = 0;
    sim::GameEvent over;
    over.kind = sim::GameEvent::Kind::GameOver;
    over.player = kRival;
    over.message = race.success_message;  // the winner's message comes with the end of the game
    const auto theirs = g.presenter.game_events(g.world, kMe, {over}, 0);
    REQUIRE(theirs.size() == 2);
    CHECK(std::get<app::ScoreUpdate>(theirs[0]).progress);
    CHECK(announcement(theirs[1])->tracks == 0x05);
    CHECK(announcement(theirs[1])->message == audio::kMessageMultiplayerWin + 1);  // the rival's banner
}

TEST_CASE("event presenter: a further colour's victory never names one of the original's four") {
    Game g;
    g.world.player(kMe)->banner = sim::kFirstExtraBanner + 1;  // green
    g.world.player(kRival)->banner = sim::kFirstExtraBanner;   // orange
    sim::GameEvent won;
    won.kind = sim::GameEvent::Kind::MissionAccomplished;
    won.player = kMe;
    won.message = audio::kMessageMultiplayerWin;
    const auto mine = g.presenter.game_events(g.world, kMe, {won}, 0);
    REQUIRE(mine.size() == 1);
    CHECK(announcement(mine[0])->message == 0x54);  // „Ihr konntet den Euch gestellten Auftrag erfüllen!“
    CHECK(announcement(mine[0])->tracks == 0);

    sim::Mission race;
    race.success_message = audio::kMessageMultiplayerWin;
    g.world.set_missions({race});
    g.world.player(kRival)->mission = 0;
    sim::GameEvent over;
    over.kind = sim::GameEvent::Kind::GameOver;
    over.player = kRival;
    over.message = race.success_message;
    ui::set_language("en");
    auto banners = render::BannerTable::original();
    banners.add_more_players();
    g.presenter.set_banners(&banners);
    const auto theirs = g.presenter.game_events(g.world, kMe, {over}, 0);
    REQUIRE(theirs.size() == 2);
    // Not „Der weiße Spieler …“: the same words with the right colour, in the status bar only.
    const app::Announcement* a = announcement(theirs[1]);
    CHECK(a->message == audio::kMessageMultiplayerWin);  // its icon, in the winner's colour
    CHECK(a->about == kRival);
    CHECK(!a->voice);
    CHECK(a->text == "The orange player has fulfilled their task!");

    // A banner the language has no words for: the winner by name.
    g.presenter.reset_game();
    g.world.player(kRival)->name = "Lady Anne";
    g.world.player(kRival)->banner = 4;  // a faction's banner
    const auto named = g.presenter.game_events(g.world, kMe, {over}, 0);
    REQUIRE(named.size() == 2);
    CHECK(announcement(named[1])->text == "Lady Anne has fulfilled their task!");
}

TEST_CASE("event presenter: the end screen waits for the delay of running game time, or for the videos") {
    Game g;
    sim::GameEvent won;
    won.kind = sim::GameEvent::Kind::MissionAccomplished;
    won.player = kMe;
    won.message = 0x54;
    (void)g.presenter.game_events(g.world, kMe, {won}, 1000);
    CHECK(!g.presenter.end_screen_due(3000, false, false));
    CHECK(!g.presenter.end_screen_due(10'000, false, true));  // paused: the time does not count
    CHECK(!g.presenter.end_screen_due(12'000, false, false));
    const auto due = g.presenter.end_screen_due(13'000, false, false);
    REQUIRE(due.has_value());
    CHECK(*due == sim::GameOutcome::Won);
    CHECK(!g.presenter.end_screen_due(20'000, false, false));  // once

    // Shown once per game: a second victory does not bring it again.
    g.presenter.end_screen_shown();
    (void)g.presenter.game_events(g.world, kMe, {won}, 30'000);
    CHECK(!g.presenter.end_screen_due(40'000, false, false));

    // A new game; the defeat's video plays: the screen follows as soon as it ended.
    g.presenter.reset_game();
    sim::GameEvent defeated;
    defeated.kind = sim::GameEvent::Kind::PlayerDefeated;
    defeated.player = kMe;
    (void)g.presenter.game_events(g.world, kMe, {defeated}, 50'000);
    CHECK(!g.presenter.end_screen_due(50'100, true, false));
    CHECK(!g.presenter.end_screen_due(90'000, true, false));
    const auto lost = g.presenter.end_screen_due(90'010, false, false);
    REQUIRE(lost.has_value());
    CHECK(*lost == sim::GameOutcome::Lost);

    // Back to the menu: nothing follows.
    g.presenter.reset_game();
    (void)g.presenter.game_events(g.world, kMe, {defeated}, 0);
    g.presenter.drop_end_screen();
    CHECK(!g.presenter.end_screen_due(100'000, false, false));
}

TEST_CASE("event presenter: notes stay in the status line for a few seconds, the chat keeps 100 lines") {
    Game g;
    CHECK(g.presenter.notice(0) == nullptr);
    g.presenter.notify("Gespeichert", "notice.game_saved spiel03.o1602", 1000);
    REQUIRE(g.presenter.notice(1000) != nullptr);
    CHECK(*g.presenter.notice(4999) == "Gespeichert");
    CHECK(g.presenter.notice(1000 + app::EventPresenter::kNoticeMs) == nullptr);

    std::deque<app::ChatLine> chat;
    for (int i = 0; i < 105; ++i) {
        app::add_chat_line(&chat, app::ChatLine{"", std::to_string(i), sim::kNoPlayer, 0, false, {}}, false);
    }
    CHECK(chat.size() == app::kChatKept);
    CHECK(chat.front().text == "5");
    CHECK(chat.back().text == "104");
    app::add_chat_line(nullptr, app::ChatLine{}, false);  // no panel: nothing to keep
}

TEST_CASE("event presenter: a good running short is told early, its shortage then only in the log") {
    Game g;
    g.presenter.set_ware_names(std::vector<std::string>(sim::ware::kJewellery + 1, "Ware"));
    sim::CityEvent low;
    low.kind = sim::CityEvent::Kind::RunningLow;
    low.owner = kMe;
    low.ware = sim::ware::kFood + 1;  // tobacco goods
    low.minutes = 4;
    sim::CityEvent rival = low;
    rival.owner = kRival;
    rival.city = 1;
    sim::CityEvent told = low;
    told.kind = sim::CityEvent::Kind::Shortage;
    told.warned = true;
    sim::CityEvent sudden = told;
    sudden.warned = false;
    const auto notices = g.presenter.city_events(g.world, kMe, {low, rival, told, sudden});
    REQUIRE(notices.size() == 2);
    const app::Announcement* early = announcement(notices[0]);
    REQUIRE(early != nullptr);
    CHECK(early->message == 0x35);  // in the voice of "Es fehlt an Tabakwaren"
    CHECK(early->voice);
    CHECK(early->text.find("Mine") != std::string::npos);
    CHECK(early->text.find('4') != std::string::npos);
    CHECK(announcement(notices[1])->message == 0x35);  // no warning came before: the original's
    CHECK(announcement(notices[1])->text.empty());
}

TEST_CASE("event presenter: a treasure is announced at its tile, to the builder only") {
    Game g;
    sim::ConstructionEvent found;
    found.kind = sim::ConstructionEvent::Kind::TreasureFound;
    found.island = 0;
    found.x = 3;
    found.y = 2;
    found.player = kMe;
    found.treasure = sim::TilePos{4, 3};
    found.money = 1000;
    sim::ConstructionEvent theirs = found;
    theirs.player = kRival;
    const auto notices = g.presenter.construction_events(g.world, kMe, {theirs, found});
    REQUIRE(notices.size() == 1);
    const app::Announcement* a = announcement(notices[0]);
    REQUIRE(a != nullptr);
    CHECK(a->message == 0x2E);  // „Ein vergrabener Schatz wurde entdeckt!“
    CHECK((a->tile == std::make_pair(104.0, 53.0)));
    CHECK(g.presenter.construction_events(g.world, kRival, {found}).empty());
}

TEST_CASE("event presenter: the seller of a ship is told who bought it") {
    Game g;
    sim::Ship ship = sim::make_ship("Aurora", 120, 60, 21, kRival);
    g.world.add_ship(ship);
    const std::uint32_t id = g.world.ships().back().id;
    sim::ShipEvent sold;
    sold.kind = sim::ShipEvent::Kind::Sold;
    sold.ship = id;
    sold.player = kMe;   // the seller
    sold.city = kRival;  // the buyer
    sold.money = 2500;
    const auto told = g.presenter.ship_events(g.world, kMe, {sold});
    REQUIRE(told.size() == 1);
    const app::Announcement* a = announcement(told[0]);
    CHECK(a->message == 0x0A);
    CHECK(!a->voice);
    CHECK(a->about == std::optional<sim::PlayerId>(kRival));
    REQUIRE(a->tile.has_value());
    CHECK(static_cast<int>(a->tile->first) == 120);  // where the ship is
    CHECK(static_cast<int>(a->tile->second) == 60);
    CHECK(a->text == "Blue player has bought your ship Aurora for 2500 gold");
    // The buyer and everyone else hear nothing of it.
    CHECK(g.presenter.ship_events(g.world, kRival, {sold}).empty());
}

TEST_CASE("event presenter: a level levels.toml gives a message tells it, or nothing") {
    Game g;
    app::MessageTable messages;
    std::vector<std::string> warnings;
    REQUIRE(messages
                .load_package("spice", "[harbour_open]\nlike = 0x3C\ntext = \"{city} has a harbour.\"\n",
                              std::filesystem::path{}, warnings)
                .ok());
    REQUIRE(warnings.empty());
    std::vector<assets::InfraLevel> levels(34);
    levels[33].message = "spice:harbour_open";
    levels[4].message = "";               // the tavern: told nothing
    levels[5].message = "spice:no_such";  // the church: the original's wish
    g.world.set_infra_levels(levels);
    g.presenter.set_messages(&messages);

    sim::CityEvent reached;
    reached.kind = sim::CityEvent::Kind::Unlocked;
    reached.owner = kMe;
    reached.infra = 33;
    sim::CityEvent tavern = reached;
    tavern.infra = 4;
    sim::CityEvent church = reached;
    church.infra = 5;
    sim::CityEvent theatre = reached;
    theatre.infra = 7;
    const auto told = g.presenter.city_events(g.world, kMe, {reached, tavern, church, theatre});
    REQUIRE(told.size() == 3);
    CHECK(announcement(told[0])->message == *messages.find("spice:harbour_open"));
    CHECK(announcement(told[0])->text == "Mine has a harbour.");
    CHECK((announcement(told[0])->tile == std::make_pair(110.0, 55.0)));
    CHECK(announcement(told[1])->message == 0x3D);  // "Euer Volk wünscht eine Kirche"
    CHECK(announcement(told[2])->message == 0x42);  // the theatre: no message of its own

    // Without the table the original's wishes, and a level of a package none.
    g.presenter.set_messages(nullptr);
    const auto plain = g.presenter.city_events(g.world, kMe, {reached, church});
    REQUIRE(plain.size() == 1);
    CHECK(announcement(plain[0])->message == 0x3D);
}
