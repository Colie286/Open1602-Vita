// SPDX-License-Identifier: GPL-3.0-or-later
//
// The islands of a scenario that makes them at the start (docs/re/worldgen.md): the endless games
// of the original carry no islands but a world plan (SZENE) with each island's number, place, size
// class and climate. When the game starts, the original draws for each island one of the island
// templates of its climate and size, puts native villages on a few of them, hands out the
// plantation crops, ore and gold veins and treasures, and finds the volcano craters. It draws
// from a table of 512 random numbers saved with the scenario (RANDTAB), starting at an index the
// host rolls, so a file has 512 possible worlds.
//
// Open1602 turns such a file into an ordinary scenario with these islands (INSEL5 headers that
// point to the templates, the village records of the native templates), which the normal loader
// and the savegame writer then handle like any other.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/assets/scenario.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/random.hpp"
#include "o1602/sim/world_plan.hpp"

namespace o1602::sim {

/// How the islands of a world plan are drawn.
struct WorldStart {
    enum class Draws : std::uint8_t {
        Table,   ///< as the original: the scenario's RANDTAB from index seed % 512 (512 worlds per file)
        Seeded,  ///< Open1602's generator from the whole 64-bit seed
    };
    Draws draws = Draws::Table;
    std::uint64_t seed = 0;
    /// Rules::guarantee_resources (S-019): after the original's passes, every crop of a climate,
    /// iron and gold that the plan asks for but did not get is placed on an island that can
    /// take it. Off: the world as the original makes it.
    bool guarantee_resources = false;
};

/// The draws of the generator: `below(n)` is a number in 0..n-1.
class PlanRandom {
public:
    PlanRandom(const std::array<std::uint16_t, 512>& table, std::uint32_t start) noexcept
        : m_table(table), m_index(start & 511U) {}
    explicit PlanRandom(std::uint64_t seed) noexcept : m_seeded(true), m_random(seed, 0x1602'5EEDULL) {}

    /// The original takes Rnd() % n (0x0048C570), slightly favouring small numbers.
    [[nodiscard]] std::uint32_t below(std::uint32_t n) noexcept;
    [[nodiscard]] std::uint32_t draws() const noexcept { return m_draws; }

private:
    bool m_seeded = false;
    std::array<std::uint16_t, 512> m_table{};
    std::uint32_t m_index = 0;
    Random m_random;
    std::uint32_t m_draws = 0;
};

/// Whether the file plans islands that are made at the start (a SZENE chunk with islands without
/// a fixed template and no islands of its own).
[[nodiscard]] bool plans_islands(const assets::ChunkFile& file);

/// What became of an island of the plan, for tests and reports.
struct GeneratedIsland {
    std::uint8_t number = 0;
    std::uint8_t climate = 0;
    std::uint8_t size = 0;
    std::uint16_t template_number = 0;
    bool natives = false;
    std::uint32_t fertility = 0;  ///< INSEL5 +0x5C
    std::vector<assets::IslandHeader::Deposit> deposits;
};

struct GeneratedWorld {
    assets::ChunkFile file;  ///< the scenario with the islands made, ready for load_world
    std::vector<GeneratedIsland> islands;
    std::uint32_t draws = 0;  ///< random numbers taken
    /// Units of the plan that found no place (the original drops them silently, S-019), and
    /// those Open1602 placed afterwards with WorldStart::guarantee_resources.
    std::uint32_t lost = 0;
    std::uint32_t guaranteed = 0;
};

/// Makes the islands of `scenario`'s world plan. Templates come from the game directory; files
/// that cannot be read are reported in `warnings` and their islands left out.
[[nodiscard]] core::Result<GeneratedWorld> generate_islands(const assets::ChunkFile& scenario,
                                                            const assets::GameDirectory& game,
                                                            const assets::BuildingTable& buildings,
                                                            const WorldStart& start,
                                                            std::vector<std::string>* warnings = nullptr);

/// Makes the islands of an own world plan (world_plan.hpp) with Open1602's generator from
/// `start.seed`: the original's content passes, then every start area gets the same (iron on the
/// home island before the passes, all six crops and a gold vein within reach after them). The
/// rest of `scenario` (players, ships, timers) is kept; its own plan, if any, is replaced.
[[nodiscard]] core::Result<GeneratedWorld> generate_islands(const assets::ChunkFile& scenario, const WorldPlan& plan,
                                                            const assets::GameDirectory& game,
                                                            const assets::BuildingTable& buildings,
                                                            const WorldStart& start,
                                                            std::vector<std::string>* warnings = nullptr);

}  // namespace o1602::sim
