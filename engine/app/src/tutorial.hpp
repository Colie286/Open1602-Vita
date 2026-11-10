// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tutorial ("Einführungsspiel", docs/re/tutorial.md): five chapters, each a scenario of its own.
// The window above the status bar shows a page of Text.cod [TUTOR] (six lines), and help bubbles
// of BUBBLE.GAD point at what to click next. Every frame the chapter's step is read from the game
// and the interface: which page the window shows and which bubbles are drawn. Only the interface
// changes, never the game, so the simulation stays the same on every computer.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/ui/unit_selection.hpp"

namespace o1602::app {

/// The bubbles of BUBBLE.GAD the steps name (bubble k: the gadgets 111001 + 10 k to + 3).
namespace bubble {
inline constexpr std::uint8_t kWorldMap = 0;
inline constexpr std::uint8_t kZoom = 1;
inline constexpr std::uint8_t kCompass = 2;
inline constexpr std::uint8_t kBuildMode = 3;
inline constexpr std::uint8_t kMilitaryMode = 4;
inline constexpr std::uint8_t kInfoMode = 5;
inline constexpr std::uint8_t kExplore = 6;
inline constexpr std::uint8_t kFoundKontor = 7;
inline constexpr std::uint8_t kTransferButton = 8;
inline constexpr std::uint8_t kFirstCategory = 9;  ///< + build menu button - 31001
inline constexpr std::uint8_t kClickShip = 17;
inline constexpr std::uint8_t kSailToIsland = 18;
inline constexpr std::uint8_t kNoOre = 19;
inline constexpr std::uint8_t kIntoKontor = 20;
inline constexpr std::uint8_t kOntoShip = 21;
inline constexpr std::uint8_t kRightMouse = 22;
inline constexpr std::uint8_t kForester = 23;
inline constexpr std::uint8_t kFisher = 24;
inline constexpr std::uint8_t kRoad = 25;
inline constexpr std::uint8_t kHouse = 26;
inline constexpr std::uint8_t kClickHouse = 27;
inline constexpr std::uint8_t kSheepFarm = 28;
inline constexpr std::uint8_t kNeedCloth = 29;
inline constexpr std::uint8_t kWeaver = 30;
inline constexpr std::uint8_t kMarket = 31;
inline constexpr std::uint8_t kLetGo = 32;
inline constexpr std::uint8_t kShortOfWood = 33;
inline constexpr std::uint8_t kToKontor = 37;
inline constexpr std::uint8_t kEnd = 38;
inline constexpr std::uint8_t kStatusLine = 39;
inline constexpr std::uint8_t kDiplomacy = 40;
inline constexpr std::uint8_t kPartnerKontor = 41;
inline constexpr std::uint8_t kOfferTreaty = 42;
inline constexpr std::uint8_t kConsumerGoods = 43;
inline constexpr std::uint8_t kScrollGoods = 44;
inline constexpr std::uint8_t kSailToPartner = 45;
inline constexpr std::uint8_t kTradeButton = 46;
inline constexpr std::uint8_t kSailHome = 47;
inline constexpr std::uint8_t kSell = 48;
inline constexpr std::uint8_t kBuy = 49;
inline constexpr std::uint8_t kAttackWithLarge = 50;
inline constexpr std::uint8_t kAttackWithSmall = 51;
inline constexpr std::uint8_t kSelectSmall = 52;
inline constexpr std::uint8_t kPatrolButton = 53;
inline constexpr std::uint8_t kPatrolPoint = 54;
inline constexpr std::uint8_t kSailToCoast = 55;
inline constexpr std::uint8_t kUnloadSoldiers = 56;
inline constexpr std::uint8_t kFrameSoldiers = 57;
inline constexpr std::uint8_t kOnlyCavalry = 58;
inline constexpr std::uint8_t kAttackWithCavalry = 59;
inline constexpr std::uint8_t kAttackWithRest = 60;
inline constexpr std::uint8_t kClickLargeShip = 61;
inline constexpr std::uint8_t kCannons = 62;
inline constexpr std::uint8_t kNext = 63;
inline constexpr std::uint8_t kCount = 64;
}  // namespace bubble

/// What a ship can do where it lies (Ship_ContextActions 0x0044E630), as bits.
namespace ship_action {
inline constexpr std::uint8_t kShipyard = 0x01;  ///< an own shipyard in reach
inline constexpr std::uint8_t kTrade = 0x02;     ///< trade or transfer at the Kontor in reach
inline constexpr std::uint8_t kPirates = 0x04;   ///< at the pirates' Kontor
inline constexpr std::uint8_t kOwnCity = 0x08;   ///< the Kontor or the coast in reach is the owner's
inline constexpr std::uint8_t kFound =
    0x10;  ///< land within 4 tiles (sim::kontor_site_near) on an island without a city of the owner
inline constexpr std::uint8_t kIsland = 0x20;    ///< inside an island's rectangle
inline constexpr std::uint8_t kExplored = 0x40;  ///< ... which the owner has explored
}  // namespace ship_action

/// What the interface shows this frame.
struct TutorialView {
    /// The side panel: its mode, the diplomacy window (in the info mode here, a mode of its own in
    /// the original: 30017) or the settings.
    enum class Mode : std::uint8_t { Build, Military, Info, Diplomacy, Settings };
    /// The window of the info mode (0x00633724).
    enum class Window : std::uint8_t { None, Ship, OwnHouse, PartnerKontor, Other };
    /// The page of the ship window (0x0063364C).
    enum class ShipPage : std::uint8_t { Main, Transfer, Trade, Other };

    Mode mode = Mode::Info;
    Window window = Window::None;
    std::uint32_t ship = 0;  ///< Window::Ship: Ship::id
    ShipPage ship_page = ShipPage::Main;
    bool consumer_goods = false;   ///< transfer page: the group of consumer goods is chosen
    std::uint8_t kontor_ware = 0;  ///< transfer page: the ware chosen at the Kontor, 0 none
    /// The military mode's selection (0x0062BDE0, 0x0061FC88).
    std::vector<ui::UnitRef> units;
    bool picking_patrol = false;  ///< the ship's window waits for the patrol's point (0x004894F0 = 5)
    /// The build menu: its pop-up of a category open (bit 0 of 0x004A0C60 clear) and which, and the
    /// building the menu has picked (0x00432970).
    bool popup_open = false;
    std::int32_t popup_category = 0;
    std::optional<std::uint32_t> menu_building;
    bool holding_building = false;  ///< the build tool holds a building to place (0x005B1A68)
    bool short_of_wood = false;     ///< what it would place lacks wood (0x0040D070 = 23)
};

/// What the game holds this frame, as far as the chapter asks (tutorial_facts).
struct TutorialFacts {
    /// A ship of the human.
    struct ShipState {
        bool exists = false;
        bool moving = false;
        bool loaded = false;         ///< anything on board, soldiers too (0x0044E400)
        std::uint8_t actions = 0;    ///< ship_action bits
        bool island_ore = false;     ///< the island whose rectangle it lies in has iron ore
        bool island_city = false;    ///< ... and a city
        std::uint32_t cloth = 0;     ///< on board, 1/32 t
        std::uint32_t bricks = 0;    ///< on board, 1/32 t
        std::uint32_t health = 128;  ///< of 128
        std::uint8_t cannons = 0;
        bool attacking = false;          ///< sails at a ship
        bool attacking_partner = false;  ///< sails at a ship of the partner
        bool patrolling = false;
    };
    /// Chapter 0: the ship of the window; 2 and 4: the human's first ship; 3: his three ships.
    std::array<ShipState, 3> ships{};

    /// Chapter 1: the human's buildings on the island of his town; "connected": a market cart from
    /// the Kontor reaches it on roads (0x004310C0).
    struct Settlement {
        std::uint32_t foresters = 0;
        std::uint32_t fishers = 0;
        bool foresters_connected = false;
        bool fishers_connected = false;
        std::uint32_t houses = 0;
        std::uint32_t sheep_farms = 0;
        std::uint32_t weavers = 0;
        bool weavers_connected = false;
        std::uint32_t markets = 0;
    };
    Settlement settlement;

    /// Chapter 2: the trade treaty with the partner (player 1) and the goods, in 1/32 t.
    bool treaty_offered = false;  ///< offered (either way) or signed
    bool treaty_signed = false;
    std::uint32_t town_cloth = 0;
    std::uint32_t partner_cloth = 0;
    std::uint32_t partner_buys_cloth = 0;    ///< what the partner's Kontor still buys
    std::uint32_t partner_sells_bricks = 0;  ///< what it still sells

    /// Chapters 3 and 4: what the partner has left.
    std::uint32_t partner_ships = 0;
    std::uint32_t partner_units = 0;  ///< on island 0
    /// Chapter 4: the human's land units on island 0 by kind, and how many of them attack a unit
    /// of another player.
    struct Units {
        std::uint32_t total = 0;
        std::uint32_t attacking = 0;
    };
    Units riders;
    Units musketeers;
    Units swordsmen;
    std::uint32_t selected_riders = 0;
};

class Tutorial {
public:
    static constexpr std::uint8_t kChapters = 5;
    static constexpr std::size_t kLinesPerPage = 6;
    /// The partner and opponent of the chapters 2 to 4 (player 1 in the original's code).
    static constexpr sim::PlayerId kPartner = 1;

    explicit Tutorial(std::uint8_t chapter = 0);

    /// Remembers the human's town and ships as the chapter starts (0x0042BB90): the largest city,
    /// the ships in the order of the game's list.
    void start(const sim::World& world, sim::PlayerId human);

    [[nodiscard]] std::uint8_t chapter() const noexcept { return m_chapter; }
    [[nodiscard]] std::int32_t page() const noexcept { return m_page; }
    [[nodiscard]] bool done() const noexcept { return m_done; }
    /// „weiter“: only chapter 0 pages forward by hand, from its first page.
    [[nodiscard]] bool can_page_forward() const noexcept { return m_chapter == 0 && m_page < kLastPaged; }
    void page_forward() noexcept;

    [[nodiscard]] static std::int32_t first_page(std::uint8_t chapter) noexcept;
    [[nodiscard]] static std::int32_t last_page(std::uint8_t chapter) noexcept;
    /// The voice file (Speech8) the chapter starts with (0x00498428).
    [[nodiscard]] static std::uint16_t speech(std::uint8_t chapter) noexcept;
    /// The chapter after `chapter` („Einführungsspiel beenden“): nothing after the last.
    [[nodiscard]] static std::optional<std::uint8_t> next_chapter(std::uint8_t chapter) noexcept;

    /// The step of this frame (0x00431500): sets the page and returns the bubbles to draw.
    std::vector<std::uint8_t> step(const TutorialView& view, const TutorialFacts& facts);

    /// What the step reads from the game.
    [[nodiscard]] TutorialFacts facts(const TutorialView& view, const sim::Simulation& simulation,
                                      const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                                      sim::PlayerId human) const;

    /// The human's ships the chapter started with (0x0049FA3C...), the first is 0x0049F79C.
    [[nodiscard]] const std::vector<std::uint32_t>& ships() const noexcept { return m_ships; }

private:
    static constexpr std::int32_t kLastPaged = 1;  ///< 0x004987F4

    /// Chapter 1's step from the buildings (0x00431330): the page 9 to 18.
    [[nodiscard]] std::int32_t settle_step(const TutorialFacts::Settlement& s) const noexcept;
    void info_discover(const TutorialView& view, const TutorialFacts& facts, std::vector<std::uint8_t>& out);
    void info_settle(const TutorialView& view, std::int32_t step, std::vector<std::uint8_t>& out);
    void build_settle(const TutorialView& view, std::int32_t step, std::vector<std::uint8_t>& out);
    void info_trade(const TutorialView& view, const TutorialFacts& facts, std::vector<std::uint8_t>& out);
    void sea_battle(const TutorialView& view, const TutorialFacts& facts, std::vector<std::uint8_t>& out);
    void land_battle(const TutorialView& view, const TutorialFacts& facts, std::vector<std::uint8_t>& out);
    [[nodiscard]] bool alone(const TutorialView& view, std::size_t ship) const;
    /// The chapter is done (0x0062C188): its last page, which asks to end it.
    void finish() noexcept;

    std::uint8_t m_chapter = 0;
    std::int32_t m_page = 0;
    bool m_done = false;                                                   ///< 0x0062C188
    bool m_house_seen = false;                                             ///< 0x0049FE00
    bool m_cloth_loaded = false;                                           ///< 0x0049FE4C bit 0x40
    bool m_partner_seen = false;                                           ///< bit 0x400
    bool m_bricks_bought = false;                                          ///< bit 0x800
    bool m_cavalry_sent = false;                                           ///< bit 0x01
    std::optional<std::pair<std::uint16_t, std::uint8_t>> m_town;          ///< island, city
    std::optional<std::pair<std::uint16_t, std::uint8_t>> m_partner_town;  ///< of the partner
    std::vector<std::uint32_t> m_ships;
};

}  // namespace o1602::app
