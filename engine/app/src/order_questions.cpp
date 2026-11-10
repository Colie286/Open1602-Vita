// SPDX-License-Identifier: GPL-3.0-or-later
#include "order_questions.hpp"

#include <string_view>
#include <variant>

#include "names.hpp"

#include "o1602/sim/construction.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

sim::PlayerId target_owner(const sim::World& world, const sim::Target& t) {
    switch (t.kind) {
        case sim::Target::Kind::Ship: {
            const sim::Ship* s = world.ship(t.ship);
            return s != nullptr ? s->owner : sim::kNoPlayer;
        }
        case sim::Target::Kind::Soldier: {
            const sim::Soldier* s = world.soldier(t.ship);
            return s != nullptr ? s->owner : sim::kNoPlayer;
        }
        case sim::Target::Kind::Building:
            if (t.island < world.islands().size() &&
                world.islands()[t.island].map.contains(static_cast<std::int32_t>(t.x),
                                                       static_cast<std::int32_t>(t.y))) {
                return world.islands()[t.island].map.top(t.x, t.y).owner;
            }
            return sim::kNoPlayer;
        case sim::Target::Kind::City: {
            const sim::City* c = world.city(t.island, t.city);
            return c != nullptr ? c->owner : sim::kNoPlayer;
        }
        default:
            return sim::kNoPlayer;
    }
}

bool ship_attacks_kontor(const sim::World& world, sim::PlayerId local, sim::PlayerId owner, bool attack_pick) {
    if (owner == local || owner == sim::kNoPlayer) {
        return false;
    }
    return attack_pick || !world.diplomacy().may_trade(local, owner);
}

std::optional<std::string> order_question(const sim::World& world, const assets::BuildingTable& buildings,
                                          const assets::FigureTable& figures, sim::PlayerId local,
                                          const sim::Command& command) {
    if (const auto* d = std::get_if<sim::DemolishOrder>(&command)) {
        // A market, Kontor or tower whose land nothing else covers takes the buildings on it down
        // with it; the original does so without a word (S-035).
        const sim::DemolishLoss loss = sim::demolish_loss(world, buildings, *d);
        if (loss.any()) {
            return ui::tr("dialog.demolish_land", {std::to_string(loss.houses), std::to_string(loss.buildings)});
        }
        return std::nullopt;
    }
    if (const auto* c = std::get_if<sim::ShipCommand>(&command);
        c != nullptr && c->op == sim::ShipCommand::Op::Scuttle) {
        const sim::Ship* s = world.ship(c->ship);
        // Windows-1252, as the fonts of the game have it.
        return ui::tr("dialog.scuttle_ship", {s != nullptr ? ship_name(*s) : std::string()});
    }
    if (const auto* c = std::get_if<sim::SoldierCommand>(&command);
        c != nullptr && c->op == sim::SoldierCommand::Op::Disband) {
        const sim::Soldier* s = world.soldier(c->soldier);
        const assets::FigureType* t = s != nullptr ? figures.get(s->figure) : nullptr;
        const std::uint32_t cost = t != nullptr ? figures.unit_cost(t->kind) : 0;
        return ui::tr("dialog.disband_unit", {std::to_string(cost)});
    }
    if (const auto* o = std::get_if<sim::DiplomacyOrder>(&command);
        o != nullptr && o->kind == sim::DiplomacyOrder::Kind::Peace && o->value == sim::Treaty::None) {
        // Breaking a peace treaty is a declaration of war (the original does not ask, U-033).
        const sim::Diplomacy& d = world.diplomacy();
        const sim::Player* p = world.player(o->partner);
        if (p != nullptr && o->player < d.size() && o->partner < d.size() &&
            d.relation(o->player, o->partner).peace == sim::Treaty::Signed) {
            return ui::tr("dialog.break_peace", {player_name(*p)});
        }
    }
    sim::Target target;
    bool attack = false;
    if (const auto* o = std::get_if<sim::ShipOrder>(&command)) {
        target = o->target;
        attack = o->attack;
    } else if (const auto* so = std::get_if<sim::SoldierOrder>(&command)) {
        target = so->target;
        attack = so->attack;
    }
    if (!attack) {
        return std::nullopt;
    }
    // An attack that means war (0x0044D540): not on the free trader, not at war already.
    const sim::PlayerId other = target_owner(world, target);
    const sim::Player* p = world.player(other);
    const sim::Diplomacy& d = world.diplomacy();
    if (p == nullptr || other == local || world.behaviour(*p) == sim::FactionBehaviour::Trader || local >= d.size() ||
        other >= d.size() || d.relation(local, other).peace == sim::Treaty::None) {
        return std::nullopt;
    }
    return ui::tr("dialog.declare_war", {player_name(*p)});
}

}  // namespace o1602::app
