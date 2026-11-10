// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/faction.hpp"

#include <algorithm>
#include <utility>

#include "o1602/core/data_file.hpp"

namespace o1602::sim {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// The field `key` of a Scaling (`nests.per`), or nullptr.
std::uint16_t* scaling_field(Scaling& s, std::string_view key) noexcept {
    return key == "base"   ? &s.base
           : key == "per"  ? &s.per
           : key == "from" ? &s.from
           : key == "min"  ? &s.min
           : key == "max"  ? &s.max
                           : nullptr;
}

/// The number field `key` of a faction, or nullptr.
std::uint16_t* number_field(Faction& f, std::string_view key) noexcept {
    if (key == "reach_players") {
        return &f.reach_players;
    }
    for (const auto& [prefix, scaling] :
         {std::pair{std::string_view("nests."), &f.nests}, std::pair{std::string_view("ships."), &f.ships}}) {
        if (key.starts_with(prefix)) {
            return scaling_field(*scaling, key.substr(prefix.size()));
        }
    }
    return nullptr;
}

}  // namespace

std::vector<Faction> base_factions() {
    std::vector<Faction> factions;
    for (const auto& [name, behaviour] :
         {std::pair{"settlers", FactionBehaviour::Settlers}, std::pair{"trader", FactionBehaviour::Trader},
          std::pair{"pirates", FactionBehaviour::Pirates}, std::pair{"natives", FactionBehaviour::Natives}}) {
        Faction f;
        f.id = std::string(kBaseFactionPackage) + ":" + name;
        f.behaviour = behaviour;
        factions.push_back(std::move(f));
    }
    return factions;
}

const char* behaviour_name(FactionBehaviour behaviour) noexcept {
    switch (behaviour) {
        case FactionBehaviour::Settlers:
            return "settlers";
        case FactionBehaviour::Trader:
            return "free trader";
        case FactionBehaviour::Pirates:
            return "pirates";
        case FactionBehaviour::Natives:
            return "natives";
    }
    return "?";
}

void apply_faction_file(std::vector<Faction>& factions, std::string_view package, std::string_view utf8,
                        std::vector<std::string>& warnings) {
    const std::string where = std::string(package) + "/" + std::string(kFactionFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& table : *tables) {
        if (table.name.empty()) {
            warnings.push_back(line_of(where, table.entries.front().value.line) +
                               "fields before the first [faction] belong to none and are passed over");
            continue;
        }
        const std::string id =
            table.name.find(':') == std::string::npos ? std::string(package) + ":" + table.name : table.name;
        const auto it = std::find_if(factions.begin(), factions.end(), [&](const Faction& f) { return f.id == id; });
        if (it == factions.end()) {
            // A faction of its own needs slots in the game and a behaviour of its own (M8).
            warnings.push_back(line_of(where, table.line) + "there is no faction " + id +
                               " to change (factions of a package's own are not supported yet)");
            continue;
        }
        for (const core::DataEntry& e : table.entries) {
            const std::string at = line_of(where, e.value.line);
            std::uint16_t* field = number_field(*it, e.key);
            const std::int64_t* n = e.value.integer();
            if (field == nullptr) {
                warnings.push_back(at + "unknown field '" + e.key + "'");
            } else if (n == nullptr || *n < 0 || *n > std::numeric_limits<std::uint16_t>::max()) {
                warnings.push_back(at + "'" + e.key + "' must be a number from 0 to 65535");
            } else {
                *field = static_cast<std::uint16_t>(*n);
            }
        }
    }
}

}  // namespace o1602::sim
