// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the sections read, checked against the world and applied to it: only when everything fits,
// so a damaged file leaves the world as it was. What a file of an earlier version lacks stays as the
// original's chunks gave it.
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

#include "section_io.hpp"

#include "o1602/assets/chunk_file.hpp"

namespace o1602::sim::save {

using core::ErrorCode;
using core::make_error;

namespace {

bool island_ok(const World& w, std::uint16_t island) {
    return island < w.islands().size();
}

core::Status check(const World& w, const Decoded& d) {
    const auto bad = [](const char* what) {
        return make_error(ErrorCode::InvalidFormat, std::string("O1602: ") + what);
    };
    if (d.islands && d.islands->size() != w.islands().size()) {
        return bad("islands do not match");
    }
    if (d.islands) {
        for (std::size_t i = 0; i < w.islands().size(); ++i) {
            if ((*d.islands)[i].number != w.islands()[i].header.number) {
                return bad("islands do not match");
            }
        }
    }
    // Sections that complete others must match them in size. A damaged file is refused instead of
    // losing the section without a word (a DIPL of another size would load as peace for everyone).
    if (d.explored && d.explored->size() != w.islands().size()) {
        return bad("explored islands do not match");
    }
    if (d.explored) {
        for (std::size_t i = 0; i < w.islands().size(); ++i) {
            const std::size_t deposits =
                d.islands ? (*d.islands)[i].deposits.size() : w.islands()[i].header.deposits.size();
            if ((*d.explored)[i].deposit_masks.size() != deposits) {
                return bad("deposits do not match");
            }
        }
    }
    const std::size_t players = d.players ? d.players->size() : w.players().size();
    if ((d.player_state && d.player_state->size() != players) || (d.scores && d.scores->players.size() != players) ||
        (d.diplomacy && d.diplomacy->size() != players) || (d.factions && d.factions->players.size() != players)) {
        return bad("player sections do not match");
    }
    const std::size_t costs = d.costs ? d.costs->size() : w.cost_buildings().size();
    if ((d.fleet && d.fleet->yards.size() != costs) || (d.army && d.army->castles.size() != costs)) {
        return bad("shipyards or castles do not match");
    }
    if (d.tiles) {
        for (const auto& t : *d.tiles) {
            const auto i = w.island_index(t.island);
            if (!i || !w.islands()[*i].map.contains(t.x, t.y)) {
                return bad("tile outside its island");
            }
        }
    }
    if (d.cities) {
        for (const City& c : *d.cities) {
            if (!island_ok(w, c.island)) {
                return bad("city on an unknown island");
            }
        }
    }
    if (d.productions) {
        for (const Production& p : d.productions->list) {
            if (p.used && (!island_ok(w, p.island) || !w.islands()[p.island].map.contains(p.x, p.y))) {
                return bad("production outside its island");
            }
        }
    }
    const auto on_island = [&](const auto& list) {
        return std::all_of(list.begin(), list.end(), [&](const auto& e) {
            return island_ok(w, e.island) && w.islands()[e.island].map.contains(e.x, e.y);
        });
    };
    if ((d.houses && !on_island(*d.houses)) || (d.growth && !on_island(*d.growth)) ||
        (d.costs && !on_island(*d.costs)) || (d.animals && !on_island(*d.animals)) ||
        (d.disasters && !on_island(*d.disasters))) {
        return bad("a record outside its island");
    }
    if (d.volcanoes) {
        for (const Eruption& e : d.volcanoes->eruptions) {
            if (!island_ok(w, e.crater.island)) {
                return bad("eruption on an unknown island");
            }
        }
        for (const LavaLump& p : d.volcanoes->lava) {
            if (!island_ok(w, p.island)) {
                return bad("lava of an unknown island");
            }
        }
    }
    if (d.world && d.world->randoms.size() > static_cast<std::size_t>(RandomStream::Count)) {
        return bad("random generators do not match");
    }
    if (d.fleet) {
        for (const Ship& s : d.fleet->ships) {
            if (!w.contains(s.x, s.y)) {
                return bad("ship outside the world");
            }
        }
    }
    if (d.army) {
        for (const Soldier& s : d.army->soldiers) {
            if (s.island != 0xFFFF && !island_ok(w, s.island)) {
                return bad("unit on an unknown island");
            }
        }
    }
    return core::Ok{};
}

}  // namespace

core::Status apply_decoded(World& world, Decoded& d) {
    if (auto status = check(world, d); !status) {
        return status;
    }

    // Everything read: apply.
    if (d.rules) {
        world.set_saved_rules(d.rules);
    }
    if (d.world) {
        const auto& w = *d.world;
        world.set_time_ms(w.time_ms);
        world.set_city_name_index(w.city_name_index);
        World::GameSettings settings = w.settings;
        settings.pirate_interval_s = d.pirate_interval_s.value_or(world.game_settings().pirate_interval_s);
        settings.tutorial = d.tutorial;
        world.set_game_settings(settings);
        world.set_next_drought_ms(w.next_drought_ms);
        if (w.drought_seed) {
            world.set_drought_seed(*w.drought_seed);
        }
        for (std::size_t s = 0; s < w.randoms.size(); ++s) {
            world.random(static_cast<RandomStream>(s)) = w.randoms[s];
        }
        world.house_clocks() = w.house_clocks;
        world.growth().time_ms = w.growth_ms;
        world.growth().counter = w.growth_counter;
        world.set_next_disaster_id(w.next_disaster_id);
    }
    if (d.islands) {
        for (std::size_t i = 0; i < d.islands->size(); ++i) {
            const auto& part = (*d.islands)[i];
            WorldIsland& island = world.island(i);
            island.deer_phase = part.deer_phase;
            island.deer_spots = part.deer_spots;
            island.drought = part.drought;
            island.craters = part.craters;
            island.volcano_erupted = part.volcano_erupted;
            island.header.deposits = part.deposits;
            island.header.fertility = part.fertility;
        }
    }
    if (d.explored) {
        for (std::size_t i = 0; i < d.explored->size(); ++i) {
            const auto& part = (*d.explored)[i];
            WorldIsland& island = world.island(i);
            island.explored = part.explored;
            for (std::size_t k = 0; k < part.deposit_masks.size(); ++k) {
                island.header.deposits[k].mask = part.deposit_masks[k];
            }
        }
    }
    if (d.tiles) {
        for (const auto& t : *d.tiles) {
            IslandMap& map = world.island(*world.island_index(t.island)).map;
            if (t.ground) {
                map.set_ground_cell(t.x, t.y, t.cell);
            } else {
                map.set_top_cell(t.x, t.y, t.cell);
            }
        }
    }
    std::vector<ScenarioSwitches> kept_switches;
    if (d.players) {
        // Files of an earlier version have no PSTA: what the original's chunks gave stays.
        if (!d.player_state && d.players->size() == world.players().size()) {
            d.player_state = world.players();
        }
        // Nor SCOR: the counters of the original's chunks stay.
        if (!d.scores && d.players->size() == world.players().size()) {
            Decoded::ScorePart& part = d.scores.emplace();
            for (const Player& p : world.players()) {
                part.players.push_back({p.soldiers_killed, p.soldiers_lost, p.ships_sunk, p.ships_lost});
            }
        }
        // A PSTA without the scenario's switches (version 1) leaves those of the original's chunks.
        for (const Player& p : world.players()) {
            kept_switches.push_back(p.scenario);
        }
        world.set_players(std::move(*d.players));
    }
    if (d.factions) {
        std::vector<Player> players = world.players();
        for (std::size_t i = 0; i < players.size(); ++i) {
            players[i].faction = d.factions->players[i];
        }
        world.set_players(std::move(players));
        world.set_factions(std::move(d.factions->factions));
    }
    Balance balance = d.balance.value_or(Balance{});  // written whenever it is not the original's
    balance.buildings = d.building_values.value_or(std::vector<BuildingValue>{});
    balance.figures = d.figure_values.value_or(std::vector<FigureValue>{});
    world.set_balance(balance);
    if (d.script_states) {
        world.set_script_states(std::move(*d.script_states));
    }
    if (d.unit_groups) {
        world.set_unit_groups(std::move(*d.unit_groups));
    }
    if (d.packages) {
        world.set_packages(std::move(*d.packages));
    }
    if (d.goods) {
        if (d.good_pictures) {
            for (auto& [ware, picture] : *d.good_pictures) {
                (void)d.goods->set_picture(ware, std::move(picture));  // one GOOD has not: passed over
            }
        }
        world.set_goods(std::move(*d.goods));
    }
    if (d.classes) {
        world.set_resident_classes(std::move(*d.classes));
    }
    if (d.levels) {
        world.set_infra_levels(std::move(*d.levels));
    }
    if (d.unlocked_more) {
        for (const Decoded::UnlockedMore& u : *d.unlocked_more) {
            if (Player* p = world.player(u.player)) {
                p->infra_more = u.levels;
            }
        }
    }
    if (d.own_buildings) {
        world.set_own_buildings(std::move(*d.own_buildings));
    }
    if (d.missions) {
        world.set_missions(std::move(d.missions->missions));
        world.set_game_end(d.missions->end);
    }
    if (d.player_state) {
        for (std::size_t i = 0; i < d.player_state->size(); ++i) {
            const Player& from = (*d.player_state)[i];
            Player* pp = world.player(static_cast<PlayerId>(i));
            if (pp == nullptr) {
                continue;
            }
            Player& p = *pp;
            p.mission = from.mission < world.missions().size() ? from.mission : kNoMission;
            p.status = from.status;
            p.won = from.won;
            p.opponents_defeated = from.opponents_defeated;
            p.bankrupt_checks = from.bankrupt_checks;
            p.empty_checks = from.empty_checks;
            p.purchases = from.purchases;
            p.sales = from.sales;
            p.gates_closed = from.gates_closed;
            if (d.player_switches) {
                p.scenario = from.scenario;
            } else if (i < kept_switches.size()) {
                p.scenario = kept_switches[i];
            }
        }
    }
    if (d.scores) {
        for (std::size_t i = 0; i < d.scores->players.size(); ++i) {
            Player* pp = world.player(static_cast<PlayerId>(i));
            if (pp == nullptr) {
                continue;
            }
            Player& p = *pp;
            const Decoded::ScorePart::Counters& from = d.scores->players[i];
            p.soldiers_killed = from.soldiers_killed;
            p.soldiers_lost = from.soldiers_lost;
            p.ships_sunk = from.ships_sunk;
            p.ships_lost = from.ships_lost;
        }
    }
    if (d.cities) {
        // Files of an earlier version have no trade settings: those of the original's chunks stay.
        for (City& c : *d.cities) {
            if (const City* old = world.city(c.island, c.index)) {
                c.trade = old->trade;
                c.goodwill = old->goodwill;
                // Files of an earlier version have no CSTA: the statistics of the original's chunks stay.
                c.purchases = old->purchases;
                c.sales = old->sales;
                c.house_growth = old->house_growth;
                c.growing_since_ms = old->growing_since_ms;
                // Files of an earlier version have no MLCK: the clock stays.
                c.material_reminder_ms = old->material_reminder_ms;
                // Files of an earlier version have no CPOP: the message of the original's chunks stays.
                c.population_message = old->population_message;
            }
        }
        world.set_cities(std::move(*d.cities));
        if (d.city_state) {
            for (const Decoded::CityStats& st : *d.city_state) {
                if (City* c = world.city(st.island, st.index)) {
                    c->purchases = st.purchases;
                    c->sales = st.sales;
                }
            }
        }
        if (d.material_clocks) {
            for (const Decoded::MaterialClock& st : *d.material_clocks) {
                if (City* c = world.city(st.island, st.index)) {
                    c->material_reminder_ms = st.material_reminder_ms;
                }
            }
        }
        if (d.population_marks) {
            for (const Decoded::PopulationMark& st : *d.population_marks) {
                if (City* c = world.city(st.island, st.index)) {
                    c->population_message = st.population_message;
                }
            }
        }
        if (d.give_up_counts) {
            for (const Decoded::GiveUpCount& st : *d.give_up_counts) {
                if (City* c = world.city(st.island, st.index)) {
                    c->ships_gave_up = st.ships_gave_up;
                }
            }
        }
        if (d.ledgers) {
            for (Decoded::CityLedger& st : *d.ledgers) {
                if (City* c = world.city(st.island, st.index)) {
                    c->ledger = std::move(st.ledger);
                    c->history = std::move(st.history);
                }
            }
        }
        if (d.scores) {
            for (const Decoded::ScorePart::CityClock& st : d.scores->cities) {
                if (City* c = world.city(st.island, st.index)) {
                    c->house_growth = st.house_growth;
                    c->growing_since_ms = st.growing_since_ms;
                }
            }
        }
    }
    if (d.productions) {
        world.set_productions(std::move(d.productions->list), std::move(d.productions->free));
    }
    if (d.houses) {
        world.set_houses(std::move(*d.houses));
    }
    if (d.growth) {
        world.growth().entries = std::move(*d.growth);
    }
    if (d.costs) {
        // The shipyard fields of files of an earlier version come from WERFT: the last old entry
        // at the same place, found through a map (HI-14: a search per entry was quadratic).
        const auto key = [](const CostBuilding& c) {
            return std::uint64_t{static_cast<std::uint8_t>(c.kind)} << 48U | std::uint64_t{c.island} << 32U |
                   std::uint64_t{c.x} << 16U | c.y;
        };
        std::unordered_map<std::uint64_t, const CostBuilding*> old_at;
        old_at.reserve(world.cost_buildings().size());
        for (const CostBuilding& old : world.cost_buildings()) {
            old_at[key(old)] = &old;
        }
        for (CostBuilding& b : *d.costs) {
            if (const auto it = old_at.find(key(b)); it != old_at.end()) {
                const bool enabled = b.enabled;
                b = *it->second;
                b.enabled = enabled;
            }
        }
        world.cost_buildings() = std::move(*d.costs);
    }
    if (d.ships && !d.fleet && d.ships->size() != world.ships().size()) {
        // Files of an earlier version: the ships of SHIP4 are the same ones with more fields;
        // only a different number replaces them.
        world.ships().clear();
        for (Ship& ship : *d.ships) {
            world.add_ship(std::move(ship));
        }
    }
    if (d.epochs) {
        world.set_island_phase(d.epochs->phase);
        for (const auto& [number, phase] : d.epochs->islands) {
            if (const auto i = world.island_index(number)) {
                world.island(*i).last_phase = phase;
            }
        }
    }
    if (d.fleet) {
        world.set_next_ship_id(d.fleet->next_ship_id);
        for (const auto& [number, players] : d.fleet->discovered) {
            if (const auto i = world.island_index(number)) {
                world.island(*i).discovered = players;
            }
        }
        for (auto& c : d.fleet->cities) {
            if (City* city = world.city(c.island, c.index)) {
                city->trade = std::move(c.trade);
                city->goodwill = std::move(c.goodwill);
                city->trader_inbound = c.trader_inbound;
            }
        }
        for (std::size_t i = 0; i < d.fleet->yards.size(); ++i) {
            CostBuilding& b = world.cost_buildings()[i];
            const CostBuilding& y = d.fleet->yards[i];
            b.order = y.order;
            b.progress = y.progress;
            b.wood = y.wood;
            b.cloth = y.cloth;
            b.rate = y.rate;
            b.countdown = y.countdown;
            b.repair_ship = y.repair_ship;
            b.idle_cycles = y.idle_cycles;
            b.failed_launches = y.failed_launches;
            b.fetching = y.fetching;
        }
        world.ships() = std::move(d.fleet->ships);
        for (const Ship& ship : world.ships()) {
            world.set_next_ship_id(std::max(world.next_ship_id(), ship.id + 1));
        }
    }
    if (d.soldiers && !d.army && d.soldiers->size() != world.soldiers().size()) {
        // Files of an earlier version: the units of SOLDAT3 are the same ones with more fields;
        // only a different number replaces them.
        world.set_soldiers(std::move(*d.soldiers));
    }
    if (d.army) {
        world.set_soldiers(std::move(d.army->soldiers));
        world.set_next_soldier_id(d.army->next_soldier_id);
        for (const Soldier& s : world.soldiers()) {
            world.set_next_soldier_id(std::max(world.next_soldier_id(), s.id + 1));
        }
        world.towers() = std::move(d.army->towers);
        world.building_damage() = std::move(d.army->damage);
        world.pending_hits() = std::move(d.army->hits);
        for (const auto& part : d.army->ships) {
            if (Ship* ship = world.ship(part.id)) {
                ship->sinking = part.sinking;
                ship->next_shot_ms = part.next_shot_ms;
            }
        }
        for (const auto& part : d.army->cities) {
            if (City* city = world.city(part.island, part.index)) {
                city->last_attacked_ms = part.last_attacked_ms;
                city->cursed = part.cursed;
            }
        }
        for (std::size_t i = 0; i < d.army->castles.size(); ++i) {
            world.cost_buildings()[i].weapons = d.army->castles[i].weapons;
            world.cost_buildings()[i].slots = std::move(d.army->castles[i].slots);
        }
    }
    if (d.diplomacy) {
        world.diplomacy() = std::move(*d.diplomacy);
    }
    if (d.animals) {
        world.set_animals(std::move(*d.animals));
    }
    if (d.disasters) {
        world.disasters() = std::move(*d.disasters);
    }
    if (d.volcanoes) {
        world.volcanoes() = std::move(*d.volcanoes);
    }
    if (d.simulation) {
        world.set_simulation_state(std::move(*d.simulation));
    }
    for (auto& [id, queue] : d.ship_queues) {
        if (Ship* ship = world.ship(id)) {
            ship->queue = std::move(queue);
        }
    }
    for (auto& [id, queue] : d.soldier_queues) {
        if (Soldier* soldier = world.soldier(id)) {
            soldier->queue = std::move(queue);
        }
    }
    for (const auto& [id, f] : d.ship_stances) {
        if (Ship* ship = world.ship(id)) {
            ship->attack_move = (f & 1U) != 0;
            ship->hold = (f & 2U) != 0;
        }
    }
    for (const auto& [id, f] : d.soldier_stances) {
        if (Soldier* soldier = world.soldier(id)) {
            soldier->attack_move = (f & 1U) != 0;
            soldier->hold = (f & 2U) != 0;
        }
    }
    if (!d.origin.empty() && world.source() == nullptr) {
        if (auto file = assets::ChunkFile::from_bytes(std::move(d.origin))) {
            world.set_source(std::make_shared<const assets::ChunkFile>(std::move(file).value()));
        }
    }
    return core::Ok{};
}

}  // namespace o1602::sim::save
