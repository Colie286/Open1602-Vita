// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scripts of a game's content packages (docs/modding/scripts.md): one sandbox per package that
// has a `script.lua`, run by the simulation at the end of every tick (sim::ScriptHost) with the
// events of that tick, and saved with the game (the table `state` and the random generator).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/assets/game_content.hpp"
#include "o1602/script/sandbox.hpp"
#include "o1602/sim/scripting.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::script {

/// The script of a package: `mods/<package>/script.lua`.
inline constexpr std::string_view kScriptFile = "script.lua";
/// The global table of a script the game saves.
inline constexpr std::string_view kStateTable = "state";
/// A script that failed this often is stopped for the rest of the game.
inline constexpr std::size_t kMaxScriptErrors = 10;
/// Messages posted beyond this many before the game takes them are dropped, the oldest first.
inline constexpr std::size_t kMaxPendingMessages = 256;

struct PackageScript {
    std::string package;  ///< the package's id
    std::string source;   ///< the text of its script.lua
};

/// The scripts of `packages` that have one, in their order. A script that cannot be read is
/// reported in `problems` and left out.
[[nodiscard]] std::vector<PackageScript> read_package_scripts(std::span<const assets::ModPackage> packages,
                                                              std::vector<std::string>& problems);

/// A message a script posted (game.message): the id of a message of messages.toml, its placeholders
/// and for whom it is (kNoPlayer: every player).
struct ScriptMessage {
    std::string id;
    std::vector<std::pair<std::string, std::string>> args;
    sim::PlayerId player = sim::kNoPlayer;
};

/// A script that failed, or a line it printed.
struct ScriptNote {
    std::string package;
    std::string text;
    bool error = false;
};

class GameScripts final : public sim::ScriptHost {
public:
    /// Runs the top level of each script for the game on `world`, which must outlive this object.
    /// A package the world's savegame kept a state for takes it up; the others start at the first
    /// tick (on_start). A script whose top level fails is reported and stays off.
    GameScripts(std::vector<PackageScript> scripts, const sim::World& world, Limits limits = {});
    ~GameScripts() override;

    void after_tick(sim::Simulation& simulation, const sim::TickEvents& events) override;
    [[nodiscard]] std::vector<sim::ScriptState> save_states() override;

    /// The messages the scripts posted since the last call.
    [[nodiscard]] std::vector<ScriptMessage> take_messages();
    /// What the scripts printed and how they failed since the last call.
    [[nodiscard]] std::vector<ScriptNote> take_notes();

    /// The packages with a script, in their order.
    [[nodiscard]] std::vector<std::string> packages() const;

    struct Package;

private:
    void dispatch(Package& package, const sim::TickEvents& events);
    /// Calls `function` if the script defines it; failures count towards kMaxScriptErrors.
    void call(Package& package, std::string_view function, std::span<const Value> args = {});
    void fail(Package& package, const std::string& message);
    void add_api(Package& package);

    const sim::World* m_world;
    std::vector<std::unique_ptr<Package>> m_packages;
    std::vector<ScriptMessage> m_messages;
    std::vector<ScriptNote> m_notes;
};

}  // namespace o1602::script
