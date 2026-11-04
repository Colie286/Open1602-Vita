// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/faction.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "o1602/core/data_file.hpp"
#include "o1602/core/text.hpp"

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

namespace {

/// The behaviour of a base faction by its name in factions.toml.
std::optional<FactionBehaviour> behaviour_of(std::string_view name) noexcept {
    for (const Faction& f : base_factions()) {
        if (f.id.substr(kBaseFactionPackage.size() + 1) == name) {
            return f.behaviour;
        }
    }
    return std::nullopt;
}

/// "name" of `package`, or an id with its package as it is.
std::string full_id(std::string_view package, std::string_view name) {
    if (name.find(':') != std::string_view::npos) {
        return std::string(name);
    }
    std::string id(package);
    id += ':';
    id += name;
    return id;
}

}  // namespace

std::optional<FactionId> find_faction(const std::vector<Faction>& factions, std::string_view id) {
    const auto it = std::find_if(factions.begin(), factions.end(), [&](const Faction& f) { return f.id == id; });
    if (it == factions.end()) {
        return std::nullopt;
    }
    return static_cast<FactionId>(it - factions.begin());
}

void apply_faction_file(std::vector<Faction>& factions, std::string_view package, std::string_view utf8,
                        std::vector<std::string>& warnings, std::vector<FactionReplacement>* replacements) {
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
        const std::string id = full_id(package, table.name);
        const auto entry = [&](std::string_view key) -> const core::DataEntry* {
            const auto e = std::find_if(table.entries.begin(), table.entries.end(),
                                        [&](const core::DataEntry& d) { return d.key == key; });
            return e != table.entries.end() ? &*e : nullptr;
        };
        std::optional<FactionId> index = find_faction(factions, id);
        const bool own = id.size() > package.size() && id.starts_with(package) && id[package.size()] == ':';
        if (!index) {
            // A faction of the package's own: it acts as one of the base game's.
            const core::DataEntry* b = entry("behaviour");
            const std::string* name = b != nullptr ? b->value.string() : nullptr;
            const auto behaviour = name != nullptr ? behaviour_of(*name) : std::nullopt;
            if (!own) {
                warnings.push_back(line_of(where, table.line) + "there is no faction " + id +
                                   " to change; a package adds factions only under its own name");
                continue;
            }
            if (!behaviour) {
                warnings.push_back(line_of(where, table.line) + "the new faction " + id +
                                   " needs 'behaviour': settlers, trader, pirates or natives");
                continue;
            }
            if (factions.size() >= kMaxFactions) {
                warnings.push_back(line_of(where, table.line) + "there are " + std::to_string(kMaxFactions) +
                                   " factions already; " + id + " is passed over");
                continue;
            }
            Faction f = base_factions()[base_faction(*behaviour)];
            f.id = id;
            factions.push_back(std::move(f));
            index = static_cast<FactionId>(factions.size() - 1);
        }
        Faction& faction = factions[*index];
        for (const core::DataEntry& e : table.entries) {
            const std::string at = line_of(where, e.value.line);
            if (e.key == "behaviour") {
                const std::string* name = e.value.string();
                if (name == nullptr || behaviour_of(*name) != faction.behaviour) {
                    warnings.push_back(at + "'behaviour' is given when a faction is added and stays as it is");
                }
                continue;
            }
            if (e.key == "replaces") {
                const std::string* name = e.value.string();
                const auto replaced = name != nullptr ? find_faction(factions, full_id(package, *name)) : std::nullopt;
                if (!own) {
                    warnings.push_back(at + "'replaces' is for the package's own factions");
                } else if (!replaced || *replaced == *index) {
                    warnings.push_back(at + "'replaces' names no other faction");
                } else if (factions[*replaced].behaviour != faction.behaviour) {
                    warnings.push_back(at + "'replaces' names a faction of another behaviour");
                } else if (replacements != nullptr) {
                    replacements->push_back(FactionReplacement{factions[*replaced].id, faction.id});
                }
                continue;
            }
            if (e.key.starts_with("name.")) {
                // A name to show (the editor's players): name.de = "Liga".
                const std::string language = e.key.substr(5);
                const std::string* text = e.value.string();
                const bool code = !language.empty() && language.size() <= 16 &&
                                  std::all_of(language.begin(), language.end(), [](char c) {
                                      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
                                  });
                if (!code || text == nullptr || text->empty()) {
                    warnings.push_back(at + "'" + e.key + "' must be name.<language> = \"a name\"");
                } else {
                    auto& names = faction.names;
                    std::erase_if(names, [&](const auto& n) { return n.first == language; });
                    names.emplace_back(language, core::utf8_to_cp1252(*text));
                }
                continue;
            }
            if (e.key == "scripted") {
                const bool* on = e.value.boolean();
                if (on == nullptr) {
                    warnings.push_back(at + "'scripted' must be true or false");
                } else if (*on && faction.behaviour != FactionBehaviour::Settlers &&
                           faction.behaviour != FactionBehaviour::Pirates) {
                    warnings.push_back(at + "'scripted' is for factions of settlers or pirates; the game steers " +
                                       behaviour_name(faction.behaviour));
                } else {
                    faction.scripted = *on;
                }
                continue;
            }
            std::uint16_t* field = number_field(faction, e.key);
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
