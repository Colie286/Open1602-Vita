// SPDX-License-Identifier: GPL-3.0-or-later
//
// The goods a game trades, Open1602's own: the original's (EISENERZ to ZIEGEL, game_data.hpp) and
// those content packages add in `goods.toml` (docs/modding/goods.md, MD-3). A good of a package
// gets a ware number after the original's (from ware::kFirstOwn), a price, the icon of a good of
// the original, optionally an icon of a package that it shows instead, and names by language. A game keeps its goods
// (section GOOD of the savegame), so it plays with them whatever the packages installed later say.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/assets/game_data.hpp"
#include "o1602/assets/localized_names.hpp"
#include "o1602/assets/package_graphics.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

namespace ware {
/// The first ware number a package's good gets: after everything the original numbers.
inline constexpr std::uint8_t kFirstOwn = kLastField + 1;
/// The last one: ware numbers are bytes in the original's records and savegames.
inline constexpr std::uint8_t kLastOwn = 255;
}  // namespace ware

/// Whether `w` can be a trade good at all: one of the original's or a number of a package's good.
/// For code without the game's Goods at hand; what is in a hold or a store is always a good of the game.
[[nodiscard]] constexpr bool is_good_number(std::uint32_t w) noexcept {
    return is_trade_ware(w) || (w >= ware::kFirstOwn && w <= ware::kLastOwn);
}

/// The file of a package that adds goods.
inline constexpr std::string_view kGoodsFile = "goods.toml";

/// A good a package added.
struct OwnGood {
    std::uint8_t number = 0;  ///< its ware number in this game
    std::string id;           ///< "<package>:<name>", UTF-8
    std::uint16_t price = 0;  ///< base price per ton, as kBasePrices
    std::uint8_t icon = 0;    ///< the good of the original whose icon it shows without `picture`
    /// The icon of a package it shows ("<package>:<name>", icons.toml); empty for none. Where no
    /// package has that icon (a game played without it), `icon` stands in.
    std::string picture;
    /// Its name by language code ("en", "de"), Windows-1252 as the game's fonts take it.
    LocalizedNames names;

    /// The name in `language`, else in English, else the first given, else the id.
    [[nodiscard]] std::string name(std::string_view language) const;
    friend bool operator==(const OwnGood&, const OwnGood&) = default;
};

/// The goods of a game: the original's trade goods and those packages added.
class Goods {
public:
    /// Whether `w` is a good that Kontors store and ships trade (is_trade_ware for the original's).
    [[nodiscard]] bool is_trade(std::uint32_t w) const noexcept;
    /// Base price per ton of a trade good, 0 for any other ware number.
    [[nodiscard]] std::uint16_t price(std::uint32_t w) const noexcept;
    /// The trade goods in the order panels list them: the original's, then the packages'.
    [[nodiscard]] const std::vector<std::uint8_t>& trade_wares() const noexcept { return m_trade; }
    /// One past the highest trade ware number: what a list indexed by ware number needs.
    [[nodiscard]] std::size_t end() const noexcept { return m_trade.empty() ? 0 : std::size_t{m_trade.back()} + 1; }
    /// The good a package added with ware number `w`, nullptr for the original's.
    [[nodiscard]] const OwnGood* own(std::uint32_t w) const noexcept;
    /// The good a package added by its id, nullptr if none is called so.
    [[nodiscard]] const OwnGood* own(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<OwnGood>& own_goods() const noexcept { return m_own; }
    /// The original's good whose icon `w` shows: `w` itself for the original's.
    [[nodiscard]] std::uint8_t icon(std::uint32_t w) const noexcept;
    /// The icon of a package `w` shows ("<package>:<name>"), empty if none.
    [[nodiscard]] std::string_view picture(std::uint32_t w) const noexcept;
    /// Gives the good a package added with ware number `w` the icon `picture`; false if there is no
    /// such good.
    bool set_picture(std::uint32_t w, std::string picture);

    /// Adds `good` with the next free ware number (the one it has is replaced); fails when none is
    /// left or a good of that id is there already.
    core::Result<std::uint8_t> add(OwnGood good);
    /// Adds `good` with the number it has (a savegame's): fails when the number is no package's,
    /// is taken, or the good is wrong (no id, no price, no icon of the original's).
    core::Status restore(OwnGood good);

    friend bool operator==(const Goods& a, const Goods& b) { return a.m_own == b.m_own; }

private:
    void list(std::uint8_t number);

    std::vector<OwnGood> m_own;  ///< by ware number
    std::vector<std::uint8_t> m_trade = [] {
        std::vector<std::uint8_t> t;
        for (std::uint32_t w = ware::kFirstTrade; w <= ware::kLastTrade; ++w) {
            t.push_back(static_cast<std::uint8_t>(w));
        }
        return t;
    }();
};

/// Reads `goods.toml` of `package` and adds its goods to `goods`, in the order of the file. A
/// table that cannot be read is passed over with a warning; a file that is no data file adds nothing.
/// `icons`: the packages' icons a good may show (`picture`).
void apply_goods_file(Goods& goods, std::string_view package, std::string_view utf8, std::vector<std::string>& warnings,
                      std::span<const PackageIcon> icons = {});

/// The ware number a string id stands for: "anno1602:<name or number>" of the original's trade goods
/// (original_names.hpp) or "<package>:<name>" of one in `goods`; nothing for any other.
[[nodiscard]] std::optional<std::uint8_t> ware_of(const Goods& goods, std::string_view id);

}  // namespace o1602::assets
