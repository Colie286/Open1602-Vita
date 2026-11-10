// SPDX-License-Identifier: GPL-3.0-or-later
//
// The computer players and the pirates' nest building (docs/re/ai.md).
//
// The original runs its AI only on the host and sends what it does over the network (AI_Tick
// 0x0042B4B0); its pace follows the frame rate, it draws from the shared C library rand() and seeds
// the island search with the clock (S-095). Open1602 runs the AI on every computer as part of the
// simulation: at a fixed pace, with its own random numbers and through the same build, ship and
// soldier orders a player gives. Its state is saved with the game; for games of the original it is
// rebuilt from the world as the original does after every load.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/result.hpp"
#include "o1602/sim/player.hpp"

namespace o1602::sim {

class Simulation;

namespace ai {
struct State;
class Engine;
}  // namespace ai

class ComputerPlayers {
public:
    explicit ComputerPlayers(Simulation& simulation);
    ~ComputerPlayers();
    ComputerPlayers(const ComputerPlayers&) = delete;
    ComputerPlayers& operator=(const ComputerPlayers&) = delete;

    /// One tick of game time (Simulation::kTickMs).
    void tick(std::uint32_t dt_ms);

    /// Rebuilds the state of every computer player from the world (AI_Init 0x0042B710).
    void reset();

    /// A ship a computer player or the pirates direct themselves (a nest founding): the pirates'
    /// own wandering leaves it alone.
    [[nodiscard]] bool directs(std::uint32_t ship) const;
    /// A city was founded (0x004294C0, event 0x0E): a computer at peace with the founder that has
    /// a young colony (fewer than 10 inhabitants) on that island, its home city elsewhere, remembers
    /// it against him (diplomacy event 5).
    void city_founded(std::uint16_t island, std::uint8_t city);
    /// Land on `island` became free (a city dissolved, a native village gone): the computers'
    /// cities there search their sites anew (Open1602; not with Rules::bugs.classic_ai).
    void land_freed(std::uint16_t island);
    /// `asker` is about to found a city on `island` (0x004294C0 with island order 0x4C): the first
    /// computer with a city there answers if it is at peace with him and the island is not a large
    /// one (DiplomacyNotice::Kind::FoundingProtest, FoundingRefused, FoundingTolerated).
    void founding_query(PlayerId asker, std::uint16_t island);

    /// The version save() writes, the section AIST of the running game's state (DS-3); load()
    /// reads every version from the first.
    [[nodiscard]] static std::uint16_t state_version() noexcept;
    void save(core::ByteWriter& out) const;
    /// Restores what save() of `version` wrote; an error if the data is damaged or the computers'
    /// engine cannot take it (nothing changed then).
    core::Status load(core::ByteReader& in, std::uint16_t version);

    /// What a computer player is doing, for tests and the debug display.
    struct View {
        PlayerId player = kNoPlayer;
        std::uint32_t cities = 0;
        std::uint8_t max_stage = 0;
        std::uint8_t score = 0;
        bool settling = false;
        bool settle_failed = false;
        std::vector<std::uint8_t> stack;
        struct City {
            std::uint16_t island = 0;
            std::uint8_t city = 0;
            std::uint8_t stage = 0;
            std::uint32_t flags = 0;
            /// Per building slot: count, attempts, failure level.
            std::vector<std::array<std::uint16_t, 3>> slots;
        };
        std::vector<City> plans;
    };
    [[nodiscard]] std::vector<View> view() const;

private:
    std::unique_ptr<ai::State> m_state;
    std::unique_ptr<ai::Engine> m_engine;
};

}  // namespace o1602::sim
