// SPDX-License-Identifier: GPL-3.0-or-later
//
// The island templates of the original (Szenes/Nord, Sued, NordNat, SuedNat): what the world
// plan and the world generator draw from, in one place (MD-7). Climates are assets::climate.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "o1602/sim/game_data.hpp"

namespace o1602::sim::templates {

/// Size classes lit, mit, med, big, lar.
inline constexpr std::size_t kSizeClasses = 5;
inline constexpr std::array<const char*, kSizeClasses> kSizeName = {"lit", "mit", "med", "big", "lar"};
/// Largest template of each size class; all templates of a class have this size except NORD med
/// (30..50 x 30..52). The height is also the nominal one of a class (0x0049A840 / 0x0049A854) for
/// the climate of islands placed by position.
inline constexpr std::array<std::uint32_t, kSizeClasses> kClassWidth = {30, 40, 50, 70, 100};
inline constexpr std::array<std::uint32_t, kSizeClasses> kClassHeight = {30, 40, 52, 60, 90};
/// Templates the original draws from per climate and size class (0x0049A868): NORD med36-38
/// exist as files but are never drawn.
inline constexpr std::array<std::array<std::uint16_t, kSizeClasses>, climate::kCount> kTemplateCount = {
    {{51, 48, 36, 67, 5}, {49, 45, 48, 63, 6}}};
/// The folders of the templates of each climate, without and with native villages.
inline constexpr std::array<const char*, climate::kCount> kClimateDir = {"Nord", "Sued"};
inline constexpr std::array<const char*, climate::kCount> kNativeDir = {"NordNat", "SuedNat"};

/// The file of a template ("Nord/med07.scp"); an odd climate is the south's, a size beyond the
/// classes the largest.
[[nodiscard]] std::string template_path(std::uint8_t climate, std::uint8_t size, std::uint16_t number, bool natives);

}  // namespace o1602::sim::templates
