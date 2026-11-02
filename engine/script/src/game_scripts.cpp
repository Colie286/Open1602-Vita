// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/script/game_scripts.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <optional>
#include <span>

#include "o1602/assets/original_names.hpp"
#include "o1602/assets/own_figures.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/hash.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::script {

struct GameScripts::Package {
    std::string id;    ///< the package's
    std::string name;  ///< what its state is saved under and its notes name (PackageScript::name)
    Sandbox sandbox;
    sim::Random random;
    bool started = false;   ///< on_start ran, or the savegame kept a state
    bool stopped = false;   ///< failed at its top level or too often
    bool campaign = false;  ///< the script of a campaign's part: keeps the table `campaign`
    std::size_t errors = 0;

    Package(std::string package, std::string script_name, Limits limits)
        : id(std::move(package)), name(std::move(script_name)), sandbox(limits) {}
};

namespace {

std::span<const std::uint8_t> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

/// The generator of a package's math.random in a new game: from the world's generators, which are
/// the same on every computer, and the package's id, without drawing from them.
sim::Random new_random(const sim::World& world, std::string_view package) {
    const std::uint64_t id = core::xxh64(bytes_of(package));
    return sim::Random(world.random(sim::RandomStream::Events).state() ^ id, id);
}

/// An integer argument; nothing for another value.
std::optional<std::int64_t> integer(std::span<const Value> args, std::size_t i) {
    if (i < args.size()) {
        if (const auto* value = std::get_if<std::int64_t>(&args[i])) {
            return *value;
        }
    }
    return std::nullopt;
}

/// The ware number of a trade good: a number, "anno1602:<name or number>", "<package>:<name>", or a
/// name of the package's own goods.toml.
std::optional<std::uint8_t> ware_argument(const sim::World& world, std::string_view package,
                                          std::span<const Value> args, std::size_t i) {
    std::optional<std::uint8_t> ware;
    if (const auto number = integer(args, i)) {
        if (*number >= 0 && *number <= 255) {
            ware = static_cast<std::uint8_t>(*number);
        }
    } else if (i < args.size()) {
        if (const auto* id = std::get_if<std::string>(&args[i])) {
            ware = assets::ware_of(world.goods(),
                                   id->find(':') == std::string::npos ? std::string(package) + ":" + *id : *id);
        }
    }
    return ware && world.goods().is_trade(*ware) ? ware : std::nullopt;
}

/// The haeuser.cod Id of a building: a number, "anno1602:<name or number>", "<package>:<name>" of a
/// package's own_buildings.toml, or a name of the package's own.
std::optional<std::uint32_t> building_argument(const sim::World& world, std::string_view package,
                                               std::span<const Value> args, std::size_t i) {
    if (const auto number = integer(args, i)) {
        return *number >= 0 && *number <= 0xFFFFFF ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(*number))
                                                   : std::nullopt;
    }
    const auto* id = i < args.size() ? std::get_if<std::string>(&args[i]) : nullptr;
    if (id == nullptr) {
        return std::nullopt;
    }
    const std::string full = id->find(':') == std::string::npos ? std::string(package) + ":" + *id : *id;
    if (const assets::OwnBuilding* own = assets::find_own_building(world.own_buildings(), full)) {
        return own->number;
    }
    return assets::original_building_id(full);
}

/// A player's number among the world's players; nothing for another value.
std::optional<sim::PlayerId> player_argument(const sim::World& world, std::span<const Value> args, std::size_t i) {
    const auto n = integer(args, i);
    if (!n || *n < 0 || *n >= static_cast<std::int64_t>(world.players().size())) {
        return std::nullopt;
    }
    return static_cast<sim::PlayerId>(*n);
}

/// The city `index` of island `island`; nullptr for none.
const sim::City* find_city(const sim::World& world, std::int64_t island, std::int64_t index) {
    if (island < 0 || island > 0xFFFF || index < 0 || index > 0xFF) {
        return nullptr;
    }
    return world.city(static_cast<std::uint16_t>(island), static_cast<std::uint8_t>(index));
}

/// The whole tons in `amount` (1/32 t), rounded down.
std::int64_t whole_tons(std::uint32_t amount) {
    return std::int64_t{amount / 32};
}

std::string text_of(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* number = std::get_if<std::int64_t>(&value)) {
        return std::to_string(*number);
    }
    if (const auto* number = std::get_if<double>(&value)) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.14g", *number);  // the C locale: a dot, as in Lua
        return buffer;
    }
    if (const auto* flag = std::get_if<bool>(&value)) {
        return *flag ? "true" : "false";
    }
    return {};
}

std::uint64_t inhabitants_of(const sim::World& world, sim::PlayerId player) {
    std::uint64_t sum = 0;
    for (const sim::City& city : world.cities()) {
        if (city.owner == player) {
            sum = std::accumulate(city.inhabitants.begin(), city.inhabitants.end(), sum);
        }
    }
    return sum;
}

Value player_table(const sim::World& world, sim::PlayerId id) {
    const sim::Player* player = world.player(id);
    if (player == nullptr) {
        return {};
    }
    const char* role = player->role == sim::PlayerRole::Human      ? "human"
                       : player->role == sim::PlayerRole::Computer ? "computer"
                                                                   : "neutral";
    return make_table({
        {"id", std::int64_t{id}},
        {"name", core::cp1252_to_utf8(player->name)},
        {"role", std::string(role)},
        {"faction", world.faction(*player).id},
        {"money", std::int64_t{player->money}},
        {"inhabitants", static_cast<std::int64_t>(inhabitants_of(world, id))},
        {"playing", player->active()},
        {"won", player->won},
    });
}

Value city_table(const sim::City& city) {
    return make_table({
        {"island", std::int64_t{city.island}},
        {"city", std::int64_t{city.index}},
        {"name", core::cp1252_to_utf8(city.name)},
        {"player", city.owner != sim::kNoPlayer ? Value{std::int64_t{city.owner}} : Value{}},
    });
}

/// The id of a trade good as scripts name it: "anno1602:<name>" for the original's, "<package>:<name>"
/// for a package's.
std::string good_id(const sim::World& world, std::uint8_t ware) {
    if (const assets::OwnGood* own = world.goods().own(ware)) {
        return own->id;
    }
    for (const assets::OriginalName& name : assets::original_ware_names()) {
        if (ware >= name.first && ware < name.first + name.count) {
            return "anno1602:" + std::string(name.name);
        }
    }
    return "anno1602:" + std::to_string(ware);
}

Value player_or_nil(sim::PlayerId p) {
    return p == sim::kNoPlayer ? Value{} : Value{std::int64_t{p}};
}

const char* treaty_name(sim::Treaty treaty) {
    switch (treaty) {
        case sim::Treaty::None:
            return "none";
        case sim::Treaty::Offered:
            return "offered";
        case sim::Treaty::Received:
            return "received";
        case sim::Treaty::Signed:
            return "signed";
    }
    return "none";
}

Value ship_table(const sim::Ship& ship) {
    return make_table({
        {"id", std::int64_t{ship.id}},
        {"name", core::cp1252_to_utf8(ship.name)},
        {"player", player_or_nil(ship.owner)},
        {"figure", std::int64_t{ship.figure}},
        {"x", std::int64_t{ship.x}},
        {"y", std::int64_t{ship.y}},
        {"cannons", std::int64_t{ship.cannons}},
        {"trader", ship.trader},
    });
}

/// A land unit as game.units() gives it.
Value unit_table(const sim::Soldier& unit) {
    return make_table({
        {"id", std::int64_t{unit.id}},
        {"player", player_or_nil(unit.owner)},
        {"figure", std::int64_t{unit.figure}},
        {"x", std::int64_t{unit.cell_x() / 2}},
        {"y", std::int64_t{unit.cell_y() / 2}},
        {"island", unit.island != 0xFFFF ? Value{std::int64_t{unit.island}} : Value{}},
        {"health", std::int64_t{unit.health / 32}},
    });
}

/// A city as game.city() gives it: the fields of city_table and its inhabitants, in all and per class.
Value city_details(const sim::City& city) {
    std::vector<Value> classes;
    std::int64_t sum = 0;
    for (const std::uint32_t n : city.inhabitants) {
        classes.emplace_back(std::int64_t{n});
        sum += n;
    }
    return make_table({
        {"island", std::int64_t{city.island}},
        {"city", std::int64_t{city.index}},
        {"name", core::cp1252_to_utf8(city.name)},
        {"player", player_or_nil(city.owner)},
        {"inhabitants", sum},
        {"classes", make_table({}, std::move(classes))},
    });
}

core::Error script_error(std::string message) {
    return core::make_error(core::ErrorCode::ScriptError, std::move(message));
}

/// The goal bits game.mission and game.set_mission name (the keys of a scenario's goals).
constexpr std::uint32_t kScriptGoals = sim::goal::kInhabitants | sim::goal::kCityCount | sim::goal::kBalance |
                                       sim::goal::kTradeBalance | sim::goal::kDefeatedCount | sim::goal::kCastle |
                                       sim::goal::kCathedral | sim::goal::kGoods1 | sim::goal::kGoods2 |
                                       sim::goal::kCity1 | sim::goal::kComputers;

/// A mission as game.mission gives it: its text and the goals by the keys of a scenario's goals;
/// `other` if it has goals or loss conditions these keys do not name (missions of the original's maps).
Value mission_table(const sim::World& world, const sim::Mission& m) {
    namespace goal = sim::goal;
    std::vector<std::pair<std::string, Value>> fields{{"text", core::cp1252_to_utf8(m.text)}};
    const auto number = [&](std::uint32_t bit, const char* name, std::int64_t value) {
        if ((m.goals & bit) != 0) {
            fields.emplace_back(name, value);
        }
    };
    number(goal::kInhabitants, "inhabitants", m.min_inhabitants);
    number(goal::kCityCount, "cities", m.min_cities);
    number(goal::kBalance, "balance", m.min_balance);
    number(goal::kTradeBalance, "trade_balance", m.min_trade_balance);
    number(goal::kDefeatedCount, "defeated", m.min_defeated);
    if ((m.goals & goal::kCastle) != 0) {
        fields.emplace_back("castle", true);
    }
    if ((m.goals & goal::kCathedral) != 0) {
        fields.emplace_back("cathedral", true);
    }
    std::vector<std::pair<std::string, Value>> goods;
    for (std::size_t i = 0; i < m.goods.size(); ++i) {
        if ((m.goals & (i == 0 ? goal::kGoods1 : goal::kGoods2)) != 0) {
            goods.emplace_back(good_id(world, m.goods[i].ware), whole_tons(m.goods[i].amount));
        }
    }
    if (!goods.empty()) {
        fields.emplace_back("goods", make_table(std::move(goods)));
    }
    if ((m.goals & goal::kCity1) != 0) {
        const sim::Mission::CityGoal& city = m.cities[0];
        fields.emplace_back("city", make_table({
                                        {"inhabitants", std::int64_t{city.inhabitants}},
                                        {"class", std::int64_t{city.resident_class}},
                                        {"count", std::int64_t{city.count}},
                                    }));
    }
    if ((m.goals & ~kScriptGoals) != 0 || m.losses != 0) {
        fields.emplace_back("other", true);
    }
    return make_table(std::move(fields));
}

/// An integer field of a table within [low, high]; an error naming `what` otherwise.
core::Result<std::int64_t> bounded(const Value& value, std::string_view what, std::int64_t low, std::int64_t high) {
    const auto* number = std::get_if<std::int64_t>(&value);
    if (number == nullptr || *number < low || *number > high) {
        return script_error("game.set_mission: " + std::string(what) + " must be a whole number from " +
                            std::to_string(low) + " to " + std::to_string(high));
    }
    return *number;
}

/// The fields of a table argument; nullptr for another value.
const Table* table_of(const Value& value) {
    const auto* table = std::get_if<std::shared_ptr<const Table>>(&value);
    return table != nullptr ? table->get() : nullptr;
}

/// The goals of game.set_mission, by the keys of a scenario's goals, and its `text` (UTF-8).
core::Result<assets::ScenarioGoals> goals_argument(const Table& table, std::string_view package, std::string& text) {
    constexpr std::int64_t kMaxInt = std::numeric_limits<std::int32_t>::max();
    constexpr std::int64_t kMaxTons = std::numeric_limits<std::uint32_t>::max() / 32;
    assets::ScenarioGoals goals;
    for (const auto& [name, value] : table.fields) {
        if (name == "text") {
            const auto* string = std::get_if<std::string>(&value);
            if (string == nullptr) {
                return script_error("game.set_mission: text must be a string");
            }
            text = *string;
        } else if (name == "inhabitants" || name == "cities" || name == "balance" || name == "trade_balance") {
            const bool money = name == "balance" || name == "trade_balance";
            auto n = bounded(value, name, money ? -kMaxInt : 0, kMaxInt);
            if (!n) {
                return n.error();
            }
            auto& field = name == "inhabitants" ? goals.inhabitants
                          : name == "cities"    ? goals.cities
                          : name == "balance"   ? goals.balance
                                                : goals.trade_balance;
            field = static_cast<std::int32_t>(*n);
        } else if (name == "defeated") {
            auto n = bounded(value, name, 0, std::numeric_limits<std::uint16_t>::max());
            if (!n) {
                return n.error();
            }
            goals.defeated = static_cast<std::uint16_t>(*n);
        } else if (name == "castle" || name == "cathedral") {
            const auto* flag = std::get_if<bool>(&value);
            if (flag == nullptr) {
                return script_error("game.set_mission: " + name + " must be true or false");
            }
            (name == "castle" ? goals.castle : goals.cathedral) = *flag;
        } else if (name == "goods") {
            const Table* goods = table_of(value);
            if (goods == nullptr || !goods->items.empty()) {
                return script_error("game.set_mission: goods must be a table of goods' names and tons");
            }
            for (const auto& [good, tons] : goods->fields) {
                auto n = bounded(tons, "the tons of " + good, 0, kMaxTons);
                if (!n) {
                    return n.error();
                }
                goals.goods.emplace_back(good.find(':') == std::string::npos ? std::string(package) + ":" + good : good,
                                         static_cast<std::uint32_t>(*n));
            }
        } else if (name == "city") {
            const Table* city = table_of(value);
            if (city == nullptr) {
                return script_error("game.set_mission: city must be a table");
            }
            assets::ScenarioCityGoal goal;
            for (const auto& [key, field] : city->fields) {
                if (key == "inhabitants") {
                    auto n = bounded(field, "city.inhabitants", 0, kMaxInt);
                    if (!n) {
                        return n.error();
                    }
                    goal.inhabitants = static_cast<std::int32_t>(*n);
                } else if (key == "class") {
                    auto n = bounded(field, "city.class", 0, std::numeric_limits<std::uint8_t>::max());
                    if (!n) {
                        return n.error();
                    }
                    goal.resident_class = static_cast<std::uint8_t>(*n);
                } else if (key == "count") {
                    auto n = bounded(field, "city.count", 0, kMaxInt);
                    if (!n) {
                        return n.error();
                    }
                    goal.count = static_cast<std::uint32_t>(*n);
                } else {
                    return script_error("game.set_mission: a city goal has no " + key);
                }
            }
            goals.city = goal;
        } else {
            return script_error("game.set_mission: there is no goal " + name);
        }
    }
    if (goals.goods.size() > 2) {
        return script_error("game.set_mission: at most two goods");
    }
    return goals;
}

}  // namespace

std::vector<PackageScript> read_package_scripts(std::span<const assets::ModPackage> packages,
                                                std::vector<std::string>& problems) {
    std::vector<PackageScript> scripts;
    for (const assets::ModPackage& p : packages) {
        const std::filesystem::path path = p.folder / std::filesystem::path(kScriptFile);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        auto bytes = core::read_file(path);
        if (!bytes) {
            problems.push_back(p.id + "/" + std::string(kScriptFile) + ": " + bytes.error().message);
            continue;
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        scripts.push_back(PackageScript{p.id, std::string(core::skip_utf8_bom(text)), {}});
    }
    return scripts;
}

std::optional<PackageScript> read_scenario_script(const assets::PackageScenario& scenario,
                                                  std::vector<std::string>& problems) {
    if (scenario.script.empty()) {
        return std::nullopt;
    }
    auto bytes = core::read_file(scenario.script);
    if (!bytes) {
        problems.push_back(scenario.id + ": its script: " + bytes.error().message);
        return std::nullopt;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    return PackageScript{scenario.package, std::string(core::skip_utf8_bom(text)), scenario.id};
}

GameScripts::GameScripts(std::vector<PackageScript> scripts, const sim::World& world, Limits limits) : m_world(&world) {
    for (PackageScript& script : scripts) {
        std::string name = script.name.empty() ? script.package : std::move(script.name);
        auto package = std::make_unique<Package>(std::move(script.package), std::move(name), limits);
        Package& p = *package;
        m_packages.push_back(std::move(package));
        const auto& part = world.campaign_part();
        p.campaign = part && p.name == world.package_scenario();
        add_api(p);
        if (p.campaign) {
            (void)p.sandbox.add_value("game", "campaign", Value{part->campaign});
            (void)p.sandbox.add_value("game", "part", Value{static_cast<std::int64_t>(part->part) + 1});
        }

        const auto saved = std::find_if(world.script_states().begin(), world.script_states().end(),
                                        [&](const sim::ScriptState& s) { return s.package == p.name; });
        p.random = saved != world.script_states().end() ? saved->random : new_random(world, p.name);
        p.started = saved != world.script_states().end();

        if (const core::Status ran = p.sandbox.run(p.name, script.source); !ran) {
            fail(p, ran.error().message);
            p.stopped = true;
            continue;
        }
        if (saved != world.script_states().end()) {
            if (const core::Status loaded = p.sandbox.load_table(kStateTable, saved->data); !loaded) {
                fail(p, loaded.error().message);
            }
        }
        if (p.campaign) {
            // What the part before left, or what this part kept when it was saved.
            const auto& carried = saved != world.script_states().end() ? saved->campaign : part->carried;
            if (const core::Status loaded = p.sandbox.load_table(kCampaignTable, carried); !loaded) {
                fail(p, loaded.error().message);
            }
        }
    }
}

GameScripts::~GameScripts() = default;

void GameScripts::add_api(Package& package) {
    Sandbox& sandbox = package.sandbox;
    sandbox.set_random([&package] { return package.random.next(); });
    sandbox.set_print([this, &package](std::string_view line) {
        if (m_notes.size() < kMaxPendingMessages) {
            m_notes.push_back(ScriptNote{package.name, std::string(line), false});
        }
    });
    const auto add = [&sandbox](std::string_view name, Sandbox::HostFunction function) {
        (void)sandbox.add_function("game", name, std::move(function));
    };
    (void)sandbox.add_value("game", "package", Value{package.id});

    add("time", [this](std::span<const Value>) -> core::Result<Value> {
        return Value{static_cast<std::int64_t>(m_world->time_ms() / 1000)};
    });
    add("players", [this](std::span<const Value>) -> core::Result<Value> {
        std::vector<Value> ids;
        for (std::size_t p = 0; p < m_world->players().size(); ++p) {
            ids.emplace_back(static_cast<std::int64_t>(p));
        }
        return make_table({}, std::move(ids));
    });
    add("player", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto id = integer(args, 0);
        if (!id) {
            return core::make_error(core::ErrorCode::ScriptError, "game.player: the player's number is missing");
        }
        if (*id < 0 || *id >= static_cast<std::int64_t>(m_world->players().size())) {
            return Value{};
        }
        return player_table(*m_world, static_cast<sim::PlayerId>(*id));
    });
    add("cities", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        if (!args.empty() && !std::holds_alternative<std::monostate>(args[0]) && !player) {
            return script_error("game.cities: the player must be a number");
        }
        std::vector<Value> cities;
        for (const sim::City& city : m_world->cities()) {
            if (!player || city.owner == *player) {
                cities.push_back(city_table(city));
            }
        }
        return make_table({}, std::move(cities));
    });
    add("goods", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        const auto island = integer(args, 0);
        const auto index = integer(args, 1);
        const auto ware = ware_argument(*m_world, package.id, args, 2);
        if (!island || !index) {
            return script_error("game.goods: the island and the city must be numbers");
        }
        if (!ware) {
            return script_error("game.goods: no such good");
        }
        const sim::City* city = find_city(*m_world, *island, *index);
        return city != nullptr ? Value{whole_tons(city->stock(*ware))} : Value{};
    });
    add("city", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto island = integer(args, 0);
        const auto index = integer(args, 1);
        if (!island || !index) {
            return script_error("game.city: the island and the city must be numbers");
        }
        const sim::City* city = find_city(*m_world, *island, *index);
        return city != nullptr ? city_details(*city) : Value{};
    });
    add("ships", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        if (!args.empty() && !std::holds_alternative<std::monostate>(args[0]) && !player) {
            return script_error("game.ships: the player must be a number");
        }
        std::vector<Value> ships;
        for (const sim::Ship& ship : m_world->ships()) {
            if (!ship.sinking && (!player || ship.owner == *player)) {
                ships.push_back(ship_table(ship));
            }
        }
        return make_table({}, std::move(ships));
    });
    add("relation", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto a = integer(args, 0);
        const auto b = integer(args, 1);
        const auto players = static_cast<std::int64_t>(m_world->players().size());
        if (!a || !b) {
            return script_error("game.relation: the two players must be numbers");
        }
        if (*a < 0 || *a >= players || *b < 0 || *b >= players || *a == *b) {
            return Value{};
        }
        const sim::Diplomacy& diplomacy = m_world->diplomacy();
        const auto pa = static_cast<sim::PlayerId>(*a);
        const auto pb = static_cast<sim::PlayerId>(*b);
        const sim::Relation& relation = diplomacy.relation(pa, pb);
        return make_table({
            {"peace", std::string(treaty_name(relation.peace))},
            {"trade", std::string(treaty_name(relation.trade))},
            {"hostile", diplomacy.hostile(pa, pb)},
        });
    });
    const auto treaty = [this](std::string_view name, bool peace) {
        return [this, name, peace](std::span<const Value> args) -> core::Result<Value> {
            const auto a = integer(args, 0);
            const auto b = integer(args, 1);
            const auto* on = args.size() > 2 ? std::get_if<bool>(&args[2]) : nullptr;
            if (m_simulation == nullptr) {
                return script_error("game." + std::string(name) + ": only while the game runs, not at the top level");
            }
            if (!a || !b || on == nullptr) {
                return script_error("game." + std::string(name) +
                                    ": two players' numbers and true or false are needed");
            }
            const auto players = static_cast<std::int64_t>(m_world->players().size());
            return Value{*a >= 0 && *a < players && *b >= 0 && *b < players &&
                         m_simulation->script_treaty(static_cast<sim::PlayerId>(*a), static_cast<sim::PlayerId>(*b),
                                                     peace, *on)};
        };
    };
    add("set_peace", treaty("set_peace", true));
    add("set_trade", treaty("set_trade", false));
    add("build", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr) {
            return script_error("game.build: only while the game runs, not at the top level");
        }
        const auto player = player_argument(*m_world, args, 0);
        const auto island = integer(args, 1);
        const auto building = building_argument(*m_world, package.id, args, 2);
        const auto x = integer(args, 3);
        const auto y = integer(args, 4);
        const auto rotation = args.size() > 5 && !std::holds_alternative<std::monostate>(args[5])
                                  ? integer(args, 5)
                                  : std::optional<std::int64_t>(0);
        if (!player) {
            return script_error("game.build: no such player");
        }
        if (!building) {
            return script_error("game.build: no such building");
        }
        if (!island || !x || !y || !rotation || *island < 0 || *island > 0xFFFF || *rotation < 0 || *rotation > 3 ||
            std::abs(*x) > 0xFFFF || std::abs(*y) > 0xFFFF) {
            return script_error("game.build: the island, x, y and the rotation (0 to 3) must be integers");
        }
        sim::BuildOrder order;
        order.player = *player;
        order.island = static_cast<std::uint16_t>(*island);
        order.orient_to_road = false;
        order.items.push_back(sim::BuildItem{*building, static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y),
                                             static_cast<std::uint8_t>(*rotation), 0});
        const sim::CommandResult result = m_simulation->execute(order);
        if (!result.build || result.build->items.empty()) {
            return Value{std::string("refused")};
        }
        const sim::PlannedBuild& item = result.build->items.front();
        return item.ok() ? Value{true} : Value{std::string(sim::refusal_name(item.refusal))};
    });
    add("demolish", [this](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr) {
            return script_error("game.demolish: only while the game runs, not at the top level");
        }
        const auto player = player_argument(*m_world, args, 0);
        const auto island = integer(args, 1);
        const auto x = integer(args, 2);
        const auto y = integer(args, 3);
        if (!player) {
            return script_error("game.demolish: no such player");
        }
        if (!island || !x || !y || *island < 0 || *island > 0xFFFF || std::abs(*x) > 0xFFFF || std::abs(*y) > 0xFFFF) {
            return script_error("game.demolish: the island, x and y must be integers");
        }
        sim::DemolishOrder order;
        order.player = *player;
        order.island = static_cast<std::uint16_t>(*island);
        order.from = sim::TilePos{static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y)};
        order.to = order.from;
        return Value{static_cast<std::int64_t>(m_simulation->execute(order).demolished)};
    });
    add("destroy", [this](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr) {
            return script_error("game.destroy: only while the game runs, not at the top level");
        }
        const auto island = integer(args, 0);
        const auto x = integer(args, 1);
        const auto y = integer(args, 2);
        if (!island || !x || !y) {
            return script_error("game.destroy: the island, x and y must be integers");
        }
        return Value{*island >= 0 && *island <= 0xFFFF && std::abs(*x) <= 0xFFFF && std::abs(*y) <= 0xFFFF &&
                     m_simulation->script_destroy(static_cast<std::uint16_t>(*island), static_cast<std::int32_t>(*x),
                                                  static_cast<std::int32_t>(*y))};
    });
    add("move_ship", [this](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr) {
            return script_error("game.move_ship: only while the game runs, not at the top level");
        }
        const auto id = integer(args, 0);
        const bool stop = args.size() < 2 || std::holds_alternative<std::monostate>(args[1]);
        const auto x = integer(args, 1);
        const auto y = integer(args, 2);
        if (!id || (!stop && (!x || !y))) {
            return script_error("game.move_ship: the ship's id and a world tile x, y (or nil to stop) are needed");
        }
        const auto& ships = m_world->ships();
        const auto ship = std::find_if(ships.begin(), ships.end(), [&](const sim::Ship& s) { return s.id == *id; });
        if (ship == ships.end() || ship->owner == sim::kNoPlayer) {
            return Value{false};
        }
        if (!stop && (*x < 0 || *y < 0 || *x >= m_world->width() || *y >= m_world->height())) {
            return Value{false};
        }
        sim::ShipOrder order;
        order.player = ship->owner;
        order.ship = ship->id;
        if (!stop) {
            order.target = sim::Target::position(static_cast<std::uint32_t>(*x), static_cast<std::uint32_t>(*y));
        }
        return Value{m_simulation->execute(order).accepted};
    });
    add("units", [this](std::span<const Value> args) -> core::Result<Value> {
        const bool all = args.empty() || std::holds_alternative<std::monostate>(args[0]);
        const auto player = integer(args, 0);
        if (!all && !player) {
            return script_error("game.units: the player must be a number");
        }
        std::vector<Value> units;
        for (const sim::Soldier& unit : m_world->soldiers()) {
            if (!unit.dying() && (all || unit.owner == *player)) {
                units.push_back(unit_table(unit));
            }
        }
        return make_table({}, std::move(units));
    });
    // A land unit's order as its owner would give it: to a world tile (or stop), or an attack on a unit.
    const auto unit_order = [this](std::int64_t id, sim::Target target, bool attack, bool attack_move) -> Value {
        const sim::Soldier* unit =
            id >= 0 && id <= 0xFFFFFFFF ? m_world->soldier(static_cast<std::uint32_t>(id)) : nullptr;
        if (unit == nullptr || unit->owner == sim::kNoPlayer || unit->away()) {
            return Value{false};
        }
        sim::SoldierOrder order;
        order.player = unit->owner;
        order.soldiers = {unit->id};
        order.target = target;
        order.attack = attack;
        order.attack_move = attack_move;
        return Value{m_simulation->execute(order).accepted};
    };
    add("add_unit", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr) {
            return script_error("game.add_unit: only while the game runs, not at the top level");
        }
        const auto player = player_argument(*m_world, args, 0);
        std::optional<std::uint32_t> family;
        if (const auto number = integer(args, 1)) {
            if (*number >= 0 && *number <= 0xFFFF) {
                family = static_cast<std::uint32_t>(*number);
            }
        } else if (const auto* id = args.size() > 1 ? std::get_if<std::string>(&args[1]) : nullptr) {
            family = assets::unit_family_number(m_world->own_figures(),
                                                id->find(':') == std::string::npos ? package.id + ":" + *id : *id);
        }
        const auto x = integer(args, 2);
        const auto y = integer(args, 3);
        if (!player) {
            return script_error("game.add_unit: no such player");
        }
        if (!x || !y || std::abs(*x) > 0xFFFF || std::abs(*y) > 0xFFFF) {
            return script_error("game.add_unit: x and y must be integers");
        }
        if (!family) {
            return Value{false};
        }
        const auto id =
            m_simulation->script_unit(*player, *family, static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y));
        return id ? Value{std::int64_t{*id}} : Value{false};
    });
    add("add_ship", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr || m_simulation->figures() == nullptr) {
            return script_error("game.add_ship: only while the game runs, not at the top level");
        }
        const auto player = player_argument(*m_world, args, 0);
        std::optional<std::uint32_t> figure;
        if (const auto number = integer(args, 1)) {
            if (*number >= 0 && *number <= 0xFFFF) {
                figure = static_cast<std::uint32_t>(*number);
            }
        } else if (const auto* id = args.size() > 1 ? std::get_if<std::string>(&args[1]) : nullptr) {
            figure = assets::figure_number(m_world->own_figures(), *m_simulation->figures(),
                                           id->find(':') == std::string::npos ? package.id + ":" + *id : *id);
        }
        const auto x = integer(args, 2);
        const auto y = integer(args, 3);
        if (!player) {
            return script_error("game.add_ship: no such player");
        }
        if (!x || !y || std::abs(*x) > 0xFFFF || std::abs(*y) > 0xFFFF) {
            return script_error("game.add_ship: x and y must be integers");
        }
        if (!figure) {
            return Value{false};
        }
        const auto id =
            m_simulation->script_ship(*player, *figure, static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y));
        return id ? Value{std::int64_t{*id}} : Value{false};
    });
    add("move_unit", [this, unit_order](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr) {
            return script_error("game.move_unit: only while the game runs, not at the top level");
        }
        const auto id = integer(args, 0);
        const bool stop = args.size() < 2 || std::holds_alternative<std::monostate>(args[1]);
        const auto x = integer(args, 1);
        const auto y = integer(args, 2);
        const auto* fight = args.size() > 3 ? std::get_if<bool>(&args[3]) : nullptr;
        if (!id || (!stop && (!x || !y)) ||
            (args.size() > 3 && fight == nullptr && !std::holds_alternative<std::monostate>(args[3]))) {
            return script_error(
                "game.move_unit: the unit's id, a world tile x, y (or nil to stop) and true or false are needed");
        }
        if (!stop && (*x < 0 || *y < 0 || *x >= m_world->width() || *y >= m_world->height())) {
            return Value{false};
        }
        const sim::Target target =
            stop ? sim::Target{}
                 : sim::Target::tile_spot(static_cast<std::uint32_t>(*x), static_cast<std::uint32_t>(*y));
        return unit_order(*id, target, false, fight != nullptr && *fight);
    });
    add("attack_unit", [this, unit_order](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr) {
            return script_error("game.attack_unit: only while the game runs, not at the top level");
        }
        const auto id = integer(args, 0);
        const auto other = integer(args, 1);
        if (!id || !other) {
            return script_error("game.attack_unit: the ids of the unit and of the one it attacks are needed");
        }
        const sim::Soldier* target =
            *other >= 0 && *other <= 0xFFFFFFFF ? m_world->soldier(static_cast<std::uint32_t>(*other)) : nullptr;
        if (target == nullptr || *other == *id) {
            return Value{false};
        }
        return unit_order(*id, sim::Target::soldier(target->id), true, false);
    });
    add("add_money", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        const auto amount = integer(args, 1);
        if (m_simulation == nullptr) {
            return script_error("game.add_money: only while the game runs, not at the top level");
        }
        if (!player || !amount) {
            return script_error("game.add_money: the player and the amount must be integers");
        }
        if (*player < 0 || *player >= static_cast<std::int64_t>(m_world->players().size()) ||
            !m_simulation->script_money(static_cast<sim::PlayerId>(*player), *amount)) {
            return script_error("game.add_money: no such player");
        }
        return Value{std::int64_t{m_world->players()[static_cast<std::size_t>(*player)].money}};
    });
    add("add_goods", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        const auto island = integer(args, 0);
        const auto index = integer(args, 1);
        const auto ware = ware_argument(*m_world, package.id, args, 2);
        const auto tons = integer(args, 3);
        if (m_simulation == nullptr) {
            return script_error("game.add_goods: only while the game runs, not at the top level");
        }
        if (!island || !index || !tons) {
            return script_error("game.add_goods: the island, the city and the tons must be integers");
        }
        if (!ware) {
            return script_error("game.add_goods: no such good");
        }
        const sim::City* city = find_city(*m_world, *island, *index);
        if (city == nullptr) {
            return script_error("game.add_goods: no such city");
        }
        // Whole tons only: as much as fits, or as much as nobody has reserved.
        constexpr std::int64_t kMaxTons = std::int64_t{1} << 40;
        const std::int64_t wanted = std::clamp(*tons, -kMaxTons, kMaxTons);
        const std::uint32_t room = city->capacity > city->stock(*ware) ? city->capacity - city->stock(*ware) : 0;
        const std::int64_t whole =
            wanted >= 0 ? std::min(wanted, whole_tons(room)) : -std::min(-wanted, whole_tons(city->available(*ware)));
        const auto changed = m_simulation->script_goods(city->island, city->index, *ware, whole * 32);
        return Value{changed ? *changed / 32 : std::int64_t{0}};
    });
    add("win", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        if (m_simulation == nullptr) {
            return script_error("game.win: only while the game runs, not at the top level");
        }
        if (!player) {
            return script_error("game.win: the player's number is missing");
        }
        return Value{*player >= 0 && *player < static_cast<std::int64_t>(m_world->players().size()) &&
                     m_simulation->script_win(static_cast<sim::PlayerId>(*player))};
    });
    add("defeat", [this](std::span<const Value> args) -> core::Result<Value> {
        const auto player = integer(args, 0);
        if (m_simulation == nullptr) {
            return script_error("game.defeat: only while the game runs, not at the top level");
        }
        if (!player) {
            return script_error("game.defeat: the player's number is missing");
        }
        return Value{*player >= 0 && *player < static_cast<std::int64_t>(m_world->players().size()) &&
                     m_simulation->script_defeat(static_cast<sim::PlayerId>(*player))};
    });
    add("mission", [this](std::span<const Value> args) -> core::Result<Value> {
        if (!integer(args, 0)) {
            return script_error("game.mission: the player's number is missing");
        }
        const auto player = player_argument(*m_world, args, 0);
        const sim::Mission* mission = player ? m_world->mission_of(m_world->players()[*player]) : nullptr;
        return mission != nullptr ? mission_table(*m_world, *mission) : Value{};
    });
    add("set_mission", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        if (m_simulation == nullptr) {
            return script_error("game.set_mission: only while the game runs, not at the top level");
        }
        if (!integer(args, 0)) {
            return script_error("game.set_mission: the player's number is missing");
        }
        std::optional<sim::Mission> mission;
        if (args.size() > 1 && !std::holds_alternative<std::monostate>(args[1])) {
            const Table* table = table_of(args[1]);
            if (table == nullptr) {
                return script_error("game.set_mission: the goals must be a table");
            }
            std::string text;
            auto goals = goals_argument(*table, package.id, text);
            if (!goals) {
                return goals.error();
            }
            if (goals->any()) {
                std::vector<std::string> problems;
                mission = sim::mission_from_goals(*m_world, *goals, core::utf8_to_cp1252(text), "game.set_mission",
                                                  &problems);
                if (!problems.empty()) {
                    return script_error(problems.front());
                }
            }
        }
        const auto player = player_argument(*m_world, args, 0);
        return Value{player && m_simulation->script_mission(*player, std::move(mission))};
    });
    add("message", [this, &package](std::span<const Value> args) -> core::Result<Value> {
        const auto* id = args.empty() ? nullptr : std::get_if<std::string>(&args[0]);
        if (id == nullptr || id->empty()) {
            return core::make_error(core::ErrorCode::ScriptError, "game.message: the message's id is missing");
        }
        ScriptMessage message;
        // A name of the package's own messages.toml, or "<package>:<name>" of another package.
        message.id = id->find(':') == std::string::npos ? package.id + ":" + *id : *id;
        if (args.size() > 1) {
            if (const auto* table = std::get_if<std::shared_ptr<const Table>>(&args[1]); table != nullptr && *table) {
                for (const auto& [name, value] : (*table)->fields) {
                    message.args.emplace_back(name, text_of(value));
                }
            } else if (!std::holds_alternative<std::monostate>(args[1])) {
                return core::make_error(core::ErrorCode::ScriptError,
                                        "game.message: the placeholders must be a table of names");
            }
        }
        if (args.size() > 2 && !std::holds_alternative<std::monostate>(args[2])) {
            const auto player = integer(args, 2);
            if (!player || *player < 0 || *player >= static_cast<std::int64_t>(m_world->players().size())) {
                return core::make_error(core::ErrorCode::ScriptError, "game.message: no such player");
            }
            message.player = static_cast<sim::PlayerId>(*player);
        }
        if (args.size() > 3 && !std::holds_alternative<std::monostate>(args[3])) {
            const auto about = integer(args, 3);
            if (!about || *about < 0 || *about >= static_cast<std::int64_t>(m_world->players().size())) {
                return core::make_error(core::ErrorCode::ScriptError, "game.message: no such player to be about");
            }
            message.about = static_cast<sim::PlayerId>(*about);
        }
        if (m_messages.size() >= kMaxPendingMessages) {
            m_messages.erase(m_messages.begin());
        }
        m_messages.push_back(std::move(message));
        return Value{};
    });
}

void GameScripts::after_tick(sim::Simulation& simulation, const sim::TickEvents& events) {
    m_simulation = &simulation;
    for (const auto& package : m_packages) {
        dispatch(*package, events);
    }
    m_simulation = nullptr;
}

void GameScripts::dispatch(Package& package, const sim::TickEvents& events) {
    if (!package.started) {
        package.started = true;
        call(package, "on_start");
    }
    for (const sim::ConstructionEvent& e : events.construction) {
        if (e.kind != sim::ConstructionEvent::Kind::Placed || e.type == nullptr) {
            continue;
        }
        const sim::City* city = m_world->city(e.island, e.city);
        const Value args[] = {make_table({
            {"building", std::int64_t{e.type->id}},
            {"island", std::int64_t{e.island}},
            {"x", std::int64_t{e.x}},
            {"y", std::int64_t{e.y}},
            {"player", city != nullptr && city->owner != sim::kNoPlayer ? Value{std::int64_t{city->owner}} : Value{}},
        })};
        call(package, "on_building_placed", args);
    }
    for (const sim::HouseEvent& e : events.houses) {
        const char* kind = e.kind == sim::HouseEvent::Kind::Upgraded     ? "upgraded"
                           : e.kind == sim::HouseEvent::Kind::Downgraded ? "downgraded"
                                                                         : "abandoned";
        const Value args[] = {make_table({
            {"kind", std::string(kind)},
            {"class", std::int64_t{e.resident_class}},
            {"island", std::int64_t{e.island}},
            {"x", std::int64_t{e.x}},
            {"y", std::int64_t{e.y}},
        })};
        call(package, "on_house_changed", args);
    }
    for (const sim::GameEvent& e : events.game) {
        if (e.kind == sim::GameEvent::Kind::MissionAccomplished) {
            const Value args[] = {player_or_nil(e.player)};
            call(package, "on_player_won", args);
        } else if (e.kind == sim::GameEvent::Kind::PlayerDefeated) {
            const Value args[] = {player_or_nil(e.player), player_or_nil(e.by)};
            call(package, "on_player_defeated", args);
        } else if (e.kind == sim::GameEvent::Kind::GameOver) {
            const Value args[] = {player_or_nil(e.player)};
            call(package, "on_game_over", args);
        }
    }
    dispatch_more(package, events);
    if (m_world->time_ms() % 1000 == 0) {
        const Value args[] = {Value{static_cast<std::int64_t>(m_world->time_ms() / 1000)}};
        call(package, "on_second", args);
    }
}

void GameScripts::dispatch_more(Package& package, const sim::TickEvents& events) {
    const sim::World& world = *m_world;
    const auto good = [&](std::uint8_t ware) { return ware != 0 ? Value{good_id(world, ware)} : Value{}; };
    for (const sim::CityEvent& e : events.cities) {
        using K = sim::CityEvent::Kind;
        const char* kind = nullptr;
        switch (e.kind) {
            case K::Shortage:
                kind = "shortage";
                break;
            case K::Hunger:
                kind = "hunger";
                break;
            case K::Unlocked:
                kind = "level";
                break;
            case K::Monument:
                kind = "monument";
                break;
            case K::Population:
                kind = "population";
                break;
            case K::RunningLow:
                kind = "running_low";
                break;
            case K::MaterialLocked:
                break;
        }
        if (kind == nullptr) {
            continue;
        }
        const sim::City* city = world.city(e.island, e.city);
        std::int64_t inhabitants = 0;
        if (city != nullptr) {
            for (const std::uint32_t n : city->inhabitants) {
                inhabitants += n;
            }
        }
        const Value args[] = {make_table({
            {"kind", std::string(kind)},
            {"island", std::int64_t{e.island}},
            {"city", std::int64_t{e.city}},
            {"player", player_or_nil(e.owner)},
            {"good", e.kind == K::Shortage || e.kind == K::RunningLow ? good(e.ware) : Value{}},
            {"level", e.kind == K::Unlocked ? Value{std::int64_t{e.infra}} : Value{}},
            {"minutes", e.kind == K::RunningLow ? Value{std::int64_t{e.minutes}} : Value{}},
            {"inhabitants", inhabitants},
        })};
        call(package, "on_city", args);
    }
    for (const sim::DisasterEvent& e : events.disasters) {
        const char* kind = e.kind == sim::DisasterEvent::Kind::Started ? "started"
                           : e.kind == sim::DisasterEvent::Kind::Ended ? "ended"
                                                                       : "burned_down";
        const Value args[] = {make_table({
            {"kind", std::string(kind)},
            {"disaster", std::string(e.disaster == sim::Disaster::Kind::Fire ? "fire" : "plague")},
            {"island", std::int64_t{e.island}},
            {"x", std::int64_t{e.x}},
            {"y", std::int64_t{e.y}},
            {"city", e.city != sim::MapCell::kNoCity ? Value{std::int64_t{e.city}} : Value{}},
            {"player", player_or_nil(e.owner)},
            {"spread", e.spread},
        })};
        call(package, "on_disaster", args);
    }
    for (const sim::VolcanoEvent& e : events.volcanoes) {
        const char* kind = e.kind == sim::VolcanoEvent::Kind::Erupted ? "erupted"
                           : e.kind == sim::VolcanoEvent::Kind::Ended ? "ended"
                           : e.kind == sim::VolcanoEvent::Kind::Fire  ? "fire"
                                                                      : "destroyed";
        const Value args[] = {make_table({
            {"kind", std::string(kind)},
            {"island", std::int64_t{e.island}},
            {"x", std::int64_t{e.x}},
            {"y", std::int64_t{e.y}},
        })};
        call(package, "on_volcano", args);
    }
    for (const sim::IslandEvent& e : events.islands) {
        if (e.kind != sim::IslandEvent::Kind::DroughtStarted && e.kind != sim::IslandEvent::Kind::DroughtOver) {
            continue;
        }
        const Value args[] = {make_table({
            {"kind", std::string(e.kind == sim::IslandEvent::Kind::DroughtStarted ? "started" : "over")},
            {"island", std::int64_t{e.island}},
        })};
        call(package, "on_drought", args);
    }
    for (const sim::ShipEvent& e : events.ships) {
        using K = sim::ShipEvent::Kind;
        const char* kind = nullptr;
        // `city` holds the other player of an attack, a sinking and a sale.
        bool other = false;
        switch (e.kind) {
            case K::ShipLaunched:
                kind = "launched";
                break;
            case K::Traded:
                kind = "traded";
                break;
            case K::IslandDiscovered:
                kind = "island_discovered";
                break;
            case K::Attacked:
                kind = "attacked";
                other = true;
                break;
            case K::Sunk:
                kind = "sunk";
                other = true;
                break;
            case K::Sold:
                kind = "sold";
                other = true;
                break;
            case K::PirateAppeared:
                kind = "pirate_appeared";
                break;
            case K::TraderArrived:
                kind = "trader_arrived";
                break;
            case K::DepositFound:
                kind = "deposit_found";
                break;
            default:
                break;
        }
        if (kind == nullptr) {
            continue;
        }
        const bool traded = e.kind == K::Traded;
        const bool place = e.kind == K::IslandDiscovered || e.kind == K::DepositFound || traded;
        const Value args[] = {make_table({
            {"kind", std::string(kind)},
            {"ship", e.ship != 0 ? Value{std::int64_t{e.ship}} : Value{}},
            {"player", player_or_nil(e.player)},
            {"other", other ? player_or_nil(e.city) : Value{}},
            {"island", place ? Value{std::int64_t{e.island}} : Value{}},
            {"city", traded ? Value{std::int64_t{e.city}} : Value{}},
            {"x", e.kind == K::DepositFound ? Value{std::int64_t{e.x}} : Value{}},
            {"y", e.kind == K::DepositFound ? Value{std::int64_t{e.y}} : Value{}},
            {"good", traded || e.kind == K::DepositFound ? good(e.ware) : Value{}},
            {"amount", traded ? Value{e.amount} : Value{}},
            {"money", traded || e.kind == K::Sold ? Value{e.money} : Value{}},
        })};
        call(package, "on_ship", args);
    }
    for (const sim::MilitaryEvent& e : events.military) {
        using K = sim::MilitaryEvent::Kind;
        const char* kind = e.kind == K::SoldierKilled       ? "soldier_killed"
                           : e.kind == K::BuildingDestroyed ? "building_destroyed"
                           : e.kind == K::WarDeclared       ? "war_declared"
                           : e.kind == K::UnitTrained       ? "unit_trained"
                           : e.kind == K::TroopsLanded      ? "troops_landed"
                                                            : nullptr;
        if (kind == nullptr) {
            continue;
        }
        const bool building = e.target.kind == sim::Target::Kind::Building;
        const Value args[] = {make_table({
            {"kind", std::string(kind)},
            {"player", player_or_nil(e.player)},
            {"owner", player_or_nil(e.owner)},
            {"island", building ? Value{std::int64_t{e.target.island}} : Value{}},
            {"x", building ? Value{std::int64_t{e.target.x}} : Value{}},
            {"y", building ? Value{std::int64_t{e.target.y}} : Value{}},
        })};
        call(package, "on_military", args);
    }
    for (const sim::DiplomacyNotice& e : events.diplomacy) {
        using K = sim::DiplomacyNotice::Kind;
        const char* kind = e.kind == K::PeaceBroken    ? "peace_broken"
                           : e.kind == K::PeaceOffered ? "peace_offered"
                           : e.kind == K::PeaceSigned  ? "peace_signed"
                           : e.kind == K::TradeEnded   ? "trade_ended"
                           : e.kind == K::TradeOffered ? "trade_offered"
                           : e.kind == K::TradeSigned  ? "trade_signed"
                                                       : nullptr;
        if (kind == nullptr) {
            continue;
        }
        const Value args[] = {make_table({
            {"kind", std::string(kind)},
            {"from", player_or_nil(e.from)},
            {"to", player_or_nil(e.to)},
        })};
        call(package, "on_diplomacy", args);
    }
}

void GameScripts::call(Package& package, std::string_view function, std::span<const Value> args) {
    if (package.stopped || !package.sandbox.has_function(function)) {
        return;
    }
    if (auto result = package.sandbox.call(function, args); !result) {
        fail(package, result.error().message);
    }
}

void GameScripts::fail(Package& package, const std::string& message) {
    ++package.errors;
    if (m_notes.size() < kMaxPendingMessages) {
        m_notes.push_back(ScriptNote{package.name, message, true});
    }
    if (package.errors >= kMaxScriptErrors && !package.stopped) {
        package.stopped = true;
        if (m_notes.size() < kMaxPendingMessages) {
            m_notes.push_back(ScriptNote{
                package.name, "stopped for the rest of the game after " + std::to_string(kMaxScriptErrors) + " errors",
                true});
        }
    }
}

std::vector<sim::ScriptState> GameScripts::save_states() {
    std::vector<sim::ScriptState> states;
    for (const auto& package : m_packages) {
        sim::ScriptState state;
        state.package = package->name;
        state.random = package->random;
        if (auto data = package->sandbox.save_table(kStateTable)) {
            state.data = std::move(data).value();
        } else if (m_notes.size() < kMaxPendingMessages) {
            // Not counted as a failure: saving also happens on one computer alone (a player saves),
            // and the scripts must go on the same everywhere.
            m_notes.push_back(ScriptNote{package->name, data.error().message, true});
        }
        if (package->campaign) {
            if (auto data = package->sandbox.save_table(kCampaignTable)) {
                state.campaign = std::move(data).value();
            } else if (m_notes.size() < kMaxPendingMessages) {
                m_notes.push_back(ScriptNote{package->name, data.error().message, true});
            }
        }
        states.push_back(std::move(state));
    }
    return states;
}

std::vector<ScriptMessage> GameScripts::take_messages() {
    return std::exchange(m_messages, {});
}

std::vector<ScriptNote> GameScripts::take_notes() {
    return std::exchange(m_notes, {});
}

std::vector<std::string> GameScripts::packages() const {
    std::vector<std::string> ids;
    for (const auto& package : m_packages) {
        ids.push_back(package->name);
    }
    return ids;
}

}  // namespace o1602::script
