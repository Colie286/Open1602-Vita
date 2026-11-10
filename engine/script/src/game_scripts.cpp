// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/script/game_scripts.hpp"

#include <algorithm>
#include <cstdio>
#include <numeric>
#include <optional>
#include <span>

#include "o1602/core/file.hpp"
#include "o1602/core/hash.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::script {

struct GameScripts::Package {
    std::string id;
    Sandbox sandbox;
    sim::Random random;
    bool started = false;  ///< on_start ran, or the savegame kept a state
    bool stopped = false;  ///< failed at its top level or too often
    std::size_t errors = 0;

    Package(std::string package, Limits limits) : id(std::move(package)), sandbox(limits) {}
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
        {"money", std::int64_t{player->money}},
        {"inhabitants", static_cast<std::int64_t>(inhabitants_of(world, id))},
        {"playing", player->active()},
        {"won", player->won},
    });
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
        scripts.push_back(PackageScript{p.id, std::string(core::skip_utf8_bom(text))});
    }
    return scripts;
}

GameScripts::GameScripts(std::vector<PackageScript> scripts, const sim::World& world, Limits limits) : m_world(&world) {
    for (PackageScript& script : scripts) {
        auto package = std::make_unique<Package>(std::move(script.package), limits);
        Package& p = *package;
        m_packages.push_back(std::move(package));
        add_api(p);

        const auto saved = std::find_if(world.script_states().begin(), world.script_states().end(),
                                        [&](const sim::ScriptState& s) { return s.package == p.id; });
        p.random = saved != world.script_states().end() ? saved->random : new_random(world, p.id);
        p.started = saved != world.script_states().end();

        if (const core::Status ran = p.sandbox.run(p.id, script.source); !ran) {
            fail(p, ran.error().message);
            p.stopped = true;
            continue;
        }
        if (saved != world.script_states().end()) {
            if (const core::Status loaded = p.sandbox.load_table(kStateTable, saved->data); !loaded) {
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
            m_notes.push_back(ScriptNote{package.id, std::string(line), false});
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
        if (m_messages.size() >= kMaxPendingMessages) {
            m_messages.erase(m_messages.begin());
        }
        m_messages.push_back(std::move(message));
        return Value{};
    });
}

void GameScripts::after_tick(sim::Simulation& /*simulation*/, const sim::TickEvents& events) {
    for (const auto& package : m_packages) {
        dispatch(*package, events);
    }
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
        const auto player_or_nil = [](sim::PlayerId p) {
            return p == sim::kNoPlayer ? Value{} : Value{std::int64_t{p}};
        };
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
    if (m_world->time_ms() % 1000 == 0) {
        const Value args[] = {Value{static_cast<std::int64_t>(m_world->time_ms() / 1000)}};
        call(package, "on_second", args);
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
        m_notes.push_back(ScriptNote{package.id, message, true});
    }
    if (package.errors >= kMaxScriptErrors && !package.stopped) {
        package.stopped = true;
        if (m_notes.size() < kMaxPendingMessages) {
            m_notes.push_back(ScriptNote{
                package.id, "stopped for the rest of the game after " + std::to_string(kMaxScriptErrors) + " errors",
                true});
        }
    }
}

std::vector<sim::ScriptState> GameScripts::save_states() {
    std::vector<sim::ScriptState> states;
    for (const auto& package : m_packages) {
        sim::ScriptState state;
        state.package = package->id;
        state.random = package->random;
        if (auto data = package->sandbox.save_table(kStateTable)) {
            state.data = std::move(data).value();
        } else if (m_notes.size() < kMaxPendingMessages) {
            // Not counted as a failure: saving also happens on one computer alone (a player saves),
            // and the scripts must go on the same everywhere.
            m_notes.push_back(ScriptNote{package->id, data.error().message, true});
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
        ids.push_back(package->id);
    }
    return ids;
}

}  // namespace o1602::script
