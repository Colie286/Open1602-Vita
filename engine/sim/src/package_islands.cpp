// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/sim/package_islands.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

#include "o1602/assets/chunk_file.hpp"
#include "o1602/assets/island.hpp"
#include "o1602/core/data_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/text.hpp"
#include "o1602/sim/island_templates.hpp"

namespace o1602::sim {

namespace {

std::string line_of(const std::string& where, std::size_t line) {
    return where + " line " + std::to_string(line) + ": ";
}

/// A name a package may give its island: lower case letters, digits and '_'.
bool is_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

/// Islands of templates are at most this wide and high (the original's tile records).
constexpr std::uint32_t kMaxTemplateSide = 255;

}  // namespace

std::string PackageIsland::name(std::string_view language) const {
    return assets::name_in(names, language, id);
}

std::optional<std::uint8_t> size_class_of(std::uint32_t width, std::uint32_t height) noexcept {
    for (std::size_t c = 0; c < templates::kSizeClasses; ++c) {
        if (width <= templates::kClassWidth[c] && height <= templates::kClassHeight[c]) {
            return static_cast<std::uint8_t>(c);
        }
    }
    return std::nullopt;
}

void apply_islands_file(std::vector<PackageIsland>& islands, const assets::ModPackage& package, std::string_view utf8,
                        std::vector<std::string>& warnings) {
    const std::string where = package.id + "/" + std::string(kIslandsFile);
    auto tables = core::parse_data_file(utf8);
    if (!tables) {
        warnings.push_back(where + " " + tables.error().message);
        return;
    }
    for (const core::DataTable& t : *tables) {
        const std::string at = line_of(where, t.line);
        if (!is_name(t.name)) {
            warnings.push_back(at + "[" + t.name + "]: an island's name holds only lower case letters, digits and '_'");
            continue;
        }
        if (islands.size() >= kMaxPackageIslands) {
            warnings.push_back(at + "[" + t.name + "]: the packages have more than " +
                               std::to_string(kMaxPackageIslands) + " islands; it is passed over");
            continue;
        }
        PackageIsland island;
        island.id = package.id + ":" + t.name;
        island.package = package.id;
        std::optional<std::uint8_t> climate;
        bool ok = true;
        const auto refuse = [&](const core::DataEntry& e, const std::string& what) {
            warnings.push_back(line_of(where, e.value.line) + "'" + e.key + "' " + what);
            ok = false;
        };
        for (const core::DataEntry& e : t.entries) {
            const std::string* text = e.value.string();
            if (e.key == "file") {
                std::error_code ec;
                const std::filesystem::path path = text != nullptr && assets::safe_package_path(*text)
                                                       ? package.folder / core::path_from_utf8(*text)
                                                       : std::filesystem::path{};
                if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
                    refuse(e, "names no island file of the package (a .scp file in its folder)");
                } else {
                    island.file = path;
                }
            } else if (e.key == "climate") {
                if (text != nullptr && *text == "north") {
                    climate = climate::kNorth;
                } else if (text != nullptr && *text == "south") {
                    climate = climate::kSouth;
                } else if (const std::size_t colon = text != nullptr ? text->find(':') : std::string::npos;
                           colon != std::string::npos && colon > 0 && colon + 1 < text->size()) {
                    island.own_climate = *text;  // a package's climate (climates.toml)
                } else {
                    refuse(e, "is \"north\", \"south\" or a package's climate \"<package>:<name>\"");
                }
            } else if (const std::string_view language = assets::name_key_language(e.key); !language.empty()) {
                if (text != nullptr) {
                    island.names.emplace_back(std::string(language), core::utf8_to_cp1252(*text));
                } else {
                    refuse(e, "is a text");
                }
            } else {
                refuse(e, "is no field of an island and is passed over");
            }
        }
        if (island.file.empty()) {
            warnings.push_back(at + island.id + " has no island file ('file'); it is passed over");
            continue;
        }
        if (!ok) {
            warnings.push_back(at + island.id + " is passed over");
            continue;
        }
        auto file = assets::ChunkFile::load(island.file);
        auto loaded = file ? assets::load_islands(*file) : core::Result<std::vector<assets::Island>>(file.error());
        if (!loaded || loaded->empty() || loaded->front().layers.empty()) {
            warnings.push_back(at + island.id + ": its file holds no island" +
                               (loaded ? std::string{} : " (" + loaded.error().message + ")"));
            continue;
        }
        const assets::IslandHeader& header = loaded->front().header;
        const auto size = size_class_of(header.width, header.height);
        if (header.width > kMaxTemplateSide || header.height > kMaxTemplateSide || !size) {
            warnings.push_back(at + island.id + " is " + std::to_string(header.width) + " x " +
                               std::to_string(header.height) + " tiles, larger than the largest templates (" +
                               std::to_string(templates::kClassWidth.back()) + " x " +
                               std::to_string(templates::kClassHeight.back()) + "); it is passed over");
            continue;
        }
        island.climate = climate.value_or(static_cast<std::uint8_t>(header.climate & 1U));
        island.size = *size;
        island.width = header.width;
        island.height = header.height;
        islands.push_back(std::move(island));
    }
}

std::vector<PackageIsland> load_package_islands(std::span<const assets::ModPackage> packages,
                                                std::vector<std::string>& warnings) {
    std::vector<PackageIsland> islands;
    for (const assets::ModPackage& package : packages) {
        const std::filesystem::path path = package.folder / kIslandsFile;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            continue;
        }
        const auto bytes = core::read_file(path);
        if (!bytes) {
            warnings.push_back(package.id + "/" + std::string(kIslandsFile) + ": " + bytes.error().message);
            continue;
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        apply_islands_file(islands, package, core::skip_utf8_bom(text), warnings);
    }
    return islands;
}

std::uint16_t package_template_number(std::span<const PackageIsland> islands, std::size_t index) noexcept {
    const PackageIsland& island = islands[index];
    std::uint16_t number = templates::kTemplateCount[island.climate & 1U][island.size];
    for (std::size_t i = 0; i < index; ++i) {
        if (islands[i].climate == island.climate && islands[i].size == island.size) {
            ++number;
        }
    }
    return number;
}

const PackageIsland* package_template(std::span<const PackageIsland> islands, std::uint8_t climate, std::uint8_t size,
                                      std::uint16_t number) noexcept {
    if (climate >= climate::kCount || size >= templates::kSizeClasses ||
        number < templates::kTemplateCount[climate][size]) {
        return nullptr;
    }
    std::uint16_t n = templates::kTemplateCount[climate][size];
    for (const PackageIsland& island : islands) {
        if (island.climate == climate && island.size == size) {
            if (n == number) {
                return &island;
            }
            ++n;
        }
    }
    return nullptr;
}

}  // namespace o1602::sim
