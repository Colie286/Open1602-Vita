// SPDX-License-Identifier: GPL-3.0-or-later
// Commands as the network and replays carry them (ADR 0004): every kind as bytes and back, nothing
// from damaged bytes, carried out at the tick they name, and a fingerprint of the game that tells
// whether two computers still compute the same.

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/figure_table.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/sim/command_io.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

/// One command of every kind, with fields away from their defaults.
std::vector<sim::Command> every_kind() {
    std::vector<sim::Command> all;
    sim::BuildOrder build;
    build.player = 1;
    build.island = 3;
    build.items = {{20501, 7, -2, 3, 1}, {20401, 9, 4, 0, 0}};
    build.ship = 12;
    build.orient_to_road = true;
    all.emplace_back(build);
    all.emplace_back(sim::DemolishOrder{2, 4, {5, 6}, {7, 8}});
    sim::ShipOrder ship;
    ship.player = 1;
    ship.ship = 77;
    ship.attack = true;
    ship.queue = true;
    all.emplace_back(ship);
    sim::CargoTransfer cargo;
    cargo.player = 1;
    cargo.ship = 5;
    cargo.amount = 320;
    cargo.kontor_island = 3;
    cargo.kontor_city = 2;
    all.emplace_back(cargo);
    sim::RouteEdit route;
    route.player = 1;
    route.ship = 5;
    route.op = sim::RouteEdit::Op::OneWay;
    route.on = true;
    all.emplace_back(route);
    sim::TradeSetting trade;
    trade.player = 1;
    trade.mode = sim::TradeSetting::Mode::Buy;
    trade.price = 123;
    all.emplace_back(trade);
    all.emplace_back(sim::ShipyardOrder{});
    all.emplace_back(sim::ExploreOrder{});
    all.emplace_back(sim::DiplomacyOrder{});
    sim::SoldierOrder soldiers;
    soldiers.player = 2;
    soldiers.soldiers = {4, 5, 6};
    soldiers.queue = true;
    all.emplace_back(soldiers);
    all.emplace_back(sim::SoldierCommand{});
    all.emplace_back(sim::CastleOrder{});
    all.emplace_back(sim::UnloadOrder{});
    all.emplace_back(sim::ProductionSwitch{1, 2, 3, 4, false, sim::ProductionSwitch::Op::Pickup});
    sim::CityOrder city;
    city.player = 1;
    city.op = sim::CityOrder::Op::Rename;
    city.name = "Neustadt";
    all.emplace_back(city);
    sim::ShipCommand rename_ship;
    rename_ship.player = 1;
    rename_ship.name = "Seeadler";
    all.emplace_back(rename_ship);
    all.emplace_back(sim::GameSpeedOrder{1, sim::GameSpeedOrder::Op::SetSpeed, 4});
    all.emplace_back(sim::PlayerRename{1, "Merlin"});
    all.emplace_back(sim::PlayerControl{1, true, 0xFEDC'BA98'7654'3210ULL});
    all.emplace_back(sim::GateOrder{1, true});
    all.emplace_back(sim::CartRequest{1, 2, 3, 40, 50, 41, 52});
    sim::BlueprintOrder blueprint;
    blueprint.player = 1;
    blueprint.kind = sim::BlueprintOrder::Kind::Auto;
    blueprint.island = 2;
    blueprint.items = {{7, 3, -4, 1, 0}};
    blueprint.from = {1, 2};
    blueprint.to = {5, 6};
    blueprint.city = 3;
    blueprint.on = true;
    all.emplace_back(blueprint);
    sim::MoveOrder move;
    move.player = 1;
    move.island = 2;
    move.from = {3, 4};
    move.x = -5;
    move.y = 6;
    move.rotation = 3;
    move.orient_to_road = false;
    all.emplace_back(move);
    return all;
}

/// The command of kind T in `all`, after going to bytes and back.
template <class T>
T round_trip(const std::vector<sim::Command>& all) {
    for (const sim::Command& c : all) {
        if (std::holds_alternative<T>(c)) {
            return std::get<T>(*sim::decode_command(sim::encode_command(c)));
        }
    }
    return T{};
}

struct Game {
    sim::World world{64, 64};
    assets::BuildingTable buildings;
    assets::FigureTable figures;
    sim::Simulation simulation;

    Game() : simulation(make(world), buildings, figures) {}

    static sim::World& make(sim::World& world) {
        std::vector<sim::Player> players(2);
        for (std::size_t p = 0; p < players.size(); ++p) {
            players[p].role = sim::PlayerRole::Human;
            players[p].banner = static_cast<sim::BannerId>(p);
            players[p].name = "Spieler " + std::to_string(p);
        }
        world.set_players(players);
        return world;
    }

    [[nodiscard]] const std::string& name(std::size_t p) const { return world.players()[p].name; }
};

}  // namespace

TEST_CASE("commands: every kind of command goes to bytes and back") {
    const auto all = every_kind();
    CHECK(all.size() == std::variant_size_v<sim::Command>);
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK(all[i].index() == i);
        const auto bytes = sim::encode_command(all[i]);
        CHECK(bytes[0] == i);
        const auto back = sim::decode_command(bytes);
        REQUIRE(back.has_value());
        CHECK(back->index() == i);
        CHECK(sim::encode_command(*back) == bytes);
    }
    const auto build = std::get<sim::BuildOrder>(*sim::decode_command(sim::encode_command(all[0])));
    CHECK(build.player == 1);
    REQUIRE(build.items.size() == 2);
    CHECK(build.items[0].y == -2);
    CHECK(build.orient_to_road);
    const auto rename = round_trip<sim::PlayerRename>(all);
    CHECK(rename.name == "Merlin");
    sim::ShipCommand mount;
    mount.op = sim::ShipCommand::Op::MountCannon;
    mount.count = sim::ShipCommand::kAllCannons;
    const auto mounted = std::get<sim::ShipCommand>(*sim::decode_command(sim::encode_command(mount)));
    CHECK(mounted.op == sim::ShipCommand::Op::MountCannon);
    CHECK(mounted.count == sim::ShipCommand::kAllCannons);
    const sim::CastleOrder all_out{1, 2, 3, 4, sim::CastleOrder::Op::ReleaseAll, 0, 0, sim::Target::spot(9, 8)};
    const auto sent = std::get<sim::CastleOrder>(*sim::decode_command(sim::encode_command(all_out)));
    CHECK(sent.op == sim::CastleOrder::Op::ReleaseAll);
    CHECK(sent.spot == sim::Target::spot(9, 8));
    const auto gates = round_trip<sim::GateOrder>(all);
    CHECK(gates.player == 1);
    CHECK(gates.closed);
    const auto control = round_trip<sim::PlayerControl>(all);
    CHECK(control.player == 1);
    CHECK(control.computer);
    CHECK(control.owner == 0xFEDC'BA98'7654'3210ULL);
    const auto cart = round_trip<sim::CartRequest>(all);
    CHECK(cart.island == 2);
    CHECK(cart.city == 3);
    CHECK(cart.x == 40);
    CHECK(cart.y == 50);
    CHECK(cart.from_x == 41);
    CHECK(cart.from_y == 52);
    const auto blueprint = round_trip<sim::BlueprintOrder>(all);
    CHECK(blueprint.kind == sim::BlueprintOrder::Kind::Auto);
    REQUIRE(blueprint.items.size() == 1);
    CHECK(blueprint.items[0].y == -4);
    CHECK((blueprint.to == sim::TilePos{5, 6}));
    CHECK(blueprint.city == 3);
    CHECK(blueprint.on);
    const auto move = round_trip<sim::MoveOrder>(all);
    CHECK(move.island == 2);
    CHECK((move.from == sim::TilePos{3, 4}));
    CHECK(move.x == -5);
    CHECK(move.y == 6);
    CHECK(move.rotation == 3);
    CHECK(!move.orient_to_road);

    // With the tick, as the network carries them, one after another.
    core::ByteWriter out;
    sim::write_scheduled(out, {1234567890123ULL, all[0]});
    sim::write_scheduled(out, {7, sim::Command{sim::PlayerRename{1, "Merlin"}}});
    const auto stream = out.take();
    core::ByteReader in(stream);
    const auto first = sim::read_scheduled(in);
    const auto second = sim::read_scheduled(in);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->tick == 1234567890123ULL);
    CHECK(second->tick == 7);
    CHECK(std::holds_alternative<sim::PlayerRename>(second->command));
    CHECK(in.at_end());
}

TEST_CASE("commands: damaged or hostile bytes give no command") {
    for (const auto& command : every_kind()) {
        const auto bytes = sim::encode_command(command);
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            CHECK(!sim::decode_command(std::span(bytes.data(), n)).has_value());
        }
        auto longer = bytes;
        longer.push_back(0);
        CHECK(!sim::decode_command(longer).has_value());
    }
    // A kind this version does not know.
    CHECK(!sim::decode_command(std::vector<std::uint8_t>{0xFF}).has_value());

    const auto bytes_of = [](const sim::Command& c) { return sim::encode_command(c); };
    // ProductionSwitch: player, island u16, x u16, y u16, on, op.
    auto prod = bytes_of(sim::ProductionSwitch{1, 2, 3, 4, true, sim::ProductionSwitch::Op::Enable});
    REQUIRE(prod.size() == 10);
    prod[8] = 2;  // a bool is 0 or 1
    CHECK(!sim::decode_command(prod).has_value());
    prod[8] = 1;
    prod[9] = 2;  // no such op
    CHECK(!sim::decode_command(prod).has_value());

    // PlayerRename: player, length u32, bytes.
    auto rename = bytes_of(sim::PlayerRename{1, std::string(sim::kMaxCommandText, 'a')});
    CHECK(sim::decode_command(rename).has_value());
    rename = bytes_of(sim::PlayerRename{1, std::string(sim::kMaxCommandText + 1, 'a')});
    CHECK(!sim::decode_command(rename).has_value());

    // A list that claims more items than there are bytes, or than any order holds.
    sim::SoldierOrder soldiers;
    soldiers.player = 1;
    soldiers.soldiers.push_back(1);
    auto order = bytes_of(sim::Command{std::move(soldiers)});
    REQUIRE(order.size() > 6);
    for (const std::uint32_t claim : {2U, sim::kMaxCommandItems + 1, 0xFFFFFFFFU}) {
        for (std::size_t b = 0; b < 4; ++b) {
            order[2 + b] = static_cast<std::uint8_t>(claim >> (8 * b));
        }
        CHECK(!sim::decode_command(order).has_value());
    }
}

TEST_CASE("commands: a command is carried out at the tick it names, by player, never late") {
    Game g;
    CHECK(g.simulation.next_tick() == 0);
    // Scheduled out of order: player 1 first, then player 0, both for tick 3.
    const auto seq1 = g.simulation.schedule(3, sim::PlayerRename{1, "Zweiter"});
    const auto seq0 = g.simulation.schedule(3, sim::PlayerRename{0, "Erster"});
    const auto seq1b = g.simulation.schedule(3, sim::PlayerRename{1, "Zweiter, neu"});
    for (int t = 0; t < 3; ++t) {
        g.simulation.tick();
    }
    CHECK(g.simulation.next_tick() == 3);
    CHECK(g.name(0) == "Spieler 0");
    CHECK(g.simulation.take_command_results().empty());
    g.simulation.tick();
    CHECK(g.name(0) == "Erster");
    CHECK(g.name(1) == "Zweiter, neu");  // one player's commands keep their order
    const auto results = g.simulation.take_command_results();
    REQUIRE(results.size() == 3);
    CHECK(results[0].sequence == seq0);  // player 0 before player 1, whoever was first
    CHECK(results[1].sequence == seq1);
    CHECK(results[2].sequence == seq1b);
    CHECK(results[0].accepted);

    // Too late: tick 2 has run. Refused, so this computer does not diverge from the others.
    const auto late = g.simulation.schedule(2, sim::PlayerRename{0, "Zu spät"});
    g.simulation.tick();
    CHECK(g.name(0) == "Erster");
    const auto refused = g.simulation.take_command_results();
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].sequence == late);
    CHECK(refused[0].late);
    CHECK(!refused[0].accepted);
}

TEST_CASE("commands: while paused, the commands of the waiting tick still apply, later ones wait") {
    Game g;
    g.simulation.submit(sim::GameSpeedOrder{0, sim::GameSpeedOrder::Op::Pause, 0});
    g.simulation.advance(200);
    CHECK(g.simulation.paused());
    const std::uint64_t at = g.simulation.next_tick();
    g.simulation.advance(10'000);
    CHECK(g.simulation.next_tick() == at);

    g.simulation.submit(sim::PlayerRename{0, "Jetzt"});
    g.simulation.schedule(at + 1, sim::PlayerRename{1, "Später"});
    g.simulation.advance(50);
    CHECK(g.name(0) == "Jetzt");
    CHECK(g.name(1) == "Spieler 1");

    g.simulation.submit(sim::GameSpeedOrder{1, sim::GameSpeedOrder::Op::Resume, 0});
    g.simulation.advance(sim::Simulation::kTickMs);
    CHECK(!g.simulation.paused());
    g.simulation.advance(2 * sim::Simulation::kTickMs);
    CHECK(g.name(1) == "Später");
}

TEST_CASE("commands: a saved game keeps its speed and its pause; older saves load running") {
    Game a;
    a.simulation.submit(sim::GameSpeedOrder{0, sim::GameSpeedOrder::Op::SetSpeed, 4});
    a.simulation.submit(sim::GameSpeedOrder{0, sim::GameSpeedOrder::Op::Pause, 0});
    a.simulation.tick();
    REQUIRE(a.simulation.paused());
    REQUIRE(a.simulation.speed() == 4);
    core::ByteWriter out;
    a.simulation.save_state(out);
    const std::vector<std::uint8_t> state = out.take();
    REQUIRE(state.size() > 5);

    // As a loaded game does: the world hands the saved state to a new simulation.
    const auto load = [](std::vector<std::uint8_t> bytes) {
        auto g = std::make_unique<Game>();
        g->world.set_simulation_state(std::move(bytes));
        auto s = std::make_unique<sim::Simulation>(g->world, g->buildings, g->figures);
        return std::make_pair(std::move(g), std::move(s));
    };
    const auto [gb, b] = load(state);
    CHECK(!b->state_damaged());
    CHECK(b->paused());
    CHECK(b->speed() == 4);

    // The speed and the pause end the part SIMC, the first after the u16 of the state (a part's
    // header is 12 bytes, its size at +8). Games of state version 5 had neither and run at speed 1
    // (tests/unit/saves, "save game: games of every earlier state version load").
    REQUIRE(state[2] == 'S' && state[3] == 'I' && state[4] == 'M' && state[5] == 'C');
    const std::size_t clocks_end = 14 + (std::size_t{state[10]} | std::size_t{state[11]} << 8 |
                                         std::size_t{state[12]} << 16 | std::size_t{state[13]} << 24);
    REQUIRE(clocks_end <= state.size());

    // A pause flag other than 0 or 1, or speed 0, marks the state as damaged.
    std::vector<std::uint8_t> bad = state;
    bad[clocks_end - 1] = 2;
    const auto [gd, d] = load(bad);
    CHECK(d->state_damaged());
    CHECK(!d->paused());
    bad = state;
    std::fill(bad.begin() + static_cast<std::ptrdiff_t>(clocks_end) - 5,
              bad.begin() + static_cast<std::ptrdiff_t>(clocks_end) - 1, std::uint8_t{0});
    const auto [ge, e] = load(bad);
    CHECK(e->state_damaged());
}

TEST_CASE("commands: the fingerprint of the game is the same for the same game, and only then") {
    Game a;
    Game b;
    const auto start = a.simulation.state_hash();
    CHECK(b.simulation.state_hash() == start);
    for (int t = 0; t < 40; ++t) {
        a.simulation.tick();
        b.simulation.tick();
    }
    CHECK(a.simulation.state_hash() == b.simulation.state_hash());
    CHECK(a.simulation.state_hash() != start);  // the game time is part of it

    a.simulation.submit(sim::PlayerRename{1, "Abweichler"});
    a.simulation.tick();
    b.simulation.tick();
    CHECK(a.simulation.state_hash() != b.simulation.state_hash());
    b.simulation.submit(sim::PlayerRename{1, "Abweichler"});
    a.simulation.tick();
    b.simulation.tick();
    CHECK(a.simulation.state_hash() == b.simulation.state_hash());  // the same game again
}

TEST_CASE("commands: a slot changes hands between the computer and a person") {
    Game g;
    g.simulation.schedule(0, sim::PlayerControl{1, true});
    g.simulation.tick();
    CHECK(g.world.players()[1].role == sim::PlayerRole::Computer);
    g.simulation.schedule(1, sim::PlayerControl{1, false});
    g.simulation.tick();
    CHECK(g.world.players()[1].role == sim::PlayerRole::Human);
    CHECK(g.world.players()[1].owner == 0);
    (void)g.simulation.take_command_results();

    // The game keeps who took the slot; the computer taking over leaves it.
    g.simulation.schedule(2, sim::PlayerControl{1, false, 42});
    g.simulation.tick();
    CHECK(g.world.players()[1].owner == 42);
    g.simulation.schedule(3, sim::PlayerControl{1, true});
    g.simulation.tick();
    CHECK(g.world.players()[1].role == sim::PlayerRole::Computer);
    CHECK(g.world.players()[1].owner == 42);
    g.simulation.schedule(4, sim::PlayerControl{1, false, 43});
    g.simulation.tick();
    CHECK(g.world.players()[1].owner == 43);
    (void)g.simulation.take_command_results();

    // Nobody steers a slot that is out of the game.
    g.world.player(0)->status = sim::PlayerStatus::Absent;
    g.simulation.schedule(5, sim::PlayerControl{0, true});
    g.simulation.tick();
    CHECK(g.world.players()[0].role == sim::PlayerRole::Human);
    const auto results = g.simulation.take_command_results();
    REQUIRE(results.size() == 1);
    CHECK(!results[0].accepted);
}

TEST_CASE("players: alone, a person plays the human player they played before") {
    Game g;  // two human players
    g.world.player(1)->owner = 77;
    CHECK(sim::take_single_player(g.world, 77) == 1);
    CHECK(g.world.players()[1].role == sim::PlayerRole::Human);
    CHECK(g.world.players()[0].role == sim::PlayerRole::Computer);  // the computer plays the other
    CHECK(sim::first_human_player(g.world) == 1);

    // Someone the game does not know: the first human player, which is theirs from now on.
    Game other;
    other.world.player(1)->owner = 77;
    CHECK(sim::take_single_player(other.world, 88) == 0);
    CHECK(other.world.players()[0].owner == 88);
    CHECK(other.world.players()[1].role == sim::PlayerRole::Computer);
    CHECK(other.world.players()[1].owner == 77);  // kept for that person

    // Without an ID nothing is given; a world without people stays as it is.
    Game none;
    CHECK(sim::take_single_player(none.world, 0) == 0);
    CHECK(none.world.players()[0].owner == 0);
    for (std::size_t p = 0; p < none.world.players().size(); ++p) {
        none.world.player(static_cast<sim::PlayerId>(p))->role = sim::PlayerRole::Computer;
    }
    CHECK(sim::take_single_player(none.world, 5) == sim::kNoPlayer);
    CHECK(none.world.players()[0].owner == 0);
}

TEST_CASE("players: a person chooses the player of a game that does not know them") {
    Game g;  // two human players
    CHECK((sim::human_players(g.world) == std::vector<sim::PlayerId>{0, 1}));
    sim::take_player(g.world, 1, 99);
    CHECK(g.world.players()[1].role == sim::PlayerRole::Human);
    CHECK(g.world.players()[1].owner == 99);
    CHECK(g.world.players()[0].role == sim::PlayerRole::Computer);
    CHECK((sim::human_players(g.world) == std::vector<sim::PlayerId>{1}));
    // Not a human player (any more): nothing changes.
    sim::take_player(g.world, 0, 99);
    CHECK(g.world.players()[0].role == sim::PlayerRole::Computer);
    CHECK(g.world.players()[0].owner == 0);
    // A player out of the game is not offered.
    Game h;
    h.world.player(0)->status = sim::PlayerStatus::Absent;
    CHECK((sim::human_players(h.world) == std::vector<sim::PlayerId>{1}));
}

TEST_CASE("commands: a player closes and opens his city gates") {
    Game g;
    g.simulation.schedule(0, sim::GateOrder{1, true});
    g.simulation.tick();
    CHECK(g.world.players()[1].gates_closed);
    CHECK(!g.world.players()[0].gates_closed);
    g.simulation.schedule(1, sim::GateOrder{1, false});
    g.simulation.tick();
    CHECK(!g.world.players()[1].gates_closed);
    (void)g.simulation.take_command_results();

    // Not for a player out of the game.
    g.world.player(0)->status = sim::PlayerStatus::Absent;
    g.simulation.schedule(2, sim::GateOrder{0, true});
    g.simulation.tick();
    CHECK(!g.world.players()[0].gates_closed);
    const auto results = g.simulation.take_command_results();
    REQUIRE(results.size() == 1);
    CHECK(!results[0].accepted);
}

TEST_CASE("simulation: the steps of a tick run in the original's order (review point 28)") {
    const std::vector<std::string_view> expected = {"diplomacy", "growth",     "production", "cities",  "city events",
                                                    "houses",    "upkeep",     "disasters",  "islands", "volcanoes",
                                                    "computers", "blueprints", "combat",     "ships",   "soldiers",
                                                    "logistics", "statistics"};
    CHECK(sim::Simulation::tick_step_names() == expected);
}

TEST_CASE("simulation: a timed tick goes on exactly like a tick and times the steps it is given") {
    Game a;
    Game b;
    const std::size_t steps = sim::Simulation::tick_step_names().size();
    std::vector<std::uint64_t> all(steps, 0);
    std::vector<std::uint64_t> first(1, 0);  // only the first step is timed
    for (int t = 0; t < 40; ++t) {
        a.simulation.tick();
        b.simulation.tick_timed(t % 2 == 0 ? std::span<std::uint64_t>(all) : std::span<std::uint64_t>(first));
    }
    CHECK(a.simulation.game_time_ms() == b.simulation.game_time_ms());
    CHECK(a.simulation.state_hash() == b.simulation.state_hash());
    b.simulation.tick_timed({});  // nothing to time
    CHECK(b.simulation.game_time_ms() == a.simulation.game_time_ms() + sim::Simulation::kTickMs);
}

TEST_CASE("simulation: events nobody takes are dropped beyond the bound, the oldest first") {
    sim::EventBuffer<std::uint32_t> buffer;
    for (std::uint32_t i = 0; i < 3 * sim::EventBuffer<std::uint32_t>::kMaxPending; ++i) {
        buffer.push(i);
        REQUIRE(buffer.size() <= sim::EventBuffer<std::uint32_t>::kMaxPending + 1);
    }
    const std::vector<std::uint32_t> events = buffer.take();
    REQUIRE(!events.empty());
    CHECK(events.back() == 3 * sim::EventBuffer<std::uint32_t>::kMaxPending - 1);
    for (std::size_t i = 1; i < events.size(); ++i) {
        CHECK(events[i] == events[i - 1] + 1);
    }
    CHECK(buffer.size() == 0);

    // What a subsystem wrote into the buffer is bounded by trim().
    buffer.sink()->resize(sim::EventBuffer<std::uint32_t>::kMaxPending + 10);
    buffer.trim();
    CHECK(buffer.size() == sim::EventBuffer<std::uint32_t>::kMaxPending / 2);
}
