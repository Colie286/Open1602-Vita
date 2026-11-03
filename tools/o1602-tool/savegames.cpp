// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602-tool: scenarios and savegames (listing, state, islands) and writing new ones.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tool.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/game_content.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/assets/text_table.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/climates.hpp"
#include "o1602/sim/computer_orders.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/package_islands.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/sim/world_plan.hpp"

namespace o1602::tool {

namespace {

/// "none|few|normal|many" of the world options.
std::optional<sim::WorldSettings::Amount> amount(const Args& args, std::string_view name) {
    const auto it = args.options.find(name);
    if (it == args.options.end()) {
        return sim::WorldSettings::Amount::Normal;
    }
    static constexpr std::array<std::string_view, 4> kNames = {"none", "few", "normal", "many"};
    for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (it->second == kNames[i]) {
            return static_cast<sim::WorldSettings::Amount>(i);
        }
    }
    return std::nullopt;
}

}  // namespace

int cmd_savegames(const Args& args) {
    if (!args.positional.empty()) {
        return usage();
    }
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return fail(game.error());
    }
    const auto dat = game->resolve("Game.dat");
    if (!dat) {
        return fail(core::make_error(core::ErrorCode::NotFound, "Game.dat not found (the game was never started?)"));
    }
    auto bytes = core::read_file(*dat);
    if (!bytes) {
        return fail(bytes.error());
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    for (const auto& slot : assets::parse_savegame_slots(text)) {
        const auto path = game->resolve(slot.file);
        std::printf("%2u  %-24s %s", slot.slot, core::cp1252_to_utf8(slot.name).c_str(), slot.file.c_str());
        if (!path) {
            std::printf("  (missing)\n");
            continue;
        }
        auto file = assets::ChunkFile::load(*path);
        std::string scenario;
        std::string player;
        if (file) {
            if (const auto* name = file->find("NAME")) {
                const auto p = file->payload(*name);
                scenario.assign(p.begin(), std::find(p.begin(), p.end(), std::uint8_t{0}));
            }
            if (auto players = assets::load_players(*file); players && !players->empty()) {
                player = players->front().name;
            }
        }
        std::printf("  scenario \"%s\", player \"%s\"\n", core::cp1252_to_utf8(scenario).c_str(),
                    core::cp1252_to_utf8(player).c_str());
    }
    return 0;
}

/// A savegame of Open1602's own (.o1602) with the original's building table, which it needs to be
/// read (ANNO1602_DIR); nullopt when the file is none.
struct OwnSave {
    assets::BuildingTable buildings;
    sim::World world;
};
std::optional<core::Result<OwnSave>> read_own_save(const fs::path& path) {
    auto bytes = core::read_file(path);
    if (!bytes || !sim::is_open1602_save(*bytes)) {
        return std::nullopt;
    }
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return core::Result<OwnSave>(game.error());
    }
    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return core::Result<OwnSave>(buildings.error());
    }
    std::vector<std::string> warnings;
    auto world = sim::read_game(*bytes, *buildings, &warnings);
    for (const auto& text : warnings) {
        std::fprintf(stderr, "warning: %s\n", text.c_str());
    }
    if (!world) {
        return core::Result<OwnSave>(world.error());
    }
    return core::Result<OwnSave>(OwnSave{std::move(buildings).value(), std::move(world).value()});
}

/// `state` of a .o1602: players, cities with their goods, ships, the computers' producers a package
/// changed; with `buildings_of_players`, how many buildings of each Id every player owns.
int own_state(const sim::World& world, const assets::BuildingTable& buildings, bool buildings_of_players) {
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const sim::Player& p = world.players()[i];
        std::printf("player %zu: role %u, banner %u, money %d %s\n", i, static_cast<unsigned>(p.role),
                    static_cast<unsigned>(p.banner), p.money, core::cp1252_to_utf8(p.name).c_str());
    }
    for (const sim::City& c : world.cities()) {
        std::size_t goods = 0;
        for (const std::uint32_t amount : c.goods) {
            goods += amount != 0 ? 1U : 0U;
        }
        std::printf("city: island %u #%u, owner %u, \"%s\", %zu goods in stock\n", c.island, c.index,
                    static_cast<unsigned>(c.owner), core::cp1252_to_utf8(c.name).c_str(), goods);
    }
    for (const sim::Ship& s : world.ships()) {
        std::printf("ship \"%s\": figure %u, owner %u, at %u,%u\n", core::cp1252_to_utf8(s.name).c_str(), s.figure,
                    static_cast<unsigned>(s.owner), s.x, s.y);
    }
    for (const sim::ComputerProducer& p : world.balance().computer_producers) {
        std::printf("computer producer changed: %s\n", sim::producer_text(p).c_str());
    }
    if (buildings_of_players) {
        // Tiles by owner and building, then buildings by their footprint.
        std::map<std::pair<unsigned, std::uint32_t>, std::size_t> tiles;
        for (const sim::WorldIsland& island : world.islands()) {
            const sim::IslandMap& map = island.map;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& cell = map.top(x, y);
                    const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
                    if (b != nullptr && cell.owner < world.players().size()) {
                        ++tiles[{static_cast<unsigned>(cell.owner), b->id}];
                    }
                }
            }
        }
        unsigned last = ~0U;
        for (const auto& [key, n] : tiles) {
            const assets::BuildingType* b = buildings.by_id(key.second);
            const std::size_t count = n / std::max<std::size_t>(b != nullptr ? b->tile_count() : 1, 1);
            if (key.first != last) {
                std::printf("%sbuildings of player %u:", last == ~0U ? "" : "\n", key.first);
                last = key.first;
            }
            std::printf(" %u(x%zu)", key.second, count);
        }
        if (last != ~0U) {
            std::printf("\n");
        }
    }
    std::printf("%zu players, %zu cities, %zu ships, %zu soldiers\n", world.players().size(), world.cities().size(),
                world.ships().size(), world.soldiers().size());
    return 0;
}

/// `islands` of a .o1602: size, position and the most common buildings on the top layer.
int own_islands(const sim::World& world, const assets::BuildingTable& buildings) {
    for (const sim::WorldIsland& island : world.islands()) {
        const sim::IslandMap& map = island.map;
        std::map<std::uint32_t, std::size_t> histogram;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& cell = map.top(x, y);
                const assets::BuildingType* b = cell.empty() ? nullptr : buildings.by_sprite(cell.sprite);
                if (b != nullptr) {
                    ++histogram[b->id];
                }
            }
        }
        std::vector<std::pair<std::size_t, std::uint32_t>> top;
        for (const auto& [id, n] : histogram) {
            top.emplace_back(n, id);
        }
        std::sort(top.rbegin(), top.rend());
        std::printf("island #%u  size %ux%u  pos (%u,%u)  %zu distinct ids; most common:", island.header.number,
                    map.width(), map.height(), island.x, island.y, histogram.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(5, top.size()); ++i) {
            std::printf(" %u(x%zu)", top[i].second, top[i].first);
        }
        std::printf("\n");
    }
    std::printf("%zu islands\n", world.islands().size());
    return 0;
}

int cmd_state(const Args& args) {
    if (args.positional.size() != 1) {
        return usage();
    }
    if (auto own = read_own_save(fs::path(args.positional[0]))) {
        return *own ? own_state((*own)->world, (*own)->buildings, args.flag("--buildings")) : fail(own->error());
    }
    auto file = assets::ChunkFile::load(fs::path(args.positional[0]));
    if (!file) {
        return fail(file.error());
    }
    auto players = assets::load_players(*file);
    auto ships = assets::load_ships(*file);
    auto state = assets::load_game_state(*file);
    if (!players || !ships || !state) {
        return fail(!players ? players.error() : !ships ? ships.error() : state.error());
    }
    for (const auto& p : *players) {
        std::printf("player %u: kind 0x%02X, colour %u, money %d %s\n", p.slot, static_cast<unsigned>(p.kind),
                    static_cast<unsigned>(p.colour), p.money, core::cp1252_to_utf8(p.name).c_str());
    }
    for (const auto& c : state->cities) {
        std::printf("city: island %u #%u, owner %u, \"%s\"\n", c.island, c.index, c.owner,
                    core::cp1252_to_utf8(c.name).c_str());
    }
    for (const auto& w : state->warehouses) {
        std::size_t used = 0;
        for (const auto& s : w.wares) {
            used += (s.raw[0] | s.raw[1] | s.raw[2] | (s.raw[3] & 0xFFFFU)) != 0 ? 1U : 0U;
        }
        std::printf("warehouse: island %u #%u at %u,%u, %zu goods with data\n", w.island, w.city, w.x, w.y, used);
        if (args.flag("--wares")) {
            for (const auto& s : w.wares) {
                std::printf("  ware %2u (producer %u): %6.2f t   raw %08X %08X %08X %08X\n", s.ware, s.producer_id,
                            s.stock() / static_cast<double>(assets::kStockUnitsPerTon), s.raw[0], s.raw[1], s.raw[2],
                            s.raw[3]);
            }
        }
    }
    struct Counts {
        std::size_t buildings = 0;
        std::size_t working = 0;
        std::size_t disabled = 0;
    };
    std::map<unsigned, Counts> production;
    for (const auto& p : state->production) {
        Counts& c = production[p.island];
        ++c.buildings;
        c.working += p.enabled && p.rate != 0 ? 1U : 0U;
        c.disabled += p.enabled ? 0U : 1U;
        if (args.flag("--production")) {
            constexpr double kTon = assets::kStockUnitsPerTon;
            std::printf(
                "  production %u: %2u,%2u %-3s rate %3u, next %3u s, raw %5.2f/%5.2f t, stock %5.2f t, idle %2u, "
                "produced %5.2f t in %3u s\n",
                p.island, p.x, p.y, p.enabled ? "on" : "off", p.rate, p.countdown, p.raw / kTon, p.raw2 / kTon,
                p.stock / kTon, p.idle_cycles, p.produced / kTon, p.elapsed);
        }
    }
    for (const auto& [island, c] : production) {
        std::printf("production: island %u, %zu buildings, %zu working, %zu switched off\n", island, c.buildings,
                    c.working, c.disabled);
    }
    std::map<unsigned, std::size_t> deer;
    for (const auto& a : state->animals) {
        ++deer[a.island];
    }
    std::printf("deer: %zu on %zu islands\n", state->animals.size(), deer.size());
    for (const auto& s : *ships) {
        std::printf("ship \"%s\": figure %u, owner %u, at %u,%u, direction %u, island %s\n",
                    core::cp1252_to_utf8(s.name).c_str(), s.figure, s.owner, s.x, s.y, s.direction,
                    s.island == 0xFF ? "-" : std::to_string(s.island).c_str());
    }
    return 0;
}

int cmd_islands(const Args& args) {
    if (args.positional.size() != 1) {
        return usage();
    }
    if (auto own = read_own_save(fs::path(args.positional[0]))) {
        return *own ? own_islands((*own)->world, (*own)->buildings) : fail(own->error());
    }
    auto file = assets::ChunkFile::load(fs::path(args.positional[0]));
    if (!file) {
        return fail(file.error());
    }
    auto islands = assets::load_islands(*file);
    if (!islands) {
        return fail(islands.error());
    }
    for (const auto& island : *islands) {
        const auto& h = island.header;
        std::printf("%s #%u  size %ux%u  pos (%u,%u)  layers %zu\n", h.chunk_name.c_str(), h.number, h.width, h.height,
                    h.pos_x, h.pos_y, island.layers.size());
        for (std::size_t l = 0; l < island.layers.size(); ++l) {
            std::map<std::uint16_t, std::size_t> histogram;
            for (const auto& tile : island.layers[l]) {
                ++histogram[tile.id];
            }
            std::vector<std::pair<std::size_t, std::uint16_t>> top;
            for (const auto& [id, n] : histogram) {
                top.emplace_back(n, id);
            }
            std::sort(top.rbegin(), top.rend());
            std::printf("  layer %zu: %zu tiles, %zu distinct ids; most common:", l, island.layers[l].size(),
                        histogram.size());
            for (std::size_t i = 0; i < std::min<std::size_t>(5, top.size()); ++i) {
                std::printf(" %u(x%zu)", top[i].second + assets::IslandTile::kCodIdOffset, top[i].first);
            }
            std::printf("\n");
        }
    }
    std::printf("%zu islands\n", islands->size());
    return 0;
}

int cmd_new_world(const Args& args) {
    if (args.positional.size() != 1) {
        return usage();
    }
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return fail(game.error());
    }
    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return fail(buildings.error());
    }
    auto texts = assets::TextTable::load(*game);
    if (!texts) {
        return fail(texts.error());
    }
    sim::NewGameOptions options;
    options.name = "Spieler";
    options.difficulty = static_cast<std::uint8_t>(std::min<std::size_t>(args.number("--level").value_or(0), 3));
    options.world.seed = args.number("--seed").value_or(1);
    options.world.guarantee_resources = true;
    if (const auto it = args.options.find("--mods"); it != args.options.end()) {
        // The island templates of the packages of a mods folder, drawn with the original's.
        std::vector<std::string> problems;
        const auto packages = assets::load_mod_packages(fs::path(it->second), problems);
        options.world.package_islands = sim::load_package_islands(packages, problems);
        // And their climates, which take a share of the islands (their forest of the original's trees).
        for (const assets::ModPackage& package : packages) {
            if (const auto bytes = core::read_file(package.folder / sim::kClimatesFile)) {
                const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
                sim::apply_climates_file(options.world.climates, package.id, text, problems, &*buildings);
            }
        }
        for (const std::string& p : problems) {
            std::fprintf(stderr, "warning: %s\n", p.c_str());
        }
    }
    // A world for more than the original's players grows with them, as in the game's start dialog.
    sim::WorldSettings w = sim::settings_for_players(
        static_cast<std::uint8_t>(std::clamp<std::size_t>(args.number("--players").value_or(4), 1, sim::kMaxPlayers)));
    if (const auto size = args.pair("--world", 'x')) {
        w.width = static_cast<std::uint32_t>(std::max(0, size->first));
        w.height = static_cast<std::uint32_t>(std::max(0, size->second));
    }
    // A .o1602 holds any number of players; a .gam the original's seven slots, of which the natives
    // (two climates) and the pirates take three: told before the world is made.
    const fs::path out_path{std::string(args.positional[0])};
    const bool own_format = out_path.extension() == sim::kSaveExtension;
    constexpr std::size_t kGamPlayers = assets::kOriginalPlayerSlots - 3;
    if (!own_format && w.players > kGamPlayers) {
        std::fprintf(stderr,
                     "error: a .gam holds at most %zu players (with the natives and pirates); use %zu or fewer, "
                     "or write a %s\n",
                     kGamPlayers, kGamPlayers, std::string(sim::kSaveExtension).c_str());
        return 1;
    }
    const auto islands = amount(args, "--island-count");
    const auto villages = amount(args, "--villages");
    const auto volcanoes = amount(args, "--volcano-islands");
    if (!islands || !villages || !volcanoes) {
        return usage();
    }
    w.islands = *islands;
    w.natives = *villages;
    w.volcanoes = *volcanoes;
    if (const auto it = args.options.find("--island-sizes"); it != args.options.end()) {
        w.sizes = it->second == "small"   ? sim::WorldSettings::Sizes::Small
                  : it->second == "large" ? sim::WorldSettings::Sizes::Large
                                          : sim::WorldSettings::Sizes::Mixed;
    }
    if (const auto it = args.options.find("--island-shapes"); it != args.options.end()) {
        w.shapes = it->second == "own"     ? sim::WorldSettings::Shapes::Own
                   : it->second == "mixed" ? sim::WorldSettings::Shapes::Mixed
                                           : sim::WorldSettings::Shapes::Templates;
    }
    if (const auto it = args.options.find("--island-preset"); it != args.options.end()) {
        const auto preset = std::ranges::find(sim::landscape_presets(), it->second, &sim::LandscapePreset::name);
        if (preset == sim::landscape_presets().end()) {
            std::fprintf(stderr, "--island-preset: no preset %s\n", std::string(it->second).c_str());
            return usage();
        }
        w.shapes = sim::WorldSettings::Shapes::Own;
        w.landscape = preset->landscape;
    }
    for (const std::string_view key : sim::landscape_names::kKeys) {
        const std::string option = "--island-" + std::string(key);
        if (const auto it = args.options.find(option);
            it != args.options.end() && !sim::set_landscape(w.landscape, key, it->second)) {
            std::fprintf(stderr, "%s: bad value %s\n", option.c_str(), std::string(it->second).c_str());
            return usage();
        }
    }
    if (const auto gold = args.number("--gold-islands")) {
        w.gold_islands = static_cast<std::uint8_t>(std::min<std::size_t>(*gold, sim::WorldSettings::kMaxGoldIslands));
    }
    if (args.flag("--together")) {
        w.starts = sim::WorldSettings::Starts::Together;
    }
    options.custom = sim::fit_landscape(w);  // room for the largest island of its own size
    std::vector<std::string> warnings;
    auto world = sim::start_new_game(*game, *buildings, *texts, options, &warnings);
    if (!world) {
        return fail(world.error());
    }
    for (const auto& text : warnings) {
        std::fprintf(stderr, "warning: %s\n", text.c_str());
    }
    std::printf("world %ux%u, %zu islands, %zu ships\n", world->width(), world->height(), world->islands().size(),
                world->ships().size());
    std::size_t villages_made = 0;
    for (const sim::City& c : world->cities()) {
        const sim::Player* p = world->player(c.owner);
        villages_made += p != nullptr && world->behaviour(*p) == sim::FactionBehaviour::Natives ? 1U : 0U;
    }
    std::printf("native villages %zu\n", villages_made);
    std::size_t volcano_islands = 0;
    for (const sim::WorldIsland& island : world->islands()) {
        volcano_islands += island.craters.empty() ? 0U : 1U;
    }
    std::printf("islands with a volcano %zu\n", volcano_islands);
    for (std::size_t c = 0; c < world->balance().climates.size(); ++c) {
        const auto n =
            std::ranges::count(world->islands(), static_cast<std::uint8_t>(c + 1), &sim::WorldIsland::climate);
        std::printf("islands of climate %s %td\n", world->balance().climates[c].id.c_str(), n);
    }
    if (own_format && args.flag("--without-original")) {
        world->set_source(nullptr);  // no section ORIG: a map to pass on holds nothing of the original's files
    }
    auto bytes = own_format ? sim::write_game(*world, *buildings) : sim::write_savegame(*world, *buildings);
    if (!bytes) {
        return fail(bytes.error());
    }
    if (auto status = core::write_file(out_path, *bytes); !status) {
        return fail(status.error());
    }
    std::printf("-> %s\n", std::string(args.positional[0]).c_str());
    return 0;
}

/// A savegame of the original or of Open1602 exported as a .GAM; with --via-own first through
/// Open1602's own format (as a game saved and loaded again), to see what the export loses.
int cmd_export(const Args& args) {
    if (args.positional.size() != 2) {
        return usage();
    }
    auto game = assets::GameDirectory::from_environment();
    if (!game) {
        return fail(game.error());
    }
    auto buildings = assets::BuildingTable::load(*game);
    if (!buildings) {
        return fail(buildings.error());
    }
    auto bytes = core::read_file(fs::path(args.positional[0]));
    if (!bytes) {
        return fail(bytes.error());
    }
    std::vector<std::string> warnings;
    const auto load = [&]() -> core::Result<sim::World> {
        if (sim::is_open1602_save(*bytes)) {
            return sim::read_game(*bytes, *buildings, &warnings);
        }
        auto file = assets::ChunkFile::from_bytes(*bytes);
        if (!file) {
            return file.error();
        }
        return sim::load_world(*file, *buildings, *game, &warnings);
    };
    core::Result<sim::World> world = load();
    if (!world) {
        return fail(world.error());
    }
    if (args.flag("--via-own")) {
        auto own = sim::write_game(*world, *buildings);
        if (!own) {
            return fail(own.error());
        }
        world = sim::read_game(*own, *buildings, &warnings);
        if (!world) {
            return fail(world.error());
        }
    }
    for (const auto& text : warnings) {
        std::fprintf(stderr, "warning: %s\n", text.c_str());
    }
    auto out = sim::write_savegame(*world, *buildings);
    if (!out) {
        return fail(out.error());
    }
    if (auto status = core::write_file(fs::path(args.positional[1]), *out); !status) {
        return fail(status.error());
    }
    std::printf("-> %s\n", std::string(args.positional[1]).c_str());
    return 0;
}

}  // namespace o1602::tool
