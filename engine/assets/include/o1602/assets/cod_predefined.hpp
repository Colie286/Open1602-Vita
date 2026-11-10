// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace o1602::assets {

/// A constant the original game registers before parsing a .cod file (see docs/formats/cod.md).
struct CodPredefinedSymbol {
    std::string_view name;
    std::int32_t value;
};

/// Symbols predefined by Cod_LoadHaeuser (1602.exe @ 0x00460750) for haeuser.cod.
std::span<const CodPredefinedSymbol> cod_predefined_haeuser() noexcept;

/// Symbols predefined by Cod_LoadFiguren (1602.exe @ 0x00441210) for figuren.cod.
std::span<const CodPredefinedSymbol> cod_predefined_figuren() noexcept;

}  // namespace o1602::assets
