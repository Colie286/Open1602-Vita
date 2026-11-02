// SPDX-License-Identifier: GPL-3.0-or-later
//
// The build orders of the computer players' stages as data (MD-4, docs/modding/computer-players.md):
// each stage of a city tries what it needs in a fixed order and stops at the first entry that finds
// something to do. The original's orders are the defaults; a package's computers.toml replaces an
// order or adds entries after it. A game keeps the orders it changed (Balance::computer_orders,
// section AIOR of the savegame).
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "o1602/assets/goods.hpp"

namespace o1602::sim {

/// One entry of an order.
struct ComputerNeed {
    enum class Kind : std::uint8_t {
        Building,      ///< n producers of slot `slot`
        Market,        ///< markets until the land reaches (n: the original's parameter)
        Houses,        ///< n houses
        Public,        ///< the public building of slot `slot`, n of them; `flag`: also where covered
        Kontor,        ///< the Kontor at level n
        Castle,        ///< a castle
        Monument,      ///< a monument
        Shipyard,      ///< a shipyard
        Processing,    ///< the workshops of the raw materials it has
        IronTools,     ///< iron and tools
        Weapons,       ///< weapons
        Luxury,        ///< n fields of luxury crops; `flag`: one crop only
        ColonyLuxury,  ///< one luxury field per n tiles of land
        Tower,         ///< n towers
        RoadsIfMason,  ///< stone roads once it has a stonemason
        Colony,        ///< a colony (n: the mode)
        Package,       ///< n producers of the package's good `slot` (a ware number)
        Crop,          ///< first_crops: the crop (`slot`, a field ware, or ore) a computer's first colony grows
    };
    static constexpr std::uint8_t kKinds = 18;
    /// A condition on the gold the player had when the stage ran.
    enum class When : std::uint8_t { Never, Always, Gold4000, Gold5000, Gold8000 };
    static constexpr std::uint8_t kWhens = 5;
    Kind kind = Kind::Building;
    std::uint8_t slot = 0;
    std::uint32_t n = 0;
    When flag = When::Never;   ///< Public: also where its service covers the houses; Luxury: one crop only
    When when = When::Always;  ///< the entry is tried only then

    friend bool operator==(const ComputerNeed&, const ComputerNeed&) = default;
};

/// The orders of the stages, by their names in computers.toml.
enum class ComputerList : std::uint8_t {
    Founded,
    Colony,
    ColonyMaterials,
    Pioneers,
    Settlers,
    Second,
    SecondLuxury,
    SecondFarming,
    Main,
    MainRich,
    Main2,
    Citizens,
    Citizens2,
    CitizensSmall,
    Merchants,
    MerchantsLuxury,
    MerchantsTail,
    Merchants2,
    Merchants2Tail,
    MerchantsSmall,
    MerchantsSmallTail,
    Aristocrats,
    Aristocrats2,
    /// Not a stage: the crops of the computers' first colonies, the computer of player p taking
    /// entry p modulo their number (Kind::Crop).
    FirstCrops,
};
inline constexpr std::size_t kComputerLists = 24;
/// The names of the lists in computers.toml, by ComputerList.
extern const std::array<std::string_view, kComputerLists> kComputerListNames;

/// An order a game changed.
struct ComputerOrder {
    ComputerList list = ComputerList::Founded;
    std::vector<ComputerNeed> needs;
    friend bool operator==(const ComputerOrder&, const ComputerOrder&) = default;
};

/// At most so many entries in an order, and so much of anything an entry asks for.
inline constexpr std::size_t kMaxOrderNeeds = 64;
inline constexpr std::uint32_t kMaxOrderCount = 1000;

/// The original's order of `list`.
[[nodiscard]] std::span<const ComputerNeed> default_order(ComputerList list);

/// The order of `list` in `orders` (a game's changed ones), else the original's.
[[nodiscard]] std::span<const ComputerNeed> order_of(const std::vector<ComputerOrder>& orders, ComputerList list);

/// Whether `need` can be run: a known kind and condition, a slot its kind knows, a count in range.
[[nodiscard]] bool valid(const ComputerNeed& need) noexcept;
/// Whether every order is valid, has at most kMaxOrderNeeds entries and the orders are sorted by
/// their list, each list once (a savegame's are checked with it).
[[nodiscard]] bool valid(const std::vector<ComputerOrder>& orders) noexcept;

/// The crop (a field ware, or ore) the first colony of the computer of `player` looks for, by the
/// list first_crops of `orders`; 0 (any island) without one.
[[nodiscard]] std::uint8_t first_crop(const std::vector<ComputerOrder>& orders, std::uint32_t player);

/// The file of a package with its orders.
inline constexpr std::string_view kComputersFile = "computers.toml";

/// Reads computers.toml of `package` into `orders`: a table names a stage's order (`[settlers]`);
/// `order = "..."` replaces it, `append = "..."` adds entries after it. An entry that cannot be read
/// is passed over with a warning that names the file and the line, the rest of the order is kept.
/// `goods`: the goods a `<package>:<name>` entry may name.
void apply_computers_file(std::vector<ComputerOrder>& orders, std::string_view package, std::string_view utf8,
                          std::vector<std::string>& warnings, const assets::Goods& goods);

/// An order as computers.toml writes it, for messages and tests; `goods` names the packages' goods.
[[nodiscard]] std::string order_text(std::span<const ComputerNeed> needs, const assets::Goods& goods);

}  // namespace o1602::sim
