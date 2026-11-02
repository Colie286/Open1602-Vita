// SPDX-License-Identifier: GPL-3.0-or-later
// The scripts of a game's packages (engine/script/include/o1602/script/game_scripts.hpp,
// docs/modding/scripts.md): the game's functions, the saved table `state`, the events of each tick
// and the savegame section SCRP.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../test_framework.hpp"
#include "test_support.hpp"

#include "o1602/assets/figure_table.hpp"
#include "o1602/script/game_scripts.hpp"
#include "o1602/script/sandbox.hpp"
#include "o1602/sim/savegame_extension.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

struct Game {
    sim::World world{64, 64};
    assets::BuildingTable buildings;
    assets::FigureTable figures;
    sim::Simulation simulation;

    Game() : simulation(make(world), buildings, figures) {}
    ~Game() { simulation.set_scripts(nullptr); }
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    static sim::World& make(sim::World& world) {
        std::vector<sim::Player> players(2);
        players[0].name = "Merlin";
        players[0].money = 5000;
        players[1].role = sim::PlayerRole::Computer;
        players[1].name = "Ex\xe9ter";  // Windows-1252: the scripts see UTF-8
        world.set_players(players);
        return world;
    }

    /// Runs the scripts of `sources` (package id, text) in this game.
    std::unique_ptr<script::GameScripts> run(std::vector<script::PackageScript> sources) {
        auto scripts = std::make_unique<script::GameScripts>(std::move(sources), world);
        simulation.set_scripts(scripts.get());
        return scripts;
    }

    void ticks(int count) {
        for (int i = 0; i < count; ++i) {
            simulation.tick();
        }
    }
};

std::vector<script::PackageScript> one(std::string package, std::string source) {
    std::vector<script::PackageScript> sources;
    sources.push_back(script::PackageScript{std::move(package), std::move(source), {}});
    return sources;
}

bool has_error(const std::vector<script::ScriptNote>& notes, const std::string& part) {
    for (const auto& note : notes) {
        if (note.error && note.text.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("script: the game's functions take and give values and tables") {
    script::Sandbox sandbox;
    std::vector<script::Value> seen;
    REQUIRE(sandbox
                .add_function("game", "echo",
                              [&seen](std::span<const script::Value> args) -> core::Result<script::Value> {
                                  seen.assign(args.begin(), args.end());
                                  return script::make_table({{"name", std::string("fleet")}},
                                                            {script::Value{std::int64_t{7}}});
                              })
                .ok());
    REQUIRE(sandbox
                .add_function("game", "refuse",
                              [](std::span<const script::Value>) -> core::Result<script::Value> {
                                  return core::make_error(core::ErrorCode::ScriptError, "not now");
                              })
                .ok());
    REQUIRE(sandbox.add_value("game", "package", script::Value{std::string("lighthouse")}).ok());
    REQUIRE(sandbox
                .run("t",
                     "local r = game.echo(1, 'two', {b = true, a = 2.5, 'x', 'y'})\n"
                     "function result() return game.package .. r.name .. r[1] end")
                .ok());
    REQUIRE(seen.size() == 3);
    CHECK(std::get<std::int64_t>(seen[0]) == 1);
    CHECK(std::get<std::string>(seen[1]) == "two");
    const auto& table = *std::get<std::shared_ptr<const script::Table>>(seen[2]);
    REQUIRE(table.fields.size() == 2);
    CHECK(table.fields[0].first == "a");  // sorted by name
    CHECK(std::get<double>(table.fields[0].second) == 2.5);
    CHECK(std::get<bool>(table.fields[1].second));
    REQUIRE(table.items.size() == 2);
    CHECK(std::get<std::string>(table.items[1]) == "y");
    CHECK(std::get<std::string>(*sandbox.call("result")) == "lighthousefleet7");

    const auto refused = sandbox.run("u", "game.refuse()");
    REQUIRE(!refused.ok());
    CHECK(refused.error().message.find("not now") != std::string::npos);
    const auto function_arg = sandbox.run("v", "game.echo(print)");
    REQUIRE(!function_arg.ok());
    CHECK(function_arg.error().message.find("argument 1") != std::string::npos);
}

TEST_CASE("script: a saved table comes back with its values, keys in a fixed order") {
    const std::string source =
        "state = {}\n"
        "for i, name in ipairs({'wood', 'tools', 'cloth', 'wine'}) do state[name] = i * 10 end\n"
        "state[true] = 'yes'\n"
        "state[3] = 0.25\n"
        "state.nested = {list = {1, 2, 3}, deep = {deeper = {'bottom'}}}\n"
        "state.skipped = print\n";
    script::Sandbox first;
    REQUIRE(first.run("a", source).ok());
    const auto bytes = first.save_table("state");
    REQUIRE(bytes.ok());
    REQUIRE(!bytes->empty());

    // The same table built in another order gives the same bytes.
    script::Sandbox second;
    REQUIRE(second
                .run("b",
                     "state = {nested = {deep = {deeper = {'bottom'}}, list = {1, 2, 3}}, [3] = 0.25}\n"
                     "for _, name in ipairs({'wine', 'cloth', 'tools', 'wood'}) do state[name] = 0 end\n"
                     "state.wood, state.tools, state.cloth, state.wine = 10, 20, 30, 40\n"
                     "state[true] = 'yes'")
                .ok());
    CHECK(*second.save_table("state") == *bytes);

    script::Sandbox loaded;
    REQUIRE(loaded.load_table("state", *bytes).ok());
    REQUIRE(loaded
                .run("c",
                     "function result() return state.wine + state.nested.list[3] .. state[true] .. state[3] .. "
                     "state.nested.deep.deeper[1] .. type(state.skipped) end")
                .ok());
    CHECK(std::get<std::string>(*loaded.call("result")) == "43yes0.25bottomnil");

    // Damaged bytes are refused and change nothing.
    std::vector<std::uint8_t> cut(bytes->begin(), bytes->end() - 3);
    CHECK(!loaded.load_table("state", cut).ok());
    std::vector<std::uint8_t> later = *bytes;
    later[0] = 99;
    CHECK(!loaded.load_table("state", later).ok());
    CHECK(std::get<std::string>(*loaded.call("result")) == "43yes0.25bottomnil");

    // No table, no bytes; no bytes, nothing changes.
    script::Sandbox empty;
    CHECK(empty.save_table("state")->empty());
    REQUIRE(empty.load_table("state", {}).ok());
    REQUIRE(empty.run("d", "function result() return type(state) end").ok());
    CHECK(std::get<std::string>(*empty.call("result")) == "nil");
}

TEST_CASE("script: scripts start at the first tick and hear the seconds") {
    Game game;
    auto scripts = game.run(one("quests",
                                "state = {seconds = 0}\n"
                                "function on_start() state.started = game.time() end\n"
                                "function on_second(s) state.seconds = s end\n"));
    game.ticks(41);  // 2.05 s
    REQUIRE(scripts->take_notes().empty());
    const auto states = scripts->save_states();
    REQUIRE(states.size() == 1);
    CHECK(states[0].package == "quests");

    script::Sandbox check;
    REQUIRE(check.load_table("state", states[0].data).ok());
    REQUIRE(check.run("c", "function result() return state.started * 100 + state.seconds end").ok());
    CHECK(std::get<std::int64_t>(*check.call("result")) == 2);
}

TEST_CASE("script: players, messages and errors") {
    Game game;
    auto scripts = game.run(one("lighthouse",
                                "function on_start()\n"
                                "  local p = game.player(0)\n"
                                "  game.message('lit', {name = p.name, money = p.money, role = game.player(1).role})\n"
                                "  game.message('other:hello', nil, 1, 0)\n"
                                "  print(pcall(game.message, 'x', nil, nil, 7))\n"
                                "  print(#game.players(), game.player(1).name, game.player(5), p.faction)\n"
                                "end\n"
                                "function on_second() error('no lamp oil') end\n"));
    game.ticks(1);
    const auto messages = scripts->take_messages();
    REQUIRE(messages.size() == 2);
    CHECK(messages[0].id == "lighthouse:lit");
    CHECK(messages[0].player == sim::kNoPlayer);
    REQUIRE(messages[0].args.size() == 3);
    CHECK(messages[0].args[0] == std::make_pair(std::string("money"), std::string("5000")));
    CHECK(messages[0].args[1] == std::make_pair(std::string("name"), std::string("Merlin")));
    CHECK(messages[0].args[2] == std::make_pair(std::string("role"), std::string("computer")));
    CHECK(messages[1].id == "other:hello");
    CHECK(messages[1].player == 1);
    CHECK(messages[0].about == sim::kNoPlayer);
    CHECK(messages[1].about == 0);  // its icon in player 0's colour
    const auto notes = scripts->take_notes();
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].text.find("no such player to be about") != std::string::npos);
    CHECK(!notes[1].error);
    CHECK(notes[1].text == "2\tEx\xc3\xa9ter\tnil\tanno1602:settlers");

    // A script that keeps failing is stopped after kMaxScriptErrors.
    game.ticks(20 * static_cast<int>(script::kMaxScriptErrors + 2));
    const auto errors = scripts->take_notes();
    CHECK(has_error(errors, "lighthouse:8: no lamp oil"));
    CHECK(has_error(errors, "stopped for the rest of the game"));
    CHECK(errors.size() == script::kMaxScriptErrors + 1);
}

TEST_CASE("script: a script whose top level fails stays off") {
    Game game;
    auto scripts = game.run(one("broken", "function on_start() state = 1 end\nerror('at load')"));
    CHECK(has_error(scripts->take_notes(), "broken:2: at load"));
    game.ticks(30);
    CHECK(scripts->take_notes().empty());
}

TEST_CASE("script: the state and random numbers go on after a save, the same as without") {
    const std::string source =
        "state = state or {rolls = {}}\n"
        "function on_second(s) state.rolls[#state.rolls + 1] = math.random(1000) end\n"
        "function result() return table.concat(state.rolls, ',') end\n";
    // One game runs 6 s at once.
    Game straight;
    auto straight_scripts = straight.run(one("dice", source));
    straight.ticks(120);

    // Another runs 3 s, is saved, and a new game loaded from it runs 3 s more.
    Game first;
    auto first_scripts = first.run(one("dice", source));
    first.ticks(60);
    const core::Bytes chunk = sim::encode_extension(first.world, first.buildings, &first.simulation, {});
    Game second;
    REQUIRE(sim::apply_extension(second.world, second.buildings, chunk, {}).ok());
    REQUIRE(second.world.script_states().size() == 1);
    auto second_scripts = second.run(one("dice", source));
    // The rest of the game does not continue from the chunk here (no Simulation took its state),
    // so the clocks start again; the scripts count their own seconds.
    second.ticks(60);

    const auto straight_state = straight_scripts->save_states();
    const auto continued_state = second_scripts->save_states();
    REQUIRE(straight_state.size() == 1);
    REQUIRE(continued_state.size() == 1);
    CHECK(straight_state[0].random == continued_state[0].random);
    CHECK(straight_state[0].data == continued_state[0].data);
}

TEST_CASE("script: the scripts' state is part of the game's fingerprint") {
    Game a;
    Game b;
    auto scripts_a = a.run(one("count", "state = {n = 0}\nfunction on_second() state.n = state.n + 1 end"));
    auto scripts_b = b.run(one("count", "state = {n = 0}\nfunction on_second() state.n = state.n + 2 end"));
    CHECK(a.simulation.state_hash() == b.simulation.state_hash());
    a.ticks(20);
    b.ticks(20);
    CHECK(a.simulation.state_hash() != b.simulation.state_hash());
}

TEST_CASE("script: buildings, houses and the end of players reach the scripts") {
    Game game;
    auto scripts = game.run(one("chronicle",
                                "function on_building_placed(e) print('built', e.building, e.island, e.x, e.y) end\n"
                                "function on_house_changed(e) print('house', e.kind, e.class) end\n"
                                "function on_player_won(p) print('won', p) end\n"
                                "function on_player_defeated(p, by) print('out', p, by) end\n"
                                "function on_game_over(p) print('over', p) end\n"));
    assets::BuildingType chapel;
    chapel.id = 20311;
    sim::ConstructionEvent placed;
    placed.type = &chapel;
    placed.island = 2;
    placed.x = 5;
    placed.y = 6;
    sim::ConstructionEvent removed = placed;
    removed.kind = sim::ConstructionEvent::Kind::Removed;
    const sim::ConstructionEvent construction[] = {placed, removed};
    sim::HouseEvent upgraded;
    upgraded.resident_class = 2;
    const sim::HouseEvent houses[] = {upgraded};
    sim::GameEvent won;
    won.kind = sim::GameEvent::Kind::MissionAccomplished;
    won.player = 0;
    sim::GameEvent out;
    out.kind = sim::GameEvent::Kind::PlayerDefeated;
    out.player = 1;
    out.by = sim::kNoPlayer;
    sim::GameEvent over;
    over.player = 0;
    const sim::GameEvent game_events[] = {won, out, over};
    scripts->after_tick(game.simulation,
                        sim::TickEvents{.construction = construction, .houses = houses, .game = game_events});

    std::vector<std::string> lines;
    for (const auto& note : scripts->take_notes()) {
        CHECK(!note.error);
        lines.push_back(note.text);
    }
    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "built\t20311\t2\t5\t6");
    CHECK(lines[1] == "house\tupgraded\t2");
    CHECK(lines[2] == "won\t0");
    CHECK(lines[3] == "out\t1\tnil");
    CHECK(lines[4] == "over\t0");
}

TEST_CASE("script: the state of the scripts goes through the savegame section") {
    Game game;
    sim::ScriptState state;
    state.package = "quests";
    state.random = sim::Random(7, 9);
    state.data = {1, 6, 0, 0, 0, 0};
    game.world.set_script_states({state});
    const core::Bytes chunk = sim::encode_extension(game.world, game.buildings, nullptr, {});
    sim::World loaded(64, 64);
    REQUIRE(sim::apply_extension(loaded, game.buildings, chunk, {}).ok());
    REQUIRE(loaded.script_states().size() == 1);
    CHECK(loaded.script_states()[0] == state);
}

TEST_CASE("script: scripts give and take money and goods") {
    Game game;
    sim::City city;
    city.island = 3;
    city.index = 1;
    city.owner = 0;
    city.name = "Port Royal";
    city.capacity = 50 * 32;
    game.world.add_city(city);
    auto scripts =
        game.run(one("trade",
                     "function on_start()\n"
                     "  print(game.add_money(0, 1500), game.add_money(1, -10))\n"
                     "  local c = game.cities(0)[1]\n"
                     "  print(#game.cities(), #game.cities(1), c.name, c.island, c.city, c.player)\n"
                     "  print(game.add_goods(3, 1, 'anno1602:tools', 30), game.add_goods(3, 1, 22, 30))\n"
                     "  print(game.goods(3, 1, 'anno1602:tools'), game.add_goods(3, 1, 'anno1602:tools', -70))\n"
                     "  print(game.goods(3, 1, 'anno1602:tools'), game.goods(4, 0, 'anno1602:tools'))\n"
                     "  game.add_goods(3, 1, 'pepper', 1)\n"
                     "end\n"
                     "function on_second() game.add_money(7, 1) end\n"));
    game.ticks(1);
    const auto notes = scripts->take_notes();
    REQUIRE(notes.size() == 6);
    CHECK(notes[0].text == "6500\t-10");
    CHECK(notes[1].text == "1\t0\tPort Royal\t3\t1\t0");
    CHECK(notes[2].text == "30\t20");
    CHECK(notes[3].text == "50\t-50");
    CHECK(notes[4].text == "0\tnil");
    CHECK(game.world.players()[0].money == 6500);
    CHECK(game.world.city(3, 1)->stock(22) == 0);
    CHECK(has_error(notes, "trade:8: game.add_goods: no such good"));
    game.ticks(19);
    CHECK(has_error(scripts->take_notes(), "game.add_money: no such player"));
}

TEST_CASE("script: scripts cannot change the game at their top level") {
    Game game;
    auto scripts = game.run(one("eager", "game.add_money(0, 100)"));
    CHECK(has_error(scripts->take_notes(), "eager:1: game.add_money: only while the game runs"));
    CHECK(game.world.players()[0].money == 5000);
}

TEST_CASE("script: scripts decide who wins and who is out") {
    Game game;
    auto scripts = game.run(one("duel",
                                "function on_second(s)\n"
                                "  print(game.defeat(1), game.defeat(1), game.win(1), game.win(0), game.win(0))\n"
                                "end\n"
                                "function on_player_won(p) print('won', p) end\n"
                                "function on_player_defeated(p) print('out', p) end\n"));
    game.ticks(20);
    const auto notes = scripts->take_notes();
    // What a script does reaches no script as an event: it knows what it did.
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].text == "true\tfalse\tfalse\ttrue\tfalse");
    CHECK(game.world.players()[1].status == sim::PlayerStatus::Defeated);
    CHECK(game.world.players()[0].won);
    REQUIRE(game.world.game_end().has_value());
    CHECK(game.world.game_end()->winner == 0);
    const auto events = game.simulation.take_game_events();
    REQUIRE(events.size() == 3);
    CHECK(events[0].kind == sim::GameEvent::Kind::PlayerDefeated);
    CHECK(events[0].reason == sim::GameEvent::Reason::Script);
    CHECK(events[0].by == sim::kNoPlayer);
    CHECK(events[1].kind == sim::GameEvent::Kind::MissionAccomplished);
    CHECK(events[2].kind == sim::GameEvent::Kind::GameOver);
    game.ticks(20);
    const auto later = scripts->take_notes();
    REQUIRE(later.size() == 1);
    CHECK(later[0].text == "false\tfalse\tfalse\tfalse\tfalse");
}

TEST_CASE("script: a scenario's script runs beside its package's, with a state of its own") {
    const std::filesystem::path folder = test::temp_path("o1602_scenario_script");
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "north.lua", std::ios::binary) << "\xEF\xBB\xBFstate = {who = 'scenario'}\n";
    assets::PackageScenario scenario;
    scenario.id = "isles:north";
    scenario.package = "isles";
    std::vector<std::string> problems;
    CHECK(!script::read_scenario_script(scenario, problems));  // no script
    scenario.script = folder / "north.lua";
    auto read = script::read_scenario_script(scenario, problems);
    CHECK(problems.empty());
    REQUIRE(read.has_value());
    CHECK(read->package == "isles");
    CHECK(read->name == "isles:north");
    CHECK(read->source == "state = {who = 'scenario'}\n");
    scenario.script = folder / "missing.lua";
    CHECK(!script::read_scenario_script(scenario, problems));
    CHECK(problems.size() == 1);
    std::filesystem::remove_all(folder);

    Game game;
    auto sources = one("isles", "state = {who = 'package'}\n");
    sources.push_back(std::move(*read));
    auto scripts = game.run(std::move(sources));
    game.ticks(2);
    const auto states = scripts->save_states();
    REQUIRE(states.size() == 2);
    CHECK(states[0].package == "isles");
    CHECK(states[1].package == "isles:north");
    CHECK(states[0].data != states[1].data);
    CHECK(states[0].random != states[1].random);
}

TEST_CASE("script: a campaign part's script takes the table the part before left and keeps it in the save") {
    const std::string source =
        "campaign = {gold = 0}\n"
        "function on_start() campaign.gold = campaign.gold + 10 * game.part end\n"
        "function result() return game.campaign .. ' ' .. campaign.gold end\n";
    // The table the first part left: gold 5, as the script's own top level writes it.
    std::vector<std::uint8_t> carried;
    {
        Game first;
        first.world.set_package_scenario("isles:north");
        first.world.set_campaign_part(sim::World::CampaignPart{"isles:frost", 0, {}});
        std::vector<script::PackageScript> sources;
        sources.push_back(script::PackageScript{"isles", "campaign = {gold = 5}\n", "isles:north"});
        auto scripts = first.run(std::move(sources));
        const auto states = scripts->save_states();
        REQUIRE(states.size() == 1);
        carried = states[0].campaign;
        CHECK(!carried.empty());
    }
    Game game;
    game.world.set_package_scenario("isles:south");
    game.world.set_campaign_part(sim::World::CampaignPart{"isles:frost", 1, carried});
    auto sources = one("isles", source);  // the package's own script keeps no campaign table
    sources.push_back(script::PackageScript{"isles", source, "isles:south"});
    auto scripts = game.run(std::move(sources));
    game.ticks(2);
    const auto states = scripts->save_states();
    REQUIRE(states.size() == 2);
    CHECK(states[0].campaign.empty());
    CHECK(!states[1].campaign.empty());

    // A loaded game goes on with the table as it was saved, not with the one carried.
    Game loaded;
    loaded.world.set_package_scenario("isles:south");
    loaded.world.set_campaign_part(sim::World::CampaignPart{"isles:frost", 1, carried});
    loaded.world.set_script_states({states[1]});
    std::vector<script::PackageScript> again;
    again.push_back(script::PackageScript{"isles", source, "isles:south"});
    auto loaded_scripts = loaded.run(std::move(again));
    const auto loaded_states = loaded_scripts->save_states();
    REQUIRE(loaded_states.size() == 1);
    CHECK(loaded_states[0].campaign == states[1].campaign);
    CHECK(loaded_states[0].campaign != carried);
}

TEST_CASE("script: cities, disasters, ships, fights and diplomacy reach the scripts") {
    Game game;
    sim::City city;
    city.island = 3;
    city.index = 1;
    city.owner = 0;
    city.inhabitants = {40, 2};
    game.world.add_city(city);
    auto scripts = game.run(one("chronicle",
                                "local function show(e)\n"
                                "  local keys = {}\n"
                                "  for k in pairs(e) do keys[#keys + 1] = k end\n"
                                "  table.sort(keys)\n"
                                "  local parts = {}\n"
                                "  for _, k in ipairs(keys) do parts[#parts + 1] = k .. '=' .. tostring(e[k]) end\n"
                                "  return table.concat(parts, ' ')\n"
                                "end\n"
                                "function on_city(e) print('city', show(e)) end\n"
                                "function on_disaster(e) print('disaster', show(e)) end\n"
                                "function on_volcano(e) print('volcano', show(e)) end\n"
                                "function on_drought(e) print('drought', show(e)) end\n"
                                "function on_ship(e) print('ship', show(e)) end\n"
                                "function on_military(e) print('military', show(e)) end\n"
                                "function on_diplomacy(e) print('diplomacy', show(e)) end\n"));
    sim::CityEvent shortage;
    shortage.kind = sim::CityEvent::Kind::Shortage;
    shortage.island = 3;
    shortage.city = 1;
    shortage.owner = 0;
    shortage.ware = 22;  // tools
    sim::CityEvent level = shortage;
    level.kind = sim::CityEvent::Kind::Unlocked;
    level.infra = 5;
    sim::CityEvent locked = shortage;
    locked.kind = sim::CityEvent::Kind::MaterialLocked;  // a message of the original only
    const sim::CityEvent cities[] = {shortage, level, locked};
    sim::DisasterEvent fire;
    fire.island = 3;
    fire.x = 10;
    fire.y = 12;
    fire.city = 1;
    fire.owner = 0;
    fire.spread = true;
    const sim::DisasterEvent disasters[] = {fire};
    sim::VolcanoEvent eruption;
    eruption.island = 4;
    eruption.x = 7;
    eruption.y = 8;
    const sim::VolcanoEvent volcanoes[] = {eruption};
    sim::IslandEvent drought;
    drought.island = 2;
    sim::IslandEvent withered = drought;
    withered.kind = sim::IslandEvent::Kind::Withered;
    const sim::IslandEvent islands[] = {drought, withered};
    sim::ShipEvent sunk;
    sunk.kind = sim::ShipEvent::Kind::Sunk;
    sunk.ship = 17;
    sunk.player = 1;
    sunk.city = 0;  // the attacker
    sim::ShipEvent traded;
    traded.kind = sim::ShipEvent::Kind::Traded;
    traded.ship = 18;
    traded.player = 0;
    traded.island = 3;
    traded.city = 1;
    traded.ware = 22;
    traded.amount = 5;
    traded.money = 120;
    sim::ShipEvent arrived = traded;
    arrived.kind = sim::ShipEvent::Kind::Arrived;  // too common to be worth a call
    const sim::ShipEvent ships[] = {sunk, traded, arrived};
    sim::MilitaryEvent destroyed;
    destroyed.kind = sim::MilitaryEvent::Kind::BuildingDestroyed;
    destroyed.target = sim::Target::building(3, 10, 12);
    destroyed.player = 1;
    destroyed.owner = 0;
    sim::MilitaryEvent shot = destroyed;
    shot.kind = sim::MilitaryEvent::Kind::Shot;
    const sim::MilitaryEvent military[] = {destroyed, shot};
    sim::DiplomacyNotice offer;
    offer.kind = sim::DiplomacyNotice::Kind::PeaceOffered;
    offer.from = 1;
    offer.to = 0;
    const sim::DiplomacyNotice diplomacy[] = {offer};
    sim::TickEvents events;
    events.cities = cities;
    events.disasters = disasters;
    events.volcanoes = volcanoes;
    events.islands = islands;
    events.ships = ships;
    events.military = military;
    events.diplomacy = diplomacy;
    scripts->after_tick(game.simulation, events);

    std::vector<std::string> lines;
    for (const auto& note : scripts->take_notes()) {
        CHECK(!note.error);
        lines.push_back(note.text);
    }
    REQUIRE(lines.size() == 9);
    CHECK(lines[0] == "city\tcity=1 good=anno1602:tools inhabitants=42 island=3 kind=shortage player=0");
    CHECK(lines[1] == "city\tcity=1 inhabitants=42 island=3 kind=level level=5 player=0");
    CHECK(lines[2] == "disaster\tcity=1 disaster=fire island=3 kind=started player=0 spread=true x=10 y=12");
    CHECK(lines[3] == "volcano\tisland=4 kind=erupted x=7 y=8");
    CHECK(lines[4] == "drought\tisland=2 kind=started");
    CHECK(lines[5] == "ship\tkind=sunk other=0 player=1 ship=17");
    CHECK(lines[6] == "ship\tamount=5 city=1 good=anno1602:tools island=3 kind=traded money=120 player=0 ship=18");
    CHECK(lines[7] == "military\tisland=3 kind=building_destroyed owner=0 player=1 x=10 y=12");
    CHECK(lines[8] == "diplomacy\tfrom=1 kind=peace_offered to=0");
}

TEST_CASE("script: scripts read cities, ships and treaties and make peace and war") {
    Game game;
    sim::City city;
    city.island = 3;
    city.index = 1;
    city.owner = 0;
    city.name = "Port Royal";
    city.inhabitants = {40, 2};
    game.world.add_city(city);
    sim::Ship ship;
    ship.id = 5;
    ship.name = "Seeadler";
    ship.owner = 1;
    ship.figure = 21;
    ship.x = 30;
    ship.y = 31;
    game.world.add_ship(ship);
    game.world.diplomacy().resize(2);
    auto scripts =
        game.run(one("treaties",
                     "function on_start()\n"
                     "  local c = game.city(3, 1)\n"
                     "  print(c.name, c.player, c.inhabitants, #c.classes, c.classes[2], game.city(3, 2))\n"
                     "  local s = game.ships(1)[1]\n"
                     "  print(#game.ships(), #game.ships(0), s.id, s.name, s.player, s.x, s.y)\n"
                     "  local r = game.relation(0, 1)\n"
                     "  print(r.peace, r.trade, r.hostile, game.relation(0, 0), game.relation(0, 9))\n"
                     "  print(game.set_peace(0, 1, false), game.set_peace(0, 1, false), game.set_trade(1, 0, true))\n"
                     "  r = game.relation(1, 0)\n"
                     "  print(r.peace, r.trade, r.hostile, game.set_peace(0, 0, true))\n"
                     "  game.set_peace(0, 1)\n"
                     "end\n"));
    game.ticks(1);
    const auto notes = scripts->take_notes();
    REQUIRE(notes.size() == 6);
    CHECK(notes[0].text == "Port Royal\t0\t42\t2\t2\tnil");
    CHECK(notes[1].text == "1\t0\t5\tSeeadler\t1\t30\t31");
    CHECK(notes[2].text == "signed\tnone\tfalse\tnil\tnil");
    CHECK(notes[3].text == "true\tfalse\ttrue");
    CHECK(notes[4].text == "none\tsigned\ttrue\tfalse");
    CHECK(has_error(notes, "treaties:11: game.set_peace: two players' numbers and true or false are needed"));
    CHECK(game.world.diplomacy().hostile(0, 1));
    CHECK(game.world.diplomacy().may_trade(0, 1));
}

TEST_CASE("script: a treaty offered in the game reaches the scripts in the same tick") {
    Game game;
    game.world.diplomacy().resize(2);
    (void)game.world.diplomacy().set_peace(game.world, 0, 1, sim::Treaty::None, 0);
    (void)game.world.diplomacy().take_notices();
    auto scripts = game.run(one("watch", "function on_diplomacy(e) print(e.kind, e.from, e.to) end\n"));
    sim::DiplomacyOrder offer;
    offer.player = 0;
    offer.partner = 1;
    offer.value = sim::Treaty::Offered;
    game.simulation.schedule(game.simulation.next_tick(), offer);
    game.ticks(1);
    const auto notes = scripts->take_notes();
    REQUIRE(!notes.empty());
    CHECK(notes[0].text.starts_with("peace_offered\t0\t1"));
}

TEST_CASE("script: scripts read and change the players' missions") {
    Game game;
    sim::Mission map_mission;
    map_mission.goals = sim::goal::kCity1 | sim::goal::kCity2;
    map_mission.text =
        "Zwei St\xe4"
        "dte";
    game.world.set_missions({map_mission});
    auto players = game.world.players();
    players[0].mission = 0;
    players[1].mission = 0;
    game.world.set_players(players);
    auto scripts =
        game.run(one("goals",
                     "function on_start()\n"
                     "  local m = game.mission(0)\n"
                     "  print(m.text, m.other, m.inhabitants, game.mission(9), game.mission(1).other)\n"
                     "  print(game.set_mission(0, {inhabitants = 500, castle = true, balance = -20,\n"
                     "                             goods = {['anno1602:tools'] = 20},\n"
                     "                             city = {inhabitants = 300, class = 2, count = 50},\n"
                     "                             text = 'Gr\xc3\xbc\xc3\x9f"
                     "e'}))\n"
                     "  m = game.mission(0)\n"
                     "  print(m.text, m.inhabitants, m.castle, m.cathedral, m.balance, m.goods['anno1602:tools'],\n"
                     "        m.city.inhabitants, m.city.class, m.city.count, m.other)\n"
                     "  print(game.mission(1).other, game.set_mission(1, {cities = 2}), game.mission(1).cities)\n"
                     "  print(game.set_mission(1, {}), game.mission(1), game.set_mission(9, {cities = 1}))\n"
                     "end\n"
                     "function on_second(s)\n"
                     "  if s == 1 then game.set_mission(0, {gold = 1}) end\n"
                     "  if s == 2 then game.set_mission(0, {goods = {pepper = 1}}) end\n"
                     "  if s == 3 then game.set_mission(0, {cities = -1}) end\n"
                     "end\n"));
    game.ticks(1);
    const auto notes = scripts->take_notes();
    REQUIRE(notes.size() == 5);
    CHECK(notes[0].text ==
          "Zwei St\xc3\xa4"
          "dte\ttrue\tnil\tnil\ttrue");
    CHECK(notes[1].text == "true");
    CHECK(notes[2].text ==
          "Gr\xc3\xbc\xc3\x9f"
          "e\t500\ttrue\tnil\t-20\t20\t300\t2\t50\tnil");
    // The computer kept the map's mission until the script gave it one of its own.
    CHECK(notes[3].text == "true\ttrue\t2");
    CHECK(notes[4].text == "true\tnil\tfalse");
    // Player 0 took a new slot, as player 1 still named the map's; no slot is left over.
    REQUIRE(game.world.missions().size() == 2);
    CHECK(game.world.players()[0].mission == 1);
    CHECK(game.world.players()[1].mission == sim::kNoMission);
    const sim::Mission& own = game.world.missions()[1];
    CHECK(own.goals == (sim::goal::kInhabitants | sim::goal::kCastle | sim::goal::kBalance | sim::goal::kGoods1 |
                        sim::goal::kCity1));
    CHECK(own.text ==
          "Gr\xfc\xdf"
          "e");
    CHECK(own.goods[0].amount == 20 * 32);
    CHECK(game.world.missions()[0].goals == (sim::goal::kCityCount | sim::goal::kComputers));
    game.ticks(60);
    const auto later = scripts->take_notes();
    CHECK(has_error(later, "game.set_mission: there is no goal gold"));
    CHECK(has_error(later, "game.set_mission: the goal names goals:pepper, a good this game does not know"));
    CHECK(has_error(later, "game.set_mission: cities must be a whole number from 0 to"));
    CHECK(game.world.players()[0].mission == 1);
}

TEST_CASE("script: game.set_mission changes no mission at the top level") {
    Game game;
    auto scripts = game.run(one("eager", "game.set_mission(0, {cities = 1})"));
    CHECK(has_error(scripts->take_notes(), "eager:1: game.set_mission: only while the game runs"));
    CHECK(game.world.missions().empty());
}

TEST_CASE("script: scripts list land units and send them as their owners would") {
    Game game;
    game.world.diplomacy().resize(2);
    sim::Soldier own = sim::make_soldier(10, 12, 1, 0, 0);
    own.health = 20 * 32;
    game.world.add_soldier(own);
    game.world.add_soldier(sim::make_soldier(14, 12, 9, 1));
    const std::uint32_t mine = game.world.soldiers()[0].id;
    const std::uint32_t theirs = game.world.soldiers()[1].id;
    auto scripts = game.run(
        one("army",
            "function on_start()\n"
            "  local u = game.units(0)[1]\n"
            "  print(#game.units(), #game.units(1), u.player, u.figure, u.x, u.y, u.island, u.health)\n"
            "  local other = game.units(1)[1]\n"
            "  print(other.island, game.move_unit(999, 5, 5))\n"
            "  print(game.move_unit(u.id, 20, 21, true), game.move_unit(u.id, 64, 1))\n"
            "  print(game.attack_unit(other.id, u.id), game.attack_unit(u.id, u.id), game.attack_unit(u.id, 999))\n"
            "  game.move_unit(u.id, 1)\n"
            "end\n"));
    game.ticks(1);
    const auto notes = scripts->take_notes();
    REQUIRE(notes.size() == 5);
    CHECK(notes[0].text == "2\t1\t0\t1\t10\t12\t0\t20");
    CHECK(notes[1].text == "nil\tfalse");
    CHECK(notes[2].text == "true\tfalse");
    CHECK(notes[3].text == "true\tfalse\tfalse");
    CHECK(has_error(notes, "army:8: game.move_unit: the unit's id, a world tile x, y"));
    const sim::Soldier* walker = game.world.soldier(mine);
    REQUIRE(walker != nullptr);
    CHECK(walker->target == sim::Target::tile_spot(20, 21));
    CHECK(walker->attack_move);
    // The attack is the owner's: player 1 is at war with player 0 now.
    const sim::Soldier* attacker = game.world.soldier(theirs);
    REQUIRE(attacker != nullptr);
    CHECK(attacker->target == sim::Target::soldier(mine));
    CHECK(game.world.diplomacy().hostile(0, 1));
}
