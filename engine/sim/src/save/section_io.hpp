// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sections one by one (docs/formats/o1602-save.md): what reading them gives, and a writer and a
// reader per section. Both are made from the same description of each record (archive.hpp) in the
// section's file (map.cpp, world.cpp, islands.cpp, players.cpp, cities.cpp, ships.cpp, army.cpp,
// nature.cpp, orders.cpp). sections.cpp puts them together; apply.cpp checks what was read against
// the world and applies it. Internal to sim.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "archive.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim::save {

/// Everything decoded, applied only when all of it was read without error.
struct Decoded {
    std::optional<Rules> rules;
    struct WorldPart {
        std::uint64_t time_ms = 0;
        std::uint32_t city_name_index = 0;
        World::GameSettings settings;
        std::uint64_t next_drought_ms = 0;
        std::optional<std::uint8_t> drought_seed;
        std::vector<Random> randoms;
        HouseClocks house_clocks;
        std::array<std::uint32_t, GrowthState::kClasses> growth_ms{};
        std::array<std::uint8_t, GrowthState::kClasses> growth_counter{};
        std::uint32_t next_disaster_id = 0;
    };
    std::optional<WorldPart> world;
    std::optional<std::uint32_t> pirate_interval_s;  ///< WRL2
    bool tutorial = false;                           ///< TUTR
    struct IslandPart {
        std::uint16_t number = 0;
        std::uint8_t deer_phase = 0;
        std::array<DeerSpot, 8> deer_spots{};
        WorldIsland::Drought drought{};
        std::vector<Crater> craters;
        bool volcano_erupted = false;
        std::vector<assets::IslandHeader::Deposit> deposits;
        std::uint32_t fertility = 0;
    };
    std::optional<std::vector<IslandPart>> islands;
    /// Explored crops and known deposits for every player (INSEL5 keeps seven slots and 8 bits).
    struct ExplorePart {
        std::vector<bool> explored;
        std::vector<std::uint32_t> deposit_masks;
    };
    std::optional<std::vector<ExplorePart>> explored;
    struct TileOverride {
        std::uint16_t island = 0;  ///< island number
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        bool ground = false;
        MapCell cell;
    };
    std::optional<std::vector<TileOverride>> tiles;
    std::optional<std::vector<Player>> players;
    /// The world's factions and each player's (FACT), in the order of PLAY; without, the base
    /// game's and the factions PLAY's role bytes give.
    struct FactionPart {
        std::vector<Faction> factions;
        std::vector<FactionId> players;
    };
    std::optional<FactionPart> factions;
    std::optional<Balance> balance;
    /// The content packages the game uses (PACK).
    std::optional<std::vector<assets::ContentPackage>> packages;
    /// The state of the packages' scripts (SCRP).
    std::optional<std::vector<ScriptState>> script_states;
    /// The groups the players stored (UGRP).
    std::optional<std::vector<UnitGroups>> unit_groups;
    /// The goods the packages added (GOOD).
    std::optional<assets::Goods> goods;
    /// The icons of packages the goods show, by ware number (GPIC).
    std::optional<std::vector<std::pair<std::uint8_t, std::string>>> good_pictures;
    /// The classes of inhabitants of the game (CLAS).
    std::optional<std::vector<assets::ResidentClass>> classes;
    /// The unlock levels of the game (LEVL).
    std::optional<std::vector<assets::InfraLevel>> levels;
    /// The levels after the original's a player unlocked (UNLK).
    struct UnlockedMore {
        std::uint8_t player = 0;
        std::array<std::uint32_t, 7> levels{};
    };
    std::optional<std::vector<UnlockedMore>> unlocked_more;
    /// The buildings the packages added and their Ids (OWNB).
    std::optional<std::vector<assets::OwnBuilding>> own_buildings;
    /// The changed values of buildings (BLDV).
    std::optional<std::vector<BuildingValue>> building_values;
    /// The changed values of figures (FIGV).
    std::optional<std::vector<FigureValue>> figure_values;
    /// Missions and the end of the game (MISN).
    struct MissionPart {
        std::vector<Mission> missions;
        std::optional<World::GameEnd> end;
    };
    std::optional<MissionPart> missions;
    /// What players have of the missions and the defeat rules (PSTA), in the order of PLAY.
    std::optional<std::vector<Player>> player_state;
    /// PSTA had the scenario's switches (kScenarioSwitches); else those of the original's chunks stay.
    bool player_switches = false;
    std::optional<std::vector<City>> cities;
    /// Trade statistics of the cities (CSTA): island, index, purchases, sales.
    struct CityStats {
        std::uint16_t island = 0;
        std::uint8_t index = 0;
        std::int64_t purchases = 0;
        std::int64_t sales = 0;
    };
    std::optional<std::vector<CityStats>> city_state;
    /// The clocks of the material locks (MLCK): island, index, City::material_reminder_ms.
    struct MaterialClock {
        std::uint16_t island = 0;
        std::uint8_t index = 0;
        std::uint64_t material_reminder_ms = 0;
    };
    std::optional<std::vector<MaterialClock>> material_clocks;
    /// The population messages (CPOP): island, index, City::population_message.
    struct PopulationMark {
        std::uint16_t island = 0;
        std::uint8_t index = 0;
        std::uint8_t population_message = 0;
    };
    std::optional<std::vector<PopulationMark>> population_marks;
    /// The ships that gave up on a Kontor (CGUP): island, index, City::ships_gave_up.
    struct GiveUpCount {
        std::uint16_t island = 0;
        std::uint8_t index = 0;
        std::uint8_t ships_gave_up = 0;
    };
    std::optional<std::vector<GiveUpCount>> give_up_counts;
    /// The goods statistics of the cities (GLDG): island, index, City::ledger and City::history.
    struct CityLedger {
        std::uint16_t island = 0;
        std::uint8_t index = 0;
        GoodsLedger ledger;
        CityHistory history;
    };
    std::optional<std::vector<CityLedger>> ledgers;
    /// The score's counters (SCOR), in the order of PLAY, and the cities' monument clocks.
    struct ScorePart {
        struct Counters {
            std::uint32_t soldiers_killed = 0;
            std::uint32_t soldiers_lost = 0;
            std::uint32_t ships_sunk = 0;
            std::uint32_t ships_lost = 0;
        };
        struct CityClock {
            std::uint16_t island = 0;
            std::uint8_t index = 0;
            std::int32_t house_growth = 0;
            std::uint64_t growing_since_ms = 0;
        };
        std::vector<Counters> players;
        std::vector<CityClock> cities;
    };
    std::optional<ScorePart> scores;
    struct ProductionPart {
        std::vector<Production> list;
        std::vector<std::uint32_t> free;  ///< free slots of the list
    };
    std::optional<ProductionPart> productions;
    std::optional<std::vector<House>> houses;
    std::optional<std::vector<GrowthEntry>> growth;
    std::optional<std::vector<CostBuilding>> costs;
    std::optional<std::vector<Ship>> ships;
    std::optional<std::vector<Soldier>> soldiers;
    std::optional<std::vector<Animal>> animals;
    std::optional<std::vector<Disaster>> disasters;
    std::optional<VolcanoState> volcanoes;
    std::optional<std::vector<std::uint8_t>> simulation;
    /// Ships with their courses and routes, shipyards, Kontor trade settings, discovered islands.
    struct FleetPart {
        std::uint32_t next_ship_id = 1;
        std::vector<std::pair<std::uint16_t, std::vector<bool>>> discovered;  ///< island number, players
        struct CityTrade {
            std::uint16_t island = 0;
            std::uint8_t index = 0;
            std::vector<City::Trade> trade;
            std::vector<std::uint32_t> goodwill;
            bool trader_inbound = false;
        };
        std::vector<CityTrade> cities;
        std::vector<CostBuilding> yards;  ///< the shipyard fields, in the order of the costs list
        std::vector<Ship> ships;
    };
    std::optional<FleetPart> fleet;
    /// Soldiers with their paths, towers, building damage, shots in flight, castle slots.
    struct ArmyPart {
        std::uint32_t next_soldier_id = 1;
        std::vector<Soldier> soldiers;
        std::vector<Tower> towers;
        std::vector<BuildingDamage> damage;
        std::vector<PendingHit> hits;
        std::vector<CostBuilding> castles;  ///< weapons and slots, in the order of the costs list
        struct ShipPart {
            std::uint32_t id = 0;
            bool sinking = false;
            std::uint64_t next_shot_ms = 0;
        };
        std::vector<ShipPart> ships;
        struct CityPart {
            std::uint16_t island = 0;
            std::uint8_t index = 0;
            std::uint64_t last_attacked_ms = 0;
            bool cursed = false;
        };
        std::vector<CityPart> cities;
    };
    std::optional<ArmyPart> army;
    std::optional<Diplomacy> diplomacy;
    /// Phase of the island epochs and the phase each island was last ticked in (island number).
    struct EpochPart {
        std::uint8_t phase = 0;
        std::vector<std::pair<std::uint16_t, std::uint8_t>> islands;
    };
    std::optional<EpochPart> epochs;
    /// Orders waiting behind the current one (QUEU) and stances (STNC), by ship or unit id.
    using Queues = std::vector<std::pair<std::uint32_t, std::vector<Target>>>;
    using Stances = std::vector<std::pair<std::uint32_t, std::uint8_t>>;
    Queues ship_queues;
    Queues soldier_queues;
    Stances ship_stances;
    Stances soldier_stances;
    core::Bytes origin;  ///< ORIG
};

// ---- Records more than one section has ------------------------------------------------------------

/// An object reference (Target) in 15 bytes.
template <class A, class T>
void io_target(A& a, T& t) {
    a.enum8(t.kind, Target::Kind::Spot);
    a.u16(t.island);
    a.u8(t.city);
    a.u32(t.x);
    a.u32(t.y);
    a.u32(t.ship);
    a.u32(t.raw);
}

/// Version 2 of ISLE and VOLC: the island coordinates of deer spots and craters are 16 bit instead
/// of 8 (islands wider or higher than 255 tiles, SC-13). The writer takes it only when a coordinate
/// needs it, so a world that fits 8 bits writes the bytes of version 1 as before.
inline constexpr std::uint16_t kWideCoordinates = 2;

/// Version 2 of PSTA: each player also has the switches of its scenario (Player::scenario). The writer
/// takes it only when a player has some, so other games write the bytes of version 1 as before.
inline constexpr std::uint16_t kScenarioSwitches = 2;
/// Version 3 of PSTA: each player also has whether his gates are closed (Player::gates_closed). The
/// writer takes it only when a player closed them.
inline constexpr std::uint16_t kClosedGates = 3;
/// FLEE version 2: every ship's route with TradeRoute::kStops stops, not the original's four.
inline constexpr std::uint16_t kLongRoutes = 2;
/// FLEE version 3: every stop with RouteStop::kEntries wares, not the original's three.
inline constexpr std::uint16_t kRouteWares = 3;
/// SCOR version 2: the fight counters of the players are 32 bit, not 16 (LC-9). The writer takes it
/// only when a counter needs it, so other games write the bytes of version 1 as before.
inline constexpr std::uint16_t kWideScores = 2;
/// SIMU version 2: the state of the running game in parts with their own versions (SIMC, LOGI,
/// FLET, MILI, AIST; Simulation::save_state, DS-3). Version 1 held them in one block.
inline constexpr std::uint16_t kSectionedSimulation = 2;
/// MAPW version 2: the island header's number, width and height as u16, and every column of a layer
/// as runs where that is shorter (DS-4, LC-11). Always written; version 1 is read.
inline constexpr std::uint16_t kCompactMap = 2;
/// Version 3 of ISLE, version 2 of EPOC, version 4 of FLEE: island numbers as u16 (worlds of more
/// than 256 islands, LC-2), and in ISLE the deposits' coordinates too (LC-10). The writer takes them
/// only when a number or a coordinate needs it.
inline constexpr std::uint16_t kWideIslands = 3;
inline constexpr std::uint16_t kWideEpochs = 2;
inline constexpr std::uint16_t kWideDiscovered = 4;

/// An island coordinate of ISLE and VOLC: 8 bit up to version 1, 16 bit from kWideCoordinates.
template <class A, class T>
void io_coordinate(A& a, T& v, std::uint16_t version) {
    if (version >= kWideCoordinates) {
        a.u16(v);
    } else {
        a.u8(v);
    }
}

constexpr bool wide(std::uint16_t x, std::uint16_t y) noexcept {
    return x > 0xFF || y > 0xFF;
}

// ---- The sections ----------------------------------------------------------------------------------

// map.cpp: MAPW (own savegame only), TILE (chunk in a .GAM only), the buildings' fingerprint (META).
void write_map(Writer& out, const World& w, const assets::BuildingTable& buildings);
/// MAPW in the fingerprint of the game: the tiles as hashes of their blocks, kept in `hashes` (per
/// island of the world) and taken again only where a block changed.
void write_map_fingerprint(Writer& out, const World& w, const assets::BuildingTable& buildings,
                           std::vector<std::vector<BlockHash>>& hashes);
[[nodiscard]] core::Result<World> read_map(Reader& in, const assets::BuildingTable& buildings, std::uint16_t version);
void write_tiles(Writer& out, const World& w, const assets::BuildingTable& buildings,
                 std::span<const std::uint8_t> before);
[[nodiscard]] std::vector<Decoded::TileOverride> read_tiles(Reader& in, const assets::BuildingTable& buildings);
[[nodiscard]] std::uint64_t buildings_fingerprint(const assets::BuildingTable& buildings);

// world.cpp: RULE, WRLD, EPOC.
void write_rules(Writer& out, const Rules& r);
[[nodiscard]] Rules read_rules(Reader& in);
void write_world(Writer& out, const World& w);
[[nodiscard]] Decoded::WorldPart read_world(Reader& in);
/// The version of EPOC: kWideEpochs if an island's number lies beyond 8 bits.
[[nodiscard]] std::uint16_t epochs_version(const World& w);
void write_epochs(Writer& out, const World& w, std::uint16_t version);
[[nodiscard]] Decoded::EpochPart read_epochs(Reader& in, std::uint16_t version);

// islands.cpp: ISLE, XPLR.
[[nodiscard]] std::uint16_t islands_version(const World& w);
void write_islands(Writer& out, const World& w, std::uint16_t version);
[[nodiscard]] std::vector<Decoded::IslandPart> read_islands(Reader& in, std::uint16_t version);
void write_explored(Writer& out, const World& w);
[[nodiscard]] std::vector<Decoded::ExplorePart> read_explored(Reader& in);

// players.cpp: PLAY, PSTA, MISN, SCOR, DIPL.
void write_players(Writer& out, const World& w);
[[nodiscard]] std::vector<Player> read_players(Reader& in);
/// Whether the world has other factions than the base game's (only then FACT is written).
[[nodiscard]] bool has_own_factions(const World& w);
void write_factions(Writer& out, const World& w);
[[nodiscard]] Decoded::FactionPart read_factions(Reader& in);
[[nodiscard]] std::uint16_t player_state_version(const World& w);
void write_player_state(Writer& out, const World& w, std::uint16_t version);
[[nodiscard]] std::vector<Player> read_player_state(Reader& in, std::uint16_t version);
void write_missions(Writer& out, const World& w);
[[nodiscard]] Decoded::MissionPart read_missions(Reader& in, std::size_t islands);
/// The version of SCOR: kWideScores if a counter is above 65 535, else 1.
[[nodiscard]] std::uint16_t scores_version(const World& w);
void write_scores(Writer& out, const World& w, std::uint16_t version);
[[nodiscard]] Decoded::ScorePart read_scores(Reader& in, std::uint16_t version);
void write_diplomacy(Writer& out, const World& w);
[[nodiscard]] Diplomacy read_diplomacy(Reader& in);

// cities.cpp: CITY, CSTA, MLCK, CPOP, PROD, HOUS, GROW, COST.
void write_cities(Writer& out, const World& w);
[[nodiscard]] std::vector<City> read_cities(Reader& in);
void write_city_state(Writer& out, const World& w);
[[nodiscard]] std::vector<Decoded::CityStats> read_city_state(Reader& in);
void write_material_clocks(Writer& out, const World& w);
[[nodiscard]] std::vector<Decoded::MaterialClock> read_material_clocks(Reader& in);
void write_population_marks(Writer& out, const World& w);
[[nodiscard]] std::vector<Decoded::PopulationMark> read_population_marks(Reader& in);
void write_give_up_counts(Writer& out, const World& w);
[[nodiscard]] std::vector<Decoded::GiveUpCount> read_give_up_counts(Reader& in);
void write_ledgers(Writer& out, const World& w);
[[nodiscard]] std::vector<Decoded::CityLedger> read_ledgers(Reader& in);
void write_productions(Writer& out, const World& w);
[[nodiscard]] Decoded::ProductionPart read_productions(Reader& in);
void write_houses(Writer& out, const World& w);
[[nodiscard]] std::vector<House> read_houses(Reader& in);
void write_growth(Writer& out, const World& w);
[[nodiscard]] std::vector<GrowthEntry> read_growth(Reader& in);
void write_costs(Writer& out, const World& w);
[[nodiscard]] std::vector<CostBuilding> read_costs(Reader& in);

// ships.cpp: SHIP (for readers older than FLEE), FLEE.
void write_ships(Writer& out, const World& w);
[[nodiscard]] std::vector<Ship> read_ships(Reader& in);
void write_fleet(Writer& out, const World& w, std::uint16_t version);
[[nodiscard]] Decoded::FleetPart read_fleet(Reader& in, std::uint16_t version);
/// The version of FLEE: kRouteWares if a stop has a ware beyond the original's three, kLongRoutes
/// if a route has a stop beyond the original's four, 1 otherwise.
[[nodiscard]] std::uint16_t fleet_version(const World& w);

// army.cpp: SOLD (files older than ARMY, read only), ARMY.
[[nodiscard]] std::vector<Soldier> read_soldiers(Reader& in);
void write_army(Writer& out, const World& w);
[[nodiscard]] Decoded::ArmyPart read_army(Reader& in);

// nature.cpp: ANIM, DISA, VOLC.
void write_animals(Writer& out, const World& w);
[[nodiscard]] std::vector<Animal> read_animals(Reader& in);
void write_disasters(Writer& out, const World& w);
[[nodiscard]] std::vector<Disaster> read_disasters(Reader& in);
[[nodiscard]] std::uint16_t volcanoes_version(const World& w);
void write_volcanoes(Writer& out, const World& w, std::uint16_t version);
[[nodiscard]] VolcanoState read_volcanoes(Reader& in, std::uint16_t version);

// orders.cpp: QUEU, STNC.
void write_queues(Writer& out, const World& w);
void read_queues(Reader& in, Decoded::Queues& ships, Decoded::Queues& soldiers);
void write_stances(Writer& out, const World& w);
void read_stances(Reader& in, Decoded::Stances& ships, Decoded::Stances& soldiers);

// apply.cpp
/// Checks everything decoded against the world and applies it; the world stays as it was if a
/// check fails.
core::Status apply_decoded(World& world, Decoded& d);

}  // namespace o1602::sim::save
