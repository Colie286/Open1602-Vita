// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenarios of content packages (docs/modding/scenarios.md, item 7 of docs/modding/open-work.md):
// `scenarios.toml` names maps a package brings, each with its names and briefing by language, its
// difficulty, the goals of the people's mission and a script of its own. The map is a scenario file
// of the original's format (.szs, .szm) or a game saved by Open1602. `campaigns.toml` puts a
// package's scenarios in a row.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "o1602/assets/game_content.hpp"
#include "o1602/assets/localized_names.hpp"

namespace o1602::assets {

/// The file of a package that names its scenarios.
inline constexpr std::string_view kScenariosFile = "scenarios.toml";

/// A city goal of a scenario (ScenarioGoals::city).
struct ScenarioCityGoal {
    std::int32_t inhabitants = 0;
    std::uint8_t resident_class = 0;
    std::uint32_t count = 0;
    friend bool operator==(const ScenarioCityGoal&, const ScenarioCityGoal&) = default;
};

/// The goals a scenario gives the people's mission; what is not given is no goal. Without any, the
/// map's own missions stay.
struct ScenarioGoals {
    std::optional<std::int32_t> inhabitants;    ///< in all cities together
    std::optional<std::int32_t> cities;         ///< so many cities
    std::optional<std::int32_t> balance;        ///< balance per minute
    std::optional<std::int32_t> trade_balance;  ///< trade balance per minute
    std::optional<std::uint16_t> defeated;      ///< so many opponents defeated
    bool castle = false;
    bool cathedral = false;
    /// Goods in stock over all own cities: the good's id ("anno1602:tools", "<package>:<name>")
    /// and tons; at most two, as the original's missions.
    std::vector<std::pair<std::string, std::uint32_t>> goods;
    /// A city with at least `inhabitants`, and `count` of them of class `resident_class` or higher.
    std::optional<ScenarioCityGoal> city;

    [[nodiscard]] bool any() const noexcept {
        return inhabitants || cities || balance || trade_balance || defeated || castle || cathedral || !goods.empty() ||
               city;
    }
    friend bool operator==(const ScenarioGoals&, const ScenarioGoals&) = default;
};

/// A scenario a package brings.
struct PackageScenario {
    std::string id;       ///< "<package>:<name>"
    std::string package;  ///< the package's id
    std::filesystem::path file;
    LocalizedNames names;      ///< Windows-1252
    LocalizedNames briefings;  ///< Windows-1252
    std::uint8_t stars = 0;    ///< difficulty 0 to 5, as the original's stars
    /// The scenario's own script (a file of the package), none if empty. It runs besides the
    /// package's script.lua, in a sandbox of its own.
    std::filesystem::path script;
    /// Money of every human player at the start; nothing keeps the map's.
    std::optional<std::int32_t> money;
    ScenarioGoals goals;
    /// Player slots of the map given to a faction (`slot.<n>`): the slot from 0 and the faction's id
    /// ("anno1602:pirates", "<package>:<name>").
    std::vector<std::pair<std::uint8_t, std::string>> slots;

    /// The name in `language`, else in English, else the first given, else the id.
    [[nodiscard]] std::string name(std::string_view language) const;
    [[nodiscard]] std::string briefing(std::string_view language) const;
};

/// Reads `scenarios.toml` of `package` (its text `utf8`) and adds its scenarios to `scenarios`, in
/// the order of the file. A table that cannot be read, or whose map or script is not a file of the
/// package, is passed over with a warning that names the file and the line.
void read_scenarios_file(std::vector<PackageScenario>& scenarios, const ModPackage& package, std::string_view utf8,
                         std::vector<std::string>& warnings);

/// The scenarios of `packages` that have a scenarios.toml, in their load order.
[[nodiscard]] std::vector<PackageScenario> load_package_scenarios(std::span<const ModPackage> packages,
                                                                  std::vector<std::string>& warnings);

/// The scenario of id `id`, nullptr if none.
[[nodiscard]] const PackageScenario* find_scenario(std::span<const PackageScenario> scenarios, std::string_view id);

/// The file of a package that names its campaigns.
inline constexpr std::string_view kCampaignsFile = "campaigns.toml";
/// Parts of a campaign at most: the choices of the mission screen (MISSION.GAD).
inline constexpr std::size_t kMaxCampaignParts = 5;

/// A campaign a package brings: scenarios of the package played one after the other, each opened
/// by winning the one before.
struct PackageCampaign {
    std::string id;            ///< "<package>:<name>"
    std::string package;       ///< the package's id
    LocalizedNames names;      ///< Windows-1252
    LocalizedNames briefings;  ///< Windows-1252
    std::uint8_t stars = 0;    ///< difficulty 0 to 5
    /// The ids of its scenarios, part 1 first; 1 to kMaxCampaignParts.
    std::vector<std::string> parts;
    /// The humans start a part with the money the local player had when winning the one before,
    /// instead of the part's own.
    bool carry_money = false;

    [[nodiscard]] std::string name(std::string_view language) const;
    [[nodiscard]] std::string briefing(std::string_view language) const;
};

/// Reads `campaigns.toml` of `package` (its text `utf8`) and adds its campaigns to `campaigns`. A
/// part names a scenario of `scenarios` of the same package by its name; a campaign with a part
/// that is none, or with a gap between its parts, is passed over with a warning.
void read_campaigns_file(std::vector<PackageCampaign>& campaigns, const ModPackage& package,
                         std::span<const PackageScenario> scenarios, std::string_view utf8,
                         std::vector<std::string>& warnings);

/// The campaigns of `packages` that have a campaigns.toml, in their load order.
[[nodiscard]] std::vector<PackageCampaign> load_package_campaigns(std::span<const ModPackage> packages,
                                                                  std::span<const PackageScenario> scenarios,
                                                                  std::vector<std::string>& warnings);

/// The campaign of id `id`, nullptr if none.
[[nodiscard]] const PackageCampaign* find_campaign(std::span<const PackageCampaign> campaigns, std::string_view id);

/// What a won part of a package's campaign leaves to the next: the local player's money and the
/// table `campaign` of the part's script (script::Sandbox::save_table).
struct CampaignCarry {
    std::int64_t money = 0;
    std::vector<std::uint8_t> table;
    friend bool operator==(const CampaignCarry&, const CampaignCarry&) = default;
};
/// The file in the user data folder's `campaigns` that keeps what winning part `part` (from 0) of
/// campaign `campaign` left: "<package>_<name>.<part + 1>.carry".
[[nodiscard]] std::filesystem::path campaign_carry_file(std::string_view campaign, std::size_t part);
[[nodiscard]] std::vector<std::uint8_t> write_campaign_carry(const CampaignCarry& carry);
/// Nothing if `bytes` is no such file.
[[nodiscard]] std::optional<CampaignCarry> read_campaign_carry(std::span<const std::uint8_t> bytes);

}  // namespace o1602::assets
