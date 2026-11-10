// SPDX-License-Identifier: GPL-3.0-or-later
//
// Life in the picture that the game does not depend on (docs/re/flavor.md): townsfolk walking from
// their houses to taverns, churches, schools and markets, whales, dolphins and krakens in the sea,
// jugglers on the markets of bigger cities, birds in the forests and surf at the beaches.
//
// The original runs these through the simulation: they take slots of its figure list, draw from
// the one rand() of the game and depend on where the player looks, so scrolling changes fires,
// plague and robbers (S-065). In Open1602 they live only here, on the side of the picture, with no
// effect on the game and nothing saved. Every decision is a function of a fixed seed, the game
// time and the world as it is after a tick, never of the camera or the frame rate: all computers
// of a multiplayer game see the same townsfolk, whales and jugglers without sending anything.
// They follow game time, so they stop when the game is paused.
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/render/figure_animation.hpp"
#include "o1602/render/world_renderer.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/random.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::render {

/// What the player at this computer sees. It only filters the picture and the sounds, never a
/// decision, so every computer of a game still keeps the same scenery.
/// A box of world tiles (continuous), by default the whole plane: what may be on screen, with a
/// margin for tall pictures and high ground.
struct TileBox {
    double x0 = -std::numeric_limits<double>::infinity();
    double y0 = -std::numeric_limits<double>::infinity();
    double x1 = std::numeric_limits<double>::infinity();
    double y1 = std::numeric_limits<double>::infinity();
    [[nodiscard]] bool contains(double x, double y) const noexcept { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};

struct AmbientView {
    std::vector<bool> islands;  ///< on screen, by index into World::islands(); only for the sounds
    /// Townsfolk and jugglers show only on islands this player has discovered, as in the original
    /// (docs/re/flavor.md); kNoPlayer shows every island.
    sim::PlayerId local_player = sim::kNoPlayer;
    std::uint32_t game_speed = 1;  ///< sounds play only at normal speed, as in the original
    /// Share of the next tick that has passed (Simulation::tick_fraction): townsfolk and sea
    /// creatures are drawn at the game time of the last tick but one plus this share of a tick,
    /// so that they move between ticks like the units of the game (TickTrail). Only the picture.
    double tick_fraction = 1.0;
    /// World tiles that may be on screen (FB-10): townsfolk, creatures and jugglers outside are
    /// not drawn and not animated (they catch up once seen).
    TileBox drawn;
};

/// A sound of the scenery to play at a world point (no sound output yet).
struct AmbientSound {
    enum class Kind : std::uint8_t { Birds, Surf };
    Kind kind = Kind::Birds;
    std::uint8_t variant = 0;  ///< WAV_VOGEL1 + variant for birds
    double x = 0.0;
    double y = 0.0;
};

// Figures of figuren.cod.
inline constexpr std::uint16_t kFigureJuggler = assets::figure::kJuggler;
inline constexpr std::uint16_t kFigureWhale = assets::figure::kWhale;
inline constexpr std::uint16_t kFigureDolphin = assets::figure::kDolphin;
inline constexpr std::uint16_t kFigureKraken = assets::figure::kKraken;

/// The townsfolk a walker going to a building of HAUS_PRODTYP Kind `kind` can be (0x004E857C),
/// four choices; FRAU for every other kind.
[[nodiscard]] std::array<std::uint16_t, 4> townsfolk_for(std::int32_t kind) noexcept;

/// HAUS_PRODTYP Kinds a walker may go to, by the roll 0..3 (0x00480370).
[[nodiscard]] std::vector<std::int32_t> walker_targets(std::uint32_t roll);

/// How often the scenery shows itself; a setting of the player, not of the game.
struct AmbientConfig {
    /// Sea creatures: on average one per sea block (about one screen of sea) every this many ms.
    /// Ambient::kSeaPeriodMs is the original's rate (a chance every 5 s); 0 turns them off.
    std::uint32_t sea_interval_ms = 60'000;
};

class Ambient {
public:
    static constexpr std::uint32_t kCityPeriodMs = 10'000;    ///< walkers: one round per city per 10 s
    static constexpr std::uint32_t kSeaPeriodMs = 5'000;      ///< sea creatures: a chance every 5 s
    static constexpr std::uint32_t kSeaBlock = 28;            ///< sea creatures: tiles per side of a sea block
    static constexpr std::uint32_t kJugglerPeriodMs = 1'000;  ///< markets are looked at once a second

    using Config = AmbientConfig;
    static constexpr std::uint32_t kSeaIntervalMaxMs = 3'600'000;  ///< an hour: settings are clamped to this

    /// All computers of a game must use the same seed (and the same Config to see the same sea).
    explicit Ambient(std::uint64_t seed = 1602, Config config = {}) noexcept : m_seed(seed), m_config(config) {}

    /// Takes effect from the next chance on; what is already in the picture stays.
    void set_config(const Config& config) noexcept { m_config = config; }
    [[nodiscard]] const Config& config() const noexcept { return m_config; }

    /// Decisions up to the world's time: call after every tick of the simulation, with the world
    /// as that tick left it. What happened between two calls is decided on the world of the later.
    void tick(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures);
    /// Pictures and sounds at the world's time: call once per displayed frame.
    void frame(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
               const AmbientView& view);

    /// Townsfolk on the roads (drawn like the carriers, from TRAEGER.BSH).
    [[nodiscard]] const std::vector<FigureInstance>& walkers() const noexcept { return m_walker_instances; }
    /// Sea creatures and jugglers.
    [[nodiscard]] const std::vector<EffectFigure>& effects() const noexcept { return m_effects; }
    /// Sounds due since the last call.
    [[nodiscard]] std::vector<AmbientSound> take_sounds() {
        std::vector<AmbientSound> s;
        s.swap(m_sounds);
        return s;
    }

    struct Stats {
        std::uint64_t walkers = 0;  ///< townsfolk sent out
        std::uint64_t creatures = 0;
    };
    [[nodiscard]] const Stats& stats() const noexcept { return m_stats; }

private:
    struct Walker {
        std::uint16_t island = 0;
        std::uint16_t figure = 0;
        std::uint64_t house = 0;            ///< key of the house it came from
        std::vector<sim::TilePos> path;     ///< island tiles, from the house to the target
        std::vector<std::uint32_t> arrive;  ///< ms after the start it reaches path[i]
        std::uint64_t start_ms = 0;         ///< game time it left the house
        std::uint64_t end_ms = 0;           ///< game time it reaches the target
        double z = 0.0;                     ///< height of the house tile (render::spawn_height)
        std::uint64_t anim_ms = 0;          ///< game time `anim` is at
        AnimState anim;
    };
    struct Creature {
        std::uint16_t figure = 0;
        double x = 0.0;  ///< where it came up
        double y = 0.0;
        double vx = 0.0;  ///< tiles per ms (whales only)
        double vy = 0.0;
        std::uint8_t direction = 0;
        std::uint64_t start_ms = 0;
        std::uint64_t end_ms = 0;
        std::uint64_t anim_ms = 0;
        AnimState anim;
    };
    struct Juggler {
        std::uint64_t anim_ms = 0;
        AnimState anim;
        bool seen = false;
    };

    void city_round(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                    const sim::City& city, std::uint64_t at_ms, std::uint64_t round);
    void send_walker(const sim::World& world, const assets::BuildingTable& buildings,
                     const assets::FigureTable& figures, const sim::House& house, sim::Random& random,
                     std::uint64_t at_ms);
    void sea_round(const sim::World& world, const assets::FigureTable& figures, std::uint64_t at_ms,
                   std::uint64_t round, std::uint32_t bx, std::uint32_t by);
    /// Whether soldiers of a player other than `owner` (a human or a computer) stand on the island.
    [[nodiscard]] bool foreign_troops(const sim::World& world, std::uint16_t island, sim::PlayerId owner);
    void sea_block(const sim::World& world, const assets::FigureTable& figures, sim::Random& random, std::uint32_t x0,
                   std::uint32_t y0, std::uint64_t at_ms);
    void jugglers(const sim::World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                  std::uint64_t at_ms);
    void sounds(const sim::World& world, const assets::BuildingTable& buildings, const AmbientView& view,
                std::uint64_t now_ms);
    void retire(std::uint64_t at_ms);
    [[nodiscard]] sim::Random stream(std::uint64_t tag, std::uint64_t a, std::uint64_t b,
                                     std::uint64_t c = 0) const noexcept;

    std::uint64_t m_seed;
    Config m_config;
    bool m_started = false;
    std::uint64_t m_tick_ms = 0;   ///< game time decided up to
    std::uint64_t m_frame_ms = 0;  ///< game time of the last pictures (sounds)
    std::vector<Walker> m_walking;
    std::unordered_set<std::uint64_t> m_busy_houses;
    std::vector<Creature> m_creatures;
    // Built once per tick() when first asked (TB-27): the ships by squares of kSeaBlock tiles
    // (index into World::ships()) and the players with troops on each island.
    std::vector<std::vector<std::uint32_t>> m_ship_squares;
    std::uint32_t m_ship_cols = 0;
    bool m_ships_squared = false;
    std::vector<std::vector<sim::PlayerId>> m_island_troops;
    bool m_troops_counted = false;
    std::map<std::uint64_t, Juggler> m_jugglers;  ///< by market (ordered: the same picture everywhere)
    struct Emitter {
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        bool birds = true;
        std::uint32_t hash = 0;
    };
    struct IslandEmitters {
        std::uint64_t built_ms = 0;
        bool built = false;
        std::vector<Emitter> list;
    };
    std::vector<IslandEmitters> m_emitters;  ///< by island, looked for again every 10 s
    std::vector<FigureInstance> m_walker_instances;
    std::vector<EffectFigure> m_effects;
    std::vector<AmbientSound> m_sounds;
    Stats m_stats;
};

}  // namespace o1602::render
