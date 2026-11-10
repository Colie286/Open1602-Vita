// SPDX-License-Identifier: GPL-3.0-or-later
//
// Players and ships of scenarios (.szs/.szm) and savegames (.gam). Both file types use the same
// chunk set (one chunk name table in 1602.exe at 0x00498244); see docs/formats/gam.md.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

/// Controller of a player slot (PLAYER4 byte +4).
enum class PlayerKind : std::uint8_t {
    Human = 0x00,
    Natives = 0x0B,
    Computer = 0x0C,
    Trader = 0x0D,  ///< free trader
    Pirate = 0x0E,
    /// Set by the game, not by scenarios: a player who lost (op 3 sub 9) or a slot nobody plays
    /// (no city and no ship at the start, 0x0048C0F0; docs/re/missions.md).
    Defeated = 0x09,
    Absent = 0x0A,
};

/// Colour of a player (PLAYER4 byte +7). Selects the flag figure (table at 0x00498F2C):
/// 0..3 FAHNE1..FAHNE4 (red, blue, yellow, white), 4 FAHNEPIRAT; 5 and 6 have no flag.
enum class PlayerColour : std::uint8_t {
    Red = 0,
    Blue = 1,
    Yellow = 2,
    White = 3,
    Pirate = 4,
    Natives = 5,
    Trader = 6,
};
inline constexpr std::uint8_t kPlayerColourCount = 7;
/// The former names (LC-20).
using PlayerColor [[deprecated("use PlayerColour")]] = PlayerColour;
[[deprecated("use kPlayerColourCount")]] inline constexpr std::uint8_t kPlayerColorCount = kPlayerColourCount;

/// One player slot. The original stores 7 slots of 0x430 bytes: four players (human or
/// computer), the free trader, the pirates and the natives.
/// Owner value meaning "nobody" (island tile attribute bits 22..25, ship byte +0x4B).
inline constexpr std::uint8_t kNoOwner = 7;

struct PlayerRecord {
    std::int32_t money = 0;
    PlayerKind kind = PlayerKind::Human;
    std::uint8_t slot = 0;  ///< index of the record, also used as owner number on tiles and ships
    PlayerColour colour = PlayerColour::Red;
    std::string name;  ///< +0x3C0, Windows-1252; empty in scenarios, the human's name in savegames
    /// Unlocked build levels: bit n - 1 = INFRA number n (+0x34; runtime player +0x6C, set by the
    /// city tick 0x0047F8A0 and never cleared).
    std::uint32_t infra_mask = 0;
    std::uint8_t castles_built = 0;       ///< +0x0E (runtime +0x87): Schloss built, at most one
    std::uint8_t cathedrals_built = 0;    ///< +0x0F (+0x88): Kathedrale built, at most one
    std::uint16_t monuments_allowed = 0;  ///< +0x38 (+0x8A): Denkmal allowances
    std::uint16_t monuments_built = 0;    ///< +0x3A (+0x8C)
    std::uint16_t arches_allowed = 0;     ///< +0x10 (+0x8E): Triumphbogen allowances
    std::uint16_t arches_built = 0;       ///< +0x12 (+0x90)
    std::uint16_t soldiers_killed = 0;    ///< +0x14: for the score
    std::uint16_t soldiers_lost = 0;      ///< +0x16
    std::uint16_t ships_lost = 0;         ///< +0x18
    std::uint16_t ships_sunk = 0;         ///< +0x1A
    /// Diplomacy towards the 7 slots (saver 0x00478160, docs/re/diplomacy.md): treaty states 0..3,
    /// the time of the last war declaration (1/10 s, only the own entry is used), the decaying
    /// tribute, trade and damage counters, and the first 7 of 32 remembered events.
    std::array<std::uint8_t, 7> peace{};        ///< +0xC0 + 8b
    std::array<std::uint32_t, 7> war_time{};    ///< +0xC4 + 8b
    std::array<std::uint8_t, 7> trade{};        ///< +0x140 + 8b
    std::array<std::uint16_t, 7> tribute{};     ///< +0x40 + 2b
    std::array<std::uint16_t, 7> trade_mood{};  ///< +0x60 + 2b
    std::array<std::uint32_t, 7> damage{};      ///< +0x80 + 4b
    struct Event {
        std::uint8_t type = 0;  ///< 0 = empty
        std::uint8_t player = 0;
        std::uint32_t time = 0;  ///< 1/10 s
    };
    std::array<Event, 7> events{};   ///< +0x1C0 + 8i
    std::uint8_t blamed = kNoOwner;  ///< +0x08: last player who destroyed a Kontor or market of it
    /// +0x0C (runtime +0x7C): the AUFTRAG4 slot of the player's mission, 0xFF = none.
    std::uint8_t mission = 0xFF;
    /// +0x0A (runtime +0x9E): bad 5-s checks so far (bankrupt, or nothing left in a multiplayer game).
    std::uint16_t defeat_counter = 0;
    /// +0x1C (runtime +0x70): the switches a scenario sets for a computer (docs/re/ai.md), and
    /// +0x20, +0x22, +0x24, +0x25 (runtime +0xA0, +0xA2, +0x7F, +0x80) the thresholds of its
    /// attitude. The loader 0x00477911 takes them for human and computer slots only.
    std::uint32_t scenario_flags = 0;
    std::uint16_t rival_inhabitants = 0;
    std::uint16_t own_inhabitants = 0;
    std::uint8_t own_development = 0;
    std::uint8_t rival_development = 0;
};

/// One mission (AUFTRAG4 record, 0x8C4 bytes; loader 0x00477B70, writer 0x00478380,
/// docs/re/missions.md). The original keeps 4 slots; each player picks one (PlayerRecord::mission).
/// Values are as stored; the checks and the load-time steps of the original are the simulation's.
struct MissionRecord {
    static constexpr std::size_t kSize = 0x8C4;
    static constexpr std::size_t kTextSize = 0x800;
    /// A city: at least `size` Einwohner (inhabitants and workers) and `count` inhabitants of
    /// class `resident_class` or higher.
    struct CityGoal {
        std::int32_t size = 0;
        std::uint32_t resident_class = 0;
        std::uint32_t count = 0;
    };
    struct GoodsGoal {
        std::uint8_t ware = 0;
        std::uint16_t amount = 0;  ///< 1/32 t
    };
    std::uint32_t slot = 0;                         ///< +0x000
    std::uint32_t goals = 0;                        ///< +0x004: bit mask of goals
    std::uint32_t losses = 0;                       ///< +0x008: bit 0 protected city, bit 1 protected city count
    std::array<std::uint8_t, 2> island_resource{};  ///< +0x020, +0x022 (goals 0x4000, 0x8000)
    std::array<std::uint8_t, 2> island_count{};     ///< +0x021, +0x023 (recomputed at load)
    std::uint8_t target = kNoOwner;                 ///< +0x024: target or protected player, 7 = any other
    std::uint8_t min_defeated = 0;                  ///< +0x02B (goal 0x200)
    std::array<std::uint8_t, 7> defeat{};           ///< +0x02C: != 0 = must be defeated (goal 0x400)
    std::int32_t min_cities = 0;                    ///< +0x040 (goal 0x20)
    std::int32_t min_inhabitants = 0;               ///< +0x044 (goal 0x80)
    std::int32_t unused_48 = 0;                     ///< +0x048: loaded and saved, never read
    std::int32_t min_balance = 0;                   ///< +0x04C (goal 0x100)
    std::int32_t success_message = 0;               ///< +0x050: MELDUNG number
    std::int32_t min_trade_balance = 0;             ///< +0x054 (goal 0x10000)
    std::int32_t unused_58 = 0;                     ///< +0x058: loaded and saved, never read
    std::int16_t protected_min_cities = 0;          ///< +0x05C (loss bit 1)
    std::string text;                               ///< +0x068: Windows-1252, at most 0x7FF bytes
    std::array<GoodsGoal, 2> goods{};               ///< +0x868, +0x86C (goals 0x1000, 0x2000)
    std::array<CityGoal, 3> cities{};               ///< +0x870 (goals 0x1, 0x2, 0x4), as stored
    CityGoal protected_city;                        ///< +0x8A0 (loss bit 0)
    CityGoal every_city;                            ///< +0x8AC (goal 0x8)
    CityGoal target_city;                           ///< +0x8B8 (goal 0x10)
};

/// AUFTRAG4 chunk, in file order. Files without it yield an empty list. The older chunks
/// AUFTRAG and AUFTRAG2 are not read (no shipped file has them).
core::Result<std::vector<MissionRecord>> load_missions(const ChunkFile& file);

/// The record as the original writes it (0x00478380): fields it does not read are 0.
[[nodiscard]] std::vector<std::uint8_t> encode_mission(const MissionRecord& record);

/// Number of PLAYER4 records in the original files (4 players, free trader, pirates, natives).
/// Only the file format is limited to this; the engine is not (ADR 0007).
inline constexpr std::uint8_t kOriginalPlayerSlots = 7;

/// One ship (SHIP4 record, 0x1B4 bytes; written by 0x004600C0 from the ship table at 0x004CF358,
/// 150 x 0x218). Position = the display figure's floating point position, truncated.
struct ShipRecord {
    std::string name;
    std::uint16_t x = 0;  ///< world tile
    std::uint16_t y = 0;
    std::uint16_t figure = 0;  ///< FIGUR number in figuren.cod (HANDEL1 = 21, PIRAT = 31, ...)
    std::uint8_t owner = kNoOwner;
    std::uint8_t direction = 0;  ///< +0x50: byte 3 of the display figure, 0..7 in 45 degree steps
    /// +0x4D: island at the ship's position (0x0046A570, e.g. lying at a warehouse), 0xFF = open sea.
    std::uint8_t island = 0xFF;
    /// +0x174: eight cargo slots of 8 bytes: the good as the Id of the building that produces it
    /// (like KONTOR2) and the amount in 1/32 t. Empty slots have producer 0.
    struct Cargo {
        std::uint32_t producer = 0;  ///< haeuser.cod Id (20000 + stored value), 0 = empty
        std::uint16_t amount = 0;    ///< bits 0-13 of the second word
        std::uint16_t price = 0;     ///< +4 bits 0-9: average price paid per ton
    };
    std::array<Cargo, 8> cargo{};
    std::uint16_t slot = 0;         ///< +0x46: place in the ship table
    std::uint8_t kind = 0;          ///< +0x4A: 1 ship, 2 free trader, 3 pirate, 5 land cart
    std::uint16_t health = 0;       ///< +0x3C: in 1/32
    std::uint8_t cannons = 0;       ///< +0x42
    std::uint8_t flags = 0;         ///< +0x43: bit 1 route on, bit 3 one-way route, bit 2 for sale, bit 4 repair
    std::uint16_t sale_factor = 0;  ///< +0x44: sale price factor, 128 = 100 %
    /// The computer player's job of the ship (docs/re/ai.md): +0x24 mission (0 idle), +0x3E its
    /// parameter (island number * 32 + city, a patrol island or a cooldown), +0x40 its counter.
    std::uint16_t ai_mission = 0;
    std::uint16_t ai_param = 0;
    std::uint8_t ai_counter = 0;
    std::uint8_t trader = 0;      ///< +0x4C: HANDLER record of a free trader
    std::uint8_t route_stop = 0;  ///< +0x4F: current stop of the trade route
    /// Object references (byte 0 = type: 1-3 ship, 0x32-0x34 building, 0x35 city, 0x37 position).
    std::uint32_t order = 0;     ///< +0x2C
    std::uint32_t target = 0;    ///< +0x30
    std::uint32_t previous = 0;  ///< +0x34
    /// +0x52 + 0x24 * s: the trade route, four stops of a reference and three wares.
    struct RouteEntry {
        std::uint32_t producer = 0;  ///< the ware as its producer's Id, 0 = none
        std::uint16_t amount = 0;
        bool unload = false;
    };
    struct RouteStop {
        std::uint32_t ref = 0;
        std::array<RouteEntry, 3> entries{};
    };
    std::array<RouteStop, 4> route{};
};

/// HANDLER chunk: the goods of the free trader's ships (0x25C bytes each).
struct TraderRecord {
    std::uint16_t ship_slot = 0;  ///< SHIP4 +0x46 of its ship
    std::uint8_t index = 0;       ///< record number (SHIP4 +0x4C)
    struct Good {
        std::uint32_t producer = 0;  ///< the ware as its producer's Id
        std::uint16_t price = 0;     ///< average purchase price per ton
        std::uint8_t age = 0;        ///< visits without a change
        std::uint32_t amount = 0;    ///< 1/32 t
    };
    std::vector<Good> goods;
};

/// HANDLER chunk. Files without it yield an empty list.
core::Result<std::vector<TraderRecord>> load_traders(const ChunkFile& file);

/// PLAYER4 chunk. Files without it (older formats PLAYER2/PLAYER3) yield an empty list.
core::Result<std::vector<PlayerRecord>> load_players(const ChunkFile& file);

/// SHIP4 chunk. Files without it yield an empty list.
core::Result<std::vector<ShipRecord>> load_ships(const ChunkFile& file);

/// The world plan of a scenario that makes its islands at the start (SZENE chunk, 0x494 bytes; the
/// endless games). For each island its number, place, size class and climate; the template, which
/// islands get a native village, the crops and the ore and gold deposits are drawn when the game
/// starts (docs/re/worldgen.md).
struct ScenarioPlan {
    static constexpr std::uint16_t kRandomTemplate = 0xFFFF;
    static constexpr std::uint8_t kClimateByPosition = 2;
    struct Island {
        std::uint8_t climate = 0;  ///< 0 north, 1 south, 2 by position on the map
        std::uint8_t size = 0;     ///< 0 lit, 1 mit, 2 med, 3 big, 4 lar
        std::uint8_t flags = 0;    ///< bit 0: native village (set by the generator)
        std::uint8_t number = 0;   ///< island number
        std::uint16_t template_number = kRandomTemplate;
        std::uint32_t x = 0;  ///< world tile of the top left corner
        std::uint32_t y = 0;
    };
    /// An entry of the ore, crop or treasure list: a haeuser.cod building whose Ware names what
    /// is placed, how many times, and (ore) 0 big vein, 1 small vein.
    struct Resource {
        std::uint32_t building = 0;  ///< haeuser.cod Id
        std::uint16_t count = 0;
        std::uint32_t type = 0;
    };
    std::array<std::uint32_t, 2> natives{};  ///< native villages to place in the north, the south
    std::uint32_t max_crops = 0;             ///< crops per island in the main passes, 0 = 2
    std::uint32_t gold_min_size = 0;         ///< size classes for gold in the first passes
    std::uint32_t gold_max_size = 0;
    std::array<Resource, 4> ores{};
    std::array<Resource, 12> crops{};
    std::array<Resource, 4> treasures{};
    std::vector<Island> islands;
};

/// The SZENE chunk, nothing if the file has none.
[[nodiscard]] core::Result<std::optional<ScenarioPlan>> load_scenario_plan(const ChunkFile& file);

/// RANDTAB: the 512 random numbers the world plan draws from (a saved rand() table), nothing if
/// the file has none. A shorter chunk repeats, as the original copies it.
[[nodiscard]] std::optional<std::array<std::uint16_t, 512>> load_random_table(const ChunkFile& file);

/// SZENE_MISSNR: 1 marks the endless game of the single player menu, 0 that of the multiplayer
/// menu; nothing if the file has none.
[[nodiscard]] std::optional<std::uint32_t> load_mission_number(const ChunkFile& file);

/// The head of a scenario as the menu's list reads it (0x0048D7A0: the SZENE_* chunks before the
/// rest), with the original's defaults for chunks a file does not have.
struct ScenarioHeader {
    static constexpr std::uint32_t kNone = 0xFF;
    std::uint32_t mission_number = kNone;  ///< SZENE_MISSNR: 0 tutorial, 1 endless, 2.. numbered missions
    std::uint32_t campaign = kNone;        ///< SZENE_KAMPAGNE
    std::uint32_t game_id = 0;             ///< SZENE_GAMEID
    std::uint32_t ranking = 2;             ///< SZENE_RANKING: stars
    std::uint32_t player_min = 1;          ///< SZENE_PLAYERMIN
    std::uint32_t player_max = 1;          ///< SZENE_PLAYERMAX
};
[[nodiscard]] ScenarioHeader load_scenario_header(const ChunkFile& file);

/// One line of a high score list (Szenes/<name>.hss: "Objekt: HISCORE", "Entry: i, \"name\",
/// points, stars, game id").
struct HighScore {
    std::string name;  ///< Windows-1252, empty for an unused line
    std::int64_t points = 0;
    std::uint32_t stars = 0;
    std::uint32_t game_id = 0;
};
/// The lines in the order of their index (12 in the original's files).
[[nodiscard]] std::vector<HighScore> parse_high_scores(std::string_view text);

/// A savegame slot as listed in Game.dat ("Objekt: SPIELNAME", "Name: slot, \"text\", flag").
/// Slot n is stored as SAVEGAME/gameNN.gam.
struct SavegameSlot {
    std::uint32_t slot = 0;
    std::string name;  ///< Windows-1252
    std::string file;  ///< relative path, e.g. "Savegame/game00.gam"
};

/// Savegame names from the text file Game.dat (settings and savegame names of the original).
/// Unused slots carry names like "Leer01" and are returned as well.
std::vector<SavegameSlot> parse_savegame_slots(std::string_view game_dat);

}  // namespace o1602::assets
