// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scores and the figures of the end screen (docs/re/missions.md).
#include "o1602/sim/score.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <span>

#include "o1602/assets/scenario.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/mission.hpp"
#include "o1602/sim/world.hpp"

namespace o1602::sim {

namespace {

constexpr std::size_t kTimersMission = 0xA7;  // TIMERS: mission number (endless game 1)
constexpr std::size_t kTimersGameId = 0xAC;   // TIMERS: game id
constexpr std::size_t kTimersRanking = 0xBC;  // TIMERS: SZENE_RANKING of the scenario

constexpr std::size_t kCityCurve = scoring::kCityCurveLast;
constexpr std::size_t kFightCurve = scoring::kFightCurveLast;

/// Fills a table with a straight segment as 0x00474F90 builds them: from a to b in steps of 1/256,
/// truncated, the end value exactly.
void fill(std::span<std::int32_t> table, const scoring::Segment& s) {
    const std::int32_t step = (s.to * 256 - s.from * 256) / (s.b - s.a);
    std::int32_t acc = s.from * 256;
    for (std::int32_t i = s.a; i < s.b; ++i) {
        table[static_cast<std::size_t>(i)] = acc >> 8;
        acc += step;
    }
    table[static_cast<std::size_t>(s.b)] = s.to;
}

const std::array<std::int32_t, kCityCurve + 1>& city_table() {
    static const auto table = [] {
        std::array<std::int32_t, kCityCurve + 1> t{};
        for (const scoring::Segment& s : scoring::kCityCurve) {
            fill(t, s);
        }
        for (std::int32_t& v : t) {
            v = std::min(v, 255);  // a byte table
        }
        return t;
    }();
    return table;
}

const std::array<std::int32_t, kFightCurve + 1>& fight_table() {
    static const auto table = [] {
        std::array<std::int32_t, kFightCurve + 1> t{};
        for (const scoring::Segment& s : scoring::kFightCurve) {
            fill(t, s);
        }
        t[0] = 0;
        return t;
    }();
    return table;
}

std::int64_t money_points(std::int64_t money) {
    for (const scoring::MoneyStep& step : scoring::kMoneySteps) {
        if (money >= step.limit) {
            return step.points;
        }
    }
    return 0;
}

/// Points of fights: by the number of wins `n` on the curve, times the share of won fights.
std::int64_t fight_points(std::int64_t won, std::int64_t lost, std::int64_t n) {
    if (won + lost == 0) {
        return 0;
    }
    const std::int64_t share = won * 128 / (won + lost);
    return std::int64_t{fight_curve(static_cast<std::size_t>(std::min<std::int64_t>(n, kFightCurve)))} * share >> 7;
}

}  // namespace

std::uint8_t city_curve(std::size_t index) noexcept {
    return static_cast<std::uint8_t>(city_table()[std::min(index, kCityCurve)]);
}

std::int32_t fight_curve(std::size_t index) noexcept {
    return fight_table()[std::min(index, kFightCurve)];
}

ScoreSheet score_sheet(const World& world, const assets::BuildingTable& buildings, PlayerId player) {
    ScoreSheet s;
    s.player = player;
    const Player* p = world.player(player);
    if (p == nullptr) {
        return s;
    }
    const auto t = static_cast<std::int64_t>(world.time_ms() / 100);  // the original's 1/10 s

    s.money = p->money;
    s.money_points = money_points(s.money);

    // The cities: Einwohner, the inhabitants per class, the size of each city, the islands and
    // the land they cover. The land is sampled on every 8th tile of each row and column.
    const std::vector<std::uint32_t> workers = city_workers(world, buildings);
    const auto& classes = buildings.resident_classes();
    std::vector<std::int64_t> per_class(classes.size(), 0);
    std::set<std::uint16_t> islands;
    std::int64_t land = 0;
    for (std::size_t i = 0; i < world.cities().size(); ++i) {
        const City& c = world.cities()[i];
        if (c.owner != player) {
            continue;
        }
        ++s.cities;
        const std::int64_t e = city_einwohner(c, i < workers.size() ? workers[i] : 0);
        s.inhabitants += e;
        s.city_points += std::int64_t{city_curve(static_cast<std::size_t>(e / 20))} * e / 64;
        for (std::size_t k = 0; k < c.inhabitants.size() && k < per_class.size(); ++k) {
            per_class[k] += c.inhabitants[k];
        }
        if (c.island < world.islands().size()) {
            islands.insert(c.island);
            const IslandMap& map = world.islands()[c.island].map;
            for (std::uint32_t y = 0; y < map.height(); y += 8) {
                for (std::uint32_t x = 0; x < map.width(); x += 8) {
                    land += map.top(x, y).city == c.index ? 64 : 0;
                }
            }
        }
    }
    for (std::size_t k = 0; k < per_class.size(); ++k) {
        s.subjects_points += per_class[k] * classes[k].hiscore / 50;
    }
    s.islands = static_cast<std::int64_t>(islands.size());
    std::int64_t area = 0;
    for (const WorldIsland& isl : world.islands()) {
        area += std::int64_t{isl.map.width()} * isl.map.height();
    }
    s.land_points = area != 0 ? land * 10000 / area : 0;

    // Monuments the people wanted, per hour of game time from the tenth minute on.
    s.monuments = p->monuments_allowed;
    s.monument_points = t <= scoring::kMonumentStart ? 0 : s.monuments * scoring::kMonumentFactor / t;

    // Opponents defeated: the original counts the triumphal arch allowances (player + 0x8E).
    s.opponents_defeated = p->arches_allowed;
    s.defeat_points = s.opponents_defeated * scoring::kDefeatPoints;

    s.soldiers_killed = p->soldiers_killed;
    s.soldiers_lost = p->soldiers_lost;
    s.soldier_points = fight_points(s.soldiers_killed, s.soldiers_lost, (s.soldiers_killed + 4) / 5);
    s.ships_sunk = p->ships_sunk;
    s.ships_lost = p->ships_lost;
    s.ship_points = fight_points(s.ships_sunk, s.ships_lost, (s.ships_sunk + 1) / 2);

    const std::int64_t sum = s.ship_points + s.city_points + s.money_points + s.monument_points + s.land_points +
                             s.soldier_points + s.defeat_points + s.subjects_points;
    // A winner's points count twice (HighScore_UpdateAllHumans 0x00475990).
    s.won = p->won;
    s.bonus_percent = s.won ? 100 : 0;
    s.bonus = s.won ? sum : 0;
    s.total = sum + s.bonus;
    return s;
}

GameIdentity game_identity(const World& world) {
    GameIdentity id;
    const assets::ChunkFile* file = world.source().get();
    if (file == nullptr) {
        return id;
    }
    if (const assets::Chunk* name = file->find("NAME")) {
        const auto bytes = file->payload(*name);
        const auto end = std::find(bytes.begin(), bytes.end(), std::uint8_t{0});
        id.name.assign(bytes.begin(), end);
    }
    id.custom = file->find("CUSTOM") != nullptr;
    const assets::ScenarioHeader header = assets::load_scenario_header(*file);
    const bool has_ranking = file->find("SZENE_RANKING") != nullptr;
    id.ranking = has_ranking ? header.ranking : 0;
    id.mission_number = header.mission_number;
    id.campaign = header.campaign;
    id.game_id = header.game_id;
    // A savegame keeps what the scenario said in TIMERS (the SZENE_* chunks are gone).
    if (const assets::Chunk* timers = file->find("TIMERS")) {
        const auto t = file->payload(*timers);
        core::ByteReader r(t);
        if (r.seek(kTimersGameId)) {
            if (const auto game = r.u32(); game && *game != 0) {
                id.game_id = *game;
            }
        }
        if (id.mission_number == GameIdentity::kNone && t.size() > kTimersMission) {
            id.mission_number = t[kTimersMission];
        }
        if (!has_ranking && r.seek(kTimersRanking)) {
            id.ranking = r.u32().value_or(0);
        }
    }
    return id;
}

EndScreenSummary end_screen_summary(const World& world, const assets::BuildingTable& buildings, PlayerId local,
                                    GameOutcome outcome) {
    EndScreenSummary s;
    s.outcome = outcome;
    s.local = local;
    s.time_ms = world.game_end() ? world.game_end()->time_ms : world.time_ms();
    s.game = game_identity(world);
    for (std::size_t i = 0; i < world.players().size(); ++i) {
        const Player& p = world.players()[i];
        if ((p.role != PlayerRole::Human && p.role != PlayerRole::Computer) || p.status == PlayerStatus::Absent) {
            continue;
        }
        EndScreenRow r;
        r.player = static_cast<PlayerId>(i);
        r.name = p.name;
        r.banner = p.banner;
        r.role = p.role;
        r.status = p.status;
        r.sheet = score_sheet(world, buildings, r.player);
        s.rows.push_back(std::move(r));
    }
    std::stable_partition(s.rows.begin(), s.rows.end(), [local](const EndScreenRow& r) { return r.player == local; });
    return s;
}

}  // namespace o1602::sim
