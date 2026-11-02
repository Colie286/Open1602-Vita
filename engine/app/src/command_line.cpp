// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_line.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <system_error>

#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/world_plan.hpp"

namespace o1602::app {

namespace {

/// A whole number and nothing after it ("12abc" is no number).
bool parse_number(std::string_view text, std::int32_t& value) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && end == text.data() + text.size();
}

}  // namespace

bool parse_input(std::string_view text, std::vector<ViewerOptions::ScriptedInput>& out) {
    using Input = ViewerOptions::ScriptedInput;
    struct Suffix {
        std::string_view text;
        std::uint16_t mod;
    };
    while (!text.empty()) {
        const std::size_t end = text.find(';');
        std::string_view item = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const std::size_t space = item.find(' ');
        const std::size_t at = item.find('@');
        if (space == std::string_view::npos || at == std::string_view::npos || at < space) {
            return false;
        }
        std::string_view verb = item.substr(0, space);
        const std::string_view args = item.substr(space + 1, at - space - 1);
        Input in;
        std::int32_t frame = 0;
        if (!parse_number(item.substr(at + 1), frame) || frame < 0) {
            return false;
        }
        in.frame = static_cast<std::uint32_t>(frame);
        if (verb == "text") {
            in.kind = Input::Kind::Text;
            in.text = std::string(args);
        } else if (verb == "key") {
            in.kind = Input::Kind::Key;
            std::string_view key = args;
            // Any of them in any order ("119+ctrl+shift+alt"): the name ends at the first.
            std::size_t name_end = key.size();
            for (const auto& [suffix, mod] :
                 {Suffix{"+ctrl", kModCtrl}, Suffix{"+shift", kModShift}, Suffix{"+alt", kModAlt}}) {
                if (const std::size_t p = key.find(suffix); p != std::string_view::npos) {
                    in.mod = static_cast<std::uint16_t>(in.mod | mod);
                    name_end = std::min(name_end, p);
                }
            }
            key = key.substr(0, name_end);
            if (!parse_number(key, in.key)) {
                return false;
            }
        } else if (verb == "wheel") {
            in.kind = Input::Kind::Wheel;
            const std::size_t c1 = args.find(',');
            const std::size_t c2 = c1 == std::string_view::npos ? c1 : args.find(',', c1 + 1);
            std::int32_t x = 0;
            std::int32_t y = 0;
            std::int32_t n = 0;
            if (c2 == std::string_view::npos || !parse_number(args.substr(0, c1), x) ||
                !parse_number(args.substr(c1 + 1, c2 - c1 - 1), y) || !parse_number(args.substr(c2 + 1), n)) {
                return false;
            }
            in.x = static_cast<float>(x);
            in.y = static_cast<float>(y);
            in.key = n;
        } else {
            // "down+shift": the modifiers held while the button goes down or up.
            // Any of them in any order ("119+ctrl+shift+alt"): the name ends at the first.
            std::size_t name_end = verb.size();
            for (const auto& [suffix, mod] :
                 {Suffix{"+ctrl", kModCtrl}, Suffix{"+shift", kModShift}, Suffix{"+alt", kModAlt}}) {
                if (const std::size_t p = verb.find(suffix); p != std::string_view::npos) {
                    in.mod = static_cast<std::uint16_t>(in.mod | mod);
                    name_end = std::min(name_end, p);
                }
            }
            verb = verb.substr(0, name_end);
            in.right = verb == "rdown" || verb == "rup";
            in.kind = verb == "down" || verb == "rdown" ? Input::Kind::Down
                      : verb == "up" || verb == "rup"   ? Input::Kind::Up
                                                        : Input::Kind::Move;
            if (verb != "down" && verb != "up" && verb != "move" && !in.right) {
                return false;
            }
            const std::size_t comma = args.find(',');
            std::int32_t x = 0;
            std::int32_t y = 0;
            if (comma == std::string_view::npos || !parse_number(args.substr(0, comma), x) ||
                !parse_number(args.substr(comma + 1), y)) {
                return false;
            }
            in.x = static_cast<float>(x);
            in.y = static_cast<float>(y);
        }
        out.push_back(in);
    }
    return true;
}

core::Result<CommandLine> parse_command_line(int argc, const char* const* argv) {
    CommandLine out;
    ViewerOptions& options = out.options;
    bool new_game = false;
    bool game_named = false;  ///< without a game the program starts in the main menu
    bool menu = false;
    sim::NewGameOptions new_game_options;  // no name given: the menu's, or "Nameless" in the language set
    sim::WorldSettings custom;
    bool custom_world = false;
    ViewerOptions::Multiplayer multiplayer;
    bool multiplayer_given = false;
    bool name_given = false;
    std::string& game_dir = out.game_dir;
    std::string& scenario = out.scenario;
    std::string& savegame = out.savegame;
    std::string& load = out.load;
    std::optional<std::uint8_t>& layout_given = out.layout;
    std::optional<std::uint64_t>& seed_given = out.seed;
    // The argument that is wrong: the option itself, or the value after it.
    const auto bad = [&](int at) -> core::Result<CommandLine> {
        const std::string_view arg = argv[at];
        if (at > 1 && std::string_view(argv[at - 1]).starts_with("--") && !arg.starts_with("--")) {
            return core::make_error(core::ErrorCode::InvalidFormat,
                                    "bad value for " + std::string(argv[at - 1]) + ": " + std::string(arg));
        }
        return core::make_error(core::ErrorCode::InvalidFormat, "unknown option or missing value: " + std::string(arg));
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--game-dir" && has_value) {
            game_dir = argv[++i];
        } else if (arg == "--scenario" && has_value) {
            scenario = argv[++i];
            game_named = true;
        } else if (arg == "--savegame" && has_value) {
            savegame = argv[++i];
            game_named = true;
        } else if (arg == "--load" && has_value) {
            load = argv[++i];
            game_named = true;
        } else if (arg == "--menu") {
            menu = true;
        } else if (arg == "--no-panel") {
            options.frame = false;
        } else if (arg == "--no-sound") {
            options.effects = false;
        } else if (arg == "--no-music") {
            options.music = false;
        } else if (arg == "--input" && has_value) {
            if (!parse_input(argv[++i], options.input)) {
                return bad(i);
            }
        } else if (arg == "--select" && has_value && std::string_view(argv[i + 1]).starts_with("ship:")) {
            std::int32_t n = 0;
            if (!parse_number(std::string_view(argv[++i]).substr(5), n) || n < 0) {
                return bad(i);
            }
            options.select_ship = static_cast<std::uint32_t>(n);
        } else if (arg == "--select" && has_value && std::string_view(argv[i + 1]).starts_with("unit:")) {
            std::int32_t n = 0;
            if (!parse_number(std::string_view(argv[++i]).substr(5), n) || n < 0) {
                return bad(i);
            }
            options.select_soldier = static_cast<std::uint32_t>(n);
        } else if (arg == "--select" && has_value) {
            std::array<std::int32_t, 3> v{};
            std::string_view rest = argv[++i];
            bool ok = true;
            for (std::size_t k = 0; k < 3; ++k) {
                const std::size_t comma = rest.find(',');
                ok = ok && parse_number(rest.substr(0, comma), v[k]) && v[k] >= 0 && v[k] <= 0xFFFF;
                rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
            }
            if (!ok) {
                return bad(i);
            }
            options.select = std::array<std::uint16_t, 3>{
                static_cast<std::uint16_t>(v[0]), static_cast<std::uint16_t>(v[1]), static_cast<std::uint16_t>(v[2])};
        } else if (arg == "--original-mouse") {
            options.original_mouse = true;
        } else if (arg == "--no-confirm") {
            options.confirm = false;
        } else if (arg == "--no-dialogs") {
            out.dialogs = false;
        } else if (arg == "--keys" && has_value) {
            options.keys_file = core::path_from_utf8(argv[++i]);
        } else if (arg == "--language" && has_value) {
            options.language = argv[++i];
        } else if (arg == "--no-videos") {
            options.videos = false;
        } else if (arg == "--intro") {
            options.intro = true;
        } else if (arg == "--video" && has_value) {
            std::int32_t video = 0;
            if (!parse_number(argv[++i], video) || video < 0 || video > 0xFFFF) {
                return bad(i);
            }
            options.video = static_cast<std::uint16_t>(video);
        } else if (arg == "--message" && has_value) {
            options.message = argv[++i];
        } else if (arg == "--owners") {
            options.show_owners = true;
        } else if (arg == "--original-sheets") {
            options.derive_small_figures = false;
        } else if (arg == "--original-flags") {
            options.original_flags = true;
        } else if (arg == "--original-data") {
            options.original_data = true;
        } else if (arg == "--frames" && has_value) {
            std::int32_t frames = 0;
            if (!parse_number(argv[++i], frames) || frames < 0) {
                return bad(i);
            }
            options.max_frames = static_cast<std::uint32_t>(frames);
        } else if (arg == "--zoom" && has_value) {
            std::int32_t zoom = 0;
            if (!parse_number(argv[++i], zoom) || zoom < 0 || zoom > 2) {
                return bad(i);
            }
            options.zoom = static_cast<render::Zoom>(zoom);
        } else if (arg == "--screenshot" && has_value) {
            options.screenshot = core::path_from_utf8(argv[++i]);
        } else if (arg == "--refund" && has_value) {
            // Game setting: what demolishing gives back (none as in the original, partial[:N], full).
            const std::string_view mode = argv[++i];
            if (mode == "none") {
                options.rules.demolish_refund = sim::Refund::None;
            } else if (mode == "full") {
                options.rules.demolish_refund = sim::Refund::Full;
            } else if (mode.starts_with("partial")) {
                options.rules.demolish_refund = sim::Refund::Partial;
                if (mode.size() > 8 && mode[7] == ':') {
                    std::int32_t percent = 0;
                    if (!parse_number(mode.substr(8), percent) || percent < 1 || percent > 100) {
                        return bad(i);
                    }
                    options.rules.refund_percent = static_cast<std::uint8_t>(percent);
                }
            } else {
                return bad(i);
            }
        } else if (arg == "--droughts" && has_value) {
            // Game setting: how often droughts come.
            const std::string_view mode = argv[++i];
            if (mode == "game") {
                options.rules.droughts = sim::Droughts::Game;
            } else if (mode == "off") {
                options.rules.droughts = sim::Droughts::Off;
            } else if (mode == "rare") {
                options.rules.droughts = sim::Droughts::Rare;
            } else if (mode == "normal") {
                options.rules.droughts = sim::Droughts::Normal;
            } else if (mode == "frequent") {
                options.rules.droughts = sim::Droughts::Frequent;
            } else {
                return bad(i);
            }
        } else if (arg == "--save" && has_value) {
            options.save = core::path_from_utf8(argv[++i]);
        } else if (arg == "--volcanoes" && has_value) {
            // Game setting: whether volcanoes erupt.
            const std::string_view mode = argv[++i];
            if (mode == "game") {
                options.rules.volcanoes = sim::Volcanoes::Game;
            } else if (mode == "off") {
                options.rules.volcanoes = sim::Volcanoes::Off;
            } else if (mode == "on") {
                options.rules.volcanoes = sim::Volcanoes::On;
            } else if (mode == "rare") {
                options.rules.volcanoes = sim::Volcanoes::Rare;
            } else {
                return bad(i);
            }
        } else if ((arg == "--plagues" || arg == "--fires") && has_value) {
            // Game setting: how often plagues or fires break out in towns.
            sim::Outbreaks& rule = arg == "--plagues" ? options.rules.plagues : options.rules.fires;
            const std::string_view mode = argv[++i];
            if (mode == "original") {
                rule = sim::Outbreaks::Original;
            } else if (mode == "rare") {
                rule = sim::Outbreaks::Rare;
            } else if (mode == "off") {
                rule = sim::Outbreaks::Off;
            } else {
                return bad(i);
            }
        } else if (arg == "--event-speed" && has_value) {
            // Game setting: which messages set the game back to normal speed.
            const std::string_view mode = argv[++i];
            if (mode == "normal") {
                options.rules.event_speed = sim::EventSpeed::Normal;
            } else if (mode == "attacks") {
                options.rules.event_speed = sim::EventSpeed::Attacks;
            } else if (mode == "keep") {
                options.rules.event_speed = sim::EventSpeed::Keep;
            } else {
                return bad(i);
            }
        } else if (arg == "--pirates" && has_value) {
            // Game setting: whether pirates come.
            const std::string_view mode = argv[++i];
            if (mode == "game") {
                options.rules.pirates = sim::Pirates::Game;
            } else if (mode == "off") {
                options.rules.pirates = sim::Pirates::Off;
            } else if (mode == "on") {
                options.rules.pirates = sim::Pirates::On;
            } else {
                return bad(i);
            }
        } else if (arg == "--pirate-nests" && has_value) {
            // Game setting: one pirate nest, or one for every four players.
            const std::string_view mode = argv[++i];
            if (mode == "one") {
                options.rules.pirate_nests = sim::PirateNests::One;
            } else if (mode == "per4") {
                options.rules.pirate_nests = sim::PirateNests::Scaled;
            } else {
                return bad(i);
            }
        } else if (arg == "--reveal" && has_value) {
            // Game setting: which towns are discovered from the start.
            const std::string_view mode = argv[++i];
            if (mode == "none") {
                options.rules.reveal = sim::Reveal::None;
            } else if (mode == "original") {
                options.rules.reveal = sim::Reveal::Original;
            } else if (mode == "all") {
                options.rules.reveal = sim::Reveal::All;
            } else {
                return bad(i);
            }
        } else if (arg == "--ore-veins" && has_value) {
            // Game setting: large iron veins as the game says, run out, or endless for the deep mine.
            // "original" is the name finite veins had before "game" came.
            const std::string_view mode = argv[++i];
            if (mode == "game") {
                options.rules.ore_veins = sim::OreVeins::Game;
            } else if (mode == "finite" || mode == "original") {
                options.rules.ore_veins = sim::OreVeins::Finite;
            } else if (mode == "endless") {
                options.rules.ore_veins = sim::OreVeins::Endless;
            } else {
                return bad(i);
            }
        } else if (arg == "--computers" && has_value) {
            // Game setting: whether the computer opponents play.
            const std::string_view mode = argv[++i];
            if (mode == "game") {
                options.rules.computer_players = sim::Computers::Game;
            } else if (mode == "off") {
                options.rules.computer_players = sim::Computers::Off;
            } else if (mode == "on") {
                options.rules.computer_players = sim::Computers::On;
            } else {
                return bad(i);
            }
        } else if (arg == "--bread" && has_value) {
            // Game setting: the original's bread chain or the revised one (data/balance/revised_bread.toml).
            const std::string_view mode = argv[++i];
            if (mode == "original" || mode == "revised") {
                options.rules.revised_bread = mode == "revised";
            } else {
                return bad(i);
            }
        } else if (arg == "--ai" && has_value) {
            // Game setting: the computer players of Open1602 or the original's (S-097 to S-099).
            const std::string_view mode = argv[++i];
            if (mode == "open1602" || mode == "classic") {
                options.rules.bugs.classic_ai = mode == "classic";
            } else {
                return bad(i);
            }
        } else if (arg == "--new-game" && has_value) {
            // A new endless game: difficulty 0 (easy) .. 3 (hard).
            std::int32_t difficulty = 0;
            if (!parse_number(argv[++i], difficulty) || difficulty < 0 ||
                difficulty >= sim::NewGameOptions::kDifficulties) {
                return bad(i);
            }
            new_game = true;
            new_game_options.difficulty = static_cast<std::uint8_t>(difficulty);
        } else if (arg == "--layout" && has_value) {
            std::int32_t layout = 0;
            if (!parse_number(argv[++i], layout) || layout < 0 || layout >= sim::NewGameOptions::kLayouts) {
                return bad(i);
            }
            layout_given = static_cast<std::uint8_t>(layout);
        } else if (arg == "--seed" && has_value) {
            std::uint64_t seed = 0;
            const std::string_view text = argv[++i];
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), seed);
            if (ec != std::errc{} || end != text.data() + text.size()) {
                return bad(i);
            }
            seed_given = seed;
        } else if (arg == "--name" && has_value) {
            new_game_options.name = core::utf8_to_cp1252(argv[++i]);  // the game keeps names in Windows-1252
            name_given = true;
        } else if (arg == "--host") {
            multiplayer.role = ViewerOptions::Multiplayer::Role::Host;
            multiplayer_given = true;
        } else if (arg == "--join" && has_value) {
            // HOST or HOST:PORT (IPv4 or a name).
            const std::string_view to = argv[++i];
            const std::size_t colon = to.rfind(':');
            multiplayer.role = ViewerOptions::Multiplayer::Role::Join;
            multiplayer.address = std::string(to.substr(0, colon));
            if (colon != std::string_view::npos) {
                std::int32_t port = 0;
                if (!parse_number(to.substr(colon + 1), port) || port <= 0 || port > 65535) {
                    return bad(i);
                }
                multiplayer.port = static_cast<std::uint16_t>(port);
            }
            multiplayer_given = !multiplayer.address.empty();
            if (!multiplayer_given) {
                return bad(i);
            }
        } else if (arg == "--password" && has_value) {
            multiplayer.password = argv[++i];
        } else if (arg == "--port" && has_value) {
            std::int32_t port = 0;
            if (!parse_number(argv[++i], port) || port <= 0 || port > 65535) {
                return bad(i);
            }
            multiplayer.port = static_cast<std::uint16_t>(port);
        } else if (arg == "--colour" && has_value) {
            std::int32_t colour = 0;
            if (!parse_number(argv[++i], colour) || colour < 0 || colour > 3) {
                return bad(i);
            }
            new_game_options.colour = static_cast<std::uint8_t>(colour);
        } else if (arg == "--islands" && has_value) {
            // How the islands of an endless game are drawn: as the original (512 worlds per file)
            // or from the whole seed.
            const std::string_view mode = argv[++i];
            if (mode == "original") {
                options.world.draws = sim::WorldStart::Draws::Table;
            } else if (mode == "seed") {
                options.world.draws = sim::WorldStart::Draws::Seeded;
            } else {
                return bad(i);
            }
        } else if ((arg == "--island-count" || arg == "--villages" || arg == "--volcano-islands") && has_value) {
            const std::string_view mode = argv[++i];
            sim::WorldSettings::Amount amount = sim::WorldSettings::Amount::Normal;
            if (mode == "none") {
                amount = sim::WorldSettings::Amount::None;
            } else if (mode == "few") {
                amount = sim::WorldSettings::Amount::Few;
            } else if (mode == "normal") {
                amount = sim::WorldSettings::Amount::Normal;
            } else if (mode == "many") {
                amount = sim::WorldSettings::Amount::Many;
            } else {
                return bad(i);
            }
            (arg == "--island-count" ? custom.islands
             : arg == "--villages"   ? custom.natives
                                     : custom.volcanoes) = amount;
            custom_world = true;
        } else if (arg == "--island-sizes" && has_value) {
            const std::string_view mode = argv[++i];
            if (mode == "small") {
                custom.sizes = sim::WorldSettings::Sizes::Small;
            } else if (mode == "mixed") {
                custom.sizes = sim::WorldSettings::Sizes::Mixed;
            } else if (mode == "large") {
                custom.sizes = sim::WorldSettings::Sizes::Large;
            } else {
                return bad(i);
            }
            custom_world = true;
        } else if (arg == "--island-shapes" && has_value) {
            const std::string_view mode = argv[++i];
            if (mode == "templates") {
                custom.shapes = sim::WorldSettings::Shapes::Templates;
            } else if (mode == "mixed") {
                custom.shapes = sim::WorldSettings::Shapes::Mixed;
            } else if (mode == "own") {
                custom.shapes = sim::WorldSettings::Shapes::Own;
            } else {
                return bad(i);
            }
            custom_world = true;
        } else if (arg == "--island-preset" && has_value) {
            const std::string_view name = argv[++i];
            const auto preset = std::ranges::find(sim::kLandscapePresets, name, &sim::LandscapePreset::name);
            if (preset == sim::kLandscapePresets.end()) {
                return bad(i);
            }
            custom.shapes = sim::WorldSettings::Shapes::Own;
            custom.landscape = preset->landscape;
            custom_world = true;
        } else if (arg.starts_with("--island-") && has_value &&
                   std::ranges::find(sim::landscape_names::kKeys, arg.substr(9)) != sim::landscape_names::kKeys.end()) {
            if (!sim::set_landscape(custom.landscape, arg.substr(9), argv[++i])) {
                return bad(i);
            }
            custom_world = true;
        } else if (arg == "--starts" && has_value) {
            const std::string_view mode = argv[++i];
            if (mode == "spread") {
                custom.starts = sim::WorldSettings::Starts::Spread;
            } else if (mode == "together") {
                custom.starts = sim::WorldSettings::Starts::Together;
            } else {
                return bad(i);
            }
            custom_world = true;
        } else if (arg == "--gold-islands" && has_value) {
            const std::string_view mode = argv[++i];
            std::int32_t gold = 0;
            if (mode != "auto" && (!parse_number(mode, gold) || gold < 1 ||
                                   gold > static_cast<std::int32_t>(sim::WorldSettings::kMaxGoldIslands))) {
                return bad(i);
            }
            custom.gold_islands = static_cast<std::uint8_t>(gold);
            custom_world = true;
        } else if (arg == "--players" && has_value) {
            std::int32_t players = 0;
            if (!parse_number(argv[++i], players) || players < 1 ||
                players > static_cast<std::int32_t>(sim::kMaxPlayers)) {
                return bad(i);
            }
            custom.players = static_cast<std::uint8_t>(players);
            custom_world = true;
        } else if (arg == "--world" && has_value) {
            const std::string_view size = argv[++i];
            const auto x = size.find('x');
            std::int32_t w = 0;
            std::int32_t h = 0;
            if (x == std::string_view::npos || !parse_number(size.substr(0, x), w) ||
                !parse_number(size.substr(x + 1), h) || w < static_cast<std::int32_t>(sim::WorldSettings::kMinSide) ||
                h < static_cast<std::int32_t>(sim::WorldSettings::kMinSide) ||
                w > static_cast<std::int32_t>(sim::WorldSettings::kMaxSide) ||
                h > static_cast<std::int32_t>(sim::WorldSettings::kMaxSide)) {
                return bad(i);
            }
            custom.width = static_cast<std::uint32_t>(w);
            custom.height = static_cast<std::uint32_t>(h);
            custom_world = true;
        } else if (arg == "--size" && has_value) {
            const std::string_view size = argv[++i];
            const auto x = size.find('x');
            if (x == std::string_view::npos || !parse_number(size.substr(0, x), options.width) ||
                !parse_number(size.substr(x + 1), options.height)) {
                return bad(i);
            }
        } else {
            return bad(i);  // an unknown option, or one whose value is missing
        }
    }

    if (custom_world && !new_game) {
        return core::make_error(core::ErrorCode::InvalidFormat, "an own world (--world ...) needs --new-game");
    }
    if (new_game) {
        if (custom_world) {
            new_game_options.custom = custom;
        }
        out.new_game = new_game_options;
    }
    if (name_given) {
        options.player_name = new_game_options.name;
    }
    if (multiplayer_given) {
        if (name_given) {
            multiplayer.name = new_game_options.name;
        }
        options.multiplayer = multiplayer;
    }
    // No game named: the main menu, as the original starts (with --frame off there is none). A
    // computer joining a host gets the game from it.
    options.menu = options.frame && !multiplayer_given && (menu || (!game_named && !new_game));
    if (options.max_frames != 0) {
        out.dialogs = false;
    }
    return out;
}

}  // namespace o1602::app
