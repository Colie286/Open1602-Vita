// SPDX-License-Identifier: GPL-3.0-or-later
//
// Figure definitions from figuren.cod (FIGUR records): ships, soldiers, carriers, flags, ...
// Only the fields needed for drawing are decoded so far.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "o1602/assets/cod_parser.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/core/result.hpp"

namespace o1602::assets {

class GameDirectory;

/// Sprite sheets of a zoom level in the order the original loads them (file names at 0x0049C4C0 ..
/// 0x0049C6F4). Values 0..6 are the figure Blocknr; shadows always come from SCHATTEN.BSH.
enum class FigureSheet : std::uint8_t {
    Stadtfld = 0,
    Soldat = 1,
    Ship = 2,
    Traeger = 3,
    Maeher = 4,
    Tiere = 5,
    Effekte = 6,
    Numbers = 7,
    Schatten = 8,
    Fische = 9,
    Gaukler = 10,
};
inline constexpr std::size_t kFigureSheetCount = 11;

/// File name of a sheet (lower case; GameDirectory resolves case-insensitively).
[[nodiscard]] constexpr const char* figure_sheet_file(FigureSheet sheet) noexcept {
    constexpr const char* kFiles[kFigureSheetCount] = {"stadtfld.bsh", "soldat.bsh", "ship.bsh",    "traeger.bsh",
                                                       "maeher.bsh",   "tiere.bsh",  "effekte.bsh", "numbers.bsh",
                                                       "schatten.bsh", "fische.bsh", "gaukler.bsh"};
    return kFiles[static_cast<std::size_t>(sheet) % kFigureSheetCount];
}

/// Kind of an animation (ANIM Kind): what happens after it ran AnimRept times (0x0045D0B0).
enum class AnimKind : std::uint8_t {
    None = 0,
    Endless = 1,  ///< start over
    Random = 2,   ///< continue with a random entry of `next`
    JumpTo = 3,   ///< continue with next[0]
};

/// One ANIM object of a figure (0x1E bytes per animation in the original, figure record + 0x60).
struct FigureAnim {
    AnimKind kind = AnimKind::None;
    std::uint32_t offset = 0;        ///< AnimOffs: first sprite relative to the figure's Gfx
    std::uint32_t add = 0;           ///< AnimAdd: sprite step per frame (none given: 0, 0x004413A9)
    std::uint32_t count = 1;         ///< AnimAnz: frames (1 to 255)
    std::uint32_t repeat = 1;        ///< AnimRept: runs before `kind` applies (1 to 255)
    std::uint32_t speed_ms = 100;    ///< AnimSpeed: duration of one frame
    std::uint32_t rotate = 0;        ///< sprite step per direction (Rotate, default: the figure's)
    std::vector<std::uint8_t> next;  ///< AnimNr
    /// AnimSmp: sound at the end of a cycle (WAV_* number, 0 = none); with `range` > 1 the sound
    /// + rand() % range, played only when below `chance` (one value: 1, 1; two: chance = range).
    std::uint16_t sound = 0;
    std::uint8_t sound_range = 1;
    std::uint8_t sound_chance = 1;
    bool defined = false;
};

/// A sound with variants (Shotsample, Hitsample): `sample` + rand() % `variants`.
struct FigureSound {
    std::uint16_t sample = 0;  ///< WAV_* number, 0 = none
    std::uint8_t variants = 1;
};

/// Figure numbers a table holds at most (FIGUR Nummer below it).
inline constexpr std::uint32_t kMaxFigures = 1024;

struct FigureType {
    std::uint32_t number = 0;  ///< Nummer (index into the original table at 0x0061FCC0)
    std::int32_t kind = 0;
    std::uint32_t gfx = 0;  ///< first sprite in its sheet
    FigureSheet sheet = FigureSheet::Stadtfld;
    std::uint32_t rotate = 0;               ///< sprite distance per direction step
    std::array<std::int32_t, 2> posoffs{};  ///< [0]: vertical offset in pixels at full zoom (record +0x0C)
    /// Fahnoffs: flag position on ships/buildings. [1] distance along the ship axis and [2] height,
    /// both in tile units (height in half tile widths). Presentation only, so floating point.
    std::array<double, 3> fahnoffs{};
    std::uint32_t shadow = 0;    ///< Schatten: figure number of the shadow, 0 = none
    std::uint32_t bow_wave = 0;  ///< Bugfignr (ships, the same slot +0x44): bow wave figure, 0 = none
    FigureSound shot_sound;      ///< Shotsample (+0x4C, +0x2F): when it fires
    FigureSound hit_sound;       ///< Hitsample (+0x42, +0x2E): where its shot or blow lands
    /// Speed (record +0x10, the original stores Speed * 0.0001): walking speed. On a tile with
    /// Wegspeed w (32 = road) a figure covers 0.16 * speed / w tiles per second (0x0044A690).
    std::uint32_t speed = 0;
    /// Speedtyp (record +0x2C): which of the four Wegspeed values of a tile applies.
    std::uint8_t speed_type = 0;
    /// Maxtrag (record +0x36): goods the figure carries, in 1/32 t.
    std::uint32_t max_carry = 0;
    /// Worktime (record +0x14): game milliseconds of one work step (harvesting a field, ...).
    std::uint32_t work_time_ms = 0;
    /// Ship data (docs/re/ships.md), zero for other figures.
    struct ShipData {
        std::uint8_t cargo_slots = 0;    ///< Maxware (+0x38): holds of 50 t each
        std::uint8_t max_cannons = 0;    ///< Maxkanon (+0x3A)
        std::uint32_t max_health = 0;    ///< Maxenergy (+0x3C), in 1/32
        std::uint32_t hit = 0;           ///< Hitpoint (+0x3E): damage of one shot, in 1/32
        std::uint32_t shot_radius = 0;   ///< Shotradius (+0x4A): range in tiles
        std::uint32_t shot_ms = 0;       ///< Shottime (+0x50): time between two shots
        std::uint32_t turn_ms = 0;       ///< Drehtime (+0x18): one 45 degree step
        std::uint32_t sink_ms = 0;       ///< Stirbtime (+0x1C): sinking
        std::uint32_t price = 0;         ///< Preis (+0x54): value of the ship (computer players, sales)
        std::uint8_t build_ware = 0;     ///< Rohstoff (+0x31): material a shipyard builds it from
        std::uint32_t build_amount = 0;  ///< Rohmenge (+0x32): that material in 1/32 t
    };
    ShipData ship;
    /// Fighting values of soldiers, towers and ships (docs/re/military.md).
    struct Combat {
        std::uint32_t max_health = 0;     ///< Maxenergy (+0x3C), 1/32 HP
        std::uint32_t hit = 0;            ///< Hitpoint (+0x3E): damage at full health, 1/32 HP (truncated)
        std::uint32_t range8 = 0;         ///< Shotradius (+0x4A) in 1/8 tile
        std::uint32_t hit_delay_ms = 0;   ///< Shottime (+0x50): from the shot to the hit
        std::uint32_t die_ms = 2500;      ///< Stirbtime (+0x1C): death animation or sinking
        std::uint8_t max_step = 1;        ///< Maxstepcnt (+0x2D): cells per run
        std::uint8_t weapon_ware = 0;     ///< Rohstoff (+0x31): weapons a castle needs for the unit
        std::uint32_t weapon_amount = 0;  ///< Rohmenge (+0x32), 1/32 t
        std::uint16_t hit_figure = 0;     ///< Hitfignr: effect at the target
        std::uint16_t shot_figure = 0;    ///< Shotfignr: smoke at the shooter
    };
    Combat combat;
    std::vector<FigureAnim> anims;  ///< indexed by ANIM Nummer
    bool defined = false;

    [[nodiscard]] const FigureAnim* anim(std::size_t index) const noexcept {
        return index < anims.size() && anims[index].defined ? &anims[index] : nullptr;
    }
};

/// Sprite of a figure within its sheet, as 0x0044F8F0 computes it:
/// Gfx + AnimOffs + AnimAdd * frame + ((direction + 2 * view rotation) & 7) * Rotate. Without the
/// animation (or for an undefined one) only Gfx + direction * the figure's Rotate.
[[nodiscard]] std::uint32_t figure_sprite(const FigureType& type, std::size_t anim, std::uint32_t frame,
                                          std::uint8_t direction, std::uint8_t view_rotation) noexcept;

/// A unit drawn once per player colour: the original keeps four consecutive figures, one each for
/// red, blue, yellow and white (SOLDAT1..4, KAVALERIE1..4, MUSKETIER1..4, KANONIER1..4).
struct UnitVariant {
    std::uint32_t family = 0;  ///< figure of variant 0 (red)
    std::uint8_t index = 0;    ///< variant of this figure, = original player colour 0..3
    std::uint8_t count = 0;    ///< variants of the family
};

class FigureTable {
public:
    /// First figures of the unit families of the original (figure numbers 1..16, 4 colours each).
    static constexpr std::array<std::uint32_t, 4> kUnitFamilies = figure::kUnitFamilies;
    static constexpr std::uint8_t kUnitVariants = 4;

    /// Ship flags by player colour (table at 0x00498F2C): FAHNE1..4, FAHNEPIRAT.
    static constexpr std::array<std::uint32_t, 5> kFlagFigures = figure::kFlags;

    static core::Result<FigureTable> from_cod(const cod::Document& figuren);
    static core::Result<FigureTable> load(const GameDirectory& game);

    /// Upkeep of soldiers per unit type (FIGTYP 1..4: swordsman, rider, musketeer, gunner) and of
    /// a cannon tower, gold per minute (`Soldat:` / `Turm:` lines of figuren.cod).
    [[nodiscard]] std::uint32_t unit_upkeep(std::int32_t unit_type) const noexcept {
        return unit_type >= 0 && static_cast<std::size_t>(unit_type) < m_unit_upkeep.size()
                   ? m_unit_upkeep[static_cast<std::size_t>(unit_type)]
                   : 0;
    }
    [[nodiscard]] std::uint32_t tower_upkeep() const noexcept { return m_tower_upkeep; }
    /// Third value of the `Soldat:` lines (0x0062BD20): what disbanding a unit of the type costs.
    [[nodiscard]] std::uint32_t unit_cost(std::int32_t unit_type) const noexcept {
        return unit_type >= 0 && static_cast<std::size_t>(unit_type) < m_unit_cost.size()
                   ? m_unit_cost[static_cast<std::size_t>(unit_type)]
                   : 0;
    }
    /// Formations (FORMATION records): slots as offsets in tiles, in the order units take them.
    struct FormationSlot {
        std::int32_t x = 0;
        std::int32_t y = 0;
    };
    [[nodiscard]] const std::vector<std::vector<FormationSlot>>& formations() const noexcept { return m_formations; }
    /// The same for corrections of the data after loading (data_fixes.hpp).
    [[nodiscard]] std::vector<std::vector<FormationSlot>>& formations() noexcept { return m_formations; }

    [[nodiscard]] const FigureType* get(std::uint32_t number) const noexcept {
        return number < m_types.size() && m_types[number].defined ? &m_types[number] : nullptr;
    }
    /// The same for the values a game changes (sim::apply_figure_values): the types keep their places.
    [[nodiscard]] FigureType* get(std::uint32_t number) noexcept {
        return number < m_types.size() && m_types[number].defined ? &m_types[number] : nullptr;
    }
    /// Every slot of the table, defined or not, in the order of their numbers.
    [[nodiscard]] std::span<FigureType> changeable_types() noexcept { return m_types; }
    [[nodiscard]] std::span<const FigureType> types() const noexcept { return m_types; }
    /// Flag figure of a player colour, or nullptr for colours without flag (natives, trader).
    [[nodiscard]] const FigureType* flag_for_colour(std::uint8_t colour) const noexcept {
        return colour < kFlagFigures.size() ? get(kFlagFigures[colour]) : nullptr;
    }
    [[deprecated("use flag_for_colour")]] [[nodiscard]] const FigureType* flag_for_color(
        std::uint8_t colour) const noexcept {
        return flag_for_colour(colour);
    }
    /// Colour variant of a unit figure, nothing for figures drawn the same for everyone (e.g. the
    /// natives' spearmen SPEER1).
    [[nodiscard]] static std::optional<UnitVariant> unit_variant(std::uint32_t figure) noexcept {
        for (const std::uint32_t family : kUnitFamilies) {
            if (figure >= family && figure < family + kUnitVariants) {
                return UnitVariant{family, static_cast<std::uint8_t>(figure - family), kUnitVariants};
            }
        }
        return std::nullopt;
    }

private:
    std::vector<FigureType> m_types;
    std::array<std::uint32_t, 8> m_unit_upkeep{};
    std::uint32_t m_tower_upkeep = 0;
    std::array<std::uint32_t, 8> m_unit_cost{};
    std::vector<std::vector<FormationSlot>> m_formations;
};

}  // namespace o1602::assets
