// SPDX-License-Identifier: GPL-3.0-or-later
#include "diplomacy_strip.hpp"

#include <algorithm>

#include "names.hpp"

#include "o1602/sim/diplomacy.hpp"
#include "o1602/sim/world.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app::diplomacy {

std::array<std::int32_t, 2> strip_slot(std::size_t index, std::size_t count) noexcept {
    count = std::max<std::size_t>(count, 1);
    index = std::min(index, count - 1);
    // The rows split the partners evenly, the larger ones on top (7: 4 + 3; 13: 5 + 4 + 4).
    const std::size_t rows = strip_rows(count);
    const std::size_t base = count / rows;
    const std::size_t larger = count % rows;  // rows with base + 1
    std::size_t row = 0;
    std::size_t first = 0;  // the index the row starts with
    while (row + 1 < rows && index >= first + base + (row < larger ? 1 : 0)) {
        first += base + (row < larger ? 1 : 0);
        ++row;
    }
    const std::size_t in_row = base + (row < larger ? 1 : 0);
    const std::size_t column = index - first;
    const std::int32_t width = static_cast<std::int32_t>(in_row) * kFrameWidth;
    const std::int32_t x = kPanelLeft + (kPanelWidth - width) / 2 + static_cast<std::int32_t>(column) * kFrameWidth;
    const std::int32_t y = rows == 1 ? kSingleRowY : kFirstRowY + static_cast<std::int32_t>(row) * kRowStep;
    return {x, y};
}

std::vector<sim::PlayerId> partners(const sim::World& world, sim::PlayerId local) {
    std::vector<sim::PlayerId> out;
    const auto& players = world.players();
    for (std::size_t i = 0; i < players.size(); ++i) {
        const auto id = static_cast<sim::PlayerId>(i);
        // A defeated opponent has nothing left to make treaties with: it leaves the window.
        if (id != local && players[i].active() &&
            (players[i].role == sim::PlayerRole::Human || players[i].role == sim::PlayerRole::Computer)) {
            out.push_back(id);
        }
    }
    return out;
}

std::vector<Offer> open_offers(const sim::World& world, sim::PlayerId local) {
    std::vector<Offer> out;
    const sim::Diplomacy& d = world.diplomacy();
    if (local >= d.size()) {
        return out;
    }
    for (const sim::PlayerId b : partners(world, local)) {
        if (b >= d.size()) {
            continue;
        }
        const sim::Relation& r = d.relation(local, b);
        Offer o{b, r.trade == sim::Treaty::Received, r.peace == sim::Treaty::Received};
        if (o.trade || o.peace) {
            out.push_back(o);
        }
    }
    return out;
}

std::vector<sim::PlayerId> enemies(const sim::World& world, sim::PlayerId player) {
    std::vector<sim::PlayerId> out;
    const sim::Diplomacy& d = world.diplomacy();
    if (player >= d.size()) {
        return out;
    }
    for (const sim::PlayerId b : partners(world, player)) {
        const sim::Player* p = world.player(b);
        if (b < d.size() && p != nullptr && p->active() && d.relation(player, b).peace != sim::Treaty::Signed) {
            out.push_back(b);
        }
    }
    return out;
}

std::string wars_text(const sim::World& world, sim::PlayerId player) {
    const sim::Player* p = world.player(player);
    if (p == nullptr) {
        return {};
    }
    std::string names;
    for (const sim::PlayerId b : enemies(world, player)) {
        names += (names.empty() ? "" : ", ") + player_name(*world.player(b));
    }
    return names.empty() ? ui::tr("diplomacy.no_wars", {player_name(*p)})
                         : ui::tr("diplomacy.wars", {player_name(*p), names});
}

std::string offer_text(const Offer& offer, const std::string& name) {
    if (offer.trade && offer.peace) {
        return ui::tr("diplomacy.offer.both", {name});
    }
    return ui::tr(offer.trade ? "diplomacy.offer.trade" : "diplomacy.offer.peace", {name});
}

}  // namespace o1602::app::diplomacy
