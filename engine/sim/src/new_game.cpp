// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/new_game.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <span>
#include <string_view>
#include <system_error>

#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/savegame_extension.hpp"

namespace o1602::sim {

namespace {

constexpr std::size_t kPlayerSize = 0x430;
constexpr std::size_t kShipSize = 0x1B4;
constexpr std::size_t kTimersSize = 0x298;  // savegames; the endless files have 0x294
constexpr std::size_t kNameOffset = 0x3C0;
constexpr std::size_t kNameSize = 64;
constexpr std::size_t kSecondName = 0x400;
constexpr std::uint8_t kOriginalColours = 4;
constexpr std::uint32_t kEndlessMission = 1;  // SZENE_MISSNR of the endless levels

void put_u32(std::vector<std::uint8_t>& d, std::size_t o, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        d[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

std::uint32_t get_u32(const std::vector<std::uint8_t>& d, std::size_t o) {
    return std::uint32_t{d[o]} | (std::uint32_t{d[o + 1]} << 8) | (std::uint32_t{d[o + 2]} << 16) |
           (std::uint32_t{d[o + 3]} << 24);
}

void put_name(std::vector<std::uint8_t>& record, std::string_view name) {
    std::fill_n(record.begin() + kNameOffset, kNameSize, std::uint8_t{0});
    std::copy_n(name.begin(), std::min(name.size(), kNameSize - 1), record.begin() + kNameOffset);
}

/// PLAYER4 after the reset (0x00474380), the gated load (0x00477911) and the start
/// (0x00477520, 0x00474C70): slot 0 is the human with the chosen colour, slots 1-3 computers with
/// the other colours in order and the names of their colours, then trader, pirates, natives.
/// Money, unlocks and the relation bytes come from the file.
std::vector<std::uint8_t> players(std::span<const std::uint8_t> data, const assets::TextTable& text,
                                  const NewGameOptions& options) {
    std::vector<std::uint8_t> out(data.begin(), data.end());
    const std::vector<std::string>& colour_names = text.section("PLAYCOL");
    const std::uint8_t human = static_cast<std::uint8_t>(options.colour % kOriginalColours);
    std::array<bool, kOriginalColours> taken{};
    taken[human] = true;
    struct Fixed {
        std::uint8_t control;
        std::uint8_t colour;
        const char* name;
        std::uint32_t unlocks;
    };
    static constexpr std::array<Fixed, 3> kOthers = {
        {{0x0D, 6, "Trader4", 0}, {0x0E, 4, "Pirat5", 0xFFFFFFFFU}, {0x0B, 5, "Native6", 0xFFFFFFFFU}}};
    for (std::size_t slot = 0; (slot + 1) * kPlayerSize <= out.size(); ++slot) {
        std::vector<std::uint8_t> r(out.begin() + static_cast<std::ptrdiff_t>(slot * kPlayerSize),
                                    out.begin() + static_cast<std::ptrdiff_t>((slot + 1) * kPlayerSize));
        r[5] = static_cast<std::uint8_t>(slot);
        r[6] = 1;  // players in the session
        r[8] = 7;
        r[0x0C] = 0xFF;
        r[0x0D] = 0;
        std::fill_n(r.begin() + 0x3C, 4, std::uint8_t{0xFF});
        std::fill(r.begin() + kSecondName, r.end(), std::uint8_t{0});
        if (slot < kOriginalColours) {
            std::uint8_t colour = human;
            if (slot != 0) {
                colour = 0;
                while (colour < kOriginalColours && taken[colour]) {
                    ++colour;
                }
                taken[colour % kOriginalColours] = true;
            }
            r[4] = slot == 0 ? 0x00 : 0x0C;
            r[7] = colour;
            put_u32(r, 0x34, 0);
            if (slot == 0) {
                put_name(r, options.name);
            } else {
                // Without [PLAYCOL] the name stays empty: the game shows it in its language (MD-10).
                put_name(r, colour < colour_names.size() ? std::string_view(colour_names[colour]) : std::string_view());
            }
        } else if (slot - kOriginalColours < kOthers.size()) {
            const Fixed& f = kOthers[slot - kOriginalColours];
            r[4] = f.control;
            r[7] = f.colour;
            put_u32(r, 0x34, f.unlocks);
            put_name(r, f.name);
        }
        std::copy(r.begin(), r.end(), out.begin() + static_cast<std::ptrdiff_t>(slot * kPlayerSize));
    }
    return out;
}

/// TIMERS after the reset (0x0040DFE0, 0x00469270): the file's values only where the gated loader
/// reads them (0x00484D30), the rest as the new game sets it.
std::vector<std::uint8_t> timers(std::span<const std::uint8_t> data, const NewGameOptions& options) {
    std::vector<std::uint8_t> t(kTimersSize, 0);
    std::copy_n(data.begin(), std::min(data.size(), kTimersSize), t.begin());
    Random random(options.world.seed, 0x7105);
    const std::uint32_t volcano_minutes = random.next() & 15U;
    const std::uint32_t volcano_seconds = random.next() & 31U;
    put_u32(t, 0x98, 0);
    put_u32(t, 0x9C, 12'800);
    put_u32(t, 0xA0, 0);                   // game time
    t[0xA7] = 1;                           // the endless game's mission number
    put_u32(t, 0xAC, options.game_id);     //
    put_u32(t, 0xB4, 54'000);              // droughts from 90 minutes on
    put_u32(t, 0xB8, 1'500);               // a pirate every 25 minutes (the file's value is not read)
    put_u32(t, 0xBC, options.difficulty);  //
    if (get_u32(t, 0xC0) == 0) {
        put_u32(t, 0xC0, 33);
    }
    put_u32(t, 0xC4, 0);  // no eruption running, none before
    put_u32(t, 0xC8, 0);
    put_u32(t, 0xCC, 42'000 + 10 * (60 * volcano_minutes + volcano_seconds));  // 70-86 minutes
    put_u32(t, 0xD0, 0);                                                       // craters: counted from the islands
    return t;
}

}  // namespace

std::uint32_t new_game_id(std::uint32_t year, std::uint32_t month, std::uint32_t day, std::uint32_t hour,
                          std::uint32_t minute, std::uint32_t random) noexcept {
    std::uint32_t id = year & 7U;
    id = (id << 4) | (month & 15U);
    id = (id << 5) | (day & 31U);
    id = (id << 5) | (hour & 31U);
    id = (id << 6) | (minute & 63U);
    return (id << 8) | (random & 0xFFU);
}

std::string endless_file(std::uint8_t difficulty, std::uint8_t layout, bool multiplayer) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Szenes/Endlosspiel%u%u.%s", static_cast<unsigned>(difficulty),
                  static_cast<unsigned>(layout), multiplayer ? "szm" : "szs");
    return buf;
}

core::Result<EndlessFile> find_endless_file(const assets::GameDirectory& game, std::uint8_t difficulty,
                                            std::uint8_t layout, bool multiplayer) {
    const std::string german = endless_file(difficulty, layout, multiplayer);
    if (const auto path = game.resolve(german)) {
        return EndlessFile{*path, std::string(kEndlessName)};
    }
    const auto folder = game.resolve("Szenes");
    if (!folder) {
        return core::make_error(core::ErrorCode::NotFound, "Szenes not found in the game directory");
    }
    const auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    const auto is_digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(*folder, ec)) {
        if (entry.is_regular_file(ec)) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());  // the same choice whatever order the file system lists
    // The scenario's name: that of a single player level, as two digits end the levels' names.
    std::optional<std::string> name;
    for (const std::filesystem::path& p : files) {
        const std::string stem = core::path_to_utf8(p.stem());
        const std::size_t n = stem.size();
        if (lower(core::path_to_utf8(p.extension())) != ".szs" || n < 3 || !is_digit(stem[n - 1]) ||
            !is_digit(stem[n - 2]) || is_digit(stem[n - 3])) {
            continue;
        }
        auto bytes = core::read_file(p);
        if (!bytes) {
            continue;
        }
        if (auto file = assets::ChunkFile::from_bytes(std::move(bytes).value());
            file && assets::load_mission_number(*file) == kEndlessMission) {
            name = stem.substr(0, n - 2);
            break;
        }
    }
    if (!name) {
        return core::make_error(core::ErrorCode::NotFound,
                                german + " not found in the game directory, nor an endless game of another name");
    }
    const std::string wanted = *name + german.substr(german.size() - 6);  // "<D><V>.szs"
    for (const std::filesystem::path& p : files) {
        if (lower(core::path_to_utf8(p.filename())) == lower(wanted)) {
            return EndlessFile{p, core::utf8_to_cp1252(*name)};
        }
    }
    return core::make_error(core::ErrorCode::NotFound, "Szenes/" + wanted + " not found in the game directory");
}

core::Result<assets::ChunkFile> new_game_scenario(const assets::ChunkFile& endless, const assets::TextTable& text,
                                                  const NewGameOptions& options, std::string_view name) {
    assets::ChunkWriter out;
    out.add("NAME", std::span(reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
    for (const assets::Chunk& c : endless.chunks()) {
        const auto data = endless.payload(c);
        if (c.name == "PLAYER4") {
            out.add(c.name, players(data, text, options));
        } else if (c.name == "TIMERS") {
            out.add(c.name, timers(data, options));
        } else if (c.name == "SHIP4") {
            // The editor left garbage in +0x30 of the single player files (the .szm have 0).
            std::vector<std::uint8_t> ships(data.begin(), data.end());
            for (std::size_t o = 0; o + kShipSize <= ships.size(); o += kShipSize) {
                put_u32(ships, o + 0x30, 0);
            }
            out.add(c.name, ships);
        } else if (c.name != "NAME" && c.name != "CUSTOM") {
            out.add(c.name, data);
        }
    }
    return assets::ChunkFile::from_bytes(out.take());
}

core::Result<World> start_new_game(const assets::GameDirectory& game, const assets::BuildingTable& buildings,
                                   const assets::TextTable& text, const NewGameOptions& options,
                                   std::vector<std::string>* warnings) {
    if (options.difficulty >= NewGameOptions::kDifficulties || options.layout >= NewGameOptions::kLayouts) {
        return core::make_error(core::ErrorCode::OutOfRange, "no endless game with these settings");
    }
    const auto found = find_endless_file(game, options.difficulty, options.layout, options.multiplayer);
    if (!found) {
        return found.error();
    }
    auto endless = assets::ChunkFile::load(found->path);
    if (!endless) {
        return endless.error();
    }
    auto scenario = new_game_scenario(*endless, text, options, found->name);
    if (!scenario) {
        return scenario.error();
    }
    if (!options.custom) {
        return load_world(*scenario, buildings, game, warnings, options.world);
    }
    // An own world: plan it, put the players' ships at their starts, make the islands.
    WorldSettings settings = *options.custom;
    settings.difficulty = options.difficulty;
    settings.seed = options.world.seed;
    const std::vector<TemplateInfo> templates = island_templates(game, buildings);
    auto plan = plan_world(settings, templates, warnings);
    if (!plan) {
        return plan.error();
    }
    auto own = own_world_scenario(*scenario, *plan, settings.players);
    if (!own) {
        return own.error();
    }
    WorldStart start = options.world;
    start.draws = WorldStart::Draws::Seeded;
    auto generated = generate_islands(*own, *plan, game, buildings, start, warnings);
    if (!generated) {
        return generated.error();
    }
    auto world = load_world(generated->file, buildings, game, warnings, start);
    if (!world) {
        return world.error();
    }
    if (auto status = add_players(*world, *plan, settings.players); !status) {
        return status.error();
    }
    return world;
}

core::Status add_players(World& world, const WorldPlan& plan, std::uint8_t players) {
    if (players <= kOriginalPlayers) {
        return core::Ok{};
    }
    std::vector<Player> slots = world.players();
    if (players > kMaxPlayers || slots.size() < 2 || slots.size() > kFirstExtraPlayer || plan.starts.size() < players) {
        return core::make_error(core::ErrorCode::LimitExceeded, "no room for " + std::to_string(players) + " players");
    }
    // The second slot is a computer as the level's file has it: money, unlocks, its ship.
    const Player model = slots[1];
    const auto model_ship =
        std::find_if(world.ships().begin(), world.ships().end(), [](const Ship& s) { return s.owner == 1; });
    if (model_ship == world.ships().end()) {
        return core::make_error(core::ErrorCode::NotFound, "the level has no ship of a computer player to copy");
    }
    const Ship ship = *model_ship;
    while (slots.size() < kFirstExtraPlayer) {
        Player absent;
        absent.status = PlayerStatus::Absent;
        slots.push_back(std::move(absent));
    }
    for (std::size_t n = kOriginalPlayers; n < players; ++n) {
        Player p = model;
        p.role = PlayerRole::Computer;
        p.banner = static_cast<BannerId>(kFirstExtraBanner + (n - kOriginalPlayers));
        p.name.clear();  // the game names it in the player's language (app::name_more_players)
        slots.push_back(std::move(p));
    }
    // A different number of players starts the diplomacy as in a new game.
    world.set_players(std::move(slots));
    for (std::size_t n = kOriginalPlayers; n < players; ++n) {
        Ship s = ship;
        s.id = 0;
        s.owner = player_slot(n);
        s.place(plan.starts[n].x, plan.starts[n].y);
        world.add_ship(std::move(s));
    }
    return core::Ok{};
}

std::optional<PlayerId> take_banner(World& world, PlayerId who, BannerId banner) {
    Player* me = world.player(who);
    if (me == nullptr || me->banner == banner) {
        return std::nullopt;
    }
    std::optional<PlayerId> other;
    for (std::size_t slot = 0; slot < world.players().size(); ++slot) {
        Player* p = world.player(static_cast<PlayerId>(slot));
        if (p != nullptr && p != me && p->banner == banner) {
            p->banner = me->banner;
            other = static_cast<PlayerId>(slot);
            break;
        }
    }
    me->banner = banner;
    return other;
}

core::Result<assets::ChunkFile> own_world_scenario(const assets::ChunkFile& scenario, const WorldPlan& plan,
                                                   std::uint8_t players) {
    assets::ChunkWriter out;
    for (const assets::Chunk& c : scenario.chunks()) {
        const auto data = scenario.payload(c);
        if (c.name == "SHIP4") {
            // The ship of each of the first four players at his start; players without a start get
            // none. The factions' ships (owners 4 to 6) are never moved to a player's start; the
            // players five on get theirs from add_players.
            std::vector<std::uint8_t> ships;
            const auto with_start = std::min<std::size_t>(players, kOriginalPlayers);
            for (std::size_t o = 0; o + kShipSize <= data.size(); o += kShipSize) {
                const std::uint8_t owner = data[o + 0x4B];
                if (owner >= with_start || owner >= plan.starts.size()) {
                    continue;
                }
                std::vector<std::uint8_t> r(data.begin() + static_cast<std::ptrdiff_t>(o),
                                            data.begin() + static_cast<std::ptrdiff_t>(o + kShipSize));
                const StartArea& s = plan.starts[owner];
                r[0x1C] = static_cast<std::uint8_t>(s.x);
                r[0x1D] = static_cast<std::uint8_t>(s.x >> 8);
                r[0x1E] = static_cast<std::uint8_t>(s.y);
                r[0x1F] = static_cast<std::uint8_t>(s.y >> 8);
                ships.insert(ships.end(), r.begin(), r.end());
            }
            out.add(c.name, ships);
        } else if (c.name != std::string_view(kMapChunk)) {
            out.add(c.name, data);
        }
    }
    if (plan.width != World::kOriginalWidth || plan.height != World::kOriginalHeight) {
        out.add(kMapChunk, encode_map_size(plan.width, plan.height));
    }
    return assets::ChunkFile::from_bytes(out.take());
}

}  // namespace o1602::sim
