// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/savegame_writer.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/savegame_extension.hpp"

namespace o1602::sim {

using core::ErrorCode;
using core::make_error;

namespace {

// Limits of the original (docs/re/savegame-writer.md, section 7).
constexpr std::size_t kMaxIslands = 50;
constexpr std::size_t kMaxOwnIslands = 255;  // island numbers are one byte
/// Island coordinates are one byte in the original's records (INSELHAUS, SIEDLER, KONTOR2, HIRSCH2,
/// craters, object refs), so an island is at most 255 tiles wide and high. Open1602 itself keeps
/// 16 bits (SC-13): the limit binds only this export.
constexpr std::uint32_t kMaxIslandSide = 255;
constexpr std::size_t kMaxCities = 75;
constexpr std::size_t kMaxShips = 150;
constexpr std::size_t kMaxSoldiers = 400;
constexpr std::size_t kMaxTowers = 350;
constexpr std::size_t kTowerSize = 0x24;
constexpr std::size_t kOriginalMissionSlots = 4;  // 0x005B8A80: 4 x 0x88C
constexpr std::size_t kCityNameMax = 23;
constexpr std::size_t kShipNameMax = 23;
constexpr std::uint8_t kNoOwner = 7;
constexpr std::uint32_t kIdOffset = 20000;

// Record sizes.
constexpr std::size_t kIslandHeaderSize = 0x74;
constexpr std::size_t kTileSize = 8;
constexpr std::size_t kDeerSize = 8;
constexpr std::size_t kProductionSize = 0x18;
constexpr std::size_t kShipyardSize = 0x18;
constexpr std::size_t kCastleSize = 0x70;
constexpr std::size_t kHouseSize = 0x10;
constexpr std::size_t kGrowthSize = 8;
constexpr std::size_t kCitySize = 0xA8;
constexpr std::size_t kWarehouseSize = 0x3EC;
constexpr std::size_t kConsumptionSize = 0x104;
constexpr std::size_t kPlayerSize = 0x430;
constexpr std::size_t kShipSize = 0x1B4;
constexpr std::size_t kSoldierSize = 0x44;
constexpr std::size_t kTimersSize = 0x298;

void put16(core::Bytes& d, std::size_t o, std::uint32_t v) {
    d[o] = static_cast<std::uint8_t>(v);
    d[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(core::Bytes& d, std::size_t o, std::uint32_t v) {
    for (std::size_t k = 0; k < 4; ++k) {
        d[o + k] = static_cast<std::uint8_t>(v >> (8 * k));
    }
}
std::uint32_t get16(std::span<const std::uint8_t> d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o] | (d[o + 1] << 8));
}
std::uint32_t get32(std::span<const std::uint8_t> d, std::size_t o) {
    return static_cast<std::uint32_t>(d[o]) | (static_cast<std::uint32_t>(d[o + 1]) << 8) |
           (static_cast<std::uint32_t>(d[o + 2]) << 16) | (static_cast<std::uint32_t>(d[o + 3]) << 24);
}
std::uint32_t clamp32(std::uint64_t v) {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(v, 0xFFFFFFFFU));
}

constexpr std::uint64_t key3(std::uint32_t a, std::uint32_t b, std::uint32_t c) noexcept {
    return (std::uint64_t{a} << 32) | (std::uint64_t{b} << 16) | c;
}

/// Records of the source file: the starting point of every record the writer patches.
class Source {
public:
    explicit Source(const assets::ChunkFile* file) : m_file(file) {}

    /// All records of a chunk (all chunks of that name), in file order.
    [[nodiscard]] std::vector<std::span<const std::uint8_t>> records(std::string_view name, std::size_t size) const {
        std::vector<std::span<const std::uint8_t>> out;
        if (m_file == nullptr) {
            return out;
        }
        for (const assets::Chunk& c : m_file->chunks()) {
            if (c.name != name) {
                continue;
            }
            const auto data = m_file->payload(c);
            for (std::size_t o = 0; o + size <= data.size(); o += size) {
                out.push_back(data.subspan(o, size));
            }
        }
        return out;
    }
    /// Records by a key.
    template <class Key>
    [[nodiscard]] std::unordered_map<std::uint64_t, std::span<const std::uint8_t>> by_key(std::string_view name,
                                                                                          std::size_t size,
                                                                                          Key key) const {
        std::unordered_map<std::uint64_t, std::span<const std::uint8_t>> out;
        for (const auto r : records(name, size)) {
            out.emplace(key(r), r);
        }
        return out;
    }
    /// The first chunk of that name, whole.
    [[nodiscard]] std::optional<std::span<const std::uint8_t>> chunk(std::string_view name) const {
        if (m_file == nullptr) {
            return std::nullopt;
        }
        const assets::Chunk* c = m_file->find(name);
        if (c == nullptr) {
            return std::nullopt;
        }
        return m_file->payload(*c);
    }
    [[nodiscard]] const assets::ChunkFile* file() const noexcept { return m_file; }

private:
    const assets::ChunkFile* m_file;
};

/// A record started from the source one, or zeros.
core::Bytes start(const std::unordered_map<std::uint64_t, std::span<const std::uint8_t>>& raw, std::uint64_t key,
                  std::size_t size) {
    core::Bytes r(size, 0);
    if (const auto it = raw.find(key); it != raw.end()) {
        std::copy(it->second.begin(), it->second.end(), r.begin());
    }
    return r;
}

class Writer {
public:
    Writer(const World& world, const assets::BuildingTable& buildings)
        : m_world(world), m_buildings(buildings), m_source(world.source().get()) {}

    core::Result<std::vector<SavedChunk>> run() {
        if (auto status = check(); !status) {
            return status.error();
        }
        assign_ship_slots();
        assign_unit_slots();
        islands();
        passthrough("CUSTOM");
        passthrough("NAME");
        production();
        cost_buildings(CostBuilding::Kind::Shipyard, "WERFT", kShipyardSize);
        cost_buildings(CostBuilding::Kind::Castle, "MILITAR", kCastleSize);
        houses();
        growth();
        cities();
        players();
        missions();
        traders();
        ships();
        soldiers();
        towers();
        timers();
        return std::move(m_out);
    }

private:
    const World& m_world;
    const assets::BuildingTable& m_buildings;
    Source m_source;
    std::vector<SavedChunk> m_out;

    void add(std::string name, core::Bytes data) { m_out.push_back(SavedChunk{std::move(name), std::move(data)}); }

    core::Status check() const {
        // A world of the original's size keeps the original's limits, so that the original can
        // load the file; a larger one is Open1602's own (O1602MAP) and only bound by the format.
        const bool original = m_world.width() == World::kOriginalWidth && m_world.height() == World::kOriginalHeight;
        const std::size_t max_islands = original ? kMaxIslands : kMaxOwnIslands;
        if (m_world.islands().size() > max_islands) {
            return make_error(ErrorCode::LimitExceeded, "more than " + std::to_string(max_islands) + " islands");
        }
        const auto fits = [](std::uint32_t x, std::uint32_t y) { return x <= 0xFF && y <= 0xFF; };
        for (const WorldIsland& i : m_world.islands()) {
            if (i.header.number >= max_islands) {
                return make_error(ErrorCode::LimitExceeded, "island number " + std::to_string(i.header.number));
            }
            if (i.map.width() > kMaxIslandSide || i.map.height() > kMaxIslandSide) {
                return make_error(ErrorCode::LimitExceeded,
                                  "island " + std::to_string(i.header.number) + " is wider or higher than 255 tiles");
            }
            // Within such an island a valid world's craters and deer spots fit one byte; a world
            // built in code is checked all the same rather than written with cut coordinates.
            const bool craters =
                std::all_of(i.craters.begin(), i.craters.end(), [&](const Crater& c) { return fits(c.x, c.y); });
            const bool spots = std::all_of(i.deer_spots.begin(), i.deer_spots.end(),
                                           [&](const DeerSpot& d) { return !d.used || fits(d.x, d.y); });
            const bool deposits = std::all_of(i.header.deposits.begin(), i.header.deposits.end(),
                                              [&](const assets::IslandHeader::Deposit& d) { return fits(d.x, d.y); });
            if (!craters || !spots || !deposits) {
                return make_error(ErrorCode::LimitExceeded, "a crater, deer spot or deposit of island " +
                                                                std::to_string(i.header.number) + " beyond 255 tiles");
            }
        }
        const VolcanoState& volcanoes = m_world.volcanoes();
        const bool last = !volcanoes.last || fits(volcanoes.last->x, volcanoes.last->y);
        const bool running = std::all_of(volcanoes.eruptions.begin(), volcanoes.eruptions.end(),
                                         [&](const Eruption& e) { return fits(e.crater.x, e.crater.y); });
        if (!last || !running) {
            return make_error(ErrorCode::LimitExceeded, "an erupting crater beyond 255 tiles");
        }
        if (m_world.width() > 0xFFFF || m_world.height() > 0xFFFF) {
            return make_error(ErrorCode::LimitExceeded, "the world is too large for the file format");
        }
        if (original && m_world.cities().size() > kMaxCities) {
            return make_error(ErrorCode::LimitExceeded, "more than 75 cities");
        }
        if (m_world.players().size() > assets::kOriginalPlayerSlots) {
            return make_error(ErrorCode::LimitExceeded, "more than 7 players");
        }
        if (m_world.ships().size() > kMaxShips) {
            return make_error(ErrorCode::LimitExceeded, "more than 150 ships");
        }
        if (m_world.soldiers().size() > kMaxSoldiers) {
            return make_error(ErrorCode::LimitExceeded, "more than 400 soldiers");
        }
        if (m_world.towers().size() > kMaxTowers) {
            return make_error(ErrorCode::LimitExceeded, "more than 350 towers");
        }
        return core::Ok{};
    }

    /// The island's number in the original's one byte (check() refused numbers above it).
    [[nodiscard]] std::uint8_t number(std::uint16_t island) const {
        return static_cast<std::uint8_t>(m_world.islands()[island].header.number);
    }

    /// Places of the ships in the original's table of 150: the place a ship was loaded from, the
    /// first free one for the others. The free trader's ships get HANDLER records in order.
    std::unordered_map<std::uint32_t, std::uint16_t> m_ship_slots;
    std::unordered_map<std::uint32_t, std::uint8_t> m_trader_records;
    void assign_ship_slots() {
        std::vector<bool> used(kMaxShips, false);
        for (const Ship& s : m_world.ships()) {
            if (s.source_slot < kMaxShips && !used[s.source_slot]) {
                used[s.source_slot] = true;
                m_ship_slots[s.id] = s.source_slot;
            }
        }
        std::uint16_t next = 0;
        std::uint8_t trader = 0;
        for (const Ship& s : m_world.ships()) {
            if (m_ship_slots.find(s.id) == m_ship_slots.end()) {
                while (next < kMaxShips && used[next]) {
                    ++next;
                }
                used[std::min<std::size_t>(next, kMaxShips - 1)] = true;
                m_ship_slots[s.id] = next;
            }
            if (s.trader) {
                m_trader_records[s.id] = trader++;
            }
        }
    }
    /// Places of soldiers (table of 400) and towers (350), likewise.
    std::unordered_map<std::uint32_t, std::uint16_t> m_soldier_slots;
    std::vector<std::uint16_t> m_tower_slots;
    template <class List, class Key, class Store>
    static void assign_slots(const List& list, std::size_t max, Key key, Store store) {
        std::vector<bool> used(max, false);
        std::vector<bool> placed(list.size(), false);
        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::uint16_t slot = list[i].source_slot;
            if (slot < max && !used[slot]) {
                used[slot] = true;
                placed[i] = true;
                store(key(list[i], i), slot);
            }
        }
        std::uint16_t next = 0;
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (!placed[i]) {
                while (next < max && used[next]) {
                    ++next;
                }
                used[std::min<std::size_t>(next, max - 1)] = true;
                store(key(list[i], i), next);
            }
        }
    }
    void assign_unit_slots() {
        assign_slots(
            m_world.soldiers(), kMaxSoldiers, [](const Soldier& s, std::size_t) { return s.id; },
            [&](std::uint32_t id, std::uint16_t slot) { m_soldier_slots[id] = slot; });
        m_tower_slots.assign(m_world.towers().size(), 0);
        assign_slots(
            m_world.towers(), kMaxTowers, [](const Tower&, std::size_t i) { return static_cast<std::uint32_t>(i); },
            [&](std::uint32_t i, std::uint16_t slot) { m_tower_slots[i] = slot; });
    }
    /// A reference that still names the same target stays as stored.
    void put_ref(core::Bytes& r, std::size_t o, const Target& t) const { put32(r, o, ref_of(t)); }
    [[nodiscard]] std::uint8_t ship_kind(const Ship& s) const {
        if (s.trader) {
            return 2;
        }
        if (s.cart()) {
            return 5;  // the trade cart (TRADER1)
        }
        const Player* p = m_world.player(s.owner);
        return p != nullptr && m_world.behaviour(*p) == FactionBehaviour::Pirates ? 3 : 1;
    }
    /// A target as the original's object reference (docs/re/ships.md).
    [[nodiscard]] std::uint32_t ref_of(const Target& t) const {
        // A reference loaded from a file goes back as it was while it names the same target.
        if (t.raw != 0 && target_from_ref(m_world, t.raw) == t) {
            return t.raw;
        }
        switch (t.kind) {
            case Target::Kind::Position:
                return 0x37U | ((t.y >> 8) << 4 | (t.x >> 8)) << 8 | (t.x & 0xFFU) << 16 | (t.y & 0xFFU) << 24;
            case Target::Kind::City:
                return t.island < m_world.islands().size()
                           ? 0x35U | std::uint32_t{number(t.island)} << 8 | std::uint32_t{t.city} << 16
                           : 0U;
            case Target::Kind::Building:
                return t.island < m_world.islands().size()
                           ? 0x33U | std::uint32_t{number(t.island)} << 8 | (t.x & 0xFFU) << 16 | (t.y & 0xFFU) << 24
                           : 0U;
            case Target::Kind::Ship: {
                const Ship* other = m_world.ship(t.ship);
                const auto it = m_ship_slots.find(t.ship);
                if (other == nullptr || it == m_ship_slots.end()) {
                    return 0;
                }
                return ship_kind(*other) | 0xFFU << 8 | std::uint32_t{it->second} << 16;
            }
            case Target::Kind::Soldier: {
                const Soldier* other = m_world.soldier(t.ship);
                const auto it = m_soldier_slots.find(t.ship);
                if (other == nullptr || it == m_soldier_slots.end()) {
                    return 0;
                }
                const std::uint8_t island = other->island < m_world.islands().size() ? number(other->island) : 0xFF;
                return 4U | std::uint32_t{island} << 8 | std::uint32_t{it->second} << 16;
            }
            case Target::Kind::Spot:
                return 0x38U | ((t.y >> 8) << 4 | (t.x >> 8)) << 8 | (t.x & 0xFFU) << 16 | (t.y & 0xFFU) << 24;
            case Target::Kind::None:
                break;
        }
        return 0;
    }
    /// The colour variant of a unit for the owner's banner (FIGUR family + colour); a banner without
    /// own figures keeps the stored variant.
    [[nodiscard]] std::uint32_t unit_figure(std::uint32_t family, PlayerId owner, std::uint32_t stored) const {
        const auto variant = assets::FigureTable::unit_variant(family);
        if (!variant) {
            return family;
        }
        const Player* p = m_world.player(owner);
        const auto old = assets::FigureTable::unit_variant(stored);
        if (p != nullptr && p->banner < variant->count) {
            return variant->family + p->banner;
        }
        return old && old->family == variant->family ? stored : family;
    }
    [[nodiscard]] std::uint8_t owner_byte(PlayerId p) const {
        return p < assets::kOriginalPlayerSlots ? static_cast<std::uint8_t>(p) : kNoOwner;
    }

    /// A chunk Open1602 does not model yet, as the source has it; `always`: the original writes it
    /// even when empty (a new endless game has no trader and no towers).
    void passthrough(std::string_view name, bool always = false) {
        if (const auto c = m_source.chunk(name)) {
            add(std::string(name), core::Bytes(c->begin(), c->end()));
        } else if (always) {
            add(std::string(name), {});
        }
    }

    // ---- Islands ---------------------------------------------------------------------------------

    /// Islands in the order of their numbers, as the original writes them.
    [[nodiscard]] std::vector<std::uint16_t> island_order() const {
        std::vector<std::uint16_t> order(m_world.islands().size());
        for (std::uint16_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::stable_sort(order.begin(), order.end(),
                         [&](std::uint16_t a, std::uint16_t b) { return number(a) < number(b); });
        return order;
    }

    void islands() {
        const std::vector<std::uint16_t> order = island_order();
        // Source tiles of each island and layer: the random bits the original keeps per tile.
        std::map<std::pair<std::uint16_t, std::size_t>, std::unordered_map<std::uint32_t, assets::IslandTile>> tiles;
        if (m_source.file() != nullptr) {
            if (auto loaded = assets::load_islands(*m_source.file())) {
                for (const assets::Island& island : *loaded) {
                    for (std::size_t l = 0; l < island.layers.size(); ++l) {
                        auto& m = tiles[{island.header.number, l}];
                        for (const assets::IslandTile& t : island.layers[l]) {
                            m.emplace(std::uint32_t{t.y} << 8 | t.x, t);
                        }
                    }
                }
            }
        }
        for (const std::uint16_t i : order) {
            const WorldIsland& island = m_world.islands()[i];
            add("INSEL5", island_header(i));
            add("INSELHAUS", island_layer(i, false, tiles[{island.header.number, 0}]));
            add("INSELHAUS", island_layer(i, true, tiles[{island.header.number, 1}]));
            add("HIRSCH2", deer(i));
        }
    }

    core::Bytes island_header(std::uint16_t i) const {
        const WorldIsland& island = m_world.islands()[i];
        const assets::IslandHeader& h = island.header;
        core::Bytes r(kIslandHeaderSize, 0);
        if (!h.raw.empty()) {
            std::copy_n(h.raw.begin(), std::min(h.raw.size(), r.size()), r.begin());
        } else {
            // A game from Open1602's own savegame: the bytes of the original's file it came from.
            for (const auto& raw : m_source.records("INSEL5", kIslandHeaderSize)) {
                if (!raw.empty() && raw[0] == h.number) {
                    std::copy_n(raw.begin(), std::min(raw.size(), r.size()), r.begin());
                    break;
                }
            }
        }
        r[0] = static_cast<std::uint8_t>(h.number);  // fits: check()
        // The island template the original builds the island from (+0x60..+0x65).
        if (h.chunk_name == "INSEL5" || h.raw.empty()) {
            put16(r, 0x60, h.template_number);
            put16(r, 0x62, h.template_size);
            r[0x64] = h.climate;
            r[0x65] = h.stored_complete ? 1 : 0;
        }
        r[1] = static_cast<std::uint8_t>(island.map.width());
        r[2] = static_cast<std::uint8_t>(island.map.height());
        r[3] = static_cast<std::uint8_t>((r[3] & ~5U) | (island.drought.curse_pending ? 1U : 0U) |
                                         (island.volcano_erupted ? 4U : 0U));
        put16(r, 4, island.x);
        put16(r, 6, island.y);
        r[8] = static_cast<std::uint8_t>((r[8] & ~3U) | (island.deer_phase & 3U));
        r[0x0A] = static_cast<std::uint8_t>((r[0x0A] & ~7U) | (island.last_phase & 7U));
        // Owners of the eight city slots.
        for (std::uint8_t c = 0; c < kCitySlotsPerIsland; ++c) {
            const City* city = m_world.city(i, c);
            r[0x0C + c] = city != nullptr ? owner_byte(city->owner) : kNoOwner;
        }
        // Volcano craters (8 bytes each from 0x3C).
        r[0x17] = static_cast<std::uint8_t>(std::min<std::size_t>(island.craters.size(), 2));
        for (std::size_t k = 0; k < 2; ++k) {
            const std::size_t o = 0x3C + 8 * k;
            if (k < island.craters.size()) {
                const Crater& c = island.craters[k];
                r[o] = 0;
                r[o + 1] = static_cast<std::uint8_t>(c.x);  // fits: check()
                r[o + 2] = static_cast<std::uint8_t>(c.y);
                r[o + 3] = r[o + 4] = r[o + 5] = 0;
                put16(r, o + 6, c.steps_left);
            } else {
                std::fill_n(r.begin() + static_cast<std::ptrdiff_t>(o), 8, std::uint8_t{0});
            }
        }
        // Deposits (8 bytes each from 0x1C).
        r[0x1A] = static_cast<std::uint8_t>(std::min<std::size_t>(h.deposits.size(), 2));
        for (std::size_t k = 0; k < 2; ++k) {
            const std::size_t o = 0x1C + 8 * k;
            if (k < h.deposits.size()) {
                const auto& d = h.deposits[k];
                r[o] = d.ware;
                r[o + 1] = static_cast<std::uint8_t>(d.x);  // fits: check()
                r[o + 2] = static_cast<std::uint8_t>(d.y);
                r[o + 3] = static_cast<std::uint8_t>(d.mask);
                r[o + 4] = d.kind;
                put16(r, o + 6, d.amount);
            } else {
                std::fill_n(r.begin() + static_cast<std::ptrdiff_t>(o), 8, std::uint8_t{0});
            }
        }
        // The loader ORs 0x1181 into the fertility: keep the stored bits if they give the same.
        constexpr std::uint32_t kAlwaysFertile = 0x1181;
        if ((get32(r, 0x5C) | kAlwaysFertile) != (h.fertility | kAlwaysFertile)) {
            put32(r, 0x5C, h.fertility);
        }
        r[0x66] = island.drought.strength;
        std::uint32_t discovered = 0;
        for (std::uint8_t p = 0; p < assets::kOriginalPlayerSlots; ++p) {
            discovered |= island.discovered_by(p) ? 1U << p : 0U;
        }
        constexpr std::uint32_t kSlots = (1U << assets::kOriginalPlayerSlots) - 1U;
        put32(r, 0x68, (get32(r, 0x68) & ~kSlots) | discovered);
        std::uint32_t explored = 0;
        for (std::uint8_t p = 0; p < assets::kOriginalPlayerSlots; ++p) {
            explored |= island.explored_by(p) ? 1U << p : 0U;
        }
        r[0x1B] = static_cast<std::uint8_t>((r[0x1B] & ~kSlots) | explored);
        put32(r, 0x6C, clamp32(island.drought.end_ms / 100));
        return r;
    }

    /// Layer 1 (`ground`): every building of the ground layer, of no city and no owner. Layer 2:
    /// the buildings on top that differ from the ground (0x00468BF0), with their city and owner.
    core::Bytes island_layer(std::uint16_t i, bool top_layer,
                             const std::unordered_map<std::uint32_t, assets::IslandTile>& source) const {
        const WorldIsland& island = m_world.islands()[i];
        const IslandMap& map = island.map;
        core::Bytes out;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const MapCell& cell = top_layer ? map.top(x, y) : map.ground(x, y);
                if (cell.empty()) {
                    continue;
                }
                const SubTile sub =
                    top_layer ? map.sub_tile(m_buildings, x, y) : map.ground_sub_tile(m_buildings, x, y);
                if (sub.building == nullptr || sub.origin_x != static_cast<std::int32_t>(x) ||
                    sub.origin_y != static_cast<std::int32_t>(y)) {
                    continue;
                }
                if (top_layer && same_as_ground(map, x, y)) {
                    continue;
                }
                const std::uint8_t city = top_layer && cell.city != MapCell::kNoCity ? cell.city : 7;
                const std::uint8_t owner =
                    top_layer && cell.city != MapCell::kNoCity ? owner_byte(cell.owner) : kNoOwner;
                std::uint32_t phase = cell.anim_phase;
                if (sub.building->anim_count > 0) {
                    phase %= sub.building->anim_count;
                }
                std::uint32_t bits = (cell.rotation & 3U) | (phase & 0xFU) << 2 |
                                     std::uint32_t{island.header.number} << 6 | std::uint32_t{city & 7U} << 14 |
                                     std::uint32_t{owner & 0xFU} << 22;
                const auto id = static_cast<std::uint16_t>(sub.building->id - kIdOffset);
                bits |= std::uint32_t{cell.random & 0x1FU} << 17;
                if (const auto it = source.find(y << 8 | x); it != source.end() && it->second.id == id) {
                    bits |= it->second.bits & (0x3FU << 26);  // bits of unknown meaning
                }
                const std::size_t o = out.size();
                out.resize(o + kTileSize);
                put16(out, o, id);
                out[o + 2] = static_cast<std::uint8_t>(x);
                out[o + 3] = static_cast<std::uint8_t>(y);
                put32(out, o + 4, bits);
            }
        }
        return out;
    }

    /// Whether the top tile counts as the ground's (0x00468BF0): no city, and the same picture
    /// (0x00464F30), or water or grass of the same ware, growing or not. The original then does not
    /// save it; on loading, the tile shows the ground.
    [[nodiscard]] bool same_as_ground(const IslandMap& map, std::uint32_t x, std::uint32_t y) const {
        const MapCell& t = map.top(x, y);
        const MapCell& g = map.ground(x, y);
        if (t.city != MapCell::kNoCity) {
            return false;
        }
        if (t.sprite == g.sprite && t.rotation == g.rotation && t.anim_phase == g.anim_phase) {
            return true;
        }
        const assets::BuildingType* a = t.empty() ? nullptr : m_buildings.by_sprite(t.sprite);
        const assets::BuildingType* b = g.empty() ? nullptr : m_buildings.by_sprite(g.sprite);
        const auto ripe = [&](const assets::BuildingType* r) {
            return r != nullptr && r->prod_kind == prod::kGrowing ? m_buildings.by_record(r->record + 1) : r;
        };
        a = ripe(a);
        b = ripe(b);
        return a != nullptr && b != nullptr && (a->kind == kind::kSea || a->ware == ware::kGrass) && a->ware == b->ware;
    }

    core::Bytes deer(std::uint16_t i) const {
        const WorldIsland& island = m_world.islands()[i];
        const auto raw = m_source.by_key("HIRSCH2", kDeerSize, [](auto r) { return key3(r[0], r[1], r[2]); });
        core::Bytes out;
        for (const DeerSpot& s : island.deer_spots) {
            if (!s.used) {
                continue;
            }
            core::Bytes r = start(raw, key3(island.header.number, s.x, s.y), kDeerSize);
            r[0] = static_cast<std::uint8_t>(island.header.number);
            r[1] = static_cast<std::uint8_t>(s.x);  // fits: check()
            r[2] = static_cast<std::uint8_t>(s.y);
            put32(r, 4, clamp32(s.respawn_ms / 100));
            out.insert(out.end(), r.begin(), r.end());
        }
        return out;
    }

    // ---- Building lists --------------------------------------------------------------------------

    void production() {
        const auto raw = m_source.by_key("PRODLIST2", kProductionSize, [](auto r) { return key3(r[0], r[1], r[2]); });
        core::Bytes out;
        for (const Production& p : m_world.productions()) {
            if (!p.used) {
                continue;
            }
            const std::uint8_t n = number(p.island);
            core::Bytes r = start(raw, key3(n, p.x, p.y), kProductionSize);
            r[0] = n;
            r[1] = static_cast<std::uint8_t>(p.x);
            r[2] = static_cast<std::uint8_t>(p.y);
            put16(r, 5, p.stock);
            put16(r, 8, p.countdown);
            put16(r, 0x0A, p.raw2);
            put16(r, 0x0C, p.raw);
            r[0x0F] = p.rate;
            put16(r, 0x10, p.produced);
            put16(r, 0x12, p.elapsed);
            r[0x14] = static_cast<std::uint8_t>((r[0x14] & 0x80U) | (p.enabled ? 1U : 0U) | ((p.flags >> 6) & 1U) << 1 |
                                                (p.anim_phase & 0xFU) << 2 | ((p.flags >> 4) & 1U) << 6);
            r[0x15] = static_cast<std::uint8_t>((r[0x15] & 0xF0U) | (p.idle_cycles & 0xFU));
            out.insert(out.end(), r.begin(), r.end());
        }
        add("PRODLIST2", std::move(out));
    }

    void cost_buildings(CostBuilding::Kind kind, const char* name, std::size_t size) {
        const auto raw = m_source.by_key(name, size, [](auto r) { return key3(r[0], r[1], r[2]); });
        core::Bytes out;
        for (const CostBuilding& b : m_world.cost_buildings()) {
            if (b.kind != kind) {
                continue;
            }
            const std::uint8_t n = number(b.island);
            core::Bytes r = start(raw, key3(n, b.x, b.y), size);
            if (raw.find(key3(n, b.x, b.y)) == raw.end() && kind == CostBuilding::Kind::Shipyard) {
                r[6] = 0xFF;  // no ship under construction
            }
            r[0] = n;
            r[1] = static_cast<std::uint8_t>(b.x);
            r[2] = static_cast<std::uint8_t>(b.y);
            if (kind == CostBuilding::Kind::Shipyard) {
                r[3] = static_cast<std::uint8_t>((r[3] & ~0x08U) | (b.enabled ? 0x08U : 0U));
                put16(r, 4, b.countdown);
                r[6] = b.order;
                put16(r, 8, std::min<std::uint32_t>(b.progress, 0xFFFF));
                put16(r, 0x0A, std::min<std::uint32_t>(b.wood, 0xFFFF));
                const auto slot = m_ship_slots.find(b.repair_ship);
                put16(r, 0x0C, b.repair_ship != 0 && slot != m_ship_slots.end() ? slot->second : 0xFFFFU);
            } else {
                put16(r, 4, b.countdown);
                for (std::size_t w = 0; w < b.weapons.size(); ++w) {
                    put16(r, 8 + 2 * w, std::min<std::uint32_t>(b.weapons[w], 0xFFFF));
                }
                for (std::size_t k = 0; k < 9; ++k) {
                    const std::size_t o = 0x10 + 8 * k;
                    if (k < b.slots.size() && b.slots[k].figure != 0) {
                        const CastleSlot& c = b.slots[k];
                        const PlayerId owner = m_world.islands()[b.island].map.top(b.x, b.y).owner;
                        const std::uint32_t figure = unit_figure(c.figure, owner, get32(r, o) & 0xFFFU);
                        put32(r, o,
                              (get32(r, o) & ~0x3FFFU) | (figure & 0xFFFU) | (c.training ? 1U << 12 : 0U) |
                                  (c.armed ? 1U << 13 : 0U));
                        put32(r, o + 4, std::min<std::uint32_t>(c.health, 0xFFF) | std::uint32_t{c.progress} << 16);
                    } else if ((get32(r, o) & 0xFFFU) != 0) {
                        // An emptied place; one empty already keeps what the original left in it.
                        put32(r, o, 0);
                        put32(r, o + 4, 0);
                    }
                }
            }
            out.insert(out.end(), r.begin(), r.end());
        }
        add(name, std::move(out));
    }

    void houses() {
        const auto raw = m_source.by_key("SIEDLER", kHouseSize, [](auto r) { return key3(r[0], r[1], r[2]); });
        // Runtime word bits 2..10 -> dword 2 bits (0x00485530).
        constexpr std::array<std::uint8_t, 9> kServiceBits = {30, 23, 24, 28, 25, 26, 29, 31, 27};
        core::Bytes out;
        for (const House& h : m_world.houses()) {
            const std::uint8_t n = number(h.island);
            core::Bytes r = start(raw, key3(n, h.x, h.y), kHouseSize);
            std::uint32_t d0 = get32(r, 0);
            std::uint32_t d1 = get32(r, 4);
            std::uint32_t d2 = get32(r, 8);
            d0 = (d0 & 0xF8000000U) | n | std::uint32_t{h.x & 0xFFU} << 8 | std::uint32_t{h.y & 0xFFU} << 16 |
                 std::uint32_t{h.epoch & 7U} << 24;
            d1 = (d1 & 0xFFF000F8U) | (h.queue_epoch & 7U) | std::uint32_t{h.resident_class} << 8 |
                 std::uint32_t{h.market_distance & 0xFU} << 16;
            d2 = (d2 & 0x00100000U) | std::min<std::uint32_t>(h.inhabitants64, 0xFFFF) |
                 std::uint32_t{h.timer_group & 0xFU} << 16 | (h.services_complete ? 1U << 21 : 0U) |
                 (h.market ? 1U << 22 : 0U);
            for (std::size_t k = 0; k < kServiceBits.size(); ++k) {
                if ((h.services >> (k + 2) & 1U) != 0) {
                    d2 |= 1U << kServiceBits[k];
                }
            }
            put32(r, 0, d0);
            put32(r, 4, d1);
            put32(r, 8, d2);
            out.insert(out.end(), r.begin(), r.end());
        }
        add("SIEDLER", std::move(out));
    }

    /// What a tile shows after loading: the ground where the building on top is not saved.
    [[nodiscard]] const MapCell& loaded_cell(const IslandMap& map, std::uint32_t x, std::uint32_t y) const {
        const SubTile sub = map.sub_tile(m_buildings, x, y);
        if (sub.building != nullptr && map.contains(sub.origin_x, sub.origin_y) &&
            same_as_ground(map, static_cast<std::uint32_t>(sub.origin_x), static_cast<std::uint32_t>(sub.origin_y))) {
            return map.ground(x, y);
        }
        return map.top(x, y);
    }

    void growth() {
        const auto raw = m_source.by_key("ROHWACHS2", kGrowthSize, [](auto r) { return key3(r[0], r[1], r[2]); });
        // Entries for tiles that grow after loading without one (the ground's growing grass where
        // the ripe grass on top is not saved): the entries the loader would add (S-020), so loading
        // the file again gives the same list.
        std::vector<GrowthEntry> entries = m_world.growth().entries;
        std::unordered_set<std::uint64_t> present;
        for (const GrowthEntry& e : entries) {
            present.insert(key3(e.island, e.x, e.y));
        }
        for (const std::uint16_t i : island_order()) {
            const IslandMap& map = m_world.islands()[i].map;
            for (std::uint16_t y = 0; y < map.height(); ++y) {
                for (std::uint16_t x = 0; x < map.width(); ++x) {
                    if (present.contains(key3(i, x, y))) {
                        continue;
                    }
                    if (const auto e =
                            missing_growth_entry(m_world.growth(), m_buildings, i, x, y, loaded_cell(map, x, y))) {
                        entries.push_back(*e);
                    }
                }
            }
        }
        core::Bytes out;
        for (const GrowthEntry& e : entries) {
            const std::uint8_t n = number(e.island);
            core::Bytes r = start(raw, key3(n, e.x, e.y), kGrowthSize);
            r[0] = n;
            r[1] = static_cast<std::uint8_t>(e.x);
            r[2] = static_cast<std::uint8_t>(e.y);
            r[3] = e.growth_class;
            r[4] = static_cast<std::uint8_t>((r[4] & ~7U) | (e.seen & 7U));
            r[5] = e.stage;
            out.insert(out.end(), r.begin(), r.end());
        }
        add("ROHWACHS2", std::move(out));
    }

    // ---- Cities ----------------------------------------------------------------------------------

    /// Id (- 20000) of the first producer of a ware (the original's ware table, 0x00609160).
    [[nodiscard]] std::uint16_t producer_id(std::uint8_t ware) const {
        // The first building in record order that produces the ware (records may have gaps).
        const assets::BuildingType* first = nullptr;
        for (const assets::BuildingType& b : m_buildings.types()) {
            if (b.kind != 0 && b.prod_kind != 0 && b.ware == ware && (first == nullptr || b.record < first->record)) {
                first = &b;
            }
        }
        return first != nullptr ? static_cast<std::uint16_t>(first->id - kIdOffset) : std::uint16_t{0};
    }

    void cities() {
        const auto raw_city = m_source.by_key("STADT4", kCitySize, [](auto r) { return key3(r[0], r[1], 0); });
        const auto raw_store =
            m_source.by_key("KONTOR2", kWarehouseSize, [](auto r) { return key3(r[0], (r[3] & 0xFU), 0); });
        const auto raw_market =
            m_source.by_key("MARKT2", kConsumptionSize, [](auto r) { return key3(r[0], (r[1] & 0xFU), 0); });
        core::Bytes cities;
        core::Bytes stores;
        core::Bytes markets;
        // The original keeps its cities in a table of 75; Open1602 writes them in its own order.
        for (const City& c : m_world.cities()) {
            if (!c.warehouse_x || !c.warehouse_y) {
                continue;  // the original saves only cities with a Kontor
            }
            const std::uint8_t n = number(c.island);
            const std::uint64_t key = key3(n, c.index, 0);
            core::Bytes r = start(raw_city, key, kCitySize);
            r[0] = n;
            r[1] = c.index;
            r[2] = owner_byte(c.owner);
            r[4] = static_cast<std::uint8_t>((r[4] & ~1U) | (c.material_lock ? 1U : 0U));
            r[5] = c.population_message;
            put32(r, 0x08, static_cast<std::uint32_t>(std::clamp<std::int64_t>(c.purchases, 0, 0xFFFFFFFFLL)));
            put32(r, 0x0C, static_cast<std::uint32_t>(std::clamp<std::int64_t>(c.sales, 0, 0xFFFFFFFFLL)));
            // The loader turns a 0 in the satisfaction, the tax rate and the food supply into 128
            // (0x00484C6C): a stored 0 that loaded as 128 stays 0.
            const auto keep = [](std::uint8_t& stored, std::uint8_t v) {
                if (!(stored == 0 && v == 128)) {
                    stored = v;
                }
            };
            for (std::size_t k = 0; k < 5; ++k) {
                put32(r, 0x5C + 4 * k, k < c.inhabitants.size() ? c.inhabitants[k] : 0);
                keep(r[0x78 + k], k < c.satisfaction.size() ? c.satisfaction[k] : std::uint8_t{128});
                keep(r[0x7F + k], k < c.tax_rate.size() ? c.tax_rate[k] : std::uint8_t{128});
            }
            keep(r[0x86], c.food_ratio);
            for (std::size_t k = 0; k < assets::kOriginalPlayerSlots && k < c.goodwill.size(); ++k) {
                put32(r, 0x18 + 4 * k, c.goodwill[k]);
            }
            const auto stored = std::span<const std::uint8_t>(r).subspan(0x87);
            const std::string old(stored.begin(), std::find(stored.begin(), stored.end(), std::uint8_t{0}));
            if (old != c.name) {
                std::fill(r.begin() + 0x87, r.end(), std::uint8_t{0});
                const std::string name = c.name.substr(0, kCityNameMax);
                std::copy(name.begin(), name.end(), r.begin() + 0x87);
            }
            cities.insert(cities.end(), r.begin(), r.end());

            core::Bytes s = start(raw_store, key, kWarehouseSize);
            put32(s, 0,
                  (get32(s, 0) & 0xF0000000U) | n | std::uint32_t{*c.warehouse_x & 0xFFU} << 8 |
                      std::uint32_t{*c.warehouse_y & 0xFFU} << 16 | std::uint32_t{c.index & 0xFU} << 24);
            for (std::uint8_t w = ware::kFirstTrade; w <= ware::kLastTrade; ++w) {
                const std::size_t o = 4 + 20 * std::size_t{w};
                const std::uint32_t stock = std::min<std::uint32_t>(c.stock(w), 0xFFFF);
                put16(s, o + 0x0C, stock);
                if (const City::Trade* t = c.trade_of(w)) {
                    // Prices, sell and buy flags (+0), what the Kontor keeps (+8) and buys up to (+0x0A).
                    put32(s, o,
                          (get32(s, o) & 0xFFC00000U) | (t->sell_price & 0x3FFU) |
                              std::uint32_t{t->buy_price & 0x3FFU} << 10 | (t->sell ? 1U << 20 : 0U) |
                              (t->buy ? 1U << 21 : 0U));
                    put16(s, o + 8, std::min<std::uint32_t>(t->keep, 0xFFFF));
                    put16(s, o + 0x0A, std::min<std::uint32_t>(t->limit, 0xFFFF));
                }
                if (get16(s, o + 0x10) == 0 && stock != 0) {
                    put16(s, o + 0x10, producer_id(w));
                }
            }
            stores.insert(stores.end(), s.begin(), s.end());

            core::Bytes m = start(raw_market, key, kConsumptionSize);
            put32(m, 0, (get32(m, 0) & 0xFFFFF000U) | n | std::uint32_t{c.index & 0xFU} << 8);
            for (std::uint8_t w = 14; w <= 24; ++w) {
                const std::size_t o = 4 + 16 * std::size_t{w - 14U};
                if (w >= c.supply.size()) {
                    continue;
                }
                const City::Supply& sup = c.supply[w];
                put32(m, o, clamp32(sup.demand));
                put32(m, o + 4, clamp32(sup.supplied));
                m[o + 0x0E] = sup.ratio;
                if (get16(m, o + 0x0C) == 0) {
                    put16(m, o + 0x0C, producer_id(w));
                }
            }
            markets.insert(markets.end(), m.begin(), m.end());
        }
        add("STADT4", std::move(cities));
        add("KONTOR2", std::move(stores));
        add("MARKT2", std::move(markets));
    }

    // ---- Players, ships, soldiers ------------------------------------------------------------------

    /// A faction of a content package goes out as the original's that it acts as.
    [[nodiscard]] assets::PlayerKind original_kind(const Player& p) const noexcept {
        if (p.status == PlayerStatus::Absent) {
            return assets::PlayerKind::Absent;
        }
        switch (m_world.behaviour(p)) {
            case FactionBehaviour::Settlers:
                break;
            case FactionBehaviour::Trader:
                return assets::PlayerKind::Trader;
            case FactionBehaviour::Pirates:
                return assets::PlayerKind::Pirate;
            case FactionBehaviour::Natives:
                return assets::PlayerKind::Natives;
        }
        return p.role == PlayerRole::Computer ? assets::PlayerKind::Computer : assets::PlayerKind::Human;
    }

    void players() {
        const auto raw = m_source.records("PLAYER4", kPlayerSize);
        core::Bytes out;
        for (std::size_t p = 0; p < assets::kOriginalPlayerSlots; ++p) {
            core::Bytes r(kPlayerSize, 0);
            if (p < raw.size()) {
                std::copy(raw[p].begin(), raw[p].end(), r.begin());
            }
            r[5] = static_cast<std::uint8_t>(p);
            const Player* player = m_world.player(static_cast<PlayerId>(p));
            if (player != nullptr && p >= raw.size()) {
                // No record of a source file (a game loaded from Open1602's own savegame or made
                // new): controller, colour and name from the player.
                // As a new game sets them (0x00477520): in the session, nobody blamed, no mission.
                r[4] = static_cast<std::uint8_t>(original_kind(*player));
                r[6] = 1;
                r[0x0C] = 0xFF;
                std::fill_n(r.begin() + 0x3C, 4, std::uint8_t{0xFF});
                r[7] = static_cast<std::uint8_t>(std::min<BannerId>(player->banner, assets::kPlayerColourCount - 1));
                const std::size_t n = std::min<std::size_t>(player->name.size(), 0x3F);
                std::copy_n(player->name.begin(), n, r.begin() + 0x3C0);
            }
            if (player != nullptr && p < raw.size() && player->status == PlayerStatus::Playing &&
                (player->role == PlayerRole::Human || player->role == PlayerRole::Computer)) {
                // A seat that changed hands since the source file (a multiplayer game saved after
                // the host left): the controller and the name as they are now.
                const auto kind = static_cast<assets::PlayerKind>(r[4]);
                if ((kind == assets::PlayerKind::Human || kind == assets::PlayerKind::Computer) &&
                    kind != original_kind(*player)) {
                    r[4] = static_cast<std::uint8_t>(original_kind(*player));
                }
                const auto name = r.begin() + 0x3C0;
                const auto stored = static_cast<std::size_t>(std::find(name, name + 0x40, 0) - name);
                const std::string_view now = std::string_view(player->name).substr(0, 0x3F);
                if (!std::equal(name, name + static_cast<std::ptrdiff_t>(stored), now.begin(), now.end(),
                                [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); })) {
                    std::fill_n(name, 0x40, std::uint8_t{0});
                    std::copy(now.begin(), now.end(), name);
                }
            }
            if (player != nullptr) {
                put32(r, 0, static_cast<std::uint32_t>(player->money));
                put32(r, 0x34, player->infra_mask);
                r[0x0E] = player->castles_built;
                r[0x0F] = player->cathedrals_built;
                put16(r, 0x10, player->arches_allowed);
                put16(r, 0x12, player->arches_built);
                put16(r, 0x38, player->monuments_allowed);
                put16(r, 0x3A, player->monuments_built);
                // The original's counters are 16 bit: saturated, not wrapped.
                put16(r, 0x14, std::min<std::uint32_t>(player->soldiers_killed, 0xFFFF));
                put16(r, 0x16, std::min<std::uint32_t>(player->soldiers_lost, 0xFFFF));
                put16(r, 0x18, std::min<std::uint32_t>(player->ships_lost, 0xFFFF));
                put16(r, 0x1A, std::min<std::uint32_t>(player->ships_sunk, 0xFFFF));
                if (player->role == PlayerRole::Human || player->role == PlayerRole::Computer) {
                    // The others keep what the file had: their slot is never read.
                    r[0x0C] = player->mission < kOriginalMissionSlots ? player->mission : std::uint8_t{0xFF};
                    put16(r, 0x0A, player->bankrupt_checks);
                    put32(r, 0x1C, player->scenario.flags);
                    put16(r, 0x20, player->scenario.rival_inhabitants);
                    put16(r, 0x22, player->scenario.own_inhabitants);
                    r[0x24] = player->scenario.own_development;
                    r[0x25] = player->scenario.rival_development;
                }
                // An absent slot keeps its controller: the original retires it again when loading
                // (0x0048C0F0), and so a file written back unchanged stays the same.
                if (player->status == PlayerStatus::Defeated) {
                    r[4] = static_cast<std::uint8_t>(assets::PlayerKind::Defeated);
                }
                const Diplomacy& d = m_world.diplomacy();
                const auto a = static_cast<PlayerId>(p);
                r[0x08] = owner_byte(d.blamed(a));
                for (std::size_t b = 0; b < assets::kOriginalPlayerSlots; ++b) {
                    const Relation& rel = d.relation(a, static_cast<PlayerId>(b));
                    if (b >= m_world.players().size()) {
                        continue;
                    }
                    r[0xC0 + 8 * b] = static_cast<std::uint8_t>(rel.peace);
                    put32(r, 0xC4 + 8 * b, clamp32(rel.war_time_ms / 100));
                    r[0x140 + 8 * b] = static_cast<std::uint8_t>(rel.trade);
                    put16(r, 0x40 + 2 * b, std::min<std::uint32_t>(rel.tribute, 0xFFFF));
                    put16(r, 0x60 + 2 * b, std::min<std::uint32_t>(rel.trade_mood, 0xFFFF));
                    put32(r, 0x80 + 4 * b, rel.damage);
                }
                // The first 7 of the 32 events, as the original saves them (S-085).
                const auto& events = d.events(a);
                for (std::size_t i = 0; i < Diplomacy::kSavedEvents; ++i) {
                    const DiplomacyEvent& e = events[i];
                    const std::size_t o = 0x1C0 + 8 * i;
                    if (e.type == DiplomacyEventType::None) {
                        put32(r, o, 0);
                        put32(r, o + 4, 0);
                        continue;
                    }
                    r[o] = static_cast<std::uint8_t>(e.type);
                    r[o + 1] = owner_byte(e.partner);
                    put32(r, o + 4, clamp32(e.time_ms / 100));
                }
            }
            out.insert(out.end(), r.begin(), r.end());
        }
        add("PLAYER4", std::move(out));
    }

    /// AUFTRAG4 (0x00478380): the first four missions; a slot is written if it has goals, slot 0
    /// also if it has a text (the endless games).
    void missions() {
        core::Bytes out;
        const auto& missions = m_world.missions();
        for (std::size_t i = 0; i < missions.size() && i < kOriginalMissionSlots; ++i) {
            if (missions[i].goals != 0 || (i == 0 && !missions[i].text.empty())) {
                const auto r = assets::encode_mission(mission_to_record(missions[i], static_cast<std::uint32_t>(i)));
                out.insert(out.end(), r.begin(), r.end());
            }
        }
        add("AUFTRAG4", std::move(out));
    }

    /// HANDLER: the free trader's goods, one record per trader ship (0x004600C0).
    void traders() {
        constexpr std::size_t kSize = 0x25C;
        constexpr std::size_t kGoods = 50;
        const auto raw = m_source.records("HANDLER", kSize);
        core::Bytes out;
        for (const Ship& s : m_world.ships()) {
            const auto record = m_trader_records.find(s.id);
            if (record == m_trader_records.end()) {
                continue;
            }
            core::Bytes r(kSize, 0);
            bool from_source = false;
            for (const auto& old : raw) {
                if (old[0] != 0 && get16(old, 1) == s.source_slot) {
                    std::copy(old.begin(), old.end(), r.begin());  // keeps the original's ware ids
                    from_source = true;
                    break;
                }
            }
            r[0] = 1;
            put16(r, 1, m_ship_slots.at(s.id));
            r[3] = record->second;
            for (std::uint8_t w = 0; w < 25 && w < kGoods; ++w) {
                const std::size_t o = 4 + 12 * std::size_t{w};
                const TraderGood g = w < s.trader_goods.size() ? s.trader_goods[w] : TraderGood{};
                put32(r, o, (g.price & 0x3FFU) | std::uint32_t{g.age & 0x1FU} << 10);
                put32(r, o + 4, g.amount);
                if (!from_source) {
                    put16(r, o + 8, producer_id(w));
                }
            }
            out.insert(out.end(), r.begin(), r.end());
        }
        add("HANDLER", std::move(out));
    }

    void ships() {
        const auto raw = m_source.records("SHIP4", kShipSize);
        std::unordered_map<std::uint16_t, std::size_t> by_slot;
        for (std::size_t k = 0; k < raw.size(); ++k) {
            by_slot.emplace(static_cast<std::uint16_t>(get16(raw[k], 0x46)), k);
        }
        core::Bytes out;
        for (const Ship& s : m_world.ships()) {
            core::Bytes r(kShipSize, 0);
            const std::uint16_t slot = m_ship_slots.at(s.id);
            const auto source = s.source_slot != 0xFFFF ? by_slot.find(s.source_slot) : by_slot.end();
            if (source != by_slot.end()) {
                std::copy(raw[source->second].begin(), raw[source->second].end(), r.begin());
            }
            put16(r, 0x46, slot);
            const auto stored = std::span<const std::uint8_t>(r).subspan(0, 0x1C);
            const std::string old(stored.begin(), std::find(stored.begin(), stored.end(), std::uint8_t{0}));
            if (old != s.name) {
                std::fill_n(r.begin(), 0x1C, std::uint8_t{0});
                const std::string name = s.name.substr(0, kShipNameMax);
                std::copy(name.begin(), name.end(), r.begin());
            }
            put16(r, 0x1C, s.x);
            put16(r, 0x1E, s.y);
            put_ref(r, 0x2C, s.order);
            put_ref(r, 0x30, s.target);
            put_ref(r, 0x34, s.previous);
            put16(r, 0x3C, std::min<std::uint32_t>(s.health, 0xFFFF));
            r[0x42] = s.cannons;
            // Flags: bit 1 route on, bit 3 one-way, bit 2 for sale, bit 4 repair; bit 0 is the UI's.
            r[0x43] = static_cast<std::uint8_t>((r[0x43] & 0xE1U) | (s.route.active ? 0x02U : 0U) |
                                                (s.route.one_way ? 0x08U : 0U) | (s.for_sale ? 0x04U : 0U) |
                                                (s.repairing ? 0x10U : 0U));
            if (!(get16(r, 0x44) == 0 && s.sale_factor == 128)) {  // the loader reads 0 as 100 %
                put16(r, 0x44, s.sale_factor);
            }
            put16(r, 0x48, s.figure);
            r[0x4A] = ship_kind(s);
            r[0x4B] = owner_byte(s.owner);
            const auto record = m_trader_records.find(s.id);
            r[0x4C] = record != m_trader_records.end() ? record->second : std::uint8_t{0};
            const bool on_map = m_world.contains(s.x, s.y);
            const std::uint16_t island = on_map ? m_world.cell(s.x, s.y).island : World::kSea;
            r[0x4D] = island == World::kSea ? std::uint8_t{0xFF} : number(island);
            // SHIP4 has the original's four stops of three wares; the others travel in the chunk O1602
            // only.
            r[0x4F] = s.route.current < TradeRoute::kOriginalStops ? s.route.current : std::uint8_t{0};
            r[0x50] = static_cast<std::uint8_t>((r[0x50] & ~7U) | (s.direction & 7U));
            for (std::size_t g = 0; g < TradeRoute::kOriginalStops; ++g) {
                const std::size_t o = 0x52 + 0x24 * g;
                const RouteStop& stop = s.route.stops[g];
                put_ref(r, o, stop.target);
                for (std::size_t e = 0; e < RouteStop::kOriginalEntries; ++e) {
                    const std::size_t eo = o + 4 + 8 * e;
                    const RouteEntry& entry = stop.entries[e];
                    const assets::BuildingType* stored_type = m_buildings.by_id(get16(r, eo) + kIdOffset);
                    const std::uint8_t stored_ware =
                        get16(r, eo) != 0 && stored_type != nullptr ? stored_type->ware : 0;
                    if (stored_ware != entry.ware) {
                        put16(r, eo, entry.ware != 0 ? producer_id(entry.ware) : 0U);
                    }
                    put16(r, eo + 2, std::min<std::uint32_t>(entry.amount, 0xFFFF));
                    r[eo + 4] = static_cast<std::uint8_t>((r[eo + 4] & ~1U) | (entry.unload ? 1U : 0U));
                }
            }
            // Cargo: eight holds of the producer id, the amount (whole tons for the original) and the
            // average price paid.
            for (std::size_t c = 0; c < 8; ++c) {
                const std::size_t o = 0x174 + 8 * c;
                if (c < s.cargo.size() && s.cargo[c].ware != 0) {
                    const std::uint16_t id = producer_id(s.cargo[c].ware);
                    if (get16(r, o) == 0 || m_buildings.by_id(get16(r, o) + kIdOffset) == nullptr ||
                        m_buildings.by_id(get16(r, o) + kIdOffset)->ware != s.cargo[c].ware) {
                        put16(r, o, id);
                    }
                    put16(r, o + 2, (get16(r, o + 2) & 0xC000U) | std::min<std::uint32_t>(s.cargo[c].amount, 0x3FFF));
                    put32(r, o + 4, (get32(r, o + 4) & ~0x3FFU) | (s.cargo[c].price & 0x3FFU));
                } else if (get16(r, o) != 0) {
                    // An emptied hold: no ware, no amount, no price (else the price of the record
                    // the game was loaded from would stay).
                    put16(r, o, 0);
                    put16(r, o + 2, 0);
                    put32(r, o + 4, get32(r, o + 4) & ~0x3FFU);
                }
            }
            out.insert(out.end(), r.begin(), r.end());
        }
        add("SHIP4", std::move(out));
    }

    void soldiers() {
        const auto raw = m_source.records("SOLDAT3", kSoldierSize);
        std::unordered_map<std::uint16_t, std::size_t> by_slot;
        for (std::size_t k = 0; k < raw.size(); ++k) {
            by_slot.emplace(static_cast<std::uint16_t>(get16(raw[k], 8)), k);
        }
        core::Bytes out;
        for (const Soldier& s : m_world.soldiers()) {
            if (s.dying()) {
                continue;  // the original does not save dying units
            }
            core::Bytes r(kSoldierSize, 0);
            const auto source = s.source_slot != 0xFFFF ? by_slot.find(s.source_slot) : by_slot.end();
            if (source != by_slot.end()) {
                std::copy(raw[source->second].begin(), raw[source->second].end(), r.begin());
            }
            const std::uint16_t slot = m_soldier_slots.at(s.id);
            put16(r, 0, static_cast<std::uint32_t>(std::max(0, s.cell_x())));
            put16(r, 2, static_cast<std::uint32_t>(std::max(0, s.cell_y())));
            put16(r, 4, std::min<std::uint32_t>(s.health, 0xFFFF));
            put16(r, 6, unit_figure(s.figure, s.owner, get16(r, 6)));
            put16(r, 8, slot);
            put_ref(r, 0x0A, s.home);
            put32(r, 0x0E, 0);
            put_ref(r, 0x12, s.target);
            r[0x16] = 4;
            r[0x17] = s.island < m_world.islands().size() ? number(s.island) : std::uint8_t{0xFF};
            r[0x18] = owner_byte(s.owner);
            r[0x19] = s.anim;
            r[0x1A] = s.radius;
            r[0x1B] = static_cast<std::uint8_t>((r[0x1B] & ~7U) | (s.direction & 7U));
            r[0x1C] = s.patrol.index;
            r[0x1D] =
                static_cast<std::uint8_t>((r[0x1D] & ~3U) | (s.patrol.active ? 1U : 0U) | (s.patrol.one_way ? 2U : 0U));
            put_ref(r, 0x1E, s.patrol.points[0]);
            put_ref(r, 0x22, s.patrol.points[1]);
            out.insert(out.end(), r.begin(), r.end());
        }
        add("SOLDAT3", std::move(out));
    }

    void towers() {
        const auto raw = m_source.records("TURM", kTowerSize);
        std::unordered_map<std::uint16_t, std::size_t> by_slot;
        for (std::size_t k = 0; k < raw.size(); ++k) {
            by_slot.emplace(static_cast<std::uint16_t>(get16(raw[k], 8)), k);
        }
        core::Bytes out;
        for (std::size_t i = 0; i < m_world.towers().size(); ++i) {
            const Tower& t = m_world.towers()[i];
            if (t.island >= m_world.islands().size()) {
                continue;
            }
            core::Bytes r(kTowerSize, 0);
            const auto source = t.source_slot != 0xFFFF ? by_slot.find(t.source_slot) : by_slot.end();
            if (source != by_slot.end()) {
                std::copy(raw[source->second].begin(), raw[source->second].end(), r.begin());
            }
            const WorldIsland& island = m_world.islands()[t.island];
            put16(r, 0, island.x + t.x);
            put16(r, 2, island.y + t.y);
            if (get16(r, 4) == 0) {
                put16(r, 4, 24 * 32);  // the tower figure's Maxenergy, never reduced
            }
            put16(r, 6, t.figure);
            put16(r, 8, m_tower_slots[i]);
            put_ref(r, 0x0A, Target::building(t.island, t.x, t.y));
            r[0x16] = 6;
            r[0x17] = number(t.island);
            r[0x18] = owner_byte(t.owner);
            r[0x1B] = static_cast<std::uint8_t>((r[0x1B] & ~7U) | (t.direction & 7U));
            out.insert(out.end(), r.begin(), r.end());
        }
        add("TURM", std::move(out));
    }

    // ---- Timers ----------------------------------------------------------------------------------

    void timers() {
        core::Bytes r(kTimersSize, 0);
        if (const auto c = m_source.chunk("TIMERS")) {
            std::copy_n(c->begin(), std::min(c->size(), r.size()), r.begin());
        }
        const GrowthState& g = m_world.growth();
        for (std::size_t k = 0; k < GrowthState::kClasses; ++k) {
            r[0x64 + k] = static_cast<std::uint8_t>((r[0x64 + k] & ~7U) | (g.counter[k] & 7U));
            // The loader takes the clock modulo the class period.
            const std::uint32_t period = growth_period_ms(static_cast<std::uint32_t>(k));
            if (get32(r, 0x218 + 4 * k) % period != g.time_ms[k]) {
                put32(r, 0x218 + 4 * k, g.time_ms[k]);
            }
        }
        const HouseClocks& h = m_world.house_clocks();
        for (std::size_t k = 0; k < HouseClocks::kGroups; ++k) {
            r[0x44 + k] = static_cast<std::uint8_t>((r[0x44 + k] & ~7U) | (h.epoch[k] & 7U));
            const std::uint32_t period = house_group_period_ms(static_cast<std::uint32_t>(k));
            if (get32(r, 0x198 + 4 * k) % period != h.time_ms[k]) {
                put32(r, 0x198 + 4 * k, h.time_ms[k]);
            }
        }
        put32(r, 0xA0, clamp32(m_world.time_ms() / 100));
        r[1] = static_cast<std::uint8_t>((r[1] & ~7U) | (m_world.island_phase() & 7U));
        put32(r, 0xB0, m_world.city_name_index());
        r[0xA5] = m_world.game_settings().tutorial ? 1 : 0;
        r[0xA6] = m_world.game_settings().difficulty;
        put32(r, 0xA8, m_world.game_settings().flags);
        // A stored 0 loads as 60 minutes.
        const std::uint32_t drought = clamp32(m_world.next_drought_ms() / 100);
        if (!(get32(r, 0xB4) == 0 && drought == 36'000)) {
            put32(r, 0xB4, drought);
        }
        const VolcanoState& v = m_world.volcanoes();
        const auto ref = [&](std::size_t o, const std::optional<CraterRef>& c) {
            if (c) {
                r[o] = 0x33;
                r[o + 1] = number(c->island);
                r[o + 2] = static_cast<std::uint8_t>(c->x);  // fits: check()
                r[o + 3] = static_cast<std::uint8_t>(c->y);
            } else if (r[o] != 0) {
                // Only the low byte tells (0x0048C0F0); a ref that was set is cleared whole so the
                // file does not depend on what ran before.
                std::fill_n(r.begin() + static_cast<std::ptrdiff_t>(o), 4, std::uint8_t{0});
            }
        };
        ref(0xC4, v.eruptions.empty() ? std::nullopt : std::optional<CraterRef>(v.eruptions.front().crater));
        if (v.eruptions.empty() && v.last) {
            // The original ends an eruption with "last = running; running.low = 0" (0x00447350):
            // the running ref keeps the crater of the last eruption.
            r[0xC4] = 0;
            r[0xC5] = number(v.last->island);
            r[0xC6] = static_cast<std::uint8_t>(v.last->x);
            r[0xC7] = static_cast<std::uint8_t>(v.last->y);
        }
        ref(0xC8, v.last);
        put32(r, 0xCC, clamp32(v.next_ms / 100));
        std::uint32_t craters = 0;
        for (const WorldIsland& island : m_world.islands()) {
            craters += static_cast<std::uint32_t>(island.craters.size());
        }
        put32(r, 0xD0, std::min<std::uint32_t>(craters, 20));
        add("TIMERS", std::move(r));
    }
};

}  // namespace

core::Result<std::vector<SavedChunk>> savegame_chunks(const World& world, const assets::BuildingTable& buildings) {
    const bool more_levels =
        world.infra_levels().size() > std::size_t{infra::kMaxLevel} + 1 ||
        std::any_of(world.players().begin(), world.players().end(), [](const Player& p) {
            return std::any_of(p.infra_more.begin(), p.infra_more.end(), [](std::uint32_t w) { return w != 0; });
        });
    if (!world.goods().own_goods().empty() || !world.own_buildings().empty() ||
        world.resident_classes().size() > assets::kResidentClasses || more_levels) {
        // The original's records have no room for them (docs/modding/goods.md, own-buildings.md,
        // classes.md, levels.md).
        return core::make_error(core::ErrorCode::Unsupported,
                                "a game with goods, buildings, classes or levels of "
                                "packages cannot be saved for the original");
    }
    return Writer(world, buildings).run();
}

core::Result<core::Bytes> write_savegame(const World& world, const assets::BuildingTable& buildings,
                                         const Simulation* simulation) {
    auto chunks = savegame_chunks(world, buildings);
    if (!chunks) {
        return chunks.error();
    }
    assets::ChunkWriter out;
    for (const SavedChunk& c : *chunks) {
        out.add(c.name, c.data);
    }
    out.add_checksum();
    if (world.width() != World::kOriginalWidth || world.height() != World::kOriginalHeight) {
        out.add(kMapChunk, encode_map_size(world.width(), world.height()));  // after WIFF: Open1602 only
    }
    const core::Bytes extension = encode_extension(world, buildings, simulation, out.bytes());
    out.add(kExtensionChunk, extension);
    return out.take();
}

}  // namespace o1602::sim
