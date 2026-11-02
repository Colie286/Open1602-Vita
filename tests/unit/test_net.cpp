// SPDX-License-Identifier: GPL-3.0-or-later
// The multiplayer (ADR 0011): messages as bytes and back, and whole sessions of a host and other
// computers over the in-memory network, with latency, jitter, desyncs, lost connections, slow
// computers and players who try to give orders for others.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/net/enet_transport.hpp"
#include "o1602/net/lan.hpp"
#include "o1602/net/loopback.hpp"
#include "o1602/net/protocol.hpp"
#include "o1602/net/session.hpp"
#include "o1602/sim/command_io.hpp"
#include "o1602/sim/random.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/ship.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

constexpr std::uint64_t kContent = 0x1602;
constexpr std::uint32_t kStepMs = 16;  // a frame of the program at about 60 per second

const assets::BuildingTable& buildings() {
    static const assets::BuildingTable table;
    return table;
}

const assets::FigureTable& figures() {
    static const assets::FigureTable table;
    return table;
}

/// One computer's game: the host builds it, the others load it from the host's savegame.
struct Game {
    std::unique_ptr<sim::World> world;
    std::unique_ptr<sim::Simulation> simulation;

    static Game fresh(std::size_t humans = 3) {
        Game g;
        g.world = std::make_unique<sim::World>(64, 64);
        std::vector<sim::Player> players(humans + 1);
        for (std::size_t p = 0; p < players.size(); ++p) {
            players[p].role = p < humans ? sim::PlayerRole::Human : sim::PlayerRole::Computer;
            players[p].banner = static_cast<sim::BannerId>(p);
            players[p].name = "Spieler " + std::to_string(p);
            players[p].money = 1000;
        }
        g.world->set_players(players);
        for (std::size_t p = 0; p < humans; ++p) {
            // A ship each, so that nobody is out of the game for having nothing (200 s).
            g.world->add_ship(sim::make_ship("Schiff", 10 + 4 * static_cast<std::uint32_t>(p), 10, 21,
                                             static_cast<sim::PlayerId>(p)));
        }
        g.simulation = std::make_unique<sim::Simulation>(*g.world, buildings(), figures());
        return g;
    }

    sim::Simulation* load(std::span<const std::uint8_t> save) {
        auto loaded = sim::read_game(save, buildings(), nullptr, &figures());
        if (!loaded) {
            return nullptr;
        }
        ++loads;
        if (diverge) {
            loaded.value().player(0)->money += 1;  // as a computer with another build would compute
        }
        simulation.reset();  // it refers to the world
        world = std::make_unique<sim::World>(std::move(loaded).value());
        simulation = std::make_unique<sim::Simulation>(*world, buildings(), figures());
        return simulation.get();
    }

    /// The world a host makes from its lobby when it starts: a player per slot, a ship for each
    /// human one, closed slots out of the game.
    static Game from_lobby(const net::Lobby& lobby) {
        Game g;
        g.world = std::make_unique<sim::World>(64, 64);
        std::vector<sim::Player> players(lobby.seats.size());
        for (std::size_t p = 0; p < players.size(); ++p) {
            const net::LobbySeat& seat = lobby.seats[p];
            players[p].role =
                seat.kind == net::LobbySeat::Kind::Computer ? sim::PlayerRole::Computer : sim::PlayerRole::Human;
            players[p].status =
                seat.kind == net::LobbySeat::Kind::Closed ? sim::PlayerStatus::Absent : sim::PlayerStatus::Playing;
            // The banners chosen in the lobby, as the program gives them to the players.
            players[p].banner = lobby.banners.empty() ? static_cast<sim::BannerId>(p) : seat.banner;
            players[p].name = seat.name;
            players[p].money = 1000;
        }
        g.world->set_players(players);
        for (std::size_t p = 0; p < players.size(); ++p) {
            if (lobby.seats[p].kind != net::LobbySeat::Kind::Closed) {
                g.world->add_ship(sim::make_ship("Schiff", 10 + 4 * static_cast<std::uint32_t>(p), 10, 21,
                                                 static_cast<sim::PlayerId>(p)));
            }
        }
        g.simulation = std::make_unique<sim::Simulation>(*g.world, buildings(), figures());
        return g;
    }

    int loads = 0;
    bool diverge = false;
};

/// A computer joining the host: its game and its session.
struct Computer {
    Game game;
    std::unique_ptr<net::ClientSession> session;
    const net::Transport* transport = nullptr;  ///< owned by the session; to cut the connection
    std::vector<net::SessionEvent> events;

    Computer(net::LoopbackNetwork& network, std::string name, std::uint64_t token = 0, std::uint64_t content = kContent,
             net::SessionConfig config = {}, std::string password = {},
             std::vector<assets::ContentPackage> packages = {}, bool fetch_packages = false, std::uint64_t player = 0) {
        net::Hello hello;
        hello.player = player;
        hello.content = content;
        hello.packages = std::move(packages);
        hello.fetch_packages = fetch_packages;
        hello.name = std::move(name);
        hello.token = token;
        hello.password = std::move(password);
        auto link = network.connect();
        transport = link.get();
        session = std::make_unique<net::ClientSession>(
            std::move(link), hello, [this](std::span<const std::uint8_t> save) { return game.load(save); }, config);
    }

    /// A person with the player ID of their installation (net::Hello::player).
    struct Person {
        std::uint64_t player = 0;
    };
    Computer(net::LoopbackNetwork& network, std::string name, Person person, std::uint64_t token = 0)
        : Computer(network, std::move(name), token, kContent, {}, {}, {}, false, person.player) {}

    void update(std::uint32_t ms, const sim::Simulation::BeforeTick& before = {}) {
        session->update(ms, before);
        for (auto& e : session->take_events()) {
            events.push_back(std::move(e));
        }
    }

    [[nodiscard]] std::size_t count(net::SessionEvent::Kind kind) const {
        std::size_t n = 0;
        for (const auto& e : events) {
            n += e.kind == kind ? 1 : 0;
        }
        return n;
    }
};

struct Host {
    Game game = Game::fresh();
    std::unique_ptr<net::HostSession> session;
    std::vector<net::SessionEvent> events;

    explicit Host(net::LoopbackNetwork& network, net::SessionConfig config = {}, Game g = Game::fresh())
        : game(std::move(g)) {
        session = std::make_unique<net::HostSession>(network.listen(), *game.simulation, buildings(), 0, "Gastgeber",
                                                     kContent, config);
    }

    void update(std::uint32_t ms, const sim::Simulation::BeforeTick& before = {}) {
        session->update(ms, before);
        for (auto& e : session->take_events()) {
            events.push_back(std::move(e));
        }
    }

    [[nodiscard]] std::size_t count(net::SessionEvent::Kind kind) const {
        std::size_t n = 0;
        for (const auto& e : events) {
            n += e.kind == kind ? 1 : 0;
        }
        return n;
    }
};

/// Time passes for the network and every computer.
void step(net::LoopbackNetwork& network, Host& host, const std::vector<Computer*>& computers,
          std::uint32_t ms = kStepMs) {
    network.advance(ms);
    host.update(ms);
    for (Computer* c : computers) {
        c->update(ms);
    }
}

void run_for(net::LoopbackNetwork& network, Host& host, const std::vector<Computer*>& computers,
             std::uint32_t total_ms) {
    for (std::uint32_t t = 0; t < total_ms; t += kStepMs) {
        step(network, host, computers);
    }
}

/// The fingerprint each computer's game had after `frame` frames (taken just before the next one).
struct FrameProbe {
    std::uint64_t frame;
    std::optional<std::uint64_t> hash;

    sim::Simulation::BeforeTick at(const net::Session& session, const Game& game) {
        return [this, &session, &game] {
            if (session.frames() == frame && !hash) {
                hash = game.simulation->state_hash();
            }
            return true;
        };
    }
};

/// The host left: `c` follows the successor the host named, on `next` (host migration).
void follow(net::LoopbackNetwork& next, Computer& c) {
    REQUIRE(c.session->can_migrate());
    REQUIRE(!c.session->hosts_next());
    auto session = std::make_unique<net::ClientSession>(*c.session, [&next] { return next.connect(); });
    c.session = std::move(session);
    c.transport = nullptr;
}

/// Time passes after the host changed: the new host (the successor's game) and its computers.
void step_moved(net::LoopbackNetwork& next, net::HostSession& host, const std::vector<Computer*>& computers,
                std::uint32_t total_ms) {
    for (std::uint32_t t = 0; t < total_ms; t += kStepMs) {
        next.advance(kStepMs);
        host.update(kStepMs);
        (void)host.take_events();
        for (Computer* c : computers) {
            c->update(kStepMs);
        }
    }
}

/// The computers hear that the host is gone.
void notice_host_left(net::LoopbackNetwork& network, const std::vector<Computer*>& computers) {
    for (int i = 0; i < 50; ++i) {
        network.advance(kStepMs);
        for (Computer* c : computers) {
            c->update(kStepMs);
        }
    }
    for (Computer* c : computers) {
        REQUIRE(c->session->ended());
        CHECK(c->session->end_reason() == net::EndReason::HostLeft);
    }
}

std::string random_name(sim::Random& r) {
    std::string name = "N";
    const std::uint32_t n = 1 + r.below(12);
    for (std::uint32_t i = 0; i < n; ++i) {
        name.push_back(static_cast<char>('a' + r.below(26)));
    }
    return name;
}

}  // namespace

TEST_CASE("net: every message goes to bytes and back") {
    std::vector<net::Message> all;
    all.emplace_back(net::Hello{net::kProtocolVersion, 77, "Merlin", 2, 1234, "geheim", false, {}});
    all.emplace_back(net::Hello{net::kProtocolVersion, 77, "Merlin", 2, 1234, "", true, {}});
    all.emplace_back(net::Hello{net::kProtocolVersion,
                                77,
                                "Merlin",
                                2,
                                0,
                                "",
                                false,
                                {{"lighthouse", "1.2", 0xFEDC}, {"\xc3\xa4rger", "", 1}}});
    all.emplace_back(net::CommandBatch{{sim::PlayerRename{1, "Zweiter"}, sim::GameSpeedOrder{1, {}, 4}}});
    all.emplace_back(net::Progress{3, 1000, net::FrameHash{900, 0xABCDEF}});
    all.emplace_back(net::Progress{1, 5, std::nullopt});
    all.emplace_back(net::Welcome{net::kProtocolVersion, 2, 99});
    all.emplace_back(net::Refused{net::RefuseReason::Content, {}});
    all.emplace_back(net::Snapshot{2, 400, 4, true, {1, 2, 3, 4, 5}});
    net::Turn turn;
    turn.first = 10;
    turn.closed = 20;
    turn.lead = 3;
    turn.frames = {net::FrameCommands{12, {sim::PlayerRename{0, "A"}}},
                   net::FrameCommands{19, {sim::PlayerRename{1, "B"}, sim::PlayerRename{2, "C"}}}};
    all.emplace_back(turn);
    all.emplace_back(net::Roster{{net::Member{0, "Gastgeber", net::Member::State::Playing, true},
                                  net::Member{sim::kNoPlayer, "Zuschauer", net::Member::State::Lagging, false}},
                                 1,
                                 "192.168.1.20"});
    all.emplace_back(net::Roster{{net::Member{0, "Gastgeber", net::Member::State::Playing, true, 100},
                                  net::Member{1, "Anna", net::Member::State::Loading, false, 42}},
                                 1,
                                 "",
                                 true});
    all.emplace_back(net::LoadProgress{57});
    all.emplace_back(net::Desync{300, {1, 2}});
    net::Lobby lobby;
    lobby.title = "Merlins Inselwelt";
    lobby.game = "Endlosspiel, Stufe 2";
    lobby.seats = {net::LobbySeat{net::LobbySeat::Kind::Human, "Merlin", true, 0},
                   net::LobbySeat{net::LobbySeat::Kind::Open, "", false, 7},
                   net::LobbySeat{net::LobbySeat::Kind::Computer, "", true, 2},
                   net::LobbySeat{net::LobbySeat::Kind::Closed, "", false, 0x1234}};
    lobby.password = true;
    lobby.setup = {9, 8, 7};
    lobby.banners = {0, 1, 2, 3, 7, 8, 0x1234};
    lobby.computer_takes_over = false;
    lobby.host_controls_speed = true;
    lobby.spectators_chat = false;
    all.emplace_back(lobby);
    all.emplace_back(net::TakeSeat{3});
    all.emplace_back(net::SetReady{true});
    all.emplace_back(net::Chat{1, "Anna", "Hallo!", true, {}, {}});
    all.emplace_back(net::Chat{1, "Anna", "Nur fuer dich", false, {0, 2}, {3}});
    all.emplace_back(net::Refused{net::RefuseReason::Kicked, {}});
    all.emplace_back(net::Refused{net::RefuseReason::Packages, {{"lighthouse", "1.2", 0xFEDC}}, 12345});
    all.emplace_back(net::PackageFiles{{"lighthouse", "1.2", 0xFEDC}, 1, 3, {{"mod.toml", {1, 2}}, {"gfx/a.png", {}}}});
    all.emplace_back(net::Standby{{net::SeatToken{1, 0xABCDEF, 0x1234567890ULL}, net::SeatToken{3, 5, 7}}, {3}});
    all.emplace_back(net::SetBanner{0x0809});
    for (const net::Message& m : all) {
        const auto bytes = net::encode(m);
        const auto back = net::decode(bytes);
        REQUIRE(back.has_value());
        CHECK(back->index() == m.index());
        CHECK(net::encode(*back) == bytes);  // same bytes: every field came back
    }
    const auto decoded = net::decode(net::encode(turn));
    const auto& t = std::get<net::Turn>(*decoded);
    REQUIRE(t.frames.size() == 2);
    CHECK(t.frames[1].frame == 19);
    CHECK(std::get<sim::PlayerRename>(t.frames[1].commands[1]).name == "C");
}

TEST_CASE("net: a load progress beyond 100 percent is no message") {
    auto bytes = net::encode(net::LoadProgress{100});
    CHECK(net::decode(bytes).has_value());
    bytes.back() = 101;
    CHECK(!net::decode(bytes).has_value());
    auto roster =
        net::encode(net::Roster{{net::Member{0, "A", net::Member::State::Loading, false, 100}}, 0, "", false});
    CHECK(net::decode(roster).has_value());
    // The member's percent comes after its seat, name (length and bytes), state and host flag.
    roster[4 + 1 + 4 + 1 + 1 + 1 + 1] = 200;
    CHECK(!net::decode(roster).has_value());
}

TEST_CASE("net: damaged or hostile bytes give no message") {
    net::Turn turn;
    turn.first = 10;
    turn.closed = 20;
    turn.frames = {net::FrameCommands{12, {sim::PlayerRename{0, "A"}}}};
    const auto bytes = net::encode(turn);
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CHECK(!net::decode(std::span(bytes.data(), n)));  // cut off anywhere
    }
    auto longer = bytes;
    longer.push_back(0);
    CHECK(!net::decode(longer));                          // bytes left over
    CHECK(!net::decode(std::vector<std::uint8_t>{200}));  // unknown type

    net::Turn backwards = turn;
    backwards.frames = {net::FrameCommands{15, {}}, net::FrameCommands{12, {}}};
    CHECK(!net::decode(net::encode(backwards)));  // frames not ascending
    net::Turn outside = turn;
    outside.frames = {net::FrameCommands{20, {}}};
    CHECK(!net::decode(net::encode(outside)));  // a frame not yet closed
    net::Hello hello;
    hello.name = std::string(net::kMaxNameLength + 1, 'x');
    CHECK(!net::decode(net::encode(hello)));  // name too long
    const net::Chat named{1, "Anna", "Hallo", true, {2}, {}};
    CHECK(!net::decode(net::encode(named)));  // to everyone, naming someone

    // Random bytes behind every type: decoding never crashes and keeps the limits.
    sim::Random r(1602, 7);
    for (int i = 0; i < 20000; ++i) {
        std::vector<std::uint8_t> noise(1 + r.below(64));
        for (auto& b : noise) {
            b = static_cast<std::uint8_t>(r.next());
        }
        noise[0] = static_cast<std::uint8_t>(r.below(16));
        if (const auto m = net::decode(noise)) {
            CHECK(net::encode(*m).size() <= noise.size() + 1024);
        }
    }
}

namespace {

/// The chat line `text` among `events`, if one came.
const net::SessionEvent* chat_line(const std::vector<net::SessionEvent>& events, std::string_view text) {
    const auto it = std::find_if(events.begin(), events.end(), [&](const net::SessionEvent& e) {
        return e.kind == net::SessionEvent::Kind::Chat && e.text == text;
    });
    return it != events.end() ? &*it : nullptr;
}

}  // namespace

TEST_CASE("net: a private line reaches only the players it is for; a line to everyone the spectators too") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);  // slot 0
    Computer anna(network, "Anna");
    Computer bernd(network, "Bernd");
    Computer clara(network, "Clara");
    Computer wim(network, "Wim");  // no slot left: watches
    run_for(network, host, {&anna, &bernd, &clara, &wim}, 1500);
    REQUIRE(anna.session->local_seat() == 1);
    REQUIRE(bernd.session->local_seat() == 2);
    REQUIRE(clara.session->local_seat() == 3);
    REQUIRE(wim.session->local_seat() == sim::kNoPlayer);
    network.cut(*clara.transport);  // Clara is gone
    const std::vector<Computer*> all = {&anna, &bernd, &wim};
    run_for(network, host, all, 500);

    // Anna to the host and Clara: the host gets it, Bernd and Wim never do.
    anna.session->say("Gegen Bernd?", {3, 0, 0});
    run_for(network, host, all, 300);
    const net::SessionEvent* at_host = chat_line(host.events, "Gegen Bernd?");
    REQUIRE(at_host != nullptr);
    CHECK(!at_host->everyone);
    CHECK(at_host->seat == 1);
    CHECK(at_host->name == "Anna");
    CHECK(at_host->to == std::vector<net::Seat>{0});
    CHECK(at_host->missed.empty());  // only the one who said it learns whom it missed
    CHECK(chat_line(bernd.events, "Gegen Bernd?") == nullptr);
    CHECK(chat_line(wim.events, "Gegen Bernd?") == nullptr);
    // Back to Anna: whom it reached and whom not.
    const net::SessionEvent* echo = chat_line(anna.events, "Gegen Bernd?");
    REQUIRE(echo != nullptr);
    CHECK(echo->to == std::vector<net::Seat>{0});
    CHECK(echo->missed == std::vector<net::Seat>{3});  // Clara left

    // The host to Bernd: Anna and Wim do not get it.
    host.session->say("Abgemacht", {2});
    run_for(network, host, all, 300);
    REQUIRE(chat_line(bernd.events, "Abgemacht") != nullptr);
    CHECK(chat_line(bernd.events, "Abgemacht")->seat == 0);
    CHECK(chat_line(host.events, "Abgemacht") != nullptr);
    CHECK(chat_line(anna.events, "Abgemacht") == nullptr);
    CHECK(chat_line(wim.events, "Abgemacht") == nullptr);

    // A spectator writes only to everyone.
    wim.session->say("Psst", {1});
    wim.session->say("Gutes Spiel!");
    run_for(network, host, all, 300);
    for (const auto* events : {&host.events, &anna.events, &bernd.events, &wim.events}) {
        CHECK(chat_line(*events, "Psst") == nullptr);
        const net::SessionEvent* line = chat_line(*events, "Gutes Spiel!");
        REQUIRE(line != nullptr);
        CHECK(line->everyone);
        CHECK(line->seat == sim::kNoPlayer);
    }
}

TEST_CASE("net: without the setting, spectators only read the chat") {
    net::LoopbackNetwork network(5, 0);
    net::SessionConfig config;
    config.spectators_chat = false;
    Host host(network, config);
    Computer anna(network, "Anna");
    Computer bernd(network, "Bernd");
    Computer clara(network, "Clara");
    Computer wim(network, "Wim");
    const std::vector<Computer*> all = {&anna, &bernd, &clara, &wim};
    run_for(network, host, all, 1500);
    REQUIRE(wim.session->local_seat() == sim::kNoPlayer);
    CHECK(!wim.session->lobby().spectators_chat);

    wim.session->say("Hallo?");
    anna.session->say("Hallo Wim");
    run_for(network, host, all, 300);
    for (const auto* events : {&host.events, &anna.events, &bernd.events, &wim.events}) {
        CHECK(chat_line(*events, "Hallo?") == nullptr);
        CHECK(chat_line(*events, "Hallo Wim") != nullptr);
    }
}

TEST_CASE("net: two computers join, everyone plays, all games stay the same") {
    net::LoopbackNetwork network(40, 25);
    Host host(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, host, {&a, &b}, 1000);

    REQUIRE(a.session->playing());
    REQUIRE(b.session->playing());
    CHECK(a.session->local_seat() == 1);
    CHECK(b.session->local_seat() == 2);
    CHECK(a.game.loads == 1);
    REQUIRE(host.session->roster().size() == 3);
    CHECK(host.session->roster()[0].host);
    CHECK(a.session->roster() == host.session->roster());
    CHECK(host.game.world->players()[1].name == "Anna");  // each player's name, as a command
    CHECK(b.game.world->players()[1].name == "Anna");

    // Every player gives orders now and then, also speed changes and pauses.
    sim::Random r(42, 1);
    Computer* computers[] = {&a, &b};
    for (int i = 0; i < 2500; ++i) {
        const std::uint32_t who = r.below(40);
        if (who == 0) {
            host.session->submit(sim::PlayerRename{0, random_name(r)});
        } else if (who <= 2) {
            Computer& c = *computers[who - 1];
            c.session->submit(sim::PlayerRename{c.session->local_seat(), random_name(r)});
        } else if (who == 3) {
            const auto op = static_cast<sim::GameSpeedOrder::Op>(r.below(3));
            a.session->submit(sim::GameSpeedOrder{1, op, 1 + r.below(4)});
        }
        step(network, host, {&a, &b});
    }
    host.session->submit(sim::GameSpeedOrder{0, sim::GameSpeedOrder::Op::Resume, 1});

    // Now the fingerprint of every game at the same frame, a few seconds ahead.
    FrameProbe ph{host.session->frames() + 60, {}};
    FrameProbe pa = ph;
    FrameProbe pb = ph;
    for (int i = 0; i < 400 && !(ph.hash && pa.hash && pb.hash); ++i) {
        network.advance(kStepMs);
        host.update(kStepMs, ph.at(*host.session, host.game));
        a.update(kStepMs, pa.at(*a.session, a.game));
        b.update(kStepMs, pb.at(*b.session, b.game));
    }
    REQUIRE(ph.hash.has_value());
    CHECK(pa.hash == ph.hash);
    CHECK(pb.hash == ph.hash);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
    CHECK(host.session->fingerprints_compared() > 10);
    CHECK(host.session->fingerprints_matched() == host.session->fingerprints_compared());
    CHECK(a.game.loads == 1);                                         // never had to load the game anew
    CHECK(host.session->kept_frames() <= host.session->lead() + 2u);  // nothing piles up
}

TEST_CASE("net: a game packed while the host plays on reaches the computer with the frames run since (TB-26)") {
    net::LoopbackNetwork network(40, 25);
    net::SessionConfig config;
    config.pack_wait_ms = 60'000;  // never waits for it: the game goes on until it is packed
    Host host(network, config);
    run_for(network, host, {}, 1000);
    Computer a(network, "Anna", 0, kContent, config);
    sim::Random r(7, 1);
    // The host gives orders in every step, so the frames run while the game packs carry commands.
    const std::uint64_t joined_at = host.session->frames();
    for (int i = 0; i < 2000 && !a.session->playing(); ++i) {
        host.session->submit(sim::PlayerRename{0, random_name(r)});
        step(network, host, {&a});
        std::this_thread::sleep_for(std::chrono::microseconds(200));  // time for the worker
    }
    REQUIRE(a.session->playing());
    CHECK(host.session->frames() > joined_at);
    CHECK(a.game.loads == 1);
    run_for(network, host, {&a}, 2000);

    FrameProbe ph{host.session->frames() + 60, {}};
    FrameProbe pa = ph;
    for (int i = 0; i < 400 && !(ph.hash && pa.hash); ++i) {
        network.advance(kStepMs);
        host.update(kStepMs, ph.at(*host.session, host.game));
        a.update(kStepMs, pa.at(*a.session, a.game));
    }
    REQUIRE(ph.hash.has_value());
    CHECK(pa.hash == ph.hash);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
    CHECK(host.session->fingerprints_matched() == host.session->fingerprints_compared());
    CHECK(a.game.loads == 1);
    CHECK(host.session->kept_frames() <= host.session->lead() + 2u);  // the frames kept for it are let go
}

TEST_CASE("net: nobody gives orders for another player") {
    net::LoopbackNetwork network(10, 0);
    Host host(network);
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 500);
    REQUIRE(a.session->playing());
    const std::string before = host.game.world->players()[0].name;

    a.session->submit(sim::PlayerRename{0, "Gekapert"});  // the computer itself refuses
    // A computer that skips its own check: the host drops the command.
    auto raw = network.connect();
    std::vector<net::TransportEvent> ev;
    network.advance(50);
    raw->poll(ev);
    raw->send(0, net::encode(net::Hello{net::kProtocolVersion, kContent, "Mallory", sim::kNoPlayer, 0, "", false, {}}));
    for (int i = 0; i < 20; ++i) {
        step(network, host, {&a});
    }
    raw->send(0, net::encode(net::CommandBatch{{sim::PlayerRename{0, "Gekapert"}, sim::PlayerRename{1, "Gekapert"}}}));
    run_for(network, host, {&a}, 500);
    CHECK(host.game.world->players()[0].name == before);
    CHECK(host.game.world->players()[1].name == "Anna");
    CHECK(a.game.world->players()[0].name == before);

    // A host's own player cannot order for others either.
    host.session->submit(sim::PlayerRename{1, "Gekapert"});
    run_for(network, host, {&a}, 300);
    CHECK(a.game.world->players()[1].name == "Anna");

    // A flood: no more than the limit per frame reaches the game.
    std::uint64_t before_frames = host.session->closed();
    raw->send(0, net::encode(net::CommandBatch{std::vector<sim::Command>(1000, sim::PlayerRename{2, "Flut"})}));
    run_for(network, host, {&a}, 300);
    CHECK(host.session->closed() > before_frames);

    // Garbage: the computer is sent away, the game goes on.
    raw->send(0, std::vector<std::uint8_t>{0xEE, 1, 2});
    run_for(network, host, {&a}, 300);
    CHECK(host.session->roster().size() == 2);
    CHECK(!a.session->ended());
}

TEST_CASE("net: a computer that diverges is noticed and gets the game anew") {
    net::LoopbackNetwork network(30, 10);
    net::SessionConfig config;
    config.hash_interval = 20;
    Host host(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    Computer b(network, "Bernd", 0, kContent, config);
    run_for(network, host, {&a, &b}, 1500);
    REQUIRE(a.session->playing());
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);

    // Something changes on one computer only, as a bug in the simulation would.
    a.game.world->player(0)->money += 1;
    run_for(network, host, {&a, &b}, 3000);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 1);
    CHECK(a.count(net::SessionEvent::Kind::Desync) == 1);
    CHECK(b.count(net::SessionEvent::Kind::Desync) == 1);  // everyone hears of it
    CHECK(a.game.loads == 2);
    CHECK(b.game.loads == 1);
    CHECK(a.game.world->player(0)->money == host.game.world->player(0)->money);
    const auto matched = host.session->fingerprints_matched();
    run_for(network, host, {&a, &b}, 2000);
    CHECK(host.session->fingerprints_matched() > matched);  // equal again
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 1);

    // A computer that diverges again and again, even right after loading (another build), is sent
    // away after a few tries.
    a.game.diverge = true;
    a.game.world->player(0)->money += 1;
    run_for(network, host, {&a, &b}, 6000);
    CHECK(a.game.loads == 2 + static_cast<int>(config.max_resyncs));
    CHECK(a.session->ended());
    CHECK(a.session->end_reason() == net::EndReason::Desync);
    CHECK(!b.session->ended());
}

TEST_CASE("net: into a running game, out and back into the same slot") {
    net::LoopbackNetwork network(25, 10);
    Host host(network);
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 5000);
    host.session->submit(sim::PlayerRename{0, "Später"});

    auto b = std::make_unique<Computer>(network, "Bernd");
    run_for(network, host, {&a, b.get()}, 1500);
    REQUIRE(b->session->playing());
    CHECK(b->session->local_seat() == 2);
    CHECK(b->game.world->players()[0].name == "Später");
    const std::uint64_t token = b->session->token();
    CHECK(token != 0);

    // The connection breaks; the game goes on without Bernd, his slot waits for him.
    network.cut(*b->transport);
    run_for(network, host, {&a, b.get()}, 500);
    CHECK(b->session->ended());
    CHECK(b->session->end_reason() == net::EndReason::HostLeft);
    CHECK(host.count(net::SessionEvent::Kind::Left) == 1);
    CHECK(host.session->roster().size() == 2);

    // Someone else comes meanwhile: slot 2 is kept for Bernd only while he is away? No: it is free.
    // Bernd comes back with his token and plays slot 2 again.
    b = std::make_unique<Computer>(network, "Bernd", token);
    run_for(network, host, {&a, b.get()}, 1500);
    REQUIRE(b->session->playing());
    CHECK(b->session->local_seat() == 2);
    CHECK(b->session->token() == token);
    b->session->submit(sim::PlayerRename{2, "Bernd wieder da"});
    run_for(network, host, {&a, b.get()}, 1000);
    CHECK(host.game.world->players()[2].name == "Bernd wieder da");
    CHECK(a.game.world->players()[2].name == "Bernd wieder da");
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
    CHECK(host.session->fingerprints_matched() == host.session->fingerprints_compared());
}

TEST_CASE("net: the computer plays on for someone who left, until he is back") {
    net::LoopbackNetwork network(25, 10);
    net::SessionConfig config;
    config.take_over_ms = 5000;
    Host host(network, config);
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 2000);
    auto b = std::make_unique<Computer>(network, "Bernd");
    run_for(network, host, {&a, b.get()}, 2000);
    REQUIRE(b->session->playing());
    REQUIRE(b->session->local_seat() == 2);
    const std::uint64_t token = b->session->token();

    // A broken connection: the slot waits a while for Bernd before the computer takes it.
    network.cut(*b->transport);
    run_for(network, host, {&a, b.get()}, 3000);
    CHECK(b->session->ended());
    CHECK(host.game.world->players()[2].role == sim::PlayerRole::Human);
    CHECK(a.game.world->players()[2].role == sim::PlayerRole::Human);
    run_for(network, host, {&a, b.get()}, 3500);
    CHECK(host.game.world->players()[2].role == sim::PlayerRole::Computer);
    CHECK(a.game.world->players()[2].role == sim::PlayerRole::Computer);
    CHECK(host.game.world->players()[2].name == "Bernd");

    // Bernd comes back and plays his slot again himself.
    b = std::make_unique<Computer>(network, "Bernd", token);
    run_for(network, host, {&a, b.get()}, 2000);
    REQUIRE(b->session->playing());
    CHECK(b->session->local_seat() == 2);
    CHECK(host.game.world->players()[2].role == sim::PlayerRole::Human);
    CHECK(a.game.world->players()[2].role == sim::PlayerRole::Human);
    CHECK(b->game.world->players()[2].role == sim::PlayerRole::Human);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
    CHECK(host.session->fingerprints_matched() == host.session->fingerprints_compared());
}

TEST_CASE("net: a person comes back into the slot they played by their player ID, without the token") {
    net::LoopbackNetwork network(25, 10);
    net::SessionConfig config;
    config.take_over_ms = 1000;
    config.player = 1;
    Host host(network, config, Game::fresh(4));  // four human slots
    Computer a(network, "Anna", Computer::Person{11});
    run_for(network, host, {&a}, 2000);
    auto b = std::make_unique<Computer>(network, "Bernd", Computer::Person{22});
    run_for(network, host, {&a, b.get()}, 2000);
    REQUIRE(b->session->playing());
    REQUIRE(b->session->local_seat() == 2);
    // Every computer's game knows who plays which slot, the host's own too.
    for (const Game* g : {&host.game, &a.game, &b->game}) {
        CHECK(g->world->players()[0].owner == net::player_verifier(1));
        CHECK(g->world->players()[1].owner == net::player_verifier(11));
        CHECK(g->world->players()[2].owner == net::player_verifier(22));
        CHECK(g->world->players()[3].owner == 0);
    }
    CHECK(host.game.world->players().size() == 5);

    // Bernd's program ends (no token any more); the computer plays his slot after a while.
    network.cut(*b->transport);
    run_for(network, host, {&a, b.get()}, 3000);
    CHECK(host.game.world->players()[2].role == sim::PlayerRole::Computer);
    CHECK(host.game.world->players()[2].owner == net::player_verifier(22));

    // Someone new gets the slot nobody played, not Bernd's.
    Computer c(network, "Clara", Computer::Person{33});
    run_for(network, host, {&a, &c}, 2000);
    REQUIRE(c.session->playing());
    CHECK(c.session->local_seat() == 3);

    // Bernd starts again and joins without a token: his slot and his colour again.
    b = std::make_unique<Computer>(network, "Bernd", Computer::Person{22});
    run_for(network, host, {&a, b.get(), &c}, 2000);
    REQUIRE(b->session->playing());
    CHECK(b->session->local_seat() == 2);
    CHECK(host.game.world->players()[2].role == sim::PlayerRole::Human);
    CHECK(a.game.world->players()[2].role == sim::PlayerRole::Human);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
    CHECK(host.session->fingerprints_matched() == host.session->fingerprints_compared());
}

TEST_CASE("net: another person's slot only when no other is free") {
    net::LoopbackNetwork network(5, 0);
    net::SessionConfig config;
    config.take_over_ms = 500;
    Host host(network, config, Game::fresh(4));
    Computer a(network, "Anna", Computer::Person{11});
    auto b = std::make_unique<Computer>(network, "Bernd", Computer::Person{22});
    run_for(network, host, {&a, b.get()}, 2000);
    REQUIRE(b->session->local_seat() == 2);
    network.cut(*b->transport);
    run_for(network, host, {&a, b.get()}, 2000);

    // Two newcomers: the slot nobody played first, then Bernd's, as there is no other.
    Computer c(network, "Clara", Computer::Person{33});
    run_for(network, host, {&a, &c}, 1500);
    CHECK(c.session->local_seat() == 3);
    Computer d(network, "Dora", Computer::Person{44});
    run_for(network, host, {&a, &c, &d}, 1500);
    REQUIRE(d.session->playing());
    CHECK(d.session->local_seat() == 2);
    run_for(network, host, {&a, &c, &d}, 500);
    CHECK(host.game.world->players()[2].owner == net::player_verifier(44));  // Dora's now
}

TEST_CASE("net: a person sent away has no claim to the slot they played") {
    net::LoopbackNetwork network(5, 0);
    Host host(network, {}, Game::fresh(4));
    Computer a(network, "Anna", Computer::Person{11});
    auto d = std::make_unique<Computer>(network, "Dora", Computer::Person{44});
    run_for(network, host, {&a, d.get()}, 2000);
    REQUIRE(d->session->local_seat() == 2);
    host.session->kick(2);
    run_for(network, host, {&a, d.get()}, 2000);
    CHECK(d->session->ended());
    // Back with her ID, she comes in like anyone new: the free slot nobody played, not hers.
    d = std::make_unique<Computer>(network, "Dora", Computer::Person{44});
    run_for(network, host, {&a, d.get()}, 2000);
    REQUIRE(d->session->playing());
    CHECK(d->session->local_seat() == 3);
}

TEST_CASE("net: in the lobby of a savegame, each person finds the slot they played") {
    net::LoopbackNetwork network(5, 0);
    net::Lobby lobby;
    lobby.seats = {net::LobbySeat{net::LobbySeat::Kind::Open, "", false, 0, net::player_verifier(1)},
                   net::LobbySeat{net::LobbySeat::Kind::Open, "", false, 1, net::player_verifier(22)},
                   net::LobbySeat{net::LobbySeat::Kind::Open, "", false, 2, 0},
                   net::LobbySeat{net::LobbySeat::Kind::Open, "", false, 3, net::player_verifier(11)}};
    net::SessionConfig config;
    config.player = 1;
    net::HostSession host(network.listen(), lobby, 0, "Gastgeber", kContent, config);
    // A newcomer first: the slot nobody played, not Anna's or Bernd's.
    Computer c(network, "Clara", Computer::Person{33});
    Computer a(network, "Anna", Computer::Person{11});
    Computer b(network, "Bernd", Computer::Person{22});
    for (int i = 0; i < 100; ++i) {
        network.advance(kStepMs);
        host.update(kStepMs);
        c.update(kStepMs);
        if (i > 20) {
            a.update(kStepMs);
            b.update(kStepMs);
        }
    }
    CHECK(c.session->local_seat() == 2);
    CHECK(a.session->local_seat() == 3);
    CHECK(b.session->local_seat() == 1);
}

TEST_CASE("net: the player ID's verifier gives the ID not away") {
    CHECK(net::player_verifier(0) == 0);
    CHECK(net::player_verifier(5) != 0);
    CHECK(net::player_verifier(5) != 5);
    CHECK(net::player_verifier(5) == net::player_verifier(5));
    CHECK(net::player_verifier(5) != net::player_verifier(6));
}

TEST_CASE("net: without the setting, the slot of someone who left gives no orders") {
    net::LoopbackNetwork network(25, 10);
    net::SessionConfig config;
    config.computer_takes_over = false;
    config.take_over_ms = 1000;
    Host host(network, config);
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 2000);
    auto b = std::make_unique<Computer>(network, "Bernd");
    run_for(network, host, {&a, b.get()}, 2000);
    REQUIRE(b->session->playing());
    REQUIRE(b->session->local_seat() == 2);
    CHECK(!a.session->lobby().computer_takes_over);
    network.cut(*b->transport);
    run_for(network, host, {&a, b.get()}, 10000);
    CHECK(host.game.world->players()[2].role == sim::PlayerRole::Human);
    CHECK(a.game.world->players()[2].role == sim::PlayerRole::Human);
}

TEST_CASE("net: with the setting, only the host pauses, resumes and sets the speed (MS-20)") {
    net::LoopbackNetwork network(5, 0);
    net::SessionConfig config;
    config.host_controls_speed = true;
    Host host(network, config);
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 2000);
    REQUIRE(a.session->playing());
    CHECK(a.session->lobby().host_controls_speed);
    CHECK(!a.session->may_change_speed());
    CHECK(host.session->may_change_speed());
    const net::Seat anna = a.session->local_seat();
    a.session->submit(sim::GameSpeedOrder{anna, sim::GameSpeedOrder::Op::Pause, 1});
    run_for(network, host, {&a}, 1000);
    CHECK(!host.game.simulation->paused());
    CHECK(!a.game.simulation->paused());
    CHECK(a.count(net::SessionEvent::Kind::SpeedRefused) == 1);
    // The host pauses; the other cannot resume.
    host.session->submit(sim::GameSpeedOrder{host.session->local_seat(), sim::GameSpeedOrder::Op::Pause, 1});
    run_for(network, host, {&a}, 1000);
    CHECK(host.game.simulation->paused());
    CHECK(a.game.simulation->paused());
    a.session->submit(sim::GameSpeedOrder{anna, sim::GameSpeedOrder::Op::Resume, 1});
    run_for(network, host, {&a}, 1000);
    CHECK(host.game.simulation->paused());
    CHECK(a.count(net::SessionEvent::Kind::SpeedRefused) == 2);
    // Nor can the other change the speed.
    host.session->submit(sim::GameSpeedOrder{host.session->local_seat(), sim::GameSpeedOrder::Op::Resume, 1});
    run_for(network, host, {&a}, 1000);
    REQUIRE(!host.game.simulation->paused());
    a.session->submit(sim::GameSpeedOrder{anna, sim::GameSpeedOrder::Op::SetSpeed, 2});
    run_for(network, host, {&a}, 1000);
    CHECK(host.game.simulation->speed() == 1);
    CHECK(a.game.simulation->speed() == 1);
    CHECK(a.count(net::SessionEvent::Kind::SpeedRefused) == 3);
    host.session->submit(sim::GameSpeedOrder{host.session->local_seat(), sim::GameSpeedOrder::Op::SetSpeed, 2});
    run_for(network, host, {&a}, 1000);
    CHECK(host.game.simulation->speed() == 2);
    CHECK(a.game.simulation->speed() == 2);
}

TEST_CASE("net: without the setting, every player pauses, resumes and sets the speed") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 2000);
    REQUIRE(a.session->playing());
    CHECK(!a.session->lobby().host_controls_speed);
    CHECK(a.session->may_change_speed());
    const net::Seat anna = a.session->local_seat();
    a.session->submit(sim::GameSpeedOrder{anna, sim::GameSpeedOrder::Op::SetSpeed, 2});
    run_for(network, host, {&a}, 1000);
    CHECK(host.game.simulation->speed() == 2);
    CHECK(a.game.simulation->speed() == 2);
    a.session->submit(sim::GameSpeedOrder{anna, sim::GameSpeedOrder::Op::Pause, 2});
    run_for(network, host, {&a}, 1000);
    CHECK(host.game.simulation->paused());
    CHECK(a.game.simulation->paused());
    a.session->submit(sim::GameSpeedOrder{anna, sim::GameSpeedOrder::Op::Resume, 2});
    run_for(network, host, {&a}, 1000);
    CHECK(!host.game.simulation->paused());
    CHECK(a.game.simulation->speed() == 2);
    CHECK(a.count(net::SessionEvent::Kind::SpeedRefused) == 0);
}

TEST_CASE("net: a slot's token is good only for the one it was given to") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    Computer a(network, "Anna");
    auto c = std::make_unique<Computer>(network, "Clara");
    run_for(network, host, {&a, c.get()}, 3000);
    REQUIRE(c->session->playing());
    const net::Seat seat = c->session->local_seat();
    const std::uint64_t old = c->session->token();
    CHECK(old != 0);
    CHECK(a.session->token() != old + 1);  // drawn at random, not one after the other
    CHECK(a.session->token() + 1 != old);

    // Sent away: someone new takes the slot under a new token, and the old one is worthless.
    host.session->kick(seat);
    run_for(network, host, {&a, c.get()}, 1000);
    CHECK(c->session->ended());
    Computer d(network, "Dora");
    run_for(network, host, {&a, &d}, 2000);
    REQUIRE(d.session->playing());
    CHECK(d.session->local_seat() == seat);
    CHECK(d.session->token() != old);
    c = std::make_unique<Computer>(network, "Clara", old);
    run_for(network, host, {&a, c.get(), &d}, 2000);
    CHECK(!d.session->ended());
    CHECK(d.session->local_seat() == seat);
    CHECK(c->session->local_seat() != seat);
}

TEST_CASE("net: the host sends a spectator away by its place in the roster") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    Computer c(network, "Clara");
    Computer w(network, "Wilma");  // every slot taken: Wilma watches
    run_for(network, host, {&a, &b, &c, &w}, 1000);
    REQUIRE(w.session->playing());
    REQUIRE(w.session->local_seat() == sim::kNoPlayer);
    const auto& roster = host.session->roster();
    REQUIRE(roster.size() == 5);
    std::size_t wilma = 0;
    for (std::size_t i = 0; i < roster.size(); ++i) {
        wilma = roster[i].name == "Wilma" ? i : wilma;
    }
    REQUIRE(wilma != 0);

    host.session->kick_member(0, roster[0].seat, roster[0].name);  // the host itself: nothing
    host.session->kick_member(99, sim::kNoPlayer, "Wilma");
    // The entry is no longer the one shown (someone left and the places moved up): nothing (MS-18).
    host.session->kick_member(wilma, sim::kNoPlayer, "Clara");
    host.session->kick_member(wilma, 1, "Wilma");
    run_for(network, host, {&a, &b, &c, &w}, 1000);
    REQUIRE(!w.session->ended());
    REQUIRE(host.session->roster().size() == 5);
    host.session->kick_member(wilma, sim::kNoPlayer, "Wilma");
    run_for(network, host, {&a, &b, &c, &w}, 1000);
    CHECK(w.session->ended());
    REQUIRE(!w.events.empty());
    CHECK(w.events.back().refused == net::RefuseReason::Kicked);
    CHECK(host.session->roster().size() == 4);
    CHECK(!a.session->ended());
    CHECK(!c.session->ended());
}

TEST_CASE("net: a flooding player leaves room for the others' commands") {
    net::LoopbackNetwork network(5, 0);
    net::SessionConfig config;
    config.max_commands_per_frame = 1024;  // one player alone could fill a whole frame
    Host host(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    Computer b(network, "Bernd", 0, kContent, config);
    run_for(network, host, {&a, &b}, 1000);
    REQUIRE(a.session->playing());
    REQUIRE(b.session->playing());
    for (int i = 0; i < 3000; ++i) {
        a.session->submit(sim::ProductionSwitch{a.session->local_seat(), 0, 1, 1});
    }
    b.session->submit(sim::PlayerRename{b.session->local_seat(), "Bea"});
    run_for(network, host, {&a, &b}, 1000);
    CHECK(host.game.world->players()[b.session->local_seat()].name == "Bea");
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
}

TEST_CASE("net: a connection that never says hello is let go") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    auto silent = network.connect();
    run_for(network, host, {}, 12'000);
    std::vector<net::TransportEvent> events;
    silent->poll(events);
    CHECK(std::any_of(events.begin(), events.end(),
                      [](const auto& e) { return e.kind == net::TransportEvent::Kind::Disconnected; }));
}

TEST_CASE("net: without a free slot a computer watches; other data or versions are refused") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    Computer c(network, "Clara");  // the computer's slot: Clara takes it over
    Computer w(network, "Wilma");  // three humans and a computer in the world, all taken
    run_for(network, host, {&a, &b, &c, &w}, 1000);
    REQUIRE(c.session->playing());
    CHECK(c.session->local_seat() == 3);
    CHECK(host.game.world->players()[3].role == sim::PlayerRole::Human);
    REQUIRE(w.session->playing());
    CHECK(w.session->local_seat() == sim::kNoPlayer);
    w.session->submit(sim::PlayerRename{3, "Zuschauer"});
    run_for(network, host, {&a, &b, &c, &w}, 500);
    CHECK(host.game.world->players()[3].name == "Clara");
    CHECK(host.session->roster().size() == 5);

    // Clara leaves: the computer plays her slot again.
    network.cut(*c.transport);
    run_for(network, host, {&a, &b, &c, &w}, 500);
    CHECK(host.game.world->players()[3].role == sim::PlayerRole::Computer);

    Computer other(network, "Dora", 0, kContent + 1);
    run_for(network, host, {&a, &b, &w, &other}, 500);
    CHECK(other.session->ended());
    CHECK(other.session->end_reason() == net::EndReason::Refused);
    REQUIRE(!other.events.empty());
    CHECK(other.events.back().refused == net::RefuseReason::Content);
    CHECK(!other.session->playing());

    net::SessionConfig closed;
    closed.spectators = false;
    net::LoopbackNetwork network2(5, 0);
    Host host2(network2, closed);
    Computer x(network2, "X");
    Computer y(network2, "Y");
    Computer v(network2, "V");
    Computer z(network2, "Z");
    run_for(network2, host2, {&x, &y, &v, &z}, 500);
    CHECK(z.session->ended());
    CHECK(z.events.back().refused == net::RefuseReason::Full);

    // No host at all.
    net::LoopbackNetwork empty(5, 0);
    Computer lonely(empty, "Niemand");
    empty.advance(50);
    lonely.update(50);
    CHECK(lonely.session->ended());
    CHECK(lonely.session->end_reason() == net::EndReason::ConnectFailed);
}

TEST_CASE("net: a computer needs the game's packages, and may have more") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    const assets::ContentPackage lighthouse{"lighthouse", "1.2", 0x1111};
    const assets::ContentPackage weather{"weather", "", 0x2222};
    host.game.world->set_packages({lighthouse, weather});
    REQUIRE(host.session->packages().size() == 2);

    Computer none(network, "Nobody");
    Computer other(network, "Other", 0, kContent, {}, {}, {lighthouse, {"weather", "", 0x3333}});
    Computer more(network, "More", 0, kContent, {}, {}, {{"extra", "9", 4}, weather, lighthouse});
    run_for(network, host, {&none, &other, &more}, 1000);
    CHECK(none.session->end_reason() == net::EndReason::Refused);
    REQUIRE(!none.events.empty());
    CHECK(none.events.back().refused == net::RefuseReason::Packages);
    CHECK(none.events.back().packages == std::vector<assets::ContentPackage>({lighthouse, weather}));
    REQUIRE(!other.events.empty());
    CHECK(other.events.back().refused == net::RefuseReason::Packages);
    CHECK(other.events.back().packages == std::vector<assets::ContentPackage>({weather}));  // as the host has it
    REQUIRE(more.session->playing());
    REQUIRE(more.game.world != nullptr);
    REQUIRE(more.game.world->packages().has_value());
    CHECK(*more.game.world->packages() == std::vector<assets::ContentPackage>({lighthouse, weather}));
}

TEST_CASE("net: the host sends the packages a computer lacks once it asks for them") {
    const std::filesystem::path root = test::temp_path("o1602_net_share");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "lighthouse" / "gfx");
    std::ofstream(root / "lighthouse" / "mod.toml") << "version = \"1.2\"\n";
    std::ofstream(root / "lighthouse" / "gfx" / "tower.txt") << "tower";
    const std::filesystem::path folder = root / "lighthouse";
    const assets::ContentPackage lighthouse{"lighthouse", "1.2", assets::package_hash(folder)};
    const std::uint64_t size = assets::package_size(folder);

    net::LoopbackNetwork network(5, 0);
    Host host(network);
    host.game.world->set_packages({lighthouse});
    host.session->share_packages({{lighthouse, folder, size}});

    Computer asks(network, "Asks", 0, kContent, {}, {}, {}, false);
    Computer fetches(network, "Fetches", 0, kContent, {}, {}, {}, true);
    std::vector<assets::ModPackage> kept;
    fetches.session->keep_packages_with([&](const assets::ContentPackage& p, std::span<const assets::PackageFile> f) {
        auto stored = assets::store_package(root / "cache", p, f);
        if (stored) {
            kept.push_back(std::move(stored).value());
        }
        return stored.ok();
    });
    Computer unasked(network, "Unasked", 0, kContent, {}, {}, {}, true);  // nowhere to keep them
    run_for(network, host, {&asks, &fetches, &unasked}, 1000);

    REQUIRE(!asks.events.empty());
    CHECK(asks.events.back().refused == net::RefuseReason::Packages);
    CHECK(asks.events.back().share_bytes == size);
    REQUIRE(fetches.session->playing());
    CHECK(fetches.count(net::SessionEvent::Kind::PackageKept) == 1);
    REQUIRE(kept.size() == 1);
    CHECK(kept[0].content() == lighthouse);
    CHECK(unasked.session->end_reason() == net::EndReason::ProtocolError);

    // More than the host sends: refused as if it did not share.
    net::LoopbackNetwork small(5, 0);
    net::SessionConfig config;
    config.max_share_bytes = size - 1;
    Host stingy(small, config);
    stingy.game.world->set_packages({lighthouse});
    stingy.session->share_packages({{lighthouse, folder, size}});
    Computer late(small, "Late", 0, kContent, {}, {}, {}, true);
    late.session->keep_packages_with(
        [](const assets::ContentPackage&, std::span<const assets::PackageFile>) { return true; });
    run_for(small, stingy, {&late}, 1000);
    REQUIRE(!late.events.empty());
    CHECK(late.events.back().refused == net::RefuseReason::Packages);
    CHECK(late.events.back().share_bytes == 0);
    std::filesystem::remove_all(root);
}

TEST_CASE("net: in the lobby the host asks for the packages it was given") {
    net::LoopbackNetwork network(5, 0);
    net::Lobby lobby;
    lobby.seats = {net::LobbySeat{net::LobbySeat::Kind::Open, "", false, 0},
                   net::LobbySeat{net::LobbySeat::Kind::Open, "", false, 1}};
    net::HostSession session(network.listen(), lobby, 0, "Gastgeber", kContent);
    CHECK(session.packages().empty());
    session.set_packages({{"lighthouse", "1", 5}});
    REQUIRE(session.packages().size() == 1);
    CHECK(session.packages()[0].id == "lighthouse");
}

TEST_CASE("net: the host waits for a slow computer, then goes on without a frozen one") {
    net::LoopbackNetwork network(20, 0);
    net::SessionConfig config;
    config.stall_ms = 2000;
    config.resync_behind = 100;
    Host host(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    Computer b(network, "Bernd", 0, kContent, config);
    run_for(network, host, {&a, &b}, 1000);
    REQUIRE(b.session->playing());

    // Bernd's computer stands still (debugger, window dragged): the others wait for a while.
    const std::uint64_t at = host.session->frames();
    const std::uint64_t ahead = host.session->lead() + config.max_ahead_ms / sim::Simulation::kTickMs;
    run_for(network, host, {&a}, 3000);
    CHECK(host.session->frames() <= at + ahead + 2);
    CHECK(host.session->waiting());
    CHECK(host.count(net::SessionEvent::Kind::Lagging) == 0);
    // ... and then go on without it.
    run_for(network, host, {&a}, 6000);
    CHECK(host.count(net::SessionEvent::Kind::Lagging) == 1);
    CHECK(host.session->frames() > at + ahead + 20);
    CHECK(!host.session->waiting());
    CHECK(a.session->frames() + 10 > host.session->frames());

    // Back, far behind: Bernd gets the game as it is now instead of running every frame.
    run_for(network, host, {&a, &b}, 3000);
    CHECK(b.game.loads == 2);
    CHECK(b.session->frames() + 10 > host.session->frames());
    CHECK(host.session->roster()[2].state == net::Member::State::Playing);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);

    // Frozen again: the others wait a quarter of the time now (a computer that stalls on purpose
    // cannot stop the game for the full time again and again).
    run_for(network, host, {&a}, 2200);  // the full time would be 1000 ms ahead and 2000 ms held
    CHECK(host.count(net::SessionEvent::Kind::Lagging) == 2);
    CHECK(!host.session->waiting());
}

TEST_CASE("net: a slow computer that keeps up some frames sets the pace and is not dropped") {
    net::LoopbackNetwork network(20, 5);
    net::SessionConfig config;
    config.stall_ms = 2000;
    Host host(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    Computer slow(network, "Langsam", 0, kContent, config);
    run_for(network, host, {&a, &slow}, 2000);
    REQUIRE(slow.session->playing());
    const std::uint64_t start = host.session->frames();
    // The slow computer's processor manages two thirds of the frames the game needs (MS-15).
    std::uint32_t budget = 0;
    const auto cpu = [&budget] {
        if (budget < sim::Simulation::kTickMs) {
            return false;
        }
        budget -= sim::Simulation::kTickMs;
        return true;
    };
    std::uint32_t hold = 0;  // real time without a frame on the host
    std::uint32_t longest_hold = 0;
    std::uint64_t last = start;
    for (std::uint32_t t = 0; t < 60'000; t += kStepMs) {
        network.advance(kStepMs);
        host.update(kStepMs);
        a.update(kStepMs);
        budget = std::min<std::uint32_t>(budget + kStepMs * 2 / 3, 10 * sim::Simulation::kTickMs);
        slow.update(kStepMs, cpu);
        hold = host.session->frames() == last ? hold + kStepMs : 0;
        last = host.session->frames();
        longest_hold = std::max(longest_hold, hold);
    }
    const std::uint64_t ran = host.session->frames() - start;
    CHECK(host.count(net::SessionEvent::Kind::Lagging) == 0);
    CHECK(slow.game.loads == 1);
    CHECK(ran > 60 * 20 / 2);  // at the slow computer's pace, about two thirds
    CHECK(ran < 60 * 20);
    CHECK(longest_hold < 500u);  // never a stretch without frames, only a slower game
}

TEST_CASE("net: a lagging computer gets the game anew after the same game time at every speed") {
    net::LoopbackNetwork network(20, 0);
    net::SessionConfig config;
    config.stall_ms = 1000;
    config.resync_behind = 100;  // 5 s of game time
    Host host(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    Computer b(network, "Bernd", 0, kContent, config);
    run_for(network, host, {&a, &b}, 1000);
    host.session->submit(sim::GameSpeedOrder{0, sim::GameSpeedOrder::Op::SetSpeed, 8});
    run_for(network, host, {&a, &b}, 500);
    REQUIRE(b.session->playing());
    // Bernd freezes for 2 s: at speed 8 more than 100 frames, but less than 100 x 8 (MS-19).
    run_for(network, host, {&a}, 2000);
    REQUIRE(host.count(net::SessionEvent::Kind::Lagging) == 1);
    run_for(network, host, {&a, &b}, 3000);
    CHECK(b.game.loads == 1);  // caught up by running the frames
    CHECK(b.session->frames() + 20 > host.session->frames());
    // Frozen for long: then the game comes anew.
    run_for(network, host, {&a}, 8000);
    run_for(network, host, {&a, &b}, 3000);
    CHECK(b.game.loads == 2);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
}

TEST_CASE("net: a long session keeps its memory and its frames in step") {
    net::LoopbackNetwork network(60, 40);
    net::SessionConfig config;
    config.hash_interval = 50;
    Host host(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    std::size_t most_kept = 0;
    std::uint64_t most_in_hand = 0;
    sim::Random r(9, 9);
    // Ten minutes of game at speed 4 and 8.
    host.session->submit(sim::GameSpeedOrder{0, sim::GameSpeedOrder::Op::SetSpeed, 4});
    for (std::uint32_t t = 0; t < 10 * 60 * 1000; t += kStepMs) {
        if (t == 5 * 60 * 1000) {
            host.session->submit(sim::GameSpeedOrder{0, sim::GameSpeedOrder::Op::SetSpeed, 8});
        }
        if (r.below(100) == 0) {
            a.session->submit(sim::PlayerRename{1, random_name(r)});
        }
        step(network, host, {&a});
        most_kept = std::max(most_kept, host.session->kept_frames());
        most_in_hand = std::max(most_in_hand, a.session->frames_in_hand());
    }
    CHECK(host.session->frames() > 10 * 60 * 20 * 4);  // the speed was kept, nobody slowed it
    CHECK(most_kept <= config.max_lead + 2u);
    CHECK(most_in_hand <= config.max_lead + 40u);
    CHECK(host.session->fingerprints_compared() > 100);
    CHECK(host.session->fingerprints_matched() == host.session->fingerprints_compared());
    CHECK(a.game.loads == 1);
    // About one message per frame step and computer, not per tick: speed costs no bandwidth.
    CHECK(network.messages_sent() < 2ULL * 10 * 60 * 1000 / kStepMs * 2);
}

TEST_CASE("net: over real UDP on this computer (ENet)") {
    constexpr std::uint16_t kPort = 46021;
    auto listening = net::enet_listen(kPort, 4);
    if (!listening) {
        SKIP("UDP port 46021 is not free here");
    }
    auto connecting = net::enet_connect("127.0.0.1", kPort);
    REQUIRE(connecting.ok());

    Game host_game = Game::fresh();
    net::HostSession host(std::move(listening).value(), *host_game.simulation, buildings(), 0, "Gastgeber", kContent);
    Game game;
    net::Hello hello;
    hello.content = kContent;
    hello.name = "Anna";
    net::ClientSession client(std::move(connecting).value(), hello,
                              [&game](std::span<const std::uint8_t> save) { return game.load(save); });
    // Both sides get the real time that passed: a slow machine (or one whose sleeps run long) runs
    // as many frames per second as a fast one.
    const auto start = std::chrono::steady_clock::now();
    auto last = start;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(15) &&
           !(client.playing() && client.frames() > 20)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto now = std::chrono::steady_clock::now();
        const auto ms = static_cast<std::uint32_t>(
            std::min<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count(), 100));
        last = now;
        host.update(ms);
        client.update(ms);
    }
    REQUIRE(client.playing());
    CHECK(client.local_seat() == 1);
    CHECK(client.frames() > 20);
    CHECK(game.world->players()[1].name == "Anna");
    CHECK(!client.ended());
}

TEST_CASE("net: a lobby: slots, password, chat and ready, then the game starts on every computer") {
    net::LoopbackNetwork network(30, 10);
    net::SessionConfig config;
    config.password = "insel";
    net::Lobby setup;
    setup.title = "Merlins Inselwelt";
    setup.seats = {net::LobbySeat{}, net::LobbySeat{}, net::LobbySeat{net::LobbySeat::Kind::Computer, "", false},
                   net::LobbySeat{}};
    net::HostSession host(network.listen(), setup, 0, "Merlin", kContent, config);
    std::vector<net::SessionEvent> host_events;
    std::optional<Game> host_game;

    Computer anna(network, "Anna", 0, kContent, {}, "insel");
    Computer mallory(network, "Mallory", 0, kContent, {}, "falsch");
    std::vector<Computer*> all = {&anna, &mallory};
    const auto tick = [&](std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += kStepMs) {
            network.advance(kStepMs);
            host.update(kStepMs);
            for (auto& e : host.take_events()) {
                host_events.push_back(std::move(e));
            }
            for (Computer* c : all) {
                c->update(kStepMs);
            }
        }
    };
    tick(300);
    Computer bernd(network, "Bernd", 0, kContent, {}, "insel");  // after Anna: the slots go in order
    all.push_back(&bernd);
    tick(300);
    CHECK(mallory.session->ended());
    CHECK(std::any_of(mallory.events.begin(), mallory.events.end(), [](const net::SessionEvent& e) {
        return e.kind == net::SessionEvent::Kind::Ended && e.refused == net::RefuseReason::Password;
    }));
    all = {&anna, &bernd};
    // The open slots in order; the computer player's slot is not one.
    CHECK(anna.session->local_seat() == 1);
    CHECK(bernd.session->local_seat() == 3);
    CHECK(anna.session->lobby() == host.lobby());
    CHECK(anna.session->lobby().seats[3].name == "Bernd");
    CHECK(anna.session->lobby().password);

    // A third computer finds no slot and watches.
    Computer clara(network, "Clara", 0, kContent, {}, "insel");
    all.push_back(&clara);
    tick(300);
    CHECK(clara.session->welcomed());
    CHECK(clara.session->local_seat() == sim::kNoPlayer);

    // The host closes Bernd's slot: he watches; opened again, he takes it back.
    host.set_seat(3, net::LobbySeat::Kind::Closed);
    tick(300);
    CHECK(bernd.session->local_seat() == sim::kNoPlayer);
    CHECK(bernd.session->lobby().seats[3].kind == net::LobbySeat::Kind::Closed);
    bernd.session->take_seat(3);
    tick(300);
    CHECK(bernd.session->local_seat() == sim::kNoPlayer);  // closed: not to be taken
    host.set_seat(3, net::LobbySeat::Kind::Open);
    bernd.session->take_seat(3);
    anna.session->take_seat(3);  // too late: Bernd asked first
    tick(300);
    CHECK(bernd.session->local_seat() == 3);
    CHECK(anna.session->local_seat() == 1);

    // Chat reaches everyone, with the name the host knows.
    anna.session->say("Stufe 2?");
    tick(300);
    for (const std::vector<net::SessionEvent>* events : {&host_events, &bernd.events, &clara.events}) {
        CHECK(std::any_of(events->begin(), events->end(), [](const net::SessionEvent& e) {
            return e.kind == net::SessionEvent::Kind::Chat && e.name == "Anna" && e.seat == 1 && e.text == "Stufe 2?";
        }));
    }

    // Ready, and a watcher sent away.
    CHECK(!host.everyone_ready());
    anna.session->set_ready(true);
    bernd.session->set_ready(true);
    tick(300);
    CHECK(host.everyone_ready());
    CHECK(clara.session->lobby().seats[1].ready);
    host.kick(3);
    tick(300);
    CHECK(bernd.session->ended());
    CHECK(host.lobby().seats[3].kind == net::LobbySeat::Kind::Open);
    Computer bernd2(network, "Bernd", 0, kContent, {}, "insel");
    all = {&anna, &bernd2, &clara};
    tick(300);
    CHECK(bernd2.session->local_seat() == 3);
    bernd2.session->set_ready(true);
    tick(300);

    // The start: the host makes the game from the lobby, every computer gets it.
    host_game.emplace(Game::from_lobby(host.lobby()));
    host.start(*host_game->simulation, buildings());
    tick(2000);
    REQUIRE(anna.session->playing());
    REQUIRE(bernd2.session->playing());
    REQUIRE(clara.session->playing());
    CHECK(anna.session->lobby().started);
    CHECK(anna.game.world->player(3)->name == "Bernd");
    CHECK(host_game->world->player(1)->name == "Anna");
    // Only the host hands a slot to the computer; a player may set the speed, pause and go on,
    // for everyone.
    anna.session->submit(sim::GameSpeedOrder{1, sim::GameSpeedOrder::Op::SetSpeed, 2});
    anna.session->submit(sim::PlayerControl{1, true});
    tick(2000);
    CHECK(host_game->simulation->speed() == 2);
    CHECK(anna.game.simulation->speed() == 2);
    CHECK(host_game->world->player(1)->role == sim::PlayerRole::Human);
    anna.session->submit(sim::GameSpeedOrder{1, sim::GameSpeedOrder::Op::Pause, 1});
    tick(2000);
    CHECK(host_game->simulation->paused());
    CHECK(anna.game.simulation->paused());
    anna.session->submit(sim::GameSpeedOrder{1, sim::GameSpeedOrder::Op::Resume, 1});
    host.submit(sim::GameSpeedOrder{0, sim::GameSpeedOrder::Op::SetSpeed, 4});
    tick(5000);
    CHECK(!host_game->simulation->paused());
    CHECK(host_game->simulation->speed() == 4);
    CHECK(host.fingerprints_compared() > 0);
    CHECK(host.fingerprints_matched() == host.fingerprints_compared());

    // Lobby messages are of no use once the game runs.
    anna.session->take_seat(sim::kNoPlayer);
    tick(300);
    CHECK(anna.session->local_seat() == 1);
}

TEST_CASE("net: a lobby's banners: each player chooses one, no two alike, the computer gives way") {
    net::LoopbackNetwork network(30, 10);
    net::Lobby setup;
    // Four slots, the third the computer's, all with the same banner and one not on offer: the host
    // makes them different.
    setup.seats = {net::LobbySeat{}, net::LobbySeat{}, net::LobbySeat{net::LobbySeat::Kind::Computer, "", false},
                   net::LobbySeat{}};
    setup.seats[3].banner = 99;
    setup.banners = {0, 1, 2, 3, 7, 8};
    net::HostSession host(network.listen(), setup, 0, "Merlin", kContent);
    const auto banners = [](const net::Lobby& lobby) {
        std::vector<sim::BannerId> b;
        for (const net::LobbySeat& s : lobby.seats) {
            b.push_back(s.banner);
        }
        return b;
    };
    CHECK((banners(host.lobby()) == std::vector<sim::BannerId>{0, 1, 2, 3}));

    Computer anna(network, "Anna");
    std::vector<Computer*> all = {&anna};
    const auto tick = [&](std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += kStepMs) {
            network.advance(kStepMs);
            host.update(kStepMs);
            (void)host.take_events();
            for (Computer* c : all) {
                c->update(kStepMs);
            }
        }
    };
    tick(300);
    REQUIRE(anna.session->local_seat() == 1);
    CHECK(anna.session->lobby().seats[1].banner == 1);  // the slot's banner

    // A free banner; the host's is another person's and stays his; not on offer, nothing.
    anna.session->set_banner(7);
    tick(300);
    CHECK((banners(anna.session->lobby()) == std::vector<sim::BannerId>{0, 7, 2, 3}));
    anna.session->set_banner(0);
    anna.session->set_banner(5);
    tick(300);
    CHECK((banners(host.lobby()) == std::vector<sim::BannerId>{0, 7, 2, 3}));
    // The computer's banner: the computer takes Anna's in exchange.
    anna.session->set_banner(2);
    tick(300);
    CHECK((banners(anna.session->lobby()) == std::vector<sim::BannerId>{0, 2, 7, 3}));

    // The host for itself and for the slots no other person plays, never for Anna.
    CHECK(!host.set_banner(1, 8));
    CHECK(!host.set_banner(0, 2));
    CHECK(host.set_banner(3, 8));
    CHECK(host.set_banner(2, 3));  // a free one: nobody gives way
    CHECK(host.set_banner(0, 8));  // the open slot's: it takes the host's
    CHECK((banners(host.lobby()) == std::vector<sim::BannerId>{8, 2, 3, 0}));
    CHECK(host.set_banner(0, 8));  // the same again: fine

    // The banner stays with the slot when its player goes; whoever comes next has it.
    anna.session.reset();
    all.clear();
    tick(300);
    CHECK(host.lobby().seats[1].kind == net::LobbySeat::Kind::Open);
    CHECK(host.lobby().seats[1].banner == 2);
    Computer bernd(network, "Bernd");
    all = {&bernd};
    tick(300);
    REQUIRE(bernd.session->local_seat() == 1);
    CHECK(bernd.session->lobby().seats[1].banner == 2);
    bernd.session->set_banner(1);
    bernd.session->set_ready(true);
    tick(300);

    // The game gets them; once it runs they stay.
    std::optional<Game> host_game;
    host_game.emplace(Game::from_lobby(host.lobby()));
    host.start(*host_game->simulation, buildings());
    tick(2000);
    REQUIRE(bernd.session->playing());
    CHECK(bernd.game.world->player(1)->banner == 1);
    CHECK(bernd.game.world->player(0)->banner == 8);
    CHECK(!host.set_banner(0, 7));
    bernd.session->set_banner(7);
    tick(300);
    CHECK(host.lobby().seats[1].banner == 1);
    CHECK(!bernd.session->ended());
}

TEST_CASE("net: a running game's lobby shows the players' banners and offers none") {
    net::LoopbackNetwork network;
    Game game = Game::fresh(2);
    game.world->player(1)->banner = 9;
    net::HostSession host(network.listen(), *game.simulation, buildings(), 0, "Merlin", kContent);
    CHECK(host.lobby().banners.empty());
    CHECK(host.lobby().seats[1].banner == 9);
    CHECK(!host.set_banner(0, 9));
}

TEST_CASE("net: searches and answers of the game search, and hostile bytes") {
    net::GameInfo info;
    info.title = "Merlins Inselwelt";
    info.host = "Merlin";
    info.game = "Endlosspiel, Stufe 2";
    info.players = 2;
    info.slots = 4;
    info.password = true;
    CHECK(net::decode_search(net::encode_search(77)) == std::optional<std::uint32_t>(77));
    const auto answer = net::decode_answer(net::encode_answer(5, info));
    REQUIRE(answer.has_value());
    CHECK(answer->first == 5);
    CHECK(answer->second == info);
    CHECK(!net::decode_answer(net::encode_search(5)));
    CHECK(!net::decode_search(net::encode_answer(5, info)));
    const auto bytes = net::encode_answer(5, info);
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CHECK(!net::decode_answer(std::span(bytes.data(), n)));
    }

    // What the host tells: the human slots of its lobby, who hosts.
    net::Lobby lobby;
    lobby.title = "T";
    lobby.seats = {net::LobbySeat{net::LobbySeat::Kind::Human, "Merlin", true}, net::LobbySeat{},
                   net::LobbySeat{net::LobbySeat::Kind::Computer, "", false},
                   net::LobbySeat{net::LobbySeat::Kind::Closed, "", false}};
    const net::GameInfo described =
        net::game_info(lobby, {net::Member{0, "Merlin", net::Member::State::Playing, true}}, 16030);
    CHECK(described.players == 1);
    CHECK(described.slots == 2);
    CHECK(described.host == "Merlin");
    CHECK(described.port == 16030);
}

TEST_CASE("net: a host is found by its address over real UDP") {
    constexpr std::uint16_t kPort = 46031;
    auto beacon = net::LanBeacon::open(kPort);
    if (!beacon) {
        SKIP("UDP port 46031 is not free here");
    }
    net::GameInfo info;
    info.title = "Probe";
    info.port = 46032;
    beacon.value()->set_info(info);
    auto browser = net::LanBrowser::open(kPort);
    REQUIRE(browser.ok());
    for (int i = 0; i < 200 && browser.value()->games().empty(); ++i) {
        if (i % 50 == 0) {
            browser.value()->search({"127.0.0.1"});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        beacon.value()->update();
        browser.value()->update(5);
    }
    REQUIRE(!browser.value()->games().empty());
    const net::FoundGame& found = browser.value()->games().front();
    CHECK(!found.address.empty());  // 127.0.0.1, or this computer's address if the broadcast came first
    CHECK(found.info.title == "Probe");
    CHECK(found.info.port == 46032);
    // Not answering any more: forgotten. Answers still on their way (to the broadcasts of the
    // search, which may come back late) arrive first, so that the forgetting starts after them.
    beacon.value().reset();
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        browser.value()->update(5);
    }
    browser.value()->update(7000);
    CHECK(browser.value()->games().empty());
}

TEST_CASE("net: a host on this computer is found without an address, once") {
    constexpr std::uint16_t kPort = 46033;
    auto beacon = net::LanBeacon::open(kPort);
    if (!beacon) {
        SKIP("UDP port 46033 is not free here");
    }
    net::GameInfo info;
    info.title = "Nebenan";
    info.port = 46034;
    beacon.value()->set_info(info);
    auto browser = net::LanBrowser::open(kPort);
    REQUIRE(browser.ok());
    browser.value()->search();
    // Every answer that arrives (loopback, each network's broadcast) names the same game.
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        beacon.value()->update();
        browser.value()->update(5);
    }
    REQUIRE(browser.value()->games().size() == 1);
    CHECK(browser.value()->games().front().address == "127.0.0.1");
    CHECK(browser.value()->games().front().info.title == "Nebenan");
}

TEST_CASE("net: the computer holds an open slot until someone comes, and takes it back") {
    net::LoopbackNetwork network(30, 10);
    net::Lobby setup;
    setup.seats = {net::LobbySeat{}, net::LobbySeat{}};
    net::HostSession host(network.listen(), setup, 0, "Merlin", kContent);
    // As the game does at the start: the slot nobody took is the computer's.
    Game host_game = Game::from_lobby(host.lobby());
    host_game.world->player(1)->role = sim::PlayerRole::Computer;
    host.start(*host_game.simulation, buildings());
    const auto tick = [&](std::uint32_t ms, const std::vector<Computer*>& all) {
        for (std::uint32_t t = 0; t < ms; t += kStepMs) {
            network.advance(kStepMs);
            host.update(kStepMs);
            (void)host.take_events();
            for (Computer* c : all) {
                c->update(kStepMs);
            }
        }
    };
    tick(500, {});

    auto anna = std::make_unique<Computer>(network, "Anna");
    tick(3000, {anna.get()});
    REQUIRE(anna->session->playing());
    CHECK(anna->session->local_seat() == 1);
    CHECK(host_game.world->player(1)->role == sim::PlayerRole::Human);
    CHECK(host_game.world->player(1)->name == "Anna");
    CHECK(anna->game.world->player(1)->role == sim::PlayerRole::Human);

    anna.reset();  // she leaves: the computer goes on with her slot
    tick(3000, {});
    CHECK(host_game.world->player(1)->role == sim::PlayerRole::Computer);
    CHECK(host.lobby().seats[1].kind == net::LobbySeat::Kind::Open);

    // And the next one takes it over again.
    Computer bernd(network, "Bernd");
    tick(3000, {&bernd});
    CHECK(bernd.session->local_seat() == 1);
    CHECK(host_game.world->player(1)->role == sim::PlayerRole::Human);
}

TEST_CASE("net: a computer that joins in a loop gets the game at most once a second") {
    net::LoopbackNetwork network(10, 0);
    Host host(network);
    int loads = 0;
    for (int i = 0; i < 12; ++i) {
        Computer c(network, "Rex");
        run_for(network, host, {&c}, 150);
        loads += c.game.loads;
    }
    // A few at once, then one for every second that passed (1.8 s).
    CHECK(loads >= 4);
    CHECK(loads <= 5);
    // Someone who comes and stays still gets in.
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 1500);
    CHECK(a.session->playing());
    CHECK(a.game.loads == 1);
}

// --- Host migration ---

TEST_CASE("net: the host names the playing computer with the lowest slot its successor") {
    net::LoopbackNetwork network(10, 0);
    auto host = std::make_unique<Host>(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    Computer c(network, "Clara");
    Computer w(network, "Wilma");  // watches: never the successor
    run_for(network, *host, {&a, &b, &c, &w}, 1500);
    REQUIRE(a.session->playing());
    REQUIRE(w.session->local_seat() == sim::kNoPlayer);
    CHECK(host->session->successor() == 1);
    CHECK(host->session->successor_address() == "loopback:1");
    for (const Computer* x : {&a, &b, &c, &w}) {
        CHECK(x->session->successor() == 1);
        // The address only to those who play; spectators get it when the host leaves (MS-25).
        CHECK(x->session->successor_address() == (x == &w ? "" : "loopback:1"));
    }
    CHECK(b.session->successor_name() == "Anna");

    // Anna's connection breaks: Bernd is next; back again, she is the successor again.
    const std::uint64_t token = a.session->token();
    network.cut(*a.transport);
    run_for(network, *host, {&b, &c, &w}, 500);
    CHECK(host->session->successor() == 2);
    CHECK(c.session->successor() == 2);
    Computer back(network, "Anna", token);
    run_for(network, *host, {&back, &b, &c, &w}, 1500);
    REQUIRE(back.session->playing());
    CHECK(c.session->successor() == 1);

    // The host goes: only the successor hosts next, the others can follow it.
    host.reset();
    notice_host_left(network, {&back, &b, &c, &w});
    CHECK(back.session->hosts_next());
    CHECK(back.session->can_migrate());
    CHECK(!b.session->hosts_next());
    CHECK(b.session->can_migrate());
    CHECK(w.session->can_migrate());  // a spectator follows too
}

TEST_CASE("net: the host leaves; its successor hosts on, the others follow, all games stay the same") {
    net::LoopbackNetwork network(20, 0);  // Anna first, in slot 1
    net::LoopbackNetwork next(20, 10);
    net::SessionConfig config;
    config.hash_interval = 20;
    auto host = std::make_unique<Host>(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    Computer b(network, "Bernd", 0, kContent, config);
    run_for(network, *host, {&a, &b}, 2000);
    REQUIRE(a.session->playing());
    REQUIRE(b.session->playing());
    const std::uint64_t token = b.session->token();

    // Orders on the way when the host goes: what it closed reaches everyone or no one.
    host->session->submit(sim::PlayerRename{0, "Weg"});
    a.session->submit(sim::PlayerRename{1, "Anna I"});
    b.session->submit(sim::PlayerRename{2, "Bernd I"});
    step(network, *host, {&a, &b});
    host.reset();
    notice_host_left(network, {&a, &b});
    REQUIRE(a.session->hosts_next());
    const std::uint64_t frames = a.session->frames();

    auto moved = std::make_unique<net::HostSession>(next.listen(), *a.session, buildings());
    follow(next, b);
    a.session.reset();
    step_moved(next, *moved, {&b}, 3000);
    REQUIRE(b.session->playing());
    CHECK(b.session->local_seat() == 2);
    CHECK(b.session->token() == token);  // its own slot, by its token
    CHECK(b.game.loads == 2);
    CHECK(moved->frames() > frames + 20);
    CHECK(moved->local_seat() == 1);
    REQUIRE(moved->roster().size() == 2);
    CHECK(moved->roster()[0].name == "Anna");
    CHECK(moved->roster()[0].host);
    CHECK(b.session->roster() == moved->roster());
    // The old host's slot: the computer plays it, on every computer.
    CHECK(a.game.world->player(0)->role == sim::PlayerRole::Computer);
    CHECK(b.game.world->player(0)->role == sim::PlayerRole::Computer);
    CHECK(a.game.world->players()[2].name == b.game.world->players()[2].name);

    // The game goes on: orders of both players, and the fingerprints match.
    b.session->submit(sim::PlayerRename{2, "Bernd II"});
    moved->submit(sim::PlayerRename{1, "Anna II"});
    step_moved(next, *moved, {&b}, 2000);
    CHECK(a.game.world->players()[2].name == "Bernd II");
    CHECK(b.game.world->players()[1].name == "Anna II");
    FrameProbe pa{moved->frames() + 40, {}};
    FrameProbe pb = pa;
    for (int i = 0; i < 400 && !(pa.hash && pb.hash); ++i) {
        next.advance(kStepMs);
        moved->update(kStepMs, pa.at(*moved, a.game));
        b.update(kStepMs, pb.at(*b.session, b.game));
    }
    REQUIRE(pa.hash.has_value());
    CHECK(pb.hash == pa.hash);
    CHECK(moved->fingerprints_compared() > 3);
    CHECK(moved->fingerprints_matched() == moved->fingerprints_compared());
    CHECK(b.count(net::SessionEvent::Kind::Desync) == 0);
}

TEST_CASE("net: the host leaves and nobody hosts on: the others give up after a while") {
    net::LoopbackNetwork network(20, 0);
    net::LoopbackNetwork next(20, 0);  // the successor never hosts there (its port is taken)
    net::SessionConfig config;
    config.migration_ms = 3000;
    auto host = std::make_unique<Host>(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    Computer b(network, "Bernd", 0, kContent, config);
    run_for(network, *host, {&a, &b}, 1500);
    REQUIRE(b.session->playing());
    host.reset();
    notice_host_left(network, {&a, &b});
    const auto before = b.game.simulation->state_hash();
    follow(next, b);
    for (std::uint32_t t = 0; t < 2500; t += kStepMs) {
        next.advance(kStepMs);
        b.update(kStepMs);
    }
    CHECK(!b.session->ended());  // still trying
    for (std::uint32_t t = 0; t < 1000; t += kStepMs) {
        next.advance(kStepMs);
        b.update(kStepMs);
    }
    CHECK(b.session->ended());
    CHECK(b.session->end_reason() == net::EndReason::ConnectFailed);
    CHECK(!b.session->playing());
    CHECK(!b.session->can_migrate());
    CHECK(b.game.loads == 1);  // the game stands as it was, to be saved
    CHECK(b.game.simulation->state_hash() == before);

    // A host that leaves before its game starts leaves nothing to carry on.
    net::LoopbackNetwork lobby_network(5, 0);
    net::Lobby setup;
    setup.seats = {net::LobbySeat{}, net::LobbySeat{}};
    auto lobby = std::make_unique<net::HostSession>(lobby_network.listen(), setup, 0, "Merlin", kContent);
    Computer c(lobby_network, "Clara");
    for (int i = 0; i < 30; ++i) {
        lobby_network.advance(kStepMs);
        lobby->update(kStepMs);
        c.update(kStepMs);
    }
    REQUIRE(c.session->welcomed());
    lobby.reset();
    notice_host_left(lobby_network, {&c});
    CHECK(!c.session->can_migrate());
}

TEST_CASE("net: after the host changed, players come back into their slots, newcomers take the old host's") {
    net::LoopbackNetwork network(15, 0);  // Anna first, in slot 1
    net::LoopbackNetwork next(15, 5);
    auto host = std::make_unique<Host>(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, *host, {&a, &b}, 2000);
    Computer c(network, "Clara");  // joins the running game: the computer's slot
    run_for(network, *host, {&a, &b, &c}, 1500);
    Computer w(network, "Wilma");  // every slot taken: she watches
    run_for(network, *host, {&a, &b, &c, &w}, 1500);
    REQUIRE(c.session->playing());
    REQUIRE(c.session->local_seat() == 3);
    REQUIRE(w.session->local_seat() == sim::kNoPlayer);
    const std::uint64_t token = c.session->token();

    host.reset();
    notice_host_left(network, {&a, &b, &c, &w});
    // The others notice first and knock in vain until the successor hosts, two seconds later.
    follow(next, b);
    follow(next, c);
    follow(next, w);
    for (std::uint32_t t = 0; t < 2000; t += kStepMs) {
        next.advance(kStepMs);
        for (Computer* x : {&b, &c, &w}) {
            x->update(kStepMs);
        }
    }
    CHECK(!c.session->welcomed());
    REQUIRE(a.session->hosts_next());
    net::HostSession moved(next.listen(), *a.session, buildings());
    step_moved(next, moved, {&b, &c, &w}, 3000);
    REQUIRE(c.session->playing());
    CHECK(c.session->local_seat() == 3);
    CHECK(c.session->token() == token);
    CHECK(b.session->local_seat() == 2);
    REQUIRE(w.session->playing());
    CHECK(w.session->local_seat() == sim::kNoPlayer);  // not the free slot of the old host
    CHECK(moved.roster().size() == 4);
    CHECK(a.game.world->player(3)->role == sim::PlayerRole::Human);

    // Someone new gets the old host's slot from the computer ...
    Computer d(next, "Dora");
    step_moved(next, moved, {&b, &c, &w, &d}, 3000);
    REQUIRE(d.session->playing());
    CHECK(d.session->local_seat() == 0);
    CHECK(a.game.world->player(0)->role == sim::PlayerRole::Human);
    CHECK(b.game.world->player(0)->name == "Dora");
    // ... and Clara's slot, the computer's at the start, goes back to it when she leaves.
    c.session.reset();
    step_moved(next, moved, {&b, &w, &d}, 1000);
    CHECK(a.game.world->player(3)->role == sim::PlayerRole::Computer);
    CHECK(b.game.world->player(3)->role == sim::PlayerRole::Computer);
    CHECK(moved.fingerprints_matched() == moved.fingerprints_compared());
}

TEST_CASE("net: a slot's token does not take it from a computer that is still there (MS-1)") {
    net::LoopbackNetwork network(20, 0);
    Host host(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, host, {&a, &b}, 2000);
    REQUIRE(b.session->playing());
    const sim::PlayerId bernd = b.session->local_seat();

    // A second connection with Bernd's own token, while Bernd plays on.
    Computer thief(network, "Mallory", b.session->token());
    run_for(network, host, {&a, &b, &thief}, 1500);
    CHECK(thief.session->ended());
    CHECK(thief.session->end_reason() == net::EndReason::Refused);
    CHECK(thief.session->local_seat() != bernd);
    CHECK(!b.session->ended());
    CHECK(b.session->local_seat() == bernd);
}

TEST_CASE("net: the successor gets verifiers of the slots' tokens, never the tokens (MS-1)") {
    const std::uint64_t token = 0x1BC5BCFBEBEB8465ULL;
    const std::uint64_t v = net::token_verifier(token, 77);
    CHECK(v != token);
    CHECK(v == net::token_verifier(token, 77));
    CHECK(v != net::token_verifier(token, 78));      // another host's salt
    CHECK(v != net::token_verifier(token + 1, 77));  // another token
}

TEST_CASE("net: a slot from the old host outside the lobby is not taken over (MS-8)") {
    // An old host that hands its successor a slot the lobby does not have, then leaves.
    net::LoopbackNetwork network(10, 0);
    net::LoopbackNetwork next(10, 0);
    auto old_host = network.listen();
    Computer a(network, "Anna");
    Game world = Game::fresh();
    const auto save = sim::write_game(*world.world, buildings(), world.simulation.get());
    REQUIRE(save.ok());
    net::Lobby lobby;
    lobby.started = true;
    for (std::size_t p = 0; p < world.world->players().size(); ++p) {
        net::LobbySeat seat;
        seat.kind = p < 2 ? net::LobbySeat::Kind::Human : net::LobbySeat::Kind::Open;
        lobby.seats.push_back(seat);
    }
    const std::uint64_t token = 0x5EC0DE;
    std::vector<net::TransportEvent> events;
    bool answered = false;  // only the Hello: a second Welcome in a running game is nonsense
    net::LinkId anna = 0;
    for (int i = 0; i < 20; ++i) {
        network.advance(kStepMs);
        events.clear();
        old_host->poll(events);
        for (const auto& e : events) {
            if (e.kind == net::TransportEvent::Kind::Received && !answered) {
                answered = true;
                anna = e.link;
                old_host->send(e.link, net::encode(net::Welcome{net::kProtocolVersion, 1, 0xA77A}));
                old_host->send(e.link, net::encode(lobby));
                old_host->send(e.link, net::encode(net::Snapshot{1, 0, 1, false, *save}));
                old_host->send(e.link, net::encode(net::Roster{{{0, "Alt", net::Member::State::Playing, true},
                                                                {1, "Anna", net::Member::State::Playing, false}},
                                                               1,
                                                               "loopback:1"}));
                old_host->send(
                    e.link, net::encode(net::Standby{{net::SeatToken{200, 9, net::token_verifier(token, 9)}}, {201}}));
            }
        }
        a.update(kStepMs);
    }
    old_host->send(anna, net::encode(net::HostLeaving{}));  // leaving on purpose
    old_host.reset();
    notice_host_left(network, {&a});
    REQUIRE(a.session->hosts_next());
    auto moved = std::make_unique<net::HostSession>(next.listen(), *a.session, buildings());
    // Someone comes back with the token of that slot: it gets no slot (and nothing is written
    // outside the lobby).
    net::Hello hello;
    hello.content = kContent;
    hello.name = "Bernd";
    hello.token = token;
    hello.returning = true;
    auto link = next.connect();
    Computer back(next, "Bernd");
    back.session = std::make_unique<net::ClientSession>(
        std::move(link), hello, [&back](std::span<const std::uint8_t> s) { return back.game.load(s); });
    step_moved(next, *moved, {&back}, 1500);
    CHECK(back.session->local_seat() == sim::kNoPlayer);
    CHECK(moved->lobby().seats.size() == lobby.seats.size());
}

namespace {

/// A fresh game with an empty island of `side` x `side` tiles at (20, 20), clear of the ships.
Game game_with_island(std::uint32_t side) {
    Game g;
    g.world = std::make_unique<sim::World>(64 + side, 64 + side);
    Game fresh = Game::fresh();
    g.world->set_players(fresh.world->players());
    for (const sim::Ship& ship : fresh.world->ships()) {
        g.world->add_ship(ship);
    }
    REQUIRE(g.world->add_island(sim::WorldIsland{assets::IslandHeader{}, sim::IslandMap(side, side), 20, 20}).ok());
    g.simulation = std::make_unique<sim::Simulation>(*g.world, buildings(), figures());
    return g;
}

}  // namespace

TEST_CASE("net: rectangles over a whole island take frames, not one long frame (MS-3)") {
    constexpr std::uint32_t kSide = 40;
    net::LoopbackNetwork network(5, 0);
    Host host(network, {}, game_with_island(kSide));
    Computer a(network, "Anna");
    run_for(network, host, {&a}, 1000);
    REQUIRE(a.session->playing());
    CHECK(net::command_work(*host.game.world, sim::DemolishOrder{1, 0, {-5, -5}, {500, 500}}) == kSide * kSide);
    CHECK(net::command_work(*host.game.world, sim::DemolishOrder{1, 7, {0, 0}, {500, 500}}) == 1);
    (void)host.game.simulation->take_command_results();

    for (int i = 0; i < 256; ++i) {
        a.session->submit(sim::DemolishOrder{a.session->local_seat(), 0, {0, 0}, {1000, 1000}});
    }
    // Two players: each has half of a frame's work, so at most a few rectangles go into a frame.
    const std::uint64_t most = net::kFrameWork / 2 / (kSide * kSide) + 1;
    std::uint64_t carried = 0;
    std::uint64_t widest = 0;
    const auto count = [&] {
        const auto results = host.game.simulation->take_command_results();
        carried += results.size();
        widest = std::max<std::uint64_t>(widest, results.size());
        return true;
    };
    for (std::uint32_t t = 0; t < 20'000 && carried < 256; t += kStepMs) {
        network.advance(kStepMs);
        host.update(kStepMs, count);
        a.update(kStepMs);
    }
    CHECK(carried == 256);  // all of them, none dropped
    CHECK(widest <= most);
    CHECK(widest >= 2);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
    CHECK(a.count(net::SessionEvent::Kind::Dropped) == 0);
}

TEST_CASE("net: an order the others could not read is not sent; a flood is dropped and told (MS-10, MS-17)") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    Computer c(network, "Clara");
    run_for(network, host, {&a, &b, &c}, 1000);
    REQUIRE(c.session->playing());
    const sim::PlayerId clara = c.session->local_seat();

    sim::SoldierOrder huge;
    huge.player = 0;
    huge.soldiers.resize(sim::kMaxCommandItems + 1);
    CHECK(!sim::command_size(huge));
    huge.soldiers.resize(sim::kMaxCommandItems);
    CHECK(sim::command_size(huge).has_value());
    huge.soldiers.resize(5000);
    host.session->submit(huge);
    huge.player = a.session->local_seat();
    a.session->submit(huge);
    run_for(network, host, {&a, &b, &c}, 500);
    CHECK(!a.session->ended());
    CHECK(!b.session->ended());
    CHECK(host.count(net::SessionEvent::Kind::Dropped) == 1);
    CHECK(a.count(net::SessionEvent::Kind::Dropped) == 1);

    // The host floods far beyond what waits for room, everyone else floods too, and Clara leaves:
    // the host's own commands still hand her slot to the computer (they are never dropped).
    for (int i = 0; i < 3000; ++i) {
        host.session->submit(sim::ProductionSwitch{0, 0, 1, 1});
        a.session->submit(sim::ProductionSwitch{a.session->local_seat(), 0, 1, 1});
        b.session->submit(sim::ProductionSwitch{b.session->local_seat(), 0, 1, 1});
    }
    network.cut(*c.transport);
    run_for(network, host, {&a, &b}, 1000);
    CHECK(host.game.world->players()[clara].role == sim::PlayerRole::Computer);
    CHECK(a.game.world->players()[clara].role == sim::PlayerRole::Computer);
    std::uint32_t dropped = 0;
    for (const auto& e : host.events) {
        dropped += e.kind == net::SessionEvent::Kind::Dropped ? e.count : 0;
    }
    CHECK(dropped > 1);
    CHECK(dropped < 3000);
    // Anna's computer paced hers: the host dropped none of them.
    CHECK(a.count(net::SessionEvent::Kind::Dropped) == 1);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
}

TEST_CASE("net: lobby requests in a loop reach the others a few times a second (MS-16)") {
    net::LoopbackNetwork network(5, 0);
    net::Lobby setup;
    setup.seats = {net::LobbySeat{}, net::LobbySeat{}, net::LobbySeat{}};
    net::HostSession host(network.listen(), setup, 0, "Merlin", kContent);
    Computer anna(network, "Anna");
    Computer bernd(network, "Bernd");
    const auto tick = [&](std::uint32_t ms, bool toggle) {
        for (std::uint32_t t = 0; t < ms; t += kStepMs) {
            network.advance(kStepMs);
            host.update(kStepMs);
            (void)host.take_events();
            for (int i = 0; toggle && i < 20; ++i) {
                anna.session->set_ready(i % 2 == 0);
            }
            anna.update(kStepMs);
            bernd.update(kStepMs);
        }
    };
    tick(500, false);
    REQUIRE(anna.session->welcomed());
    REQUIRE(bernd.session->welcomed());
    const std::size_t before = bernd.count(net::SessionEvent::Kind::LobbyChanged);
    tick(1000, true);
    const std::size_t changes = bernd.count(net::SessionEvent::Kind::LobbyChanged) - before;
    CHECK(changes >= 2);
    CHECK(changes <= 1000 / 100 + 1);
    // A change still arrives.
    anna.session->set_ready(true);
    tick(500, false);
    CHECK(bernd.session->lobby().seats[anna.session->local_seat()].ready);
}

TEST_CASE("net: a newcomer does not get the slot of a player whose link just broke (MS-5)") {
    net::LoopbackNetwork network(15, 0);
    Host host(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, host, {&a, &b}, 2000);
    REQUIRE(b.session->playing());
    const sim::PlayerId bernd = b.session->local_seat();
    const std::uint64_t token = b.session->token();

    network.cut(*b.transport);
    run_for(network, host, {&a}, 1000);
    Computer stranger(network, "Mallory");
    run_for(network, host, {&a, &stranger}, 1500);
    REQUIRE(stranger.session->playing());
    CHECK(stranger.session->local_seat() != bernd);

    Computer back(network, "Bernd", token);
    run_for(network, host, {&a, &stranger, &back}, 1500);
    REQUIRE(back.session->playing());
    CHECK(back.session->local_seat() == bernd);
    CHECK(host.game.world->players()[bernd].role == sim::PlayerRole::Human);
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
}

TEST_CASE("net: without joining the running game, a player whose link broke still comes back (MS-6)") {
    net::LoopbackNetwork network(15, 0);
    net::SessionConfig config;
    config.join_running = false;
    net::Lobby setup;
    setup.seats = {net::LobbySeat{}, net::LobbySeat{}, net::LobbySeat{}};
    net::HostSession host(network.listen(), setup, 0, "Merlin", kContent, config);
    Game host_game = Game::from_lobby(host.lobby());
    const auto tick = [&](std::uint32_t ms, const std::vector<Computer*>& all) {
        for (std::uint32_t t = 0; t < ms; t += kStepMs) {
            network.advance(kStepMs);
            host.update(kStepMs);
            (void)host.take_events();
            for (Computer* c : all) {
                c->update(kStepMs);
            }
        }
    };
    Computer anna(network, "Anna");
    Computer bernd(network, "Bernd");
    tick(500, {&anna, &bernd});
    REQUIRE(bernd.session->welcomed());
    host.start(*host_game.simulation, buildings());
    tick(2000, {&anna, &bernd});
    REQUIRE(bernd.session->playing());
    const sim::PlayerId seat = bernd.session->local_seat();
    const std::uint64_t token = bernd.session->token();

    network.cut(*bernd.transport);
    tick(1000, {&anna});
    Computer stranger(network, "Mallory");
    tick(1000, {&anna, &stranger});
    CHECK(stranger.session->ended());  // nobody new comes into the running game
    Computer back(network, "Bernd", token);
    tick(2000, {&anna, &back});
    REQUIRE(back.session->playing());
    CHECK(back.session->local_seat() == seat);
}

TEST_CASE("net: once the time to come back is over, a newcomer may take the slot") {
    net::LoopbackNetwork network(15, 0);
    net::SessionConfig config;
    config.take_over_ms = 2000;
    Host host(network, config);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    Computer c(network, "Clara");  // the computer's slot: the only other one
    run_for(network, host, {&a, &b, &c}, 2000);
    REQUIRE(b.session->playing());
    const sim::PlayerId bernd = b.session->local_seat();
    network.cut(*b.transport);
    run_for(network, host, {&a, &c}, 3000);
    CHECK(host.game.world->players()[bernd].role == sim::PlayerRole::Computer);
    Computer d(network, "Dora");
    run_for(network, host, {&a, &c, &d}, 1500);
    REQUIRE(d.session->playing());
    CHECK(d.session->local_seat() == bernd);
}

TEST_CASE("net: during a host migration a newcomer does not take a returning player's slot (MS-14)") {
    net::LoopbackNetwork network(15, 0);
    net::LoopbackNetwork next(15, 5);
    auto host = std::make_unique<Host>(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    run_for(network, *host, {&a, &b}, 2000);
    REQUIRE(b.session->playing());
    const sim::PlayerId bernd = b.session->local_seat();

    host.reset();
    notice_host_left(network, {&a, &b});
    REQUIRE(a.session->hosts_next());
    net::HostSession moved(next.listen(), *a.session, buildings());
    // A newcomer is quicker than Bernd, who notices the host's leaving later.
    Computer stranger(next, "Mallory");
    step_moved(next, moved, {&stranger}, 1500);
    REQUIRE(stranger.session->playing());
    CHECK(stranger.session->local_seat() != bernd);
    follow(next, b);
    step_moved(next, moved, {&stranger, &b}, 3000);
    REQUIRE(b.session->playing());
    CHECK(b.session->local_seat() == bernd);
}

TEST_CASE("net: the successor whose own link breaks comes back, it does not host a second game (MS-2)") {
    net::LoopbackNetwork network(10, 0);
    Host host(network);
    Computer a(network, "Anna");  // the successor
    Computer b(network, "Bernd");
    for (Computer* x : {&a, &b}) {
        x->session->reconnect_with([&network] { return network.connect(); });
    }
    run_for(network, host, {&a, &b}, 2000);
    REQUIRE(host.session->successor() == a.session->local_seat());
    const sim::PlayerId anna = a.session->local_seat();

    network.cut(*a.transport);  // only Anna's link: the host and Bernd play on
    run_for(network, host, {&a, &b}, 3000);
    CHECK(a.count(net::SessionEvent::Kind::Reconnecting) == 1);
    CHECK(!a.session->ended());
    CHECK(!a.session->hosts_next());
    REQUIRE(a.session->playing());
    CHECK(a.session->local_seat() == anna);
    CHECK(host.session->roster().size() == 3);
    run_for(network, host, {&a, &b}, 6000);
    CHECK(host.session->fingerprints_matched() == host.session->fingerprints_compared());
    CHECK(host.count(net::SessionEvent::Kind::Desync) == 0);
}

TEST_CASE("net: a host that vanishes without a word: after trying it again, the successor carries on (MS-2)") {
    net::LoopbackNetwork network(10, 0);
    net::SessionConfig config;
    config.reconnect_ms = 1500;
    auto host = std::make_unique<Host>(network, config);
    Computer a(network, "Anna", 0, kContent, config);
    Computer b(network, "Bernd", 0, kContent, config);
    for (Computer* x : {&a, &b}) {
        x->session->reconnect_with([&network] { return network.connect(); });
    }
    run_for(network, *host, {&a, &b}, 2000);
    REQUIRE(host->session->successor() == a.session->local_seat());
    // Every link breaks at once and the host is gone (its HostLeaving reaches nobody).
    network.cut(*a.transport);
    network.cut(*b.transport);
    host.reset();
    for (std::uint32_t t = 0; t < 1000; t += kStepMs) {
        network.advance(kStepMs);
        a.update(kStepMs);
        b.update(kStepMs);
    }
    CHECK(!a.session->ended());  // still trying the host
    for (std::uint32_t t = 0; t < 2000; t += kStepMs) {
        network.advance(kStepMs);
        a.update(kStepMs);
        b.update(kStepMs);
    }
    REQUIRE(a.session->ended());
    CHECK(a.session->end_reason() == net::EndReason::HostLeft);
    CHECK(a.session->hosts_next());
    CHECK(b.session->can_migrate());
}

TEST_CASE("net: a host that leaves on purpose: the others move on at once, spectators too (MS-2, MS-25)") {
    net::LoopbackNetwork network(10, 0);
    auto host = std::make_unique<Host>(network);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    Computer c(network, "Clara");
    Computer w(network, "Wilma");  // watches
    for (Computer* x : {&a, &b, &c, &w}) {
        x->session->reconnect_with([&network] { return network.connect(); });
    }
    run_for(network, *host, {&a, &b, &c, &w}, 1500);
    REQUIRE(w.session->local_seat() == sim::kNoPlayer);
    CHECK(w.session->successor_address().empty());
    host.reset();
    notice_host_left(network, {&a, &b, &c, &w});
    CHECK(a.count(net::SessionEvent::Kind::Reconnecting) == 0);
    CHECK(a.session->hosts_next());
    CHECK(b.session->can_migrate());
    CHECK(w.session->can_migrate());
    CHECK(w.session->successor_address() == "loopback:1");
}

TEST_CASE("net: a computer of another version is told so, however its Hello looks (MS-22)") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    for (const std::uint16_t version :
         {std::uint16_t{net::kProtocolVersion - 1}, std::uint16_t{net::kProtocolVersion + 1}}) {
        auto link = network.connect();
        net::Hello hello;
        hello.content = kContent;
        hello.name = "Zukunft";
        std::vector<std::uint8_t> bytes = net::encode(hello);
        bytes[1] = static_cast<std::uint8_t>(version & 0xFF);
        bytes[2] = static_cast<std::uint8_t>(version >> 8);
        bytes.push_back(0x42);  // a field this version does not know
        std::vector<net::TransportEvent> events;
        std::optional<net::RefuseReason> refused;
        for (int i = 0; i < 30 && !refused; ++i) {
            network.advance(kStepMs);
            host.update(kStepMs);
            events.clear();
            link->poll(events);
            for (const auto& e : events) {
                if (e.kind == net::TransportEvent::Kind::Connected) {
                    link->send(0, bytes);
                } else if (e.kind == net::TransportEvent::Kind::Received) {
                    if (const auto m = net::decode(e.data)) {
                        if (const auto* r = std::get_if<net::Refused>(&*m)) {
                            refused = r->reason;
                        }
                    }
                }
            }
        }
        REQUIRE(refused.has_value());
        CHECK(*refused == net::RefuseReason::Version);
    }
}

TEST_CASE("net: a message larger than a packet travels in parts and arrives whole (MS-7, MS-11)") {
    // Up to a packet: as it is.
    const std::vector<std::uint8_t> small(net::kMaxPacketBytes, 7);
    REQUIRE(net::packets(small).size() == 1);
    CHECK(net::packets(small).front() == small);

    // A fake host sends a game, then a Turn of many frames that only fits into parts.
    net::LoopbackNetwork network(5, 0);
    auto host = network.listen();
    Computer a(network, "Anna");
    Game world = Game::fresh();
    const auto save = sim::write_game(*world.world, buildings(), world.simulation.get());
    REQUIRE(save.ok());
    net::Lobby lobby;
    lobby.started = true;
    lobby.seats.resize(world.world->players().size());
    net::Turn turn;
    turn.first = 0;
    turn.closed = 400;
    for (std::uint64_t f = 0; f < turn.closed; ++f) {
        net::FrameCommands frame{f, {}};
        for (int i = 0; i < 300; ++i) {
            frame.commands.emplace_back(sim::ProductionSwitch{1, 0, 1, 1});
        }
        turn.frames.push_back(std::move(frame));
    }
    const std::vector<std::uint8_t> bytes = net::encode(turn);
    REQUIRE(bytes.size() > net::kMaxPacketBytes);
    const auto parts = net::packets(bytes);
    REQUIRE(parts.size() > 1);
    for (const auto& p : parts) {
        CHECK(p.size() <= net::kMaxPacketBytes);
    }
    std::vector<net::TransportEvent> events;
    bool answered = false;
    for (int i = 0; i < 20; ++i) {
        network.advance(kStepMs);
        events.clear();
        host->poll(events);
        for (const auto& e : events) {
            if (e.kind == net::TransportEvent::Kind::Received && !answered) {
                answered = true;
                host->send(e.link, net::encode(net::Welcome{net::kProtocolVersion, 1, 0xA77A}));
                host->send(e.link, net::encode(lobby));
                host->send(e.link, net::encode(net::Snapshot{1, 0, 1, true, *save}));
                for (const auto& p : parts) {
                    host->send(e.link, p);
                }
            }
        }
        a.update(0);  // it catches up on them, a few at a time
    }
    REQUIRE(a.session->playing());
    CHECK(!a.session->ended());
    CHECK(a.session->frames() + a.session->frames_in_hand() == 400);
    CHECK(a.session->frames() > 0);

    // Another message between the pieces of one is nonsense.
    Computer b(network, "Bernd");
    answered = false;
    for (int i = 0; i < 20; ++i) {
        network.advance(kStepMs);
        events.clear();
        host->poll(events);
        for (const auto& e : events) {
            if (e.kind == net::TransportEvent::Kind::Received && !answered) {
                answered = true;
                host->send(e.link, net::encode(net::Welcome{net::kProtocolVersion, 2, 0xB0B}));
                host->send(e.link, parts.front());
                host->send(e.link, net::encode(lobby));
            }
        }
        b.update(0);
    }
    CHECK(b.session->ended());
    CHECK(b.session->end_reason() == net::EndReason::ProtocolError);
}

TEST_CASE("net: a joining computer tells how far the game on its way has come") {
    using Stage = net::ClientSession::JoinStage;
    net::LoopbackNetwork network(5, 0);
    auto host = network.listen();
    Computer a(network, "Anna");
    net::Lobby lobby;
    lobby.seats.resize(4);
    // A game larger than a packet (one that cannot be loaded: only its way matters here).
    const std::vector<std::uint8_t> game =
        net::encode(net::Snapshot{1, 0, 1, true, std::vector<std::uint8_t>(3 * net::kMaxPacketBytes, 0)});
    const auto parts = net::packets(game);
    REQUIRE(parts.size() > 3);
    std::optional<net::LinkId> link;
    std::vector<std::uint8_t> told;  // LoadProgress the host was sent
    const auto step = [&] {
        network.advance(kStepMs);
        std::vector<net::TransportEvent> events;
        host->poll(events);
        for (const auto& e : events) {
            if (e.kind == net::TransportEvent::Kind::Received && !link) {
                link = e.link;
                host->send(e.link, net::encode(net::Welcome{net::kProtocolVersion, 1, 0xA77A}));
                host->send(e.link, net::encode(lobby));
            } else if (e.kind == net::TransportEvent::Kind::Received) {
                if (const auto m = net::decode(e.data); m && std::holds_alternative<net::LoadProgress>(*m)) {
                    told.push_back(std::get<net::LoadProgress>(*m).percent);
                }
            }
        }
        a.update(kStepMs);
    };
    for (int i = 0; i < 10; ++i) {
        step();
    }
    REQUIRE(a.session->welcomed());
    CHECK(a.session->join_stage() == Stage::None);  // in the lobby, before the start
    CHECK(a.session->total_bytes() == 0);

    lobby.started = true;
    host->send(*link, net::encode(lobby));
    step();
    step();
    CHECK(a.session->join_stage() == Stage::Waiting);  // the host writes the game

    // The pieces one by one: the bytes so far grow towards the whole.
    std::uint64_t before = 0;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        host->send(*link, parts[i]);
        step();
        step();
        CHECK(a.session->join_stage() == Stage::Receiving);
        CHECK(a.session->total_bytes() == game.size());
        CHECK(a.session->received_bytes() > before);
        CHECK(a.session->received_bytes() < game.size());
        before = a.session->received_bytes();
    }
    // The last piece: whole, and loaded only with the next update, so that a frame can say so.
    host->send(*link, parts.back());
    network.advance(kStepMs);
    a.update(kStepMs);
    for (int i = 0; i < 4 && a.session->join_stage() != Stage::Loading; ++i) {
        network.advance(kStepMs);
        a.update(kStepMs);
    }
    CHECK(a.session->join_stage() == Stage::Loading);
    CHECK(a.session->received_bytes() == game.size());
    CHECK(!a.session->ended());
    // The host heard how far it came, rising, and of the whole game at once.
    step();
    REQUIRE(told.size() >= 2);
    CHECK(std::is_sorted(told.begin(), told.end()));
    CHECK(told.front() > 0);
    CHECK(told.front() < 100);
    CHECK(told.back() == 100);
    CHECK(std::adjacent_find(told.begin(), told.end()) == told.end());  // each only once
    a.update(kStepMs);
    CHECK(a.session->ended());
    CHECK(a.session->end_reason() == net::EndReason::LoadFailed);
    CHECK(a.session->join_stage() == Stage::None);
}

TEST_CASE("net: the host takes a few spectators, not any number (MS-12)") {
    net::LoopbackNetwork network(5, 0);
    net::SessionConfig config;
    config.max_spectators = 2;
    Host host(network, config);
    Computer a(network, "Anna");
    Computer b(network, "Bernd");
    Computer c(network, "Clara");  // every slot taken
    Computer w1(network, "W1");
    Computer w2(network, "W2");
    Computer w3(network, "W3");
    run_for(network, host, {&a, &b, &c, &w1, &w2, &w3}, 1500);
    CHECK(w1.session->playing());
    CHECK(w2.session->playing());
    CHECK(w3.session->ended());
    REQUIRE(!w3.events.empty());
    CHECK(w3.events.back().refused == net::RefuseReason::Full);
}

namespace {

/// A lobby of four slots, the third the computer's, whose host waits for everyone at the start
/// (SessionConfig::start_wait_ms); Anna and Bernd join it.
struct StartingLobby {
    net::LoopbackNetwork network{30, 10};
    std::unique_ptr<net::HostSession> host;
    std::vector<net::SessionEvent> host_events;
    std::optional<Game> host_game;
    std::optional<Computer> anna;
    std::optional<Computer> bernd;
    bool bernd_frozen = false;  ///< Bernd's program stands still (loading, a debugger)

    explicit StartingLobby(std::uint32_t start_wait_ms) {
        net::SessionConfig config;
        config.start_wait_ms = start_wait_ms;
        net::Lobby setup;
        setup.seats = {net::LobbySeat{}, net::LobbySeat{}, net::LobbySeat{net::LobbySeat::Kind::Computer, "", false},
                       net::LobbySeat{}};
        host = std::make_unique<net::HostSession>(network.listen(), setup, 0, "Merlin", kContent, config);
        anna.emplace(network, "Anna");
        tick(300);
        bernd.emplace(network, "Bernd");
        tick(300);
        REQUIRE(anna->session->local_seat() == 1);
        REQUIRE(bernd->session->local_seat() == 3);
    }

    void tick(std::uint32_t ms) {
        for (std::uint32_t t = 0; t < ms; t += kStepMs) {
            network.advance(kStepMs);
            host->update(kStepMs);
            for (auto& e : host->take_events()) {
                host_events.push_back(std::move(e));
            }
            anna->update(kStepMs);
            if (bernd && !bernd_frozen) {
                bernd->update(kStepMs);
            }
        }
    }

    void start() {
        host_game.emplace(Game::from_lobby(host->lobby()));
        host->start(*host_game->simulation, buildings());
    }

    [[nodiscard]] std::size_t host_count(net::SessionEvent::Kind kind) const {
        return static_cast<std::size_t>(std::count_if(host_events.begin(), host_events.end(),
                                                      [kind](const net::SessionEvent& e) { return e.kind == kind; }));
    }
};

const net::Member* member(const net::Session& session, std::string_view name) {
    const auto& roster = session.roster();
    const auto it = std::find_if(roster.begin(), roster.end(), [&](const net::Member& m) { return m.name == name; });
    return it != roster.end() ? &*it : nullptr;
}

}  // namespace

TEST_CASE("net: the game starts for everyone at once: its first frame waits until every player has it") {
    StartingLobby lobby(60'000);
    lobby.bernd_frozen = true;  // Bernd's computer is slow to take the game
    lobby.start();
    lobby.tick(3000);
    net::HostSession& host = *lobby.host;
    Computer& anna = *lobby.anna;
    Computer& bernd = *lobby.bernd;
    // The host holds frame 0 for Bernd; Anna has the game, but no frame to run either.
    CHECK(host.starting());
    CHECK(host.frames() == 0);
    CHECK(host.waiting());
    REQUIRE(anna.session->playing());
    CHECK(anna.session->frames() == 0);
    CHECK(anna.session->starting());
    CHECK(anna.count(net::SessionEvent::Kind::Started) == 0);
    // Everyone sees who is ready and who is still loading.
    const net::Member* b = member(*anna.session, "Bernd");
    REQUIRE(b != nullptr);
    CHECK(b->state == net::Member::State::Loading);
    const net::Member* a = member(*anna.session, "Anna");
    REQUIRE(a != nullptr);
    CHECK(a->state == net::Member::State::Playing);
    CHECK(a->loaded == 100);

    // Bernd's computer takes the game: then all begin, at the same frame.
    lobby.bernd_frozen = false;
    for (int i = 0; i < 400 && host.starting(); ++i) {
        lobby.tick(kStepMs);
    }
    CHECK(!host.starting());
    CHECK(lobby.host_count(net::SessionEvent::Kind::Started) == 1);
    lobby.tick(2000);
    REQUIRE(bernd.session->playing());
    CHECK(!anna.session->starting());
    CHECK(!bernd.session->starting());
    CHECK(anna.count(net::SessionEvent::Kind::Started) == 1);
    CHECK(bernd.count(net::SessionEvent::Kind::Started) == 1);
    CHECK(host.frames() > 0);
    // Nobody ran ahead: all three a few frames apart at most, not seconds.
    const auto gap = [](std::uint64_t x, std::uint64_t y) { return x > y ? x - y : y - x; };
    CHECK(gap(host.frames(), anna.session->frames()) <= 10);
    CHECK(gap(host.frames(), bernd.session->frames()) <= 10);
    CHECK(member(*bernd.session, "Bernd")->state == net::Member::State::Playing);
}

TEST_CASE("net: a player whose game makes no progress at the start is not waited for for ever") {
    StartingLobby lobby(2'000);
    lobby.start();
    lobby.tick(400);  // the game is on its way to everyone
    lobby.bernd_frozen = true;
    lobby.tick(4000);
    // Bernd stood still too long: the others began without him.
    CHECK(!lobby.host->starting());
    CHECK(lobby.host->frames() > 0);
    CHECK(lobby.anna->count(net::SessionEvent::Kind::Started) == 1);
    // He catches up once his computer goes on.
    lobby.bernd_frozen = false;
    lobby.tick(4000);
    CHECK(lobby.bernd->session->playing());
    CHECK(lobby.bernd->session->frames() > 0);
    CHECK(!lobby.bernd->session->ended());
}

TEST_CASE("net: the host stops waiting for the players still loading at the start") {
    StartingLobby lobby(60'000);
    lobby.bernd_frozen = true;
    lobby.start();
    lobby.tick(1000);
    REQUIRE(lobby.host->starting());
    lobby.host->start_now();
    lobby.tick(1000);
    CHECK(!lobby.host->starting());
    CHECK(lobby.host->frames() > 0);
    CHECK(lobby.anna->session->frames() > 0);
    CHECK(lobby.host_count(net::SessionEvent::Kind::Started) == 1);
}

TEST_CASE("net: without waiting at the start the host's game runs at once") {
    StartingLobby lobby(0);
    lobby.bernd_frozen = true;
    lobby.start();
    lobby.tick(1000);
    CHECK(!lobby.host->starting());
    CHECK(lobby.host->frames() > 0);
    CHECK(lobby.host_count(net::SessionEvent::Kind::Started) == 0);  // nothing was held
}

TEST_CASE("net: a game loaded in the background: the computer goes on meanwhile, then plays in step") {
    net::LoopbackNetwork network(20, 5);
    Host host(network);
    Computer a(network, "Anna");
    // Reading the game waits for the test: as a large world on a slow computer.
    std::promise<void> go;
    std::shared_future<void> gate = go.get_future().share();
    std::atomic<int> prepared = 0;
    a.session->load_in_background([&a, gate, &prepared](std::vector<std::uint8_t> save) -> net::ClientSession::Finish {
        gate.wait();
        ++prepared;
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::move(save));
        return [&a, bytes] { return a.game.load(*bytes); };
    });
    run_for(network, host, {&a}, 1500);
    // The game is here and being read: nothing of it runs, the session still answers at once.
    CHECK(!a.session->playing());
    CHECK(a.count(net::SessionEvent::Kind::Loaded) == 0);
    CHECK(!a.session->ended());
    CHECK(prepared == 0);
    const std::uint64_t host_frames = host.session->frames();
    CHECK(host_frames > 0);

    go.set_value();
    for (int i = 0; i < 1000 && !a.session->playing(); ++i) {
        step(network, host, {&a});
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(a.session->playing());
    CHECK(prepared == 1);
    CHECK(a.count(net::SessionEvent::Kind::Loaded) == 1);
    // The frames that came while it was read were kept, in order: it catches up and stays in step.
    run_for(network, host, {&a}, 12'000);
    CHECK(!a.session->ended());
    CHECK(a.session->frames() + 20 > host.session->frames());
    CHECK(host.session->fingerprints_compared() > 0);
    CHECK(host.session->fingerprints_matched() == host.session->fingerprints_compared());
}

TEST_CASE("net: a game that cannot be read in the background ends the session as one read at once") {
    net::LoopbackNetwork network(5, 0);
    Host host(network);
    Computer a(network, "Anna");
    a.session->load_in_background(
        [](std::vector<std::uint8_t>) -> net::ClientSession::Finish { throw std::runtime_error("damaged"); });
    for (int i = 0; i < 500 && !a.session->ended(); ++i) {
        step(network, host, {&a});
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(a.session->ended());
    CHECK(a.session->end_reason() == net::EndReason::LoadFailed);
}

TEST_CASE("net: over real UDP, a game read for longer than the connection's timeout keeps the connection") {
    constexpr std::uint16_t kPort = 46022;
    auto listening = net::enet_listen(kPort, 4);
    if (!listening) {
        SKIP("UDP port 46022 is not free here");
    }
    auto connecting = net::enet_connect("127.0.0.1", kPort);
    REQUIRE(connecting.ok());

    Game host_game = Game::fresh();
    net::HostSession host(std::move(listening).value(), *host_game.simulation, buildings(), 0, "Gastgeber", kContent);
    Game game;
    net::Hello hello;
    hello.content = kContent;
    hello.name = "Anna";
    net::ClientSession client(std::move(connecting).value(), hello, [](std::span<const std::uint8_t>) {
        return nullptr;  // not used: the game is read in the background
    });
    // Reading takes 10 s, longer than a link that hears nothing lives here (without hearing the host
    // meanwhile, the host drops it).
    client.load_in_background([&game](std::vector<std::uint8_t> save) -> net::ClientSession::Finish {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::move(save));
        return [&game, bytes] { return game.load(*bytes); };
    });
    const auto start = std::chrono::steady_clock::now();
    auto last = start;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(25) &&
           !(client.playing() && client.frames() > 20) && !client.ended()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto now = std::chrono::steady_clock::now();
        const auto ms = static_cast<std::uint32_t>(
            std::min<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count(), 100));
        last = now;
        host.update(ms);
        client.update(ms);
    }
    CHECK(!client.ended());
    REQUIRE(client.playing());
    CHECK(client.frames() > 20);
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::seconds(10));
    CHECK(host.roster().size() == 2);  // the host never let it go
}
