// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/figure_table.hpp"

#include <algorithm>

#include "o1602/assets/cod.hpp"
#include "o1602/assets/cod_predefined.hpp"
#include "o1602/assets/game_directory.hpp"
#include "o1602/assets/savegame.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/numeric.hpp"

namespace o1602::assets {

using core::ErrorCode;
using core::make_error;

core::Result<FigureTable> FigureTable::from_cod(const cod::Document& figuren) {
    const auto* records = figuren.root.records("FIGUR");
    if (records == nullptr) {
        return make_error(ErrorCode::InvalidFormat, "figuren.cod contains no FIGUR records");
    }
    FigureTable table;
    for (const auto& [nummer, r] : *records) {
        if (nummer < 0 || static_cast<std::uint32_t>(nummer) >= kMaxFigures) {
            return make_error(ErrorCode::InvalidFormat, "FIGUR number out of range: " + std::to_string(nummer));
        }
        if (static_cast<std::size_t>(nummer) >= table.m_types.size()) {
            table.m_types.resize(static_cast<std::size_t>(nummer) + 1);
        }
        FigureType f;
        f.number = static_cast<std::uint32_t>(nummer);
        f.kind = r.integer("Kind");
        f.gfx = static_cast<std::uint32_t>(std::max(0, r.integer("Gfx")));
        f.sheet = static_cast<FigureSheet>(std::clamp(r.integer("Blocknr"), 0, 6));
        f.shadow = static_cast<std::uint32_t>(std::max(0, r.integer("Schatten")));
        for (auto [key, sound] : {std::pair{"Shotsample", &f.shot_sound}, std::pair{"Hitsample", &f.hit_sound}}) {
            sound->sample = static_cast<std::uint16_t>(std::clamp(r.integer(key, 0), 0, 0xFFFF));
            sound->variants = static_cast<std::uint8_t>(std::clamp(r.integer(key, 1, 1), 1, 255));
        }
        f.bow_wave = static_cast<std::uint32_t>(std::max(0, r.integer("Bugfignr")));
        f.rotate = static_cast<std::uint32_t>(std::max(0, r.integer("Rotate")));
        f.posoffs = {r.integer("Posoffs", 0), r.integer("Posoffs", 1)};
        f.speed = static_cast<std::uint32_t>(std::max(0, r.integer("Speed")));
        f.speed_type = static_cast<std::uint8_t>(std::clamp(r.integer("Speedtyp"), 0, 3));
        f.max_carry = static_cast<std::uint32_t>(std::max(0, r.integer("Maxtrag"))) * kStockUnitsPerTon;
        if (const cod::Field* work = r.field("Worktime"); work != nullptr && !work->values.empty()) {
            f.work_time_ms = core::saturating_cast<std::uint32_t>(work->values[0].real * 1000.0 + 0.5);
        }
        const auto seconds_ms = [&](const char* key) -> std::uint32_t {
            const cod::Field* field = r.field(key);
            return field != nullptr && !field->values.empty()
                       ? core::saturating_cast<std::uint32_t>(field->values[0].real * 1000.0 + 0.5)
                       : 0U;
        };
        const auto tons = [&](const char* key) -> std::uint32_t {
            const cod::Field* field = r.field(key);
            return field != nullptr && !field->values.empty()
                       ? core::saturating_cast<std::uint32_t>(field->values[0].real * kStockUnitsPerTon)
                       : 0U;
        };
        f.ship.cargo_slots = static_cast<std::uint8_t>(std::clamp(r.integer("Maxware"), 0, 255));
        f.ship.max_cannons = static_cast<std::uint8_t>(std::clamp(r.integer("Maxkanon"), 0, 255));
        f.ship.max_health = tons("Maxenergy");
        f.ship.hit = tons("Hitpoint");
        f.ship.shot_radius = static_cast<std::uint32_t>(std::max(0, r.integer("Shotradius")));
        f.ship.shot_ms = seconds_ms("Shottime");
        f.ship.turn_ms = seconds_ms("Drehtime");
        f.ship.sink_ms = seconds_ms("Stirbtime");
        f.ship.price = static_cast<std::uint32_t>(std::max(0, r.integer("Preis")));
        f.ship.build_ware = static_cast<std::uint8_t>(std::clamp(r.integer("Rohstoff"), 0, 255));
        f.ship.build_amount = tons("Rohmenge");
        f.combat.max_health = f.ship.max_health;
        f.combat.hit = f.ship.hit;
        if (const cod::Field* radius = r.field("Shotradius"); radius != nullptr && !radius->values.empty()) {
            f.combat.range8 = core::saturating_cast<std::uint32_t>(radius->values[0].real * 8.0);
        }
        f.combat.hit_delay_ms = f.ship.shot_ms;
        if (r.field("Stirbtime") != nullptr) {
            f.combat.die_ms = f.ship.sink_ms;
        }
        f.combat.max_step = static_cast<std::uint8_t>(std::clamp(r.integer("Maxstepcnt", 0, 1), 1, 15));
        f.combat.weapon_ware = f.ship.build_ware;
        f.combat.weapon_amount = f.ship.build_amount;
        f.combat.hit_figure = static_cast<std::uint16_t>(std::clamp(r.integer("Hitfignr"), 0, 0xFFFF));
        f.combat.shot_figure = static_cast<std::uint16_t>(std::clamp(r.integer("Shotfignr"), 0, 0xFFFF));
        if (const cod::Field* fahn = r.field("Fahnoffs")) {
            for (std::size_t i = 0; i < f.fahnoffs.size() && i < fahn->values.size(); ++i) {
                f.fahnoffs[i] = fahn->values[i].real;
            }
        }
        if (const auto* anims = r.records("ANIM")) {
            for (const auto& [index, a] : *anims) {
                if (index < 0 || index >= 64) {
                    continue;  // the original reserves room for a few dozen per figure
                }
                if (static_cast<std::size_t>(index) >= f.anims.size()) {
                    f.anims.resize(static_cast<std::size_t>(index) + 1);
                }
                FigureAnim anim;
                anim.kind = static_cast<AnimKind>(std::clamp(a.integer("Kind", 0, 1), 0, 3));  // ENDLESS by default
                anim.offset = static_cast<std::uint32_t>(std::max(0, a.integer("AnimOffs")));
                anim.add = static_cast<std::uint32_t>(std::max(0, a.integer("AnimAdd", 0, 0)));
                // At most 255 frames and runs: the animation's state counts them in a byte, and
                // with more an animation would never end (FB-19).
                anim.count = static_cast<std::uint32_t>(std::clamp(a.integer("AnimAnz", 0, 1), 1, 255));
                anim.repeat = static_cast<std::uint32_t>(std::clamp(a.integer("AnimRept", 0, 1), 1, 255));
                anim.speed_ms = static_cast<std::uint32_t>(std::max(1, a.integer("AnimSpeed", 0, 100)));
                anim.rotate = a.field("Rotate") != nullptr
                                  ? static_cast<std::uint32_t>(std::max(0, a.integer("Rotate")))
                                  : f.rotate;
                if (const cod::Field* next = a.field("AnimNr")) {
                    for (const cod::Value& v : next->values) {
                        anim.next.push_back(static_cast<std::uint8_t>(std::clamp(v.integer, 0, 255)));
                    }
                }
                if (const cod::Field* smp = a.field("AnimSmp"); smp != nullptr && !smp->values.empty()) {
                    anim.sound = static_cast<std::uint16_t>(std::clamp(smp->values[0].integer, 0, 0xFFFF));
                    const std::int32_t range = smp->values.size() > 1 ? smp->values[1].integer : 1;
                    anim.sound_range = static_cast<std::uint8_t>(std::clamp(range, 1, 255));
                    anim.sound_chance = static_cast<std::uint8_t>(
                        std::clamp(smp->values.size() > 2 ? smp->values[2].integer : range, 1, 255));
                }
                anim.defined = true;
                f.anims[static_cast<std::size_t>(index)] = std::move(anim);
            }
        }
        f.defined = true;
        table.m_types[static_cast<std::size_t>(nummer)] = std::move(f);
    }
    // `Soldat: FIGTYP, upkeep, price` and `Turm: KANONTURM, upkeep` (0x004424xx).
    for (const auto& [name, values] : figuren.root.history) {
        if (name == "Soldat" && values.size() >= 2 && values[0].integer >= 0 &&
            static_cast<std::size_t>(values[0].integer) < table.m_unit_upkeep.size()) {
            table.m_unit_upkeep[static_cast<std::size_t>(values[0].integer)] =
                static_cast<std::uint32_t>(std::max(0, values[1].integer));
            if (values.size() >= 3) {
                table.m_unit_cost[static_cast<std::size_t>(values[0].integer)] =
                    static_cast<std::uint32_t>(std::max(0, values[2].integer));
            }
        } else if (name == "Turm" && values.size() >= 2) {
            table.m_tower_upkeep = static_cast<std::uint32_t>(std::max(0, values[1].integer));
        }
    }
    if (const auto* formations = figuren.root.records("FORMATION")) {
        for (const auto& [nummer, r] : *formations) {
            if (nummer < 0 || nummer >= 64) {
                continue;
            }
            if (static_cast<std::size_t>(nummer) >= table.m_formations.size()) {
                table.m_formations.resize(static_cast<std::size_t>(nummer) + 1);
            }
            auto& slots = table.m_formations[static_cast<std::size_t>(nummer)];
            for (const auto& [name, values] : r.history) {
                if (name == "Offs" && values.size() >= 3) {
                    const auto index = static_cast<std::size_t>(std::clamp(values[0].integer, 0, 63));
                    if (index >= slots.size()) {
                        slots.resize(index + 1);
                    }
                    slots[index] = {values[1].integer, values[2].integer};
                }
            }
        }
    }
    return table;
}

std::uint32_t figure_sprite(const FigureType& type, std::size_t anim, std::uint32_t frame, std::uint8_t direction,
                            std::uint8_t view_rotation) noexcept {
    const std::uint32_t dir = (direction + 2U * view_rotation) & 7U;
    if (const FigureAnim* a = type.anim(anim)) {
        return type.gfx + a->offset + a->add * frame + dir * a->rotate;
    }
    return type.gfx + dir * type.rotate;
}

core::Result<FigureTable> FigureTable::load(const GameDirectory& game) {
    const auto path = game.resolve("figuren.cod");
    if (!path) {
        return make_error(ErrorCode::NotFound, "figuren.cod not found in the game directory");
    }
    auto bytes = core::read_file(*path);
    if (!bytes) {
        return bytes.error();
    }
    return from_cod(cod::parse(decode_cod(*bytes), cod_predefined_figuren()));
}

}  // namespace o1602::assets
