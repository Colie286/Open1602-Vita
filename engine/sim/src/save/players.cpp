// SPDX-License-Identifier: GPL-3.0-or-later
//
// The players: PLAY (who they are), FACT (their factions), PSTA (missions and defeat), MISN (the missions themselves
// and the end of the game), SCOR (the score's counters and the monument clocks of the cities) and DIPL (treaties
// between them).
#include <algorithm>
#include <array>

#include "section_io.hpp"

namespace o1602::sim::save {

namespace {

/// PLAY keeps the role byte of earlier versions: 0 human, 1 computer, 2 to 4 the base game's free
/// trader, pirates and natives. A faction of a content package goes as the one it acts as; FACT
/// tells which it is.
std::uint8_t legacy_kind(const Player& p, const std::vector<Faction>& factions) noexcept {
    const FactionBehaviour b = p.faction < factions.size() ? factions[p.faction].behaviour : FactionBehaviour::Settlers;
    if (b != FactionBehaviour::Settlers) {
        return static_cast<std::uint8_t>(1U + static_cast<std::uint8_t>(b));
    }
    return p.role == PlayerRole::Computer ? 1 : 0;
}

void from_legacy_kind(Player& p, std::uint8_t kind) noexcept {
    if (kind >= 2 && kind <= 1U + static_cast<std::uint8_t>(kLastBehaviour)) {
        make_neutral(p, base_faction(static_cast<FactionBehaviour>(kind - 1U)));
    } else {
        p.role = kind == 1 ? PlayerRole::Computer : PlayerRole::Human;
    }
}

template <class A, class P>
void io_player(A& a, P& p, const std::vector<Faction>& factions) {
    if constexpr (A::kReading) {
        std::uint8_t kind = 0;
        a.u8(kind);
        from_legacy_kind(p, kind);
    } else {
        a.u8(legacy_kind(p, factions));
    }
    a.u16(p.banner);
    a.i32(p.money);
    a.string(p.name);
    a.u32(p.infra_mask);
    a.u8(p.castles_built);
    a.u8(p.cathedrals_built);
    a.u16(p.arches_allowed);
    a.u16(p.arches_built);
    a.u16(p.monuments_allowed);
    a.u16(p.monuments_built);
}

template <class A, class P>
void io_player_state(A& a, P& p, std::uint16_t version) {
    a.u8(p.mission);
    a.enum8(p.status, PlayerStatus::Absent);
    a.boolean(p.won);
    a.u16(p.opponents_defeated);
    a.u8(p.bankrupt_checks);
    a.u8(p.empty_checks);
    a.i64(p.purchases);
    a.i64(p.sales);
    if (version >= kScenarioSwitches) {
        a.u32(p.scenario.flags);
        a.u16(p.scenario.rival_inhabitants);
        a.u16(p.scenario.own_inhabitants);
        a.u8(p.scenario.own_development);
        a.u8(p.scenario.rival_development);
    }
    if (version >= kClosedGates) {
        a.boolean(p.gates_closed);
    }
}

template <class A, class S>
void io_scaling(A& a, S& s) {
    a.u16(s.base);
    a.u16(s.per);
    a.u16(s.from);
    a.u16(s.min);
    a.u16(s.max);
}

template <class A, class F>
void io_faction(A& a, F& f) {
    a.string(f.id);
    a.enum8(f.behaviour, kLastBehaviour);
    io_scaling(a, f.nests);
    io_scaling(a, f.ships);
    a.u16(f.reach_players);
}

template <class A, class G>
void io_city_goal(A& a, G& g) {
    a.i32(g.inhabitants);
    a.u8(g.resident_class);
    a.u32(g.count);
}

/// A mission; `islands` (reading only) is the number of islands its island goals may name.
template <class A, class M>
void io_mission(A& a, M& m, std::size_t islands) {
    a.u32(m.goals);
    a.u32(m.losses);
    a.u8(m.target);
    a.u16(m.min_defeated);
    a.u8s(m.defeat);
    a.i32(m.min_cities);
    a.i32(m.min_inhabitants);
    a.i32(m.min_balance);
    a.i32(m.min_trade_balance);
    a.i32(m.protected_min_cities);
    a.u16(m.success_message);
    a.string(m.text);
    for (auto& g : m.goods) {
        a.u8(g.ware);
        a.u32(g.amount);
    }
    for (auto& g : m.islands) {
        a.u8(g.resource);
        a.list(g.islands, 2, [&](auto& i) {
            a.u16(i);
            if constexpr (A::kReading) {
                if (i >= islands) {
                    a.fail();  // the island list and the islands do not belong together
                }
            }
        });
    }
    for (auto& g : m.cities) {
        io_city_goal(a, g);
    }
    io_city_goal(a, m.every_city);
    io_city_goal(a, m.target_city);
    io_city_goal(a, m.protected_city);
    a.i32(m.unused_48);
    a.i32(m.unused_58);
    if constexpr (A::kReading) {
        if ((m.goals & ~(goal::kKnown | goal::kComputers)) != 0 || (m.losses & ~loss::kKnown) != 0) {
            a.fail();  // goals of a later version
        }
    }
}

template <class A, class E>
void io_game_end(A& a, E& end) {
    bool ended = end.has_value();
    a.boolean(ended);
    if (!ended) {
        return;
    }
    World::GameEnd e = end.value_or(World::GameEnd{});
    a.u64(e.time_ms);
    a.u8(e.winner);
    if constexpr (A::kReading) {
        end = e;
    }
}

/// SCOR: the counters of a Player (writing) or of Decoded::ScorePart::Counters (reading), which has
/// the same names.
template <class A, class P>
void io_score_counters(A& a, P& p, std::uint16_t version) {
    for (auto* counter : {&p.soldiers_killed, &p.soldiers_lost, &p.ships_sunk, &p.ships_lost}) {
        if (version >= kWideScores) {
            a.u32(*counter);
        } else {
            a.u16(*counter);
        }
    }
}

/// SCOR: the monument clock of a City (writing) or of Decoded::ScorePart::CityClock (reading).
template <class A, class C>
void io_city_clock(A& a, C& c) {
    a.u16(c.island);
    a.u8(c.index);
    a.i32(c.house_growth);
    a.u64(c.growing_since_ms);
}

template <class A, class R>
void io_relation(A& a, R& r) {
    a.u8_masked(r.peace, 3U);
    a.u8_masked(r.trade, 3U);
    a.u64(r.war_time_ms);
    a.u32(r.tribute);
    a.u32(r.trade_mood);
    a.u32(r.damage);
}

template <class A, class E>
void io_event(A& a, E& e) {
    a.u8(e.type);
    if constexpr (A::kReading) {
        if (e.type > DiplomacyEventType::HostileGesture) {
            e.type = DiplomacyEventType::None;  // a kind of a later version
        }
    }
    a.u8(e.partner);
    a.u64(e.time_ms);
}

/// The relations of every pair of players, then each player's events and who it blames.
template <class A, class D>
void io_diplomacy(A& a, D& d) {
    std::uint32_t n = static_cast<PlayerId>(d.size());  // players are numbered by PlayerId
    a.u32(n);
    if constexpr (A::kReading) {
        if (n > kMaxPlayerCount) {
            a.fail();
            return;
        }
        d.resize(n);
    }
    for (std::uint32_t i = 0; i < n && a.ok(); ++i) {
        const auto pa = static_cast<PlayerId>(i);
        for (std::uint32_t k = 0; k < n; ++k) {
            io_relation(a, d.relation(pa, static_cast<PlayerId>(k)));
        }
        std::array<DiplomacyEvent, Diplomacy::kEvents> events = d.events(pa);
        for (DiplomacyEvent& e : events) {
            io_event(a, e);
        }
        PlayerId blamed = d.blamed(pa);
        a.u8(blamed);
        if constexpr (A::kReading) {
            d.set_events(pa, events);
            d.set_blamed(pa, blamed);
        }
    }
}

}  // namespace

void write_players(Writer& out, const World& w) {
    out.list(w.players(), 8, [&](const Player& p) { io_player(out, p, w.factions()); });
}

std::vector<Player> read_players(Reader& in) {
    std::vector<Player> list;
    const std::vector<Faction> base = base_factions();
    in.list(list, 8, [&](Player& p) { io_player(in, p, base); });
    if (list.size() > kMaxPlayerCount) {
        in.fail();  // before the diplomacy table (players squared) is sized for them
    }
    return list;
}

bool has_own_factions(const World& w) {
    return w.factions() != base_factions();
}

void write_factions(Writer& out, const World& w) {
    out.list(w.factions(), 23, [&](const Faction& f) { io_faction(out, f); });
    out.list(w.players(), 1, [&](const Player& p) { out.u8(p.faction); });
}

Decoded::FactionPart read_factions(Reader& in) {
    Decoded::FactionPart part;
    in.list(part.factions, 23, [&](Faction& f) { io_faction(in, f); });
    in.list(part.players, 1, [&](FactionId& f) { in.u8(f); });
    if (part.factions.empty() || part.factions.size() > kMaxPlayerCount ||
        std::any_of(part.players.begin(), part.players.end(), [&](FactionId f) { return f >= part.factions.size(); })) {
        in.fail();
    }
    return part;
}

std::uint16_t player_state_version(const World& w) {
    const bool any = std::any_of(w.players().begin(), w.players().end(),
                                 [](const Player& p) { return p.scenario != ScenarioSwitches{}; });
    if (std::any_of(w.players().begin(), w.players().end(), [](const Player& p) { return p.gates_closed; })) {
        return kClosedGates;
    }
    return any ? kScenarioSwitches : 1;
}

namespace {

std::size_t player_state_size(std::uint16_t version) {
    if (version >= kClosedGates) {
        return 34;
    }
    return version >= kScenarioSwitches ? 33 : 23;
}

}  // namespace

void write_player_state(Writer& out, const World& w, std::uint16_t version) {
    out.list(w.players(), player_state_size(version), [&](const Player& p) { io_player_state(out, p, version); });
}

std::vector<Player> read_player_state(Reader& in, std::uint16_t version) {
    std::vector<Player> list;
    in.list(list, player_state_size(version), [&](Player& p) { io_player_state(in, p, version); });
    return list;
}

void write_missions(Writer& out, const World& w) {
    out.list(w.missions(), 40, [&](const Mission& m) { io_mission(out, m, 0); });
    io_game_end(out, w.game_end());
}

Decoded::MissionPart read_missions(Reader& in, std::size_t islands) {
    Decoded::MissionPart part;
    in.list(part.missions, 40, [&](Mission& m) { io_mission(in, m, islands); });
    io_game_end(in, part.end);
    return part;
}

std::uint16_t scores_version(const World& w) {
    const bool wide = std::ranges::any_of(w.players(), [](const Player& p) {
        return std::max({p.soldiers_killed, p.soldiers_lost, p.ships_sunk, p.ships_lost}) > 0xFFFF;
    });
    return wide ? kWideScores : 1;
}

void write_scores(Writer& out, const World& w, std::uint16_t version) {
    out.list(w.players(), version >= kWideScores ? 16 : 8,
             [&](const Player& p) { io_score_counters(out, p, version); });
    out.list(w.cities(), 15, [&](const City& c) { io_city_clock(out, c); });
}

Decoded::ScorePart read_scores(Reader& in, std::uint16_t version) {
    Decoded::ScorePart part;
    in.list(part.players, version >= kWideScores ? 16 : 8, [&](auto& p) { io_score_counters(in, p, version); });
    in.list(part.cities, 15, [&](auto& c) { io_city_clock(in, c); });
    return part;
}

void write_diplomacy(Writer& out, const World& w) {
    io_diplomacy(out, w.diplomacy());
}

Diplomacy read_diplomacy(Reader& in) {
    Diplomacy d;
    io_diplomacy(in, d);
    return d;
}

}  // namespace o1602::sim::save
