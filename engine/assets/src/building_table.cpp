// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/building_table.hpp"

#include <algorithm>
#include <array>

#include "o1602/assets/cod.hpp"
#include "o1602/assets/cod_predefined.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/numeric.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

namespace {

// Sprites of the island map are 16 bit, 0xFFFF marks an empty tile (sim::MapCell). The original's
// grid word has only 13 bits, but savegames store buildings, not sprites, so mods may go beyond.
constexpr std::uint32_t kMaxSprite = 0xFFFF;

std::uint32_t non_negative(std::int32_t v) {
    return v < 0 ? 0U : static_cast<std::uint32_t>(v);
}

/// An amount times a unit, at most the largest u32 instead of wrapping (HI-10).
std::uint32_t times(std::uint32_t v, std::uint32_t unit) {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(std::uint64_t{v} * unit, 0xFFFF'FFFFU));
}

/// A number that names something (a ware, a figure, a ruin record): beyond `max` it would name
/// another one after a plain cast, so the file is refused instead (HI-10).
template <typename T>
core::Result<T> id_field(std::int32_t nummer, const char* name, std::int32_t value, std::uint32_t max) {
    const std::uint32_t v = non_negative(value);
    if (v > max) {
        return make_error(ErrorCode::InvalidFormat, "HAUS #" + std::to_string(nummer) + ": " + name + " " +
                                                        std::to_string(value) + " is beyond " + std::to_string(max));
    }
    return static_cast<T>(v);
}

}  // namespace

core::Result<BuildingTable> BuildingTable::from_cod(const cod::Document& haeuser) {
    const auto* records = haeuser.root.records("HAUS");
    if (records == nullptr) {
        return make_error(ErrorCode::InvalidFormat, "haeuser.cod contains no HAUS records");
    }

    BuildingTable table;
    table.m_by_id.assign(BuildingTable::kMaxId - kIdOffset + 1, -1);

    for (const auto& [nummer, r] : *records) {
        const std::int32_t id = r.integer("Id");
        if (id == 0) {
            continue;  // unused slot (filled with defaults by ObjFill)
        }
        if (id < static_cast<std::int32_t>(kIdOffset) || id > static_cast<std::int32_t>(BuildingTable::kMaxId)) {
            return make_error(ErrorCode::InvalidFormat,
                              "HAUS #" + std::to_string(nummer) + " has invalid Id " + std::to_string(id));
        }
        if (nummer < 0) {
            return make_error(ErrorCode::InvalidFormat, "HAUS #" + std::to_string(nummer) + " has a negative Nummer");
        }
        BuildingType b;
        b.id = static_cast<std::uint32_t>(id);
        b.record = static_cast<std::uint32_t>(nummer);
        b.kind = r.integer("Kind");
        b.gfx = non_negative(r.integer("Gfx"));
        b.size_x = std::max(1U, non_negative(r.integer("Size", 0, 1)));
        b.size_y = std::max(1U, non_negative(r.integer("Size", 1, 1)));
        // The original's islands are at most 255 tiles wide; record numbers index a table.
        if (b.size_x > 255 || b.size_y > 255 || nummer > 0xFFFF) {
            return make_error(ErrorCode::InvalidFormat,
                              "HAUS #" + std::to_string(nummer) + " has an invalid Size or Nummer");
        }
        b.rotate = non_negative(r.integer("Rotate"));
        b.anim_count = non_negative(r.integer("AnimAnz"));
        b.anim_add = non_negative(r.integer("AnimAdd"));
        b.anim_time = r.integer("AnimTime");
        b.anim_frame = non_negative(r.integer("AnimFrame"));
        b.posoffs = r.integer("Posoffs");
        if (const cod::Field* einhoffs = r.field("Einhoffs"); einhoffs != nullptr && !einhoffs->values.empty()) {
            b.icon_height = einhoffs->values[0].real;
        }
        b.high = (r.integer("Highflg") & 1) != 0;
        b.draw_ground = (r.integer("Grundflg") & 1) != 0;
        b.door_on_edge = (r.integer("Tuerflg") & 1) != 0;
        b.blocks_shots = (r.integer("NoShotFlg") & 1) != 0;
        if (const cod::Field* energy = r.field("Maxenergy"); energy != nullptr && !energy->values.empty()) {
            b.max_health = core::saturating_cast<std::uint32_t>(energy->values[0].real * kStockUnitsPerTon);
        }
        if (const cod::Field* ruin = r.field("Ruinenr"); ruin != nullptr && !ruin->values.empty()) {
            // A negative number is no ruin (0xFF, as the plain cast made of -1).
            const std::int32_t n = ruin->values[0].integer;
            auto v = id_field<std::uint8_t>(nummer, "Ruinenr", n, 0xFF);
            if (!v) {
                return v.error();
            }
            b.ruin = n < 0 ? std::uint8_t{0xFF} : *v;
        }
        for (std::size_t i = 0; i < b.walk_speed.size(); ++i) {
            // 0x00462834: trunc(value * 32 / 100), at most 126. Zero would mean infinite speed
            // (a division by zero in the original); Open1602 keeps at least 1.
            // Clamped before the product (HI-17): a large value overflowed int, undefined in C++.
            const std::int32_t percent = std::clamp(r.integer("Wegspeed", i, 100), 0, 400);
            b.walk_speed[i] = static_cast<std::uint8_t>(std::clamp(percent * 32 / 100, 1, 126));
        }
        b.place_outside = (r.integer("PlaceFlg") & 1) != 0;
        b.coastal = (r.integer("Strandflg") & 1) != 0;
        b.upgrade = (r.integer("Ausbauflg") & 1) != 0;
        b.destroyed = (r.integer("Destroyflg") & 1) != 0;
        b.build_sound = static_cast<std::uint16_t>(std::clamp(r.integer("Bausample"), 0, 0xFFFF));
        b.beach_offset = static_cast<std::uint8_t>(std::clamp(r.integer("Strandoff"), 0, 255));
        b.mouth_offset = static_cast<std::uint8_t>(std::clamp(r.integer("Mundoff"), 0, 255));
        // Stored as value * 128 / 100 (record +0x40).
        b.edge_growth = static_cast<std::uint8_t>(
            std::clamp(std::clamp(r.integer("Randwachs", 0, 100), 0, 200) * 128 / 100, 0, 255));
        b.build_gfx = non_negative(r.integer("Baugfx"));
        b.kreuz_base = r.integer("KreuzBase");
        b.rand_count = static_cast<std::uint16_t>(std::min(non_negative(r.integer("RandAnz")), 0xFFFFU));
        b.rand_add = static_cast<std::uint16_t>(std::min(non_negative(r.integer("RandAdd")), 0xFFFFU));
        if (const auto* prod = r.records("HAUS_PRODTYP"); prod != nullptr && !prod->empty()) {
            const auto& prodtyp = prod->begin()->second;
            b.prod_kind = prodtyp.integer("Kind");
            for (std::size_t i = 0; i < b.attached_figures.size(); ++i) {
                b.attached_figures[i] = static_cast<std::uint16_t>(std::max(0, prodtyp.integer("Rauchfignr", i)));
            }
            const auto tons = [&](const char* name) {
                const cod::Field* f = prodtyp.field(name);
                const double v = f != nullptr && !f->values.empty() ? f->values[0].real : 0.0;
                // The original truncates (_ftol); the values in haeuser.cod are exact multiples of 1/32.
                return core::saturating_cast<std::uint32_t>(v * kStockUnitsPerTon);
            };
            auto ware = id_field<std::uint8_t>(nummer, "Ware", prodtyp.integer("Ware"), 0xFF);
            auto raw = id_field<std::uint8_t>(nummer, "Rohstoff", prodtyp.integer("Rohstoff"), 0xFF);
            auto raw2 = id_field<std::uint8_t>(nummer, "Workstoff", prodtyp.integer("Workstoff"), 0xFF);
            auto figure = id_field<std::uint16_t>(nummer, "Figurnr", prodtyp.integer("Figurnr"), 0xFFFF);
            auto group = id_field<std::uint8_t>(nummer, "BGruppe", prodtyp.integer("BGruppe"), 0xFF);
            auto mountain = id_field<std::uint8_t>(nummer, "Erzbergnr", prodtyp.integer("Erzbergnr"), 0xFF);
            for (const auto* v : {&ware, &raw, &raw2, &group, &mountain}) {
                if (!*v) {
                    return v->error();
                }
            }
            if (!figure) {
                return figure.error();
            }
            b.ware = *ware;
            b.raw_ware = *raw;
            b.raw2_ware = *raw2;
            b.produce_amount = tons("Prodmenge");
            b.raw_amount = tons("Rohmenge");
            b.raw2_amount = tons("Workmenge");
            b.interval = non_negative(prodtyp.integer("Interval"));
            b.worker_figure = *figure;
            b.worker_count = static_cast<std::uint8_t>(std::min(non_negative(prodtyp.integer("Figuranz")), 0xFFU));
            b.radius = static_cast<std::uint8_t>(std::min(255U, non_negative(prodtyp.integer("Radius"))));
            b.max_stock = times(non_negative(prodtyp.integer("Maxlager")), kStockUnitsPerTon);
            b.max_idle_cycles = non_negative(prodtyp.integer("Maxnorohst"));
            b.stock_animation = (prodtyp.integer("LagAniFlg") & 1) != 0;
            b.continuous_animation = (prodtyp.integer("Anicontflg") & 1) != 0;
            b.withered = (prodtyp.integer("Doerrflg") & 1) != 0;
            b.native = (prodtyp.integer("Nativflg") & 1) != 0;
            b.resident_class = *group;
            b.infra = static_cast<std::uint8_t>(std::min(255U, non_negative(prodtyp.integer("Bauinfra"))));
            b.workers = static_cast<std::uint16_t>(std::min(65535U, non_negative(prodtyp.integer("Arbeiter"))));
            if (const cod::Field* kosten = prodtyp.field("Kosten"); kosten != nullptr) {
                b.running_cost = non_negative(kosten->integer(0));
                b.idle_cost = non_negative(kosten->integer(1));
            }
            b.ore_mountain = *mountain;
            b.max_wares = static_cast<std::uint8_t>(std::min(255U, non_negative(prodtyp.integer("Maxware"))));
            b.max_wares2 = static_cast<std::uint8_t>(std::min(255U, non_negative(prodtyp.integer("Maxware", 1))));
        }

        if (const auto* cost = r.records("HAUS_BAUKOST"); cost != nullptr && !cost->empty()) {
            const auto& c = cost->begin()->second;
            const auto goods = [&](const char* name) {
                const cod::Field* f = c.field(name);
                const double v = f != nullptr && !f->values.empty() ? f->values[0].real : 0.0;
                return core::saturating_cast<std::uint32_t>(v * kStockUnitsPerTon);
            };
            b.build_cost.tools = goods("Werkzeug");
            b.build_cost.wood = goods("Holz");
            b.build_cost.bricks = goods("Ziegel");
            b.build_cost.cannons = goods("Kanon");
            b.build_cost.money = c.integer("Money");
            if (b.build_cost.money < 0) {
                // A negative cost would pay the player for building (HI-10).
                return make_error(ErrorCode::InvalidFormat,
                                  "HAUS #" + std::to_string(nummer) + " has a negative Money");
            }
        }
        if (b.prod_kind == prod::kGrowing && b.anim_count > 0) {
            // 0x00462F1D: the number of k in 1..32 with 40 s + 7 s * k <= Interval / AnimAnz,
            // clamped to 31 when an entry is added (0x0047C760).
            const std::uint64_t stage_ms = std::uint64_t{b.interval} * 1000U / b.anim_count;
            std::uint32_t cls = 0;
            for (std::uint32_t k = 1; k <= 32; ++k) {
                cls += 40000U + 7000U * k <= stage_ms ? 1U : 0U;
            }
            b.growth_class = static_cast<std::uint8_t>(std::min(cls, 31U));
        }

        // Duplicate ids exist in the original (the wooden walls at a river mouth repeat those of
        // the wooden beach walls, D-007); the first record wins the id, as in the id -> record
        // table the original builds while loading, but every record keeps its number and sprites.
        const std::size_t slot = b.id - kIdOffset;
        const auto index = static_cast<std::int32_t>(table.m_types.size());
        if (table.m_by_id[slot] < 0) {
            table.m_by_id[slot] = index;
        }
        if (std::uint64_t{b.gfx} + b.tile_count() > kMaxSprite) {
            return make_error(ErrorCode::InvalidFormat,
                              "building " + std::to_string(b.id) + ": sprites " + std::to_string(b.gfx) + " + " +
                                  std::to_string(b.tile_count()) + " reach beyond sprite 65534");
        }
        if (b.gfx + b.tile_count() > table.m_by_sprite.size()) {
            table.m_by_sprite.resize(b.gfx + b.tile_count(), -1);
        }
        for (std::uint32_t k = 0; k < b.tile_count(); ++k) {
            if (table.m_by_sprite[b.gfx + k] < 0) {
                table.m_by_sprite[b.gfx + k] = index;
            }
        }
        if (b.record >= table.m_by_record.size()) {
            table.m_by_record.resize(std::size_t{b.record} + 1, -1);
        }
        table.m_by_record[b.record] = index;
        table.m_types.push_back(b);
    }

    // Beach and river mouth variants know the land building they stand for.
    for (const BuildingType& land : table.m_types) {
        if (land.beach_offset == 0) {
            continue;
        }
        const std::uint32_t beach = land.record + land.beach_offset;
        if (beach >= table.m_by_record.size() || table.m_by_record[beach] < 0) {
            continue;
        }
        BuildingType& b = table.m_types[static_cast<std::size_t>(table.m_by_record[beach])];
        b.land_record = land.record;
        const std::uint32_t mouth = beach + b.mouth_offset;
        if (b.mouth_offset != 0 && mouth < table.m_by_record.size() && table.m_by_record[mouth] >= 0) {
            table.m_types[static_cast<std::size_t>(table.m_by_record[mouth])].land_record = land.record;
        }
    }

    // Classes of inhabitants and their needs (0x00460750: k = trunc(v * 8192) / 600).
    const auto per_tick = [](double v) -> std::uint32_t {
        return core::saturating_cast<std::uint32_t>(
            static_cast<double>(core::saturating_cast<std::int64_t>(v * 8192.0) / 600));
    };
    if (const cod::Field* food = haeuser.root.field("Nahrung"); food != nullptr && !food->values.empty()) {
        table.m_food_need = per_tick(food->values[0].real);
    }
    const auto ware_symbol = [&](const char* name, std::uint8_t& out) {
        if (const cod::Symbol* sym = haeuser.symbols.find(name); sym != nullptr && sym->defined) {
            out = static_cast<std::uint8_t>(sym->values[0].integer);
        }
    };
    ware_symbol("NAHRUNG", table.m_food_ware);
    ware_symbol("WERKZEUG", table.m_materials.tools);
    ware_symbol("HOLZ", table.m_materials.wood);
    ware_symbol("ZIEGEL", table.m_materials.bricks);
    ware_symbol("KANONEN", table.m_materials.cannons);
    if (const auto* groups = haeuser.root.records("BGRUPPE")) {
        for (const auto& [nummer, g] : *groups) {
            if (nummer < 0 || nummer > 255) {
                continue;
            }
            if (static_cast<std::size_t>(nummer) >= table.m_classes.size()) {
                table.m_classes.resize(static_cast<std::size_t>(nummer) + 1);
            }
            ResidentClass& c = table.m_classes[static_cast<std::size_t>(nummer)];
            c.look = static_cast<std::uint8_t>(nummer);
            c.share = times(non_negative(g.integer("Prozent")), 128U) / 100U;
            c.max_per_house = non_negative(g.integer("Maxwohn"));
            if (const cod::Field* tax = g.field("Steuer"); tax != nullptr && !tax->values.empty()) {
                c.tax = core::saturating_cast<std::uint32_t>(tax->values[0].real * 16.0 / 3.0);
            }
            c.hiscore = non_negative(g.integer("Hiscore"));
            if (const auto* wares = g.records("BGRUPPE_WARE")) {
                for (const auto& [n, list] : *wares) {
                    for (const auto& [name, values] : list.history) {
                        if (name != "Ware" || values.size() < 2) {
                            continue;
                        }
                        const auto ware = static_cast<std::uint8_t>(std::max(0, values[0].integer));
                        const std::uint32_t amount = per_tick(values[1].real);
                        if (ware == 0 || amount == 0) {
                            continue;
                        }
                        auto it = std::find_if(c.needs.begin(), c.needs.end(),
                                               [&](const ResidentClass::Need& need) { return need.ware == ware; });
                        if (it != c.needs.end()) {
                            it->amount = amount;  // a later line replaces an earlier one
                        } else {
                            c.needs.push_back(ResidentClass::Need{ware, amount});
                        }
                    }
                }
            }
        }
    }

    // Unlock levels (BAUINFRA, table 0x0061FBC0: {BGruppe, Minwohn} by INFRA number).
    if (const auto* infra = haeuser.root.records("BAUINFRA")) {
        for (const auto& [nummer, level] : *infra) {
            if (nummer < 0 || nummer > 255) {
                continue;
            }
            if (static_cast<std::size_t>(nummer) >= table.m_infra.size()) {
                table.m_infra.resize(static_cast<std::size_t>(nummer) + 1);
            }
            table.m_infra[static_cast<std::size_t>(nummer)] =
                InfraLevel{static_cast<std::uint8_t>(non_negative(level.integer("BGruppe"))),
                           non_negative(level.integer("Minwohn")),
                           {}};
        }
    }
    std::size_t max_infra = table.m_infra.size();
    for (const BuildingType& b : table.m_types) {
        max_infra = std::max<std::size_t>(max_infra, std::size_t{b.infra} + 1);
    }
    table.m_infra.resize(max_infra);

    for (std::size_t i = 0; i < table.m_types.size(); ++i) {
        if (table.m_types[i].kind == kind::kSea && table.m_types[i].prod_kind == prod::kRipe) {
            table.m_open_sea = static_cast<std::int32_t>(i);
            break;
        }
    }
    return table;
}

const BuildingType* BuildingTable::by_id(std::uint32_t id) const noexcept {
    if (id < kIdOffset || id > BuildingTable::kMaxId) {
        return nullptr;
    }
    const std::int32_t index = m_by_id[id - kIdOffset];
    return index < 0 ? nullptr : &m_types[static_cast<std::size_t>(index)];
}

const BuildingType* BuildingTable::first_house(std::size_t resident_class) const noexcept {
    for (const BuildingType& b : m_types) {
        if (b.prod_kind == prod::kHouse && b.resident_class == resident_class) {
            return &b;
        }
    }
    return nullptr;
}

const BuildingType* BuildingTable::ruin_record(std::uint8_t ruin, bool beach) const noexcept {
    std::size_t slot = ruin;
    // The original takes the next slot on beach for every slot (0x00463FE8); only the fisher's
    // hut (0) and the pier (2) can stand on beach, and their next slot is their beach ruin. For
    // any other slot the next one would be another building's ruin (or past the table).
    if (beach && (slot == 0 || slot == 2)) {
        ++slot;
    }
    return slot < id::kRuins.size() ? by_id(id::kRuins[slot]) : nullptr;
}

const BuildingType* BuildingTable::by_record(std::uint32_t record) const noexcept {
    if (record >= m_by_record.size()) {
        return nullptr;
    }
    const std::int32_t index = m_by_record[record];
    return index < 0 ? nullptr : &m_types[static_cast<std::size_t>(index)];
}

core::Result<BuildingTable> BuildingTable::load(const GameDirectory& game) {
    const auto path = game.resolve("haeuser.cod");
    if (!path) {
        return make_error(ErrorCode::NotFound, "haeuser.cod not found in the game directory");
    }
    auto bytes = core::read_file(*path);
    if (!bytes) {
        return bytes.error();
    }
    auto table = from_cod(cod::parse(decode_cod(*bytes), cod_predefined_haeuser()));
    if (table) {
        if (const auto id = table->missing()) {
            return make_error(ErrorCode::InvalidFormat,
                              "haeuser.cod has no HAUS with Id " + std::to_string(*id) + ", which the game needs");
        }
        // The rules know the needs of kResidentClasses classes and infra::kMaxLevel levels (MD-2,
        // MD-8): more would be given another class's needs or never be unlocked.
        if (table->resident_classes().size() > kResidentClasses) {
            return make_error(ErrorCode::LimitExceeded,
                              "haeuser.cod: " + std::to_string(table->resident_classes().size()) +
                                  " resident classes, at most " + std::to_string(kResidentClasses));
        }
        if (table->infra_levels().size() > std::size_t{infra::kMaxLevel} + 1) {
            return make_error(ErrorCode::LimitExceeded, "haeuser.cod: unlock level " +
                                                            std::to_string(table->infra_levels().size() - 1) +
                                                            ", at most " + std::to_string(infra::kMaxLevel));
        }
    }
    return table;
}

core::Result<std::uint32_t> BuildingTable::add(const BuildingType& like) {
    // From the top down, so that a modded haeuser.cod of its own keeps the Ids after the
    // original's and a package's buildings do not take them.
    std::uint32_t id = 0;
    for (std::uint32_t candidate = BuildingTable::kMaxId; candidate >= kIdOffset; --candidate) {
        if (m_by_id[candidate - kIdOffset] < 0) {
            id = candidate;
            break;
        }
    }
    if (id == 0) {
        return make_error(ErrorCode::Unsupported, "haeuser.cod has no Id free below " +
                                                      std::to_string(BuildingTable::kMaxId) + " for another building");
    }
    BuildingType b = like;
    b.id = id;
    // The variants of the original's houses are the records after the first; a copy has none.
    b.rand_count = 0;
    b.record = static_cast<std::uint32_t>(m_by_record.size());
    b.land_record = BuildingType::kNoRecord;
    b.beach_offset = 0;
    b.mouth_offset = 0;
    if (b.record > 0xFFFF) {
        return make_error(ErrorCode::Unsupported, "haeuser.cod has no record number free below 65536");
    }
    if (std::uint64_t{b.gfx} + b.tile_count() > kMaxSprite) {
        return make_error(ErrorCode::InvalidFormat, "building " + std::to_string(b.id) + ": sprites " +
                                                        std::to_string(b.gfx) + " + " + std::to_string(b.tile_count()) +
                                                        " reach beyond sprite 65534");
    }
    const auto index = static_cast<std::int32_t>(m_types.size());
    m_by_id[id - kIdOffset] = index;
    if (b.gfx + b.tile_count() > m_by_sprite.size()) {
        m_by_sprite.resize(b.gfx + b.tile_count(), -1);
    }
    for (std::uint32_t k = 0; k < b.tile_count(); ++k) {
        // The original's sprites keep their record; only sprites nothing else draws become this one's.
        if (m_by_sprite[b.gfx + k] < 0) {
            m_by_sprite[b.gfx + k] = index;
        }
    }
    m_by_record.push_back(index);
    m_types.push_back(b);
    return id;
}

std::optional<std::uint32_t> BuildingTable::missing() const noexcept {
    for (const std::uint32_t id : id::kRequired) {
        if (by_id(id) == nullptr) {
            return id;
        }
    }
    return std::nullopt;
}

}  // namespace o1602::assets
