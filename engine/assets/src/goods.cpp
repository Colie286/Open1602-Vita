// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/goods.hpp"

#include <algorithm>
#include <charconv>
#include <optional>

#include "o1602/assets/localized_names.hpp"
#include "o1602/assets/original_names.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/text.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its good: lower case letters, digits and '_', as the original's.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

constexpr std::uint16_t kMaxPrice = 10000;

}  // namespace

std::string OwnGood::name(std::string_view language) const {
    return name_in(names, language, id);
}

bool Goods::is_trade(std::uint32_t w) const noexcept {
    return is_trade_ware(w) || own(w) != nullptr;
}

std::uint16_t Goods::price(std::uint32_t w) const noexcept {
    if (const OwnGood* g = own(w)) {
        return g->price;
    }
    return is_trade_ware(w) ? base_price(static_cast<std::uint8_t>(w)) : std::uint16_t{0};
}

const OwnGood* Goods::own(std::uint32_t w) const noexcept {
    const auto it = std::find_if(m_own.begin(), m_own.end(), [&](const OwnGood& g) { return g.number == w; });
    return it == m_own.end() ? nullptr : &*it;
}

const OwnGood* Goods::own(std::string_view id) const noexcept {
    const auto it = std::find_if(m_own.begin(), m_own.end(), [&](const OwnGood& g) { return g.id == id; });
    return it == m_own.end() ? nullptr : &*it;
}

std::uint8_t Goods::icon(std::uint32_t w) const noexcept {
    if (const OwnGood* g = own(w)) {
        return g->icon;
    }
    return static_cast<std::uint8_t>(w);
}

std::string_view Goods::picture(std::uint32_t w) const noexcept {
    const OwnGood* g = own(w);
    return g != nullptr ? std::string_view(g->picture) : std::string_view{};
}

bool Goods::set_picture(std::uint32_t w, std::string picture) {
    const auto it = std::find_if(m_own.begin(), m_own.end(), [&](const OwnGood& g) { return g.number == w; });
    if (it == m_own.end()) {
        return false;
    }
    it->picture = std::move(picture);
    return true;
}

void Goods::list(std::uint8_t number) {
    m_trade.insert(std::upper_bound(m_trade.begin(), m_trade.end(), number), number);
}

core::Result<std::uint8_t> Goods::add(OwnGood good) {
    if (own(std::string_view(good.id)) != nullptr) {
        return core::make_error(core::ErrorCode::Unsupported, good.id + " is there already");
    }
    for (std::uint32_t w = ware::kFirstOwn; w <= ware::kLastOwn; ++w) {
        if (own(w) == nullptr) {
            good.number = static_cast<std::uint8_t>(w);
            const std::uint8_t number = good.number;
            m_own.push_back(std::move(good));
            list(number);
            return number;
        }
    }
    return core::make_error(core::ErrorCode::Unsupported, "no ware number is free: a game has at most " +
                                                              std::to_string(ware::kLastOwn - ware::kFirstOwn + 1) +
                                                              " goods of packages");
}

core::Status Goods::restore(OwnGood good) {
    if (good.number < ware::kFirstOwn || own(good.number) != nullptr || own(std::string_view(good.id)) != nullptr ||
        good.id.empty() || good.price == 0 || good.price > kMaxPrice || !is_trade_ware(good.icon)) {
        return core::make_error(core::ErrorCode::InvalidFormat, "a good that cannot be: " + good.id);
    }
    const std::uint8_t number = good.number;
    m_own.push_back(std::move(good));
    list(number);
    return core::Ok{};
}

std::optional<std::uint8_t> ware_of(const Goods& goods, std::string_view id) {
    const std::size_t colon = id.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    if (id.substr(0, colon) == "anno1602") {
        const std::string_view after = id.substr(colon + 1);
        if (const OriginalName* n = find_original_name(original_ware_names(), after)) {
            return static_cast<std::uint8_t>(n->first);
        }
        // Or by its number, as the original's files name it.
        std::uint32_t number = 0;
        const auto [end, error] = std::from_chars(after.data(), after.data() + after.size(), number);
        if (error == std::errc{} && end == after.data() + after.size() && is_trade_ware(number)) {
            return static_cast<std::uint8_t>(number);
        }
        return std::nullopt;
    }
    if (const OwnGood* g = goods.own(id)) {
        return g->number;
    }
    return std::nullopt;
}

void apply_goods_file(Goods& goods, std::string_view package, std::string_view utf8, std::vector<std::string>& warnings,
                      std::span<const PackageIcon> icons) {
    const std::string where = std::string(package) + "/" + std::string(kGoodsFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: a good's name holds only lower case letters, digits and '_'");
            continue;
        }
        OwnGood good;
        good.id = std::string(package) + ":" + t.name;
        if (goods.own(std::string_view(good.id)) != nullptr) {
            warnings.push_back(at + good.id + " is declared twice; the second is passed over");
            continue;
        }
        bool ok = true;
        for (const core::DataEntry& e : t.entries) {
            const std::string field = line_of(where, e.value.line) + "'" + e.key + "' ";
            if (e.key == "price") {
                const std::int64_t* n = e.value.integer();
                if (n == nullptr || *n < 1 || *n > kMaxPrice) {
                    warnings.push_back(field + "is a number from 1 to " + std::to_string(kMaxPrice));
                    ok = false;
                } else {
                    good.price = static_cast<std::uint16_t>(*n);
                }
            } else if (e.key == "icon") {
                const std::string* s = e.value.string();
                const auto w = s != nullptr ? ware_of(Goods{}, *s) : std::nullopt;
                if (!w) {
                    warnings.push_back(field + "names no good of the original (anno1602:<name or number>)");
                    ok = false;
                } else {
                    good.icon = *w;
                }
            } else if (e.key == "picture") {
                const std::string* s = e.value.string();
                if (s == nullptr || find_package_icon(icons, *s) == nullptr) {
                    warnings.push_back(field + "names no icon of a package (icons.toml, <package>:<name>)");
                    ok = false;
                } else {
                    good.picture = *s;
                }
            } else if (const std::string_view language = name_key_language(e.key); !language.empty()) {
                if (e.value.string() == nullptr || e.value.string()->empty()) {
                    warnings.push_back(field + "is a text");
                    ok = false;
                } else {
                    good.names.emplace_back(language, core::utf8_to_cp1252(*e.value.string()));
                }
            } else {
                warnings.push_back(field + "is no field of a good and is passed over");
            }
        }
        if (!ok) {
            continue;
        }
        if (good.price == 0 || good.icon == 0 || good.names.empty()) {
            warnings.push_back(at + good.id + " needs a 'price', an 'icon' and a name ('name.en')");
            continue;
        }
        if (auto added = goods.add(std::move(good)); !added) {
            warnings.push_back(at + added.error().message);
        }
    }
}

}  // namespace o1602::assets
