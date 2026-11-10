// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sections put together: which are written in which order, and the reading of them all before
// anything is applied. Each section's records are in its own file (section_io.hpp).
#include "sections.hpp"

#include <algorithm>
#include <array>
#include <optional>

#include "section_io.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/assets/unlock_levels.hpp"
#include "o1602/sim/simulation.hpp"

namespace o1602::sim::save {

using core::ErrorCode;
using core::make_error;

namespace {

constexpr std::uint32_t tag(const char (&s)[5]) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24;
}

// Sections (version 1 unless said otherwise). A section that changes in a way older readers cannot
// follow gets the next version; a reader skips what it cannot read unless the section is required.
constexpr std::uint32_t kRules = tag("RULE");
constexpr std::uint32_t kWorld = tag("WRLD");
constexpr std::uint32_t kIslands = tag("ISLE");
constexpr std::uint32_t kTiles = tag("TILE");
constexpr std::uint32_t kPlayers = tag("PLAY");
constexpr std::uint32_t kCities = tag("CITY");
constexpr std::uint32_t kProductions = tag("PROD");
constexpr std::uint32_t kHouses = tag("HOUS");
constexpr std::uint32_t kGrowth = tag("GROW");
constexpr std::uint32_t kCosts = tag("COST");
constexpr std::uint32_t kShips = tag("SHIP");
constexpr std::uint32_t kSoldiers = tag("SOLD");
constexpr std::uint32_t kAnimals = tag("ANIM");
constexpr std::uint32_t kDisasters = tag("DISA");
constexpr std::uint32_t kVolcanoes = tag("VOLC");
constexpr std::uint32_t kSimulation = tag("SIMU");
constexpr std::uint32_t kFleet = tag("FLEE");
constexpr std::uint32_t kEpochs = tag("EPOC");
constexpr std::uint32_t kArmy = tag("ARMY");
constexpr std::uint32_t kDiplomacy = tag("DIPL");
constexpr std::uint32_t kExplored = tag("XPLR");
// Only in Open1602's own savegame (save_game.hpp): the whole map, further world settings, what the
// file holds.
constexpr std::uint32_t kMap = tag("MAPW");
constexpr std::uint32_t kWorld2 = tag("WRL2");
constexpr std::uint32_t kMeta = tag("META");
constexpr std::uint32_t kMissions = tag("MISN");
constexpr std::uint32_t kPlayerState = tag("PSTA");
constexpr std::uint32_t kCityState = tag("CSTA");
/// The clocks of the cities' material locks: when the people last complained.
constexpr std::uint32_t kMaterialClocks = tag("MLCK");
/// The last population message of each city.
constexpr std::uint32_t kPopulationMarks = tag("CPOP");
/// The score's counters of the players and the monument clocks of the cities.
constexpr std::uint32_t kScores = tag("SCOR");
/// The original's file the game came from, without the island tiles (Open1602's own savegame only):
/// the export to a .GAM takes the bytes Open1602 does not model from it (savegame_writer.cpp).
constexpr std::uint32_t kOrigin = tag("ORIG");
/// Orders waiting behind the current one of ships and units (Shift + right click).
constexpr std::uint32_t kQueues = tag("QUEU");
/// Attack move and hold position of ships and units (Open1602, ADR 0010).
constexpr std::uint32_t kStances = tag("STNC");
/// The tutorial flag of the world (TIMERS +0xA5), only written when set.
constexpr std::uint32_t kTutorial = tag("TUTR");
/// The ships that gave up their way to each Kontor (city + 0x252), only written when one did.
constexpr std::uint32_t kGiveUps = tag("CGUP");
/// The goods statistics of the cities (City::ledger), only written when one has any.
constexpr std::uint32_t kLedgers = tag("GLDG");
/// The world's factions and the players' (ADR 0007), only written when they are not the base game's.
constexpr std::uint32_t kFactions = tag("FACT");
/// The values of the balance (balance.hpp, MD-9), only written when they are not the original's.
constexpr std::uint32_t kBalance = tag("BALA");
/// The changed values of buildings (Balance::buildings), only written when there are any.
constexpr std::uint32_t kBuildingValues = tag("BLDV");
/// The changed values of figures (Balance::figures), only written when there are any.
constexpr std::uint32_t kFigureValues = tag("FIGV");
/// The content packages the game uses (World::packages(), ADR 0007 section 7), only written when the
/// world names them.
constexpr std::uint32_t kPackages = tag("PACK");
/// The buildings the packages added and their Ids (World::own_buildings()), only written when there
/// are any. Required: the islands' tiles name those Ids.
constexpr std::uint32_t kOwnBuildings = tag("OWNB");
/// The goods the packages added (World::goods(), goods.hpp), only written when there are any.
/// Required: stocks, holds and buildings name their ware numbers.
constexpr std::uint32_t kGoods = tag("GOOD");
/// The icons of packages the packages' goods show (OwnGood::picture), only written when one does.
/// Optional: without them the goods show the original's icons they name.
constexpr std::uint32_t kGoodPictures = tag("GPIC");
/// The classes of inhabitants as packages changed and added them (World::resident_classes(),
/// resident_classes.hpp), only written when they differ from haeuser.cod's. Required: houses name
/// the classes, and their needs drive the economy.
constexpr std::uint32_t kClasses = tag("CLAS");
/// The unlock levels as packages changed and added them (World::infra_levels(), unlock_levels.hpp),
/// only written when they differ from haeuser.cod's. Required: buildings name the levels.
constexpr std::uint32_t kLevels = tag("LEVL");
/// The levels after the original's 32 the players unlocked (Player::infra_more), only written when
/// one did. Required: without them the players lose what they may build.
constexpr std::uint32_t kUnlocked = tag("UNLK");
/// The state of the packages' scripts (World::script_states(), scripting.hpp), only written when
/// there is any. Optional: without it the scripts start anew.
constexpr std::uint32_t kScripts = tag("SCRP");
/// The groups the players stored (World::unit_groups()), only written when there are any and not
/// for the game's fingerprint. Optional: without them the groups are empty.
constexpr std::uint32_t kUnitGroups = tag("UGRP");

/// Every section this version reads; others are of a later version.
constexpr std::array kKnownSections = {
    kRules,           kWorld,        kIslands,   kTiles,     kPlayers,  kCities,    kProductions,    kHouses,
    kGrowth,          kCosts,        kShips,     kSoldiers,  kAnimals,  kDisasters, kVolcanoes,      kSimulation,
    kFleet,           kEpochs,       kArmy,      kDiplomacy, kExplored, kMap,       kWorld2,         kMeta,
    kMissions,        kPlayerState,  kCityState, kOrigin,    kQueues,   kStances,   kScores,         kMaterialClocks,
    kPopulationMarks, kTutorial,     kGiveUps,   kFactions,  kLedgers,  kBalance,   kBuildingValues, kPackages,
    kFigureValues,    kOwnBuildings, kGoods,     kClasses,   kLevels,   kUnlocked,  kGoodPictures,   kScripts,
    kUnitGroups};

/// Section flag (the u16 after the version): the section holds state the game cannot do without, so
/// a reader that cannot read it (unknown identifier or later version) must not load the file. Other
/// sections it skips with a warning. Files of earlier versions have no flags: all sections optional.
constexpr std::uint16_t kRequired = 1;

/// Whether a section is required (kRequired) when written.
enum class Need : std::uint8_t { Required, Optional };

/// The highest version of a section this version reads.
constexpr std::uint16_t max_version(std::uint32_t id) noexcept {
    if (id == kPlayerState) {
        return kClosedGates;
    }
    if (id == kFleet) {
        return kWideDiscovered;
    }
    if (id == kMap) {
        return kCompactMap;
    }
    if (id == kEpochs) {
        return kWideEpochs;
    }
    if (id == kIslands) {
        return kWideIslands;
    }
    if (id == kScores) {
        return kWideScores;
    }
    if (id == kSimulation) {
        return kSectionedSimulation;
    }
    return id == kIslands || id == kVolcanoes ? kWideCoordinates : 1;
}

/// Writes a section: tag, version, flags, size, payload.
template <class Fn>
void section(core::ByteWriter& out, std::uint32_t id, Fn&& body, Need need = Need::Required,
             std::uint16_t version = 1) {
    out.u32(id);
    out.u16(version);
    out.u16(need == Need::Required ? kRequired : 0);
    const std::size_t size_at = out.size();
    out.u32(0);
    const std::size_t start = out.size();
    Writer writer(out);
    body(writer);
    out.patch_u32(size_at, static_cast<std::uint32_t>(out.size() - start));
}

/// The identifier of a section as text, for messages.
std::string tag_name(std::uint32_t id) {
    std::string name;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const auto c = static_cast<char>((id >> (8 * i)) & 0xFFU);
        name.push_back(c >= ' ' && c <= '~' ? c : '?');
    }
    return name;
}

/// The data of a section.
using Body = std::span<const std::uint8_t>;

/// Calls `fn(id, version, body)` for every section this version reads (a known identifier up to its
/// max_version);
/// an error if the sections are damaged or `fn` returns false. A section of a later version is
/// skipped and reported in `warnings`, or fails the load if it is required (kRequired). A section
/// that comes twice fails the load: no writer makes one, and which of the two counts is a guess.
template <class Fn>
core::Status for_each_section(std::span<const std::uint8_t> data, std::vector<std::string>* warnings, Fn&& fn) {
    core::ByteReader reader(data);
    std::vector<std::uint32_t> seen;
    while (!reader.at_end()) {
        const auto id = reader.u32();
        const auto section_version = reader.u16();
        const auto flags = reader.u16();
        const auto size = reader.u32();
        if (!id || !section_version || !flags || !size) {
            return make_error(ErrorCode::InvalidFormat, "O1602: damaged section header");
        }
        const auto body = reader.bytes(*size);
        if (!body) {
            return make_error(ErrorCode::InvalidFormat, "O1602: damaged section header");
        }
        if (std::find(seen.begin(), seen.end(), *id) != seen.end()) {
            return make_error(ErrorCode::InvalidFormat, "O1602: section " + tag_name(*id) + " twice");
        }
        seen.push_back(*id);
        if (*section_version == 0 || *section_version > max_version(*id) ||
            std::find(kKnownSections.begin(), kKnownSections.end(), *id) == kKnownSections.end()) {
            const std::string what = "section " + tag_name(*id) + " (version " + std::to_string(*section_version) + ")";
            if ((*flags & kRequired) != 0) {
                return make_error(ErrorCode::Unsupported, "O1602: " + what + " needs a newer Open1602");
            }
            if (warnings != nullptr) {
                warnings->push_back(what + " is of a newer Open1602 and was skipped");
            }
            continue;
        }
        if (!fn(*id, *section_version, *body)) {
            return make_error(ErrorCode::InvalidFormat, "O1602: damaged section " + tag_name(*id));
        }
    }
    return core::Ok{};
}

}  // namespace

core::Bytes encode_sections(const World& world, const assets::BuildingTable& buildings, const Simulation* simulation,
                            TileMode tiles, std::span<const std::uint8_t> before, Origin origin) {
    core::ByteWriter out;
    encode_sections(out, world, buildings, simulation, tiles, before, origin);
    return out.take();
}

void encode_sections(core::ByteWriter& out, const World& world, const assets::BuildingTable& buildings,
                     const Simulation* simulation, TileMode tiles, std::span<const std::uint8_t> before, Origin origin,
                     std::vector<std::vector<BlockHash>>* block_hashes) {
    if (tiles == TileMode::Full) {
        // For a list of savegames; a reader takes only the fingerprint (world_from_sections).
        section(
            out, kMeta,
            [&](Writer& o) {
                o.u32(world.width());
                o.u32(world.height());
                o.u64(world.time_ms());
                o.u64(buildings_fingerprint(buildings));
                const PlayerId local = first_human_player(world);
                const Player* p = world.player(local);
                o.string(p != nullptr ? p->name : std::string());
            },
            Need::Optional);
        section(
            out, kMap,
            [&](Writer& o) {
                if (block_hashes != nullptr) {
                    write_map_fingerprint(o, world, buildings, *block_hashes);
                } else {
                    write_map(o, world, buildings);
                }
            },
            Need::Required, kCompactMap);
    }
    // The settings: those of the running game, or those the world was loaded with.
    if (simulation != nullptr || world.saved_rules()) {
        const Rules rules = simulation != nullptr ? simulation->rules() : *world.saved_rules();
        section(out, kRules, [&](Writer& o) { write_rules(o, rules); });
    }
    section(out, kWorld, [&](Writer& o) { write_world(o, world); });
    if (tiles == TileMode::Full) {
        section(out, kWorld2, [&](Writer& o) { o.u32(world.game_settings().pirate_interval_s); });
    }
    if (world.game_settings().tutorial) {
        section(out, kTutorial, [&](Writer& o) { o.u8(1); }, Need::Optional);
    }
    const std::uint16_t islands_at = islands_version(world);
    section(out, kIslands, [&](Writer& o) { write_islands(o, world, islands_at); }, Need::Required, islands_at);
    if (tiles == TileMode::Differences) {
        section(out, kTiles, [&](Writer& o) { write_tiles(o, world, buildings, before); });
    }
    section(out, kPlayers, [&](Writer& o) { write_players(o, world); });
    if (has_own_factions(world)) {
        // Optional: a reader without it plays the base game's factions the role bytes give.
        section(out, kFactions, [&](Writer& o) { write_factions(o, world); }, Need::Optional);
    }
    if (!original_numbers(world.balance())) {
        // Required: a reader without it would compute another game.
        section(out, kBalance, [&](Writer& o) {
            for (const BalanceField& f : kBalanceFields) {
                o.u32(world.balance().*f.value);
            }
        });
    }
    if (!world.balance().buildings.empty()) {
        // Required as well: the buildings would produce and cost otherwise.
        section(out, kBuildingValues, [&](Writer& o) {
            o.list(world.balance().buildings, 9, [&](const BuildingValue& v) {
                o.u32(v.id);
                o.u8(static_cast<std::uint8_t>(v.field));
                o.u32(v.value);
            });
        });
    }
    if (!world.balance().figures.empty()) {
        // Required too: soldiers, ships and carriers would act otherwise.
        section(out, kFigureValues, [&](Writer& o) {
            o.list(world.balance().figures, 9, [&](const FigureValue& v) {
                o.u32(v.number);
                o.u8(static_cast<std::uint8_t>(v.field));
                o.u32(v.value);
            });
        });
    }
    if (!world.goods().own_goods().empty()) {
        section(out, kGoods, [&](Writer& o) {
            o.list(world.goods().own_goods(), 8, [&](const assets::OwnGood& g) {
                o.u8(g.number);
                o.string(g.id);
                o.u16(g.price);
                o.u8(g.icon);
                o.list(g.names, 2, [&](const std::pair<std::string, std::string>& n) {
                    o.string(n.first);
                    o.string(n.second);
                });
            });
        });
    }
    std::vector<const assets::OwnGood*> pictured;
    for (const assets::OwnGood& g : world.goods().own_goods()) {
        if (!g.picture.empty()) {
            pictured.push_back(&g);
        }
    }
    if (!pictured.empty()) {
        section(
            out, kGoodPictures,
            [&](Writer& o) {
                o.list(pictured, 3, [&](const assets::OwnGood* g) {
                    o.u8(g->number);
                    o.string(g->picture);
                });
            },
            Need::Optional);
    }
    if (!world.resident_classes().empty()) {
        section(out, kClasses, [&](Writer& o) {
            o.list(world.resident_classes(), 29, [&](const assets::ResidentClass& c) {
                o.string(c.id);
                o.u8(c.look);
                o.u32(c.share);
                o.u32(c.max_per_house);
                o.u32(c.tax);
                o.u32(c.hiscore);
                o.list(c.needs, 5, [&](const assets::ResidentClass::Need& n) {
                    o.u8(n.ware);
                    o.u32(n.amount);
                });
                o.list(c.names, 8, [&](const std::pair<std::string, std::string>& n) {
                    o.string(n.first);
                    o.string(n.second);
                });
            });
        });
    }
    if (!world.infra_levels().empty()) {
        section(out, kLevels, [&](Writer& o) {
            o.list(world.infra_levels(), 9, [&](const assets::InfraLevel& l) {
                o.string(l.id);
                o.u8(l.resident_class);
                o.u32(l.min_inhabitants);
            });
        });
    }
    std::vector<std::size_t> unlocked_more;
    for (std::size_t p = 0; p < world.players().size(); ++p) {
        const auto& more = world.players()[p].infra_more;
        if (std::any_of(more.begin(), more.end(), [](std::uint32_t w) { return w != 0; })) {
            unlocked_more.push_back(p);
        }
    }
    if (!unlocked_more.empty()) {
        section(out, kUnlocked, [&](Writer& o) {
            o.list(unlocked_more, 29, [&](std::size_t p) {
                o.u8(static_cast<std::uint8_t>(p));
                for (const std::uint32_t w : world.players()[p].infra_more) {
                    o.u32(w);
                }
            });
        });
    }
    if (!world.own_buildings().empty()) {
        // Required: without the buildings the packages added, the saved tiles name nothing.
        section(out, kOwnBuildings, [&](Writer& o) {
            o.list(world.own_buildings(), 8, [&](const assets::OwnBuilding& b) {
                o.string(b.id);
                o.u32(b.number);
            });
        });
    }
    if (world.packages()) {
        // Optional: the packages decide only what this computer loads beside the game, never what
        // the simulation computes with (factions and balance are sections of their own).
        section(
            out, kPackages,
            [&](Writer& o) {
                o.list(*world.packages(), 16, [&](const assets::ContentPackage& p) {
                    o.string(p.id);
                    o.string(p.version);
                    o.u64(p.hash);
                });
            },
            Need::Optional);
    }
    const std::uint16_t player_state_at = player_state_version(world);
    section(
        out, kPlayerState, [&](Writer& o) { write_player_state(o, world, player_state_at); }, Need::Required,
        player_state_at);
    section(out, kMissions, [&](Writer& o) { write_missions(o, world); });
    section(out, kCities, [&](Writer& o) { write_cities(o, world); });
    section(out, kCityState, [&](Writer& o) { write_city_state(o, world); });
    section(out, kMaterialClocks, [&](Writer& o) { write_material_clocks(o, world); });
    section(out, kPopulationMarks, [&](Writer& o) { write_population_marks(o, world); });
    if (std::ranges::any_of(world.cities(), [](const City& c) { return c.ships_gave_up != 0; })) {
        section(out, kGiveUps, [&](Writer& o) { write_give_up_counts(o, world); }, Need::Optional);
    }
    if (std::ranges::any_of(world.cities(), [](const City& c) {
            return c.ledger.minutes_closed() != 0 || !c.ledger.wares().empty() || c.history.minutes_closed() != 0;
        })) {
        section(out, kLedgers, [&](Writer& o) { write_ledgers(o, world); }, Need::Optional);
    }
    const std::uint16_t scores_at = scores_version(world);
    section(out, kScores, [&](Writer& o) { write_scores(o, world, scores_at); }, Need::Required, scores_at);
    section(out, kProductions, [&](Writer& o) { write_productions(o, world); });
    section(out, kHouses, [&](Writer& o) { write_houses(o, world); });
    section(out, kGrowth, [&](Writer& o) { write_growth(o, world); });
    section(out, kCosts, [&](Writer& o) { write_costs(o, world); });
    if (tiles == TileMode::Differences) {
        section(out, kShips, [&](Writer& o) { write_ships(o, world); }, Need::Optional);  // for readers older than FLEE
    }
    section(out, kAnimals, [&](Writer& o) { write_animals(o, world); });
    section(out, kDisasters, [&](Writer& o) { write_disasters(o, world); });
    const std::uint16_t volcanoes_at = volcanoes_version(world);
    section(out, kVolcanoes, [&](Writer& o) { write_volcanoes(o, world, volcanoes_at); }, Need::Required, volcanoes_at);
    const std::uint16_t fleet_at = fleet_version(world);
    section(out, kFleet, [&](Writer& o) { write_fleet(o, world, fleet_at); }, Need::Required, fleet_at);
    const std::uint16_t epochs_at = epochs_version(world);
    section(out, kEpochs, [&](Writer& o) { write_epochs(o, world, epochs_at); }, Need::Required, epochs_at);
    section(out, kArmy, [&](Writer& o) { write_army(o, world); });
    section(out, kDiplomacy, [&](Writer& o) { write_diplomacy(o, world); });
    section(out, kExplored, [&](Writer& o) { write_explored(o, world); });
    section(out, kQueues, [&](Writer& o) { write_queues(o, world); });
    section(out, kStances, [&](Writer& o) { write_stances(o, world); });
    if (tiles == TileMode::Full && origin == Origin::Keep && world.source() != nullptr) {
        section(
            out, kOrigin,
            [&](Writer& o) {
                assets::ChunkWriter chunks;
                for (const assets::Chunk& c : world.source()->chunks()) {
                    if (c.name != "INSELHAUS") {
                        chunks.add(c.name, world.source()->payload(c));
                    }
                }
                o.out().bytes(chunks.bytes());
            },
            Need::Optional);  // only for the export as .GAM
    }
    std::vector<ScriptState> running;
    const std::vector<ScriptState>* scripts = &world.script_states();
    if (simulation != nullptr && simulation->scripts() != nullptr) {
        running = simulation->scripts()->save_states();
        scripts = &running;
    }
    if (!scripts->empty()) {
        section(
            out, kScripts,
            [&](Writer& o) {
                o.list(*scripts, 24, [&](const ScriptState& s) {
                    o.string(s.package);
                    o.u64(s.random.state());
                    o.u64(s.random.increment());
                    o.u8s(s.data);
                });
            },
            Need::Optional);
    }
    const auto has_groups = [](const UnitGroups& g) {
        return std::any_of(g.begin(), g.end(), [](const auto& members) { return !members.empty(); });
    };
    if (origin == Origin::Keep && std::any_of(world.unit_groups().begin(), world.unit_groups().end(), has_groups)) {
        section(
            out, kUnitGroups,
            [&](Writer& o) {
                o.list(world.unit_groups(), 10 * 4, [&](const UnitGroups& player) {
                    for (const std::vector<GroupMember>& members : player) {
                        o.list(members, 9, [&](const GroupMember& m) {
                            o.boolean(m.ship);
                            o.u32(m.id);
                            o.u16(m.dx);
                            o.u16(m.dy);
                        });
                    }
                });
            },
            Need::Optional);
    }
    if (simulation != nullptr) {
        section(
            out, kSimulation, [&](Writer& o) { simulation->save_state(o.out()); }, Need::Required,
            kSectionedSimulation);
    } else if (const std::vector<std::uint8_t>& state = world.simulation_state(); !state.empty()) {
        // A world loaded but not yet run keeps the figures it was saved with, in the version they
        // came in: a state in parts begins with 7 (Simulation::save_state), the earlier ones with 1..6.
        const bool in_parts = state.size() >= 2 && (state[0] | state[1] << 8) == 7;
        section(
            out, kSimulation, [&](Writer& o) { o.out().bytes(state); }, Need::Required,
            in_parts ? kSectionedSimulation : std::uint16_t{1});
    }
}

std::optional<SaveMeta> read_meta(std::span<const std::uint8_t> sections) {
    core::ByteReader reader(sections);
    const auto id = reader.u32();
    const auto version = reader.u16();
    (void)reader.u16();  // flags
    const auto size = reader.u32();
    if (id != kMeta || version != std::uint16_t{1} || !size) {
        return std::nullopt;
    }
    const auto body = reader.bytes(*size);
    if (!body) {
        return std::nullopt;
    }
    core::ByteReader sr(*body);
    StateIn in(sr);
    SaveMeta meta;
    meta.width = in.u32();
    meta.height = in.u32();
    meta.time_ms = in.u64();
    in.u64();  // fingerprint of the building data
    meta.player = in.string();
    if (!in.ok()) {
        return std::nullopt;
    }
    return meta;
}

core::Result<World> world_from_sections(std::span<const std::uint8_t> sections, const assets::BuildingTable& buildings,
                                        std::vector<std::string>* warnings) {
    std::optional<core::Result<World>> world;
    bool meta_ok = true;
    const auto status = for_each_section(sections, warnings, [&](std::uint32_t id, std::uint16_t version, Body body) {
        core::ByteReader sr(body);
        StateIn in(sr);
        if (id == kMeta) {
            in.u32();
            in.u32();
            in.u64();
            const std::uint64_t fingerprint = in.u64();
            meta_ok = in.ok() && fingerprint == buildings_fingerprint(buildings);
        } else if (id == kMap && !world) {
            Reader reader(in);
            world = read_map(reader, buildings, version);
        }
        return true;
    });
    if (!status) {
        return status.error();
    }
    if (!world) {
        return make_error(ErrorCode::InvalidFormat, "save: no map");
    }
    if (*world && !meta_ok && warnings != nullptr) {
        warnings->push_back("the save was made with other game data (haeuser.cod); buildings are matched by id");
    }
    return std::move(*world);
}

core::Status apply_sections(World& world, const assets::BuildingTable& buildings,
                            std::span<const std::uint8_t> sections, std::vector<std::string>* warnings) {
    Decoded d;
    bool damaged = false;
    auto status = for_each_section(sections, warnings, [&](std::uint32_t id, std::uint16_t version, Body body) {
        core::ByteReader sr(body);
        StateIn state(sr);
        Reader in(state);
        switch (id) {
            case kRules:
                d.rules = read_rules(in);
                break;
            case kWorld:
                d.world = read_world(in);
                break;
            case kWorld2: {
                std::uint32_t interval = 0;
                in.u32(interval);
                d.pirate_interval_s = interval;
                break;
            }
            case kTutorial: {
                std::uint8_t on = 0;
                in.u8(on);
                d.tutorial = on != 0;
                break;
            }
            case kIslands:
                d.islands = read_islands(in, version);
                break;
            case kTiles:
                d.tiles = read_tiles(in, buildings);
                break;
            case kPlayers:
                d.players = read_players(in);
                break;
            case kFactions:
                d.factions = read_factions(in);
                break;
            case kBalance: {
                Balance b;
                for (const BalanceField& f : kBalanceFields) {
                    in.u32(b.*f.value);
                }
                if (!valid(b)) {
                    in.fail();
                }
                d.balance = b;
                break;
            }
            case kBuildingValues: {
                std::vector<BuildingValue> values;
                in.list(values, 9, [&](BuildingValue& v) {
                    std::uint8_t field = 0;
                    in.u32(v.id);
                    in.u8(field);
                    in.u32(v.value);
                    v.field =
                        field < kBuildingFields.size() ? static_cast<BuildingField>(field) : BuildingField::Produce;
                    if (field >= kBuildingFields.size() || buildings.by_id(v.id) == nullptr) {
                        in.fail();  // a field of a later version, or a building the data does not have
                    }
                });
                Balance b;
                b.buildings = values;
                if (!valid(b)) {
                    in.fail();
                }
                d.building_values = std::move(values);
                break;
            }
            case kFigureValues: {
                std::vector<FigureValue> values;
                in.list(values, 9, [&](FigureValue& v) {
                    std::uint8_t field = 0;
                    in.u32(v.number);
                    in.u8(field);
                    in.u32(v.value);
                    v.field = field < kFigureFields.size() ? static_cast<FigureField>(field) : FigureField::Speed;
                    if (field >= kFigureFields.size() || v.number >= assets::kMaxFigures) {
                        in.fail();  // a field of a later version, or no figure number of figuren.cod
                    }
                });
                Balance b;
                b.figures = values;
                if (!valid(b)) {
                    in.fail();
                }
                d.figure_values = std::move(values);
                break;
            }
            case kGoods: {
                std::vector<assets::OwnGood> own;
                in.list(own, 8, [&](assets::OwnGood& g) {
                    in.u8(g.number);
                    in.string(g.id);
                    in.u16(g.price);
                    in.u8(g.icon);
                    in.list(g.names, 2, [&](std::pair<std::string, std::string>& n) {
                        in.string(n.first);
                        in.string(n.second);
                    });
                });
                assets::Goods goods;
                for (assets::OwnGood& g : own) {
                    if (!goods.restore(std::move(g))) {
                        in.fail();
                    }
                }
                d.goods = std::move(goods);
                break;
            }
            case kGoodPictures: {
                std::vector<std::pair<std::uint8_t, std::string>> pictures;
                in.list(pictures, 3, [&](std::pair<std::uint8_t, std::string>& p) {
                    in.u8(p.first);
                    in.string(p.second);
                    // "<package>:<name>" (icons.toml).
                    if (p.second.size() < 3 || p.second.size() > 2 * assets::kMaxPackageIdLength + 1 ||
                        p.second.find(':') == std::string::npos) {
                        in.fail();
                    }
                });
                d.good_pictures = std::move(pictures);
                break;
            }
            case kClasses: {
                std::vector<assets::ResidentClass> classes;
                in.list(classes, 29, [&](assets::ResidentClass& c) {
                    in.string(c.id);
                    in.u8(c.look);
                    in.u32(c.share);
                    in.u32(c.max_per_house);
                    in.u32(c.tax);
                    in.u32(c.hiscore);
                    in.list(c.needs, 5, [&](assets::ResidentClass::Need& n) {
                        in.u8(n.ware);
                        in.u32(n.amount);
                        if (!assets::is_good_number(n.ware) || n.ware == assets::ware::kFood) {
                            in.fail();
                        }
                    });
                    in.list(c.names, 8, [&](std::pair<std::string, std::string>& n) {
                        in.string(n.first);
                        in.string(n.second);
                    });
                    // The limits of classes.toml (resident_classes.cpp).
                    if (c.look >= assets::kResidentClasses || c.max_per_house == 0 || c.max_per_house > 255) {
                        in.fail();
                    }
                });
                if (classes.empty() || classes.size() > assets::kMaxResidentClasses) {
                    in.fail();
                }
                d.classes = std::move(classes);
                break;
            }
            case kLevels: {
                std::vector<assets::InfraLevel> levels;
                in.list(levels, 9, [&](assets::InfraLevel& l) {
                    in.string(l.id);
                    in.u8(l.resident_class);
                    in.u32(l.min_inhabitants);
                    // The limits of levels.toml (unlock_levels.cpp).
                    if (l.resident_class >= assets::kMaxResidentClasses || l.min_inhabitants > 10'000'000) {
                        in.fail();
                    }
                });
                if (levels.empty() || levels.size() > assets::kMaxUnlockLevel + 1) {
                    in.fail();
                }
                d.levels = std::move(levels);
                break;
            }
            case kUnlocked: {
                std::vector<Decoded::UnlockedMore> unlocked;
                in.list(unlocked, 29, [&](Decoded::UnlockedMore& u) {
                    in.u8(u.player);
                    for (std::uint32_t& w : u.levels) {
                        in.u32(w);
                    }
                });
                d.unlocked_more = std::move(unlocked);
                break;
            }
            case kOwnBuildings: {
                std::vector<assets::OwnBuilding> own;
                in.list(own, 8, [&](assets::OwnBuilding& b) {
                    in.string(b.id);
                    in.u32(b.number);
                    const assets::BuildingType* type = buildings.by_id(b.number);
                    // The packages of this computer must give the same buildings the same Ids
                    // again (docs/modding/own-buildings.md).
                    if (b.id.size() < 3 || b.id.size() > 2 * assets::kMaxPackageIdLength + 1 || type == nullptr) {
                        in.fail();
                    }
                });
                d.own_buildings = std::move(own);
                break;
            }
            case kScripts: {
                std::vector<ScriptState> states;
                in.list(states, 24, [&](ScriptState& s) {
                    std::uint64_t random = 0;
                    std::uint64_t increment = 0;
                    in.string(s.package);
                    in.u64(random);
                    in.u64(increment);
                    in.u8s(s.data);
                    s.random = Random::from_state(random, increment);
                    if (s.package.empty() || s.package.size() > assets::kMaxPackageIdLength) {
                        in.fail();
                    }
                });
                d.script_states = std::move(states);
                break;
            }
            case kUnitGroups: {
                std::vector<UnitGroups> groups;
                in.list(groups, 10 * 4, [&](UnitGroups& player) {
                    for (std::vector<GroupMember>& members : player) {
                        in.list(members, 9, [&](GroupMember& m) {
                            in.boolean(m.ship);
                            in.u32(m.id);
                            in.u16(m.dx);
                            in.u16(m.dy);
                        });
                    }
                });
                d.unit_groups = std::move(groups);
                break;
            }
            case kPackages: {
                std::vector<assets::ContentPackage> packages;
                in.list(packages, 16, [&](assets::ContentPackage& p) {
                    in.string(p.id);
                    in.string(p.version);
                    in.u64(p.hash);
                    if (p.id.empty() || p.id.size() > assets::kMaxPackageIdLength ||
                        p.version.size() > assets::kMaxPackageVersionLength) {
                        in.fail();
                    }
                });
                d.packages = std::move(packages);
                break;
            }
            case kPlayerState:
                d.player_state = read_player_state(in, version);
                d.player_switches = version >= kScenarioSwitches;
                break;
            case kMissions:
                d.missions = read_missions(in, world.islands().size());
                break;
            case kCities:
                d.cities = read_cities(in);
                break;
            case kCityState:
                d.city_state = read_city_state(in);
                break;
            case kMaterialClocks:
                d.material_clocks = read_material_clocks(in);
                break;
            case kGiveUps:
                d.give_up_counts = read_give_up_counts(in);
                break;
            case kPopulationMarks:
                d.population_marks = read_population_marks(in);
                break;
            case kLedgers:
                d.ledgers = read_ledgers(in);
                break;
            case kScores:
                d.scores = read_scores(in, version);
                break;
            case kProductions:
                d.productions = read_productions(in);
                break;
            case kHouses:
                d.houses = read_houses(in);
                break;
            case kGrowth:
                d.growth = read_growth(in);
                break;
            case kCosts:
                d.costs = read_costs(in);
                break;
            case kShips:
                d.ships = read_ships(in);
                break;
            case kFleet:
                d.fleet = read_fleet(in, version);
                break;
            case kEpochs:
                d.epochs = read_epochs(in, version);
                break;
            case kSoldiers:
                d.soldiers = read_soldiers(in);
                break;
            case kArmy:
                d.army = read_army(in);
                break;
            case kDiplomacy:
                d.diplomacy = read_diplomacy(in);
                break;
            case kExplored:
                d.explored = read_explored(in);
                break;
            case kAnimals:
                d.animals = read_animals(in);
                break;
            case kDisasters:
                d.disasters = read_disasters(in);
                break;
            case kVolcanoes:
                d.volcanoes = read_volcanoes(in, version);
                break;
            case kSimulation:
                d.simulation = std::vector<std::uint8_t>(body.begin(), body.end());
                break;
            case kOrigin:
                d.origin.assign(body.begin(), body.end());
                break;
            case kStances:
                read_stances(in, d.ship_stances, d.soldier_stances);
                break;
            case kQueues:
                read_queues(in, d.ship_queues, d.soldier_queues);
                break;
            default:
                return true;  // META, MAPW (read before)
        }
        // A reader that leaves bytes over does not read what the writer wrote (HI-9).
        const bool whole = id == kSimulation || id == kOrigin || sr.remaining() == 0;
        damaged = damaged || !in.ok() || !whole;
        return !damaged;
    });
    if (!status) {
        return status;
    }
    // Every Open1602 writes these; a file without one would load with an empty part.
    if (!d.world || !d.islands || !d.players) {
        return make_error(ErrorCode::InvalidFormat, "O1602: the world, its islands or its players are missing");
    }
    return apply_decoded(world, d);
}

}  // namespace o1602::sim::save
