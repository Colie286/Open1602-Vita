// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/diplomacy.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>
#include <utility>

#include "o1602/sim/world.hpp"

namespace o1602::sim {

namespace {

/// Piecewise linear table of the original (Table_InterpByte 0x00403370): knots (x, value); 0 past
/// the last knot unless it says otherwise.
struct Knot {
    std::int32_t x;
    std::int32_t v;
};

std::int32_t interpolate(std::initializer_list<Knot> knots, std::int32_t x) noexcept {
    const Knot* prev = nullptr;
    for (const Knot& k : knots) {
        if (x <= k.x) {
            if (prev == nullptr) {
                return k.v;
            }
            return prev->v + (k.v - prev->v) * (x - prev->x) / (k.x - prev->x);
        }
        prev = &k;
    }
    return prev != nullptr ? prev->v : 0;
}

/// Only the settlers make and break treaties; the neutral factions answer.
bool acts_diplomatically(FactionBehaviour behaviour) noexcept {
    return behaviour == FactionBehaviour::Settlers;
}

PlayerRole role_of(const World& world, PlayerId p) noexcept {
    const Player* player = world.player(p);
    return player != nullptr ? player->role : PlayerRole::Human;
}

FactionBehaviour behaviour_of(const World& world, PlayerId p) noexcept {
    const Player* player = world.player(p);
    return player != nullptr ? world.behaviour(*player) : FactionBehaviour::Settlers;
}

/// A byte table of the original filled piece by piece (Table_InterpByte 0x00403370, exactly: 8.8
/// fixed point, the step rounded towards zero, the last value of a piece set at its end).
struct Piece {
    std::int32_t a;
    std::int32_t b;
    std::int32_t v0;
    std::int32_t v1;
};
template <std::size_t N>
constexpr std::array<std::uint8_t, 129> byte_table(const std::array<Piece, N>& pieces) {
    std::array<std::uint8_t, 129> t{};
    for (const Piece& p : pieces) {
        const std::int32_t step = (p.v1 * 256 - p.v0 * 256) / (p.b - p.a);
        std::int32_t acc = p.v0 * 256;
        for (std::int32_t i = p.a; i < p.b; ++i) {
            t[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(std::min(acc >> 8, 255));
            acc += step;
        }
        t[static_cast<std::size_t>(p.b)] = static_cast<std::uint8_t>(p.v1);
    }
    return t;
}

/// The scenario attitude's penalties (Dipl_InitTables 0x00474600): by the share of its target a
/// computer's own inhabitants reach (0x00548E38), and by how far a development misses its threshold
/// (0x00549044).
constexpr auto kOwnSizePenalty =
    byte_table(std::array<Piece, 3>{{{0, 64, 170, 130}, {64, 102, 130, 80}, {102, 128, 80, 0}}});
constexpr auto kDevelopmentPenalty =
    byte_table(std::array<Piece, 3>{{{0, 25, 0, 70}, {25, 42, 70, 130}, {42, 128, 130, 170}}});
/// A human beyond the rival threshold: the table 0x00548FC0 at its end. The original computes the share
/// (at least 128 when the threshold is passed) and then always takes 128.
constexpr std::int32_t kRivalSizePenalty = 170;

/// Penalty of a development `missed` levels away from its threshold. The original reads past the
/// table's end beyond 128 (S-118); Open1602 stays at its last value.
std::int32_t development_penalty(std::int32_t missed) noexcept {
    return kDevelopmentPenalty[static_cast<std::size_t>(std::clamp(missed, 0, 128))];
}

constexpr std::uint32_t kCounterMax = 0xFFFF;  // the original's u16 counters, saturating here (S-086)

}  // namespace

std::int32_t diplomacy_event_value(DiplomacyEventType type, std::uint64_t age_ms) noexcept {
    // Age in units of 12.8 s ((now - time) >> 7 in 1/10 s), at most 255.
    const auto age = static_cast<std::int32_t>(std::min<std::uint64_t>(255, age_ms / 100 >> 7));
    switch (type) {
        case DiplomacyEventType::TradeCancelled:
            return interpolate({{0, -30}, {50, -18}, {75, 0}}, age);
        case DiplomacyEventType::WarDeclared:
            return interpolate({{0, -30}, {75, -18}, {150, -8}, {200, 0}}, age);
        case DiplomacyEventType::Settled:
            return interpolate({{0, -50}, {50, -25}, {75, 0}}, age);
        case DiplomacyEventType::Surrendered:
            return interpolate({{0, 12}, {30, 7}, {50, 0}}, age);
        case DiplomacyEventType::PeaceBought:
            return interpolate({{0, 70}, {100, 35}, {250, 0}}, age);
        case DiplomacyEventType::BribedAgainst:
            return interpolate({{0, -60}, {80, -30}, {200, 0}}, age);
        case DiplomacyEventType::AnySurrender:
            return interpolate({{0, 25}, {30, 13}, {45, 0}}, age);
        case DiplomacyEventType::ShipsNearby:
            return interpolate({{0, -100}, {50, -50}, {70, -10}, {100, 0}}, age);
        case DiplomacyEventType::BribedBy:
            return interpolate({{0, 40}, {80, 20}, {200, 0}}, age);
        case DiplomacyEventType::FriendlyGesture:
            return interpolate({{0, 30}, {80, 15}, {200, 0}}, age);
        case DiplomacyEventType::HostileGesture:
            return interpolate({{0, -30}, {80, -15}, {200, 0}}, age);
        case DiplomacyEventType::TradeOffered:
        case DiplomacyEventType::PeaceOffered:
        case DiplomacyEventType::None:
            break;
    }
    return 0;
}

void Diplomacy::resize(std::size_t count) {
    if (count == m_count) {
        return;
    }
    std::vector<Relation> pairs(count * count);
    for (std::size_t a = 0; a < std::min(count, m_count); ++a) {
        for (std::size_t b = 0; b < std::min(count, m_count); ++b) {
            pairs[a * count + b] = m_pairs[a * m_count + b];
        }
    }
    m_pairs = std::move(pairs);
    m_events.resize(count);
    m_blamed.resize(count, kNoPlayer);
    m_count = count;
}

void Diplomacy::reset(const std::vector<Player>& players, const std::vector<Faction>& factions) {
    const auto behaviour = [&](const Player& p) {
        return p.faction < factions.size() ? factions[p.faction].behaviour : FactionBehaviour::Settlers;
    };
    m_count = 0;
    m_pairs.clear();
    m_events.clear();
    m_blamed.clear();
    resize(players.size());
    for (std::size_t a = 0; a < m_count; ++a) {
        for (std::size_t b = 0; b < m_count; ++b) {
            const FactionBehaviour ra = behaviour(players[a]);
            const FactionBehaviour rb = behaviour(players[b]);
            Relation& r = m_pairs[a * m_count + b];
            const bool pirates = ra == FactionBehaviour::Pirates || rb == FactionBehaviour::Pirates;
            // Peace with everyone except the pirates; the pirates are at peace only with the
            // trader and themselves.
            r.peace = pirates && !(ra == FactionBehaviour::Trader || rb == FactionBehaviour::Trader || a == b)
                          ? Treaty::None
                          : Treaty::Signed;
            Treaty trade = Treaty::None;
            switch (ra) {
                case FactionBehaviour::Settlers:
                    trade = acts_diplomatically(rb) ? Treaty::None : Treaty::Signed;
                    break;
                case FactionBehaviour::Trader:
                case FactionBehaviour::Natives:
                    trade = acts_diplomatically(rb) ? Treaty::Signed : Treaty::None;
                    break;
                case FactionBehaviour::Pirates:
                    trade = a == b ? Treaty::Signed : Treaty::None;
                    break;
            }
            r.trade = trade;
        }
    }
}

const Relation& Diplomacy::relation(PlayerId a, PlayerId b) const noexcept {
    static const Relation kNone{};
    return a < m_count && b < m_count ? m_pairs[std::size_t{a} * m_count + b] : kNone;
}

Relation& Diplomacy::relation(PlayerId a, PlayerId b) noexcept {
    if (a < m_count && b < m_count) {
        return m_pairs[std::size_t{a} * m_count + b];
    }
    m_scratch = Relation{};  // a pair that does not exist: written to and forgotten
    return m_scratch;
}

bool Diplomacy::hostile(PlayerId a, PlayerId b) const noexcept {
    if (a == b || a >= m_count || b >= m_count) {
        return false;
    }
    return relation(a, b).peace != Treaty::Signed;
}

bool Diplomacy::may_trade(PlayerId a, PlayerId b) const noexcept {
    if (a == b) {
        return true;
    }
    return a < m_count && b < m_count && relation(a, b).trade == Treaty::Signed;
}

const std::array<DiplomacyEvent, Diplomacy::kEvents>& Diplomacy::events(PlayerId a) const noexcept {
    static const std::array<DiplomacyEvent, kEvents> kNone{};
    return a < m_events.size() ? m_events[a] : kNone;
}

void Diplomacy::set_events(PlayerId a, const std::array<DiplomacyEvent, kEvents>& events) noexcept {
    if (a < m_events.size()) {
        m_events[a] = events;
    }
}

bool Diplomacy::set_peace(const World& world, PlayerId a, PlayerId b, Treaty v, std::uint64_t now_ms) {
    if (a == b || a >= m_count || b >= m_count || relation(a, b).peace == v || v == Treaty::Received) {
        return false;
    }
    const FactionBehaviour ra = behaviour_of(world, a);
    const FactionBehaviour rb = behaviour_of(world, b);
    Relation& ab = relation(a, b);
    Relation& ba = relation(b, a);
    switch (v) {
        case Treaty::None:
            // War cancels a trade treaty, except with the pirates.
            if (ra != FactionBehaviour::Pirates && rb != FactionBehaviour::Pirates &&
                (ab.trade == Treaty::Received || ab.trade == Treaty::Signed)) {
                ab.trade = Treaty::None;
                ba.trade = Treaty::None;
                if (acts_diplomatically(ra)) {
                    m_notices.push({DiplomacyNotice::Kind::TradeEnded, b, a});
                }
                add_event(b, a, DiplomacyEventType::TradeCancelled, now_ms);
            }
            if (acts_diplomatically(ra) && ab.peace == Treaty::Signed) {
                m_notices.push({DiplomacyNotice::Kind::PeaceBroken, b, a});
            }
            // Fix S-113: withdrawing one's own offer goes back to the war there was; the original
            // sends the same value as a declaration of war and b remembers one.
            if (ab.peace != Treaty::Offered) {
                add_event(b, a, DiplomacyEventType::WarDeclared, now_ms);
                relation(b, b).war_time_ms = now_ms;  // the victim's own entry, as the original
            }
            ab.peace = Treaty::None;
            ba.peace = Treaty::None;
            break;
        case Treaty::Offered:
            m_notices.push({DiplomacyNotice::Kind::PeaceOffered, b, a});
            add_event(b, a, DiplomacyEventType::PeaceOffered, now_ms);
            ab.peace = Treaty::Offered;
            ba.peace = Treaty::Received;
            break;
        case Treaty::Signed:
            // Natives and the trader make a trade treaty with the peace.
            if (!acts_diplomatically(ra) && ra != FactionBehaviour::Pirates) {
                ab.trade = Treaty::Signed;
                ba.trade = Treaty::Signed;
            }
            if (acts_diplomatically(ra)) {
                m_notices.push({DiplomacyNotice::Kind::PeaceSigned, b, a});
            }
            ab.peace = Treaty::Signed;
            ba.peace = Treaty::Signed;
            break;
        case Treaty::Received:
            break;
    }
    return true;
}

bool Diplomacy::set_trade(const World& world, PlayerId a, PlayerId b, Treaty v, std::uint64_t now_ms) {
    (void)world;
    if (a == b || a >= m_count || b >= m_count || relation(a, b).trade == v || v == Treaty::Received) {
        return false;
    }
    Relation& ab = relation(a, b);
    Relation& ba = relation(b, a);
    switch (v) {
        case Treaty::None:
            if (ab.trade == Treaty::Signed) {
                m_notices.push({DiplomacyNotice::Kind::TradeEnded, b, a});
            }
            if (ab.trade != Treaty::Offered) {  // Fix S-113: a withdrawn offer ends nothing
                add_event(b, a, DiplomacyEventType::TradeCancelled, now_ms);
            }
            ab.trade = Treaty::None;
            ba.trade = Treaty::None;
            break;
        case Treaty::Offered:
            m_notices.push({DiplomacyNotice::Kind::TradeOffered, b, a});
            add_event(b, a, DiplomacyEventType::TradeOffered, now_ms);
            ab.trade = Treaty::Offered;
            ba.trade = Treaty::Received;
            break;
        case Treaty::Signed:
            m_notices.push({DiplomacyNotice::Kind::TradeSigned, b, a});
            ab.trade = Treaty::Signed;
            ba.trade = Treaty::Signed;
            break;
        case Treaty::Received:
            break;
    }
    return true;
}

void Diplomacy::add_event(PlayerId a, PlayerId b, DiplomacyEventType type, std::uint64_t now_ms) {
    if (a >= m_count || type == DiplomacyEventType::None) {
        return;
    }
    auto& list = m_events[a];
    const auto remove = [&](DiplomacyEventType t) {
        for (DiplomacyEvent& e : list) {
            if (e.type == t && e.partner == b) {
                e = DiplomacyEvent{};
            }
        }
    };
    switch (type) {
        case DiplomacyEventType::Surrendered:
            relation(a, b).damage >>= 3U;
            break;
        case DiplomacyEventType::PeaceBought:
        case DiplomacyEventType::BribedBy:
            remove(DiplomacyEventType::BribedAgainst);
            relation(a, b).damage = 0;
            break;
        case DiplomacyEventType::BribedAgainst:
            remove(DiplomacyEventType::PeaceBought);
            remove(DiplomacyEventType::BribedBy);
            break;
        default:
            break;
    }
    const auto t = static_cast<std::uint8_t>(type);
    if (t >= 6 && t <= 11) {
        for (DiplomacyEvent& e : list) {
            if (e.type == type && e.partner == b) {
                e.time_ms = now_ms;  // refreshed in place
                return;
            }
        }
    }
    // The slot whose event weighs least now; the first empty one wins.
    std::size_t best = 0;
    std::int32_t best_value = 0x7FFFFFFF;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].type == DiplomacyEventType::None) {
            best = i;
            break;
        }
        const std::int32_t v =
            std::abs(diplomacy_event_value(list[i].type, now_ms >= list[i].time_ms ? now_ms - list[i].time_ms : 0));
        if (v < best_value) {
            best_value = v;
            best = i;
        }
    }
    list[best] = DiplomacyEvent{type, b, now_ms};
}

void Diplomacy::add_damage(PlayerId victim, PlayerId attacker, std::uint32_t damage) noexcept {
    if (victim < m_count && attacker < m_count) {
        Relation& r = relation(victim, attacker);
        r.damage = r.damage + damage < r.damage ? 0xFFFFFFFFU : r.damage + damage;
    }
}

void Diplomacy::add_trade(PlayerId a, PlayerId b, std::uint32_t value) noexcept {
    if (a < m_count && b < m_count) {
        Relation& r = relation(a, b);
        r.trade_mood = std::min(kCounterMax, r.trade_mood + value);
    }
}

void Diplomacy::decay(PlayerId a) noexcept {
    if (a >= m_count) {
        return;
    }
    for (std::size_t b = 0; b < m_count; ++b) {
        Relation& r = m_pairs[std::size_t{a} * m_count + b];
        r.damage = static_cast<std::uint32_t>(std::uint64_t{r.damage} * 255 / 256);
        r.tribute = r.tribute * 255 / 256;
        r.trade_mood = r.trade_mood * 255 / 256;
    }
}

std::uint32_t city_development(const City& city) noexcept {
    // 0x0047F220 with the scale 0x80: 32 per class above the first, plus the share of the highest
    // class in 32nds, which counts as full (31) from two thirds on.
    constexpr std::uint32_t kStep = 32;
    const std::size_t classes = std::min<std::size_t>(city.inhabitants.size(), 5);
    std::uint32_t total = 0;
    std::size_t top = 0;
    for (std::size_t c = 0; c < classes; ++c) {
        total += city.inhabitants[c];
        if (city.inhabitants[c] != 0) {
            top = c;
        }
    }
    if (total == 0) {
        return 0;
    }
    std::uint32_t share = static_cast<std::uint32_t>(std::uint64_t{city.inhabitants[top]} * kStep / total);
    if (share >= 2 * kStep / 3) {
        share = kStep - 1;
    }
    return kStep * static_cast<std::uint32_t>(top) + share;
}

std::int32_t Diplomacy::mood(const World& world, PlayerId a, PlayerId b, std::uint32_t extra,
                             std::uint64_t now_ms) const {
    if (a >= m_count || b >= m_count) {
        return 0;
    }
    std::int32_t m = 0;
    for (const DiplomacyEvent& e : m_events[a]) {
        if (e.type != DiplomacyEventType::None && e.partner == b) {
            m += diplomacy_event_value(e.type, now_ms >= e.time_ms ? now_ms - e.time_ms : 0);
        }
    }
    const Relation& r = relation(a, b);
    m -= static_cast<std::int32_t>(std::min<std::uint32_t>(100, r.damage / 4));

    // Territory: computer players and pirates mind others on their small islands.
    const bool minds_territory =
        role_of(world, a) == PlayerRole::Computer || behaviour_of(world, a) == FactionBehaviour::Pirates;
    std::uint64_t size_a = 0;
    std::uint64_t size_b = 0;
    std::uint32_t cities_a = 0;
    std::uint32_t development_a = 0;
    std::uint32_t development_b = 0;
    for (const City& c : world.cities()) {
        std::uint64_t n = 0;
        for (const std::uint32_t i : c.inhabitants) {
            n += i;
        }
        if (c.owner == a) {
            size_a += n;
            ++cities_a;
            development_a = std::max(development_a, city_development(c));
        } else if (c.owner == b) {
            size_b += n;
            development_b = std::max(development_b, city_development(c));
        }
    }
    if (minds_territory) {
        std::int32_t settle_penalty = 0;
        std::int32_t troop_penalty = 0;
        const auto small = [&](const City& c) {
            return c.island < world.islands().size() && world.islands()[c.island].header.template_size <= 3;
        };
        // Who has cities and troops on the islands of a, in one pass over the cities and one over
        // the soldiers instead of one per city of a (TB-8).
        constexpr std::uint8_t kWatched = 1;
        constexpr std::uint8_t kBCity = 2;
        constexpr std::uint8_t kATroops = 4;
        constexpr std::uint8_t kBTroops = 8;
        std::vector<std::uint8_t> islands(world.islands().size(), 0);
        bool any = false;
        for (const City& c : world.cities()) {
            if (c.owner == a && c.island < islands.size()) {
                islands[c.island] = kWatched;
                any = true;
            }
        }
        if (any) {
            for (const City& o : world.cities()) {
                if (o.owner == b && o.island < islands.size() && islands[o.island] != 0) {
                    islands[o.island] |= kBCity;
                }
            }
            for (const Soldier& s : world.soldiers()) {
                if (s.island < islands.size() && islands[s.island] != 0) {
                    islands[s.island] |= s.owner == b ? kBTroops : 0;
                    islands[s.island] |= s.owner == a ? kATroops : 0;
                }
            }
        }
        for (const City& c : world.cities()) {
            if (c.owner != a || c.island >= islands.size()) {
                continue;
            }
            const std::uint8_t seen = islands[c.island];
            const bool b_city = (seen & kBCity) != 0;
            bool empty = true;
            for (const std::uint32_t i : c.inhabitants) {
                empty = empty && i == 0;
            }
            // A city of b only counts on a small island of a; troops of b on any island of a, as
            // in the original and the History Edition (0x00475C60).
            if (b_city && small(c) && (cities_a <= 2 || empty)) {
                settle_penalty = 200;
            }
            const bool b_troops = (seen & kBTroops) != 0;
            const bool a_troops = (seen & kATroops) != 0;
            if (b_troops) {
                troop_penalty = std::max(troop_penalty, a_troops && b_city ? 60 : 200);
            }
        }
        m -= settle_penalty + troop_penalty;
    }

    // The scenario's attitude of a (player +0x70 with its thresholds, only set by scenarios).
    if (const Player* pa = world.player(a); pa != nullptr) {
        const ScenarioSwitches& s = pa->scenario;
        const bool human_b = role_of(world, b) == PlayerRole::Human;
        if (s.has(scenario_flag::kRivalSize) && human_b && size_b > s.rival_inhabitants) {
            m -= kRivalSizePenalty;
        }
        // The original divides by the target even when it is 0 (S-118); no target, no penalty.
        if (s.has(scenario_flag::kOwnSize) && human_b && s.own_inhabitants != 0 && size_a <= s.own_inhabitants) {
            m -= kOwnSizePenalty[static_cast<std::size_t>(size_a * 128 / s.own_inhabitants)];
        }
        if (s.has(scenario_flag::kRivalDevelopment) && human_b && development_b > s.rival_development) {
            m -= development_penalty(static_cast<std::int32_t>(development_b - s.rival_development));
        }
        if (s.has(scenario_flag::kOwnDevelopment) && development_a < s.own_development) {
            m -= development_penalty(static_cast<std::int32_t>(s.own_development - development_a));
        }
    }

    // Tribute and trade count less between big players.
    const auto x = static_cast<std::int32_t>(std::min<std::uint64_t>(127, (size_b * 76 + size_a * 51) >> 14U));
    const auto f = static_cast<std::uint64_t>(
        interpolate({{0, 254}, {1, 213}, {4, 128}, {8, 96}, {12, 64}, {20, 51}, {40, 38}, {70, 25}, {127, 12}}, x));
    m += static_cast<std::int32_t>(std::min<std::uint64_t>(110, (std::uint64_t{r.tribute} + extra) * f >> 12U));
    m += static_cast<std::int32_t>(std::min<std::uint64_t>(70, std::uint64_t{r.trade_mood} * f >> 12U));
    return m;
}

namespace {

/// Money steps of the pirates' prices (0x00428970, 0x00428A30). The original takes the payer's slot
/// number at exactly 5000 gold (S-090); Open1602 counts 5000 with the next step.
std::uint32_t price_base(std::int64_t money, const std::array<std::uint32_t, 7>& steps) noexcept {
    if (money > 70'000) {
        return steps[6];
    }
    if (money > 40'000) {
        return steps[5];
    }
    if (money > 20'000) {
        return steps[4];
    }
    if (money > 10'000) {
        return steps[3];
    }
    if (money >= 5'000) {
        return steps[2];
    }
    return steps[0];
}

PlayerId pirate_player(const World& world) noexcept {
    return world.first_of(FactionBehaviour::Pirates);
}

}  // namespace

bool apply_diplomacy(World& world, const DiplomacyOrder& order) {
    if (order.kind == DiplomacyOrder::Kind::BuyPeace || order.kind == DiplomacyOrder::Kind::Bribe) {
        // The pirate Kontor's window (0x0043CE00): a ship of the payer lies at a pirate Kontor.
        const PlayerId pirates = pirate_player(world);
        Player* payer = world.player(order.player);
        const Ship* ship = world.ship(order.ship);
        if (pirates == kNoPlayer || payer == nullptr || ship == nullptr || ship->owner != order.player) {
            return false;
        }
        bool at_nest = false;
        for (const City& c : world.cities()) {
            if (c.owner == pirates && c.warehouse_x && c.warehouse_y && c.island < world.islands().size()) {
                const WorldIsland& isl = world.islands()[c.island];
                const std::int32_t dx =
                    static_cast<std::int32_t>(ship->x) - static_cast<std::int32_t>(isl.x + *c.warehouse_x);
                const std::int32_t dy =
                    static_cast<std::int32_t>(ship->y) - static_cast<std::int32_t>(isl.y + *c.warehouse_y);
                at_nest = at_nest || (dx * dx + dy * dy <= 7 * 7);
            }
        }
        if (!at_nest) {
            return false;
        }
        Diplomacy& d = world.diplomacy();
        if (order.kind == DiplomacyOrder::Kind::BuyPeace) {
            const std::uint32_t price = pirate_peace_price(world, pirates, order.player);
            if (price == 0 || payer->money < static_cast<std::int64_t>(price)) {
                return false;
            }
            payer->money = money_plus(payer->money, -std::int64_t{price});  // the money is gone (original)
            d.add_event(pirates, order.player, DiplomacyEventType::PeaceBought, world.time_ms());
            pirates_reconsider(world);
            return true;
        }
        const Player* target = world.player(order.target);
        if (target == nullptr || order.target == order.player || !acts_diplomatically(world.behaviour(*target))) {
            return false;
        }
        const std::uint32_t price = pirate_bribe_price(world, pirates, order.player, order.target);
        if (payer->money < static_cast<std::int64_t>(price)) {
            return false;
        }
        payer->money = money_plus(payer->money, -std::int64_t{price});
        d.add_event(pirates, order.player, DiplomacyEventType::BribedBy, world.time_ms());
        d.add_event(pirates, order.target, DiplomacyEventType::BribedAgainst, world.time_ms());
        pirates_reconsider(world);
        return true;
    }
    const Player* self = world.player(order.player);
    const Player* partner = world.player(order.partner);
    if (self == nullptr || partner == nullptr || order.player == order.partner ||
        !acts_diplomatically(world.behaviour(*partner)) || !acts_diplomatically(world.behaviour(*self))) {
        return false;
    }
    Diplomacy& d = world.diplomacy();
    const std::uint64_t now = world.time_ms();
    const auto allowed = [](Treaty current, Treaty v) {
        switch (current) {
            case Treaty::None:
                return v == Treaty::Offered;
            case Treaty::Offered:
                return v == Treaty::None;
            case Treaty::Received:
                return v == Treaty::Signed;
            case Treaty::Signed:
                return v == Treaty::None;
        }
        return false;
    };
    switch (order.kind) {
        case DiplomacyOrder::Kind::Peace:
            return allowed(d.relation(order.player, order.partner).peace, order.value) &&
                   d.set_peace(world, order.player, order.partner, order.value, now);
        case DiplomacyOrder::Kind::Trade:
            return allowed(d.relation(order.player, order.partner).trade, order.value) &&
                   d.set_trade(world, order.player, order.partner, order.value, now);
        case DiplomacyOrder::Kind::Gesture:
            d.add_event(order.partner, order.player,
                        order.friendly ? DiplomacyEventType::FriendlyGesture : DiplomacyEventType::HostileGesture, now);
            return true;
        case DiplomacyOrder::Kind::BuyPeace:
        case DiplomacyOrder::Kind::Bribe:
            return false;  // handled above
        case DiplomacyOrder::Kind::AskFounding:
            return false;  // the computers answer (Simulation::execute)
        case DiplomacyOrder::Kind::Tribute: {
            if (order.gold == 0 || order.gold > DiplomacyOrder::kMaxTribute ||
                self->money < static_cast<std::int64_t>(order.gold)) {
                return false;
            }
            // op 0x49: the counter grows by gold * 32 / (half the base price of Tabak = 17).
            Relation& r = d.relation(order.partner, order.player);
            r.tribute = std::min(kCounterMax, r.tribute + order.gold * 32 / 17);
            if (Player* payer = world.player(order.player)) {
                payer->money = money_plus(payer->money, -std::int64_t{order.gold});
            }
            if (Player* receiver = world.player(order.partner)) {
                receiver->money = money_plus(receiver->money, std::int64_t{order.gold});
            }
            return true;
        }
    }
    return false;
}

std::uint32_t pirate_peace_price(const World& world, PlayerId pirates, PlayerId payer) {
    const Player* p = world.player(payer);
    if (p == nullptr) {
        return 0;
    }
    const std::uint32_t base = price_base(p->money, {500, 500, 1000, 1500, 2000, 4000, 6000});
    const std::int32_t m = world.diplomacy().mood(world, pirates, payer, 0, world.time_ms());
    const std::int32_t factor = m < 0 ? 100 - std::max(m, -60) : 70 - std::min(m, 70);
    const std::uint32_t price = base / 100 * static_cast<std::uint32_t>(factor);
    return price < 100 ? 0 : price;
}

std::uint32_t pirate_bribe_price(const World& world, PlayerId pirates, PlayerId payer, PlayerId target) {
    const Player* p = world.player(payer);
    if (p == nullptr) {
        return 0;
    }
    const std::uint32_t base = price_base(p->money, {1000, 1000, 1500, 2000, 3000, 4000, 6000});
    const std::int32_t mp = world.diplomacy().mood(world, pirates, payer, 0, world.time_ms());
    const std::int32_t mt = world.diplomacy().mood(world, pirates, target, 0, world.time_ms());
    std::uint32_t price = base;
    if (mp < mt) {
        price += base / 100 * static_cast<std::uint32_t>(std::min(100, mt - mp));
    }
    return price;
}

void pirates_reconsider(World& world) {
    const PlayerId pirates = pirate_player(world);
    if (pirates == kNoPlayer) {
        return;
    }
    Diplomacy& d = world.diplomacy();
    for (std::size_t b = 0; b < world.players().size(); ++b) {
        if (!acts_diplomatically(world.behaviour(world.players()[b]))) {
            continue;
        }
        const auto pb = static_cast<PlayerId>(b);
        const std::int32_t m = d.mood(world, pirates, pb, 0, world.time_ms());
        if (m <= 0) {
            d.set_peace(world, pirates, pb, Treaty::None, world.time_ms());
        } else if (m > 20) {
            d.set_peace(world, pirates, pb, Treaty::Signed, world.time_ms());
        }
    }
}

bool operator==(const Diplomacy& a, const Diplomacy& b) noexcept {
    if (a.m_count != b.m_count || a.m_blamed != b.m_blamed) {
        return false;
    }
    for (std::size_t i = 0; i < a.m_pairs.size(); ++i) {
        const Relation& x = a.m_pairs[i];
        const Relation& y = b.m_pairs[i];
        if (x.peace != y.peace || x.trade != y.trade || x.war_time_ms != y.war_time_ms || x.tribute != y.tribute ||
            x.trade_mood != y.trade_mood || x.damage != y.damage) {
            return false;
        }
    }
    for (std::size_t p = 0; p < a.m_events.size(); ++p) {
        for (std::size_t i = 0; i < Diplomacy::kEvents; ++i) {
            const DiplomacyEvent& x = a.m_events[p][i];
            const DiplomacyEvent& y = b.m_events[p][i];
            if (x.type != y.type || x.partner != y.partner || x.time_ms != y.time_ms) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace o1602::sim
