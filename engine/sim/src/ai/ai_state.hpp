// SPDX-License-Identifier: GPL-3.0-or-later
//
// State of the computer players (docs/re/ai.md). The original keeps one context of 0x11E88 bytes per
// player slot (0x006337C0) and rebuilds it from the world after every load (0x0040F580); Open1602
// keeps the same fields, saves them with the game and rebuilds them only for games of the original.
// Field comments name the original's offset in the context (C+...) or the per-city record (R+...).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "o1602/sim/player.hpp"

namespace o1602::sim::ai {

/// Version of the saved state of the computer players. 2 dropped what nothing read (GB-5): the
/// attack plans and the islands' state byte. 3 added the deferred yard request (GB-2). 4 added the
/// target of a search (Search::target_x/target_y, task kClaimLand). Every version from
/// kFirstStateVersion on loads.
inline constexpr std::uint32_t kStateVersion = 4;
inline constexpr std::uint32_t kFirstStateVersion = 1;

/// Task ids of the original's dispatcher (AI_RunTask 0x00429AA0). The numbers are kept so the
/// documentation and the analysis of the original can be compared directly.
enum Task : std::uint8_t {
    kRoot = 0x00,  ///< empty stack: city round robin, colonisation, giving up
    kSettleArea = 0x02,
    kSettleSite = 0x03,
    kSettleLand = 0x04,
    kDevelop = 0x05,
    kRoads = 0x06,
    kLayout = 0x07,
    kSurvey = 0x08,
    kWoodcutter = 0x09,
    kHunter = 0x0A,
    kBakery = 0x0B,
    kMill = 0x0C,
    kGrainFarm = 0x0D,
    kHouses = 0x0E,
    kKapelle = 0x0F,
    kQuarry = 0x10,
    kStonemason = 0x11,
    kCotton = 0x12,
    kWeaving = 0x13,
    kWine = 0x14,
    kWirt = 0x15,
    kMarket = 0x16,
    kSpices = 0x17,
    kSchule = 0x18,
    kCocoa = 0x19,
    kCattle = 0x1A,
    kButcher = 0x1B,
    kTobacco = 0x1C,
    kTobaccoGoods = 0x1D,
    kIronMine = 0x1E,
    kSmelter = 0x1F,
    kToolsmith = 0x20,
    kShipyard = 0x21,
    kKontorUpgrade = 0x22,
    kPaving = 0x23,
    kTailor = 0x24,
    kSheep = 0x25,
    kSugar = 0x26,
    kRum = 0x27,
    kFisher = 0x28,
    kKlinik = 0x29,
    kKirche = 0x2A,
    kBadehaus = 0x2B,
    kTowers = 0x2C,
    kAbandon = 0x2E,
    kSwords = 0x2F,
    kMuskets = 0x30,
    kCannonFoundry = 0x31,
    kCastle = 0x32,
    kPirateHouses = 0x33,
    kGoldMine = 0x34,
    kHochschule = 0x35,
    kTheater = 0x36,
    kGoldsmith = 0x37,
    kGalgen = 0x38,
    kBrunnen = 0x3A,
    kShipyardUpgrade = 0x3B,
    kDenkmal = 0x3C,
    /// Open1602: a market that brings a deposit the city wants into its land (Engine::claim_land).
    kClaimLand = 0x3D,
};

/// Building slots of a city record (R+0x34 + 6k): what the city has of each kind of building.
enum Slot : std::uint8_t {
    kSlotKontor = 0x00,  ///< the Kontor's level 1..4, not a count
    kSlotWoodcutter = 0x01,
    kSlotHunter = 0x02,
    kSlotGrain = 0x03,
    kSlotMill = 0x04,
    kSlotBakery = 0x05,
    kSlotQuarry = 0x06,
    kSlotStonemason = 0x07,
    kSlotMarket = 0x08,
    kSlotHouses = 0x09,
    kSlotKapelle = 0x0A,
    kSlotCotton = 0x0B,
    kSlotWebstube = 0x0C,
    kSlotWine = 0x0D,
    kSlotWirt = 0x0E,
    kSlotSpices = 0x0F,
    kSlotSchule = 0x10,
    kSlotCocoa = 0x11,
    kSlotCattle = 0x12,
    kSlotButcher = 0x13,
    kSlotTobacco = 0x14,
    kSlotTobaccoGoods = 0x15,
    kSlotIronMine = 0x16,
    kSlotSmelter = 0x17,
    kSlotToolsmith = 0x18,
    kSlotShipyard = 0x19,  ///< 1 small yard, 2 big yard
    kSlotStoneRoads = 0x1A,
    kSlotTailor = 0x1B,
    kSlotSheep = 0x1C,
    kSlotSugar = 0x1D,
    kSlotRum = 0x1E,
    kSlotFisher = 0x1F,
    kSlotFood = 0x20,     ///< every food producer
    kSlotAlcohol = 0x21,  ///< every alcohol producer
    kSlotKlinik = 0x22,
    kSlotWeberei = 0x23,
    kSlotRumBuilt = 0x24,  ///< the original's second rum counter (S-098); Open1602 counts slot 0x1E
    kSlotKirche = 0x25,
    kSlotBadehaus = 0x26,
    kSlotWoodTower = 0x27,
    kSlotStoneTower = 0x28,
    kSlotPirateTower = 0x29,
    kSlotSwords = 0x2B,
    kSlotMuskets = 0x2C,
    kSlotCannons = 0x2D,
    kSlotCastle = 0x2E,
    kSlotPirateHouses = 0x2F,
    kSlotGoldMine = 0x30,
    kSlotHochschule = 0x31,
    kSlotTheater = 0x32,
    kSlotGalgen = 0x33,
    kSlotGoldsmith = 0x34,
    kSlotBrunnen = 0x35,
    kSlotDenkmal = 0x36,
};
inline constexpr std::size_t kSlots = 0x37;

/// Flags of a city record (R+0x30).
namespace city_flag {
inline constexpr std::uint32_t kAbandon = 0x0001;   ///< give the city up (task 0x2E)
inline constexpr std::uint32_t kSurveyed = 0x0002;  ///< task 8 ran this visit: the sweep may work
inline constexpr std::uint32_t kKlinik = 0x0004;    ///< "covered" bits of the public buildings
inline constexpr std::uint32_t kKapelle = 0x0008;
inline constexpr std::uint32_t kMarket = 0x0010;
inline constexpr std::uint32_t kWirt = 0x0020;
inline constexpr std::uint32_t kSchule = 0x0040;
inline constexpr std::uint32_t kKirche = 0x0080;
inline constexpr std::uint32_t kBadehaus = 0x0100;
inline constexpr std::uint32_t kHochschule = 0x0200;
inline constexpr std::uint32_t kTheater = 0x0400;
inline constexpr std::uint32_t kGalgen = 0x0800;
inline constexpr std::uint32_t kBrunnen = 0x1000;
}  // namespace city_flag

/// Development stages of a city (R+0x10; switch of task 5, 0x00428E70).
namespace stage {
inline constexpr std::uint8_t kFounded = 0x00;
inline constexpr std::uint8_t kColony = 0x01;  ///< a city that grows raw materials (terminal)
inline constexpr std::uint8_t kPioneers = 0x14;
inline constexpr std::uint8_t kSettlers = 0x15;
inline constexpr std::uint8_t kSecond = 0x16;  ///< a smaller city next to the main one
inline constexpr std::uint8_t kSecondLuxury = 0x17;
inline constexpr std::uint8_t kSecondFarming = 0x18;
inline constexpr std::uint8_t kMain = 0x1E;
inline constexpr std::uint8_t kMain2 = 0x1F;
inline constexpr std::uint8_t kCitizens = 0x20;
inline constexpr std::uint8_t kCitizens2 = 0x21;
inline constexpr std::uint8_t kCitizensSmall = 0x22;
inline constexpr std::uint8_t kMerchants = 0x28;
inline constexpr std::uint8_t kMerchants2 = 0x29;
inline constexpr std::uint8_t kMerchantsSmall = 0x2C;
inline constexpr std::uint8_t kAristocrats = 0x32;
inline constexpr std::uint8_t kAristocrats2 = 0x33;
}  // namespace stage

struct SlotState {
    std::uint16_t count = 0;
    std::uint16_t attempts = 0;
    std::uint16_t fail_level = 0;
};

/// A market of the city that soldiers guard (R+0x188).
struct GuardPost {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

/// What the computer knows and wants of one of its cities (R = C+0x3E80 + 0x298 * city).
struct CityPlan {
    std::uint16_t island = 0;   ///< index into World::islands()
    std::uint8_t city = 0;      ///< city number on the island
    std::uint16_t sweep_x = 0;  ///< R+0x00/+0x04: cursor of the maintenance sweep
    std::uint16_t sweep_y = 0;
    std::uint16_t tower_ring = 0;    ///< R+0x08: ring around the Kontor the towers search
    std::uint16_t paving_ring = 0;   ///< R+0x0C: ring around the Kontor the paving searches
    std::uint8_t stage = 0;          ///< R+0x10
    std::uint64_t stage_ms = 0;      ///< R+0x14: game time the stage began
    std::uint32_t land = 0;          ///< R+0x1C: usable land of the city, tiles
    std::uint32_t land_percent = 0;  ///< R+0x18
    std::uint32_t flags = 0;         ///< R+0x30, city_flag
    std::array<SlotState, kSlots> slots{};
    std::optional<std::pair<std::uint16_t, std::uint16_t>> doctor;  ///< R+0x28/+0x2C: tile of the Klinik
    std::vector<GuardPost> posts;                                   ///< R+0x188: the city's markets
    std::uint8_t post_cursor = 0;                                   ///< R+0x184
};

/// A straight run of road of the current city (C+0x1DF4; task 7, 0x00417C80).
struct Segment {
    std::int32_t x0 = 0;  ///< first coordinate (the walking end of the cursor)
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;  ///< second coordinate
    std::int32_t y1 = 0;
    std::int32_t dx = 0;  ///< unit step of the run
    std::int32_t dy = 0;
    std::uint8_t dir = 0;     ///< 0 +x, 1 +y, 2 -x, 3 -y
    std::int32_t length = 0;  ///< tiles - 1
    bool open = false;        ///< the run ends at a tile that is no road (a dead end)
};

/// The one building site search of a player (C+0x1D14..+0x1DA7; 0x004159F0).
struct Search {
    std::int32_t segment = 0;  ///< +0x1D14
    std::int32_t x = 0;        ///< +0x1D18: cursor on the road
    std::int32_t y = 0;
    std::int32_t remaining = 0;  ///< +0x1D20
    std::int32_t ref_x = 0;      ///< +0x1D24: reference point for the parity
    std::int32_t ref_y = 0;
    std::uint8_t radius = 0;     ///< +0x1D2C
    std::uint32_t building = 0;  ///< +0x1D30: Id of the building to place
    std::uint32_t service = 0;   ///< +0x1D48: public buildings, the service bit they bring
    std::uint8_t slot = 0;       ///< +0x1D4C
    std::int32_t phase = 0;      ///< +0x1D50
    std::int32_t best_x0 = 0;    ///< +0x1D54..+0x1D60
    std::int32_t best_y0 = 0;
    std::int32_t best_x1 = 0;
    std::int32_t best_y1 = 0;
    std::int32_t best = 0;      ///< +0x1D64: best score, 0 = nothing found
    std::int32_t extra = 0;     ///< +0x1D68
    std::int32_t rotation = 0;  ///< +0x1D6C
    std::int32_t stub_x = -1;   ///< +0x1D70/+0x1D74: road tile to connect a farm, -1 none
    std::int32_t stub_y = -1;
    /// Open1602, task kClaimLand: the tile the new market should bring into the city's land, -1 none.
    std::int32_t target_x = -1;
    std::int32_t target_y = -1;
};

/// Money and building material taken from a city while a building is planned (C+0x1D34..+0x1D44).
/// The original takes them when it plans and gives them back right before the real build; Open1602
/// also gives them back when the plan is dropped.
struct Reservation {
    std::int64_t money = 0;
    std::uint32_t wood = 0;
    std::uint32_t bricks = 0;
    std::uint32_t tools = 0;
    std::uint32_t cannons = 0;
    std::uint16_t island = 0;  ///< the city the goods came from
    std::uint8_t city = 0;
    [[nodiscard]] bool empty() const noexcept {
        return money == 0 && wood == 0 && bricks == 0 && tools == 0 && cannons == 0;
    }
};

/// A connected area of free land found by the settlement scan (0x00415AF0, C+0x510 + 0x18 k).
struct Region {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;
    std::int32_t area = 0;
};

/// Founding a city (C+0x28, +0x2C, +0x30, +0x4E4, +0x1DA8..+0x1DD4).
struct Settlement {
    bool active = false;             ///< +0x2C: looking for an island
    bool failed = false;             ///< +0x28: no island found
    std::uint64_t last_ms = 0;       ///< +0x30: last settlement event
    std::int32_t need = 1800;        ///< +0x4E4: free land the island must have, tiles
    std::int32_t floor = 300;        ///< +0x4E8
    std::uint32_t ship = 0;          ///< +0x1DAC: Ship::id, 0 = none
    std::uint16_t cursor = 0;        ///< +0x1DC8: island cursor
    std::uint32_t pass = 0;          ///< pirates: size class allowed this pass
    std::uint8_t crop = 0;           ///< +0x1DCC: crop (ware 45..51 or 2 for ore) the island must grow
    std::uint8_t previous_crop = 0;  ///< +0x1DD0
    std::int32_t island = -1;        ///< +0x1DD4/+0x1DB0: island under test
    std::int32_t site_x = 0;         ///< +0x1DB4..+0x1DBC: where the Kontor goes
    std::int32_t site_y = 0;
    std::uint8_t site_rotation = 0;
    std::uint8_t retries = 0;     ///< +0x1DA8
    bool sailing = false;         ///< the ship sails to the site
    std::vector<Region> regions;  ///< result of the area scan
};

/// A castle the computer recruits in (C+0x440..+0x49C).
struct CastleEntry {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t rally_x = 0;  ///< the Kontor of the city: released units walk there
    std::uint16_t rally_y = 0;
};

/// An attack being planned against one owner (C+0x107C0 + 0x288 * owner). Open1602 never planned
/// one; state version 1 kept an empty list of them per computer, which a loader reads and drops.
struct AttackPlan {
    bool active = false;             ///< +0x268
    std::uint16_t island = 0;        ///< +0x00
    std::uint8_t city = 0;           ///< +0x04
    std::uint32_t ship = 0;          ///< +0x08
    std::vector<GuardPost> targets;  ///< +0x158: markets or the Kontor of the target city
    std::uint8_t phase = 0;          ///< +0x26C
    std::int32_t cursor = 0;         ///< +0x270
    struct Candidate {
        std::int32_t x = 0;  ///< world tile
        std::int32_t y = 0;
        std::uint8_t flag = 0;
    };
    std::vector<Candidate> candidates;  ///< +0x18, at most 16
    std::int32_t best_x = 0;            ///< +0x274
    std::int32_t best_y = 0;
    std::int32_t best_score = 10000;  ///< +0x280
    std::uint8_t best_flag = 0;       ///< +0x284
};

/// What a computer player has its ship doing (the original keeps it in the ship: +0x1AE mission,
/// +0x1AC parameter or cooldown, +0x1A7 counter; SHIP4 +0x24, +0x3E, +0x40).
struct ShipNote {
    std::uint32_t id = 0;       ///< Ship::id
    std::uint8_t mission = 0;   ///< 0 idle, 1 fetch troops, 2 go home, 3 shell an outpost, 5..9 attack
    std::uint32_t param = 0;    ///< mission parameter (target city, owner)
    std::uint8_t counter = 0;   ///< tries left, or patrol stops left
    std::uint8_t cooldown = 0;  ///< logistics passes before the next job
    bool patrol = false;        ///< sails a patrol around `patrol_island`
    std::uint16_t patrol_island = 0;
    std::uint8_t last_stop = 0;  ///< route stop at the last look (patrol stops are counted)
    bool supply = false;         ///< on a supply run home: its goods are for the home city
};

/// Island clock of the soldier controller (0x006B1180 + 0x5A0 * island). State version 1 also had a
/// state byte that nothing read.
struct IslandWatch {
    std::uint64_t next_ms = 0;
};

/// One computer player (or the pirates).
struct AiPlayer {
    PlayerId player = kNoPlayer;
    bool pirates = false;
    std::int32_t t_main = 0;          ///< +0x04: game ms to the next step
    std::int32_t t_10s = 20000;       ///< +0x0C: logistics
    std::int32_t t_6s = 3000;         ///< +0x14: recruiting; pirates: their own clock
    std::uint64_t init_ms = 0;        ///< +0x08: game time of the last (re)initialisation
    std::uint64_t refresh_ms = 0;     ///< +0x10: game time of the last refresh of task 5
    std::uint32_t cities = 0;         ///< +0x18: own cities (at init, +1 founding, -1 abandoning)
    std::uint32_t surveyed = 0;       ///< +0x20: cities surveyed by task 8 since the init
    std::vector<std::uint8_t> stack;  ///< +0x24/+0x34: task ids, at most 256, no duplicates
    std::int32_t budget = 0;          ///< +0x3E78: pushes left in this city visit
    Settlement settle;
    bool colony_failed = false;  ///< the island search failed since the init (S-097)
    // The current city (+0x1DDC/+0x1DE0/+0x1DE8/+0x10638).
    bool has_city = false;  ///< +0x1DE8 != 0
    std::uint16_t island = 0;
    std::uint8_t city = 0;
    bool has_plan = false;          ///< +0x10638 != 0: some city was visited since the init
    std::uint32_t city_cursor = 0;  ///< +0x1DE4: round robin over the world's cities
    bool home_valid = false;        ///< +0x3E7C
    std::uint16_t home_island = 0;
    std::uint8_t home_city = 0;
    bool skip_layout = false;            ///< +0x3E74
    std::uint8_t max_stage = 0;          ///< +0x10640: highest stage any city reached
    std::uint8_t score = 0;              ///< +0x1063C: development score 0..4
    std::array<bool, kSlots> present{};  ///< +0x10644: the player has a building of slot k
    std::vector<CityPlan> plans;         ///< the city records
    // Building
    Search search;
    std::vector<Segment> segments;  ///< +0x1DF0/+0x1DF4
    Reservation reserved;
    // Own shipyard (+0x434..+0x43C)
    bool yard_valid = false;
    bool yard_wanted = false;  ///< Open1602 (GB-2): the 10-s logistics asked for a yard (state 3)
    std::uint16_t yard_island = 0;
    std::uint16_t yard_x = 0;
    std::uint16_t yard_y = 0;
    // Military (+0x440..+0x4A0)
    std::optional<std::uint16_t> castle_island;
    std::vector<CastleEntry> castles;
    std::uint8_t castle_cursor = 0;           ///< +0x44C
    std::uint32_t recruit_cursor = 0;         ///< +0x4A0
    std::uint32_t target_cursor = 0;          ///< +0x1DD8
    std::vector<std::int32_t> previous_mood;  ///< +0x4A4: by player
    std::uint32_t diplomacy_ms = 0;           ///< game time since the last diplomacy pass
    std::vector<std::uint8_t> seen_treaties;  ///< peace and trade towards each player at the last look
    bool diplomacy_due = false;               ///< a treaty changed: the next tick looks again
    // Ships
    std::vector<ShipNote> ships;
    /// Not saved: where Engine::note() last found a ship in `ships`, checked before it is used
    /// (`ships` may have been reordered or shortened since). Only looked up, never walked.
    std::unordered_map<std::uint32_t, std::uint32_t> ship_slots;
    std::int32_t fleet_target = 0;    ///< +0x4DC; pirates: the last new ship is armed
    std::int32_t ships_per_city = 1;  ///< +0x4E0
};

/// Everything of all computer players.
struct State {
    std::vector<AiPlayer> players;     ///< one per computer or pirate player
    std::uint8_t max_score = 0;        ///< 0x006C2EEC: highest development score of all
    std::vector<IslandWatch> islands;  ///< by island
    std::uint32_t island_cursor = 0;   ///< 0x006C2EF0
    bool initialised = false;
};

}  // namespace o1602::sim::ai
