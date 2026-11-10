// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/simulation.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>

#include "save/sections.hpp"

#include "o1602/core/hash.hpp"
#include "o1602/sim/goods_statistics.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/victory.hpp"

namespace o1602::sim {

namespace {

constexpr std::uint32_t kProductionPeriodMs = 1000;  // 0x0047DAF0: once per game second
constexpr std::uint32_t kUpkeepPeriodMs = 5000;      // 0x00476350: each player once per 5 s
constexpr std::uint32_t kDiplomacyTickMs = 5000;     // 0x00476350: the diplomacy counters fade
constexpr std::uint8_t kDefeatChecks = 40;           // 0x00476350: 40 checks of 5 s
constexpr std::size_t kArchClass = 2;                // 0x004805D0(K, 2): inhabitants of class 2 and up
constexpr std::uint64_t kArchInhabitants = 200;      // more than 199, in the largest city
constexpr std::uint16_t kMissionMessage = 0x54;      // "Ihr konntet den Euch gestellten Auftrag erfüllen!"
constexpr std::uint16_t kColourWinMessage = 0x6B;    // "Der rote Spieler konnte seinen Auftrag erfüllen!" + colour

/// What does not fit into the city's warehouse is lost.
void fit_into_storage(City& c) {
    for (std::size_t w = 0; w < c.goods.size(); ++w) {
        if (c.goods[w] > c.capacity) {
            c.book(static_cast<std::uint8_t>(w), GoodsFlow::Lost, c.goods[w] - c.capacity);
            c.goods[w] = c.capacity;
        }
    }
}

}  // namespace

Simulation::Simulation(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                       Rules rules, Parts)
    : m_world(&world),
      m_buildings(&buildings),
      m_rules(rules),
      m_figures(&figures),
      m_logistics(world, buildings, figures, &m_rules),
      m_fleet(world, buildings, figures, m_rules),
      m_combat(world, buildings, figures),
      m_military(world, buildings, figures, m_rules, m_combat),
      m_ai(*this) {
    fit_goodwill(world);
    m_military.set_fleet(&m_fleet);
    m_fleet.set_combat(&m_combat);
    m_fleet.set_directed([this](std::uint32_t ship) { return m_ai.directs(ship); });
    m_time_ms = world.time_ms();
    m_island_ms = static_cast<std::uint32_t>(m_time_ms % Logistics::kIslandEpochMs);
    // The cities consume at whole multiples of 10 s of game time (game08 -> game09, the video of
    // game04); the extra tick the original runs right after loading is not taken over (S-091).
    m_city_ms = static_cast<std::uint32_t>(m_time_ms % kCityTickMs);
    if (world.saved_rules()) {
        m_rules = *world.saved_rules();  // the settings the game was started with
    }
    world.set_original_bugs(m_rules.bugs);
}

Simulation::Simulation(World& world, const assets::BuildingTable& buildings, const assets::FigureTable& figures,
                       Rules rules)
    : Simulation(world, buildings, figures, rules, Parts{}) {
    recount_disasters(world);
    // As the original after loading: a city holds no more of a good than its warehouse takes. A city
    // without a warehouse (capacity 0) keeps what it has.
    for (const City& c : world.cities()) {
        if (City* city = world.city(c.island, c.index); city != nullptr && city->capacity != 0) {
            fit_into_storage(*city);
        }
    }
    // The towns the settings show from the start; a saved game has them discovered already.
    world.reveal_islands(m_rules.reveal);
    if (const std::vector<std::uint8_t> state = world.take_simulation_state(); !state.empty()) {
        // Each part loads whole or not at all, but a later part may fail after an earlier one
        // was taken over: then all go back to how they are now, fresh.
        core::ByteWriter fresh;
        save_state(fresh);
        if (restore_state(state)) {
            return;  // a game saved by Open1602 continues where it was
        }
        m_state_damaged = true;
        (void)restore_state(fresh.take());
    }
    // As the original after every load: the computer players look at the world anew (0x0042B710).
    m_ai.reset();
    // As when the original loads a game: every deer spot gets its deer.
    m_logistics.spawn_deer_at_spots();
    // The original takes the saved coverage of the houses; Open1602 recomputes it once, which
    // also restores what the savegame does not keep (gallows, fountains).
    update_services(world, buildings);
}

core::Status Simulation::state_readable(World& world, const assets::BuildingTable& buildings,
                                        const assets::FigureTable& figures) {
    if (world.simulation_state().empty()) {
        return core::Ok{};
    }
    Simulation probe(world, buildings, figures, Rules::open1602(), Parts{});
    return probe.restore_state(world.simulation_state());
}

std::uint64_t Simulation::state_hash() const {
    // The sections of the savegame as they are, without the file around them: neither packed nor
    // checked with a CRC-32, and without the original's file the game came from, which never
    // changes. XXH64 takes them eight bytes at a time. The tiles, most of a large game, go in as
    // the hashes of their blocks, kept from one fingerprint to the next for the blocks that stay.
    // The buffer is kept too: written anew, a game of a megabyte would cost an allocation and its
    // growth each time.
    core::ByteWriter out(std::move(m_hash_buffer));
    save::encode_sections(out, *m_world, *m_buildings, this, save::TileMode::Full, {}, save::Origin::Leave,
                          &m_block_hashes);
    m_hash_buffer = out.take();
    return core::xxh64(m_hash_buffer);
}

std::uint64_t Simulation::state_hash_anew() const {
    std::vector<std::vector<BlockHash>> blocks;
    core::ByteWriter out;
    save::encode_sections(out, *m_world, *m_buildings, this, save::TileMode::Full, {}, save::Origin::Leave, &blocks);
    return core::xxh64(out.data());
}

namespace {

/// Name lists of a game: at most this many names of at most this many bytes each.
constexpr std::uint32_t kMaxNames = 4096;
constexpr std::uint32_t kMaxNameBytes = 255;

void write_names(core::ByteWriter& out, const std::vector<std::string>& names) {
    const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(names.size(), kMaxNames));
    out.u32(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        out.string(std::string_view(names[i]).substr(0, kMaxNameBytes));
    }
}

bool read_names(StateIn& in, std::vector<std::string>& names) {
    const std::uint32_t n = in.count(4);
    if (n > kMaxNames) {
        return false;
    }
    names.clear();
    for (std::uint32_t i = 0; i < n && in.ok(); ++i) {
        std::string name = in.string();
        if (name.size() > kMaxNameBytes) {
            return false;
        }
        names.push_back(std::move(name));
    }
    return in.ok();
}

}  // namespace

void Simulation::offer_names(std::vector<std::string> cities, std::vector<std::string> ships) {
    if (m_city_names.empty() && m_fleet.ship_names().empty()) {
        m_city_names = std::move(cities);
        m_fleet.set_ship_names(std::move(ships));
    }
}

namespace {

// The state of the running game (the section SIMU) is a list of parts, each a section of its own
// as in the savegame (tag, version, flags, size; save/sections.cpp), so that one part can change
// without the others and a reader knows what it cannot read (DS-3). It begins with the u16
// kSectionedState; the bodies of the earlier versions 1 to 6 began with their version and held
// the parts one after the other without a frame (restore_legacy_state).
constexpr std::uint16_t kSectionedState = 7;

constexpr std::uint32_t part_tag(const char (&s)[5]) noexcept {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[0])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[1])) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[2])) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[3])) << 24;
}

/// The clocks of the simulation, the names for new cities and ships, the speed and the pause.
constexpr std::uint32_t kClocksPart = part_tag("SIMC");
constexpr std::uint32_t kLogisticsPart = part_tag("LOGI");
constexpr std::uint32_t kFleetPart = part_tag("FLET");
constexpr std::uint32_t kMilitaryPart = part_tag("MILI");
/// The computers' plans: optional, as a reader that cannot read them lets the computers look at
/// the world anew, as after loading an original savegame.
constexpr std::uint32_t kComputersPart = part_tag("AIST");
constexpr std::uint16_t kClocksVersion = 1;
constexpr std::uint16_t kRequiredPart = 1;  ///< flag: a reader that cannot read the part must not load

template <class Fn>
void write_part(core::ByteWriter& out, std::uint32_t id, std::uint16_t version, bool required, Fn&& body) {
    out.u32(id);
    out.u16(version);
    out.u16(required ? kRequiredPart : 0);
    const std::size_t size_at = out.size();
    out.u32(0);
    const std::size_t start = out.size();
    body();
    out.patch_u32(size_at, static_cast<std::uint32_t>(out.size() - start));
}

std::string part_name(std::uint32_t id) {
    std::string name;
    for (std::uint32_t i = 0; i < 4; ++i) {
        const auto c = static_cast<char>((id >> (8 * i)) & 0xFFU);
        name.push_back(c >= ' ' && c <= '~' ? c : '?');
    }
    return name;
}

core::Status damaged(std::string what) {
    return core::make_error(core::ErrorCode::InvalidFormat, "SIMU: " + std::move(what));
}

/// Reads all of a part through `load(reader, version)`: bytes left over are damage too, as the
/// reader did not read what the writer wrote.
template <class Load>
core::Status load_part(std::span<const std::uint8_t> body, std::uint32_t id, std::uint16_t version, Load&& load) {
    core::ByteReader reader(body);
    if (auto status = load(reader, version); !status) {
        return damaged(status.error().message);
    }
    if (!reader.at_end()) {
        return damaged(part_name(id) + ": " + std::to_string(reader.remaining()) + " bytes not read");
    }
    return core::Ok{};
}

}  // namespace

void Simulation::save_state(core::ByteWriter& out) const {
    out.u16(kSectionedState);
    write_part(out, kClocksPart, kClocksVersion, true, [&] {
        out.u32(m_production_ms);
        out.u32(m_city_ms);
        out.u32(m_disaster_ms);
        out.u32(m_upkeep_ms);
        out.u32(m_island_ms);
        write_names(out, m_city_names);
        write_names(out, m_fleet.ship_names());
        out.u32(m_speed);
        out.u8(m_paused ? 1 : 0);
    });
    write_part(out, kLogisticsPart, Logistics::kStateVersion, true, [&] { m_logistics.save(out); });
    write_part(out, kFleetPart, m_fleet.state_version(), true, [&] { m_fleet.save(out); });
    write_part(out, kMilitaryPart, Military::kStateVersion, true, [&] { m_military.save(out); });
    write_part(out, kComputersPart, ComputerPlayers::state_version(), false, [&] { m_ai.save(out); });
}

core::Status Simulation::restore_state(const std::vector<std::uint8_t>& state) {
    core::ByteReader reader(state);
    const auto version = reader.u16();
    if (!version) {
        return damaged("empty");
    }
    if (*version >= 1 && *version <= 6) {
        return restore_legacy_state(reader, *version);
    }
    if (*version != kSectionedState) {
        return core::make_error(core::ErrorCode::Unsupported,
                                "SIMU: version " + std::to_string(*version) + " needs a newer Open1602");
    }
    // Every part found first, then read in a fixed order.
    struct Part {
        std::uint16_t version = 0;
        std::span<const std::uint8_t> body;
    };
    std::optional<Part> clocks, logistics, fleet, military, computers;
    while (!reader.at_end()) {
        const auto id = reader.u32();
        const auto part_version = reader.u16();
        const auto flags = reader.u16();
        const auto size = reader.u32();
        const auto body = id && part_version && flags && size ? reader.bytes(*size) : std::nullopt;
        if (!body) {
            return damaged("a part's header is cut off");
        }
        std::optional<Part>* slot = nullptr;
        std::uint16_t max_version = 0;
        switch (*id) {
            case kClocksPart:
                slot = &clocks;
                max_version = kClocksVersion;
                break;
            case kLogisticsPart:
                slot = &logistics;
                max_version = Logistics::kStateVersion;
                break;
            case kFleetPart:
                slot = &fleet;
                max_version = Fleet::kMaxStateVersion;
                break;
            case kMilitaryPart:
                slot = &military;
                max_version = Military::kStateVersion;
                break;
            case kComputersPart:
                slot = &computers;
                max_version = ComputerPlayers::state_version();
                break;
            default:
                break;
        }
        const std::string what = part_name(*id) + " (version " + std::to_string(*part_version) + ")";
        if (slot == nullptr || *part_version == 0 || *part_version > max_version) {
            if ((*flags & kRequiredPart) != 0) {
                return core::make_error(core::ErrorCode::Unsupported, "SIMU: " + what + " needs a newer Open1602");
            }
            continue;  // a part of a later Open1602 the game can do without
        }
        if (slot->has_value()) {
            return damaged(part_name(*id) + " twice");
        }
        *slot = Part{*part_version, *body};
    }
    if (!clocks || !logistics || !fleet || !military) {
        return damaged("a part is missing");
    }

    core::ByteReader clock_reader(clocks->body);
    StateIn in(clock_reader);
    const std::uint32_t production = in.u32();
    const std::uint32_t city = in.u32();
    const std::uint32_t disaster = in.u32();
    const std::uint32_t upkeep = in.u32();
    const std::uint32_t island = in.u32();
    std::vector<std::string> cities;
    std::vector<std::string> ships;
    const bool names = read_names(in, cities) && read_names(in, ships);
    const std::uint32_t speed = in.u32();
    const std::uint8_t pause = in.u8();
    if (!in.ok() || !names || !clock_reader.at_end() || speed == 0 || pause > 1) {
        return damaged("SIMC cut off or out of range");
    }
    // A clock at or past its period would fire on every tick until it has caught up.
    if (production >= kProductionPeriodMs || city >= kCityTickMs || disaster >= kDisasterEpochMs ||
        upkeep >= kUpkeepPeriodMs || island >= Logistics::kIslandEpochMs) {
        return damaged("SIMC: a clock past its period");
    }
    const auto load_logistics = [&](core::ByteReader& r, std::uint16_t v) { return m_logistics.load(r, v); };
    const auto load_fleet = [&](core::ByteReader& r, std::uint16_t v) { return m_fleet.load(r, v); };
    const auto load_military = [&](core::ByteReader& r, std::uint16_t v) { return m_military.load(r, v); };
    const auto load_computers = [&](core::ByteReader& r, std::uint16_t v) { return m_ai.load(r, v); };
    if (auto s = load_part(logistics->body, kLogisticsPart, logistics->version, load_logistics); !s) {
        return s;
    }
    if (auto s = load_part(fleet->body, kFleetPart, fleet->version, load_fleet); !s) {
        return s;
    }
    if (auto s = load_part(military->body, kMilitaryPart, military->version, load_military); !s) {
        return s;
    }
    if (computers) {
        if (auto s = load_part(computers->body, kComputersPart, computers->version, load_computers); !s) {
            return s;
        }
    } else {
        m_ai.reset();  // a later version's plans, or none: the computers look at the world anew
    }
    m_production_ms = production;
    m_city_ms = city;
    m_disaster_ms = disaster;
    m_upkeep_ms = upkeep;
    m_island_ms = island;
    m_city_names = std::move(cities);
    m_fleet.set_ship_names(std::move(ships));
    set_speed(speed);
    m_paused = pause != 0;
    return core::Ok{};
}

core::Status Simulation::restore_legacy_state(core::ByteReader& reader, std::uint16_t version) {
    // Versions 1 to 6 (until 2026-10): the parts one after the other, each new version adding one
    // at the end. Kept for the games saved with them (tests/unit/saves).
    StateIn in(reader);
    const std::uint32_t production = in.u32();
    const std::uint32_t city = in.u32();
    const std::uint32_t disaster = in.u32();
    const std::uint32_t upkeep = in.u32();
    const std::uint32_t island = in.u32();
    if (!in.ok()) {
        return damaged("cut off");
    }
    if (production >= kProductionPeriodMs || city >= kCityTickMs || disaster >= kDisasterEpochMs ||
        upkeep >= kUpkeepPeriodMs || island >= Logistics::kIslandEpochMs) {
        return damaged("a clock past its period");
    }
    // Version 1 had the walkers before shipyards had pack donkeys.
    if (auto s = m_logistics.load(reader, version >= 2 ? 2 : 1); !s) {
        return damaged(s.error().message);
    }
    if (version >= 2) {
        // The fleet's state began with its own version.
        const auto fleet = reader.u16();
        if (!fleet) {
            return damaged("FLET cut off");
        }
        if (auto s = m_fleet.load(reader, *fleet); !s) {
            return damaged(s.error().message);
        }
    }
    if (version >= 3) {
        // The military's clocks had no version: in version 3 they ended the state, with one, two
        // or three of them; from version 4 on the computers' state followed all three.
        const std::uint16_t military =
            version == 3 ? static_cast<std::uint16_t>(std::clamp<std::size_t>(reader.remaining() / 4, 1, 3)) : 3;
        if (auto s = m_military.load(reader, military); !s) {
            return damaged(s.error().message);
        }
    }
    if (version >= 4) {
        // The computers' state began with its own version (u32).
        const auto computers = reader.u32();
        if (!computers || *computers > 0xFFFF) {
            return damaged("AIST cut off");
        }
        if (auto s = m_ai.load(reader, static_cast<std::uint16_t>(*computers)); !s) {
            return damaged(s.error().message);
        }
    } else {
        m_ai.reset();  // games of earlier versions had no computer players' state
    }
    std::vector<std::string> cities;
    std::vector<std::string> ships;
    if (version >= 5 && (!read_names(in, cities) || !read_names(in, ships))) {
        return damaged("names cut off");
    }
    // Games of earlier versions did not keep the speed and the pause: they load running at speed 1.
    std::uint32_t speed = 1;
    bool paused = false;
    if (version >= 6) {
        speed = in.u32();
        const std::uint8_t pause = in.u8();
        if (!in.ok() || speed == 0 || pause > 1) {
            return damaged("speed cut off or out of range");
        }
        paused = pause != 0;
    }
    m_production_ms = production;
    m_city_ms = city;
    m_disaster_ms = disaster;
    m_upkeep_ms = upkeep;
    m_island_ms = island;
    if (version >= 5) {
        m_city_names = std::move(cities);
        m_fleet.set_ship_names(std::move(ships));
    }
    set_speed(speed);
    m_paused = paused;
    return core::Ok{};
}

void Simulation::set_speed(std::uint32_t speed) noexcept {
    m_speed = std::min(speed, m_rules.max_game_speed);
}

void Simulation::advance(std::uint32_t real_ms, const BeforeTick& before_tick) {
    m_behind = false;
    if (m_speed == 0 || m_paused) {
        apply_commands();
        return;
    }
    // The limit is on real time, before the speed: at speed 8 a frame of 400 ms is no stall.
    const std::uint64_t game_ms = real_ms > kMaxCatchUpMs ? kTickMs : std::uint64_t{real_ms} * m_speed;
    constexpr std::uint64_t kMaxOwedMs = std::uint64_t{kTickMs} * kMaxTicksPerAdvance;
    m_pending_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(m_pending_ms + game_ms, 2 * kMaxOwedMs));
    std::uint32_t ran = 0;
    while (m_pending_ms >= kTickMs) {
        if (ran == kMaxTicksPerAdvance || (before_tick && !before_tick())) {
            m_behind = true;
            break;
        }
        m_pending_ms -= kTickMs;
        tick();
        ++ran;
        if (m_paused) {
            m_pending_ms = 0;  // a pause from the commands of this tick: the rest of the time is dropped
            break;
        }
    }
    // Owed is at most one more step; beyond that the game runs slower instead of ever catching up.
    m_pending_ms = static_cast<std::uint32_t>(std::min<std::uint64_t>(m_pending_ms, kMaxOwedMs));
}

namespace {

PlayerId sender(const Command& command) {
    return std::visit([](const auto& c) { return c.player; }, command);
}

}  // namespace

std::uint64_t Simulation::schedule(std::uint64_t tick, Command command) {
    const std::uint64_t sequence = m_next_command++;
    const PlayerId from = sender(command);
    // After every command of an earlier tick, or of the same tick and an earlier or the same player.
    const auto at = std::upper_bound(m_commands.begin(), m_commands.end(), std::pair{tick, from},
                                     [](const std::pair<std::uint64_t, PlayerId>& key, const Queued& q) {
                                         return key < std::pair{q.tick, sender(q.command)};
                                     });
    m_commands.insert(at, Queued{tick, sequence, std::move(command)});
    return sequence;
}

void Simulation::apply_commands() {
    const std::uint64_t now = next_tick();
    std::size_t due = 0;
    while (due < m_commands.size() && m_commands[due].tick <= now) {
        ++due;
    }
    // Moved out first: a command may not add to the queue, but the vector must not be touched
    // while it is walked.
    std::vector<Queued> commands(std::make_move_iterator(m_commands.begin()),
                                 std::make_move_iterator(m_commands.begin() + static_cast<std::ptrdiff_t>(due)));
    m_commands.erase(m_commands.begin(), m_commands.begin() + static_cast<std::ptrdiff_t>(due));
    for (Queued& q : commands) {
        CommandResult result;
        // A player's command acts for that player only: one without a valid sender would pass every
        // owner check (kNoPlayer) and is refused.
        const PlayerId from = sender(q.command);
        if (q.tick < now) {
            result.late = true;
        } else if (from != kNoPlayer && m_world->player(from) != nullptr) {
            result = execute(q.command);
        }
        result.sequence = q.sequence;
        m_command_results.push(std::move(result));
    }
    trim_events();  // also while the game is paused
}

CommandResult Simulation::execute(const Command& command) {
    CommandResult result;
    // A defeated player or a slot nobody plays gives no orders (the original's controller 9, 10).
    const PlayerId from = sender(command);
    if (const Player* p = m_world->player(from); p != nullptr && !p->active()) {
        return result;
    }
    const std::size_t first = m_construction_events.size();
    if (const auto* build = std::get_if<BuildOrder>(&command)) {
        result.build = execute_build(*m_world, *m_buildings, *build, m_city_names, m_construction_events.sink());
    } else if (const auto* demolish = std::get_if<DemolishOrder>(&command)) {
        const DemolishResult d =
            execute_demolish(*m_world, *m_buildings, *demolish, m_construction_events.sink(), m_rules.refund_share());
        result.demolished = d.removed;
        result.refund = d.refund;
    } else if (const auto* order = std::get_if<ShipOrder>(&command)) {
        result.accepted = m_fleet.order(*order);
        if (result.accepted && order->attack) {
            if (const Ship* ship = m_world->ship(order->ship)) {
                m_military.declare_war_by_attack(ship->owner, order->target);
            }
        }
    } else if (const auto* transfer = std::get_if<CargoTransfer>(&command)) {
        result.moved = m_fleet.transfer(*transfer);
        result.accepted = result.moved != 0;
    } else if (const auto* route = std::get_if<RouteEdit>(&command)) {
        result.accepted = m_fleet.edit_route(*route);
    } else if (const auto* trade = std::get_if<TradeSetting>(&command)) {
        result.accepted = m_fleet.set_trade(*trade);
    } else if (const auto* yard = std::get_if<ShipyardOrder>(&command)) {
        result.accepted = m_fleet.shipyard(*yard);
    } else if (const auto* explore = std::get_if<ExploreOrder>(&command)) {
        result.accepted = m_fleet.explore(*explore);
    } else if (const auto* dipl = std::get_if<DiplomacyOrder>(&command)) {
        result.accepted =
            dipl->kind == DiplomacyOrder::Kind::AskFounding ? ask_founding(*dipl) : apply_diplomacy(*m_world, *dipl);
    } else if (const auto* soldiers = std::get_if<SoldierOrder>(&command)) {
        result.accepted = m_military.order(*soldiers);
    } else if (const auto* soldier = std::get_if<SoldierCommand>(&command)) {
        result.accepted = m_military.command(*soldier);
    } else if (const auto* castle = std::get_if<CastleOrder>(&command)) {
        result.accepted = m_military.castle(*castle);
    } else if (const auto* unload = std::get_if<UnloadOrder>(&command)) {
        result.accepted = m_military.unload(*unload);
    } else if (const auto* sw = std::get_if<ProductionSwitch>(&command)) {
        result.accepted = switch_production(*sw);
    } else if (const auto* city = std::get_if<CityOrder>(&command)) {
        result.accepted = city_order(*city);
    } else if (const auto* ship = std::get_if<ShipCommand>(&command)) {
        result.accepted = m_fleet.command(*ship);
    } else if (const auto* speed = std::get_if<GameSpeedOrder>(&command)) {
        result.accepted = game_speed(*speed);
    } else if (const auto* rename = std::get_if<PlayerRename>(&command)) {
        result.accepted = rename_player(*rename);
    } else if (const auto* control = std::get_if<PlayerControl>(&command)) {
        result.accepted = control_player(*control);
    } else if (const auto* gates = std::get_if<GateOrder>(&command)) {
        result.accepted = gate_order(*gates);
    } else if (const auto* cart = std::get_if<CartRequest>(&command)) {
        result.accepted = m_fleet.request_cart(*cart) != 0;
    }
    handle(m_construction_events.events(), first);
    return result;
}

bool Simulation::ask_founding(const DiplomacyOrder& order) {
    const Ship* ship = m_world->ship(order.ship);
    if (ship == nullptr || ship->owner != order.player) {
        return false;
    }
    // The island by the ship (0x0046AAA0), where the ship panel offers the founding.
    const auto site = kontor_site_near(*m_world, *m_buildings, ship->x, ship->y);
    if (!site) {
        return false;
    }
    m_ai.founding_query(order.player, site->island);
    return true;
}

bool Simulation::switch_production(const ProductionSwitch& s) {
    if (s.island >= m_world->islands().size()) {
        return false;
    }
    const IslandMap& map = m_world->islands()[s.island].map;
    if (!map.contains(s.x, s.y) || (s.player != kNoPlayer && map.top(s.x, s.y).owner != s.player)) {
        return false;
    }
    const auto index = m_world->production_index(s.island, s.x, s.y);
    if (!index) {
        return false;
    }
    Production& p = m_world->productions()[*index];
    if (!p.used) {
        return false;
    }
    // M-019: both switches are commands, so every computer applies them.
    if (s.op == ProductionSwitch::Op::Pickup) {
        if (p.no_pickup() == !s.on) {
            return false;
        }
        p.flags = static_cast<std::uint8_t>(s.on ? p.flags & ~Production::kNoPickupFlag
                                                 : p.flags | Production::kNoPickupFlag);
        return true;
    }
    if (p.enabled == s.on) {
        return false;
    }
    p.enabled = s.on;
    if (s.on && !p.working()) {
        // An idle building starts its next cycle in the next second; the time it waited does not
        // count against its productivity (0x0047CFA0).
        p.idle_cycles = 0;
        p.elapsed = p.elapsed > p.countdown ? static_cast<std::uint16_t>(p.elapsed - p.countdown) : 0;
        p.countdown = 0;
    }
    return true;
}

bool Simulation::city_order(const CityOrder& o) {
    City* city = m_world->city(o.island, o.city);
    if (city == nullptr || (o.player != kNoPlayer && city->owner != o.player)) {
        return false;
    }
    switch (o.op) {
        case CityOrder::Op::TaxRate: {
            // 0x0047F130 posts the rate, 0x0047F850 measures the class against it at once.
            const auto& classes = m_buildings->resident_classes();
            if (o.resident_class >= classes.size() || o.resident_class >= city->tax_rate.size() ||
                o.rate < CityOrder::kMinTax || o.rate > CityOrder::kMaxTax) {
                return false;
            }
            city->tax_rate[o.resident_class] = o.rate;
            if (o.resident_class < city->satisfaction.size()) {
                city->satisfaction[o.resident_class] = satisfaction_of(*city, classes[o.resident_class], o.rate);
            }
            return true;
        }
        case CityOrder::Op::Rename:
            if (!valid_name(o.name, CityOrder::kMaxNameLength)) {
                return false;
            }
            city->name = o.name;
            return true;
        case CityOrder::Op::MaterialLock:
            // Switching the lock on starts the people's clock (0x0048073A).
            if (o.on && !city->material_lock) {
                city->material_reminder_ms = m_world->time_ms();
            }
            city->material_lock = o.on;
            return true;
    }
    return false;
}

bool Simulation::game_speed(const GameSpeedOrder& order) {
    switch (order.op) {
        case GameSpeedOrder::Op::SetSpeed:
            if (order.speed == 0) {
                return false;  // pausing is Op::Pause, so that resuming keeps the speed
            }
            set_speed(order.speed);
            return true;
        case GameSpeedOrder::Op::Pause:
            m_paused = true;
            return true;
        case GameSpeedOrder::Op::Resume:
            m_paused = false;
            return true;
    }
    return false;
}

bool Simulation::rename_player(const PlayerRename& order) {
    Player* p = m_world->player(order.player);
    if (p == nullptr || !valid_name(order.name, PlayerRename::kMaxNameLength)) {
        return false;
    }
    p->name = order.name;
    return true;
}

bool Simulation::control_player(const PlayerControl& order) {
    Player* p = m_world->player(order.player);
    // Only the two kinds of players change hands. The computer goes on with the state it kept for a
    // slot it played before; a slot new to it it takes on from what it finds (ai::Engine).
    if (p == nullptr || !p->active() || (p->role != PlayerRole::Human && p->role != PlayerRole::Computer)) {
        return false;
    }
    const bool to_person = p->role == PlayerRole::Computer && !order.computer;
    p->role = order.computer ? PlayerRole::Computer : PlayerRole::Human;
    if (to_person) {
        // The person finds the ships standing where the computer left them, not sailing on to
        // where it sent them; trade routes keep running (they show in the ship's window).
        for (const Ship& ship : m_world->ships()) {
            if (ship.owner == order.player && !ship.route.active && !ship.target.empty()) {
                (void)m_fleet.order(ShipOrder{order.player, ship.id, {}});
            }
        }
    }
    return true;
}

bool Simulation::gate_order(const GateOrder& order) {
    Player* p = m_world->player(order.player);
    if (p == nullptr || !p->active() || (p->role != PlayerRole::Human && p->role != PlayerRole::Computer)) {
        return false;
    }
    p->gates_closed = order.closed;
    return true;
}

namespace {

/// Whether an event can change the towers of its island: they depend on the tower buildings and
/// on the city of the tiles they stand on, which only markets, Kontors and towers change (TB-19).
bool towers_affected(const ConstructionEvent& e) {
    if ((e.kind != ConstructionEvent::Kind::Placed && e.kind != ConstructionEvent::Kind::Removed) ||
        e.type == nullptr) {
        return true;
    }
    const std::int32_t p = e.type->prod_kind;
    return p == prod::kTower || p == prod::kMarket || p == prod::kKontor || e.type->kind == kind::kTower ||
           e.type->kind == kind::kTowerBeach;
}

}  // namespace

void Simulation::handle(const std::vector<ConstructionEvent>& events, std::size_t first) {
    bool storage = false;
    // S-002: figures of a building that is gone end their trip; what they carry is lost and what
    // they reserved is released. All removals up to the next founded city at once (TB-22).
    std::vector<std::uint32_t> removed;
    for (std::size_t i = first; i < events.size(); ++i) {
        const ConstructionEvent& e = events[i];
        switch (e.kind) {
            case ConstructionEvent::Kind::Removed:
                if (e.production) {
                    removed.push_back(*e.production);
                }
                break;
            case ConstructionEvent::Kind::CityFounded:
                m_logistics.buildings_removed(removed);
                removed.clear();
                m_ai.city_founded(e.island, e.city);
                m_world->reveal_islands(m_rules.reveal, e.island);
                storage = true;
                break;
            case ConstructionEvent::Kind::CityDissolved:
                m_ai.land_freed(e.island);
                storage = true;
                break;
            case ConstructionEvent::Kind::StorageChanged:
            case ConstructionEvent::Kind::CityTaken:
                storage = true;
                break;
            case ConstructionEvent::Kind::Placed:
            case ConstructionEvent::Kind::TreasureFound:
                break;
        }
        // The sea map, the land map and the towers depend only on the island as it is now, so each
        // touched island is updated once per batch, in the order of its first event (TB-19).
        const auto touch = [](std::vector<std::uint16_t>& list, std::uint16_t island) {
            if (std::find(list.begin(), list.end(), island) == list.end()) {
                list.push_back(island);
            }
        };
        if (e.kind == ConstructionEvent::Kind::Placed || e.kind == ConstructionEvent::Kind::Removed) {
            // The building's tiles, whichever way it is turned; without a type the whole island.
            constexpr std::int32_t kAll = std::numeric_limits<std::int32_t>::max() / 2;
            const std::int32_t size =
                e.type != nullptr ? static_cast<std::int32_t>(std::max(e.type->size_x, e.type->size_y)) : kAll;
            const std::int32_t x0 = e.type != nullptr ? e.x : 0;
            const std::int32_t y0 = e.type != nullptr ? e.y : 0;
            const std::int32_t x1 = x0 + size - 1;
            const std::int32_t y1 = y0 + size - 1;
            const auto it = std::find_if(m_coast_islands.begin(), m_coast_islands.end(),
                                         [&](const CoastChange& c) { return c.island == e.island; });
            if (it == m_coast_islands.end()) {
                m_coast_islands.push_back(CoastChange{e.island, x0, y0, x1, y1});
            } else {
                it->x0 = std::min(it->x0, x0);
                it->y0 = std::min(it->y0, y0);
                it->x1 = std::max(it->x1, x1);
                it->y1 = std::max(it->y1, y1);
            }
        }
        if (towers_affected(e)) {
            touch(m_tower_islands, e.island);
        }
    }
    m_logistics.buildings_removed(removed);
    for (const CoastChange& c : m_coast_islands) {
        m_fleet.island_changed(c.island, c.x0, c.y0, c.x1, c.y1);
        m_combat.island_changed(c.island);
    }
    for (const std::uint16_t island : m_tower_islands) {
        m_military.update_towers(island);
    }
    m_coast_islands.clear();
    m_tower_islands.clear();
    if (storage) {
        m_logistics.update_storage();
        // A smaller warehouse loses what does not fit (0x00482120).
        for (std::size_t i = first; i < events.size(); ++i) {
            if (events[i].kind != ConstructionEvent::Kind::StorageChanged) {
                continue;
            }
            if (City* c = m_world->city(events[i].island, events[i].city)) {
                fit_into_storage(*c);
            }
        }
    }
}

std::span<const Simulation::TickStep> Simulation::tick_steps() noexcept {
    // The original runs its subsystems in this order (0x00489670): growth first (0x0047CA80),
    // production (0x0047DAF0), then the cities (0x0047F8A0) and houses (0x0047B9C0), ..., the
    // computer players after the players' tick (AI_Tick 0x0042B4B0) and the figures last
    // (0x00451890). Changing the order changes the game.
    static constexpr std::array<TickStep, 16> kSteps = {{
        {"diplomacy", &Simulation::diplomacy_step},
        {"growth", &Simulation::growth_step},
        {"production", &Simulation::production_step},
        {"cities", &Simulation::cities_step},
        {"city events", &Simulation::city_events_step},
        {"houses", &Simulation::houses_step},
        {"upkeep", &Simulation::upkeep_step},
        {"disasters", &Simulation::disasters_step},
        {"islands", &Simulation::islands_step},
        {"volcanoes", &Simulation::volcanoes_step},
        {"computers", &Simulation::computers_step},
        {"combat", &Simulation::combat_step},
        {"ships", &Simulation::ships_step},
        {"soldiers", &Simulation::soldiers_step},
        {"logistics", &Simulation::logistics_step},
        {"statistics", &Simulation::statistics_step},  // Open1602's own, last: it only watches
    }};
    return kSteps;
}

std::vector<std::string_view> Simulation::tick_step_names() {
    std::vector<std::string_view> names;
    for (const TickStep& step : tick_steps()) {
        names.push_back(step.name);
    }
    return names;
}

void Simulation::tick() {
    tick_timed({});
}

void Simulation::tick_timed(std::span<std::uint64_t> step_ns) {
    const std::uint64_t construction_before = m_construction_events.total();
    const std::uint64_t houses_before = m_house_events.total();
    const std::uint64_t game_before = m_game_events.total();
    apply_commands();
    if (m_paused) {
        return;  // paused by a command of this tick: the game time stands still
    }
    m_time_ms += kTickMs;
    m_world->set_time_ms(m_time_ms);
    const std::span<const TickStep> steps = tick_steps();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (i < step_ns.size()) {
            const auto start = std::chrono::steady_clock::now();
            (this->*steps[i].run)();
            step_ns[i] += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
        } else {
            (this->*steps[i].run)();
        }
    }
    if (m_scripts != nullptr) {
        const TickEvents events{m_construction_events.since(construction_before), m_house_events.since(houses_before),
                                m_game_events.since(game_before)};
        m_scripts->after_tick(*this, events);
    }
    trim_events();
    if (m_tick_observer) {
        m_tick_observer(*m_world);
    }
}

bool Simulation::elapsed(std::uint32_t& clock, std::uint32_t period) noexcept {
    clock += kTickMs;
    if (clock < period) {
        return false;
    }
    clock -= period;
    return true;
}

void Simulation::trim_events() {
    m_command_results.trim();
    m_construction_events.trim();
    m_production_events.trim();
    m_city_events.trim();
    m_house_events.trim();
    m_island_events.trim();
    m_disaster_events.trim();
    m_volcano_events.trim();
    m_game_events.trim();
    m_ship_events.trim();
    m_military_events.trim();
}

void Simulation::diplomacy_step() {
    // Every 5 s the diplomacy counters fade (Player_Tick 0x00476350; the original takes one
    // player per call, each once in 5 s).
    if (m_time_ms % kDiplomacyTickMs != 0) {
        return;
    }
    for (std::size_t p = 0; p < m_world->players().size(); ++p) {
        m_world->diplomacy().decay(static_cast<PlayerId>(p));
    }
    pirates_reconsider(*m_world);
    players_tick();
}

void Simulation::growth_step() {
    advance_growth(*m_world, *m_buildings, kTickMs);
}

void Simulation::production_step() {
    if (!elapsed(m_production_ms, kProductionPeriodMs)) {
        return;
    }
    const std::size_t first = m_production_events.size();
    advance_production(*m_world, *m_buildings, m_production_events.sink());
    for (std::size_t i = first; i < m_production_events.size(); ++i) {
        m_logistics.handle(m_production_events.events()[i]);
    }
}

void Simulation::cities_step() {
    // The cities consume every 10 s (0x0047F8A0 comes after the production in the original).
    if (!elapsed(m_city_ms, kCityTickMs)) {
        return;
    }
    advance_cities(*m_world, *m_buildings, m_city_events.sink());
    m_military.natives_tick();
}

void Simulation::city_events_step() {
    // Every 60-67 s a city may catch the plague, send out a robber or catch fire (0x0047F8A0).
    advance_city_events(*m_world, *m_buildings, m_rules, m_disaster_events.sink(), &m_robbers);
    for (const RobberSpawn& r : m_robbers) {
        m_logistics.spawn_robber(r);
    }
    m_robbers.clear();
}

void Simulation::statistics_step() {
    // The goods statistics take their sample at the end of every game minute.
    if (m_time_ms % kGoodsMinuteMs == 0) {
        close_goods_minute(*m_world, *m_buildings, m_city_events.sink());
    }
}

void Simulation::houses_step() {
    advance_houses(*m_world, *m_buildings, kTickMs, m_house_events.sink());
}

void Simulation::upkeep_step() {
    // Soldiers' upkeep of human players every 5 s (0x00476350): a twelfth of the upkeep per minute.
    if (elapsed(m_upkeep_ms, kUpkeepPeriodMs)) {
        pay_upkeep();
    }
}

void Simulation::disasters_step() {
    // Disasters in epochs of 10 s (0x0047A020): they spread, end or burn the building down.
    if (!elapsed(m_disaster_ms, kDisasterEpochMs)) {
        return;
    }
    const std::size_t first = m_construction_events.size();
    advance_disasters(*m_world, *m_buildings, m_rules, m_disaster_events.sink(), m_construction_events.sink());
    handle(m_construction_events.events(), first);
}

void Simulation::islands_step() {
    // Island epochs of 30 s (0x0046B3E0): droughts, then deer come back and settle. The phase
    // steps every 30 s; an island whose last phase differs is ticked. In a new game that happens
    // at once for islands whose template says so (docs/re/newgame.md).
    const bool epoch = elapsed(m_island_ms, Logistics::kIslandEpochMs);
    if (epoch) {
        m_world->set_island_phase(static_cast<std::uint8_t>(m_world->island_phase() + 1U));
    }
    std::vector<bool>& due = m_islands_due;
    const std::size_t count = m_world->islands().size();
    due.assign(count, false);
    bool any = false;
    for (std::uint16_t i = 0; i < count; ++i) {
        WorldIsland& island = m_world->island(i);
        // Island i takes its turn i / count of the way into the epoch, not all in one tick (TB-28).
        const std::uint64_t offset = std::uint64_t{Logistics::kIslandEpochMs} * i / count;
        if (island.last_phase != m_world->island_phase() && m_island_ms >= offset) {
            island.last_phase = m_world->island_phase();
            due[i] = true;
            any = true;
        }
    }
    if (any) {
        advance_island_events(
            *m_world, *m_buildings, m_rules,
            [this](std::uint16_t island, TilePos tile) { return m_logistics.field_reserved(island, tile); },
            m_island_events.sink(), &due);
    }
    if (epoch) {
        // The eruption roll; the original makes it only while no drought roll is due (S-042).
        roll_eruptions(*m_world, m_rules, m_volcano_events.sink());
    }
    if (any) {
        m_logistics.advance_islands(&due);
    }
}

void Simulation::volcanoes_step() {
    // Eruptions and lava (figures of the original, kinds 0x20 and 0x21).
    const std::size_t first = m_construction_events.size();
    const assets::FigureType* lava = m_figures->get(kFigureLava);
    advance_volcanoes(*m_world, *m_buildings, lava != nullptr && lava->speed != 0 ? lava->speed : kLavaSpeed,
                      m_volcano_events.sink(), m_disaster_events.sink(), m_construction_events.sink());
    handle(m_construction_events.events(), first);
}

void Simulation::computers_step() {
    m_ai.tick(kTickMs);
}

void Simulation::combat_step() {
    // Figures last: first who sees whom, then ships and shipyards, then soldiers, towers and the
    // shots that land.
    m_combat.update();
}

void Simulation::ships_step() {
    m_fleet.tick(kTickMs);
}

void Simulation::soldiers_step() {
    const std::size_t first = m_construction_events.size();
    m_military.tick(kTickMs, m_construction_events.sink());
    handle(m_construction_events.events(), first);
    m_military_events.append(m_military.take_events());
    // Castles fetch their weapons with a pack donkey, like shipyards their wood.
    for (const Military::FetchRequest& r : m_military.take_fetch_requests()) {
        if (!send_yard_carrier(r.island, r.x, r.y, r.ware)) {
            m_military.castle_delivery(r.island, r.x, r.y, r.ware, 0);
        }
    }
    for (const Fleet::FetchRequest& r : m_fleet.take_fetch_requests()) {
        if (!send_yard_carrier(r.island, r.x, r.y, r.ware)) {
            m_fleet.yard_delivery(r.island, r.x, r.y, r.ware, 0);
        }
    }
    m_ship_events.append(m_fleet.take_events());
}

bool Simulation::send_yard_carrier(std::uint16_t island, std::uint16_t x, std::uint16_t y, std::uint8_t ware) {
    const assets::BuildingType* building = nullptr;
    if (island < m_world->islands().size()) {
        const MapCell& c = m_world->islands()[island].map.top(x, y);
        building = c.empty() ? nullptr : m_buildings->by_sprite(c.sprite);
    }
    return building != nullptr && m_logistics.send_yard_carrier(island, TilePos{x, y}, ware, building->worker_figure);
}

void Simulation::logistics_step() {
    m_logistics.tick();
    for (const Logistics::YardDelivery& d : m_logistics.take_yard_deliveries()) {
        // Shipyards and castles (each ignores what is not its own).
        const auto x = static_cast<std::uint16_t>(d.origin.x);
        const auto y = static_cast<std::uint16_t>(d.origin.y);
        m_fleet.yard_delivery(d.island, x, y, d.ware, d.amount);
        m_military.castle_delivery(d.island, x, y, d.ware, d.amount);
    }
    m_disaster_events.append(m_logistics.take_disaster_events());
}
void Simulation::pay_upkeep() {
    const std::vector<std::uint32_t> upkeep = military_upkeep(*m_world, *m_figures);
    for (std::size_t p = 0; p < upkeep.size(); ++p) {
        // Computer players pay nothing, as in the original (0x00475B00).
        Player* player = m_world->player(static_cast<PlayerId>(p));
        if (player != nullptr && player->role == PlayerRole::Human) {
            player->money = money_plus(player->money, -static_cast<std::int64_t>(upkeep[p] / 12));
        }
    }
}

void Simulation::players_tick() {
    // Player_Tick 0x00476350: each player once per 5 s; nothing more once the game is over.
    if (m_world->game_end()) {
        return;
    }
    std::size_t humans = 0;
    for (const Player& p : m_world->players()) {
        humans += p.role == PlayerRole::Human && p.status != PlayerStatus::Absent ? 1 : 0;
    }
    const bool multiplayer = humans > 1;
    // The victory conditions of an endless game: alone "your task is fulfilled", with others the
    // message with the winner's colour, as the original's multiplayer races.
    const VictoryConditions& victory = m_rules.victory;
    const std::uint16_t victory_message = multiplayer ? kColourWinMessage : kMissionMessage;
    if (time_limit_over(victory, m_time_ms)) {
        if (const PlayerId winner = time_limit_winner(*m_world, *m_buildings, victory); winner != kNoPlayer) {
            accomplish(winner, victory_message, false);
            return;
        }
    }
    for (std::size_t i = 0; i < m_world->players().size(); ++i) {
        const auto id = static_cast<PlayerId>(i);
        Player* player = m_world->player(id);
        if (player == nullptr || !player->active()) {
            continue;
        }
        Player& p = *player;
        p.purchases = p.purchases * 179 / 180;
        p.sales = p.sales * 179 / 180;
        const MissionCheck check = check_mission(*m_world, *m_buildings, *m_figures, id);
        if (check.won) {
            accomplish(id);
            return;
        }
        if (victory.goals() && check_victory(*m_world, *m_buildings, *m_figures, victory, id).won) {
            accomplish(id, victory_message, true);
            return;
        }
        const bool city = owns_city(*m_world, id);
        const bool ship = owns_ship(*m_world, id);
        switch (p.role) {
            case PlayerRole::Neutral:
                // Only the pirates, when a mission asks to defeat them (settings bit 5).
                if (m_world->behaviour(p) == FactionBehaviour::Pirates &&
                    (m_world->game_settings().flags & World::GameSettings::kPirateGoal) != 0 && !city && !ship) {
                    defeat(id, GameEvent::Reason::Retired, true);
                }
                break;
            case PlayerRole::Computer:
                // The AI root retires a computer with no city and no ship (0x00429070), not in
                // the tutorial (0x0042942C).
                if (!city && !ship && !m_world->game_settings().tutorial) {
                    defeat(id, GameEvent::Reason::Retired, true);
                }
                break;
            case PlayerRole::Human:
                if (check.lost) {
                    defeat(id, GameEvent::Reason::MissionFailed, true);
                } else if (city || ship) {
                    p.empty_checks = 0;
                    // Bankrupt after 40 checks in a row; the original never resets the counter,
                    // so 40 bad checks over the whole game are enough (S-107).
                    if (p.money <= -1001 && player_balance(*m_world, *m_buildings, *m_figures, id) <= 0) {
                        if (++p.bankrupt_checks >= kDefeatChecks) {
                            defeat(id, GameEvent::Reason::Bankrupt, false);
                        }
                    } else {
                        p.bankrupt_checks = 0;
                    }
                } else if (!multiplayer || ++p.empty_checks >= kDefeatChecks) {
                    // Nothing left: at once alone, after 200 s with other humans in the game.
                    defeat(id, GameEvent::Reason::NoPossessions, true);
                }
                break;
        }
    }
}

void Simulation::accomplish(PlayerId winner) {
    const Player* p = m_world->player(winner);
    const Mission* m = p != nullptr ? m_world->mission_of(*p) : nullptr;
    accomplish(winner, m != nullptr ? m->success_message : std::uint16_t{0}, true);
}

void Simulation::accomplish(PlayerId winner, std::uint16_t message, bool co_winners) {
    // Op 0x3E (0x00476950): the winner, and every other human or computer who meets his own
    // mission at this moment; the original checks players without a mission against mission 0
    // (S-106). Victory conditions count as everyone's mission, but a time limit has one winner.
    // The game is over for everybody.
    const VictoryConditions& victory = m_rules.victory;
    const auto win = [&](PlayerId id, std::uint16_t text) {
        Player* p = m_world->player(id);
        if (p == nullptr) {
            return;
        }
        p->won = true;
        m_game_events.push(
            GameEvent{GameEvent::Kind::MissionAccomplished, id, kNoPlayer, GameEvent::Reason::None, text});
    };
    win(winner, message);
    for (std::size_t i = 0; i < m_world->players().size() && co_winners; ++i) {
        const auto id = static_cast<PlayerId>(i);
        if (id == winner || !m_world->players()[i].active()) {
            continue;
        }
        if (check_mission(*m_world, *m_buildings, *m_figures, id).won) {
            const Mission* m = m_world->mission_of(m_world->players()[i]);
            win(id, m != nullptr ? m->success_message : std::uint16_t{0});
        } else if (victory.goals() && check_victory(*m_world, *m_buildings, *m_figures, victory, id).won) {
            win(id, message);
        }
    }
    m_world->set_game_end(World::GameEnd{m_time_ms, winner});
    m_game_events.push(GameEvent{GameEvent::Kind::GameOver, winner, kNoPlayer, GameEvent::Reason::None, message});
}

void Simulation::defeat(PlayerId id, GameEvent::Reason reason, bool blame) {
    // Op 3 sub 9 (0x004766A0): the player is out. Unless he went bankrupt, the player blamed for
    // it (who last destroyed his Kontor or market) counts him as defeated. With 200 inhabitants
    // of the third class or higher in his largest city he may build a triumphal arch; otherwise
    // the allowance counts as used up straight away. The original does not credit him on the
    // defeated player's computer (S-105).
    Player* p = m_world->player(id);
    if (p == nullptr) {
        return;
    }
    p->status = PlayerStatus::Defeated;
    PlayerId by = blame ? m_world->diplomacy().blamed(id) : kNoPlayer;
    Player* k = by != id ? m_world->player(by) : nullptr;
    if (k == nullptr || m_world->behaviour(*p) == FactionBehaviour::Natives) {
        by = kNoPlayer;  // nobody is credited, and nobody hears of it
        k = nullptr;
    }
    m_game_events.push(GameEvent{GameEvent::Kind::PlayerDefeated, id, by, reason});
    if (k == nullptr) {
        return;
    }
    add_count(k->opponents_defeated, 1);
    add_count(k->arches_allowed, 1);
    if (upper_inhabitants_of_largest_city(by) >= kArchInhabitants) {
        k->set_unlocked(infra::kArch, true);
        m_game_events.push(GameEvent{GameEvent::Kind::ArchAllowed, by, id});
    } else {
        add_count(k->arches_built, 1);
    }
}

std::uint64_t Simulation::upper_inhabitants_of_largest_city(PlayerId player) const {
    // 0x004805D0 with 0x00480580: the first city of the player with the most Einwohner
    // (0x0047F790, workers included); a player whose cities are all empty has none.
    const City* largest = nullptr;
    std::uint32_t most = 0;
    for (const City& c : m_world->cities()) {
        if (c.owner != player) {
            continue;
        }
        const std::uint32_t n = city_einwohner(c, city_workers(*m_world, *m_buildings, c));
        if (n > most) {
            largest = &c;
            most = n;
        }
    }
    std::uint64_t upper = 0;
    if (largest != nullptr) {
        for (std::size_t r = kArchClass; r < largest->inhabitants.size(); ++r) {
            upper += largest->inhabitants[r];
        }
    }
    return upper;
}

std::vector<ShipEvent> Simulation::take_ship_events() {
    m_ship_events.append(m_fleet.take_events());  // events of commands applied outside a tick
    return m_ship_events.take();
}

}  // namespace o1602::sim
