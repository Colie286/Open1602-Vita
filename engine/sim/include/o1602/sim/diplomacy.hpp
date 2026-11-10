// SPDX-License-Identifier: GPL-3.0-or-later
//
// Relations between players (docs/re/diplomacy.md): peace and trade treaties per pair, what one
// player remembers of another (events, damage, tribute, trade) and the mood that follows from it.
//
// The original keeps a 7 x 7 table in its player records (PLAYER4 +0x40 .. +0x1FF). Open1602 keeps
// one entry per ordered pair of any number of players (ADR 0007).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "o1602/sim/event_buffer.hpp"
#include "o1602/sim/player.hpp"

namespace o1602::sim {

class World;
struct City;

/// How far a city has come (0x0047F220 with the scale 0x80): 32 per class above the first that has
/// residents, plus the share of the highest one in 32nds (31 from two thirds on); 0 .. 159.
[[nodiscard]] std::uint32_t city_development(const City& city) noexcept;

/// State of a peace or trade treaty of player a towards player b (the original's values 0..3).
enum class Treaty : std::uint8_t {
    None = 0,      ///< war (peace) or no treaty (trade)
    Offered = 1,   ///< a offered it to b
    Received = 2,  ///< b offered it to a
    Signed = 3,
};

/// Something player a remembers about player b; its weight fades with age (Dipl_EventValue).
enum class DiplomacyEventType : std::uint8_t {
    None = 0,
    TradeCancelled = 1,  ///< b ended a trade treaty with a
    TradeOffered = 2,
    WarDeclared = 3,
    PeaceOffered = 4,
    Settled = 5,           ///< b founded a city on an island of a computer player a
    Surrendered = 6,       ///< (pirates) a ship of b surrendered to them
    PeaceBought = 7,       ///< (pirates) b paid for peace
    BribedAgainst = 8,     ///< (pirates) someone paid them to fight b
    AnySurrender = 9,      ///< (pirates) any surrender, given towards every player
    ShipsNearby = 10,      ///< (computer) ships of b hang around a's city
    BribedBy = 11,         ///< (pirates) b paid them to fight someone
    FriendlyGesture = 12,  ///< b made a friendly gesture
    HostileGesture = 13,
};

struct DiplomacyEvent {
    DiplomacyEventType type = DiplomacyEventType::None;
    PlayerId partner = kNoPlayer;
    std::uint64_t time_ms = 0;  ///< game time when it happened (or was refreshed)
};

/// What player a has towards player b.
struct Relation {
    Treaty peace = Treaty::Signed;
    Treaty trade = Treaty::None;
    /// The original's +0xF4: when war was last declared on a. As in the original, only a's own
    /// entry relation(a, a) is written; the pair entries keep what a savegame brought. Nothing
    /// reads it (docs/re/diplomacy.md); it is kept for the savegame round trip.
    std::uint64_t war_time_ms = 0;
    std::uint32_t tribute = 0;     ///< tribute received from b, scaled; decays
    std::uint32_t trade_mood = 0;  ///< trade (and plunder) with b; decays
    std::uint32_t damage = 0;      ///< damage received from b (1/32 HP); decays
};

/// Messages for the players.
struct DiplomacyNotice {
    enum class Kind : std::uint8_t {
        PeaceBroken,   ///< MELDUNG 0x4D
        PeaceOffered,  ///< 0x4E
        PeaceSigned,   ///< 0x4F
        TradeEnded,    ///< 0x50
        TradeOffered,  ///< 0x51
        TradeSigned,   ///< 0x52
        // A computer's answer to a ship that is about to found a city on its island (0x004294C0):
        FoundingProtest,    ///< 0x49 „Euer Gegner protestiert gegen Bauvorhaben unter Kriegsandrohung!“
        FoundingRefused,    ///< 0x4A „Euer Gegner toleriert Stadtgründung zu diesem Zeitpunkt nicht!“
        FoundingTolerated,  ///< 0x4B „Euer Gegner würde die Stadtgründung tolerieren.“
    };
    Kind kind = Kind::PeaceOffered;
    PlayerId to = kNoPlayer;    ///< the player who gets the message
    PlayerId from = kNoPlayer;  ///< the player who acted
    std::uint16_t island = 0;   ///< the founding answers: the computer's city on the island
    std::uint8_t city = 0;
};

class Diplomacy {
public:
    static constexpr std::size_t kEvents = 32;
    /// Events the original saves per player (PLAYER4 +0x1C0); Open1602 keeps all in its own chunk (S-085).
    static constexpr std::size_t kSavedEvents = 7;

    /// Default relations of a new game (0x00474380): peace between everyone except with the
    /// pirates, trade treaties of the players with the trader, the pirates and the natives.
    void reset(const std::vector<Player>& players, const std::vector<Faction>& factions);
    /// Makes room for `count` players; new pairs start at peace without trade treaty.
    void resize(std::size_t count);
    [[nodiscard]] std::size_t size() const noexcept { return m_count; }

    [[nodiscard]] const Relation& relation(PlayerId a, PlayerId b) const noexcept;
    [[nodiscard]] Relation& relation(PlayerId a, PlayerId b) noexcept;

    /// Whether units of a fight units of b: anything but a signed peace (war or a pending offer).
    /// Nobody fights himself or "nobody".
    [[nodiscard]] bool hostile(PlayerId a, PlayerId b) const noexcept;
    /// Whether ships of a may trade at Kontors of b (Ship_ActionMask 0x0044E630).
    [[nodiscard]] bool may_trade(PlayerId a, PlayerId b) const noexcept;

    [[nodiscard]] PlayerId blamed(PlayerId a) const noexcept { return a < m_blamed.size() ? m_blamed[a] : kNoPlayer; }
    void set_blamed(PlayerId a, PlayerId by) noexcept {
        if (a < m_blamed.size()) {
            m_blamed[a] = by;
        }
    }
    [[nodiscard]] const std::array<DiplomacyEvent, kEvents>& events(PlayerId a) const noexcept;
    void set_events(PlayerId a, const std::array<DiplomacyEvent, kEvents>& events) noexcept;

    /// Peace treaty: player a sends value v towards b (op 0x2F): None = war (or withdraws an
    /// offer, or breaks the treaty), Offered = offer, Signed = accept. Returns whether anything
    /// changed.
    bool set_peace(const World& world, PlayerId a, PlayerId b, Treaty v, std::uint64_t now_ms);
    /// Trade treaty (op 0x30), same values.
    bool set_trade(const World& world, PlayerId a, PlayerId b, Treaty v, std::uint64_t now_ms);
    /// Adds an event to a's memory about b (Dipl_AddEvent 0x00477390).
    void add_event(PlayerId a, PlayerId b, DiplomacyEventType type, std::uint64_t now_ms);
    /// Damage player `victim` received from `attacker` (hits on ships, soldiers, buildings).
    void add_damage(PlayerId victim, PlayerId attacker, std::uint32_t damage) noexcept;
    /// Kontor trade worth `gold` between them (op 0x4A): friendlier both ways.
    void add_trade(PlayerId a, PlayerId b, std::uint32_t value) noexcept;

    /// How a feels about b (Dipl_Mood 0x00475C60): events, damage, territory, the attitude a
    /// scenario gives a (Player::scenario), tribute and trade. `extra` is tribute being offered
    /// (scaled like the counter).
    [[nodiscard]] std::int32_t mood(const World& world, PlayerId a, PlayerId b, std::uint32_t extra,
                                    std::uint64_t now_ms) const;
    /// The 5-second decay of the counters of player a (Player_Tick 0x00476350): x 255/256.
    void decay(PlayerId a) noexcept;

    [[nodiscard]] std::vector<DiplomacyNotice> take_notices() { return m_notices.take(); }
    /// A message from outside the treaties (a computer's answer to a founding, ComputerPlayers).
    void notify(const DiplomacyNotice& notice) { m_notices.push(notice); }

    friend bool operator==(const Diplomacy& a, const Diplomacy& b) noexcept;

private:
    std::size_t m_count = 0;
    std::vector<Relation> m_pairs;  ///< a * count + b
    Relation m_scratch;             ///< what relation() gives for a pair that does not exist
    std::vector<std::array<DiplomacyEvent, kEvents>> m_events;
    std::vector<PlayerId> m_blamed;  ///< the original's +0x81: last player who destroyed a's Kontor/market
    EventBuffer<DiplomacyNotice> m_notices;
};

/// A player's diplomatic action (the diplomacy panel 0x0043CE00, docs/re/diplomacy.md).
struct DiplomacyOrder {
    static constexpr std::uint32_t kMaxTribute = 10'000;  ///< gold of one tribute at most
    enum class Kind : std::uint8_t {
        Peace,     ///< `value`: Offered (offer), Signed (accept an offer), None (withdraw or break)
        Trade,     ///< the same for the trade treaty
        Gesture,   ///< `friendly` or not: remembered by the partner
        Tribute,   ///< `gold` (at most kMaxTribute) goes to the partner and makes him friendlier
        BuyPeace,  ///< with `ship` at a pirate Kontor: pay the pirates for peace (B.6)
        Bribe,     ///< with `ship` at a pirate Kontor: pay the pirates to fight `target`
        /// „Kontor gründen“ with `ship` by an island (island order 0x4C): a computer with a city there
        /// answers whether it tolerates the founding (Simulation, ComputerPlayers::founding_query).
        AskFounding,
    };
    PlayerId player = kNoPlayer;
    PlayerId partner = kNoPlayer;
    Kind kind = Kind::Peace;
    Treaty value = Treaty::Offered;
    bool friendly = true;
    std::uint32_t gold = 0;
    std::uint32_t ship = 0;       ///< BuyPeace, Bribe: Ship::id lying at the pirates' Kontor; AskFounding
    PlayerId target = kNoPlayer;  ///< Bribe: whom the pirates go after
};

/// What the pirates want for peace (0x00428970): a base by the payer's money (500 to 6000), times a
/// factor by their mood towards him. 0: nothing to pay.
[[nodiscard]] std::uint32_t pirate_peace_price(const World& world, PlayerId pirates, PlayerId payer);
/// What they want to fight `target` (0x00428A30): a base by the payer's money (1000 to 6000), more if
/// they like the target better than the payer.
[[nodiscard]] std::uint32_t pirate_bribe_price(const World& world, PlayerId pirates, PlayerId payer, PlayerId target);
/// The pirates look at every player again (Pirates_ReconsiderPeace 0x00476230, every 5 s): war
/// with whom they do not like (mood 0 or less), peace with whom they like (more than 20).
void pirates_reconsider(World& world);

/// Carries out a diplomatic action if the panel would allow it: only towards human and computer
/// players, and only the steps the treaty state offers. Returns whether it was carried out.
bool apply_diplomacy(World& world, const DiplomacyOrder& order);

/// Current weight of an event of this type and age (0x00475FE0).
[[nodiscard]] std::int32_t diplomacy_event_value(DiplomacyEventType type, std::uint64_t age_ms) noexcept;

}  // namespace o1602::sim
