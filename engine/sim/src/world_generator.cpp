// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/world_generator.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>

#include "o1602/assets/chunk_writer.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/sim/game_data.hpp"
#include "o1602/sim/island_map.hpp"
#include "o1602/sim/island_templates.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/volcanoes.hpp"

namespace o1602::sim {

namespace {

using assets::ScenarioPlan;

// Nominal size of a size class: only the height matters, for the climate of islands placed by
// position.
constexpr const auto& kNominalHeight = templates::kClassHeight;
using templates::kTemplateCount;
using templates::template_path;

// Fertility bit 0 is the first crop, the treasure the last ware with a bit.
constexpr std::uint8_t kFirstCrop = ware::kCornField;
constexpr std::uint8_t kLastCrop = ware::kTreasure;

// Every generated island grows grain, grass, trees and fish (0x0048C8F0).
constexpr std::uint32_t kBaseFertility = 0x1181;

// Amounts of a new deposit (0x0046BE60), 1/32 t; a treasure is money.
constexpr std::uint16_t kBigIron = 0x1E00;     // 240 t
constexpr std::uint16_t kSmallIron = 0x0A00;   // 80 t
constexpr std::uint16_t kGoldAmount = 0x0640;  // 50 t
constexpr std::uint16_t kTreasureMoney = 1000;

constexpr std::size_t kMaxMountains = 40;
constexpr std::uint32_t kMaxCraters = 20;  // in the whole world (0x00609128)

// Size of the island records in the state chunks, whose first byte is the island number.
struct StateChunk {
    const char* name;
    std::size_t size;
};
constexpr std::array<StateChunk, 8> kStateChunks = {{{"PRODLIST2", 0x18},
                                                     {"WERFT", 0x18},
                                                     {"MILITAR", 0x70},
                                                     {"SIEDLER", 0x10},
                                                     {"ROHWACHS2", 8},
                                                     {"STADT4", 0xA8},
                                                     {"KONTOR2", 0x3EC},
                                                     {"MARKT2", 0x104}}};

constexpr std::size_t kHeaderSize = 116;  // INSEL5

struct Mountain {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

/// Deposit slots as the island record keeps them (INSEL5 +0x1C, 2 x 8 bytes): bytes the
/// generator does not write keep what the template had.
struct DepositSlots {
    std::array<std::uint8_t, 16> bytes{};
    std::uint8_t count = 0;

    [[nodiscard]] std::uint8_t ware(std::size_t i) const noexcept { return bytes[8 * i]; }
    [[nodiscard]] std::uint8_t x(std::size_t i) const noexcept { return bytes[8 * i + 1]; }
    [[nodiscard]] std::uint8_t y(std::size_t i) const noexcept { return bytes[8 * i + 2]; }
    [[nodiscard]] bool at(std::uint8_t px, std::uint8_t py) const noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            if (x(i) == px && y(i) == py) {
                return true;
            }
        }
        return false;
    }
    void write(std::size_t i, std::uint8_t ware, std::uint8_t px, std::uint8_t py, std::uint16_t amount) noexcept {
        bytes[8 * i] = ware;
        bytes[8 * i + 1] = px;
        bytes[8 * i + 2] = py;
        bytes[8 * i + 6] = static_cast<std::uint8_t>(amount);
        bytes[8 * i + 7] = static_cast<std::uint8_t>(amount >> 8);
    }
    /// 0x0046BE60: the slot of the same mountain, else a new one while there is room.
    bool place(std::uint8_t ware, std::uint8_t px, std::uint8_t py, std::uint8_t kind, std::uint16_t amount) {
        std::size_t i = 0;
        while (i < count && !(x(i) == px && y(i) == py)) {
            ++i;
        }
        if (i == count) {
            if (count >= 2) {
                return false;
            }
            ++count;
        }
        write(i, ware, px, py, amount);
        bytes[8 * i + 4] = kind;
        return true;
    }
};

/// An island of the plan while it is made.
struct Slot {
    ScenarioPlan::Island plan;
    assets::Island base;                    ///< the template
    std::optional<assets::Island> village;  ///< the native template on top of it
    std::shared_ptr<assets::ChunkFile> village_file;
    std::optional<IslandMap> map;
    /// The header the original loads: the native template's if there is a village, else the
    /// template's. Deer phase, island tick phase, treasure flag and stale slot bytes survive.
    std::vector<std::uint8_t> loaded_header;
    std::uint32_t fertility = kBaseFertility;
    std::uint8_t crops = 0;
    DepositSlots deposits;
    bool treasure = false;
    std::array<std::uint32_t, 2> mountains{};  ///< big (Erzbergnr 0) and small ones among the first 40

    [[nodiscard]] bool has(std::uint8_t ware) const {
        for (std::size_t i = 0; i < deposits.count; ++i) {
            if (deposits.ware(i) == ware) {
                return true;
            }
        }
        return false;
    }
};

std::shared_ptr<assets::ChunkFile> open(const assets::GameDirectory& game, const std::string& path) {
    const auto resolved = game.resolve(path);
    if (!resolved) {
        return nullptr;
    }
    auto file = assets::ChunkFile::load(*resolved);
    if (!file) {
        return nullptr;
    }
    return std::make_shared<assets::ChunkFile>(std::move(file).value());
}

class Generator {
public:
    Generator(ScenarioPlan plan, const assets::GameDirectory& game, const assets::BuildingTable& buildings,
              PlanRandom& random, std::vector<std::string>* warnings, std::vector<StartArea> starts = {},
              std::uint8_t gold_islands = 0)
        : m_plan(std::move(plan)),
          m_game(game),
          m_buildings(buildings),
          m_random(random),
          m_warnings(warnings),
          m_starts(std::move(starts)),
          m_gold_islands(gold_islands) {}

    core::Result<GeneratedWorld> run(const assets::ChunkFile& scenario, bool guarantee);

private:
    void warn(std::string text) {
        if (m_warnings != nullptr) {
            m_warnings->push_back(std::move(text));
        }
    }
    void choose_templates();
    void choose_natives();
    bool load(Slot& slot);
    void hand_out_crops();
    void place_ores();
    void place_treasures();
    void guarantee();
    void reserve_home_iron();
    [[nodiscard]] bool village_fits(const ScenarioPlan::Island& e) const;
    void fair_starts();
    bool give_start_gold(const StartArea& start, const std::vector<Slot*>& near);
    void reserve_start_gold();
    void top_up_gold();
    [[nodiscard]] std::uint32_t gold_islands() const;
    [[nodiscard]] std::vector<Slot*> in_reach(const StartArea& start) const;

    [[nodiscard]] std::vector<Mountain> mountains(const Slot& slot, int type, bool free_only) const;
    [[nodiscard]] std::vector<Slot*> crop_candidates(std::uint8_t ware, std::uint8_t min_size, std::uint8_t max_size,
                                                     std::uint32_t max_crops) const;
    [[nodiscard]] std::vector<Slot*> ore_candidates(std::uint8_t ware, int type, std::uint32_t min_mountains,
                                                    std::uint32_t min_size, std::uint32_t max_size,
                                                    std::uint32_t max_crops, std::uint32_t max_same) const;
    [[nodiscard]] std::vector<Mountain> treasure_spots(const Slot& slot) const;
    void give_crop(Slot& slot, std::uint8_t ware) {
        slot.fertility |= 1U << (ware - kFirstCrop);
        ++slot.crops;
    }
    [[nodiscard]] std::uint8_t ware_of(std::uint32_t building) const {
        const assets::BuildingType* b = m_buildings.by_id(building);
        return b != nullptr ? b->ware : 0;
    }
    [[nodiscard]] std::vector<std::uint8_t> header(const Slot& slot, const std::vector<Crater>& craters,
                                                   const std::array<std::uint8_t, 8>& city_owners) const;
    [[nodiscard]] std::array<bool, 8> native_cities(const Slot& slot) const;
    [[nodiscard]] std::vector<std::uint8_t> village_tiles(const Slot& slot, const std::array<bool, 8>& natives) const;
    [[nodiscard]] std::vector<std::uint8_t> village_records(const Slot& slot, const StateChunk& chunk,
                                                            const std::array<bool, 8>& natives) const;

    ScenarioPlan m_plan;
    const assets::GameDirectory& m_game;
    const assets::BuildingTable& m_buildings;
    PlanRandom& m_random;
    std::vector<std::string>* m_warnings;
    std::vector<StartArea> m_starts;  ///< own worlds (world_plan.hpp): the players' start areas
    std::uint8_t m_gold_islands = 0;  ///< own worlds: exactly this many islands with gold (0: as planned)
    std::vector<Slot> m_slots;        ///< in the order of the plan
    std::vector<Slot*> m_by_number;   ///< in the order of the island table (island number)
    std::uint32_t m_max_crops = 2;
    std::uint32_t m_lost = 0;
    std::uint32_t m_guaranteed = 0;
};

void Generator::choose_templates() {
    // Loop A (0x0048C613): the climate of islands placed by position, then a template of their
    // climate and size that no other island of the plan has; candidates from the highest number.
    for (ScenarioPlan::Island& e : m_plan.islands) {
        e.size = std::min<std::uint8_t>(e.size, 4);
        if (e.climate == ScenarioPlan::kClimateByPosition) {
            const std::uint32_t centre = e.y + kNominalHeight[e.size] / 2;
            if (centre <= 350 / 3) {
                e.climate = climate::kNorth;  // the north is the top of the map
            } else if (centre >= 2 * 350 / 3) {
                e.climate = climate::kSouth;
            } else {
                e.climate = static_cast<std::uint8_t>(m_random.below(climate::kCount));
            }
        }
        e.climate &= 1U;
        if (e.template_number != ScenarioPlan::kRandomTemplate) {
            continue;
        }
        std::vector<bool> used(kTemplateCount[e.climate][e.size], false);
        for (const ScenarioPlan::Island& other : m_plan.islands) {
            if (other.climate == e.climate && other.size == e.size &&
                other.template_number != ScenarioPlan::kRandomTemplate && other.template_number < used.size()) {
                used[other.template_number] = true;
            }
        }
        std::vector<std::uint16_t> candidates;
        for (std::size_t t = used.size(); t-- > 0;) {
            if (!used[t]) {
                candidates.push_back(static_cast<std::uint16_t>(t));
            }
        }
        if (candidates.empty()) {
            // The original divides by zero here; Open1602 reuses a template.
            warn("no unused island template left for island " + std::to_string(e.number));
            e.template_number = static_cast<std::uint16_t>(m_random.below(kTemplateCount[e.climate][e.size]));
            continue;
        }
        e.template_number = candidates[m_random.below(static_cast<std::uint32_t>(candidates.size()))];
    }
}

bool Generator::village_fits(const ScenarioPlan::Island& e) const {
    // SuedNat/big15 is 30 x 30 on a 70 x 60 island (D-010); own worlds leave such villages out.
    const auto header_of = [&](bool natives) -> std::optional<std::pair<std::uint32_t, std::uint32_t>> {
        const auto file = open(m_game, template_path(e.climate, e.size, e.template_number, natives));
        if (!file) {
            return std::nullopt;
        }
        auto islands = assets::load_islands(*file);
        if (!islands || islands->empty()) {
            return std::nullopt;
        }
        return std::pair{std::uint32_t{islands->front().header.width}, std::uint32_t{islands->front().header.height}};
    };
    const auto base = header_of(false);
    const auto village = header_of(true);
    return base && village && *base == *village;
}

void Generator::choose_natives() {
    // Loop B: villages on islands whose template has a native version, north then south.
    for (std::uint8_t climate = 0; climate < climate::kCount; ++climate) {
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < m_plan.islands.size(); ++i) {
            const ScenarioPlan::Island& e = m_plan.islands[i];
            if (e.climate == climate && (e.flags & kPlanNoVillage) == 0 &&
                m_game.resolve(template_path(e.climate, e.size, e.template_number, true)).has_value() &&
                (m_starts.empty() || village_fits(e))) {
                candidates.push_back(i);
            }
        }
        for (std::uint32_t n = 0; n < m_plan.natives[climate] && !candidates.empty(); ++n) {
            const std::uint32_t k = m_random.below(static_cast<std::uint32_t>(candidates.size()));
            m_plan.islands[candidates[k]].flags |= 1U;
            candidates[k] = candidates.back();
            candidates.pop_back();
        }
    }
}

bool Generator::load(Slot& slot) {
    const ScenarioPlan::Island& e = slot.plan;
    const std::string base_path = template_path(e.climate, e.size, e.template_number, false);
    const auto base_file = open(m_game, base_path);
    if (!base_file) {
        warn("island template not found: " + base_path);
        return false;
    }
    auto base = assets::load_islands(*base_file);
    if (!base || base->empty()) {
        warn("island template invalid: " + base_path);
        return false;
    }
    slot.base = std::move(base->front());
    if ((e.flags & 1U) != 0) {
        const std::string path = template_path(e.climate, e.size, e.template_number, true);
        slot.village_file = open(m_game, path);
        std::optional<assets::Island> village;
        if (slot.village_file) {
            if (auto loaded = assets::load_islands(*slot.village_file); loaded && !loaded->empty()) {
                village = std::move(loaded->front());
            }
        }
        if (village) {
            slot.village = std::move(village);
        } else {
            warn("native island template not found or invalid: " + path);
            slot.village_file.reset();
        }
    }
    // The base terrain, with the village on top: what the mountain and treasure searches see.
    if (slot.village) {
        slot.map = IslandMap::from_island(*slot.village, m_buildings, m_warnings, &slot.base);
    } else {
        slot.map = IslandMap::from_island(slot.base, m_buildings, m_warnings, nullptr);
    }
    slot.loaded_header = slot.village ? slot.village->header.raw : slot.base.header.raw;
    if (slot.loaded_header.size() == kHeaderSize) {
        // Deposit slots keep the loaded bytes, the count starts at 0 (0x0048C9A0); the treasure
        // flag survives (29 native templates carry one, so they never get a treasure).
        std::copy_n(slot.loaded_header.begin() + 0x1C, 16, slot.deposits.bytes.begin());
        slot.treasure = slot.loaded_header[0x18] != 0;
    }
    for (const Mountain& m : mountains(slot, 2, false)) {
        const assets::BuildingType* b = m_buildings.by_sprite(slot.map->top(m.x, m.y).sprite);
        if (b != nullptr && b->ore_mountain < 2) {
            ++slot.mountains[b->ore_mountain];
        }
    }
    return true;
}

std::vector<Mountain> Generator::mountains(const Slot& slot, int type, bool free_only) const {
    // 0x0046C2F0: origins of ore mountains, rows from the top, each row from the right.
    std::vector<Mountain> out;
    const IslandMap& map = *slot.map;
    for (std::uint32_t y = 0; y < map.height() && out.size() < kMaxMountains; ++y) {
        for (std::uint32_t x = map.width(); x-- > 0 && out.size() < kMaxMountains;) {
            const MapCell& cell = map.top(x, y);
            const assets::BuildingType* b = cell.empty() ? nullptr : m_buildings.by_sprite(cell.sprite);
            if (b == nullptr || b->prod_kind != prod::kOreMountain || b->ware != ware::kOreMountain ||
                (type != 2 && b->ore_mountain != type)) {
                continue;
            }
            const SubTile sub = map.sub_tile(m_buildings, x, y);
            if (sub.origin_x != static_cast<std::int32_t>(x) || sub.origin_y != static_cast<std::int32_t>(y)) {
                continue;
            }
            const auto mx = static_cast<std::uint8_t>(x);
            const auto my = static_cast<std::uint8_t>(y);
            if (free_only && slot.deposits.at(mx, my)) {
                continue;
            }
            out.push_back(Mountain{mx, my});
        }
    }
    return out;
}

std::vector<Slot*> Generator::crop_candidates(std::uint8_t ware, std::uint8_t min_size, std::uint8_t max_size,
                                              std::uint32_t max_crops) const {
    // 0x0048D5C0: north islands take tobacco, sugar or wine (not both of the last two), south
    // islands spices, cotton or cocoa.
    std::vector<Slot*> out;
    for (Slot* s : m_by_number) {
        const std::uint32_t bit = 1U << (ware - kFirstCrop);
        if (s->plan.size < min_size || s->plan.size > max_size || s->crops >= max_crops || (s->fertility & bit) != 0) {
            continue;
        }
        const bool north = s->plan.climate == climate::kNorth;
        bool ok = false;
        switch (ware) {
            case ware::kTobaccoField:
                ok = north;
                break;
            case ware::kSpiceField:
            case ware::kCottonField:
            case ware::kCocoaField:
                ok = !north;
                break;
            case ware::kSugarField:
                ok = north && (s->fertility & (1U << (ware::kWineField - kFirstCrop))) == 0;
                break;
            case ware::kWineField:
                ok = north && (s->fertility & (1U << (ware::kSugarField - kFirstCrop))) == 0;
                break;
            default:
                break;
        }
        if (ok) {
            out.push_back(s);
        }
    }
    return out;
}

void Generator::hand_out_crops() {
    // 0x0048C9F1: big islands first, then middle ones, then everyone up to the limit, then one
    // little island per crop.
    struct Entry {
        std::uint8_t ware;
        std::uint32_t count;
    };
    std::vector<Entry> list;
    for (const ScenarioPlan::Resource& r : m_plan.crops) {
        list.push_back(Entry{r.building != 0 ? ware_of(r.building) : std::uint8_t{0}, r.count});
    }
    const auto give = [&](std::uint8_t ware, std::vector<Slot*> candidates) {
        Slot* s = candidates[m_random.below(static_cast<std::uint32_t>(candidates.size()))];
        give_crop(*s, ware);
    };
    struct SizeRange {
        std::uint8_t min;
        std::uint8_t max;
    };
    for (const auto& [min_size, max_size] : {SizeRange{3, 4}, SizeRange{1, 2}}) {
        for (Entry& e : list) {
            if (e.count == 0 || e.ware == 0) {
                continue;
            }
            if (auto c = crop_candidates(e.ware, min_size, max_size, 1); !c.empty()) {
                --e.count;
                give(e.ware, std::move(c));
            }
        }
    }
    struct Try {
        std::uint8_t min_size;
        std::uint8_t max_size;
        std::uint32_t max_crops;
    };
    const std::array<Try, 5> tries = {
        {{2, 4, m_max_crops}, {1, 4, m_max_crops}, {0, 4, m_max_crops}, {3, 4, 3}, {2, 4, 3}}};
    for (bool any = true; any;) {
        any = false;
        for (Entry& e : list) {
            if (e.count == 0 || e.ware == 0) {
                continue;
            }
            for (const Try& t : tries) {
                if (auto c = crop_candidates(e.ware, t.min_size, t.max_size, t.max_crops); !c.empty()) {
                    --e.count;
                    give(e.ware, std::move(c));
                    any = true;
                    break;
                }
            }
        }
    }
    for (std::uint8_t ware = ware::kTobaccoField; ware <= kLastCrop; ++ware) {
        if (auto c = crop_candidates(ware, 0, 0, 2); !c.empty()) {
            give(ware, std::move(c));
        }
    }
}

std::vector<Slot*> Generator::ore_candidates(std::uint8_t ware, int type, std::uint32_t min_mountains,
                                             std::uint32_t min_size, std::uint32_t max_size, std::uint32_t max_crops,
                                             std::uint32_t max_same) const {
    // 0x0048D6B0. The mountain test counts mountains, not free ones; and when "any mountain"
    // fails, the original compares the ware byte of the first deposit slot instead.
    std::vector<Slot*> out;
    for (Slot* s : m_by_number) {
        if (s->plan.size < min_size || s->plan.size > max_size || s->deposits.count >= 2 || s->crops >= max_crops) {
            continue;
        }
        bool ok = false;
        if (type == 2) {
            ok = s->mountains[0] + s->mountains[1] >= min_mountains || s->deposits.ware(0) >= min_mountains;
        } else {
            ok = s->mountains[static_cast<std::size_t>(type)] >= min_mountains;
        }
        if (!ok) {
            continue;
        }
        std::uint32_t same = 0;
        for (std::size_t i = 0; i < s->deposits.count; ++i) {
            same += ware == 0 || s->deposits.ware(i) == ware ? 1U : 0U;
        }
        if (same < max_same) {
            out.push_back(s);
        }
    }
    return out;
}

void Generator::place_ores() {
    // 0x0048CC9D: each unit is one attempt; a unit whose island has no free mountain is lost.
    struct Try {
        std::uint8_t ware;
        int type;
        std::uint32_t mountains;
        std::uint32_t min_size;
        std::uint32_t max_size;
        std::uint32_t max_crops;
        std::uint32_t max_same;
    };
    const std::uint32_t mc = m_max_crops;
    const std::uint32_t g1 = m_plan.gold_min_size;
    const std::uint32_t g2 = m_plan.gold_max_size;
    for (const ScenarioPlan::Resource& r : m_plan.ores) {
        if (r.building == 0) {
            continue;
        }
        const std::uint8_t ware = ware_of(r.building);
        std::vector<Try> tries;
        int search = 2;
        std::uint8_t kind = 1;
        std::uint16_t amount = 0;
        if (ware == ware::kOre && r.type == 1) {
            tries = {{2, 2, 3, 2, 4, mc, 1},  {2, 2, 2, 2, 4, mc, 1},  {2, 2, 2, 2, 4, 255, 1},
                     {2, 2, 1, 1, 4, 255, 1}, {2, 2, 3, 1, 4, 255, 2}, {2, 2, 2, 1, 4, 255, 2}};
            amount = kSmallIron;
        } else if (ware == ware::kOre && r.type == 0) {
            tries = {{0, 0, 3, 1, 4, mc, 1}, {0, 0, 2, 1, 4, mc, 1}, {0, 0, 2, 1, 4, 255, 1}, {0, 0, 1, 1, 4, 255, 1}};
            search = 0;
            kind = 0;
            amount = kBigIron;
        } else if (ware == ware::kGold) {
            tries = {{3, 2, 3, g1, g2, mc, 1},  {3, 2, 2, g1, g2, mc, 1}, {3, 2, 2, g1, g2, 255, 1},
                     {3, 2, 1, g1, g2, 255, 1}, {3, 2, 3, g1, 4, 255, 1}, {3, 2, 2, g1, 4, 255, 1},
                     {3, 2, 2, g1, 4, 255, 2},  {3, 2, 1, g1, 4, 255, 2}};
            if (m_gold_islands != 0) {
                tries.resize(6);  // a fixed number of gold islands: one vein each
            }
            amount = kGoldAmount;
        } else {
            continue;
        }
        for (std::uint32_t unit = 0; unit < r.count; ++unit) {
            std::vector<Slot*> candidates;
            for (const Try& t : tries) {
                candidates =
                    ore_candidates(t.ware, t.type, t.mountains, t.min_size, t.max_size, t.max_crops, t.max_same);
                if (!candidates.empty()) {
                    break;
                }
            }
            if (candidates.empty()) {
                ++m_lost;
                continue;
            }
            Slot& s = *candidates[m_random.below(static_cast<std::uint32_t>(candidates.size()))];
            const std::vector<Mountain> free = mountains(s, search, true);
            if (free.empty()) {
                ++m_lost;  // S-019: the island had mountains, but none free
                continue;
            }
            const Mountain& m = free[m_random.below(static_cast<std::uint32_t>(free.size()))];
            if (!s.deposits.place(ware, m.x, m.y, kind, amount)) {
                ++m_lost;
            }
        }
    }
}

std::vector<Mountain> Generator::treasure_spots(const Slot& slot) const {
    // 0x0048D320 with 0x0048D460: every second tile of the 7 tiles wide strips along the left and
    // right edge (row by row), then along the top and bottom edge (column by column): open
    // ground whose four neighbours are ground or forest. Hits can repeat; after the 41st, each
    // further row or column adds at most one.
    constexpr std::int32_t kStep = 2;
    constexpr std::int32_t kStrip = 7;
    constexpr std::int32_t kForest = 10;
    constexpr std::int32_t kGround = 11;
    const IslandMap& map = *slot.map;
    const auto w = static_cast<std::int32_t>(map.width());
    const auto h = static_cast<std::int32_t>(map.height());
    const auto kind_at = [&](std::int32_t x, std::int32_t y) -> std::int32_t {
        const MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        const assets::BuildingType* b = c.empty() ? nullptr : m_buildings.by_sprite(c.sprite);
        return b != nullptr ? b->kind : -1;
    };
    const auto ok = [&](std::int32_t x, std::int32_t y) {
        if (x < 1 || x + 1 >= w || y < 1 || y + 1 >= h || kind_at(x, y) != kGround) {
            return false;
        }
        for (const auto& [nx, ny] : {std::pair{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}}) {
            const std::int32_t k = kind_at(nx, ny);
            if (k != kForest && k != kGround) {
                return false;
            }
        }
        return true;
    };
    // The original stores the left column (top row) for a hit in the right (bottom) strip, so
    // such treasures lie on tiles that were never tested, mostly water (S-067); Open1602 stores
    // the tile it tested. The draws stay the same.
    std::vector<Mountain> out;
    const auto hit = [&](std::int32_t x, std::int32_t y) {
        out.push_back(Mountain{static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)});
        return out.size() > kMaxMountains;
    };
    if (h > 1) {
        for (std::int32_t y = 1; y < h; y += kStep) {
            for (std::int32_t x = 1; x <= kStrip; x += kStep) {
                if (ok(x, y) && hit(x, y)) {
                    break;
                }
                if (ok(w - 1 - x, y) && hit(w - 1 - x, y)) {
                    break;
                }
            }
        }
    }
    if (w > 1) {
        for (std::int32_t x = 1; x < w; x += kStep) {
            for (std::int32_t y = 1; y <= kStrip; y += kStep) {
                if (ok(x, y) && hit(x, y)) {
                    break;
                }
                if (ok(x, h - 1 - y) && hit(x, h - 1 - y)) {
                    break;
                }
            }
        }
    }
    return out;
}

void Generator::place_treasures() {
    // 0x0048D116: treasures on middle islands without one, up to 4 tries for a spot. Own worlds
    // have fewer middle islands than the original's plans (every home island is big), so a
    // treasure that finds no place there may go to a big island that is nobody's home.
    constexpr std::uint32_t kTries = 4;
    const auto is_home = [&](const Slot& s) {
        return std::any_of(m_starts.begin(), m_starts.end(),
                           [&](const StartArea& a) { return a.home_island == s.plan.number; });
    };
    const auto candidates_of = [&](std::uint8_t min_size, std::uint8_t max_size) {
        std::vector<Slot*> out;
        for (Slot* s : m_by_number) {
            if (s->plan.size >= min_size && s->plan.size <= max_size && s->deposits.count < 2 && !s->treasure &&
                !(min_size >= 3 && is_home(*s))) {
                out.push_back(s);
            }
        }
        return out;
    };
    const auto try_place = [&](const std::vector<Slot*>& candidates) {
        for (std::uint32_t t = 0; t < kTries && !candidates.empty(); ++t) {
            Slot& s = *candidates[m_random.below(static_cast<std::uint32_t>(candidates.size()))];
            const std::vector<Mountain> spots = treasure_spots(s);
            if (spots.empty()) {
                continue;
            }
            const Mountain& spot = spots[m_random.below(static_cast<std::uint32_t>(spots.size()))];
            const std::size_t i = s.deposits.count++;
            s.deposits.write(i, ware::kTreasure, spot.x, spot.y, kTreasureMoney);
            s.treasure = true;
            return true;
        }
        return false;
    };
    for (const ScenarioPlan::Resource& r : m_plan.treasures) {
        if (r.building == 0 || ware_of(r.building) != ware::kJewellery) {
            continue;
        }
        for (std::uint32_t unit = 0; unit < r.count; ++unit) {
            bool placed = try_place(candidates_of(1, 2));
            if (!placed && !m_starts.empty()) {
                placed = try_place(candidates_of(3, 4));
            }
            m_lost += placed ? 0U : 1U;
        }
    }
}

void Generator::guarantee() {
    // S-019 (Rules::guarantee_resources): what the plan asks for and no island got.
    const auto has_crop = [&](std::uint8_t ware) {
        return std::any_of(m_slots.begin(), m_slots.end(),
                           [&](const Slot& s) { return (s.fertility & (1U << (ware - kFirstCrop))) != 0; });
    };
    for (const ScenarioPlan::Resource& r : m_plan.crops) {
        const std::uint8_t ware = r.building != 0 ? ware_of(r.building) : 0;
        if (r.count == 0 || ware < ware::kTobaccoField || ware > ware::kCocoaField || has_crop(ware)) {
            continue;
        }
        if (auto c = crop_candidates(ware, 0, 4, 255); !c.empty()) {
            give_crop(*c[m_random.below(static_cast<std::uint32_t>(c.size()))], ware);
            ++m_guaranteed;
        }
    }
    const auto has_deposit = [&](std::uint8_t ware) {
        return std::any_of(m_slots.begin(), m_slots.end(), [&](const Slot& s) {
            for (std::size_t i = 0; i < s.deposits.count; ++i) {
                if (s.deposits.ware(i) == ware) {
                    return true;
                }
            }
            return false;
        });
    };
    for (const auto& [ware, amount] : {std::pair{ware::kOre, kSmallIron}, {ware::kGold, kGoldAmount}}) {
        const bool asked = std::any_of(m_plan.ores.begin(), m_plan.ores.end(), [&](const ScenarioPlan::Resource& r) {
            return r.count > 0 && r.building != 0 && ware_of(r.building) == ware;
        });
        if (!asked || has_deposit(ware)) {
            continue;
        }
        // A free mountain on an island with a free slot; failing that, the second iron vein of an
        // island makes room (gold is rarer than iron).
        std::vector<std::pair<Slot*, Mountain>> spots;
        for (Slot* s : m_by_number) {
            if (s->deposits.count >= 2 || (ware == ware::kGold && s->plan.size == 0)) {
                continue;
            }
            for (const Mountain& m : mountains(*s, 2, true)) {
                spots.emplace_back(s, m);
            }
        }
        if (spots.empty() && ware == ware::kGold) {
            for (Slot* s : m_by_number) {
                if (s->deposits.count == 2 && s->deposits.ware(0) == ware::kOre && s->deposits.ware(1) == ware::kOre) {
                    spots.emplace_back(s, Mountain{s->deposits.x(1), s->deposits.y(1)});
                }
            }
        }
        if (spots.empty()) {
            continue;
        }
        const auto& [s, m] = spots[m_random.below(static_cast<std::uint32_t>(spots.size()))];
        if (s->deposits.place(ware, m.x, m.y, 1, amount)) {
            ++m_guaranteed;
        }
    }
}

std::vector<Slot*> Generator::in_reach(const StartArea& start) const {
    std::vector<Slot*> out;
    const std::int64_t r2 = std::int64_t{start.reach} * start.reach;
    for (Slot* s : m_by_number) {
        const std::int64_t dx = std::int64_t{s->plan.x} + s->map->width() / 2 - start.x;
        const std::int64_t dy = std::int64_t{s->plan.y} + s->map->height() / 2 - start.y;
        if (dx * dx + dy * dy <= r2 || s->plan.number == start.home_island) {
            out.push_back(s);
        }
    }
    return out;
}

void Generator::reserve_home_iron() {
    // Own worlds: every home island gets an iron vein before the main passes, a big one where it
    // has a big mountain; it counts against the plan's iron, so the world has as much as planned.
    for (const StartArea& start : m_starts) {
        Slot* home = nullptr;
        for (Slot* s : m_by_number) {
            home = s->plan.number == start.home_island ? s : home;
        }
        if (home == nullptr) {
            continue;
        }
        bool big = true;
        std::vector<Mountain> free = mountains(*home, 0, true);
        if (free.empty()) {
            big = false;
            free = mountains(*home, 2, true);
        }
        if (free.empty()) {
            warn("home island " + std::to_string(home->plan.number) + " has no ore mountain");
            continue;
        }
        const Mountain& m = free[m_random.below(static_cast<std::uint32_t>(free.size()))];
        if (!home->deposits.place(ware::kOre, m.x, m.y, big ? 0 : 1, big ? kBigIron : kSmallIron)) {
            continue;
        }
        for (ScenarioPlan::Resource& r : m_plan.ores) {
            if (r.building != 0 && ware_of(r.building) == ware::kOre && r.type == (big ? 0U : 1U) && r.count > 0) {
                --r.count;
                break;
            }
        }
    }
}

void Generator::fair_starts() {
    // Own worlds: every start has the six crops and a gold vein within reach. What the main
    // passes did not put there goes to an island near the start. With a fixed number of gold
    // islands the starts got their gold before the passes (reserve_start_gold).
    for (const StartArea& start : m_starts) {
        const std::vector<Slot*> near = in_reach(start);
        const auto reachable = [&](const Slot* s) { return std::find(near.begin(), near.end(), s) != near.end(); };
        for (std::uint8_t ware = ware::kTobaccoField; ware <= ware::kCocoaField; ++ware) {
            const std::uint32_t bit = 1U << (ware - kFirstCrop);
            if (std::any_of(near.begin(), near.end(), [&](const Slot* s) { return (s->fertility & bit) != 0; })) {
                continue;
            }
            std::vector<Slot*> candidates;
            for (const std::uint32_t max_crops : {m_max_crops, 3U, 255U}) {
                for (Slot* s : crop_candidates(ware, 0, 4, max_crops)) {
                    if (reachable(s)) {
                        candidates.push_back(s);
                    }
                }
                if (!candidates.empty()) {
                    break;
                }
            }
            if (candidates.empty() && (ware == ware::kSugarField || ware == ware::kWineField)) {
                // Sugar and wine never share an island: one of two islands with the other one
                // grows this one instead.
                const std::uint8_t other = ware == ware::kSugarField ? ware::kWineField : ware::kSugarField;
                const std::uint32_t other_bit = 1U << (other - kFirstCrop);
                std::vector<Slot*> with_other;
                for (Slot* s : near) {
                    if ((s->fertility & other_bit) != 0) {
                        with_other.push_back(s);
                    }
                }
                if (with_other.size() >= 2) {
                    Slot* s = with_other.back();
                    s->fertility = (s->fertility & ~other_bit) | bit;
                    ++m_guaranteed;
                    continue;
                }
                // Only one northern island near: in an own world it may grow both.
                Slot* best = nullptr;
                for (Slot* s : near) {
                    if (s->plan.climate == climate::kNorth && (s->fertility & bit) == 0 &&
                        (best == nullptr || s->crops < best->crops)) {
                        best = s;
                    }
                }
                if (best != nullptr) {
                    give_crop(*best, ware);
                    ++m_guaranteed;
                    continue;
                }
            }
            if (candidates.empty()) {
                warn("no island near start (" + std::to_string(start.x) + ", " + std::to_string(start.y) +
                     ") can grow crop " + std::to_string(ware));
                continue;
            }
            // The island with the fewest crops, the larger one on a tie.
            Slot* best = candidates.front();
            for (Slot* s : candidates) {
                if (s->crops < best->crops || (s->crops == best->crops && s->plan.size > best->plan.size)) {
                    best = s;
                }
            }
            give_crop(*best, ware);
            ++m_guaranteed;
        }
        if (m_gold_islands == 0 && give_start_gold(start, near)) {
            ++m_guaranteed;
        }
    }
}

bool Generator::give_start_gold(const StartArea& start, const std::vector<Slot*>& near) {
    if (std::any_of(near.begin(), near.end(), [](const Slot* s) { return s->has(ware::kGold); })) {
        return false;
    }
    std::vector<std::pair<Slot*, Mountain>> spots;
    for (const bool little : {false, true}) {
        for (Slot* s : near) {
            if (s->deposits.count >= 2 || (s->plan.size == 0) != little) {
                continue;
            }
            for (const Mountain& m : mountains(*s, 2, true)) {
                spots.emplace_back(s, m);
            }
        }
        if (!spots.empty()) {
            break;
        }
    }
    if (spots.empty()) {
        // The second iron vein of a near island makes room.
        for (Slot* s : near) {
            if (s->deposits.count == 2 && s->deposits.ware(0) == ware::kOre && s->deposits.ware(1) == ware::kOre) {
                spots.emplace_back(s, Mountain{s->deposits.x(1), s->deposits.y(1)});
            }
        }
    }
    if (spots.empty()) {
        // Nothing near: the nearest island with a free mountain and a free slot (with a fixed
        // number of gold islands, one without gold).
        std::int64_t best = std::numeric_limits<std::int64_t>::max();
        for (Slot* s : m_by_number) {
            if (m_gold_islands != 0 && s->has(ware::kGold)) {
                continue;
            }
            const std::int64_t dx = std::int64_t{s->plan.x} + s->map->width() / 2 - start.x;
            const std::int64_t dy = std::int64_t{s->plan.y} + s->map->height() / 2 - start.y;
            const std::vector<Mountain> free = mountains(*s, 2, true);
            if (s->deposits.count < 2 && !free.empty() && dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                spots.assign(1, {s, free.front()});
            }
        }
    }
    if (spots.empty()) {
        warn("no mountain for gold near start (" + std::to_string(start.x) + ", " + std::to_string(start.y) + ")");
        return false;
    }
    const auto& [s, m] = spots[m_random.below(static_cast<std::uint32_t>(spots.size()))];
    return s->deposits.place(ware::kGold, m.x, m.y, 1, kGoldAmount);
}

std::uint32_t Generator::gold_islands() const {
    return static_cast<std::uint32_t>(
        std::count_if(m_slots.begin(), m_slots.end(), [](const Slot& s) { return s.has(ware::kGold); }));
}

void Generator::reserve_start_gold() {
    // A fixed number of gold islands: the starts get theirs before the main passes, in the order of
    // the players, as long as the number lasts; the passes place the rest.
    ScenarioPlan::Resource* gold = nullptr;
    for (ScenarioPlan::Resource& r : m_plan.ores) {
        gold = r.building != 0 && ware_of(r.building) == ware::kGold ? &r : gold;
    }
    for (const StartArea& start : m_starts) {
        if (gold == nullptr || gold->count == 0) {
            return;
        }
        if (give_start_gold(start, in_reach(start))) {
            --gold->count;  // counts against the plan's gold, as the home iron does
        }
    }
}

void Generator::top_up_gold() {
    // A fixed number of gold islands: units the main passes lost go to islands without gold, the
    // larger classes first, as the passes would.
    while (gold_islands() < m_gold_islands) {
        std::vector<std::pair<Slot*, Mountain>> spots;
        for (const bool little : {false, true}) {
            for (Slot* s : m_by_number) {
                if (s->deposits.count >= 2 || s->has(ware::kGold) || (s->plan.size == 0) != little) {
                    continue;
                }
                for (const Mountain& m : mountains(*s, 2, true)) {
                    spots.emplace_back(s, m);
                }
            }
            if (!spots.empty()) {
                break;
            }
        }
        if (spots.empty()) {
            warn("only " + std::to_string(gold_islands()) + " of " + std::to_string(m_gold_islands) +
                 " gold islands: no free mountain left");
            return;
        }
        const auto& [s, m] = spots[m_random.below(static_cast<std::uint32_t>(spots.size()))];
        if (!s->deposits.place(ware::kGold, m.x, m.y, 1, kGoldAmount)) {
            return;
        }
        ++m_guaranteed;
    }
}

std::vector<std::uint8_t> Generator::header(const Slot& slot, const std::vector<Crater>& craters,
                                            const std::array<std::uint8_t, 8>& city_owners) const {
    // INSEL5 as the original saves a generated island (0x00468740 on a zeroed buffer): the plan's
    // number, place, template, size class and climate, the crops, deposits and craters, and a few
    // bytes of the loaded header.
    std::vector<std::uint8_t> h(kHeaderSize, 0);
    const std::vector<std::uint8_t>& loaded = slot.loaded_header;
    const bool full = loaded.size() == kHeaderSize;
    const auto u16 = [&](std::size_t o, std::uint32_t v) {
        h[o] = static_cast<std::uint8_t>(v);
        h[o + 1] = static_cast<std::uint8_t>(v >> 8);
    };
    h[0] = slot.plan.number;
    h[1] = static_cast<std::uint8_t>(slot.map->width());
    h[2] = static_cast<std::uint8_t>(slot.map->height());
    u16(4, slot.plan.x);
    u16(6, slot.plan.y);
    if (full) {
        std::copy_n(loaded.begin() + 0x08, 4, h.begin() + 0x08);  // deer phase, island tick phase
        h[0x66] = loaded[0x66];
    }
    std::copy(city_owners.begin(), city_owners.end(), h.begin() + 0x0C);
    h[0x17] = static_cast<std::uint8_t>(craters.size());
    h[0x18] = slot.treasure ? 1 : 0;
    h[0x19] = slot.crops;
    h[0x1A] = slot.deposits.count;
    std::copy_n(slot.deposits.bytes.begin(), 8 * slot.deposits.count, h.begin() + 0x1C);
    for (std::size_t i = 0; i < craters.size(); ++i) {
        const std::size_t o = 0x3C + 8 * i;
        if (full) {
            std::copy_n(loaded.begin() + static_cast<std::ptrdiff_t>(o), 8, h.begin() + static_cast<std::ptrdiff_t>(o));
        }
        // The original's header: islands of templates are at most 255 tiles wide.
        h[o + 1] = static_cast<std::uint8_t>(craters[i].x);
        h[o + 2] = static_cast<std::uint8_t>(craters[i].y);
    }
    h[0x5C] = static_cast<std::uint8_t>(slot.fertility);
    h[0x5D] = static_cast<std::uint8_t>(slot.fertility >> 8);
    h[0x5E] = static_cast<std::uint8_t>(slot.fertility >> 16);
    h[0x5F] = static_cast<std::uint8_t>(slot.fertility >> 24);
    u16(0x60, slot.plan.template_number);
    u16(0x62, slot.plan.size);
    h[0x64] = slot.plan.climate;
    return h;
}

std::array<bool, 8> Generator::native_cities(const Slot& slot) const {
    // 0x0046A220 gives a city to the natives when its centre building has Nativflg; the village
    // templates consist of native buildings, so any of the city's buildings tells.
    std::array<bool, 8> natives{};
    if (!slot.village || slot.village->layers.empty()) {
        return natives;
    }
    for (const assets::IslandTile& t : slot.village->layers.front()) {
        const assets::BuildingType* b = m_buildings.by_id(t.cod_id());
        if (b != nullptr && b->native && t.city() < natives.size()) {
            natives[t.city()] = true;
        }
    }
    return natives;
}

std::vector<std::uint8_t> Generator::village_tiles(const Slot& slot, const std::array<bool, 8>& natives) const {
    // The village's own records, on the island of the plan. Tiles of the natives' cities belong to
    // the natives: 97 older templates say player 0, which the original leaves in the tiles and
    // only corrects in the city; Open1602 keeps tile and city in agreement.
    constexpr std::uint8_t kNatives = 6;
    constexpr std::uint8_t kNoCity = 7;
    constexpr std::uint8_t kMinKontorRadius = 8;
    std::vector<std::uint8_t> out;
    if (!slot.village || slot.village->layers.empty()) {
        return out;
    }
    // At the start of the game the original recomputes the territory of every island
    // (0x0048C0F0 -> 0x00482120): village tiles outside the circle of the Kontor and the markets
    // (radius at least 8) lose their city. Some older village templates have one such tile.
    struct Anchor {
        Catchment area;
        std::uint8_t radius;
        std::uint8_t city;
    };
    std::vector<Anchor> anchors;
    for (const assets::IslandTile& t : slot.village->layers.front()) {
        const assets::BuildingType* b = m_buildings.by_id(t.cod_id());
        if (b == nullptr || (b->prod_kind != prod::kMarket && b->prod_kind != prod::kKontor)) {
            continue;
        }
        const auto radius = static_cast<std::uint8_t>(
            b->prod_kind == prod::kKontor ? std::max(b->radius, kMinKontorRadius) : b->radius);
        anchors.push_back(
            Anchor{catchment_of(*b, static_cast<std::uint8_t>(t.bits & 3U), {t.x, t.y}), radius, t.city()});
    }
    const auto covered = [&](std::uint8_t city, std::int32_t x, std::int32_t y) {
        if (anchors.empty()) {
            return true;  // nothing to measure against: leave the template as it is
        }
        for (const Anchor& a : anchors) {
            if (a.city != city) {
                continue;
            }
            const std::int32_t dy = y < a.area.centre.y                     ? a.area.centre.y - y
                                    : y > a.area.centre.y + a.area.parity_y ? y - a.area.centre.y - a.area.parity_y
                                                                            : 0;
            const std::int32_t dx = x < a.area.centre.x                     ? a.area.centre.x - x
                                    : x > a.area.centre.x + a.area.parity_x ? x - a.area.centre.x - a.area.parity_x
                                                                            : 0;
            const std::vector<std::uint8_t> half = circle_half_widths(a.radius);
            if (dy <= a.radius && dx <= half[static_cast<std::size_t>(dy)]) {
                return true;
            }
        }
        return false;
    };
    for (const assets::IslandTile& t : slot.village->layers.front()) {
        std::uint32_t bits = (t.bits & ~(0xFFU << 6)) | (std::uint32_t{slot.plan.number} << 6);
        if (t.city() < natives.size() && natives[t.city()]) {
            bits = (bits & ~(0xFU << 22)) | (std::uint32_t{kNatives} << 22);
            const assets::BuildingType* b = m_buildings.by_id(t.cod_id());
            if (b != nullptr && b->size_x == 1 && b->size_y == 1 && !covered(t.city(), t.x, t.y)) {
                bits = (bits & ~(7U << 14) & ~(0xFU << 22)) | (std::uint32_t{kNoCity} << 14) |
                       (std::uint32_t{kNoCity} << 22);
            }
        }
        const std::array<std::uint8_t, 8> e = {static_cast<std::uint8_t>(t.id),
                                               static_cast<std::uint8_t>(t.id >> 8),
                                               t.x,
                                               t.y,
                                               static_cast<std::uint8_t>(bits),
                                               static_cast<std::uint8_t>(bits >> 8),
                                               static_cast<std::uint8_t>(bits >> 16),
                                               static_cast<std::uint8_t>(bits >> 24)};
        out.insert(out.end(), e.begin(), e.end());
    }
    return out;
}

std::vector<std::uint8_t> Generator::village_records(const Slot& slot, const StateChunk& chunk,
                                                     const std::array<bool, 8>& natives) const {
    constexpr std::uint8_t kNatives = 6;
    const bool city = std::strcmp(chunk.name, "STADT4") == 0;
    const bool store = std::strcmp(chunk.name, "KONTOR2") == 0;
    std::vector<std::uint8_t> out;
    if (!slot.village_file) {
        return out;
    }
    const auto add = [&](std::vector<std::uint8_t> record) {
        if (record.size() < 3) {
            return;
        }
        record[0] = slot.plan.number;  // the handlers take the island being made (0x0060912C)
        if (city && record[1] < natives.size() && natives[record[1]]) {
            record[2] = kNatives;
            // 0x0046A220: goodwill 200 towards the four player slots; the loader turns zero
            // satisfaction, taxes and food into 128 (0x00484910).
            constexpr std::size_t kPlayers = 4;
            constexpr std::uint8_t kGoodwill = 200;
            for (std::size_t p = 0; p < kPlayers; ++p) {
                std::fill_n(record.begin() + static_cast<std::ptrdiff_t>(0x18 + 4 * p), 4, std::uint8_t{0});
                record[0x18 + 4 * p] = kGoodwill;
            }
            for (std::size_t k = 0; k < 5; ++k) {
                record[0x78 + k] = record[0x78 + k] == 0 ? std::uint8_t{0x80} : record[0x78 + k];
                record[0x7F + k] = record[0x7F + k] == 0 ? std::uint8_t{0x80} : record[0x7F + k];
            }
            record[0x86] = record[0x86] == 0 ? std::uint8_t{0x80} : record[0x86];
        }
        // 0x0047F5E0: a native village sells tobacco goods (north) or spices (south) and buys
        // alcohol and cloth up to 10 t, all for nothing. The ware slots (20 bytes each, from +4)
        // must lie within the record.
        constexpr std::uint8_t kTobaccoGoods = 15;
        constexpr std::uint8_t kSpiceGoods = 16;
        constexpr std::uint8_t kAlcohol = 18;
        constexpr std::uint8_t kCloth = 19;
        const auto slot_at = [](std::uint8_t ware) { return std::size_t{4} + 20U * ware; };
        if (store && record.size() >= slot_at(kCloth) + 20 && (record[3] & 0xFU) < natives.size() &&
            natives[record[3] & 0xFU]) {
            const auto set_flags = [&](std::uint8_t ware, std::uint32_t clear, std::uint32_t set) {
                const std::size_t o = slot_at(ware);
                std::uint32_t v = std::uint32_t{record[o]} | (std::uint32_t{record[o + 1]} << 8) |
                                  (std::uint32_t{record[o + 2]} << 16) | (std::uint32_t{record[o + 3]} << 24);
                v = (v & ~clear) | set;
                for (std::size_t i = 0; i < 4; ++i) {
                    record[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
                }
            };
            const std::uint8_t sold = slot.plan.climate == climate::kNorth ? kTobaccoGoods : kSpiceGoods;
            set_flags(sold, 0x3FFU, 1U << 20);
            record[slot_at(sold) + 8] = 0;
            record[slot_at(sold) + 9] = 0;
            for (const std::uint8_t ware : {kAlcohol, kCloth}) {
                set_flags(ware, 0x3FFU << 10, 1U << 21);
                record[slot_at(ware) + 10] = 0x40;  // 320 = 10 t
                record[slot_at(ware) + 11] = 0x01;
            }
        }
        out.insert(out.end(), record.begin(), record.end());
    };
    for (const assets::Chunk& c : slot.village_file->chunks()) {
        const auto data = slot.village_file->payload(c);
        if (c.name == chunk.name) {
            for (std::size_t o = 0; o + chunk.size <= data.size(); o += chunk.size) {
                add(std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(o),
                                              data.begin() + static_cast<std::ptrdiff_t>(o + chunk.size)));
            }
        } else if (city && c.name == "STADT3") {
            // Ten older village templates have the city in the old format (100 bytes); the same
            // fields as the loader takes them (0x00484910), in the places of STADT4.
            constexpr std::size_t kOld = 100;
            for (std::size_t o = 0; o + kOld <= data.size(); o += kOld) {
                const auto r = data.subspan(o, kOld);
                constexpr std::size_t kCity = 0xA8;
                std::array<std::uint8_t, kCity> n{};
                const std::uint32_t head = std::uint32_t{r[0]} | (std::uint32_t{r[1]} << 8) |
                                           (std::uint32_t{r[2]} << 16) | (std::uint32_t{r[3]} << 24);
                n[1] = static_cast<std::uint8_t>((head >> 8) & 0xFU);
                n[2] = static_cast<std::uint8_t>((head >> 15) & 0x1FU);
                n[4] = static_cast<std::uint8_t>((head >> 20) & 1U);
                std::copy_n(r.begin() + 0x14, 4, n.begin() + 0x58);
                std::copy_n(r.begin() + 0x18, 20, n.begin() + 0x5C);  // inhabitants
                std::copy_n(r.begin() + 0x36, 5, n.begin() + 0x78);
                std::copy_n(r.begin() + 0x3B, 5, n.begin() + 0x7F);
                n[0x86] = r[0x42];
                const auto name = r.subspan(0x43);
                std::copy_n(name.begin(), std::min<std::size_t>(name.size(), kCity - 0x87 - 1), n.begin() + 0x87);
                add(std::vector<std::uint8_t>(n.begin(), n.end()));
            }
        }
    }
    return out;
}

core::Result<GeneratedWorld> Generator::run(const assets::ChunkFile& scenario, bool guarantee_wanted) {
    m_max_crops = m_plan.max_crops != 0 ? m_plan.max_crops : 2;
    choose_templates();
    choose_natives();
    m_slots.reserve(m_plan.islands.size());
    for (const ScenarioPlan::Island& e : m_plan.islands) {
        Slot slot;
        slot.plan = e;
        if (load(slot)) {
            m_slots.push_back(std::move(slot));
        }
    }
    for (Slot& s : m_slots) {
        m_by_number.push_back(&s);
    }
    std::stable_sort(m_by_number.begin(), m_by_number.end(),
                     [](const Slot* a, const Slot* b) { return a->plan.number < b->plan.number; });
    if (!m_starts.empty()) {
        reserve_home_iron();
        if (m_gold_islands != 0) {
            reserve_start_gold();
        }
    }
    hand_out_crops();
    place_ores();
    place_treasures();
    if (!m_starts.empty()) {
        fair_starts();
    }
    if (m_gold_islands != 0) {
        top_up_gold();
    }
    if (guarantee_wanted) {
        guarantee();
    }

    // The scenario with these islands: per island INSEL5, the village's tiles, an empty HIRSCH2
    // (the island tick finds the deer spots) and the village's records; then the rest of the
    // scenario without the plan.
    assets::ChunkWriter out;
    GeneratedWorld result;
    std::uint32_t craters_total = 0;
    for (const Slot* s : m_by_number) {
        std::vector<Crater> craters = sim::find_craters(*s->map, m_buildings);
        if (craters.size() > 2) {
            craters.resize(2);
        }
        while (!craters.empty() && craters_total + craters.size() > kMaxCraters) {
            craters.pop_back();
        }
        craters_total += static_cast<std::uint32_t>(craters.size());
        const std::array<bool, 8> natives = native_cities(*s);
        std::vector<std::uint8_t> cities;
        for (const StateChunk& c : kStateChunks) {
            if (std::strcmp(c.name, "STADT4") == 0) {
                cities = village_records(*s, c, natives);
            }
        }
        // INSEL5 +0x0C: the owner of each city slot of the island, 7 for none.
        std::array<std::uint8_t, 8> owners;
        owners.fill(7);
        for (std::size_t o = 0; o + 0xA8 <= cities.size(); o += 0xA8) {
            if (cities[o + 1] < owners.size()) {
                owners[cities[o + 1]] = cities[o + 2];
            }
        }
        out.add("INSEL5", header(*s, craters, owners));
        out.add("INSELHAUS", village_tiles(*s, natives));
        out.add("HIRSCH2", {});
        for (const StateChunk& c : kStateChunks) {
            if (auto records = village_records(*s, c, natives); !records.empty()) {
                out.add(c.name, records);
            }
        }
        GeneratedIsland info;
        info.number = s->plan.number;
        info.climate = s->plan.climate;
        info.size = s->plan.size;
        info.template_number = s->plan.template_number;
        info.natives = s->village.has_value();
        info.fertility = s->fertility;
        for (std::size_t i = 0; i < s->deposits.count; ++i) {
            assets::IslandHeader::Deposit d;
            d.ware = s->deposits.ware(i);
            d.x = s->deposits.x(i);
            d.y = s->deposits.y(i);
            d.mask = s->deposits.bytes[8 * i + 3];
            d.kind = s->deposits.bytes[8 * i + 4];
            d.amount = static_cast<std::uint16_t>(s->deposits.bytes[8 * i + 6] | (s->deposits.bytes[8 * i + 7] << 8));
            info.deposits.push_back(d);
        }
        result.islands.push_back(std::move(info));
    }
    for (const assets::Chunk& c : scenario.chunks()) {
        if (c.name == "SZENE" || c.name == "RANDTAB" || c.name.rfind("SZENE_", 0) == 0 || c.name == "INSEL5" ||
            c.name == "INSELHAUS") {
            continue;
        }
        out.add(c.name, scenario.payload(c));
    }
    auto file = assets::ChunkFile::from_bytes(out.take());
    if (!file) {
        return file.error();
    }
    result.file = std::move(file).value();
    result.draws = m_random.draws();
    result.lost = m_lost;
    result.guaranteed = m_guaranteed;
    return result;
}

}  // namespace

std::uint32_t PlanRandom::below(std::uint32_t n) noexcept {
    ++m_draws;
    if (n == 0) {
        return 0;
    }
    if (m_seeded) {
        return m_random.below(n);
    }
    const auto v = static_cast<std::int16_t>(m_table[m_index]);
    m_index = (m_index + 1) & 511U;
    const std::int32_t r = v % static_cast<std::int32_t>(n);
    return static_cast<std::uint32_t>(r < 0 ? r + static_cast<std::int32_t>(n) : r);
}

bool plans_islands(const assets::ChunkFile& file) {
    if (file.find("INSEL5") != nullptr || file.find("INSEL4") != nullptr || file.find("INSEL3") != nullptr) {
        return false;
    }
    const auto plan = assets::load_scenario_plan(file);
    return plan && plan->has_value() && !(*plan)->islands.empty();
}

core::Result<GeneratedWorld> generate_islands(const assets::ChunkFile& scenario, const WorldPlan& plan,
                                              const assets::GameDirectory& game, const assets::BuildingTable& buildings,
                                              const WorldStart& start, std::vector<std::string>* warnings) {
    PlanRandom random(start.seed);
    Generator generator(plan.plan, game, buildings, random, warnings,
                        plan.shared_start ? std::vector<StartArea>{} : plan.starts, plan.gold_islands);
    return generator.run(scenario, start.guarantee_resources);
}

core::Result<GeneratedWorld> generate_islands(const assets::ChunkFile& scenario, const assets::GameDirectory& game,
                                              const assets::BuildingTable& buildings, const WorldStart& start,
                                              std::vector<std::string>* warnings) {
    auto plan = assets::load_scenario_plan(scenario);
    if (!plan) {
        return plan.error();
    }
    if (!plan->has_value()) {
        return core::make_error(core::ErrorCode::InvalidFormat, "the scenario has no world plan (SZENE)");
    }
    std::optional<PlanRandom> random;
    if (start.draws == WorldStart::Draws::Seeded) {
        random.emplace(start.seed);
    } else if (const auto table = assets::load_random_table(scenario)) {
        random.emplace(*table, static_cast<std::uint32_t>(start.seed & 511U));
    } else {
        random.emplace(start.seed);  // no table: Open1602's generator
    }
    Generator generator(std::move(**plan), game, buildings, *random, warnings);
    return generator.run(scenario, start.guarantee_resources);
}

}  // namespace o1602::sim
