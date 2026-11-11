// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/assets/package_scenarios.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

#include "o1602/assets/original_names.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"

namespace o1602::assets {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its scenario: lower case letters, digits and '_'.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

/// The language of a key `<prefix>.<language>`, empty if `key` is none.
std::string_view text_key_language(std::string_view key, std::string_view prefix) noexcept {
    if (!key.starts_with(prefix) || key.size() <= prefix.size() || key[prefix.size()] != '.') {
        return {};
    }
    const std::string_view code = key.substr(prefix.size() + 1);
    const bool ok = code.size() <= 16 && std::all_of(code.begin(), code.end(),
                                                     [](const char c) { return (c >= 'a' && c <= 'z') || c == '-'; });
    return ok ? code : std::string_view{};
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// The file `relative` of `package`, if it is a safe path to a file that is there.
std::optional<std::filesystem::path> package_file(const ModPackage& package, const std::string& relative) {
    if (!safe_package_path(relative)) {
        return std::nullopt;
    }
    std::filesystem::path path = package.folder / core::path_from_utf8(relative);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }
    return path;
}

/// A whole number from `min` to `max`, else nothing.
std::optional<std::int64_t> number(const core::DataValue& value, std::int64_t min, std::int64_t max) {
    const std::int64_t* n = value.integer();
    if (n == nullptr || *n < min || *n > max) {
        return std::nullopt;
    }
    return *n;
}

constexpr std::int64_t kMaxInt32 = std::numeric_limits<std::int32_t>::max();
constexpr std::int64_t kMinInt32 = std::numeric_limits<std::int32_t>::min();
constexpr std::uint8_t kMaxStars = 5;
constexpr std::size_t kMaxGoods = 2;
/// The highest player slot a scenario names (slot numbers are a u8 from 0, kNoPlayer excluded).
constexpr std::uint32_t kMaxSlot = 255;
/// Tons a goods goal may ask: the amount is kept in 1/32 t in 32 bits.
constexpr std::int64_t kMaxTons = std::numeric_limits<std::uint32_t>::max() / 32;

}  // namespace

std::string PackageScenario::name(std::string_view language) const {
    return name_in(names, language, id);
}

std::string PackageScenario::briefing(std::string_view language) const {
    return name_in(briefings, language, {});
}

void read_scenarios_file(std::vector<PackageScenario>& scenarios, const ModPackage& package, std::string_view utf8,
                         std::vector<std::string>& warnings) {
    const std::string where = package.id + "/" + std::string(kScenariosFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name +
                               "]: a scenario's name holds only lower case letters, digits and '_'");
            continue;
        }
        PackageScenario s;
        s.id = package.id + ":" + t.name;
        s.package = package.id;
        bool ok = true;
        const auto refuse = [&](const core::DataEntry& e, const std::string& what) {
            warnings.push_back(line_of(where, e.value.line) + "'" + e.key + "' " + what);
            ok = false;
        };
        for (const core::DataEntry& e : t.entries) {
            const std::string* text = e.value.string();
            if (e.key == "file") {
                const std::string ending = text != nullptr
                                               ? lower(core::path_to_utf8(core::path_from_utf8(*text).extension()))
                                               : std::string();
                const auto path = text != nullptr ? package_file(package, *text) : std::nullopt;
                if (!path || (ending != ".szs" && ending != ".szm" && ending != ".o1602")) {
                    refuse(e, "names no map of the package (a .szs, .szm or .o1602 file in its folder)");
                } else {
                    s.file = *path;
                }
            } else if (e.key == "script") {
                const auto path = text != nullptr ? package_file(package, *text) : std::nullopt;
                if (!path) {
                    refuse(e, "names no file of the package");
                } else {
                    s.script = *path;
                }
            } else if (e.key == "stars") {
                if (const auto n = number(e.value, 0, kMaxStars)) {
                    s.stars = static_cast<std::uint8_t>(*n);
                } else {
                    refuse(e, "is a number from 0 to " + std::to_string(kMaxStars));
                }
            } else if (e.key == "money") {
                if (const auto n = number(e.value, kMinInt32, kMaxInt32)) {
                    s.money = static_cast<std::int32_t>(*n);
                } else {
                    refuse(e, "is a whole number");
                }
            } else if (const std::string_view language = name_key_language(e.key); !language.empty()) {
                if (text == nullptr || text->empty()) {
                    refuse(e, "is a text");
                } else {
                    s.names.emplace_back(language, core::utf8_to_cp1252(*text));
                }
            } else if (const std::string_view b = text_key_language(e.key, "briefing"); !b.empty()) {
                if (text == nullptr) {
                    refuse(e, "is a text");
                } else {
                    s.briefings.emplace_back(b, core::utf8_to_cp1252(*text));
                }
            } else if (e.key.starts_with("slot.")) {
                // slot.3 = "corsairs": the package's faction; "anno1602:natives": another's.
                std::uint32_t n = 0;
                const std::string_view digits = std::string_view(e.key).substr(5);
                const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), n);
                if (ec != std::errc{} || end != digits.data() + digits.size() || n < 1 || n > kMaxSlot) {
                    refuse(e, "is no player slot: slots are numbered 1 to " + std::to_string(kMaxSlot));
                } else if (text == nullptr || text->empty()) {
                    refuse(e, "is the id of a faction");
                } else {
                    std::string faction = text->find(':') == std::string::npos ? package.id + ":" + *text : *text;
                    s.slots.emplace_back(static_cast<std::uint8_t>(n - 1), std::move(faction));
                }
            } else if (e.key.starts_with("goal.")) {
                ScenarioGoals& g = s.goals;
                const std::string_view goal = std::string_view(e.key).substr(5);
                const auto whole = [&](std::optional<std::int32_t>& field, std::int64_t min) {
                    if (const auto n = number(e.value, min, kMaxInt32)) {
                        field = static_cast<std::int32_t>(*n);
                    } else {
                        refuse(e, "is a whole number from " + std::to_string(min));
                    }
                };
                if (goal == "inhabitants") {
                    whole(g.inhabitants, 1);
                } else if (goal == "cities") {
                    whole(g.cities, 1);
                } else if (goal == "balance") {
                    whole(g.balance, kMinInt32);
                } else if (goal == "trade_balance") {
                    whole(g.trade_balance, kMinInt32);
                } else if (goal == "defeated") {
                    if (const auto n = number(e.value, 1, std::numeric_limits<std::uint16_t>::max())) {
                        g.defeated = static_cast<std::uint16_t>(*n);
                    } else {
                        refuse(e, "is a number from 1 to 65535");
                    }
                } else if (goal == "castle" || goal == "cathedral") {
                    if (e.value.boolean() == nullptr) {
                        refuse(e, "is true or false");
                    } else {
                        (goal == "castle" ? g.castle : g.cathedral) = *e.value.boolean();
                    }
                } else if (goal.starts_with("goods.")) {
                    // goal.goods.tools: the original's; goal.goods.pepper: the package's own;
                    // goal.goods."spice:pepper": another package's.
                    std::string good(goal.substr(6));
                    if (good.find(':') == std::string::npos) {
                        const bool original = find_original_name(original_ware_names(), good) != nullptr;
                        std::string full = original ? std::string("anno1602") : package.id;
                        full += ':';
                        full += good;
                        good = std::move(full);
                    }
                    const auto tons = number(e.value, 1, kMaxTons);
                    if (!tons) {
                        refuse(e, "is the tons, a number from 1");
                    } else if (g.goods.size() >= kMaxGoods) {
                        refuse(e, "is one goods goal too many: a mission has at most two");
                    } else {
                        g.goods.emplace_back(std::move(good), static_cast<std::uint32_t>(*tons));
                    }
                } else if (goal == "city.inhabitants" || goal == "city.class" || goal == "city.count") {
                    ScenarioCityGoal& c = g.city ? *g.city : g.city.emplace();
                    if (goal == "city.inhabitants") {
                        if (const auto n = number(e.value, 1, kMaxInt32)) {
                            c.inhabitants = static_cast<std::int32_t>(*n);
                        } else {
                            refuse(e, "is a whole number from 1");
                        }
                    } else if (goal == "city.class") {
                        if (const auto n = number(e.value, 0, 255)) {
                            c.resident_class = static_cast<std::uint8_t>(*n);
                        } else {
                            refuse(e, "is a class number from 0");
                        }
                    } else if (const auto n = number(e.value, 0, kMaxInt32)) {
                        c.count = static_cast<std::uint32_t>(*n);
                    } else {
                        refuse(e, "is a whole number from 0");
                    }
                } else {
                    refuse(e, "is no goal of a scenario and is passed over");
                }
            } else {
                refuse(e, "is no field of a scenario and is passed over");
            }
        }
        if (s.file.empty()) {
            warnings.push_back(at + s.id + " has no map ('file'); the scenario is passed over");
            continue;
        }
        if (!ok) {
            warnings.push_back(at + s.id + " is passed over");
            continue;
        }
        scenarios.push_back(std::move(s));
    }
}

namespace {

/// The text of the file `name` of `package`, nothing if it has none or it cannot be read (with a
/// warning).
std::optional<std::string> package_text(const ModPackage& package, std::string_view name,
                                        std::vector<std::string>& warnings) {
    const std::filesystem::path path = package.folder / std::filesystem::path(name);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }
    const auto bytes = core::read_file(path);
    if (!bytes) {
        warnings.push_back(package.id + "/" + std::string(name) + ": " + bytes.error().message);
        return std::nullopt;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    return std::string(core::skip_utf8_bom(text));
}

}  // namespace

std::vector<PackageScenario> load_package_scenarios(std::span<const ModPackage> packages,
                                                    std::vector<std::string>& warnings) {
    std::vector<PackageScenario> scenarios;
    for (const ModPackage& p : packages) {
        if (const auto text = package_text(p, kScenariosFile, warnings)) {
            read_scenarios_file(scenarios, p, *text, warnings);
        }
    }
    return scenarios;
}

const PackageScenario* find_scenario(std::span<const PackageScenario> scenarios, std::string_view id) {
    const auto it =
        std::find_if(scenarios.begin(), scenarios.end(), [&](const PackageScenario& s) { return s.id == id; });
    return it != scenarios.end() ? &*it : nullptr;
}

std::string PackageCampaign::name(std::string_view language) const {
    return name_in(names, language, id);
}

std::string PackageCampaign::briefing(std::string_view language) const {
    return name_in(briefings, language, {});
}

void read_campaigns_file(std::vector<PackageCampaign>& campaigns, const ModPackage& package,
                         std::span<const PackageScenario> scenarios, std::string_view utf8,
                         std::vector<std::string>& warnings) {
    const std::string where = package.id + "/" + std::string(kCampaignsFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name +
                               "]: a campaign's name holds only lower case letters, digits and '_'");
            continue;
        }
        PackageCampaign c;
        c.id = package.id + ":" + t.name;
        c.package = package.id;
        std::vector<std::string> parts(kMaxCampaignParts);
        bool ok = true;
        const auto refuse = [&](const core::DataEntry& e, const std::string& what) {
            warnings.push_back(line_of(where, e.value.line) + "'" + e.key + "' " + what);
            ok = false;
        };
        for (const core::DataEntry& e : t.entries) {
            const std::string* text = e.value.string();
            if (e.key == "stars") {
                if (const auto n = number(e.value, 0, kMaxStars)) {
                    c.stars = static_cast<std::uint8_t>(*n);
                } else {
                    refuse(e, "is a number from 0 to " + std::to_string(kMaxStars));
                }
            } else if (e.key == "carry.money") {
                if (e.value.boolean() == nullptr) {
                    refuse(e, "is true or false");
                } else {
                    c.carry_money = *e.value.boolean();
                }
            } else if (e.key.starts_with("part.")) {
                const std::string_view digits = std::string_view(e.key).substr(5);
                const std::size_t n = digits.size() == 1 && digits[0] >= '1' && digits[0] <= '9'
                                          ? static_cast<std::size_t>(digits[0] - '0')
                                          : 0;
                std::string scenario = package.id;
                scenario += ':';
                scenario += text != nullptr ? *text : std::string();
                if (n == 0 || n > kMaxCampaignParts) {
                    refuse(e, "is no part: parts are numbered 1 to " + std::to_string(kMaxCampaignParts));
                } else if (text == nullptr || find_scenario(scenarios, scenario) == nullptr) {
                    refuse(e, "names no scenario of the package (scenarios.toml)");
                } else {
                    parts[n - 1] = std::move(scenario);
                }
            } else if (const std::string_view language = name_key_language(e.key); !language.empty()) {
                if (text == nullptr || text->empty()) {
                    refuse(e, "is a text");
                } else {
                    c.names.emplace_back(language, core::utf8_to_cp1252(*text));
                }
            } else if (const std::string_view b = text_key_language(e.key, "briefing"); !b.empty()) {
                if (text == nullptr) {
                    refuse(e, "is a text");
                } else {
                    c.briefings.emplace_back(b, core::utf8_to_cp1252(*text));
                }
            } else {
                refuse(e, "is no field of a campaign and is passed over");
            }
        }
        while (!parts.empty() && parts.back().empty()) {
            parts.pop_back();
        }
        if (!ok) {
            warnings.push_back(at + c.id + " is passed over");
            continue;
        }
        if (parts.empty()) {
            warnings.push_back(at + c.id + " has no parts ('part.1'); the campaign is passed over");
            continue;
        }
        if (std::find(parts.begin(), parts.end(), std::string()) != parts.end()) {
            warnings.push_back(at + c.id +
                               " misses a part between its first and its last; the campaign is passed over");
            continue;
        }
        c.parts = std::move(parts);
        campaigns.push_back(std::move(c));
    }
}

std::vector<PackageCampaign> load_package_campaigns(std::span<const ModPackage> packages,
                                                    std::span<const PackageScenario> scenarios,
                                                    std::vector<std::string>& warnings) {
    std::vector<PackageCampaign> campaigns;
    for (const ModPackage& p : packages) {
        if (const auto text = package_text(p, kCampaignsFile, warnings)) {
            read_campaigns_file(campaigns, p, scenarios, *text, warnings);
        }
    }
    return campaigns;
}

const PackageCampaign* find_campaign(std::span<const PackageCampaign> campaigns, std::string_view id) {
    const auto it =
        std::find_if(campaigns.begin(), campaigns.end(), [&](const PackageCampaign& c) { return c.id == id; });
    return it != campaigns.end() ? &*it : nullptr;
}

namespace {
constexpr std::uint32_t kCarryMagic = 0x43363130;  ///< "016C" as u32 little endian
constexpr std::uint8_t kCarryVersion = 1;
}  // namespace

std::filesystem::path campaign_carry_file(std::string_view campaign, std::size_t part) {
    std::string name(campaign);
    std::replace(name.begin(), name.end(), ':', '_');
    name += '.';
    name += std::to_string(part + 1);
    name += ".carry";
    return core::path_from_utf8(name);
}

std::vector<std::uint8_t> write_campaign_carry(const CampaignCarry& carry) {
    core::ByteWriter out;
    out.u32(kCarryMagic);
    out.u8(kCarryVersion);
    out.i64(carry.money);
    out.bytes(carry.table);
    return out.take();
}

std::optional<CampaignCarry> read_campaign_carry(std::span<const std::uint8_t> bytes) {
    core::ByteReader in(bytes);
    const auto magic = in.u32();
    const auto version = in.u8();
    const auto money = in.u64();
    if (magic != kCarryMagic || version != kCarryVersion || !money) {
        return std::nullopt;
    }
    CampaignCarry carry;
    carry.money = static_cast<std::int64_t>(*money);
    carry.table.assign(bytes.begin() + static_cast<std::ptrdiff_t>(in.offset()), bytes.end());
    return carry;
}

}  // namespace o1602::assets
