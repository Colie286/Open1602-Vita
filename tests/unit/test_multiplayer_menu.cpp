// SPDX-License-Identifier: GPL-3.0-or-later
// The multiplayer menu (ADR 0011): addresses, its screens at work, its texts in both languages.

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../test_framework.hpp"
#include "event_presenter.hpp"
#include "multiplayer_menu.hpp"
#include "multiplayer_texts.hpp"

#include "o1602/net/loopback.hpp"
#include "o1602/render/banner.hpp"
#include "o1602/ui/localization.hpp"

using namespace o1602;
using app::MpText;
using app::MultiplayerEvent;
using app::MultiplayerMenu;

namespace {

constexpr std::array<const ui::Font*, ui::kFontSlots> kNoFonts = {nullptr, nullptr, nullptr};

/// Where the text is drawn (its anchor in design pixels), if the screen shows it.
std::optional<std::pair<std::int32_t, std::int32_t>> find_text(MultiplayerMenu& menu,
                                                               const app::MultiplayerModel& model,
                                                               const std::string& text) {
    for (const ui::DrawItem& d : menu.draw(model, kNoFonts)) {
        if (d.kind == ui::DrawItem::Kind::Text && d.text == text) {
            return std::pair{d.x, d.y};
        }
    }
    return std::nullopt;
}

/// A click on the widget that shows `text` (just above its text's base line).
std::vector<MultiplayerEvent> click(MultiplayerMenu& menu, const app::MultiplayerModel& model,
                                    const std::string& text) {
    const auto at = find_text(menu, model, text);
    REQUIRE(at.has_value());
    (void)menu.mouse(model, at->first, at->second - 4, true);
    return menu.mouse(model, at->first, at->second - 4, false);
}

/// A click at a point of the design.
std::vector<MultiplayerEvent> click_at(MultiplayerMenu& menu, const app::MultiplayerModel& model, std::int32_t x,
                                       std::int32_t y) {
    (void)menu.mouse(model, x, y, true);
    return menu.mouse(model, x, y, false);
}

/// Puts English back for the tests that follow.
struct LanguageGuard {
    explicit LanguageGuard(std::string_view code) { ui::set_language(code); }
    ~LanguageGuard() { ui::set_language(ui::kFallbackLanguage); }
    LanguageGuard(const LanguageGuard&) = delete;
    LanguageGuard& operator=(const LanguageGuard&) = delete;
};

}  // namespace

TEST_CASE("multiplayer menu: addresses with and without a port") {
    const auto plain = app::parse_address(" 192.168.1.20 ");
    REQUIRE(plain.has_value());
    CHECK(plain->first == "192.168.1.20");
    CHECK(plain->second == net::kDefaultPort);
    const auto named = app::parse_address("anno.example.org:17000");
    REQUIRE(named.has_value());
    CHECK(named->first == "anno.example.org");
    CHECK(named->second == 17000);
    CHECK(!app::parse_address("").has_value());
    CHECK(!app::parse_address("host:").has_value());
    CHECK(!app::parse_address("host:0").has_value());
    CHECK(!app::parse_address("host:70000").has_value());
    CHECK(!app::parse_address("two words").has_value());
    CHECK(!app::parse_address(":16020").has_value());
}

TEST_CASE("multiplayer menu: every text in both languages, with its value") {
    const ui::Catalogue english = ui::builtin_catalogue("en");
    const ui::Catalogue german = ui::builtin_catalogue("de");
    for (std::size_t i = 0; i <= static_cast<std::size_t>(MpText::GameLoading); ++i) {
        const std::string_view key = app::mp_key(static_cast<MpText>(i));
        CHECK(english.find(key) != nullptr);
        CHECK(german.find(key) != nullptr);
    }
    CHECK(app::mp_text(MpText::PlayerJoined, "Anna") == "Anna joined.");
    CHECK(app::mp_text(MpText::HostLeftSaved, "hostleft.o1602").find("hostleft.o1602") != std::string::npos);
    CHECK(app::mp_text(MpText::HostMoved, "Anna").find("Anna") != std::string::npos);
    CHECK(!app::mp_text(net::RefuseReason::Kicked).empty());
    CHECK(!app::mp_text(net::EndReason::HostLeft).empty());
    const LanguageGuard german_game("de");
    CHECK(app::mp_text(MpText::PlayerJoined, "Anna") == "Anna ist beigetreten.");
    // The fonts' Windows-1252: "Zurück" with 0xFC.
    CHECK(app::mp_text(MpText::Back) ==
          "Zur\xFC"
          "ck");
    CHECK(app::mp_text(net::RefuseReason::Password) == "Falsches Kennwort.");
}

TEST_CASE("multiplayer menu: searching, joining by address and back") {
    const LanguageGuard german_game("de");
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    menu.open();
    CHECK(menu.screen() == MultiplayerMenu::Screen::Browse);
    CHECK(find_text(menu, model, app::mp_text(MpText::NoGamesFound)).has_value());
    CHECK(menu.name() == "Anna");

    // A game that answered the search: one click chooses it, the button joins it.
    net::FoundGame found;
    found.address = "192.168.1.20";
    found.info.title = "Annas Spiel";
    found.info.host = "Anna";
    found.info.players = 1;
    found.info.slots = 4;
    found.ping_ms = 12;
    model.found.push_back(found);
    CHECK(click(menu, model, "Annas Spiel").empty());
    auto events = click(menu, model, app::mp_text(MpText::Join));
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Join);
    CHECK(events[0].address == "192.168.1.20");
    CHECK(events[0].port == net::kDefaultPort);

    // An address typed in, with a password; Enter connects.
    CHECK(click_at(menu, model, 800, 166).empty());  // the address field
    CHECK(menu.typing());
    menu.type("10.0.0.7:17000");
    (void)menu.key(model, 9);  // Tab: the password
    menu.type("geheim");
    events = menu.key(model, 13);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Join);
    CHECK(events[0].address == "10.0.0.7");
    CHECK(events[0].port == 17000);
    CHECK(events[0].password == "geheim");
    CHECK(!menu.typing());

    // Escape: on the search it closes the menu.
    const auto back = menu.back();
    REQUIRE(back.has_value());
    CHECK(back->kind == MultiplayerEvent::Kind::Close);
}

TEST_CASE("multiplayer menu: a field keeps the keys when the mouse leaves it, until a click beside") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    menu.open();
    CHECK(click_at(menu, model, 800, 48).empty());  // the name field
    CHECK(menu.typing());

    // The mouse moves away, over the list and the buttons, without a button held.
    CHECK(menu.mouse(model, 600, 48, false).empty());
    CHECK(menu.mouse(model, 300, 400, false).empty());
    CHECK(menu.mouse(model, 800, 360, false).empty());  // over "Create game"
    CHECK(menu.typing());
    (void)menu.key(model, 8);
    menu.type("e");
    CHECK(menu.name() == "Anne");

    // A click beside the fields ends typing, and the name counts.
    auto events = click_at(menu, model, 400, 720);
    CHECK(!menu.typing());
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Name);
    CHECK(events[0].text == "Anne");

    // Enter ends it as before; the mouse moving away in between changes nothing.
    CHECK(click_at(menu, model, 800, 166).empty());  // the address field
    CHECK(menu.mouse(model, 100, 700, false).empty());
    menu.type("10.0.0.7");
    CHECK(menu.typing());
    events = menu.key(model, 13);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Join);
    CHECK(!menu.typing());

    // Escape too.
    CHECK(click_at(menu, model, 800, 166).empty());
    CHECK(menu.mouse(model, 100, 700, false).empty());
    CHECK(menu.typing());
    (void)menu.key(model, 27);
    CHECK(!menu.typing());
}

TEST_CASE("multiplayer menu: Ctrl+A selects the whole field, typing or Backspace replaces it") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    menu.open();
    CHECK(click_at(menu, model, 800, 48).empty());  // the name field
    REQUIRE(menu.typing());
    (void)menu.key(model, 1);  // Ctrl+A
    menu.type("Bernd");
    CHECK(menu.name() == "Bernd");
    menu.type("t");  // the selection is gone: typing adds again
    CHECK(menu.name() == "Berndt");

    (void)menu.key(model, 1);
    (void)menu.key(model, 8);  // Backspace takes the whole selection
    CHECK(menu.name().empty());
    menu.type("Carla");
    (void)menu.key(model, 8);  // and then one character, as before
    CHECK(menu.name() == "Carl");
}

TEST_CASE("multiplayer menu: the suggested title follows the name, a title of one's own stays") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    menu.open();
    CHECK(click(menu, model, "Create game").empty());
    CHECK(!menu.back().has_value());

    // The name changes after the title was suggested: the suggestion changes with it.
    CHECK(click_at(menu, model, 800, 48).empty());
    (void)menu.key(model, 1);
    menu.type("Bernd");
    (void)menu.key(model, 13);
    CHECK(click(menu, model, "Create game").empty());
    auto events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].setup.title == "Bernd's game");

    // A title typed by the person is kept, whatever the name.
    MultiplayerMenu own(ui::FormStyle{});
    own.open();
    CHECK(click(own, model, "Create game").empty());
    CHECK(click(own, model, "Anna's game").empty());  // the title field
    (void)own.key(model, 1);
    own.type("Abend");
    (void)own.key(model, 13);
    CHECK(!own.back().has_value());
    CHECK(click_at(own, model, 800, 48).empty());
    (void)own.key(model, 1);
    own.type("Carla");
    (void)own.key(model, 13);
    CHECK(click(own, model, "Create game").empty());
    events = click(own, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].setup.title == "Abend");
}

TEST_CASE("multiplayer menu: creating a game: endless, scenario, settings") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Bernd";
    model.level_names = {"Easy", "Medium", "Hard", "Very hard"};
    app::MenuScenario scenario;
    scenario.name = "Die Insel";
    scenario.files = {{0xFF, "Szenes/Insel.szs"}};
    model.scenarios.push_back(scenario);
    menu.open();
    CHECK(click(menu, model, app::mp_text(MpText::CreateGame)).empty());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Create);
    // Escape goes back to the search by itself: nothing for the viewer.
    CHECK(!menu.back().has_value());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Browse);
    CHECK(click(menu, model, "Create game").empty());

    CHECK(click(menu, model, "Hard").empty());
    CHECK(click(menu, model, "Host without a player").empty());
    auto events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Host);
    const app::HostSetup& endless = events[0].setup;
    CHECK(endless.kind == app::HostSetup::Kind::Endless);
    CHECK(endless.level == 2);
    CHECK(endless.dedicated);
    CHECK(endless.title == "Bernd's game");
    CHECK(endless.port == net::kDefaultPort);
    CHECK(endless.file_name.find("Hard") != std::string::npos);

    // A scenario must be chosen before the lobby opens.
    CHECK(click(menu, model, "Scenario").empty());
    CHECK(click(menu, model, "Open lobby").empty());  // not yet: nothing chosen
    CHECK(click(menu, model, "Die Insel").empty());
    events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].setup.kind == app::HostSetup::Kind::Scenario);
    CHECK(events[0].setup.file == std::filesystem::path("Szenes/Insel.szs"));
    CHECK(events[0].setup.file_name == "Die Insel");
}

TEST_CASE("multiplayer menu: the mouse wheel scrolls the scenarios of a game being created a row per notch") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Bernd";
    for (int i = 0; i < 20; ++i) {
        app::MenuScenario scenario;
        scenario.name = "Karte " + std::to_string(i);
        scenario.files = {{0xFF, "Szenes/Karte.szs"}};
        model.scenarios.push_back(scenario);
    }
    menu.open();
    CHECK(menu.wheel(model, -1));  // the search list: nothing found, nothing to scroll, no zoom
    CHECK(click(menu, model, "Create game").empty());
    CHECK(!menu.wheel(model, -1));  // the endless game has no list
    CHECK(click(menu, model, "Scenario").empty());
    CHECK(find_text(menu, model, "Karte 0").has_value());
    CHECK(menu.wheel(model, -2));  // down two rows
    CHECK(!find_text(menu, model, "Karte 1").has_value());
    CHECK(find_text(menu, model, "Karte 2").has_value());
    CHECK(menu.wheel(model, -50));  // not past the end: the last eleven rows
    CHECK(find_text(menu, model, "Karte 19").has_value());
    CHECK(find_text(menu, model, "Karte 9").has_value());
    CHECK(!find_text(menu, model, "Karte 8").has_value());
    CHECK(menu.wheel(model, 50));  // up, not before the first
    CHECK(find_text(menu, model, "Karte 0").has_value());
}

TEST_CASE("multiplayer menu: the mouse wheel scrolls the lobby's chat, which holds still for new lines") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.game = "Endless game";
    lobby.seats.resize(4);
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);

    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    const auto say = [&](int n) {
        app::ChatLine line;
        line.text = "Line " + std::to_string(n);
        app::add_chat_line(&model.chat, std::move(line), false);
    };
    for (int n = 0; n < 30; ++n) {
        say(n);
    }
    menu.show_lobby();
    // Four slots leave room for eleven lines: the newest show.
    CHECK(find_text(menu, model, "Line 29").has_value());
    CHECK(find_text(menu, model, "Line 19").has_value());
    CHECK(!find_text(menu, model, "Line 18").has_value());

    CHECK(menu.wheel(model, 3));  // back three lines
    CHECK(find_text(menu, model, "Line 16").has_value());
    CHECK(find_text(menu, model, "Line 26").has_value());
    CHECK(!find_text(menu, model, "Line 27").has_value());

    // A new line does not move what is being read.
    say(30);
    CHECK(find_text(menu, model, "Line 16").has_value());
    CHECK(!find_text(menu, model, "Line 27").has_value());

    CHECK(menu.wheel(model, 50));  // not before the oldest
    CHECK(find_text(menu, model, "Line 0").has_value());
    CHECK(find_text(menu, model, "Line 10").has_value());
    CHECK(!find_text(menu, model, "Line 11").has_value());

    CHECK(menu.wheel(model, -50));  // not past the newest
    CHECK(find_text(menu, model, "Line 30").has_value());
    CHECK(!find_text(menu, model, "Line 19").has_value());

    // Following the newest lines, a new one shows at once.
    say(31);
    CHECK(find_text(menu, model, "Line 31").has_value());

    // Saying something goes back to the newest lines.
    CHECK(menu.wheel(model, 5));
    CHECK(!find_text(menu, model, "Line 31").has_value());
    menu.type("Hallo");
    (void)menu.key(model, 13);
    CHECK(find_text(menu, model, "Line 31").has_value());

    // A cleared chat starts over at the newest lines.
    CHECK(menu.wheel(model, 5));
    model.chat.clear();
    for (int n = 0; n < 20; ++n) {
        say(n);
    }
    CHECK(find_text(menu, model, "Line 19").has_value());
}

TEST_CASE("multiplayer menu: the host's lobby: slots, chat and the start") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.game = "Endless game";
    lobby.seats.resize(4);
    lobby.setup = app::lobby_setup(app::EndlessSetup::defaults(app::SetupScope::MultiplayerEndless),
                                   app::SetupScope::MultiplayerEndless);
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);

    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    model.everyone_ready = host.everyone_ready();
    menu.show_lobby();
    CHECK(find_text(menu, model, "Abend").has_value());
    CHECK(find_text(menu, model, "Anna (you)").has_value());
    CHECK(menu.typing());  // the chat line takes the keys

    // The host clicks an open slot through the kinds: the computer plays an open one anyway, so
    // the other kind is a blocked slot nobody plays, and back.
    auto events = click(menu, model, "Open");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::SetSeat);
    CHECK(events[0].seat == 1);
    CHECK(events[0].seat_kind == net::LobbySeat::Kind::Closed);
    host.set_seat(1, events[0].seat_kind);
    events = click(menu, model, "Blocked");
    REQUIRE(events.size() == 1);
    CHECK(events[0].seat == 1);
    CHECK(events[0].seat_kind == net::LobbySeat::Kind::Open);
    host.set_seat(1, events[0].seat_kind);

    menu.type("Hallo");
    events = menu.key(model, 13);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Say);
    CHECK(events[0].text == "Hallo");
    CHECK(menu.typing());  // the chat line stays open

    events = click(menu, model, "Start game");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Start);

    // Escape twice: out of the chat line, then out of the session.
    (void)menu.key(model, 27);
    const auto back = menu.back();
    REQUIRE(back.has_value());
    CHECK(back->kind == MultiplayerEvent::Kind::Leave);
}

TEST_CASE("multiplayer menu: joining, the lobby shows how far the game on its way has come") {
    using Stage = net::ClientSession::JoinStage;
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.seats.resize(4);
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.session = &host;  // only its lobby is shown
    menu.show_lobby();
    // Without fonts a paragraph is one line, cut to the box: what it begins with.
    const auto shows = [&](std::string_view start) {
        const auto items = menu.draw(model, kNoFonts);
        return std::any_of(items.begin(), items.end(), [&](const ui::DrawItem& d) {
            return d.kind == ui::DrawItem::Kind::Text && d.text.starts_with(start);
        });
    };
    CHECK(shows("The host starts"));

    // The gold part of the bar (its frame is gold too, but higher and as wide as the bar).
    const auto bar_done = [&]() -> std::optional<std::int32_t> {
        for (const ui::DrawItem& d : menu.draw(model, kNoFonts)) {
            if (d.kind != ui::DrawItem::Kind::Text && d.colour == ui::FormStyle{}.frame && d.x == 696 && d.y == 616) {
                return d.w;
            }
        }
        return std::nullopt;
    };
    model.join = Stage::Waiting;
    CHECK(shows("The host is preparing"));
    CHECK(!shows("The host starts"));
    CHECK(!bar_done().has_value());

    model.join = Stage::Receiving;
    model.join_received = 10U << 20;
    model.join_total = (40U << 20) - 5;
    CHECK(find_text(menu, model, "Receiving the game ...").has_value());
    CHECK(find_text(menu, model, "10 of 40 MB").has_value());
    REQUIRE(bar_done().has_value());
    CHECK(*bar_done() == 67);  // a quarter of the 268 inside

    model.join = Stage::Loading;
    CHECK(find_text(menu, model, "Loading the game ...").has_value());
    CHECK(bar_done() == std::optional<std::int32_t>(268));

    const LanguageGuard german("de");
    model.join = Stage::Receiving;
    CHECK(find_text(menu, model,
                    "Das Spiel wird \xFC"
                    "bertragen ...")
              .has_value());
    CHECK(find_text(menu, model, "10 von 40 MB").has_value());
}

TEST_CASE("multiplayer menu: a lobby without a title is the host's game, in this computer's language") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.seats.resize(4);
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.session = &host;
    menu.show_lobby();
    CHECK(find_text(menu, model, "Anna's game").has_value());
    {
        const LanguageGuard german("de");
        CHECK(find_text(menu, model, "Spiel von Anna").has_value());
    }
    host.set_lobby_info("Abend", {}, {});
    CHECK(find_text(menu, model, "Abend").has_value());
    CHECK(!find_text(menu, model, "Anna's game").has_value());
}

TEST_CASE("multiplayer menu: the host sends a spectator away") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.game = "Endless game";
    lobby.seats.resize(1);  // only the host's: whoever comes watches
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    net::Hello hello;
    hello.content = 1;
    hello.name = "Wilma";
    net::ClientSession wilma(network.connect(), hello, [](std::span<const std::uint8_t>) { return nullptr; });
    for (int i = 0; i < 20; ++i) {
        host.update(50);
        wilma.update(50);
    }
    REQUIRE(host.roster().size() == 2);
    REQUIRE(host.roster()[1].seat == sim::kNoPlayer);

    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    menu.show_lobby();
    CHECK(find_text(menu, model, "Wilma").has_value());
    const auto events = click(menu, model, "Remove");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::KickMember);
    CHECK(events[0].member == 1);
    CHECK(events[0].seat == sim::kNoPlayer);  // the entry as shown (MS-18)
    CHECK(events[0].text == "Wilma");
}

TEST_CASE("multiplayer menu: four to twelve players, the factions' slots left out of the lobby") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Bernd";
    model.level_names = {"Easy", "Medium", "Hard", "Very hard"};
    menu.open();
    CHECK(click(menu, model, "Create game").empty());
    CHECK(find_text(menu, model, "4").has_value());
    CHECK(click(menu, model, "-").empty());  // never fewer than four
    CHECK(find_text(menu, model, "4").has_value());
    CHECK(click(menu, model, "+").empty());
    CHECK(click(menu, model, "+").empty());
    auto events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].setup.players == 6);
    CHECK(events[0].setup.file_name.find("6 players") != std::string::npos);

    // Six players: slots 0-3 and 7-8; the free trader's, pirates' and natives' are not shown.
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.seats = app::endless_seats(6);
    lobby.setup = app::lobby_setup(app::EndlessSetup::defaults(app::SetupScope::MultiplayerEndless),
                                   app::SetupScope::MultiplayerEndless);
    net::HostSession host(network.listen(), lobby, 0, "Bernd", 1);
    host.set_seat(5, net::LobbySeat::Kind::Open);  // the pirates' slot stays out of the game
    CHECK(host.lobby().seats[5].kind == net::LobbySeat::Kind::Reserved);
    model.session = &host;
    model.hosting = true;
    menu.show_lobby();
    std::size_t open = 0;
    for (const ui::DrawItem& d : menu.draw(model, kNoFonts)) {
        open += d.kind == ui::DrawItem::Kind::Text && d.text == "Open" ? 1U : 0U;
    }
    CHECK(open == 5);
    // The host clicks a slot between open and blocked.
    events = click(menu, model, "Open");
    REQUIRE(events.size() == 1);
    CHECK(events[0].seat_kind == net::LobbySeat::Kind::Closed);
}

TEST_CASE("multiplayer menu: only a new endless game blocks slots") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.seats.resize(4);
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    model.hosting = true;
    CHECK(!app::may_block_seats(lobby));  // no settings: nothing known of the game

    // A scenario's and a savegame's players are in their world: the host clicks a slot between
    // open and computer.
    for (const app::SetupScope scope : {app::SetupScope::Scenario, app::SetupScope::Savegame}) {
        lobby.setup = app::lobby_setup(app::EndlessSetup::defaults(scope), scope);
        CHECK(!app::may_block_seats(lobby));
        net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
        host.update(0);
        model.session = &host;
        menu.show_lobby();
        const auto events = click(menu, model, "Open");
        REQUIRE(events.size() == 1);
        CHECK(events[0].seat_kind == net::LobbySeat::Kind::Computer);
    }

    lobby.setup = app::lobby_setup(app::EndlessSetup::defaults(app::SetupScope::MultiplayerEndless),
                                   app::SetupScope::MultiplayerEndless);
    CHECK(app::may_block_seats(lobby));
    lobby.started = true;
    CHECK(!app::may_block_seats(lobby));  // the running game's slots stay as they are
}

TEST_CASE("multiplayer menu: the factions' slots are reserved for every number of players") {
    // Five players once took the free trader's slot for a computer, and six the pirates' too:
    // both were out of the game at the first check.
    for (std::size_t players = app::HostSetup::kMinPlayers; players <= sim::kMaxPlayers; ++players) {
        const std::vector<net::LobbySeat> seats = app::endless_seats(players);
        REQUIRE(seats.size() == std::size_t{sim::player_slot(players - 1)} + 1U);
        std::size_t playable = 0;
        for (std::size_t s = 0; s < seats.size(); ++s) {
            const bool faction = s >= sim::kOriginalPlayers && s < sim::kFirstExtraPlayer;
            CHECK((seats[s].kind == net::LobbySeat::Kind::Reserved) == faction);
            playable += faction ? 0U : 1U;
        }
        CHECK(playable == players);
    }
    CHECK(app::endless_seats(4).size() == 4);
    CHECK(app::endless_seats(5).size() == 8);
}

namespace {

/// The palette colour of banner 0 in the tests; the others follow (none is a colour of the form).
constexpr std::int32_t kFirstColour = 150;
/// The palette colour of banner 0's names in the chat (made legible); the others follow.
constexpr std::int32_t kFirstText = 200;

/// The menu's banners as the game gives them (Hud): the original's seven and the further colours.
std::vector<app::MenuBanner> menu_banners() {
    render::BannerTable table = render::BannerTable::original();
    table.add_more_players();
    std::vector<app::MenuBanner> banners;
    for (std::size_t i = 0; i < table.size(); ++i) {
        banners.push_back(app::MenuBanner{static_cast<std::uint8_t>(kFirstColour + i), table.banners()[i].id,
                                          static_cast<std::uint8_t>(kFirstText + i)});
    }
    return banners;
}

}  // namespace

TEST_CASE("multiplayer menu: a banner for every player, each slot its own") {
    const std::vector<sim::BannerId> offered = app::player_banners();
    REQUIRE(offered.size() == sim::kMaxPlayers);
    render::BannerTable table = render::BannerTable::original();
    table.add_more_players();
    for (std::size_t i = 0; i < offered.size(); ++i) {
        const render::Banner* b = table.get(offered[i]);
        REQUIRE(b != nullptr);
        CHECK(b->flag == render::FlagStyle::Tinted);  // a player's, not the pirates' or the natives'
        CHECK(std::count(offered.begin(), offered.end(), offered[i]) == 1);
    }
    // A new world's players have the banner of their slot (sim::add_players).
    const std::vector<net::LobbySeat> seats = app::endless_seats(sim::kMaxPlayers);
    for (std::size_t n = 0; n < sim::kMaxPlayers; ++n) {
        CHECK(seats[sim::player_slot(n)].banner == offered[n]);
    }
    CHECK(app::banner_name("anno1602:red") == "Red");
    CHECK(app::banner_name("open1602:magenta") == "Magenta");
    CHECK(app::banner_name("mymod:gold") == "gold");              // no text: its id
    CHECK(app::banner_name("open1602:colour13") == "Colour 13");  // generated beyond the twelve
    CHECK(app::banner_name("mymod:colours") == "colours");
    const ui::Catalogue english = ui::builtin_catalogue("en");
    const ui::Catalogue german = ui::builtin_catalogue("de");
    for (const render::Banner& b : table.banners()) {
        if (b.flag == render::FlagStyle::Tinted) {
            const std::string colour = b.id.substr(b.id.find(':') + 1);
            // Beyond the twelve hand-picked colours generated ones, named "Colour N".
            const std::string key =
                colour.rfind("colour", 0) == 0 ? std::string("net.colour.number") : "net.colour." + colour;
            CHECK(english.find(key) != nullptr);
            CHECK(german.find(key) != nullptr);
        }
    }
    const LanguageGuard german_game("de");
    CHECK(app::banner_name("anno1602:white") == "Wei\xDF");
    CHECK(app::banner_name("open1602:green") ==
          "Gr\xFC"
          "n");
}

TEST_CASE("multiplayer menu: choosing a banner in the lobby") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.seats = app::endless_seats(6);
    lobby.banners = app::player_banners();
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    net::Hello hello;
    hello.content = 1;
    hello.name = "Wilma";
    net::ClientSession wilma(network.connect(), hello, [](std::span<const std::uint8_t>) { return nullptr; });
    for (int i = 0; i < 20; ++i) {
        host.update(50);
        wilma.update(50);
    }
    REQUIRE(wilma.local_seat() == 1);

    MultiplayerMenu menu(ui::FormStyle{});
    menu.set_banners(menu_banners(), 1);
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    menu.show_lobby();
    // Each slot shows the colour of its banner.
    const auto colours = [&] {
        std::vector<std::int32_t> c;
        for (const ui::DrawItem& d : menu.draw(model, kNoFonts)) {
            if (d.kind == ui::DrawItem::Kind::Fill && d.colour >= kFirstColour && d.colour < kFirstColour + 20) {
                c.push_back(d.colour - kFirstColour);
            }
        }
        return c;
    };
    CHECK((colours() == std::vector<std::int32_t>{0, 1, 2, 3, 7, 8}));
    CHECK(!find_text(menu, model, "Green").has_value());

    // The host's own colour (the first slot's): the colours on offer show in place of the chat.
    CHECK(click_at(menu, model, 62, 112).empty());
    REQUIRE(menu.choosing_banner() == std::optional<std::size_t>(0));
    CHECK(find_text(menu, model, "Choose a banner").has_value());
    // Wilma's blue is hers: nothing happens.
    CHECK(click(menu, model, "Blue").empty());
    auto events = click(menu, model, "Green");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::SetBanner);
    CHECK(events[0].seat == 0);
    CHECK(events[0].banner == 8);
    CHECK(!menu.choosing_banner());
    CHECK(host.set_banner(events[0].seat, events[0].banner));
    CHECK((colours() == std::vector<std::int32_t>{8, 1, 2, 3, 7, 0}));  // the open slot took red

    // An open slot's too; not Wilma's (no button: a click there does not open the colours).
    CHECK(click_at(menu, model, 62, 112 + 36 * 2).empty());
    CHECK(menu.choosing_banner() == std::optional<std::size_t>(2));
    CHECK(!menu.back().has_value());  // Escape closes the colours first
    CHECK(!menu.choosing_banner());
    CHECK(click_at(menu, model, 62, 112 + 36).empty());
    CHECK(!menu.choosing_banner());

    // Wilma's menu: only her own slot.
    MultiplayerMenu theirs(ui::FormStyle{});
    theirs.set_banners(menu_banners(), 1);
    app::MultiplayerModel their_model;
    their_model.name = "Wilma";
    their_model.session = &wilma;
    theirs.show_lobby();
    for (int i = 0; i < 4; ++i) {
        host.update(50);
        wilma.update(50);
    }
    CHECK(theirs.choosing_banner() == std::nullopt);
    CHECK(click_at(theirs, their_model, 62, 112).empty());
    CHECK(!theirs.choosing_banner());
    CHECK(click_at(theirs, their_model, 62, 112 + 36).empty());
    CHECK(theirs.choosing_banner() == std::optional<std::size_t>(1));
    CHECK(click(theirs, their_model, "Green").empty());  // the host's now
    events = click(theirs, their_model, "Teal");
    REQUIRE(events.size() == 1);
    CHECK(events[0].banner == 9);
}

TEST_CASE("multiplayer menu: the lobby's chat shows each player's name in their colour") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.seats = app::endless_seats(4);
    lobby.banners = app::player_banners();
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);

    MultiplayerMenu menu(ui::FormStyle{});
    menu.set_banners(menu_banners(), 1);
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    const auto say = [&](std::string name, std::string text, sim::PlayerId seat, bool whisper = false) {
        app::ChatLine line;
        line.name = std::move(name);
        line.text = std::move(text);
        line.seat = seat;
        line.whisper = whisper;
        line.to = whisper ? "to you" : "";
        app::add_chat_line(&model.chat, std::move(line), false);
    };
    say("Anna", "Hallo", 0);
    say("Bernd", "Psst", 2, true);
    say("Zoe", "Moin", sim::kNoPlayer);  // a spectator
    app::ChatLine notice;
    notice.text = "Bernd joined";
    app::add_chat_line(&model.chat, std::move(notice), false);
    menu.show_lobby();

    const std::vector<ui::DrawItem> items = menu.draw(model, kNoFonts);
    const auto text = [&](const std::string& t) -> const ui::DrawItem* {
        const auto it = std::find_if(items.begin(), items.end(), [&](const ui::DrawItem& d) {
            return d.kind == ui::DrawItem::Kind::Text && d.text == t;
        });
        return it != items.end() ? &*it : nullptr;
    };
    const ui::FormStyle style;
    // The name in the banner's colour, the rest right behind it in the line's.
    const ui::DrawItem* anna = text("Anna");
    const ui::DrawItem* hallo = text(": Hallo");
    REQUIRE(anna != nullptr);
    REQUIRE(hallo != nullptr);
    CHECK(anna->ink == kFirstText + 0);
    CHECK(anna->colour == style.text);
    CHECK(hallo->ink == -1);
    CHECK(hallo->colour == style.text);
    CHECK(hallo->y == anna->y);
    CHECK(hallo->x == anna->x + 4 * 9);  // without a font every byte is 9 units wide
    // A whisper keeps its colour behind the name.
    const ui::DrawItem* bernd = text("Bernd");
    const ui::DrawItem* psst = text(" (to you): Psst");
    REQUIRE(bernd != nullptr);
    REQUIRE(psst != nullptr);
    CHECK(bernd->ink == kFirstText + 2);
    CHECK(psst->colour == style.heading);
    // A spectator has no colour, a notice no name.
    REQUIRE(text("Zoe: Moin") != nullptr);
    CHECK(text("Zoe: Moin")->ink == -1);
    REQUIRE(text("Bernd joined") != nullptr);
    CHECK(text("Bernd joined")->ink == -1);
}

TEST_CASE("multiplayer menu: a long line in the lobby's chat is wrapped, not cut") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.seats = app::endless_seats(4);
    lobby.banners = app::player_banners();
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);

    MultiplayerMenu menu(ui::FormStyle{});
    menu.set_banners(menu_banners(), 1);
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    const auto say = [&](std::string name, std::string text) {
        app::ChatLine line;
        line.name = std::move(name);
        line.text = std::move(text);
        line.seat = line.name.empty() ? sim::kNoPlayer : 0;
        app::add_chat_line(&model.chat, std::move(line), false);
    };
    for (int n = 0; n < 20; ++n) {
        say({}, "Line " + std::to_string(n));
    }
    // Without a font every byte is 9 units wide: a row of the chat holds 65 bytes.
    const std::string first = "Wer baut die erste Kirche auf der grossen Insel im Norden, ehe";
    const std::string second = "der Winter kommt und die Schiffe der Piraten wieder auslaufen?";
    say("Anna", first.substr(4) + " " + second);
    menu.show_lobby();

    const auto texts = [&] {
        std::vector<std::string> out;
        for (const ui::DrawItem& d : menu.draw(model, kNoFonts)) {
            if (d.kind == ui::DrawItem::Kind::Text) {
                out.push_back(d.text);
            }
        }
        return out;
    };
    const auto shows = [&](const std::string& t) {
        const std::vector<std::string> all = texts();
        return std::find(all.begin(), all.end(), t) != all.end();
    };
    // The name in its colour on the first row, the rest of the line on the next; nothing is cut.
    CHECK(shows("Anna"));
    CHECK(shows(": " + first.substr(4)));
    CHECK(shows(second));
    for (const std::string& t : texts()) {
        CHECK(
            !(t.ends_with("...") && (t.find("Kirche") != std::string::npos || t.find("Winter") != std::string::npos)));
    }
    // The newest two rows are the long line's; four slots leave room for eleven rows.
    CHECK(shows("Line 19"));
    CHECK(shows("Line 11"));
    CHECK(!shows("Line 10"));

    // The wheel goes back by rows: one notch shows the long line's first row at the bottom.
    CHECK(menu.wheel(model, 1));
    CHECK(shows("Line 10"));
    CHECK(shows(": " + first.substr(4)));
    CHECK(!shows(second));
    // A new long line does not move what is being read.
    say("Anna", first + " " + second);
    CHECK(shows("Line 10"));
    CHECK(!shows(second));
    // Back to the newest rows.
    CHECK(menu.wheel(model, -50));
    CHECK(shows(": " + first.substr(4)));
    CHECK(shows(second));
    CHECK(!shows("Line 10"));
    CHECK(!shows("Line 9"));
}

TEST_CASE("multiplayer menu: the host's settings travel in the lobby") {
    app::EndlessSetup settings = app::EndlessSetup::defaults(app::SetupScope::MultiplayerEndless);
    app::choose(settings, app::SetupItem::ClassicAi, 1);
    app::choose(settings, app::SetupItem::Map, 1);
    const std::vector<std::uint8_t> bytes = app::lobby_setup(settings, app::SetupScope::MultiplayerEndless);
    const auto back = app::read_lobby_setup(bytes);
    REQUIRE(back.has_value());
    CHECK(back->first == settings);
    CHECK(back->second == app::SetupScope::MultiplayerEndless);
    CHECK(app::read_lobby_setup(app::lobby_setup(settings, app::SetupScope::Scenario))->second ==
          app::SetupScope::Scenario);
    // Another program's setup, or none: no settings to show.
    const std::string other = "map=Insel";
    CHECK(!app::read_lobby_setup(std::span(reinterpret_cast<const std::uint8_t*>(other.data()), other.size())));
    CHECK(!app::read_lobby_setup({}));
}

namespace {

/// What the preview of a multiplayer game was asked for: the world's number and the level.
struct PreviewAsked {
    std::mutex mutex;
    std::vector<std::pair<std::uint64_t, std::uint8_t>> worlds;
};

app::GameSettingsMenu::PreviewMaker recording_maker(const std::shared_ptr<PreviewAsked>& asked) {
    return [asked](const app::EndlessSetup& setup, std::uint8_t level) -> core::Result<sim::WorldPreview> {
        {
            const std::lock_guard lock(asked->mutex);
            asked->worlds.emplace_back(setup.world_seed, level);
        }
        sim::WorldPreview p;
        p.width = 20;
        p.height = 20;
        p.tiles.assign(400, sim::WorldPreview::Tile::Sea);
        return p;
    };
}

std::pair<std::uint64_t, std::uint8_t> last_asked(const std::shared_ptr<PreviewAsked>& asked) {
    const std::lock_guard lock(asked->mutex);
    REQUIRE(!asked->worlds.empty());
    return asked->worlds.back();
}

}  // namespace

TEST_CASE("multiplayer menu: the preview of the islands shows the world of the host's number") {
    MultiplayerMenu menu(ui::FormStyle{});
    const auto asked = std::make_shared<PreviewAsked>();
    menu.set_preview_maker(recording_maker(asked));
    app::MultiplayerModel model;
    model.name = "Bernd";
    model.level_names = {"Easy", "Normal", "Hard", "Very hard"};
    menu.open();
    (void)click(menu, model, "Create game");
    (void)click(menu, model, "Hard");
    (void)click(menu, model, "Game settings ...");
    REQUIRE(menu.screen() == MultiplayerMenu::Screen::Settings);
    (void)click(menu, model, ui::tr("setup.island_page.open", {ui::tr("setup.island_preset.original")}));
    REQUIRE(menu.settings_dialog().islands_page());
    menu.settings_dialog().wait_preview();
    REQUIRE(menu.settings_dialog().preview().has_value());
    const auto [typed, level] = last_asked(asked);
    CHECK(level == 2);
    CHECK(typed < app::kMultiplayerSeeds);
    CHECK(find_text(menu, model, ui::tr("setup.preview.seed", {std::to_string(typed)})).has_value());

    // Another world: a number the host could have typed, which the game then gets.
    std::uint64_t other = typed;
    for (int i = 0; i < 8 && other == typed; ++i) {
        (void)click(menu, model, ui::tr("setup.preview.reroll"));
        other = menu.settings_dialog().setup().world_seed;
    }
    CHECK(other != typed);
    CHECK(other < app::kMultiplayerSeeds);
    menu.settings_dialog().wait_preview();
    CHECK(last_asked(asked).first == other);
    (void)click(menu, model, "Apply");
    REQUIRE(menu.screen() == MultiplayerMenu::Screen::Create);
    const auto events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    CHECK(events[0].setup.seed == other);
    CHECK(events[0].setup.level == 2);
}

TEST_CASE("multiplayer menu: in the lobby everyone's preview shows the host's world, which stays") {
    app::EndlessSetup settings = app::EndlessSetup::defaults(app::SetupScope::MultiplayerEndless);
    settings.world_seed = 4711;
    const std::vector<std::uint8_t> bytes = app::lobby_setup(settings, app::SetupScope::MultiplayerEndless, 3);
    const auto back = app::read_lobby_setup(bytes);
    REQUIRE(back.has_value());
    CHECK(back->first.world_seed == 4711);
    CHECK(app::read_lobby_level(bytes) == 3);
    CHECK(app::read_lobby_level({}) == 1);

    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.game = "Endless game";
    lobby.seats.resize(4);
    lobby.setup = bytes;
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);
    MultiplayerMenu menu(ui::FormStyle{});
    const auto asked = std::make_shared<PreviewAsked>();
    menu.set_preview_maker(recording_maker(asked));
    app::MultiplayerModel model;
    model.name = "Bernd";
    model.session = &host;
    menu.show_lobby();
    (void)click(menu, model, "Show game settings");
    (void)click(menu, model, ui::tr("setup.island_page.open", {ui::tr("setup.island_preset.original")}));
    REQUIRE(menu.settings_dialog().islands_page());
    menu.settings_dialog().wait_preview();
    CHECK((last_asked(asked) == std::pair<std::uint64_t, std::uint8_t>{4711, 3}));
    (void)click(menu, model, ui::tr("setup.preview.reroll"));
    CHECK(menu.settings_dialog().setup().world_seed == 4711);
}

TEST_CASE("multiplayer menu: the host chooses the settings of the game") {
    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Bernd";
    model.settings.rules.max_army = 500;  // the host's last choice
    menu.open();
    (void)click(menu, model, "Create game");
    CHECK(find_text(menu, model, "Rules: Custom settings").has_value());
    CHECK(click(menu, model, "Game settings ...").empty());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Settings);
    CHECK(!menu.typing());
    CHECK(find_text(menu, model, ui::tr("setup.title.multiplayer")).has_value());
    (void)click(menu, model, ui::tr("setup.defaults"));
    (void)click(menu, model, ui::tr("setup.bug.classic_ai.fixed"));
    CHECK(click(menu, model, "Apply").empty());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Create);
    // Escape out of the dialog keeps what was there.
    (void)click(menu, model, "Game settings ...");
    CHECK(!menu.back().has_value());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Create);

    auto events = click(menu, model, "Open lobby");
    REQUIRE(events.size() == 1);
    const app::HostSetup& setup = events[0].setup;
    CHECK(setup.settings.rules.bugs.classic_ai);
    CHECK(setup.settings.rules.max_army == 250);
    CHECK(setup.scope() == app::SetupScope::MultiplayerEndless);
    CHECK(setup.game_settings().world.players == 4);

    // A savegame keeps its own: no button.
    (void)click(menu, model, "Savegame");
    CHECK(!find_text(menu, model, "Game settings ...").has_value());
}

TEST_CASE("multiplayer menu: in the lobby the host changes the settings, the others look") {
    net::LoopbackNetwork network;
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.game = "Endless game";
    lobby.seats.resize(4);
    lobby.setup = app::lobby_setup(app::EndlessSetup::defaults(app::SetupScope::MultiplayerEndless),
                                   app::SetupScope::MultiplayerEndless);
    net::HostSession host(network.listen(), lobby, 0, "Anna", 1);
    host.update(0);

    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.name = "Anna";
    model.session = &host;
    model.hosting = true;
    menu.show_lobby();
    CHECK(find_text(menu, model, "Rules: Open1602 defaults").has_value());
    CHECK(click(menu, model, "Game settings ...").empty());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Settings);
    (void)click(menu, model, ui::tr("setup.original"));
    auto events = click(menu, model, "Apply");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::Settings);
    CHECK(events[0].setup.settings.rules == sim::Rules::original());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Lobby);
    CHECK(menu.typing());  // the chat line again

    // Someone who joined sees the host's settings and cannot change them.
    model.hosting = false;
    CHECK(click(menu, model, "Show game settings").empty());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Settings);
    CHECK(!find_text(menu, model, "Apply").has_value());
    CHECK(click(menu, model, ui::tr("setup.back")).empty());
    CHECK(menu.screen() == MultiplayerMenu::Screen::Lobby);
}

TEST_CASE("multiplayer menu: while the game starts, everyone in it with how far they are, scrolled when many") {
    // A computer in a started lobby of twelve players and two spectators, whose host still waits
    // for some of them (net::Roster::starting).
    net::LoopbackNetwork network(5, 0);
    auto host = network.listen();
    net::Hello hello;
    hello.name = "P1";
    hello.content = 1;
    net::ClientSession client(network.connect(), hello, [](std::span<const std::uint8_t>) { return nullptr; });
    net::Lobby lobby;
    lobby.title = "Abend";
    lobby.started = true;
    lobby.seats.resize(12);
    net::Roster roster;
    roster.starting = true;
    for (std::size_t s = 0; s < 12; ++s) {
        const std::string name = "P" + std::to_string(s);
        lobby.seats[s].kind = net::LobbySeat::Kind::Human;
        lobby.seats[s].name = name;
        lobby.seats[s].banner = static_cast<sim::BannerId>(s);
        // The host ready, then waiting, receiving, loading and ready in turn.
        const auto state = s % 4 == 0 ? net::Member::State::Playing : net::Member::State::Loading;
        const std::uint8_t loaded = s % 4 == 1 ? 0 : s % 4 == 2 ? 45 : 100;
        roster.members.push_back(net::Member{static_cast<sim::PlayerId>(s), name, state, s == 0, loaded});
    }
    roster.members.push_back(net::Member{sim::kNoPlayer, "W1", net::Member::State::Loading, false, 10});
    roster.members.push_back(net::Member{sim::kNoPlayer, "W2", net::Member::State::Playing, false, 100});
    std::optional<net::LinkId> link;
    for (int i = 0; i < 20; ++i) {
        network.advance(16);
        std::vector<net::TransportEvent> events;
        host->poll(events);
        for (const auto& e : events) {
            if (e.kind == net::TransportEvent::Kind::Received && !link) {
                link = e.link;
                host->send(e.link, net::encode(net::Welcome{net::kProtocolVersion, 1, 7}));
                host->send(e.link, net::encode(lobby));
                host->send(e.link, net::encode(roster));
            }
        }
        client.update(16);
    }
    REQUIRE(client.starting());
    REQUIRE(client.roster().size() == 14);

    MultiplayerMenu menu(ui::FormStyle{});
    app::MultiplayerModel model;
    model.session = &client;
    menu.show_lobby();
    CHECK(find_text(menu, model, "P0 (host)").has_value());
    CHECK(find_text(menu, model, "P1 (you)").has_value());
    CHECK(find_text(menu, model, "Waiting").has_value());
    CHECK(find_text(menu, model, "45%").has_value());
    CHECK(find_text(menu, model, "Loading").has_value());
    CHECK(find_text(menu, model, "Ready").has_value());
    CHECK(find_text(menu, model, "3 of 12 players ready").has_value());  // P0, P4 and P8
    CHECK(find_text(menu, model, "The game is starting").has_value());
    // No slots to take, no ready button and no start: the game has started.
    CHECK(!find_text(menu, model, "Take").has_value());
    CHECK(!find_text(menu, model, "Stop waiting").has_value());
    // Ten rows: the rest by the button, or the wheel over the list.
    CHECK(!find_text(menu, model, "W2 (spectator)").has_value());
    CHECK(!find_text(menu, model, "-").has_value());
    (void)click(menu, model, "+");
    CHECK(find_text(menu, model, "W1 (spectator)").has_value());
    CHECK(find_text(menu, model, "W2 (spectator)").has_value());
    CHECK(!find_text(menu, model, "P0 (host)").has_value());
    CHECK(!find_text(menu, model, "+").has_value());
    (void)click(menu, model, "-");
    CHECK(find_text(menu, model, "P0 (host)").has_value());
    (void)menu.mouse(model, 200, 150, false);  // the mouse over the list
    CHECK(menu.wheel(model, -2));
    CHECK(!find_text(menu, model, "P1 (you)").has_value());
    CHECK(find_text(menu, model, "P2").has_value());

    // The host may stop waiting.
    model.hosting = true;
    const auto events = click(menu, model, "Stop waiting");
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == MultiplayerEvent::Kind::StartNow);

    // In German.
    const LanguageGuard german("de");
    CHECK(find_text(menu, model, "Das Spiel startet").has_value());
    CHECK(find_text(menu, model, "3 von 12 Spielern bereit").has_value());
    CHECK(find_text(menu, model, "45 %").has_value());
    CHECK(find_text(menu, model, "Nicht mehr warten").has_value());
}
